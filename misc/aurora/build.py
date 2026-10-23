#!/usr/bin/env python3
"""Run the same Godot build under SDK, an entered PSDK, or an explicit toolchain.

The SDK adapter intentionally uses sfdk for transport and target SB2 for the
build environment, without changing global sfdk configuration.
"""

import argparse
import base64
import json
import os
import subprocess
from contextlib import contextmanager, nullcontext
from pathlib import Path


def source_provenance(root):
    """Read Git on the source host, never through the SDK's cached shared view."""
    if not (root / ".git").exists():
        return {}
    metadata = subprocess.check_output(
        ["git", "show", "-s", "--format=%H%n%ct", "--no-show-signature", "HEAD"], cwd=root, text=True
    )
    revision, timestamp = metadata.strip().splitlines()
    return {"GODOT_BUILD_GIT_HASH": revision, "GODOT_BUILD_GIT_TIMESTAMP": timestamp}


def with_source_provenance(build, environment, provenance):
    process_environment = {**os.environ, **provenance}
    if environment == "sdk" and provenance:
        # sfdk does not promise to forward arbitrary host environment variables.
        # Execute env INSIDE the engine, preserving literal argv values.
        build = build[:3] + ["env"] + [key + "=" + value for key, value in provenance.items()] + build[3:]
    return build, process_environment


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


@contextmanager
def sdk_generated_sources(sfdk, root):
    """Regenerate configuration-dependent outputs and restore the host variants.

    SCons signatures are platform-specific, but these generated paths are not.
    A cached target build can otherwise reuse the host module list or Wayland
    wrappers. Concurrent builds in the same checkout are unsupported.
    """
    directory = root / "platform/linuxbsd/wayland/protocol"
    generated = set(directory.glob("*.gen.h")) | set(directory.glob("*.gen.c"))
    for pattern in (
        "modules/*.gen.h",
        "modules/*.gen.cpp",
        "core/disabled_classes.gen.h",
        "core/version_generated.gen.h",
        "core/version_hash.gen.cpp",
        "core/script_encryption_key.gen.cpp",
        "editor/export/register_exporters.gen.cpp",
        "editor/doc/doc_data*.gen.h",
    ):
        generated.update(root.glob(pattern))
    saved = {str(path.relative_to(root)): path.read_bytes() for path in sorted(generated)}
    if not saved:
        yield
        return
    remove_script = """
import json, pathlib, sys
for name in json.load(sys.stdin):
    path = pathlib.Path(name)
    if path.exists():
        path.unlink()
"""
    restore_script = """
import base64, json, pathlib, sys
for name, encoded in json.load(sys.stdin).items():
    pathlib.Path(name).write_bytes(base64.b64decode(encoded))
"""
    print("Temporarily regenerating configuration-dependent sources for Aurora", flush=True)
    try:
        subprocess.run(
            [sfdk, "engine", "exec", "python3", "-c", remove_script],
            cwd=root,
            input=json.dumps(list(saved)).encode(),
            check=True,
        )
        yield
    finally:
        payload = {name: base64.b64encode(data).decode("ascii") for name, data in saved.items()}
        subprocess.run(
            [sfdk, "engine", "exec", "python3", "-c", restore_script],
            cwd=root,
            input=json.dumps(payload).encode(),
            check=True,
        )
        print("Restored host generated sources", flush=True)


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
    root = Path(__file__).resolve().parents[2]
    provenance = source_provenance(root)
    build, process_environment = with_source_provenance(build, args.environment, provenance)
    if args.dry_run:
        import shlex

        local_env = (
            ["env"] + [key + "=" + value for key, value in provenance.items()]
            if provenance and args.environment != "sdk"
            else []
        )
        print(shlex.join(local_env + build))
        return
    if provenance:
        print("Source revision:", provenance["GODOT_BUILD_GIT_HASH"], flush=True)
    if args.environment == "sdk":
        refresh_sdk_sources(args.sfdk, root)
    with sdk_generated_sources(args.sfdk, root) if args.environment == "sdk" else nullcontext():
        subprocess.run(build, cwd=root, env=process_environment, check=True)


if __name__ == "__main__":
    main()
