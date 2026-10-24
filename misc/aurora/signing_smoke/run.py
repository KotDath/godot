#!/usr/bin/env python3
"""Exercise protected signing and cancellation using public SDK test credentials.

The synthetic passphrase stays in memory/environment/stdin. The encrypted test
key stays beside the SDK credentials, outside the project, and is removed finally.
Existing user keys, certificates, editor settings and RPMs are never overwritten.
"""

import argparse
import json
import os
import secrets
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_sdk_job import Job

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--editor", type=Path, required=True)
parser.add_argument("--sfdk", type=Path, required=True)
parser.add_argument("--project", type=Path, required=True, help="Prepared native-layout smoke project")
parser.add_argument("--output", type=Path, required=True, help="New test artifacts directory")
parser.add_argument("--tool-target", required=True, help="SDK x86_64 target with OpenSSL/GOST")
parser.add_argument("--modes", nargs="+", default=["headless", "wrong", "cancel_prompt", "accept", "cancel_retry"])
args = parser.parse_args()
if args.output.exists():
    parser.error("Use a new output directory")
args.output = args.output.resolve()
args.output.mkdir(parents=True)
args.editor = args.editor.resolve()
args.sfdk = args.sfdk.resolve()
sdk_signing = args.sfdk.parent.parent / "package-signing"
source_key = sdk_signing / "regular_key.pem"
certificate = sdk_signing / "regular_cert.pem"
assert source_key.is_file() and certificate.is_file()

passphrase = secrets.token_urlsafe(32)
launcher = [
    str(args.sfdk),
    "engine",
    "exec",
    "python3",
    "-u",
    "-c",
    (Path(__file__).resolve().parents[1] / "sdk_job.py").read_text(),
]
summary = []


def sdk(*command):
    result = subprocess.run([str(args.sfdk), "engine", "exec", *command], text=True, capture_output=True, check=True)
    return result.stdout


def inspect_host_rpm(path):
    # The SDK shared mount may retain the previous destination inode's size
    # after a host overwrite. Inspect the actual final host bytes through stdin
    # in an SDK-private temporary file, independently of the shared-path view.
    script = """import os, shutil, subprocess, sys, tempfile
with tempfile.TemporaryDirectory(prefix="godot-signature-check-") as directory:
    path = os.path.join(directory, "result.rpm")
    with open(path, "wb") as stream:
        shutil.copyfileobj(sys.stdin.buffer, stream)
    result = subprocess.run(["rpmsign-external", "dump", path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sys.stdout.buffer.write(result.stdout)
    sys.exit(result.returncode)
"""
    result = subprocess.run(
        [str(args.sfdk), "engine", "exec", "python3", "-c", script],
        input=path.read_bytes(),
        capture_output=True,
        check=True,
        timeout=30,
    )
    return result.stdout.decode("utf-8")


