#!/usr/bin/env python3
"""Observe the real compositor's activation contract with a standalone probe.

This does not run Godot or establish its editor/hardware acceptance. It opens
only disposable probe windows. Native session supervision owns their cleanup.
"""

import argparse
import hashlib
import json
import secrets
import shlex
import subprocess
import time
from pathlib import Path

from build import refresh_sdk_sources

ROOT = Path(__file__).resolve().parents[2]
DRIVER = """#include "remote_session.h"
extern "C" int activation_probe_main(int, char **);
int main(int argc, char **argv) {
    int code;
    if (aurora_remote_session_main(argc, argv, code)) return code;
    return activation_probe_main(argc, argv);
}
"""


def capture(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    return result.stdout


def build_probe(sfdk, target, scanner, output):
    refresh_sdk_sources(str(sfdk), ROOT)
    (output / "driver.cpp").write_text(DRIVER)
    protocol = ROOT / "platform/auroraos/protocol/surface-extension.xml"
    capture([scanner, "client-header", str(protocol), str(output / "surface_extension.h")])
    capture([scanner, "private-code", str(protocol), str(output / "surface_extension.c")])
    sdk = [str(sfdk), "engine", "exec", "sb2", "-t", target, "-m", "sdk-build"]
    cflags = shlex.split(capture(sdk + ["pkg-config", "--cflags", "wayland-client"]))
    libraries = shlex.split(capture(sdk + ["pkg-config", "--libs", "wayland-client"]))
    binary = output / "activation-probe"
    commands = [
        [
            "gcc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Werror",
            *cflags,
            "-I" + str(output),
            "-Dmain=activation_probe_main",
            "-c",
            str(ROOT / "misc/aurora/window_activation_probe.c"),
            "-o",
            str(output / "probe.o"),
        ],
        ["gcc", *cflags, "-c", str(output / "surface_extension.c"), "-o", str(output / "protocol.o")],
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I" + str(ROOT / "platform/auroraos"),
            str(output / "driver.cpp"),
            str(ROOT / "platform/auroraos/remote_session.cpp"),
            str(output / "probe.o"),
            str(output / "protocol.o"),
            *libraries,
            "-o",
            str(binary),
        ],
    ]
    with (output / "build.log").open("w") as log:
        for command in commands:
            subprocess.run(sdk + command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=90, check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sfdk", type=Path, required=True)
    parser.add_argument("--sdk-target", required=True, help="Exact SDK target/snapshot with wayland-devel")
    parser.add_argument("--device", required=True, help="sfdk device name")
    parser.add_argument("--audb-device", required=True, help="Matching audb device profile, used for setup")
    parser.add_argument("--audb", default="audb")
    parser.add_argument("--scanner", default="wayland-scanner", help="Native host protocol generator")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--modes",
        nargs="+",
        choices=("none", "before", "visibility", "frame"),
        default=["none", "before", "visibility", "frame"],
    )
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Use a new output directory")
    output.mkdir(parents=True)
    sfdk = args.sfdk.resolve()
    audb = [args.audb, "-d", args.audb_device, "--json", "--command-timeout", "20"]
    info = json.loads(capture(audb + ["info"]))
    display = json.loads(capture(audb + ["display", "status"]))
    assert info["ok"] and display["ok"]
    assert display["data"]["display"] == "on" and display["data"]["lockMode"] == "unlocked", (
        "Wake/unlock the test device first"
    )
    (output / "device.json").write_text(json.dumps({"info": info, "display": display}, indent=2) + "\n")
    binary = build_probe(sfdk, args.sdk_target, args.scanner, output)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    prefix = [str(sfdk), "device", "exec", args.device, "--"]
    remote = "/tmp/godot-activation-probe-" + secrets.token_hex(16)
    script = (ROOT / "misc/aurora/remote_session.sh").read_text()
    summary = []
    cleanup_errors = []

    def device(*command):
        return capture(prefix + list(command))

    def request(action, token, *arguments):
        fields = device("sh", "-c", script, "--", action, token, *arguments).strip().split(":")
        assert len(fields) == 6 and fields[:2] == ["GODOT_AURORA_SESSION", token], fields
        return fields[2]

    def events(token, path):
        raw = device(
            "sh",
            "-c",
            'test ! -e "$HOME/.cache/godot-aurora/$1.log" || cat "$HOME/.cache/godot-aurora/$1.log"',
            "--",
            token,
        )
        path.write_text(raw)
        return [json.loads(line) for line in raw.splitlines() if line.startswith("{")]

    def wait_for(token, path, predicate):
        deadline = time.monotonic() + 6
        while True:
            values = events(token, path)
            if predicate(values):
                return values
            assert time.monotonic() < deadline, f"Inspect {path}"
            time.sleep(0.15)

    def focus(values):
        return [value["value"] for value in values if value["event"] == "focus"]

    try:
        pushed = json.loads(capture(audb + ["push", str(binary), remote]))
        assert pushed["ok"]
        device("chmod", "700", remote)
        assert device("sha256sum", "--", remote).split()[0] == digest
        assert device(remote, "--aurora-remote-session-version").strip() == "GODOT_AURORA_REMOTE_SESSION_V1"
        machine = device("uname", "-m").strip()
        for mode in args.modes:
            case = output / mode
            case.mkdir()
            parent, child = secrets.token_hex(16), secrets.token_hex(16)
            control = "/tmp/godot-activation-raise-" + parent
            app_id = "org.godotengine.aurora_activation_probe_" + parent[:12]
            print("START:", mode, flush=True)
            try:
                assert request("launch", parent, remote, "parent", "visibility", "45", app_id, control) == "running"
                wait_for(
                    parent,
                    case / "parent-initial.log",
                    lambda values: focus(values)[-1:] == [1] and any(value["event"] == "raise" for value in values),
                )
                assert request("launch", child, remote, "child", mode, "30", app_id) == "running"
                if mode == "visibility":
                    child_events = wait_for(
                        child,
                        case / "child.log",
                        lambda values: focus(values)[-1:] == [1] and any(value["event"] == "raise" for value in values),
                    )
                    concrete = next(
                        index
                        for index, value in enumerate(child_events)
                        if value["event"] == "visibility" and value["value"] in (0, 2, 3, 4, 5)
                    )
                    assert concrete < next(
                        index for index, value in enumerate(child_events) if value["event"] == "raise"
                    )
                else:
                    # These modes are timing controls. Automatic compositor
                    # focus must not count as proof that an early raise works;
                    # background surfaces need not receive any frame callback.
                    time.sleep(1)
                    child_events = events(child, case / "child.log")
                    ordering = [value["event"] for value in child_events]
                    assert "attached" in ordering
                    if mode == "none":
                        assert "raise" not in ordering
                    elif mode == "before":
                        assert ordering.index("raise") < ordering.index("attached")
                    elif "frame" in ordering:
                        assert ordering.index("frame") < ordering.index("raise")
                    else:
                        assert "raise" not in ordering
                assert request("stop", child) in ("stopped", "exited")
                time.sleep(0.5)
                before = events(parent, case / "parent-before-restore.log")
                device("touch", control)

                def restored(values):
                    requests = [value["ms"] for value in values if value["event"] == "explicit_raise"]
                    visible = [
                        value["value"] for value in values if value["event"] == "visibility" and value["value"] != 1
                    ]
                    if not requests or focus(values)[-1:] != [1] or not visible or visible[-1] in (0, 3):
                        return False
                    return focus(before)[-1:] == [1] or any(
                        value["event"] == "focus" and value["value"] == 1 and value["ms"] >= requests[-1]
                        for value in values
                    )

                after = wait_for(parent, case / "parent-restored.log", restored)
                summary.append({
                    "mode": mode,
                    "child_focused": focus(child_events)[-1:] == [1],
                    "child_raised": any(value["event"] == "raise" for value in child_events),
                    "child_frame_received": any(value["event"] == "frame" for value in child_events),
                    "parent_focused_before_request": focus(before)[-1:] == [1],
                    "parent_restored": focus(after)[-1:] == [1],
                })
                print("PASS:", mode, flush=True)
            finally:
                for role, token in (("child", child), ("parent", parent)):
                    try:
                        state = request("stop", token)
                        assert state in ("stopped", "exited", "failed"), state
                        values = events(token, case / (role + "-final.log"))
                        assert not any(value["event"] == "finished" and value["value"] != 0 for value in values), values
                    except Exception as error:
                        cleanup_errors.append(str(error))
                device("rm", "-f", "--", control)
    finally:
        if not cleanup_errors:
            device("rm", "-f", "--", remote)
    assert not cleanup_errors, cleanup_errors
    result = {
        "scope": "standalone compositor API and native SDK transport; not engine acceptance",
        "engine_tested": False,
        "machine": machine,
        "sdk_target": args.sdk_target,
        "binary_sha256": digest,
        "cases": summary,
    }
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: raw compositor contract; no Godot editor/runtime acceptance claimed", flush=True)


if __name__ == "__main__":
    main()
