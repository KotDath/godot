"""Compile and exercise the actual pre-display Aurora session supervisor."""

import concurrent.futures
import os
import secrets
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).with_name("remote_session.sh").read_text()
DRIVER = r"""
#include "remote_session.h"
#include <sys/prctl.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <string>
int main(int argc, char **argv) {
    int code;
    if (aurora_remote_session_main(argc, argv, code)) return code;
    if (argc == 3 && std::string(argv[1]) == "exit") return std::atoi(argv[2]);
    if (argc < 3 || std::string(argv[1]) != "payload") return 64;
    if (argc > 3 && std::string(argv[3]) == "named") prctl(PR_SET_NAME, "a ) b ( c)");
    if (argc > 3 && std::string(argv[3]) == "descendant") {
        pid_t child = fork();
        if (!child) {
            signal(SIGTERM, SIG_IGN);
            std::ofstream(std::string(argv[2]) + ".child") << getpid() << "\n";
            while (true) pause();
        }
    }
    std::ofstream(argv[2], std::ios::app) << getpid() << "\n";
    while (true) pause();
}
"""


def running(pid):
    try:
        return not Path(f"/proc/{pid}/stat").read_text().rsplit(") ", 1)[1].startswith("Z ")
    except FileNotFoundError:
        return False


class RemoteSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="aurora-session-build-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        source = directory / "driver.cpp"
        source.write_text(DRIVER)
        cls.driver = directory / "driver"
        compiler = shutil.which("c++")
        if not compiler:
            raise unittest.SkipTest("A host C++ compiler is required")
        subprocess.run(
            [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(ROOT / "platform/auroraos"),
                str(source),
                str(ROOT / "platform/auroraos/remote_session.cpp"),
                "-o",
                str(cls.driver),
            ],
            check=True,
            capture_output=True,
        )

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="aurora-session-home-")
        self.home = Path(self.directory.name)
        self.environment = {**os.environ, "HOME": str(self.home)}
        self.tokens = []

    def tearDown(self):
        for token in self.tokens:
            self.request("stop", token)
        self.directory.cleanup()

    def token(self):
        token = secrets.token_hex(16)
        self.tokens.append(token)
        return token

    def session(self, token):
        return self.home / ".cache/godot-aurora/sessions" / token

    def command(self, action, token, *arguments):
        return ["sh", "-c", SCRIPT, "--", action, token, *map(str, arguments)]

    def request(self, action, token, *arguments):
        result = subprocess.run(
            self.command(action, token, *arguments),
            env=self.environment,
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
        fields = result.stdout.strip().split(":")
        self.assertEqual(fields[:2], ["GODOT_AURORA_SESSION", token])
        self.assertEqual(len(fields), 6)
        return {"state": fields[2], "boot": fields[3], "pid": int(fields[4]), "start": fields[5]}

    def wait_file(self, path):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size:
                return path.read_text()
            time.sleep(0.02)
        self.fail(f"Missing fixture evidence: {path.name}")

    def wait_state(self, token, state):
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            result = self.request("status", token)
            if result["state"] == state:
                return result
            time.sleep(0.05)
        self.fail(f"Expected {state}, got {result}")

    def test_lost_launch_reply_recovers_exact_process(self):
        token = self.token()
        marker = self.home / "literal path ' $ 😀.pid"
        client = subprocess.Popen(
            self.command("launch", token, self.driver, "payload", marker, "named"),
            env=self.environment,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            pid = int(self.wait_file(marker))
            if client.poll() is None:
                client.terminate()
            client.wait(timeout=5)
            result = self.request("status", token)
            self.assertEqual(result["state"], "running")
            self.assertEqual(result["pid"], pid)
            self.assertEqual(self.request("stop", token)["state"], "stopped")
            self.assertFalse(running(pid))
        finally:
            if client.poll() is None:
                client.kill()
            client.wait()

    def test_duplicate_launch_is_idempotent(self):
        token = self.token()
        marker = self.home / "once.pid"
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(self.request, "launch", token, self.driver, "payload", marker) for _ in range(4)]
            for future in futures:
                future.result()
        self.assertEqual(len(self.wait_file(marker).splitlines()), 1)

    def test_stop_fences_a_late_launch(self):
        token = self.token()
        marker = self.home / "must-not-exist"
        self.assertEqual(self.request("stop", token)["state"], "stopped")
        self.assertEqual(self.request("launch", token, self.driver, "payload", marker)["state"], "stopped")
        self.assertFalse(marker.exists())

    def test_stop_cleans_ignoring_descendant(self):
        token = self.token()
        marker = self.home / "parent.pid"
        self.request("launch", token, self.driver, "payload", marker, "descendant")
        child = int(self.wait_file(Path(str(marker) + ".child")))
        self.assertEqual(self.request("stop", token)["state"], "stopped")
        self.assertFalse(running(child))

    def test_forged_or_stale_pid_never_signals_an_unrelated_process(self):
        outsider = subprocess.Popen([str(self.driver), "payload", str(self.home / "outsider.pid")])
        try:
            self.wait_file(self.home / "outsider.pid")
            boot = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
            start = Path(f"/proc/{outsider.pid}/stat").read_text().rsplit(") ", 1)[1].split()[19]
            for recorded_boot, recorded_start in ((boot, start), ("00000000", start), (boot, "1")):
                token = self.token()
                directory = self.session(token)
                directory.mkdir(parents=True)
                (directory / "process").write_text(f"{recorded_boot} {outsider.pid} {recorded_start}\n")
                self.request("stop", token)
                self.assertIsNone(outsider.poll())
        finally:
            outsider.terminate()
            outsider.wait(timeout=5)

    def test_supervisor_survives_hup_and_handles_term(self):
        token = self.token()
        marker = self.home / "signals.pid"
        self.request("launch", token, self.driver, "payload", marker)
        pid = int(self.wait_file(marker))
        manager = int((self.session(token) / "manager").read_text().split()[1])
        os.kill(manager, signal.SIGHUP)
        self.assertEqual(self.request("status", token)["state"], "running")
        os.kill(manager, signal.SIGTERM)
        self.wait_state(token, "exited")
        self.assertFalse(running(pid))

    def test_natural_exit_is_recorded(self):
        token = self.token()
        self.request("launch", token, self.driver, "exit", 7)
        self.wait_state(token, "exited")
        self.assertEqual((self.session(token) / "exit").read_text(), "7\n")

    def test_capability_probe_and_invalid_token_do_not_launch(self):
        result = subprocess.run(
            [str(self.driver), "--aurora-remote-session-version"],
            env=self.environment,
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(result.stdout.strip(), "GODOT_AURORA_REMOTE_SESSION_V1")
        result = subprocess.run(
            [str(self.driver), "--aurora-remote-session", "../invalid"],
            env=self.environment,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 64)
        self.assertFalse((self.home / ".cache").exists())


if __name__ == "__main__":
    unittest.main()
