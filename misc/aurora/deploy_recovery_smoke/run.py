#!/usr/bin/env python3
"""Actual editor/SDK packaging with a fake device; no install or hardware claim."""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_remote_session import DRIVER, ROOT, running

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--editor", type=Path, required=True)
parser.add_argument("--sfdk", type=Path, required=True)
parser.add_argument("--project", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--arch", choices=("arm32", "arm64"), default="arm64")
parser.add_argument(
    "--cases",
    nargs="+",
    default=[
        "recover_run",
        "restore_stop",
        "install_loss",
        "gate",
        "late_launch",
        "journal_block",
        "install_recover",
        "runtime_stale",
        "pending_upgrade",
        "api_unknown",
        "no_api_confirmed",
        "missing_recover",
        "version_mismatch",
        "missing_stale",
    ],
)
args = parser.parse_args()
if args.output.exists():
    parser.error("Use a new output directory")
args.output = args.output.resolve()
args.output.mkdir(parents=True)
args.editor = args.editor.resolve()
args.sfdk = args.sfdk.resolve()
source = args.output / "driver.cpp"
source.write_text(DRIVER)
driver = args.output / "driver"
subprocess.run(
    [
        "c++",
        "-std=c++17",
        "-I",
        str(ROOT / "platform/auroraos"),
        str(source),
        str(ROOT / "platform/auroraos/remote_session.cpp"),
        "-o",
        str(driver),
    ],
    check=True,
)
fake = args.output / "fake sdk/sfdk"
fake.parent.mkdir()
shutil.copy2(Path(__file__).with_name("fake_sfdk.py"), fake)
fake.chmod(0o755)
signing = args.sfdk.parent.parent / "package-signing"
summary = []

for case_name in args.cases:
    case = args.output / case_name
    case.mkdir()
    project = case / "project"
    shutil.copytree(args.project, project, ignore=shutil.ignore_patterns(".godot"))
    addon = project / "addons/aurora_recovery_smoke"
    addon.mkdir(parents=True)
    for name in ("plugin.cfg", "plugin.gd"):
        shutil.copy2(Path(__file__).with_name(name), addon / name)
    with (project / "project.godot").open("a") as config:
        config.write('\n[editor_plugins]\nenabled=PackedStringArray("res://addons/aurora_recovery_smoke/plugin.cfg")\n')
    blocks = re.split(r"(?m)(?=^\[preset\.[0-9]+\]$)", (project / "export_presets.cfg").read_text())
    matching = [
        block for block in blocks if re.search(r'^binary_format/architecture="' + args.arch + '"$', block, re.MULTILINE)
    ]
    assert len(matching) == 1, "Prepare exactly one Aurora preset for the selected architecture"
    presets = re.sub(
        r"(?m)^\[preset\.[0-9]+(\.options)?\]$", lambda match: "[preset.0" + (match[1] or "") + "]", matching[0]
    )
    presets = re.sub(r"^sdk/sfdk=.*$", 'sdk/sfdk="' + str(fake) + '"', presets, flags=re.MULTILINE)
    presets += "\nremote_deploy/enabled=true\nsigning/enabled=true\n"
    if case_name == "gate":
        original = Path(re.search(r'^custom_template/debug="(.*)"$', presets, re.MULTILINE).group(1))
        unsupported = case / "no-session-runtime"
        marker = b"GODOT_AURORA_REMOTE_SESSION_V1"
        binary = original.read_bytes()
        assert marker in binary
        unsupported.write_bytes(binary.replace(marker, b"X" * len(marker)))
        presets = re.sub(
            r"^custom_template/debug=.*$",
            'custom_template/debug="' + str(unsupported) + '"',
            presets,
            flags=re.MULTILINE,
        )
    (project / "export_presets.cfg").write_text(presets)
    events = case / "events.jsonl"
    device_home = case / "device-home"
    device_home.mkdir()
    app_marker = case / "launched.pid"
    env = {
        **os.environ,
        "LANG": "en_US.UTF-8",
        "LC_ALL": "en_US.UTF-8",
        "XDG_CONFIG_HOME": str(case / "config"),
        "XDG_DATA_HOME": str(case / "data"),
        "XDG_CACHE_HOME": str(case / "cache"),
        "AURORA_TEST_SIGNING_KEY": str(signing / "regular_key.pem"),
        "AURORA_TEST_SIGNING_CERTIFICATE": str(signing / "regular_cert.pem"),
        "AURORA_FAKE_REAL_SFDK": str(args.sfdk),
        "AURORA_FAKE_EVENTS": str(events),
        "AURORA_FAKE_DRIVER": str(driver),
        "AURORA_FAKE_DEVICE_HOME": str(device_home),
        "AURORA_FAKE_APP_MARKER": str(app_marker),
        "AURORA_FAKE_JOURNAL": str(project / ".godot/editor/aurora-remote-session.json"),
        "AURORA_FAKE_MACHINE": "armv7l" if args.arch == "arm32" else "aarch64",
        "AURORA_FAKE_FAILURE": {
            "recover_run": "launch",
            "late_launch": "late",
            "install_loss": "install",
            "install_recover": "install_committed",
            "runtime_stale": "stale",
            "pending_upgrade": "pending",
            "api_unknown": "api_unknown",
            "no_api_confirmed": "api_unknown_confirmed",
            "missing_recover": "before_dispatch",
            "version_mismatch": "version",
            "missing_stale": "before_dispatch",
        }.get(case_name, "none"),
    }
    modes = {
        "recover_run": ["launch_loss", "recover_run"],
        "late_launch": ["launch_loss", "restore_stop"],
        "restore_stop": ["success", "restore_stop"],
        "install_recover": ["install_pending", "install_recover"],
        "runtime_stale": ["activation_pending", "activation_recover"],
        "pending_upgrade": ["activation_pending", "activation_recover"],
        "api_unknown": ["install_loss"],
        "no_api_confirmed": ["success_stop"],
        "missing_recover": ["launch_loss", "recover_run"],
        "version_mismatch": ["activation_pending", "activation_recover"],
        "missing_stale": ["launch_loss", "activation_loss"],
    }.get(case_name, [case_name])
    evidence = []
    try:
        for index, mode in enumerate(modes):
            if index == 1 and case_name == "missing_stale":
                saved = json.loads((project / ".godot/editor/aurora-remote-session.json").read_text())
                library = next(name for name in saved["receipt"]["files"] if "/lib/" in name and name.endswith(".so"))
                (device_home / "active-runtime" / library.lstrip("/")).write_bytes(
                    b"runtime changed after the first check"
                )
            if index == 1 and case_name in ("runtime_stale", "pending_upgrade", "version_mismatch"):
                shutil.copytree(device_home / "staged-runtime", device_home / "active-runtime", dirs_exist_ok=True)
                installed = json.loads((device_home / "installed.json").read_text())
                installed["pending"] = False
                saved = json.loads((project / ".godot/editor/aurora-remote-session.json").read_text())
                installed["version"] = saved["receipt"]["version"] + "-" + saved["receipt"]["release"]
                (device_home / "installed.json").write_text(json.dumps(installed))
            result_path = case / f"{index}-{mode}.json"
            with (case / f"{index}-{mode}.log").open("w") as log:
                result = subprocess.run(
                    [str(args.editor), "--headless", "--editor", "--path", str(project)],
                    env={**env, "GODOT_AURORA_RECOVERY_TEST": mode, "AURORA_TEST_RESULT": str(result_path)},
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=180,
                )
            assert result.returncode == 0 and result_path.exists(), f"Inspect {case / f'{index}-{mode}.log'}"
            value = json.loads(result_path.read_text())
            assert value["matched"], value
            evidence.append(value)
            if mode in ("install_pending", "activation_pending"):
                saved = value["journal"]
                assert not app_marker.exists()
                assert (Path(saved["staging"]) / saved["rpm"]).is_file()
                before = [json.loads(line)["kind"] for line in events.read_text().splitlines()]
                if case_name in ("pending_upgrade", "version_mismatch"):
                    assert "hash" not in before, before
                elif case_name == "runtime_stale":
                    assert "hash" in before, before
        operations = [json.loads(line)["kind"] for line in events.read_text().splitlines()] if events.exists() else []
        if case_name == "gate":
            assert not operations, operations
            assert "Rebuild the Aurora debug export template" in (case / "0-gate.log").read_text()
        elif case_name == "journal_block":
            assert "deploy" not in operations and "launch" not in operations
            assert operations.count("engine") >= 6
            assert "Could not save the remote operation record" in (case / "0-journal_block.log").read_text()
            blocked = project / ".godot/editor/aurora-remote-session.json"
            assert blocked.is_dir()
            blocked.rmdir()
        else:
            assert operations.count("deploy") == 1, operations
            assert operations.count("payload_verified") == 1, operations
            if case_name in (
                "recover_run",
                "restore_stop",
                "install_recover",
                "runtime_stale",
                "pending_upgrade",
                "no_api_confirmed",
                "missing_recover",
                "version_mismatch",
            ):
                expected_launch_requests = 2 if case_name == "missing_recover" else 1
                assert operations.count("launch") == expected_launch_requests and operations.count("stop") == 1, (
                    operations
                )
                if case_name == "recover_run":
                    assert operations.count("status") == 1
                if case_name != "no_api_confirmed":
                    assert evidence[-1]["restored_stop"]
                assert len(app_marker.read_text().splitlines()) == 1
                assert not running(int(app_marker.read_text()))
                if len(evidence) >= 2:
                    assert not Path(evidence[0]["journal"]["staging"]).exists(), (
                        "Confirmed owned staging was not cleaned"
                    )
            if case_name in ("install_loss", "api_unknown", "missing_stale"):
                assert not app_marker.exists()
                if case_name == "missing_stale":
                    assert operations.count("launch") == 1 and operations.count("stop") == 1
                else:
                    assert "launch" not in operations
                archives = list((project / ".godot/editor").glob("aurora-install-uncertain-*.json"))
                assert len(archives) == 1
                saved = json.loads(archives[0].read_text())
                if case_name == "missing_stale":
                    assert saved["runtime_verified"] is False and saved["install_unconfirmed"] is False
                rpm = Path(saved["staging"]) / saved["rpm"]
                assert rpm.is_file() and rpm.stat().st_size > 1024
            if case_name == "late_launch":
                deadline = time.monotonic() + 20
                late_result = case / "late-result.json"
                while not late_result.exists() and time.monotonic() < deadline:
                    time.sleep(0.1)
                assert late_result.exists(), "Queued fixture request did not finish"
                late = json.loads(late_result.read_text())
                assert late["code"] == 0 and ":stopped:" in late["stdout"], late
                assert not app_marker.exists()
                assert (
                    operations.count("deploy") == 1
                    and operations.count("launch") == 1
                    and operations.count("stop") == 1
                )
        assert not (project / ".godot/editor/aurora-remote-session.json").exists()
        summary.append({
            "case": case_name,
            "arch": args.arch,
            "evidence": evidence,
            "operations": operations,
            "hardware": False,
        })
        print("PASS:", case_name, flush=True)
    finally:
        # Only fixture token directories are touched, including a failed test.
        script = (ROOT / "misc/aurora/remote_session.sh").read_text()
        tokens = {session.name for session in (device_home / ".cache/godot-aurora/sessions").glob("*")}
        journal = project / ".godot/editor/aurora-remote-session.json"
        if journal.is_file():
            tokens.add(json.loads(journal.read_text())["token"])
        for token in tokens:
            subprocess.run(
                ["sh", "-c", script, "--", "stop", token],
                env={**os.environ, "HOME": str(device_home)},
                capture_output=True,
                timeout=10,
            )
(args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
