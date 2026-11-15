from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FRAME = ROOT / "rbl-ide" / "wx-ui" / "src" / "main_frame.cpp"
WORKER = ROOT / "rbl-ide" / "worker" / "rbl-studio-worker.rs"


class WxWorkerLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.frame = FRAME.read_text(encoding="utf-8")
        cls.worker = WORKER.read_text(encoding="utf-8")

    def test_destructor_drains_worker_pipes_before_hard_kill_fallback(self) -> None:
        start = self.frame.index("MainFrame::~MainFrame()")
        end = self.frame.index("void MainFrame::OpenStartupFile", start)
        destructor = self.frame[start:end]
        shutdown = destructor.index('SendWorkerRequest("{\\"command\\":\\"shutdown\\"}")')
        drain = destructor.index("PollWorker();", shutdown)
        hard_kill = destructor.index("wxProcess::Kill(worker_pid_, wxSIGKILL, wxKILL_CHILDREN)")
        stop_timer = destructor.index("worker_timer_.Stop()", hard_kill)
        self.assertLess(shutdown, drain)
        self.assertLess(drain, hard_kill)
        self.assertLess(hard_kill, stop_timer)
        self.assertIn("std::chrono::seconds(5)", destructor)

    def test_restart_has_bounded_hard_kill_fallback_while_polling(self) -> None:
        restart = self.frame.index("void MainFrame::RestartWorker()")
        restart_end = self.frame.index("bool MainFrame::SendWorkerLine", restart)
        self.assertIn("worker_shutdown_deadline_", self.frame[restart:restart_end])
        poll = self.frame.index("void MainFrame::PollWorker()")
        poll_end = self.frame.index("void MainFrame::HandleWorkerEvent", poll)
        poll_body = self.frame[poll:poll_end]
        self.assertIn("restart_worker_pending_ && !worker_shutdown_kill_sent_", poll_body)
        self.assertIn("wxProcess::Kill(worker_pid_, wxSIGKILL, wxKILL_CHILDREN)", poll_body)

    def test_protocol_requests_pause_before_control_reserve_is_consumed(self) -> None:
        self.assertIn("fn ready_request_receiver(", self.worker)
        self.assertIn("output_queue_has_control_reserve(pending_output_len)", self.worker)
        self.assertIn("ready_request_receiver(&request_rx, pending_output.len())", self.worker)
        self.assertIn("recv(request_rx_ready)", self.worker)

    def test_bounded_backpressure_discards_only_output_payloads(self) -> None:
        self.assertIn("fn is_output_event(value: &Value) -> bool", self.worker)
        self.assertIn("let mut output_backpressure_degraded = false;", self.worker)
        self.assertIn("pending_output.retain(|value| !is_output_event(value));", self.worker)
        self.assertIn("reset_output_backpressure_if_drained", self.worker)
        self.assertIn('"event": "bye"', self.worker)
        self.assertIn("if !output_broken", self.worker)

    def test_gui_worker_poll_has_per_turn_byte_and_frame_budgets(self) -> None:
        poll = self.frame.index("void MainFrame::PollWorker()")
        poll_end = self.frame.index("void MainFrame::HandleWorkerEvent", poll)
        body = self.frame[poll:poll_end]
        self.assertIn("max_worker_poll_bytes", body)
        self.assertIn("max_worker_poll_frames", body)
        self.assertIn("worker_poll_bytes < max_worker_poll_bytes", body)
        self.assertIn("parsed_worker_frames < max_worker_poll_frames", body)
        self.assertNotIn("while (stream->CanRead())", body)
        parse_loop = body.index("std::size_t parsed_worker_frames = 0;")
        post_parse_guard = body.index("if (worker_buffer_.size() > max_worker_frame &&", parse_loop)
        guard_end = body.index("flush_output_batch();", post_parse_guard)
        self.assertIn("worker_buffer_.find('\\n') == std::string::npos", body[post_parse_guard:guard_end])

    def test_worker_owns_temporary_snapshot_lifecycle(self) -> None:
        self.assertNotIn("snapshot_file", self.worker)
        self.assertNotIn("remove_file(&path)", self.worker)
        self.assertIn("snapshot: request.snapshot", self.worker)
        self.assertIn("ProcessEvent::SnapshotCreated", (ROOT / "rbl-ide" / "worker" / "process.rs").read_text(encoding="utf-8"))
        self.assertIn("JsonEscape(FromUtf8(source_snapshot))", self.frame)

    def test_gui_frame_limit_includes_ndjson_delimiter_and_stdin(self) -> None:
        self.assertIn('#include "worker_frame_limits.hpp"', self.frame)
        self.assertIn("MaxFrameBytes = 36U * 1024U * 1024U", (ROOT / "rbl-ide" / "wx-ui" / "include" / "worker_frame_limits.hpp").read_text(encoding="utf-8"))
        send_start = self.frame.index("bool MainFrame::SendWorkerLine(")
        send_end = self.frame.index("bool MainFrame::SendWorkerRequest(", send_start)
        self.assertIn('ToUtf8(line + "\\n")', self.frame[send_start:send_end])
        self.assertIn("FrameFits(bytes.size())", self.frame[send_start:send_end])
        task_start = self.frame.index("void MainFrame::StartTask(")
        task_end = self.frame.index("void MainFrame::PollWorker(", task_start)
        self.assertIn("JsonPayloadFitsFrame(request.ToUTF8().length())", self.frame[task_start:task_end])
        stdin_start = self.frame.index("void MainFrame::SendStdin()")
        stdin_end = self.frame.index("void MainFrame::SendStdinEof()", stdin_start)
        self.assertIn("JsonPayloadFitsFrame(request.ToUTF8().length())", self.frame[stdin_start:stdin_end])

    def test_gui_stdin_limit_matches_worker_and_retains_oversized_input(self) -> None:
        self.assertIn("MaxStdinLineBytes = 16U * 1024U", (ROOT / "rbl-ide" / "wx-ui" / "include" / "worker_frame_limits.hpp").read_text(encoding="utf-8"))
        stdin_start = self.frame.index("void MainFrame::SendStdin()")
        stdin_end = self.frame.index("void MainFrame::SendStdinEof()", stdin_start)
        body = self.frame[stdin_start:stdin_end]
        guard = body.index("StdinLineFits(line.ToUTF8().length())")
        request = body.index("const auto request =")
        self.assertLess(guard, request)
        self.assertIn("input was retained", body[guard:request])
        self.assertIn("line.len() > 16 * 1024", self.worker)
        self.assertIn("MAX_FRAME_BYTES: u64 = 36 * 1024 * 1024", self.worker)
        self.assertIn("frame_exceeds_limit(frame.len())", self.worker)

    def test_windows_directory_chain_uses_synchronous_relative_opens(self) -> None:
        self.assertIn('#include "windows_path_parts.hpp"', self.frame)
        start = self.frame.index("bool LockWindowsDirectoryChain(")
        end = self.frame.index("std::filesystem::path UniqueWindowsSiblingPath", start)
        body = self.frame[start:end]
        self.assertIn("split_absolute_directory_path(directory.native(), path_parts)", body)
        self.assertNotIn("directory.root_path()", body)
        self.assertNotIn("directory.relative_path()", body)
        self.assertIn("CreateFileW(path_parts.root.c_str()", body)
        self.assertIn("attributes.RootDirectory = guard.handles.back()", body)
        self.assertIn("FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT", body)
        self.assertIn("FILE_SHARE_READ | FILE_SHARE_WRITE", body)


if __name__ == "__main__":
    unittest.main()
