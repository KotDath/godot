#!/usr/bin/env python3
"""Build the WSI probe; test native Linux in a private KWin virtual session.

An SDK target builds only: cross-compilation is not a device/renderer result.
The host test requires Khronos validation (including synchronization validation).
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from build import refresh_sdk_sources

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "misc/aurora/vulkan_wsi_probe.c"
PROTOCOL = ROOT / "thirdparty/wayland-protocols/stable/xdg-shell/xdg-shell.xml"
RESULT = re.compile(
    r"^WSI_RESULT:(PASS|FAIL):exit=(\d+):presented=(\d+):generations=(\d+)"
    r":readbacks=(\d+):callbacks=(\d+):validation_errors=(\d+)$",
    re.MULTILINE,
)


def build_probe(output, sdk, compiler, scanner, source=SOURCE):
    subprocess.run([scanner, "client-header", str(PROTOCOL), str(output / "xdg-shell-client.h")], check=True)
    subprocess.run([scanner, "private-code", str(PROTOCOL), str(output / "xdg-shell-code.c")], check=True)
    binary = output / "probe"
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I" + str(ROOT / "thirdparty/vulkan/include"),
        "-I" + str(output),
        str(source),
        str(output / "xdg-shell-code.c"),
        "-lwayland-client",
        "-ldl",
        "-o",
        str(binary),
    ]
    with (output / "build.log").open("w") as log:
        subprocess.run(sdk + command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
    elf = subprocess.check_output(["readelf", "-h", str(binary)], text=True)
    dynamic = subprocess.check_output(["readelf", "-d", str(binary)], text=True)
    assert "libvulkan.so" not in dynamic and "libQt" not in dynamic, dynamic
    manifest = {
        "scope": "standalone diagnostic, not Godot engine acceptance",
        "command": sdk + command,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "protocol_sha256": hashlib.sha256(PROTOCOL.read_bytes()).hexdigest(),
        "elf_header": elf,
        "elf_dynamic": dynamic,
        "hardware_tested": False,
    }
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return binary


def result_values(content):
    matches = RESULT.findall(content)
    assert len(matches) == 1, content
    state, *values = matches[0]
    keys = ("exit", "presented", "generations", "readbacks", "callbacks", "validation_errors")
    return {"state": state, **dict(zip(keys, map(int, values)))}


def run_session(config):
    output = Path(config["output"])
    env = dict(os.environ)
    env["WAYLAND_DISPLAY"] = "wsi-probe"
    records = []
    with (output / "compositor.log").open("w") as log:
        compositor = subprocess.Popen(
            [
                config["compositor"],
                "--virtual",
                "--no-lockscreen",
                "--no-global-shortcuts",
                "--no-kactivities",
                "--socket",
                env["WAYLAND_DISPLAY"],
                "--width",
                "960",
                "--height",
                "600",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 25
            socket = Path(env["XDG_RUNTIME_DIR"]) / env["WAYLAND_DISPLAY"]
            while not socket.is_socket():
                assert compositor.poll() is None, f"Inspect {output / 'compositor.log'}"
                assert time.monotonic() < deadline, "No private Wayland socket"
                time.sleep(0.1)
            if config["layer_dir"]:
                env["VK_LAYER_PATH"] = config["layer_dir"]
            if config["library_dir"]:
                env["LD_LIBRARY_PATH"] = config["library_dir"] + os.pathsep + env.get("LD_LIBRARY_PATH", "")
            env["VK_VALIDATION_VALIDATE_SYNC"] = "1"
            env["VK_VALIDATION_VALIDATE_CORE"] = "1"
            env["VK_VALIDATION_THREAD_SAFETY"] = "1"
            cases = [("default", None)]
            if config["software_icd"]:
                cases.append(("software", config["software_icd"]))
            for name, icd in cases:
                current = dict(env)
                if icd:
                    current["VK_DRIVER_FILES"] = icd
                command = [
                    config["binary"],
                    "--validation",
                    "--require-readback",
                    "--require-present-fence",
                    "--frames",
                    "60",
                    "--timeout",
                    "25",
                ]
                path = output / (name + ".log")
                with path.open("w") as log:
                    result = subprocess.run(
                        command, env=current, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=35
                    )
                content = path.read_text()
                values = result_values(content)
                assert result.returncode == values["exit"] == 0 and values["state"] == "PASS", content
                assert values["presented"] == values["readbacks"] == values["callbacks"] == 60, content
                assert values["generations"] >= 2 and values["validation_errors"] == 0, content
                assert "WSI_PRESENT_FENCE:extension" in content and "WSI_RECREATE:forced-not-a-resize-claim" in content
                device = next(line for line in content.splitlines() if line.startswith("WSI_DEVICE:"))
                records.append({"case": name, "device": device, "icd": icd, **values})
                print("PASS:", name, device, flush=True)
            # Real setup failures must not be reported as a partial WSI success.
            for name, executable, extra, expected in (
                ("missing-loader", config["binary"], ["--loader", str(output / "missing-loader.so")], 2),
                ("device-index", config["binary"], ["--device", "255"], 4),
                ("no-compositor", config["binary"], [], 6),
                ("pixel-control", config["negative_binary"], ["--validation", "--require-present-fence"], 5),
            ):
                current = dict(env)
                if name == "no-compositor":
                    current["WAYLAND_DISPLAY"] = "missing-wsi-socket"
                path = output / (name + ".log")
                with path.open("w") as log:
                    result = subprocess.run(
                        [executable, "--timeout", "10", *extra],
                        env=current,
                        cwd=ROOT,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        timeout=15,
                    )
                content = path.read_text()
                values = result_values(content)
                assert result.returncode == values["exit"] == expected and values["state"] == "FAIL", values
                assert values["presented"] == 0, values
                if name == "pixel-control":
                    assert "WSI_PIXEL_MISMATCH:0:" in content and values["generations"] == 1, content
                    assert values["validation_errors"] == 0, content
                records.append({"case": name, **values})
                print("PASS: negative", name, flush=True)
            path = output / "watchdog.log"
            with path.open("w") as log:
                timed = subprocess.run(
                    [config["binary"], "--frames", "10000", "--timeout", "1"],
                    env=env,
                    cwd=ROOT,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=10,
                )
            content = path.read_text()
            assert timed.returncode == 124 and "WSI_TIMEOUT" in content and "WSI_RESULT:PASS" not in content, content
            records.append({"case": "watchdog", "state": "FAIL", "exit": 124})
            print("PASS: negative watchdog", flush=True)
            (output / "result.json").write_text(
                json.dumps(
                    {
                        "scope": "native Linux WSI diagnostic; no Aurora engine/hardware acceptance",
                        "compositor": "private KWin virtual framebuffer, D-Bus and runtime",
                        "sync_validation_requested": True,
                        "cases": records,
                    },
                    indent=2,
                )
                + "\n"
            )
        finally:
            compositor.terminate()
            try:
                compositor.wait(timeout=10)
            except subprocess.TimeoutExpired:
                compositor.kill()
                compositor.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New directory, shared with SDK for target builds")
    parser.add_argument("--sfdk", type=Path)
    parser.add_argument("--sdk-target", help="Exact SDK target/snapshot; implies build-only")
    parser.add_argument("--cc", help="Compiler executable (default: cc on host, gcc in SDK)")
    parser.add_argument("--scanner", default="wayland-scanner", help="Native host protocol generator")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--compositor", default="kwin_wayland")
    parser.add_argument("--layer-dir", type=Path, help="Optional directory with validation layer JSON")
    parser.add_argument("--library-dir", type=Path, help="Optional process-local validation DSO directory")
    parser.add_argument("--software-icd", type=Path, help="Also run using this explicit software ICD JSON")
    args = parser.parse_args()
    if bool(args.sfdk) != bool(args.sdk_target):
        parser.error("Pass --sfdk and --sdk-target together")
    output = args.output.resolve()
    if output.exists():
        parser.error("Use a new output directory")
    output.mkdir(parents=True)
    sdk = []
    if args.sdk_target:
        refresh_sdk_sources(str(args.sfdk.resolve()), ROOT)
        sdk = [str(args.sfdk.resolve()), "engine", "exec", "sb2", "-t", args.sdk_target, "-m", "sdk-build"]
    binary = build_probe(output, sdk, args.cc or ("gcc" if sdk else "cc"), args.scanner)
    if sdk or args.build_only:
        print("PASS: compiled diagnostic only; no rendering/hardware claim", flush=True)
        return
    negative = output / "pixel-control"
    negative.mkdir()
    source = SOURCE.read_text()
    expected = "const uint8_t expected[4][4] = { { 255, 0, 0, 255 }"
    assert source.count(expected) == 1, "Update the readback negative control for this source"
    negative_source = negative / "probe.c"
    negative_source.write_text(source.replace(expected, expected.replace("255, 0, 0, 255", "254, 0, 0, 255"), 1))
    negative_binary = build_probe(negative, [], args.cc or "cc", args.scanner, negative_source)
    runtime = Path(tempfile.mkdtemp(prefix="godot-vk-"))
    env = dict(os.environ)
    # Explicit test settings must not inherit a disabled layer or hidden ICD.
    for name in list(env):
        if name.startswith("VK_"):
            env.pop(name)
    for name in (
        "WAYLAND_DISPLAY",
        "DISPLAY",
        "XDG_SESSION_ID",
        "XDG_SEAT",
        "XDG_VTNR",
        "SESSION_MANAGER",
        "DBUS_SESSION_BUS_ADDRESS",
        "DBUS_STARTER_ADDRESS",
        "DBUS_STARTER_BUS_TYPE",
        "VK_DRIVER_FILES",
        "VK_ICD_FILENAMES",
    ):
        env.pop(name, None)
    env.update(
        XDG_RUNTIME_DIR=str(runtime),
        XDG_CONFIG_HOME=str(output / "config"),
        XDG_DATA_HOME=str(output / "data"),
        XDG_CACHE_HOME=str(output / "cache"),
        QT_QPA_PLATFORM="offscreen",
    )
    config = {
        "output": str(output),
        "binary": str(binary),
        "negative_binary": str(negative_binary),
        "compositor": args.compositor,
        "layer_dir": str(args.layer_dir.resolve()) if args.layer_dir else None,
        "library_dir": str(args.library_dir.resolve()) if args.library_dir else None,
        "software_icd": str(args.software_icd.resolve()) if args.software_icd else None,
    }
    helper = (
        "import json,sys;sys.path.insert(0,sys.argv[1]);"
        "from vulkan_wsi_smoke import run_session;run_session(json.loads(sys.argv[2]))"
    )
    try:
        with (output / "session.log").open("w") as log:
            session = subprocess.Popen(
                [
                    "dbus-run-session",
                    "--",
                    sys.executable,
                    "-c",
                    helper,
                    str(Path(__file__).parent),
                    json.dumps(config),
                ],
                cwd=ROOT,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            try:
                returncode = session.wait(timeout=160)
            except subprocess.TimeoutExpired:
                # wait(timeout) has not reaped this live group leader.
                os.killpg(session.pid, signal.SIGKILL)
                session.wait()
                raise
        assert returncode == 0, f"Inspect {output / 'session.log'}"
        print("PASS: native Linux WSI and setup-error checks; evidence:", output, flush=True)
    finally:
        # A private document portal can finish unmounting just after D-Bus exits.
        for attempt in range(10):
            try:
                shutil.rmtree(runtime)
                break
            except OSError:
                if attempt == 9:
                    raise
                time.sleep(0.2)


if __name__ == "__main__":
    main()
