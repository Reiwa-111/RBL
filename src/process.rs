use crate::settings::Settings;
use crossbeam_channel::{Receiver, Sender};
use std::{
    io::Write,
    path::{Path, PathBuf},
    process::{Child, ChildStdin, Command, ExitStatus, Stdio},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::Duration,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskKind {
    Run,
    Build,
    Check,
    Assembly,
    Tests,
    Benchmarks,
    Doctor,
}

impl TaskKind {
    pub fn label(self) -> &'static str {
        match self {
            Self::Run => "Run",
            Self::Build => "Build",
            Self::Check => "Check",
            Self::Assembly => "Assembly",
            Self::Tests => "Tests",
            Self::Benchmarks => "Benchmarks",
            Self::Doctor => "Toolchain doctor",
        }
    }
}

#[derive(Debug)]
pub enum ProcessEvent {
    Started(String),
    SnapshotCreated(PathBuf, Option<PathBuf>),
    Output(bool, String),
    Finished(i32),
}

#[derive(Debug)]
pub enum ProcessCommand {
    StdinLine(String),
    CloseStdin,
}

pub struct SourceDocument {
    pub path: Option<PathBuf>,
    pub is_rbl: bool,
    pub snapshot: Option<String>,
}

pub struct ProcessStartOptions {
    pub application_root: PathBuf,
    pub workspace_root: PathBuf,
    pub settings: Settings,
    pub kind: TaskKind,
    pub source_document: SourceDocument,
    pub tx: Sender<ProcessEvent>,
    pub input_rx: Receiver<ProcessCommand>,
    pub cancel: Arc<AtomicBool>,
}

#[allow(dead_code)] // The persistent wx worker uses start_with_roots; egui keeps this legacy entry point.
pub fn start(
    root: PathBuf,
    settings: Settings,
    kind: TaskKind,
    source_document: SourceDocument,
    tx: Sender<ProcessEvent>,
    input_rx: Receiver<ProcessCommand>,
    cancel: Arc<AtomicBool>,
) -> Option<JoinHandle<()>> {
    start_with_roots(ProcessStartOptions {
        application_root: root.clone(),
        workspace_root: root,
        settings,
        kind,
        source_document,
        tx,
        input_rx,
        cancel,
    })
}

pub fn start_with_roots(options: ProcessStartOptions) -> Option<JoinHandle<()>> {
    let ProcessStartOptions {
        application_root,
        workspace_root,
        settings,
        kind,
        source_document,
        tx,
        input_rx,
        cancel,
    } = options;
    let SourceDocument {
        path: source,
        is_rbl: source_is_rbl,
        snapshot,
    } = source_document;
    if matches!(
        kind,
        TaskKind::Run | TaskKind::Build | TaskKind::Check | TaskKind::Assembly
    ) && (!source_is_rbl || source.as_ref().is_some_and(|path| !is_rbl_source(path)))
    {
        let _ = tx.send(ProcessEvent::Output(
            true,
            "Compiler actions accept only .rbl source files.\n".into(),
        ));
        let _ = tx.send(ProcessEvent::Finished(1));
        return None;
    }
    Some(thread::spawn(move || {
        let mut temporary = None;
        let actual_source = if let Some(snapshot_text) = snapshot {
            let dir = snapshot_directory(&workspace_root, source.as_deref());
            let _ = std::fs::create_dir_all(&dir);
            match tempfile::Builder::new()
                .prefix("source-")
                .suffix(".rbl")
                .tempfile_in(&dir)
            {
                Ok(mut file) => {
                    use std::io::Write;
                    if let Err(error) = file.write_all(snapshot_text.as_bytes()) {
                        let _ = tx.send(ProcessEvent::Output(
                            true,
                            format!("Cannot create check snapshot: {error}\n"),
                        ));
                        let _ = tx.send(ProcessEvent::Finished(1));
                        return;
                    }
                    // The editor retains its own text buffer. Release the task's
                    // duplicate as soon as the temporary source file is complete.
                    drop(snapshot_text);
                    let path = file.path().to_path_buf();
                    let _ = tx.send(ProcessEvent::SnapshotCreated(path.clone(), source.clone()));
                    temporary = Some(file);
                    Some(path)
                }
                Err(error) => {
                    let _ = tx.send(ProcessEvent::Output(
                        true,
                        format!("Cannot create check snapshot: {error}\n"),
                    ));
                    let _ = tx.send(ProcessEvent::Finished(1));
                    return;
                }
            }
        } else {
            source
        };

        let mut command = match build_command_with_roots(
            &application_root,
            &workspace_root,
            &settings,
            kind,
            actual_source.as_deref(),
        ) {
            Ok(command) => command,
            Err(error) => {
                let _ = tx.send(ProcessEvent::Output(true, format!("{error:#}\n")));
                let _ = tx.send(ProcessEvent::Finished(1));
                return;
            }
        };
        command
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .stdin(if kind == TaskKind::Run {
                Stdio::piped()
            } else {
                Stdio::null()
            });
        configure_process_group(&mut command);
        command.env("RBL_TOOLCHAIN", &settings.wsl_mode);
        command.env("RBL_WSL_DISTRO", &settings.wsl_distro);
        if matches!(
            kind,
            TaskKind::Run | TaskKind::Build | TaskKind::Check | TaskKind::Assembly
        ) {
            command.env("RBL_STREAM_OUTPUT", "1");
        }
        #[cfg(target_os = "windows")]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000 | 0x00000004); // CREATE_NO_WINDOW | CREATE_SUSPENDED
        }

        #[cfg(windows)]
        let process_job = match WindowsProcessJob::new() {
            Ok(job) => job,
            Err(error) => {
                let _ = tx.send(ProcessEvent::Output(
                    true,
                    format!("Cannot create child-process job: {error}\n"),
                ));
                let _ = tx.send(ProcessEvent::Finished(1));
                return;
            }
        };

        let display = kind.label().to_owned();
        let mut child = match command.spawn() {
            Ok(child) => child,
            Err(error) => {
                let _ = tx.send(ProcessEvent::Output(
                    true,
                    format!("Cannot start {display}: {error}\n"),
                ));
                let _ = tx.send(ProcessEvent::Finished(1));
                return;
            }
        };
        #[cfg(windows)]
        if let Err(error) = process_job.assign(&child) {
            let _ = child.kill();
            let _ = child.wait();
            let _ = tx.send(ProcessEvent::Output(
                true,
                format!("Cannot assign child process to its job: {error}\n"),
            ));
            let _ = tx.send(ProcessEvent::Finished(1));
            return;
        }
        #[cfg(windows)]
        if let Err(error) = resume_suspended_process(child.id()) {
            process_job.terminate();
            let _ = child.wait();
            let _ = tx.send(ProcessEvent::Output(
                true,
                format!("Cannot resume child process after job assignment: {error}\n"),
            ));
            let _ = tx.send(ProcessEvent::Finished(1));
            return;
        }
        let _ = tx.send(ProcessEvent::Started(display));
        let stdin_stop = Arc::new(AtomicBool::new(false));
        let stdin_writer = child.stdin.take().map(|stdin| {
            let writer_tx = tx.clone();
            let writer_cancel = cancel.clone();
            let writer_stop = stdin_stop.clone();
            thread::spawn(move || {
                write_stdin(stdin, input_rx, writer_cancel, writer_stop, writer_tx)
            })
        });
        let stdout = child.stdout.take();
        let stderr = child.stderr.take();
        let out_tx = tx.clone();
        let out_thread = thread::spawn(move || read_stream(stdout, false, out_tx));
        let err_tx = tx.clone();
        let err_thread = thread::spawn(move || read_stream(stderr, true, err_tx));
        let status = wait_for_child(&mut child, &cancel);
        #[cfg(unix)]
        terminate_remaining_process_group(child.id());
        #[cfg(windows)]
        process_job.terminate();
        stdin_stop.store(true, Ordering::Relaxed);
        if let Some(writer) = stdin_writer {
            let _ = writer.join();
        }
        let output_ok = out_thread.join().unwrap_or(false);
        let error_output_ok = err_thread.join().unwrap_or(false);
        drop(temporary);
        let code = if cancel.load(Ordering::Relaxed) {
            130
        } else if !output_ok || !error_output_ok {
            1
        } else {
            status.map(|s| s.code().unwrap_or(1)).unwrap_or(1)
        };
        let _ = tx.send(ProcessEvent::Finished(code));
    }))
}

