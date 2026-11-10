from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FRAME = ROOT / "native" / "wx-ui" / "src" / "main_frame.cpp"
WORKER = ROOT / "src" / "bin" / "rbl-studio-worker.rs"


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
        self.assertIn('"event": "bye"', self.worker)
        self.assertIn("if !output_broken", self.worker)


if __name__ == "__main__":
    unittest.main()
