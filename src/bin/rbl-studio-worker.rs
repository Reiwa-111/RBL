//! Persistent process supervisor for the wxWidgets desktop frontend.
//!
//! Standard input/output are a bounded NDJSON protocol. Child process output
//! is forwarded as events; diagnostics and internal logs stay on stderr.

#[path = "../process.rs"]
mod process;
#[path = "../settings.rs"]
#[allow(dead_code)]
mod settings;

use crossbeam_channel::{bounded, never, select, tick, Receiver, Sender, TrySendError};
use process::{ProcessCommand, ProcessEvent, SourceDocument, TaskKind};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{
    io::{self, BufRead, BufReader, Read, Write},
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

// The reader, one queued frame and the coordinator's current frame can
// coexist. At 36 MiB each, raw protocol buffers stay near 108 MiB before
// decoded JSON/snapshot allocations. The 16 MiB UTF-8 source snapshot remains
// below this frame ceiling even after JSON escaping.
const MAX_FRAME_BYTES: u64 = 36 * 1024 * 1024;
const INPUT_LINE_QUEUE_CAPACITY: usize = 32;
// One queued request prevents bursts of large snapshots from multiplying the
// per-frame memory bound. The reader applies backpressure until the coordinator
// accepts the request.
const INPUT_QUEUE_CAPACITY: usize = 1;
const EVENT_QUEUE_CAPACITY: usize = 256;
const MAX_PENDING_OUTPUT_EVENTS: usize = 256;
// Keep a few slots for protocol responses (stdin acknowledgements/errors and
// shutdown-related messages) while process events are backpressured.
const CONTROL_OUTPUT_RESERVE: usize = 8;
const MAX_OUTPUT_BACKPRESSURE: Duration = Duration::from_secs(2);

#[derive(Debug, Deserialize)]
struct Request {
    command: String,
    #[serde(default)]
    request_id: Value,
    #[serde(default)]
    root: Option<PathBuf>,
    #[serde(default)]
    workspace_root: Option<PathBuf>,
    #[serde(default)]
    task: Option<String>,
    #[serde(default)]
    source_path: Option<PathBuf>,
    #[serde(default)]
    snapshot_file: Option<PathBuf>,
    #[serde(default)]
    is_rbl: bool,
    #[serde(default)]
    snapshot: Option<String>,
    #[serde(default)]
    line: Option<String>,
}

struct ActiveTask {
    request_id: Value,
    input: Sender<ProcessCommand>,
    cancel: Arc<AtomicBool>,
    join: Option<JoinHandle<()>>,
    snapshot_file: Option<PathBuf>,
    stdin_closed: bool,
}

fn valid_request_id(value: &Value) -> bool {
    match value {
        Value::Null | Value::Bool(_) | Value::Number(_) => true,
        Value::String(value) => value.len() <= 64,
        Value::Array(_) | Value::Object(_) => false,
    }
}

fn emit(output: &mut impl Write, value: Value) -> io::Result<()> {
    serde_json::to_writer(&mut *output, &value)?;
    output.write_all(b"\n")
}

fn output_writer(rx: Receiver<Value>, failed: Sender<()>, done: Sender<()>) {
    let stdout = io::stdout();
    // Output events are already bounded by the coordinator. Flushing every
    // 8 KiB NDJSON frame makes a 20 MiB program output depend on hundreds of
    // UI polling turns. Buffer up to 128 frames (1 MiB), flush control events
    // immediately, and flush a short burst after 25 ms so interactive prompts
    // remain responsive even when no further output is produced.
    const OUTPUT_BATCH_FRAMES: usize = 128;
    let mut output = io::BufWriter::with_capacity(1024 * 1024, stdout.lock());
    let mut pending_output_frames = 0usize;
    loop {
        match rx.recv_timeout(Duration::from_millis(25)) {
            Ok(value) => {
                let event = value.get("event").and_then(Value::as_str);
                let is_bye = event == Some("bye");
                let is_output = event == Some("output");
                if let Err(error) = emit(&mut output, value) {
                    eprintln!("worker stdout write failed: {error}");
                    let _ = failed.try_send(());
                    break;
                }
                if is_output {
                    pending_output_frames += 1;
                } else {
                    pending_output_frames = 0;
                }
                if !is_output || pending_output_frames >= OUTPUT_BATCH_FRAMES || is_bye {
                    if let Err(error) = output.flush() {
                        eprintln!("worker stdout flush failed: {error}");
                        let _ = failed.try_send(());
                        break;
                    }
                    pending_output_frames = 0;
                }
                if is_bye {
                    let _ = done.try_send(());
                    break;
                }
            }
            Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
                if pending_output_frames > 0 {
                    if let Err(error) = output.flush() {
                        eprintln!("worker stdout flush failed: {error}");
                        let _ = failed.try_send(());
                        break;
                    }
                    pending_output_frames = 0;
                }
            }
            Err(crossbeam_channel::RecvTimeoutError::Disconnected) => {
                if let Err(error) = output.flush() {
                    eprintln!("worker stdout flush failed: {error}");
                    let _ = failed.try_send(());
                }
                break;
            }
        }
    }
}