fn write_stdin(
    mut stdin: ChildStdin,
    input_rx: Receiver<ProcessCommand>,
    cancel: Arc<AtomicBool>,
    stop: Arc<AtomicBool>,
    tx: Sender<ProcessEvent>,
) {
    loop {
        if cancel.load(Ordering::Relaxed) || stop.load(Ordering::Relaxed) {
            break;
        }
        match input_rx.recv_timeout(Duration::from_millis(50)) {
            Ok(command) => match command {
                ProcessCommand::StdinLine(line) => {
                    if let Err(error) = stdin
                        .write_all(line.as_bytes())
                        .and_then(|()| stdin.write_all(b"\n"))
                        .and_then(|()| stdin.flush())
                    {
                        if error.kind() != std::io::ErrorKind::BrokenPipe
                            && !cancel.load(Ordering::Relaxed)
                        {
                            let _ = tx.send(ProcessEvent::Output(
                                true,
                                format!("Could not send input to the running program: {error}\n"),
                            ));
                        }
                        break;
                    }
                }
                ProcessCommand::CloseStdin => break,
            },
            Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
            Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
        }
    }
}

fn snapshot_directory(root: &Path, source: Option<&Path>) -> PathBuf {
    source
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .unwrap_or_else(|| root.join("build").join("ide-check"))
}

fn configure_process_group(_command: &mut Command) {
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        _command.process_group(0);
    }
}

fn wait_for_child(child: &mut Child, cancel: &AtomicBool) -> std::io::Result<ExitStatus> {
    loop {
        if cancel.load(Ordering::Relaxed) {
            terminate_process_tree(child);
            return child.wait();
        }
        match child.try_wait()? {
            Some(status) => return Ok(status),
            None => thread::sleep(Duration::from_millis(40)),
        }
    }
}

fn terminate_process_tree(child: &mut Child) {
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let status = Command::new("taskkill")
            .args(["/PID", &child.id().to_string(), "/T", "/F"])
            .creation_flags(0x08000000)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        if !status.is_ok_and(|status| status.success()) {
            let _ = child.kill();
        }
    }
    #[cfg(unix)]
    {
        let process_group = -(child.id() as i32);
        // The child and every descendant inherit the dedicated process group.
        unsafe {
            libc::kill(process_group, libc::SIGTERM);
        }
        thread::sleep(Duration::from_millis(180));
        unsafe {
            libc::kill(process_group, libc::SIGKILL);
        }
    }
}

