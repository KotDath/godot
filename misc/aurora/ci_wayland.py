#!/usr/bin/env python3
"""Render the CI native-chain fixture on an isolated Linux Wayland compositor."""

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def session(config):
    output = Path(config["output"])
    env = dict(os.environ)
    env.update(
        WAYLAND_DISPLAY="godot-ci",
        LIBGL_ALWAYS_SOFTWARE="1",
        AURORA_CI_SCREENSHOT=str(output / "frame.png"),
    )
    with (output / "compositor.log").open("w") as log:
        compositor = subprocess.Popen(
            [
                "kwin_wayland",
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
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 25
            socket = Path(env["XDG_RUNTIME_DIR"]) / env["WAYLAND_DISPLAY"]
            while not socket.is_socket():
                if compositor.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Private compositor failed; inspect compositor.log")
                time.sleep(0.1)
            command = [
                config["editor"],
                "--display-driver",
                "wayland",
                "--rendering-driver",
                "opengl3",
                "--rendering-method",
                "gl_compatibility",
                "--audio-driver",
                "Dummy",
                "--windowed",
                "--resolution",
                "960x600",
                "--path",
                config["project"],
                "--quit-after",
                "180",
            ]
            with (output / "runtime.log").open("w") as log:
                result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90)
            content = (output / "runtime.log").read_text()
            if (
                result.returncode
                or "AURORA_GDEXTENSION_CHAIN_OK" not in content
                or "AURORA_CI_FRAME:Wayland:0" not in content
            ):
                raise RuntimeError("Linux Wayland/3D/native-chain regression failed; inspect runtime.log")
            if not (output / "frame.png").is_file():
                raise RuntimeError("No rendered viewport capture")
            (output / "result.json").write_text(
                json.dumps(
                    {
                        "scope": "Linux Wayland 3D + native42; not Aurora acceptance",
                        "compositor": "private KWin virtual output and D-Bus",
                        "software_gl": True,
                        "command": command,
                        "returncode": result.returncode,
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
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = Path(tempfile.mkdtemp(prefix="godot-ci-"))
    env = dict(os.environ)
    for name in (
        "DISPLAY",
        "WAYLAND_DISPLAY",
        "DBUS_SESSION_BUS_ADDRESS",
        "DBUS_STARTER_ADDRESS",
        "DBUS_STARTER_BUS_TYPE",
        "XDG_SESSION_ID",
        "XDG_SEAT",
        "XDG_VTNR",
        "SESSION_MANAGER",
    ):
        env.pop(name, None)
    env.update(
        XDG_RUNTIME_DIR=str(runtime),
        XDG_CONFIG_HOME=str(runtime / "config"),
        XDG_DATA_HOME=str(runtime / "data"),
        XDG_CACHE_HOME=str(runtime / "cache"),
        QT_QPA_PLATFORM="offscreen",
    )
    config = {"output": str(output), "editor": str(args.editor.resolve()), "project": str(args.project.resolve())}
    helper = (
        "import json,sys;sys.path.insert(0,sys.argv[1]);from ci_wayland import session;session(json.loads(sys.argv[2]))"
    )
    try:
        with (output / "session.log").open("w") as log:
            process = subprocess.Popen(
                [
                    "dbus-run-session",
                    "--",
                    sys.executable,
                    "-c",
                    helper,
                    str(Path(__file__).parent),
                    json.dumps(config),
                ],
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            try:
                status = process.wait(timeout=140)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
                raise
        if status:
            raise RuntimeError("Private Wayland session failed; inspect " + str(output / "session.log"))
        print("PASS: isolated Linux Wayland 3D frame and native42")
    finally:
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
