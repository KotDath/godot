#!/usr/bin/env python3
"""Test-only sfdk frontend: real engine, fake device with the actual supervisor."""

import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

arguments = sys.argv[1:]
events = Path(os.environ["AURORA_FAKE_EVENTS"])
device_home = Path(os.environ["AURORA_FAKE_DEVICE_HOME"])
device_state = device_home / "installed.json"
active_root = device_home / "active-runtime"
staged_root = device_home / "staged-runtime"
failure = os.environ["AURORA_FAKE_FAILURE"]


def event(kind):
    # Never log stdin (protected-key protocol), complete command lines or env.
    with events.open("a") as stream:
        stream.write(json.dumps({"kind": kind}) + "\n")


def extract_exported_runtime(rpm):
    record = json.loads(Path(os.environ["AURORA_FAKE_JOURNAL"]).read_text())
    receipt = record["receipt"]
    staged_root.mkdir()
    producer = subprocess.Popen(["rpm2cpio", rpm], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        extracted = subprocess.run(
            ["cpio", "-idm", "--quiet", "--no-absolute-filenames"],
            cwd=staged_root,
            stdin=producer.stdout,
            capture_output=True,
        )
        producer.stdout.close()
        assert extracted.returncode == 0 and producer.wait(timeout=20) == 0
    finally:
        if producer.poll() is None:
            producer.kill()
            producer.wait()
        producer.stderr.close()
    packaged = json.loads((staged_root / "usr/share" / receipt["package"] / "runtime-receipt.json").read_text())
    assert packaged == receipt
    # Independently inspect final RPM payload bytes, after RPM post-processing
    # and signing. The fake must not merely echo declared expected hashes.
    for name, digest in receipt["files"].items():
        assert hashlib.sha256((staged_root / name.lstrip("/")).read_bytes()).hexdigest() == digest, name
    event("payload_verified")
    if failure in ("install", "api_unknown"):
        return
    shutil.copytree(staged_root, active_root)
    state = {
        "id": receipt["package"],
        "version": receipt["version"] + "-" + receipt["release"],
        "pending": failure == "pending",
    }
    if failure == "version":
        state["version"] = "0.0.0-older"
    device_state.write_text(json.dumps(state))
    if failure == "stale":
        library = next(name for name in receipt["files"] if "/lib/" in name and name.endswith(".so"))
        (active_root / library.lstrip("/")).write_bytes(b"previous library version")


if arguments[:2] == ["engine", "exec"]:
    event("engine")
    executable = os.environ["AURORA_FAKE_REAL_SFDK"]
    os.execv(executable, [executable, *arguments])
elif len(arguments) > 2 and arguments[0] == "-c" and arguments[2] == "deploy":
    event("deploy")
    extract_exported_runtime(arguments[-1])
    if failure in ("install", "install_committed", "api_unknown"):
        print("Fixture: installation reply lost", file=sys.stderr)
        sys.exit(255)
    sys.exit(0)
elif arguments[:4] == ["device", "exec", "Fixture device", "--"]:
    command = arguments[4:]
    if command == ["uname", "-m"]:
        event("uname")
        print(os.environ["AURORA_FAKE_MACHINE"])
        sys.exit(0)
    if command[0] == "gdbus":
        method = command[command.index("--method") + 1]
        event("pending" if method.endswith("CheckPackageHasPendingUpgrade") else "package")
        print("sfdk: fixture warning belongs to stderr, not the GVariant result", file=sys.stderr)
        if failure in ("api_unknown", "api_unknown_confirmed"):
            print("GDBus.Error:org.freedesktop.DBus.Error.UnknownMethod: unavailable", file=sys.stderr)
            sys.exit(1)
        if not device_state.exists():
            print("GDBus.Error:ru.omp.APM.Error.PackageNotExist: not registered", file=sys.stderr)
            sys.exit(1)
        state = json.loads(device_state.read_text())
        if method.endswith("CheckPackageHasPendingUpgrade"):
            print("(true,)" if state["pending"] else "(false,)")
        else:
            print(
                repr((
                    {"general.id": state["id"], "general.version": state["version"], "certificate.subgroup": "regular"},
                ))
            )
        sys.exit(0)
    if command[:2] == ["sha256sum", "--"]:
        event("hash")
        failed = False
        for name in command[2:]:
            path = active_root / name.lstrip("/")
            if not path.is_file():
                print("Fixture: active file unavailable", file=sys.stderr)
                failed = True
            else:
                print(hashlib.sha256(path.read_bytes()).hexdigest() + "  " + name)
        sys.exit(1 if failed else 0)
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
        dropped = events.parent / "launch-reply-dropped"
        if failure == "before_dispatch" and not dropped.exists():
            dropped.touch()
            print("Fixture: connection lost before native launch dispatch", file=sys.stderr)
            sys.exit(255)
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