with tempfile.TemporaryDirectory(prefix="godot-encrypted-key-test-", dir=sdk_signing) as key_directory:
    encrypted = Path(key_directory) / "encrypted.pem"
    command = [
        "sb2",
        "-t",
        args.tool_target,
        "-m",
        "sdk-build",
        "sh",
        "-c",
        'umask 077; exec "$@"',
        "fixture",
        "openssl",
        "pkey",
        "-engine",
        "gost",
        "-in",
        str(source_key),
        "-out",
        str(encrypted),
        "-aes-256-cbc",
        "-passout",
        "env:KEY_PASSPHRASE",
    ]
    job = Job(command, launcher=launcher, secret_environment={"KEY_PASSPHRASE": passphrase})
    try:
        job.running()
        assert job.result()["exit_code"] == 0 and job.transport_exit == 0, "Could not prepare protected SDK fixture key"
    finally:
        job.close()
    encrypted.chmod(0o600)
    for mode in args.modes:
        case = args.output / mode
        case.mkdir()
        project = case / "project"
        shutil.copytree(args.project, project, ignore=shutil.ignore_patterns(".godot"))
        addon = project / "addons/aurora_signing_smoke"
        addon.mkdir(parents=True)
        for name in ("plugin.cfg", "plugin.gd"):
            shutil.copy2(Path(__file__).with_name(name), addon / name)
        with (project / "project.godot").open("a") as config:
            config.write(
                '\n[editor_plugins]\nenabled=PackedStringArray("res://addons/aurora_signing_smoke/plugin.cfg")\n'
            )
        presets = (project / "export_presets.cfg").read_text().split("[preset.1]")[0]
        (project / "export_presets.cfg").write_text(presets)
        rpm = case / "ru.kotdath.godotnested-1.0.0-1.aarch64.rpm"
        sentinel = b"previous-output-must-survive-failure"
        rpm.write_bytes(sentinel)
        marker = case / "slow-job.pid"
        slow = case / "slow-patchelf"
        slow.write_text(
            "#!/usr/bin/python3\nimport os,signal,time\n"
            "signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
            f"open({str(marker)!r},'w').write(str(os.getpid()))\n"
            "time.sleep(60)\n"
        )
        slow.chmod(0o755)
        result_path = case / "result.json"
        env = {
            **os.environ,
            "LANG": "en_US.UTF-8",
            "LC_ALL": "en_US.UTF-8",
            "XDG_CONFIG_HOME": str(case / "config"),
            "XDG_DATA_HOME": str(case / "data"),
            "XDG_CACHE_HOME": str(case / "cache"),
            "GODOT_AURORA_SIGNING_TEST": mode,
            "AURORA_TEST_SIGNING_KEY": str(encrypted),
            "AURORA_TEST_SIGNING_CERTIFICATE": str(certificate),
            "AURORA_TEST_RESULT": str(result_path),
            "AURORA_TEST_RPM": str(rpm),
            "AURORA_TEST_PASSPHRASE": passphrase,
            "AURORA_TEST_JOB_MARKER": str(marker),
            "AURORA_TEST_SLOW_PATCHELF": str(slow),
            "AURORA_TEST_REAL_PATCHELF": "",
        }
        # Read the fixture's existing explicit SDK patchelf path without changing
        # the product preset schema or storing any credential value in it.
        for line in presets.splitlines():
            if line.startswith("sdk/patchelf="):
                env["AURORA_TEST_REAL_PATCHELF"] = line.split("=", 1)[1].strip('"')
        assert env["AURORA_TEST_REAL_PATCHELF"]
        env.pop("GODOT_AURORA_KEY_PASSPHRASE", None)
        if mode in ("headless", "wrong"):
            env["GODOT_AURORA_KEY_PASSPHRASE"] = (
                passphrase if mode == "headless" else "intentionally-wrong-fixture-passphrase"
            )
            display = ["--headless"]
        else:
            display = ["--display-driver", "wayland"]
            env["AURORA_TEST_SCREENSHOT"] = str(case / "masked-dialog.png")
        with (case / "editor.log").open("w") as log:
            result = subprocess.run(
                [str(args.editor), *display, "--editor", "--path", str(project)],
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=180,
            )
        log = (case / "editor.log").read_text()
        assert passphrase not in log, "Synthetic passphrase leaked into editor output"
        assert result.returncode == 0 and result_path.exists(), (
            f"Signing fixture failed: {mode}; inspect {case / 'editor.log'}"
        )
        evidence = json.loads(result_path.read_text())
        assert evidence["matched"], mode
        if mode in ("cancel_prompt", "accept", "cancel_retry"):
            assert evidence["results"][-1]["masked"] and evidence["results"][-1]["answered"], mode
        if mode in ("wrong", "cancel_prompt"):
            assert rpm.read_bytes() == sentinel, mode
        else:
            signature = inspect_host_rpm(rpm)
            assert "Subgroup: regular" in signature, mode
            (case / "signature.log").write_text(signature)
        if mode == "cancel_retry":
            assert evidence["cancel_clicked"] and len(evidence["results"]) == 2
            assert evidence["results"][0]["previous_intact"]
            assert not evidence["results"][0]["artifact"] and evidence["results"][1]["artifact"]
            pid = int(marker.read_text())
            sdk(
                "python3",
                "-c",
                "from pathlib import Path\n"
                f"p=Path('/proc/{pid}/stat')\n"
                "try:\n assert p.read_text().rsplit(') ',1)[1].startswith('Z ')\n"
                "except FileNotFoundError:\n pass\n",
            )
        assert not list(case.glob(".godot-aurora-*")), "Export staging survived completion"
        assert passphrase not in (project / "export_presets.cfg").read_text()
        for settings in (case / "config").rglob("*.tres"):
            assert passphrase not in settings.read_text(), "Synthetic passphrase leaked into editor settings"
        summary.append({"mode": mode, **evidence})
        print("PASS:", mode, flush=True)
(args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print("Protected fixture key removed; no root-CA trust verification was performed")