#[cfg(unix)]
fn terminate_remaining_process_group(process_id: u32) {
    let process_group = -(process_id as i32);
    // The direct child has exited; only descendants that inherited its pipes
    // should remain in this dedicated process group.
    let exists = unsafe { libc::kill(process_group, 0) } == 0;
    if !exists {
        return;
    }
    unsafe {
        libc::kill(process_group, libc::SIGTERM);
    }
    thread::sleep(Duration::from_millis(120));
    unsafe {
        libc::kill(process_group, libc::SIGKILL);
    }
}

#[cfg(windows)]
struct WindowsProcessJob(*mut std::ffi::c_void);

#[cfg(windows)]
impl WindowsProcessJob {
    fn new() -> std::io::Result<Self> {
        use std::{ffi::c_void, mem::size_of, ptr::null_mut};

        let handle = unsafe { CreateJobObjectW(null_mut(), std::ptr::null()) };
        if handle.is_null() {
            return Err(std::io::Error::last_os_error());
        }
        let mut limits: JobObjectExtendedLimitInformation = unsafe { std::mem::zeroed() };
        limits.basic_limit_information.limit_flags = 0x0000_2000; // KILL_ON_JOB_CLOSE
        let configured = unsafe {
            SetInformationJobObject(
                handle,
                9, // JobObjectExtendedLimitInformation
                &mut limits as *mut _ as *mut c_void,
                size_of::<JobObjectExtendedLimitInformation>() as u32,
            )
        };
        if configured == 0 {
            let error = std::io::Error::last_os_error();
            unsafe { CloseHandle(handle) };
            return Err(error);
        }
        Ok(Self(handle))
    }

    fn assign(&self, child: &Child) -> std::io::Result<()> {
        use std::os::windows::io::AsRawHandle;
        let assigned = unsafe { AssignProcessToJobObject(self.0, child.as_raw_handle()) };
        if assigned == 0 {
            Err(std::io::Error::last_os_error())
        } else {
            Ok(())
        }
    }

    fn terminate(&self) {
        unsafe {
            TerminateJobObject(self.0, 1);
        }
    }
}

#[cfg(windows)]
fn resume_suspended_process(process_id: u32) -> std::io::Result<()> {
    use std::mem::size_of;

    let snapshot = unsafe { CreateToolhelp32Snapshot(0x0000_0004, 0) };
    if snapshot == -1isize as *mut std::ffi::c_void {
        return Err(std::io::Error::last_os_error());
    }
    let mut entry: ThreadEntry32 = unsafe { std::mem::zeroed() };
    entry.size = size_of::<ThreadEntry32>() as u32;
    let mut result = Err(std::io::Error::new(
        std::io::ErrorKind::NotFound,
        "suspended process thread was not found",
    ));
    let mut has_entry = unsafe { Thread32First(snapshot, &mut entry) } != 0;
    while has_entry {
        if entry.owner_process_id == process_id {
            let thread = unsafe { OpenThread(0x0002, 0, entry.thread_id) };
            if thread.is_null() {
                result = Err(std::io::Error::last_os_error());
            } else {
                let resumed = unsafe { ResumeThread(thread) };
                result = match resumed {
                    u32::MAX => Err(std::io::Error::last_os_error()),
                    0 => Err(std::io::Error::new(
                        std::io::ErrorKind::InvalidData,
                        "process thread was not suspended before Job Object assignment",
                    )),
                    _ => Ok(()),
                };
                unsafe { CloseHandle(thread) };
            }
            break;
        }
        has_entry = unsafe { Thread32Next(snapshot, &mut entry) } != 0;
    }
    unsafe { CloseHandle(snapshot) };
    result
}

#[cfg(windows)]
impl Drop for WindowsProcessJob {
    fn drop(&mut self) {
        unsafe {
            CloseHandle(self.0);
        }
    }
}

#[cfg(windows)]
#[repr(C)]
struct JobObjectBasicLimitInformation {
    per_process_user_time_limit: i64,
    per_job_user_time_limit: i64,
    limit_flags: u32,
    minimum_working_set_size: usize,
    maximum_working_set_size: usize,
    active_process_limit: u32,
    affinity: usize,
    priority_class: u32,
    scheduling_class: u32,
}

#[cfg(windows)]
#[repr(C)]
struct JobObjectIoCounters {
    read_operation_count: u64,
    write_operation_count: u64,
    other_operation_count: u64,
    read_transfer_count: u64,
    write_transfer_count: u64,
    other_transfer_count: u64,
}

#[cfg(windows)]
#[repr(C)]
struct JobObjectExtendedLimitInformation {
    basic_limit_information: JobObjectBasicLimitInformation,
    io_info: JobObjectIoCounters,
    process_memory_limit: usize,
    job_memory_limit: usize,
    peak_process_memory_used: usize,
    peak_job_memory_used: usize,
}

#[cfg(windows)]
#[repr(C)]
struct ThreadEntry32 {
    size: u32,
    usage: u32,
    thread_id: u32,
    owner_process_id: u32,
    base_priority: i32,
    delta_priority: i32,
    flags: u32,
}

