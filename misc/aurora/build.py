#!/usr/bin/env python3
"""Run the same Godot build under SDK, an entered PSDK, or an explicit toolchain.

The SDK adapter intentionally uses sfdk for transport and target SB2 for the
build environment, without changing global sfdk configuration.
"""

import argparse
import base64
import json
import subprocess
from pathlib import Path


def command(args, extra):
    fixed = ("platform=", "arch=", "target=")
    if any(value.startswith(fixed) for value in extra):
        raise ValueError("Set platform/arch/target with this runner's options, not trailing SCons arguments")
    build = [
        str(args.host_python),
        "-m",
        "SCons",
        "platform=auroraos",
        "arch=" + args.arch,
        "target=" + args.target,
        "-j" + str(args.jobs),
    ] + extra
    if args.environment in ("sdk", "psdk"):
        if not args.sdk_target:
            raise ValueError("--sdk-target is required for SDK/PSDK")
        rpm_arch = {"arm32": "armv7hl", "arm64": "aarch64"}[args.arch]
        target_name = args.sdk_target[:-8] if args.sdk_target.endswith(".default") else args.sdk_target
        if not target_name.endswith("-" + rpm_arch):
            raise ValueError("SDK target does not match requested architecture " + args.arch)
        build = ["sb2", "-t", args.sdk_target, "-m", "sdk-build"] + build
        if args.environment == "sdk":
            build = [args.sfdk, "engine", "exec"] + build
    return build


def refresh_sdk_sources(sfdk, root):
    """Refresh changed files through fakeowner so cached inode sizes match.

    Git-visible changes and generated source headers are transferred. Ignored
    binaries, tool caches and local credentials never enter the payload. Must run before SCons starts.
    """
    changed = subprocess.check_output(["git", "diff", "--name-only", "HEAD", "-z"], cwd=root)
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"], cwd=root)
    ignored = subprocess.check_output(
        ["git", "ls-files", "--others", "--ignored", "--exclude-standard", "-z"], cwd=root
    )
    generated = {
        name
        for name in ignored.split(b"\0")
        if name.endswith((b".gen.h", b".gen.cpp", b".gen.c", b".gen.inc")) and not name.startswith((b".", b"bin/"))
    }
    files = {}
    for raw in set((changed + untracked).split(b"\0")) | generated:
        if not raw:
            continue
        name = raw.decode("utf-8")
        path = root / name
        if path.is_file() and not path.is_symlink():
            files[name] = base64.b64encode(path.read_bytes()).decode("ascii")
    script = """
import base64, json, pathlib, sys
count = 0
for name, encoded in json.load(sys.stdin).items():
    path = pathlib.Path(name)
    data = base64.b64decode(encoded)
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        count += 1
print("SDK shared sources refreshed:", count)
"""
    subprocess.run(
        [sfdk, "engine", "exec", "python3", "-c", script], cwd=root, input=json.dumps(files).encode("utf-8"), check=True
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", choices=("sdk", "psdk", "direct"), default="sdk")
    parser.add_argument("--sfdk", default="sfdk")
    parser.add_argument("--sdk-target")
    parser.add_argument("--host-python", type=Path, required=True)
    parser.add_argument("--arch", choices=("arm32", "arm64"), required=True)
    parser.add_argument("--target", choices=("editor", "template_debug", "template_release"), default="template_debug")
    parser.add_argument("--jobs", type=int, default=16)
    parser.add_argument("--dry-run", action="store_true")
    args, extra = parser.parse_known_args()
    if extra and extra[0] == "--":
        extra = extra[1:]
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        build = command(args, extra)
    except ValueError as error:
        parser.error(str(error))
    if args.dry_run:
        import shlex

        print(shlex.join(build))
        return
    root = Path(__file__).resolve().parents[2]
    if args.environment == "sdk":
        refresh_sdk_sources(args.sfdk, root)
    subprocess.run(build, cwd=root, check=True)


if __name__ == "__main__":
    main()
