"""SDK-job protocol, secret handling and process-group lifecycle acceptance."""

import json
import os
import selectors
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path


class Job:
    def __init__(self, argv, launcher=None, **spec):
        self.process = subprocess.Popen(
            launcher or [sys.executable, "-u", str(Path(__file__).with_name("sdk_job.py"))],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.pending = b""
        self.events = []
        assert self.next()["event"] == "ready"
        self.send({"argv": argv, "terminate_grace": 0.1, **spec})
        self.child = None

    def send(self, message):
        self.process.stdin.write(json.dumps(message).encode() + b"\n")
        self.process.stdin.flush()

    def next(self, timeout=8):
        deadline = time.monotonic() + timeout
        while b"\n" not in self.pending:
            if not self.selector.select(max(0, deadline - time.monotonic())):
                raise TimeoutError("No SDK job event")
            data = os.read(self.process.stdout.fileno(), 4096)
            if not data:
                raise EOFError("SDK job closed before a result")
            self.pending += data
        line, self.pending = self.pending.split(b"\n", 1)
        event = json.loads(line)
        self.events.append(event)
        return event

    def running(self):
        event = self.next()
        assert event["event"] == "running", event
        self.child = event["pid"]
        return self.child

    def result(self):
        while True:
            event = self.next()
            if event["event"] == "result":
                self.transport_exit = self.process.wait(timeout=5)
                self.diagnostics = self.process.stderr.read().decode("utf-8", "replace")
                return event

    def close(self):
        if self.process.poll() is None:
            try:
                self.send({"event": "cancel"})
                self.process.wait(timeout=5)
            except (BrokenPipeError, subprocess.TimeoutExpired):
                self.process.kill()
                self.process.wait()
        self.selector.close()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


class SDKJobTests(unittest.TestCase):
    def job(self, code, **spec):
        job = Job([sys.executable, "-c", code], **spec)
        self.addCleanup(job.close)
        return job

    def test_exact_exit_and_unicode_output(self):
        job = self.job("import sys; print('Кириллица 😀'); sys.exit(2)")
        job.running()
        self.assertEqual(job.result()["exit_code"], 2)
        self.assertEqual(job.transport_exit, 2)
        self.assertNotIn("Fatal Python error", job.diagnostics)
        self.assertIn("Кириллица 😀", "".join(row.get("text", "") for row in job.events))

    def test_secret_environment_is_not_in_argv_or_output(self):
        secret = "synthetic-passphrase-Юникод"
        job = self.job(
            "import os,time; print(os.environ['KEY_PASSPHRASE'],flush=True); time.sleep(.2)",
            secret_environment={"KEY_PASSPHRASE": secret},
        )
        child = job.running()
        self.assertNotIn(secret.encode(), Path(f"/proc/{child}/cmdline").read_bytes())
        result = job.result()
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(job.transport_exit, 0)
        self.assertTrue(result["output_suppressed"])
        self.assertNotIn(secret, json.dumps(job.events, ensure_ascii=False))
        self.assertFalse(any(row["event"] == "output" for row in job.events))

    def test_cancel_kills_uncooperative_descendant(self):
        with tempfile.TemporaryDirectory() as directory:
            pid_file = Path(directory) / "child.pid"
            child_code = (
                "import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); "
                f"open({str(pid_file)!r},'w').write(str(os.getpid())); time.sleep(60)"
            )
            job = self.job(
                f"import subprocess,time; p=subprocess.Popen([{sys.executable!r},'-c',{child_code!r}]); time.sleep(60)"
            )
            job.running()
            deadline = time.monotonic() + 5
            while not pid_file.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            descendant = int(pid_file.read_text())
            job.send({"event": "cancel"})
            result = job.result()
            self.assertTrue(result["cancelled"])
            self.assertEqual(result["reason"], "cancel")
            # An orphan can briefly remain as an init-owned zombie; it must not run.
            stat = Path(f"/proc/{descendant}/stat")
            try:
                self.assertTrue(stat.read_text().rsplit(") ", 1)[1].startswith("Z "))
            except FileNotFoundError:
                pass

    def test_eof_and_missing_heartbeat_cancel(self):
        for eof in (True, False):
            with self.subTest(eof=eof):
                job = self.job("import time; time.sleep(60)", lease_seconds=1)
                job.running()
                if eof:
                    job.process.stdin.close()
                result = job.result()
                self.assertTrue(result["cancelled"])
                self.assertEqual(result["reason"], "eof" if eof else "lease-expired")

    def test_heartbeats_keep_long_command_alive(self):
        job = self.job("import time; time.sleep(2)", lease_seconds=1)
        job.running()
        for _ in range(5):
            time.sleep(0.3)
            job.send({"event": "ping"})
        self.assertEqual(job.result()["exit_code"], 0)

    def test_transport_signals_clean_up_owned_child(self):
        for number in (signal.SIGHUP, signal.SIGTERM):
            with self.subTest(signal=number):
                job = self.job("import time; time.sleep(60)")
                child = job.running()
                job.process.send_signal(number)
                result = job.result()
                self.assertTrue(result["cancelled"])
                self.assertEqual(result["reason"], "signal")
                self.assertEqual(job.transport_exit, 130)
                self.assertFalse(Path(f"/proc/{child}").exists())

    def test_drains_large_output_without_deadlock(self):
        job = self.job("import sys; sys.stdout.write('x'*200000)")
        job.running()
        self.assertEqual(job.result()["exit_code"], 0)
        self.assertEqual(sum(len(row.get("text", "")) for row in job.events), 200000)


if __name__ == "__main__":
    unittest.main()