#[cfg(windows)]
#[link(name = "kernel32")]
unsafe extern "system" {
    fn CreateJobObjectW(
        job_attributes: *mut std::ffi::c_void,
        name: *const u16,
    ) -> *mut std::ffi::c_void;
    fn SetInformationJobObject(
        job: *mut std::ffi::c_void,
        information_class: i32,
        information: *mut std::ffi::c_void,
        information_length: u32,
    ) -> i32;
    fn AssignProcessToJobObject(job: *mut std::ffi::c_void, process: *mut std::ffi::c_void) -> i32;
    fn TerminateJobObject(job: *mut std::ffi::c_void, exit_code: u32) -> i32;
    fn CloseHandle(handle: *mut std::ffi::c_void) -> i32;
    fn CreateToolhelp32Snapshot(flags: u32, process_id: u32) -> *mut std::ffi::c_void;
    fn Thread32First(snapshot: *mut std::ffi::c_void, entry: *mut ThreadEntry32) -> i32;
    fn Thread32Next(snapshot: *mut std::ffi::c_void, entry: *mut ThreadEntry32) -> i32;
    fn OpenThread(access: u32, inherit: i32, thread_id: u32) -> *mut std::ffi::c_void;
    fn ResumeThread(thread: *mut std::ffi::c_void) -> u32;
}

fn read_stream(
    stream: Option<impl std::io::Read>,
    is_stderr: bool,
    tx: Sender<ProcessEvent>,
) -> bool {
    let Some(mut stream) = stream else {
        return true;
    };
    let mut buffer = [0_u8; 8192];
    let mut pending = Vec::with_capacity(8196);
    loop {
        match stream.read(&mut buffer) {
            Ok(0) => {
                if !pending.is_empty() {
                    let chunk = String::from_utf8_lossy(&pending).into_owned();
                    if tx.send(ProcessEvent::Output(is_stderr, chunk)).is_err() {
                        return false;
                    }
                }
                return true;
            }
            Ok(count) => {
                pending.extend_from_slice(&buffer[..count]);
                loop {
                    match std::str::from_utf8(&pending) {
                        Ok(valid) => {
                            if !valid.is_empty()
                                && tx
                                    .send(ProcessEvent::Output(is_stderr, valid.to_owned()))
                                    .is_err()
                            {
                                return false;
                            }
                            pending.clear();
                            break;
                        }
                        Err(error) => {
                            let valid_len = error.valid_up_to();
                            if valid_len > 0 {
                                let valid = String::from_utf8(pending.drain(..valid_len).collect())
                                    .expect("valid UTF-8 prefix");
                                if tx.send(ProcessEvent::Output(is_stderr, valid)).is_err() {
                                    return false;
                                }
                            }
                            if let Some(invalid_len) = error.error_len() {
                                let invalid: Vec<u8> = pending.drain(..invalid_len).collect();
                                let lossy = String::from_utf8_lossy(&invalid).into_owned();
                                if tx.send(ProcessEvent::Output(is_stderr, lossy)).is_err() {
                                    return false;
                                }
                            } else {
                                break;
                            }
                        }
                    }
                }
            }
            Err(error) if error.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(error) => {
                let _ = tx.send(ProcessEvent::Output(
                    true,
                    format!("Output read error: {error}\n"),
                ));
                return false;
            }
        }
    }
}

fn build_command_with_roots(
    application_root: &Path,
    workspace_root: &Path,
    _settings: &Settings,
    kind: TaskKind,
    source: Option<&Path>,
) -> anyhow::Result<Command> {
    let python = std::env::var_os("RBL_PYTHON").unwrap_or_else(|| {
        if cfg!(target_os = "windows") {
            "python".into()
        } else {
            "python3".into()
        }
    });
    let mut command = Command::new(python);
    command.current_dir(workspace_root);
    match kind {
        TaskKind::Run | TaskKind::Build | TaskKind::Check | TaskKind::Assembly => {
            let source =
                source.ok_or_else(|| anyhow::anyhow!("select or save an RBL source file first"))?;
            if !is_rbl_source(source) {
                anyhow::bail!("compiler actions accept only .rbl source files");
            }
            command.arg(application_root.join("tools").join("rbltool.py"));
            command.arg(match kind {
                TaskKind::Run => "run",
                TaskKind::Build => "build",
                TaskKind::Check => "check",
                _ => "asm",
            });
            command.arg(source);
        }
        TaskKind::Tests => {
            command.arg(application_root.join("tests").join("run_tests.py"));
        }
        TaskKind::Benchmarks => {
            command.arg(
                application_root
                    .join("benchmarks")
                    .join("run_benchmarks.py"),
            );
        }
        TaskKind::Doctor => {
            command
                .arg(application_root.join("tools").join("rbltool.py"))
                .arg("doctor");
        }
    }
    Ok(command)
}

#[cfg(test)]
fn build_command(
    root: &Path,
    settings: &Settings,
    kind: TaskKind,
    source: Option<&Path>,
) -> anyhow::Result<Command> {
    build_command_with_roots(root, root, settings, kind, source)
}