fn input_reader(tx: Sender<Vec<u8>>) {
    let stdin = io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    loop {
        let mut frame = Vec::new();
        let read = match reader
            .by_ref()
            .take(MAX_FRAME_BYTES + 1)
            .read_until(b'\n', &mut frame)
        {
            Ok(read) => read,
            Err(error) => {
                eprintln!("worker stdin read failed: {error}");
                break;
            }
        };
        if read == 0 {
            break;
        }
        if frame.len() as u64 > MAX_FRAME_BYTES {
            eprintln!("worker rejected an NDJSON frame larger than {MAX_FRAME_BYTES} bytes");
            break;
        }
        if tx.send(frame).is_err() {
            break;
        }
    }
}

fn parse_task(value: &str) -> Option<TaskKind> {
    match value.to_ascii_lowercase().as_str() {
        "run" => Some(TaskKind::Run),
        "build" => Some(TaskKind::Build),
        "check" => Some(TaskKind::Check),
        "assembly" | "asm" => Some(TaskKind::Assembly),
        "tests" | "test" => Some(TaskKind::Tests),
        "benchmarks" | "benchmark" => Some(TaskKind::Benchmarks),
        "doctor" => Some(TaskKind::Doctor),
        _ => None,
    }
}

fn task_roots<'a, 'b>(
    application_root: Option<&'a Path>,
    request: &'b Request,
) -> Result<(&'a Path, &'b Path), &'static str> {
    let application_root = application_root.ok_or("worker is not initialized")?;
    let workspace_root = request
        .workspace_root
        .as_deref()
        .ok_or("start requires workspace_root")?;
    Ok((application_root, workspace_root))
}

fn event_json(request_id: &Value, event: ProcessEvent) -> Value {
    match event {
        ProcessEvent::Started(label) => json!({
            "event": "started", "request_id": request_id, "label": label
        }),
        ProcessEvent::SnapshotCreated(temporary, original) => json!({
            "event": "snapshot_created", "request_id": request_id,
            "temporary_path": temporary.to_string_lossy(),
            "original_path": original.map(|path| path.to_string_lossy().into_owned())
        }),
        ProcessEvent::Output(stderr, text) => json!({
            "event": "output", "request_id": request_id,
            "stderr": stderr, "text": text
        }),
        ProcessEvent::Finished(exit_code) => json!({
            "event": "finished", "request_id": request_id, "exit_code": exit_code
        }),
    }
}

fn output_queue_has_control_reserve(pending_output_len: usize) -> bool {
    pending_output_len < MAX_PENDING_OUTPUT_EVENTS - CONTROL_OUTPUT_RESERVE
}

fn ready_request_receiver(
    request_rx: &Receiver<Vec<u8>>,
    pending_output_len: usize,
) -> Receiver<Vec<u8>> {
    if output_queue_has_control_reserve(pending_output_len) {
        request_rx.clone()
    } else {
        never()
    }
}

fn ready_event_receiver(
    event_rx: &Receiver<(Value, ProcessEvent)>,
    pending_output_len: usize,
) -> Receiver<(Value, ProcessEvent)> {
    if output_queue_has_control_reserve(pending_output_len) {
        event_rx.clone()
    } else {
        never()
    }
}

fn is_output_event(value: &Value) -> bool {
    value.get("event").and_then(Value::as_str) == Some("output")
}

