#!/usr/bin/env python3
"""Opt-in tests through a real sfdk engine; no device or signing key required."""

import argparse
import hashlib
import json
import secrets
import signal
import subprocess
from pathlib import Path

from test_sdk_job import Job

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sfdk", required=True)
args = parser.parse_args()
prefix = [args.sfdk, "engine", "exec"]
launcher = prefix + ["python3", "-u", "-c", Path(__file__).with_name("sdk_job.py").read_text()]


def execute(code):
    result = subprocess.run(prefix + ["python3", "-c", code], text=True, capture_output=True, check=True)
    return result.stdout


job = Job(["python3", "-c", "import sys; print('SDK Unicode: ё 😀'); sys.exit(2)"], launcher=launcher)
try:
    job.running()
    assert job.result()["exit_code"] == 2
    assert "Fatal Python error" not in job.diagnostics
    assert "ё 😀" in "".join(event.get("text", "") for event in job.events)
    print("PASS: actual SDK exit status and Unicode")
finally:
    job.close()

# The value is synthetic and remains only in this process and the stdin frame.
secret = secrets.token_urlsafe(32) + "ёж"
digest = hashlib.sha256(secret.encode()).hexdigest()
code = (
    "import os,hashlib,time; p=os.environ['KEY_PASSPHRASE']; "
    f"assert hashlib.sha256(p.encode()).hexdigest()=={digest!r}; "
    "print(p,flush=True); time.sleep(2)"
)
job = Job(["python3", "-c", code], launcher=launcher, secret_environment={"KEY_PASSPHRASE": secret})
try:
    pid = job.running()
    cmdline_hex = execute(f"from pathlib import Path; print(Path('/proc/{pid}/cmdline').read_bytes().hex())").strip()
    assert secret.encode() not in bytes.fromhex(cmdline_hex)
    assert job.result()["exit_code"] == 0
    assert job.transport_exit == 0
    assert "Fatal Python error" not in job.diagnostics
    assert not any(event["event"] == "output" for event in job.events)
    assert secret not in json.dumps(job.events, ensure_ascii=False)
    print("PASS: actual SDK exact secret environment; no secret argv/output")
finally:
    job.close()

child = "import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); print('SDK_DESCENDANT:'+str(os.getpid()),flush=True); time.sleep(60)"
code = f"import subprocess,time; subprocess.Popen(['python3','-c',{child!r}]); time.sleep(60)"
job = Job(["python3", "-c", code], launcher=launcher)
sentinel = Job(["python3", "-c", "import time; time.sleep(60)"], launcher=launcher)
try:
    sentinel_pid = sentinel.running()
    job.running()
    descendant = None
    while descendant is None:
        event = job.next()
        for line in event.get("text", "").splitlines():
            if line.startswith("SDK_DESCENDANT:"):
                descendant = int(line.split(":")[1])
    job.send({"event": "cancel"})
    result = job.result()
    assert result["cancelled"] and result["reason"] == "cancel"
    assert "Fatal Python error" not in job.diagnostics
    check = (
        "from pathlib import Path\n"
        f"p=Path('/proc/{descendant}/stat')\n"
        "try:\n s=p.read_text().rsplit(') ',1)[1]; assert s.startswith('Z '),s\n"
        "except FileNotFoundError:\n pass\n"
    )
    execute(check)
    execute(
        "from pathlib import Path\n"
        f"state=Path('/proc/{sentinel_pid}/stat').read_text().rsplit(') ',1)[1]\n"
        "assert not state.startswith('Z '),state\n"
    )
    print("PASS: actual SDK cancellation kills a SIGTERM-ignoring descendant and preserves an unrelated job")
finally:
    job.close()
    sentinel.close()

for close_stdin in (True, False):
    job = Job(["python3", "-c", "import time; time.sleep(60)"], launcher=launcher, lease_seconds=1)
    try:
        job.running()
        if close_stdin:
            job.process.stdin.close()
        result = job.result()
        assert result["cancelled"] and result["reason"] in ("eof", "lease-expired")
        assert "Fatal Python error" not in job.diagnostics
        print(
            "PASS: actual SDK lost-client cleanup",
            "closed stdin" if close_stdin else "missing heartbeat",
            result["reason"],
        )
    finally:
        job.close()

for number in (signal.SIGHUP, signal.SIGTERM):
    job = Job(["python3", "-c", "import time; time.sleep(60)"], launcher=launcher)
    try:
        child = job.running()
        execute(
            "import os,signal\n"
            f"parent=int(open('/proc/{child}/status').read().split('PPid:')[1].split()[0])\n"
            f"os.kill(parent,{int(number)})\n"
        )
        result = job.result()
        assert result["cancelled"] and result["reason"] == "signal"
        assert "Fatal Python error" not in job.diagnostics
        execute(f"from pathlib import Path; assert not Path('/proc/{child}').exists()")
        print("PASS: actual SDK supervisor signal cleanup", number.name)
    finally:
        job.close()
