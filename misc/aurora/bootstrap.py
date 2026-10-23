#!/usr/bin/env python3
"""Build host Python and install SCons without altering SDK system packages.

This bootstrap itself supports Python 3.8 (the SDK tooling interpreter).
Download outside SB2, then build offline in a host-architecture SDK target.
The resulting Python must run on the build machine, never on the ARM target.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def archive(package, cache, offline):
    path = cache / package["url"].rsplit("/", 1)[1]
    if not path.exists():
        if offline:
            raise RuntimeError("Missing cached source: {}. Run --download-only outside the SDK.".format(path))
        temporary = path.with_suffix(path.suffix + ".part")
        with urllib.request.urlopen(package["url"], timeout=60) as response, temporary.open("wb") as target:
            shutil.copyfileobj(response, target)
        if digest(temporary) != package["sha256"]:
            temporary.unlink()
            raise RuntimeError("Source checksum mismatch: " + package["url"])
        temporary.replace(path)
    if digest(path) != package["sha256"]:
        raise RuntimeError("Cached source checksum mismatch: " + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 16))
    parser.add_argument("--download-only", action="store_true")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    packages = json.loads((HERE / "host-tools.json").read_text())
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    sources = {key: archive(value, cache, args.offline) for key, value in packages.items()}
    if args.download_only:
        print("Verified Python and SCons sources in", cache)
        return

    prefix = args.prefix.resolve()
    compiler = os.environ.get("CC", "cc")
    machine = subprocess.check_output([compiler, "-dumpmachine"], text=True).strip()
    host = platform.machine()
    aliases = {"amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}
    if aliases.get(machine.split("-")[0], machine.split("-")[0]) != aliases.get(host, host):
        raise RuntimeError("Host Python needs a host compiler; compiler={}, host={}".format(machine, host))
    identity = {
        "packages": packages,
        "compiler": subprocess.check_output([compiler, "--version"], text=True),
        "host": host,
        "libc": platform.libc_ver(),
        "recipe": digest(Path(__file__).resolve()),
    }
    # Normalize tuples so JSON round trips compare equal.
    identity = json.loads(json.dumps(identity))
    stamp = prefix / "bootstrap.json"
    python = prefix / "bin/python3.12"
    if stamp.exists():
        if json.loads(stamp.read_text()) != identity:
            raise RuntimeError("Host tool prefix belongs to another recipe/environment; choose a new --prefix")
        subprocess.run([str(python), "-c", "import SCons, zlib, hashlib; print(SCons.__version__)"], check=True)
        print("Host tools already ready:", python)
        return

    prefix.mkdir(parents=True, exist_ok=True)
    source_dir = prefix / "sources"
    source_dir.mkdir(exist_ok=True)
    with tarfile.open(sources["python"]) as source:
        # Archives are checksum-pinned; additionally reject paths outside staging.
        for member in source.getmembers():
            resolved = (source_dir / member.name).resolve()
            if source_dir not in resolved.parents or member.issym() or member.islnk():
                raise RuntimeError("Unsafe archive member: " + member.name)
        source.extractall(source_dir)
    build = prefix / "build"
    build.mkdir(exist_ok=True)
    configure = source_dir / ("Python-" + packages["python"]["version"]) / "configure"
    subprocess.run([str(configure), "--prefix=" + str(prefix), "--with-ensurepip=no"], cwd=build, check=True)
    subprocess.run(["make", "-j" + str(args.jobs)], cwd=build, check=True)
    subprocess.run(["make", "install"], cwd=build, check=True)
    site = prefix / "lib/python3.12/site-packages"
    site.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(sources["scons"]) as wheel:
        wheel.extractall(site)
    subprocess.run([str(python), "-c", "import SCons, zlib, hashlib; print(SCons.__version__)"], check=True)
    stamp.write_text(json.dumps(identity, indent=2) + "\n")
    print("Host tools ready:", python, "-m SCons")


if __name__ == "__main__":
    main()
