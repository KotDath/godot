#!/usr/bin/env python3
"""Run the actual ARM pre-display supervisor inside an SDK target, without a device."""

import argparse
import hashlib
import json
import secrets
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sfdk", required=True)
parser.add_argument("--sdk-target", required=True)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True, help="New directory inside the SDK shared workspace")
args = parser.parse_args()
if args.output.exists():
    parser.error("Use a new output directory")
args.output = args.output.resolve()
args.output.mkdir(parents=True)
args.binary = args.binary.resolve()
prefix = [args.sfdk, "engine", "exec", "sb2", "-t", args.sdk_target, "-m", "sdk-build"]
protocol = "GODOT_AURORA_REMOTE_SESSION_V1"
probe = subprocess.run(
    prefix + [str(args.binary), "--aurora-remote-session-version"],
    text=True,
    capture_output=True,
    check=True,
    timeout=30,
)
assert probe.stdout.strip() == protocol
token = secrets.token_hex(16)
fenced_token = secrets.token_hex(16)
script = """task_home=$1; token=$2; fenced_token=$3; binary=$4
HOME="$task_home" "$binary" --aurora-remote-session "$token" --aurora-remote-session-version || exit
session="$task_home/.cache/godot-aurora/sessions/$token"
count=0
while test ! -f "$session/exit" && test "$count" -lt 10; do sleep 1; count=$((count+1)); done
test -f "$session/exit" || exit 70
cat "$session/exit"
cat "$task_home/.cache/godot-aurora/$token.log"
fenced="$task_home/.cache/godot-aurora/sessions/$fenced_token"
mkdir "$fenced" || exit
: > "$fenced/stop" || exit
HOME="$task_home" "$binary" --aurora-remote-session "$fenced_token" --aurora-remote-session-version || exit
sleep 1
test ! -f "$fenced/process"
"""
result = subprocess.run(
    prefix + ["sh", "-c", script, "--", str(args.output), token, fenced_token, str(args.binary)],
    text=True,
    capture_output=True,
    check=True,
    timeout=30,
)
assert result.stdout.splitlines() == ["0", protocol], result.stdout
session = args.output / ".cache/godot-aurora/sessions" / token
assert (session / "exit").read_text() == "0\n"
for name in ("manager", "process"):
    fields = (session / name).read_text().split()
    assert len(fields) == 3 and int(fields[1]) > 1 and int(fields[2]) > 0
evidence = {
    "binary": str(args.binary),
    "binary_sha256": hashlib.sha256(args.binary.read_bytes()).hexdigest(),
    "sdk_target": args.sdk_target,
    "protocol": protocol,
    "native_fork_exec_exit": 0,
    "stop_fenced_late_launch": True,
    "hardware": False,
    "sdk_diagnostics": probe.stderr + result.stderr,
}
(args.output / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
print("PASS: actual ARM supervisor fork/exec/exit and Stop fence inside SDK; no hardware claim")