fn start_task(
    request: Request,
    application_root: &Path,
    workspace_root: &Path,
    settings: &settings::Settings,
    event_tx: &Sender<(Value, ProcessEvent)>,
) -> Result<ActiveTask, String> {
    let task_name = request.task.as_deref().ok_or("start requires a task")?;
    let kind = parse_task(task_name).ok_or_else(|| format!("unknown task: {task_name}"))?;
    if request
        .snapshot
        .as_ref()
        .is_some_and(|text| text.len() > 16 * 1024 * 1024)
    {
        return Err("source snapshot exceeds the 16 MiB editor limit".into());
    }
    let (input_tx, input_rx) = bounded(32);
    let cancel = Arc::new(AtomicBool::new(false));
    let task_events = event_tx.clone();
    let request_id = request.request_id.clone();
    let snapshot_file = request.snapshot_file.clone();
    let source_path = snapshot_file
        .clone()
        .or_else(|| request.source_path.clone());
    if let Some(path) = snapshot_file.as_ref() {
        let _ = event_tx.send((
            request_id.clone(),
            ProcessEvent::SnapshotCreated(path.clone(), request.source_path.clone()),
        ));
    }
    let (process_tx, process_rx) = bounded(EVENT_QUEUE_CAPACITY);
    let forward_id = request_id.clone();
    let forward_events = task_events.clone();
    let forwarder = thread::spawn(move || {
        while let Ok(event) = process_rx.recv() {
            if forward_events.send((forward_id.clone(), event)).is_err() {
                break;
            }
        }
    });
    let join = process::start_with_roots(process::ProcessStartOptions {
        application_root: application_root.to_path_buf(),
        workspace_root: workspace_root.to_path_buf(),
        settings: settings.clone(),
        kind,
        source_document: SourceDocument {
            path: source_path,
            is_rbl: request.is_rbl,
            snapshot: request.snapshot,
        },
        tx: process_tx,
        input_rx,
        cancel: cancel.clone(),
    });
    // Keep the forwarding thread attached to the process lifetime. The
    // supervisor always emits Finished, including synchronous validation
    // failures, so the coordinator can safely clear the active task there.
    let join = Some(thread::spawn(move || {
        if let Some(join) = join {
            let _ = join.join();
        }
        let _ = forwarder.join();
    }));
    Ok(ActiveTask {
        request_id,
        input: input_tx,
        cancel,
        join,
        snapshot_file,
        stdin_closed: false,
    })
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let (request_tx, request_rx) = bounded::<Vec<u8>>(INPUT_QUEUE_CAPACITY);
    let mut request_rx = request_rx;
    let (event_tx, event_rx) = bounded::<(Value, ProcessEvent)>(EVENT_QUEUE_CAPACITY);
    let (output_tx, output_rx) = bounded::<Value>(EVENT_QUEUE_CAPACITY);
    let (output_failed_tx, output_failed_rx) = bounded::<()>(1);
    let (output_done_tx, output_done_rx) = bounded::<()>(1);
    let output_join =
        thread::spawn(move || output_writer(output_rx, output_failed_tx, output_done_tx));
    thread::spawn(move || input_reader(request_tx));

    let mut output_tx = Some(output_tx);
    let mut output_broken = false;
    let mut output_backpressure_degraded = false;
    let mut pending_output = std::collections::VecDeque::<Value>::new();
    let mut pending_since: Option<Instant> = None;
    let output_tick = tick(Duration::from_millis(100));
    let mut application_root: Option<PathBuf> = None;
    let mut active: Option<ActiveTask> = None;
    let settings_value = settings::load();
    let mut shutting_down = false;

    macro_rules! queue_output {
        ($value:expr) => {{
            if !output_broken {
                let value = $value;
                let mut failed = false;
                if pending_output.is_empty() {
                    match output_tx.as_ref().map(|sender| sender.try_send(value)) {
                        Some(Ok(())) => {}
                        Some(Err(TrySendError::Full(value))) => {
                            if !(output_backpressure_degraded && is_output_event(&value)) {
                                pending_output.push_back(value);
                                pending_since = Some(Instant::now());
                            }
                        }
                        _ => failed = true,
                    }
                } else if output_backpressure_degraded && is_output_event(&value) {
                    // Once a consumer has exceeded the bounded backpressure
                    // window, stop retaining bulk output. Control frames remain
                    // ordered and bounded in pending_output.
                } else if pending_output.len() < MAX_PENDING_OUTPUT_EVENTS {
                    pending_output.push_back(value);
                } else if output_backpressure_degraded && is_output_event(&value) {
                    // The queue is full of control events; drop only output.
                } else if output_backpressure_degraded {
                    if let Some(index) = pending_output.iter().position(is_output_event) {
                        pending_output.remove(index);
                        pending_output.push_back(value);
                    } else {
                        failed = true;
                    }
                } else {
                    failed = true;
                }
                if failed {
                    eprintln!(
                        "worker output queue failed; pending events={}, shutting down",
                        pending_output.len()
                    );
                    output_broken = true;
                    output_tx.take();
                    pending_output.clear();
                    shutting_down = true;
                    if let Some(task) = active.as_ref() {
                        task.cancel.store(true, Ordering::Relaxed);
                    }
                }
            }
        }};
    }

    queue_output!(json!({ "event": "worker_started", "protocol": 1 }));

    loop {
        // Do not drain process events when the pending stdout queue reaches its
        // high-water mark. This propagates bounded backpressure through the
        // forwarding channel and stream readers instead of treating a burst as
        // a broken stdout pipe and discarding already accepted output.
        // Stop accepting new protocol requests at the same high-water mark as
        // process events. The bounded input reader then backpressures stdin,
        // preserving CONTROL_OUTPUT_RESERVE for responses to already accepted
        // requests and terminal process events instead of consuming those slots
        // with an unbounded stream of new control responses.
        let request_rx_ready = ready_request_receiver(&request_rx, pending_output.len());
        let event_rx_ready = ready_event_receiver(&event_rx, pending_output.len());
        select! {
            recv(request_rx_ready) -> frame => {
                let Ok(frame) = frame else {
                    request_rx = never();
                    if let Some(task) = active.as_ref() {
                        task.cancel.store(true, Ordering::Relaxed);
                    }
                    shutting_down = true;
                    if active.is_none() && pending_output.is_empty() { break; }
                    continue;
                };
                let request: Request = match serde_json::from_slice(&frame) {
                    Ok(request) => request,
                    Err(error) => {
                        queue_output!(json!({ "event": "protocol_error", "message": error.to_string() }));
                        continue;
                    }
                };
        if !valid_request_id(&request.request_id) {
                    queue_output!(json!({ "event": "protocol_error", "message": "request_id must be a scalar of at most 64 bytes" }));
                    continue;
                }
                if shutting_down {
                    continue;
                }
                match request.command.as_str() {
                    "initialize" => {
                        if active.is_some() {
                            queue_output!(json!({ "event": "error", "message": "cannot reinitialize while a task is active" }));
                            continue;
                        }
                        let Some(root) = request.root else {
                            queue_output!(json!({ "event": "error", "message": "initialize requires root" }));
                            continue;
                        };
                        application_root = Some(root);
                        queue_output!(json!({ "event": "ready", "protocol": 1 }));
                    }
                    "start" => {
                        let (app_root, workspace_root) = match task_roots(application_root.as_deref(), &request) {
                            Ok(roots) => roots,
                            Err(message) => {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": message }));
                                continue;
                            }
                        };
                        let workspace_root = workspace_root.to_path_buf();
                        if active.is_some() {
                            queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "another task is already active" }));
                            continue;
                        }
                        let request_id = request.request_id.clone();
                        match start_task(request, app_root, &workspace_root, &settings_value, &event_tx) {
                            Ok(task) => active = Some(task),
                            Err(message) => queue_output!(json!({ "event": "error", "request_id": request_id, "message": message })),
                        }
                    }
                    "stdin_line" => {
                        if let Some(task) = active.as_mut() {
                            if request.request_id != task.request_id {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "request_id does not match the active task" }));
                                continue;
                            }
                            if task.stdin_closed {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "child stdin is already closed" }));
                                continue;
                            }
                            if let Some(line) = request.line {
                                if line.len() > 16 * 1024 {
                                    queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "stdin line exceeds 16 KiB" }));
                                } else if task.input.len() >= INPUT_LINE_QUEUE_CAPACITY {
                                    queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "child stdin queue is full" }));
                                } else if let Err(error) = task.input.try_send(ProcessCommand::StdinLine(line)) {
                                    let message = match error {
                                        TrySendError::Full(_) => "child stdin queue is full",
                                        TrySendError::Disconnected(_) => "child stdin is closed",
                                    };
                                    queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": message }));
                                } else {
                                    queue_output!(json!({ "event": "stdin_accepted", "request_id": request.request_id }));
                                }
                            } else {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "stdin_line requires a line field" }));
                            }
                        } else {
                            queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "no active task accepts stdin" }));
                        }
                    }
                    "stdin_eof" => {
                        if let Some(task) = active.as_mut() {
                            if request.request_id != task.request_id {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "request_id does not match the active task" }));
                                continue;
                            }
                            if task.stdin_closed { continue; }
                            if let Err(error) = task.input.try_send(ProcessCommand::CloseStdin) {
                                let message = match error {
                                    TrySendError::Full(_) => "child stdin queue is full",
                                    TrySendError::Disconnected(_) => "child stdin is closed",
                                };
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": message }));
                            } else {
                                task.stdin_closed = true;
                                queue_output!(json!({ "event": "stdin_eof_accepted", "request_id": request.request_id }));
                            }
                        } else {
                            queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "no active task accepts stdin EOF" }));
                        }
                    }
                    "cancel" => {
                        if let Some(task) = active.as_ref() {
                            if request.request_id != task.request_id {
                                queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "request_id does not match the active task" }));
                            } else {
                                task.cancel.store(true, Ordering::Relaxed);
                            }
                        } else {
                            queue_output!(json!({ "event": "error", "request_id": request.request_id, "message": "no active task to cancel" }));
                        }
                    }
                    "shutdown" => {
                        shutting_down = true;
                        request_rx = never();
                        if let Some(task) = active.as_ref() { task.cancel.store(true, Ordering::Relaxed); }
                        else if pending_output.is_empty() { break; }
                    }
                    other => queue_output!(json!({ "event": "error", "message": format!("unknown command: {other}") })),
                }
            }
            recv(event_rx_ready) -> event => {
                if let Ok((request_id, event)) = event {
                    let finished = matches!(&event, ProcessEvent::Finished(_));
                    if finished {
                        if let Some(mut task) = active.take() {
                            if let Some(join) = task.join.take() { let _ = join.join(); }
                            if let Some(path) = task.snapshot_file.take() {
                                if let Err(error) = std::fs::remove_file(&path) {
                                    if error.kind() != std::io::ErrorKind::NotFound {
                                        eprintln!("could not remove editor snapshot {}: {error}", path.display());
                                    }
                                }
                            }
                        }
                    }
                    queue_output!(event_json(&request_id, event));
                    // Keep draining accepted events during shutdown. The `bye`
                    // frame is queued only after pending_output reaches the
                    // writer channel, preserving `finished`-before-`bye` order.
                }
            }
            recv(output_failed_rx) -> failed => {
                if failed.is_ok() {
                    eprintln!("worker stdout writer failed; shutting down supervisor");
                    output_broken = true;
                    output_tx.take();
                    pending_output.clear();
                    shutting_down = true;
                    request_rx = never();
                    if let Some(task) = active.as_ref() {
                        task.cancel.store(true, Ordering::Relaxed);
                    }
                }
            }
            recv(output_tick) -> _ => {
                if !output_broken {
                    let mut failed = false;
                    let mut failure_reason = "stdout channel disconnected";
                    if let Some(sender) = output_tx.as_ref() {
                        while let Some(value) = pending_output.front() {
                            match sender.try_send(value.clone()) {
                                Ok(()) => { pending_output.pop_front(); }
                                Err(TrySendError::Full(_)) => break,
                                Err(TrySendError::Disconnected(_)) => {
                                    failed = true;
                                    failure_reason = "stdout channel disconnected";
                                    break;
                                }
                            }
                        }
                    }
                    if pending_output.is_empty() {
                        pending_since = None;
                    } else if pending_since.is_some_and(|started| started.elapsed() >= MAX_OUTPUT_BACKPRESSURE) {
                        // Do not tear down the protocol merely because a GUI
                        // pauses its stdout polling. Drop bounded bulk output
                        // after the deadline, but retain Started/Snapshot/
                        // Finished/bye control events. If the parent resumes
                        // draining, terminal events can still be delivered in
                        // order. A parent that never drains must use its own
                        // bounded process-termination fallback.
                        output_backpressure_degraded = true;
                        pending_output.retain(|value| !is_output_event(value));
                        pending_since = None;
                    }
                    if failed {
                        eprintln!(
                            "worker output delivery failed: {failure_reason}; pending events={}",
                            pending_output.len()
                        );
                        output_broken = true;
                        output_tx.take();
                        pending_output.clear();
                        shutting_down = true;
                        if let Some(task) = active.as_ref() {
                            task.cancel.store(true, Ordering::Relaxed);
                        }
                    }
                }
            }
        }
        if shutting_down && active.is_none() && (pending_output.is_empty() || output_broken) {
            break;
        }
    }

    if let Some(mut task) = active.take() {
        task.cancel.store(true, Ordering::Relaxed);
        drop(task.input);
        if let Some(join) = task.join.take() {
            let _ = join.join();
        }
    }
    if !output_broken {
        if let Some(sender) = output_tx.take() {
            let queued = sender
                .send_timeout(json!({ "event": "bye" }), Duration::from_millis(100))
                .is_ok();
            drop(sender);
            if queued {
                // Wait briefly for the final protocol frame, but never let a
                // blocked stdout pipe hold shutdown indefinitely.
                let _ = output_done_rx.recv_timeout(Duration::from_millis(250));
            }
        }
        // Do not join a thread that may be blocked writing to a pipe whose
        // reader has stopped consuming. Dropping the handle detaches it; once
        // run() returns, process exit closes stdout and releases that thread.
        drop(output_join);
    } else {
        drop(output_join);
    }
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("RBL Studio worker failed: {error:#}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod worker_tests {
    use super::*;

    #[test]
    fn initialize_root_and_per_task_workspace_are_independent() {
        let initialize: Request = serde_json::from_value(json!({
            "command": "initialize",
            "root": "/opt/rbl-studio"
        }))
        .unwrap();
        let application_root = initialize.root.unwrap();
        let first: Request = serde_json::from_value(json!({
            "command": "start",
            "task": "check",
            "workspace_root": "/work/project-a"
        }))
        .unwrap();
        let second: Request = serde_json::from_value(json!({
            "command": "start",
            "task": "tests",
            "workspace_root": "/work/project-b"
        }))
        .unwrap();

        assert_eq!(application_root, PathBuf::from("/opt/rbl-studio"));
        assert_eq!(
            task_roots(Some(&application_root), &first).unwrap(),
            (Path::new("/opt/rbl-studio"), Path::new("/work/project-a"))
        );
        assert_eq!(
            task_roots(Some(&application_root), &second).unwrap(),
            (Path::new("/opt/rbl-studio"), Path::new("/work/project-b"))
        );
        assert!(first.root.is_none());
        assert!(second.root.is_none());
        assert!(task_roots(None, &first).is_err());
    }

    #[test]
    fn start_request_requires_workspace_root() {
        let request: Request = serde_json::from_value(json!({
            "command": "start",
            "task": "check"
        }))
        .unwrap();
        assert!(task_roots(Some(Path::new("/opt/rbl-studio")), &request).is_err());
    }

    #[test]
    fn protocol_requests_pause_at_output_high_water_mark_to_preserve_control_reserve() {
        let (sender, receiver) = bounded(1);
        sender.send(vec![1]).unwrap();

        let high_water = MAX_PENDING_OUTPUT_EVENTS - CONTROL_OUTPUT_RESERVE;
        let paused = ready_request_receiver(&receiver, high_water);
        assert!(paused.try_recv().is_err());
        assert_eq!(
            receiver.len(),
            1,
            "the coordinator must leave protocol requests queued while output is backpressured"
        );

        let resumed = ready_request_receiver(&receiver, high_water - 1);
        assert_eq!(resumed.try_recv().unwrap(), vec![1]);
        assert!(receiver.is_empty());
    }

    #[test]
    fn process_events_pause_at_output_high_water_mark() {
        let (sender, receiver) = bounded(1);
        sender
            .send((json!(1), ProcessEvent::Output(false, "queued".into())))
            .unwrap();

        let high_water = MAX_PENDING_OUTPUT_EVENTS - CONTROL_OUTPUT_RESERVE;
        let paused = ready_event_receiver(&receiver, high_water);
        assert!(paused.try_recv().is_err());
        assert_eq!(
            receiver.len(),
            1,
            "the coordinator must leave the event queued"
        );

        let resumed = ready_event_receiver(&receiver, high_water - 1);
        match resumed.try_recv().unwrap().1 {
            ProcessEvent::Output(false, text) => assert_eq!(text, "queued"),
            other => panic!("unexpected process event: {other:?}"),
        }
        assert!(receiver.is_empty());
    }
}
