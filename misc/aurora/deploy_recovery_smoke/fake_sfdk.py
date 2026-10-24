#!/usr/bin/env python3
"""Test-only sfdk frontend: real engine, fake device with the actual supervisor."""

import json
import os
import subprocess
import sys
from pathlib import Path

arguments = sys.argv[1:]
events = Path(os.environ["AURORA_FAKE_EVENTS"])


def event(kind):
    # Never log stdin (protected-key protocol), complete command lines or env.
    with events.open("a") as stream:
        stream.write(json.dumps({"kind": kind}) + "\n")


if arguments[:2] == ["engine", "exec"]:
    event("engine")
    executable = os.environ["AURORA_FAKE_REAL_SFDK"]
    os.execv(executable, [executable, *arguments])
elif len(arguments) > 2 and arguments[0] == "-c" and arguments[2] == "deploy":
    event("deploy")
    if os.environ["AURORA_FAKE_FAILURE"] == "install":
        print("Fixture: installation reply lost", file=sys.stderr)
        sys.exit(255)
    sys.exit(0)
elif arguments[:4] == ["device", "exec", "Fixture device", "--"]:
    command = arguments[4:]
    if command == ["uname", "-m"]:
        event("uname")
        print("aarch64")
        sys.exit(0)
    assert command[:2] == ["sh", "-c"] and command[3] == "--"
    action = command[4]
    event(action)
    if action == "launch":
        assert command[6].startswith("/usr/bin/")
        command = command[:6] + [
            os.environ["AURORA_FAKE_DRIVER"],
            "payload",
            os.environ["AURORA_FAKE_APP_MARKER"],
            "named",
        ]
        if os.environ["AURORA_FAKE_FAILURE"] == "late":
            code = (
                "import json,subprocess,sys,time; from pathlib import Path; time.sleep(10); "
                "r=subprocess.run(json.loads(sys.argv[1]),capture_output=True,text=True); "
                "p=Path(sys.argv[2]); t=p.with_suffix('.tmp'); "
                "t.write_text(json.dumps({'code':r.returncode,'stdout':r.stdout,'stderr':r.stderr})); t.rename(p)"
            )
            process = subprocess.Popen(
                [sys.executable, "-c", code, json.dumps(command), str(events.parent / "late-result.json")],
                env={**os.environ, "HOME": os.environ["AURORA_FAKE_DEVICE_HOME"]},
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            (events.parent / "late-launch.pid").write_text(str(process.pid))
            print("Fixture: queued launch lost its connection before dispatch", file=sys.stderr)
            sys.exit(255)
    result = subprocess.run(
        command,
        env={**os.environ, "HOME": os.environ["AURORA_FAKE_DEVICE_HOME"]},
        text=True,
        capture_output=True,
    )
    dropped = events.parent / "launch-reply-dropped"
    if action == "launch" and os.environ["AURORA_FAKE_FAILURE"] == "launch" and not dropped.exists():
        dropped.touch()
        print("Fixture: reply lost after actual native launch", file=sys.stderr)
        sys.exit(255)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    sys.exit(result.returncode)
else:
    raise RuntimeError("Unexpected fake SDK request")