fn is_rbl_source(path: &Path) -> bool {
    path.extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("rbl"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;

    #[test]
    fn compiler_commands_use_argument_boundaries_not_a_shell() {
        let root = PathBuf::from("C:/RBL Studio/project");
        let source = PathBuf::from("C:/Users/test/My Project/main file.rbl");
        let command =
            build_command(&root, &Settings::default(), TaskKind::Run, Some(&source)).unwrap();
        let args: Vec<_> = command
            .get_args()
            .map(|arg| arg.to_string_lossy().into_owned())
            .collect();
        assert_eq!(args.len(), 3);
        assert!(args[0].ends_with("tools/rbltool.py") || args[0].ends_with("tools\\rbltool.py"));
        assert_eq!(args[1], "run");
        assert_eq!(args[2], source.to_string_lossy());
    }

    #[test]
    fn external_workspace_uses_application_scripts_and_workspace_cwd() {
        let application_root = PathBuf::from("/opt/rbl-studio");
        let workspace_root = PathBuf::from("/home/user/projects/example");
        let source = workspace_root.join("src/main.rbl");
        for (kind, expected_script) in [
            (TaskKind::Run, "tools/rbltool.py"),
            (TaskKind::Build, "tools/rbltool.py"),
            (TaskKind::Check, "tools/rbltool.py"),
            (TaskKind::Assembly, "tools/rbltool.py"),
            (TaskKind::Tests, "tests/run_tests.py"),
            (TaskKind::Benchmarks, "benchmarks/run_benchmarks.py"),
            (TaskKind::Doctor, "tools/rbltool.py"),
        ] {
            let command = build_command_with_roots(
                &application_root,
                &workspace_root,
                &Settings::default(),
                kind,
                matches!(
                    kind,
                    TaskKind::Run | TaskKind::Build | TaskKind::Check | TaskKind::Assembly
                )
                .then_some(source.as_path()),
            )
            .unwrap();
            assert_eq!(command.get_current_dir(), Some(workspace_root.as_path()));
            let first_arg = command.get_args().next().unwrap().to_string_lossy();
            assert_eq!(
                PathBuf::from(first_arg.as_ref()),
                application_root.join(expected_script),
                "wrong script root for {kind:?}"
            );
        }
    }

    #[test]
    fn non_source_tasks_do_not_require_a_source_path() {
        let root = PathBuf::from("/tmp/rbl project");
        for kind in [TaskKind::Tests, TaskKind::Benchmarks, TaskKind::Doctor] {
            assert!(build_command(&root, &Settings::default(), kind, None).is_ok());
        }
        assert!(build_command(&root, &Settings::default(), TaskKind::Run, None).is_err());
    }

    #[test]
    fn compiler_commands_reject_non_rbl_text_files() {
        let root = PathBuf::from("/tmp/rbl project");
        for path in ["notes.txt", "README.md", "script.py", "source.RS"] {
            let source = PathBuf::from(path);
            for kind in [
                TaskKind::Run,
                TaskKind::Build,
                TaskKind::Check,
                TaskKind::Assembly,
            ] {
                assert!(
                    build_command(&root, &Settings::default(), kind, Some(&source)).is_err(),
                    "{kind:?} unexpectedly accepted {path}"
                );
            }
        }
        assert!(build_command(
            &root,
            &Settings::default(),
            TaskKind::Run,
            Some(Path::new("PROGRAM.RBL"))
        )
        .is_ok());
    }

    #[test]
    fn process_start_rejects_non_rbl_before_creating_a_snapshot() {
        let (tx, rx) = crossbeam_channel::unbounded();
        start(
            PathBuf::from("/repo"),
            Settings::default(),
            TaskKind::Run,
            SourceDocument {
                path: Some(PathBuf::from("notes.txt")),
                is_rbl: false,
                snapshot: Some("not RBL".into()),
            },
            tx,
            crossbeam_channel::unbounded().1,
            Arc::new(AtomicBool::new(false)),
        );
        let mut output = String::new();
        let mut finished = None;
        while let Ok(event) = rx.recv_timeout(Duration::from_secs(1)) {
            match event {
                ProcessEvent::Output(_, chunk) => output.push_str(&chunk),
                ProcessEvent::Finished(code) => {
                    finished = Some(code);
                    break;
                }
                ProcessEvent::Started(_) | ProcessEvent::SnapshotCreated(_, _) => {
                    panic!("non-RBL task must be rejected before process startup")
                }
            }
        }
        assert_eq!(finished, Some(1));
        assert!(output.contains("only .rbl"));
    }

    #[test]
    fn process_start_rejects_unsaved_plain_text_without_a_source_path() {
        let (tx, rx) = crossbeam_channel::unbounded();
        start(
            PathBuf::from("/repo"),
            Settings::default(),
            TaskKind::Run,
            SourceDocument {
                path: None,
                is_rbl: false,
                snapshot: Some("ordinary text".into()),
            },
            tx,
            crossbeam_channel::unbounded().1,
            Arc::new(AtomicBool::new(false)),
        );
        let mut events = Vec::new();
        while let Ok(event) = rx.try_recv() {
            events.push(event);
        }
        assert!(
            matches!(events.first(), Some(ProcessEvent::Output(true, message)) if message.contains("only .rbl"))
        );
        assert!(matches!(events.last(), Some(ProcessEvent::Finished(1))));
        assert_eq!(events.len(), 2);
    }

    #[test]
    fn output_reader_preserves_utf8_across_chunk_boundaries() {
        struct ChunkedReader {
            bytes: Vec<u8>,
            offset: usize,
        }
        impl std::io::Read for ChunkedReader {
            fn read(&mut self, output: &mut [u8]) -> std::io::Result<usize> {
                if self.offset >= self.bytes.len() {
                    return Ok(0);
                }
                let count = output.len().min(1).min(self.bytes.len() - self.offset);
                output[..count].copy_from_slice(&self.bytes[self.offset..self.offset + count]);
                self.offset += count;
                Ok(count)
            }
        }
        let expected = "stdout: Привет 🙂\n";
        let reader = ChunkedReader {
            bytes: expected.as_bytes().to_vec(),
            offset: 0,
        };
        let (tx, rx) = crossbeam_channel::unbounded();
        read_stream(Some(reader), false, tx);
        let mut actual = String::new();
        while let Ok(ProcessEvent::Output(_, chunk)) = rx.try_recv() {
            actual.push_str(&chunk);
        }
        assert_eq!(actual, expected);
    }

    #[test]
    fn output_reader_failure_is_reported_to_the_task_supervisor() {
        struct FailingReader(bool);
        impl std::io::Read for FailingReader {
            fn read(&mut self, output: &mut [u8]) -> std::io::Result<usize> {
                if self.0 {
                    return Err(std::io::Error::other("simulated pipe failure"));
                }
                self.0 = true;
                output[..7].copy_from_slice(b"partial");
                Ok(7)
            }
        }
        let (tx, rx) = crossbeam_channel::unbounded();
        assert!(!read_stream(Some(FailingReader(false)), false, tx));
        let output: String = rx
            .try_iter()
            .filter_map(|event| match event {
                ProcessEvent::Output(_, text) => Some(text),
                _ => None,
            })
            .collect();
        assert!(output.contains("partial"));
        assert!(output.contains("Output read error: simulated pipe failure"));
    }

    #[test]
    fn output_reader_reports_disconnected_receiver_for_final_utf8_fragment() {
        let (tx, rx) = crossbeam_channel::bounded(1);
        drop(rx);
        let reader = std::io::Cursor::new(vec![0xF0, 0x9F]);
        assert!(!read_stream(Some(reader), false, tx));
    }

    #[test]
    fn interactive_input_reaches_child_stdin_as_complete_lines() {
        use std::io::Read;

        let mut command = if cfg!(windows) {
            let mut command = Command::new("powershell.exe");
            command.args([
                "-NoProfile",
                "-Command",
                "$a=[Console]::In.ReadLine(); $b=[Console]::In.ReadLine(); [Console]::Write(\"$a|$b\")",
            ]);
            command
        } else {
            let mut command = Command::new("sh");
            command.args([
                "-c",
                "IFS= read -r a; IFS= read -r b; printf '%s|%s' \"$a\" \"$b\"",
            ]);
            command
        };
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let stdin = child.stdin.take().unwrap();
        let mut stdout = child.stdout.take().unwrap();
        let (input_tx, input_rx) = crossbeam_channel::unbounded();
        let (event_tx, event_rx) = crossbeam_channel::unbounded();
        input_tx
            .send(ProcessCommand::StdinLine("first".into()))
            .unwrap();
        input_tx
            .send(ProcessCommand::StdinLine("second".into()))
            .unwrap();
        drop(input_tx);
        let cancel = Arc::new(AtomicBool::new(false));
        let stop = Arc::new(AtomicBool::new(false));
        let writer_cancel = cancel.clone();
        let writer_stop = stop.clone();
        let writer = thread::spawn(move || {
            write_stdin(stdin, input_rx, writer_cancel, writer_stop, event_tx)
        });
        let status = wait_for_child(&mut child, &cancel).unwrap();
        stop.store(true, Ordering::Relaxed);
        writer.join().unwrap();
        let mut output = String::new();
        stdout.read_to_string(&mut output).unwrap();

        assert!(status.success());
        assert_eq!(output, "first|second");
        assert!(event_rx.is_empty());
    }

    #[test]
    fn close_stdin_sends_eof_to_the_child() {
        use std::io::Read;

        let mut command = if cfg!(windows) {
            let mut command = Command::new("powershell.exe");
            command.args([
                "-NoProfile",
                "-Command",
                "$value = [Console]::In.ReadToEnd(); [Console]::Out.Write($value)",
            ]);
            command
        } else {
            let mut command = Command::new("sh");
            command.args(["-c", "cat"]);
            command
        };
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let stdin = child.stdin.take().unwrap();
        let mut stdout = child.stdout.take().unwrap();
        let (input_tx, input_rx) = crossbeam_channel::bounded(4);
        let (event_tx, event_rx) = crossbeam_channel::unbounded();
        input_tx
            .send(ProcessCommand::StdinLine("hello".into()))
            .unwrap();
        input_tx.send(ProcessCommand::CloseStdin).unwrap();
        drop(input_tx);
        let cancel = Arc::new(AtomicBool::new(false));
        let stop = Arc::new(AtomicBool::new(false));
        let writer_cancel = cancel.clone();
        let writer_stop = stop.clone();
        let writer = thread::spawn(move || {
            write_stdin(stdin, input_rx, writer_cancel, writer_stop, event_tx)
        });

        let status = child.wait().unwrap();
        stop.store(true, Ordering::Relaxed);
        writer.join().unwrap();
        let mut output = String::new();
        stdout.read_to_string(&mut output).unwrap();

        assert!(status.success());
        assert_eq!(output, "hello\n");
        assert!(event_rx.is_empty());
    }

    #[test]
    fn blocked_stdin_writer_does_not_block_process_cancellation() {
        let mut command = if cfg!(windows) {
            let mut command = Command::new("powershell.exe");
            command.args(["-NoProfile", "-Command", "Start-Sleep -Seconds 30"]);
            command
        } else {
            let mut command = Command::new("sh");
            command.args(["-c", "sleep 30"]);
            command
        };
        configure_process_group(&mut command);
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let stdin = child.stdin.take().unwrap();
        let (input_tx, input_rx) = crossbeam_channel::bounded(32);
        let (event_tx, _event_rx) = crossbeam_channel::unbounded();
        let cancel = Arc::new(AtomicBool::new(false));
        let stop = Arc::new(AtomicBool::new(false));
        let writer_cancel = cancel.clone();
        let writer_stop = stop.clone();
        let writer = thread::spawn(move || {
            write_stdin(stdin, input_rx, writer_cancel, writer_stop, event_tx)
        });
        for _ in 0..32 {
            input_tx
                .try_send(ProcessCommand::StdinLine("x".repeat(16 * 1024)))
                .unwrap();
        }
        drop(input_tx);
        thread::sleep(Duration::from_millis(200));

        let started = std::time::Instant::now();
        cancel.store(true, Ordering::Relaxed);
        let status = wait_for_child(&mut child, &cancel).unwrap();
        #[cfg(unix)]
        terminate_remaining_process_group(child.id());
        stop.store(true, Ordering::Relaxed);
        writer.join().unwrap();

        assert!(!status.success());
        assert!(started.elapsed() < Duration::from_secs(4));
    }

    #[test]
    fn unsaved_snapshots_stay_next_to_the_original_for_relative_imports() {
        let root = PathBuf::from("/repo");
        let original = PathBuf::from("/workspace/project/src/main.rbl");
        assert_eq!(
            snapshot_directory(&root, Some(&original)),
            PathBuf::from("/workspace/project/src")
        );
        assert_eq!(
            snapshot_directory(&root, None),
            PathBuf::from("/repo/build/ide-check")
        );
    }

    #[test]
    fn cancellation_terminates_the_child_process_group() {
        let mut command = if cfg!(windows) {
            let mut command = Command::new("ping");
            command.args(["-n", "30", "127.0.0.1"]);
            command
        } else {
            let mut command = Command::new("sh");
            command.args(["-c", "sleep 30"]);
            command
        };
        configure_process_group(&mut command);
        let mut child = command
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let cancel = Arc::new(AtomicBool::new(false));
        let signal = cancel.clone();
        thread::spawn(move || {
            thread::sleep(Duration::from_millis(100));
            signal.store(true, Ordering::Relaxed);
        });
        let started = Instant::now();
        let status = wait_for_child(&mut child, &cancel).unwrap();
        assert!(!status.success());
        assert!(started.elapsed() < Duration::from_secs(3));
    }

    #[cfg(unix)]
    #[test]
    fn exited_parent_cannot_leave_pipe_reader_blocked_by_a_descendant() {
        use std::io::Read;

        let mut command = Command::new("sh");
        command.args(["-c", "sleep 30 &"]);
        configure_process_group(&mut command);
        let mut child = command
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        let process_id = child.id();
        let mut stdout = child.stdout.take().unwrap();
        let (done_tx, done_rx) = crossbeam_channel::bounded(1);
        let reader = thread::spawn(move || {
            let mut output = Vec::new();
            let _ = stdout.read_to_end(&mut output);
            let _ = done_tx.send(());
        });

        assert!(child.wait().unwrap().success());
        let started = Instant::now();
        terminate_remaining_process_group(process_id);
        done_rx
            .recv_timeout(Duration::from_secs(3))
            .expect("descendant kept the inherited stdout pipe open");
        reader.join().unwrap();
        assert!(started.elapsed() < Duration::from_secs(3));
    }

    #[test]
    fn bounded_output_channel_applies_backpressure_to_stream_readers() {
        let input = std::io::Cursor::new(vec![b'x'; 8192 * 3]);
        let (tx, rx) = crossbeam_channel::bounded(1);
        let reader = thread::spawn(move || read_stream(Some(input), false, tx));

        let first = rx.recv_timeout(Duration::from_secs(1)).unwrap();
        assert!(matches!(first, ProcessEvent::Output(false, _)));
        let second = rx.recv_timeout(Duration::from_secs(1)).unwrap();
        assert!(matches!(second, ProcessEvent::Output(false, _)));
        let third = rx.recv_timeout(Duration::from_secs(1)).unwrap();
        assert!(matches!(third, ProcessEvent::Output(false, _)));
        reader.join().unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn windows_job_object_has_the_expected_abi_and_terminates_a_child() {
        use std::mem::size_of;

        assert_eq!(size_of::<JobObjectBasicLimitInformation>(), 64);
        assert_eq!(size_of::<JobObjectIoCounters>(), 48);
        assert_eq!(size_of::<JobObjectExtendedLimitInformation>(), 144);
        assert_eq!(size_of::<ThreadEntry32>(), 28);

        let job = WindowsProcessJob::new().unwrap();
        let mut command = Command::new("ping");
        command.args(["-n", "30", "127.0.0.1"]);
        let mut child = command
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .spawn()
            .unwrap();
        job.assign(&child).unwrap();
        job.terminate();
        let status = child.wait().unwrap();
        assert!(!status.success());
    }

    #[cfg(windows)]
    #[test]
    fn windows_job_object_terminates_descendants_after_the_parent_exits() {
        let temp = tempfile::tempdir().unwrap();
        let pid_file = temp.path().join("descendant.pid");
        let pid_path = pid_file.to_string_lossy().replace('\'', "''");
        let script = format!(
            "$child = Start-Process -FilePath ping.exe -ArgumentList '-n 30 127.0.0.1' -WindowStyle Hidden -PassThru; Set-Content -LiteralPath '{pid_path}' -Value $child.Id"
        );
        use std::os::windows::process::CommandExt;
        let mut command = Command::new("powershell.exe");
        command
            .args(["-NoProfile", "-Command", &script])
            .creation_flags(0x08000000 | 0x00000004)
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        let mut parent = command
            .spawn()
            .expect("PowerShell is available on Windows test hosts");
        let job = WindowsProcessJob::new().expect("create descendant test job");
        job.assign(&parent)
            .expect("assign parent before it spawns a child");
        resume_suspended_process(parent.id()).expect("resume parent after job assignment");
        assert!(parent.wait().unwrap().success());

        let child_pid = std::fs::read_to_string(&pid_file)
            .expect("parent writes the spawned child PID")
            .trim()
            .parse::<u32>()
            .unwrap();
        job.terminate();

        let filter = format!("PID eq {child_pid}");
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            let listing = Command::new("tasklist")
                .args(["/FI", &filter, "/FO", "CSV", "/NH"])
                .output()
                .expect("tasklist is available on Windows test hosts");
            let listing = String::from_utf8_lossy(&listing.stdout);
            if !listing.contains(&format!("\",\"{child_pid}\",\"")) {
                break;
            }
            if Instant::now() >= deadline {
                let _ = Command::new("taskkill")
                    .args(["/PID", &child_pid.to_string(), "/T", "/F"])
                    .status();
                panic!("job object did not terminate descendant process {child_pid}");
            }
            thread::sleep(Duration::from_millis(50));
        }
    }

    #[cfg(unix)]
    #[test]
    fn process_runner_executes_an_unsaved_rbl_snapshot_and_streams_output() {
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let dir = tempfile::tempdir().unwrap();
        let source = dir.path().join("main.rbl");
        std::fs::write(&source, "func main() (\n    print(\"saved version\")\n)\n").unwrap();
        let snapshot = "func main() (\n    print(\"NATIVE_IDE_PROCESS_OK\")\n)\n".to_owned();
        let (tx, rx) = crossbeam_channel::unbounded();
        let cancel = Arc::new(AtomicBool::new(false));

        start(
            root,
            Settings::default(),
            TaskKind::Run,
            SourceDocument {
                path: Some(source),
                is_rbl: true,
                snapshot: Some(snapshot),
            },
            tx,
            crossbeam_channel::unbounded().1,
            cancel,
        );

        let deadline = Instant::now() + Duration::from_secs(30);
        let mut output = String::new();
        let mut exit_code = None;
        while Instant::now() < deadline && exit_code.is_none() {
            match rx.recv_timeout(Duration::from_millis(100)) {
                Ok(ProcessEvent::Output(_, text)) => output.push_str(&text),
                Ok(ProcessEvent::Finished(code)) => exit_code = Some(code),
                Ok(ProcessEvent::Started(_)) | Ok(ProcessEvent::SnapshotCreated(_, _)) => {}
                Err(crossbeam_channel::RecvTimeoutError::Timeout) => {}
                Err(error) => panic!("process event channel closed unexpectedly: {error}"),
            }
        }

        assert_eq!(exit_code, Some(0), "process output: {output}");
        assert!(
            output.contains("NATIVE_IDE_PROCESS_OK"),
            "process output: {output}"
        );
    }

    #[cfg(unix)]
    #[test]
    fn process_runner_supports_two_sequential_rbl_input_prompts() {
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let source = root.join("examples/utilities/unit_converter.rbl");
        assert!(source.is_file(), "missing interactive example: {source:?}");
        let (event_tx, event_rx) = crossbeam_channel::unbounded();
        let (input_tx, input_rx) = crossbeam_channel::bounded(32);
        let cancel = Arc::new(AtomicBool::new(false));

        start(
            root,
            Settings {
                wsl_mode: "native".into(),
                ..Settings::default()
            },
            TaskKind::Run,
            SourceDocument {
                path: Some(source),
                is_rbl: true,
                snapshot: None,
            },
            event_tx,
            input_rx,
            cancel,
        );

        let deadline = Instant::now() + Duration::from_secs(45);
        let mut output = String::new();
        let mut first_input_sent = false;
        let mut second_input_sent = false;
        let mut exit_code = None;
        while Instant::now() < deadline && exit_code.is_none() {
            match event_rx.recv_timeout(Duration::from_millis(100)) {
                Ok(ProcessEvent::Output(_, chunk)) => {
                    output.push_str(&chunk);
                    if !first_input_sent && output.contains("Choose 1, 2 or 3: ") {
                        input_tx
                            .send(ProcessCommand::StdinLine("1".into()))
                            .unwrap();
                        first_input_sent = true;
                    }
                    if !second_input_sent && output.contains("Value to convert: ") {
                        input_tx
                            .send(ProcessCommand::StdinLine("10".into()))
                            .unwrap();
                        second_input_sent = true;
                    }
                }
                Ok(ProcessEvent::Finished(code)) => exit_code = Some(code),
                Ok(ProcessEvent::Started(_)) | Ok(ProcessEvent::SnapshotCreated(_, _)) => {}
                Err(crossbeam_channel::RecvTimeoutError::Timeout) => {}
                Err(error) => panic!("process event channel closed unexpectedly: {error}"),
            }
        }

        assert!(
            first_input_sent,
            "first input prompt was not streamed: {output}"
        );
        assert!(
            second_input_sent,
            "second input prompt was not streamed: {output}"
        );
        assert_eq!(exit_code, Some(0), "process output: {output}");
        assert!(
            output.contains("10.000 km = 6.214 mi"),
            "process output: {output}"
        );
    }
}
