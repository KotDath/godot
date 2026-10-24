"""Identity and verification helpers for the Aurora source/build matrix."""

import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

MATRIX = {(arch, target) for arch in ("arm32", "arm64") for target in ("editor", "template_debug", "template_release")}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments], text=True).strip()


def source_snapshot(root):
    """Measure clean tracked files as they exist on the source host."""
    root = Path(root).resolve()
    if git(root, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("Source checkout is not clean: " + str(root))
    revision = git(root, "rev-parse", "HEAD")
    records = []
    raw = subprocess.check_output(["git", "-C", str(root), "ls-files", "--stage", "-z"])
    for entry in raw.split(b"\0"):
        if not entry:
            continue
        metadata, name = entry.decode().split("\t", 1)
        mode, blob, stage = metadata.split()
        if stage != "0" or mode == "160000":
            raise ValueError("Unmerged entries/submodules need separate source identities: " + name)
        path = root / name
        if mode == "120000":
            if not path.is_symlink():
                raise ValueError("Tracked symbolic link changed: " + name)
            if not path.resolve().is_relative_to(root):
                raise ValueError("External source link needs a separate input identity: " + name)
            digest = hashlib.sha256(path.readlink().as_posix().encode()).hexdigest()
        else:
            if path.is_symlink() or not path.is_file():
                raise ValueError("Tracked file changed type: " + name)
            digest = sha256(path)
        records.append({"path": name, "mode": mode, "git_blob": blob, "sha256": digest})
    # Detect mutations that happened while measuring, including checkout changes.
    if git(root, "rev-parse", "HEAD") != revision or git(root, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("Source checkout changed while measuring: " + str(root))
    return {
        "revision": revision,
        "timestamp": int(git(root, "show", "-s", "--format=%ct", "--no-show-signature", revision)),
        "tree": git(root, "rev-parse", revision + "^{tree}"),
        "tracked_files_sha256": canonical_digest(records),
        "tracked_files": records,
    }


def elf_record(path):
    path = Path(path)
    with path.open("rb") as stream:
        header = stream.read(52)
    if len(header) < 52 or header[:4] != b"\x7fELF" or header[5] != 1:
        raise ValueError("Expected little-endian ELF: " + str(path))
    machine = int.from_bytes(header[18:20], "little")
    arch = {(1, 40): "arm32", (2, 183): "arm64", (2, 62): "x86_64"}.get((header[4], machine))
    if arch is None:
        raise ValueError("Unsupported ELF class/machine: " + str(path))
    flags = int.from_bytes(header[36:40] if header[4] == 1 else header[48:52], "little")
    if arch == "arm32" and not flags & 0x400:
        raise ValueError("ARM32 must use the hard-float ABI: " + str(path))
    dynamic = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    notes = subprocess.check_output(["readelf", "-n", str(path)], text=True, stderr=subprocess.PIPE)
    sections = subprocess.check_output(["readelf", "-SW", str(path)], text=True)
    return {
        "arch": arch,
        "elf_class": header[4] * 32,
        "machine": machine,
        "flags": flags,
        "needed": re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic),
        "soname": re.findall(r"\(SONAME\).*\[(.*?)\]", dynamic),
        "runpath": re.findall(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", dynamic),
        "build_ids": re.findall(r"Build ID:\s*(\w+)", notes),
        "debug_info": bool(re.search(r"\s\.debug_info\s", sections)),
    }


def archive_record(path):
    output = subprocess.check_output(
        ["readelf", "-h", str(path)], text=True, env={**os.environ, "LC_ALL": "C"}, stderr=subprocess.PIPE
    )
    classes = re.findall(r"^\s*Class:\s+ELF(32|64)\s*$", output, re.MULTILINE)
    machines = re.findall(r"^\s*Machine:\s+(.+?)\s*$", output, re.MULTILINE)
    mapping = {("32", "ARM"): "arm32", ("64", "AArch64"): "arm64", ("64", "Advanced Micro Devices X86-64"): "x86_64"}
    arches = {mapping.get(pair) for pair in zip(classes, machines)}
    if not classes or len(classes) != len(machines) or len(arches) != 1 or None in arches:
        raise ValueError("Mixed/unsupported/empty binding archive: " + str(path))
    return {"arch": arches.pop(), "elf_members": len(classes)}


def confined_file(root, relative):
    """Artifact manifests may name regular files only inside their bundle."""
    root = Path(root).resolve()
    path = Path(relative)
    if path.is_absolute() or path.as_posix() != relative or any(part in ("..", ".") for part in path.parts):
        raise ValueError("Noncanonical artifact path: " + str(relative))
    candidate = root / path
    for parent in (candidate, *candidate.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise ValueError("Artifact path contains a symbolic link: " + str(relative))
    if not candidate.resolve().is_relative_to(root) or not candidate.is_file():
        raise ValueError("Artifact file is missing/outside bundle: " + str(relative))
    return candidate


def artifact_record(root, path, kind, **metadata):
    root = Path(root).resolve()
    path = Path(path).absolute()
    relative = path.relative_to(root).as_posix()
    path = confined_file(root, relative)
    result = {"path": relative, "kind": kind, "sha256": sha256(path), "size": path.stat().st_size, **metadata}
    if kind in ("engine", "native", "host"):
        result["elf"] = elf_record(path)
        if result.get("arch", result["elf"]["arch"]) != result["elf"]["arch"]:
            raise ValueError("Artifact architecture mismatch: " + relative)
    if kind == "bindings_archive":
        result["archive"] = archive_record(path)
        if result.get("arch") != result["archive"]["arch"]:
            raise ValueError("Binding archive architecture mismatch")
    return result


def verify_artifacts(root, records):
    seen = set()
    for record in records:
        if record["path"] in seen:
            raise ValueError("Duplicate artifact path: " + record["path"])
        seen.add(record["path"])
        path = confined_file(root, record["path"])
        if path.stat().st_size != record["size"] or sha256(path) != record["sha256"]:
            raise ValueError("Artifact bytes differ: " + record["path"])
        if record["kind"] in ("engine", "native", "host"):
            actual = elf_record(path)
            if actual != record["elf"] or record.get("arch", actual["arch"]) != actual["arch"]:
                raise ValueError("Artifact ELF identity differs: " + record["path"])
        if record["kind"] == "bindings_archive" and archive_record(path) != record["archive"]:
            raise ValueError("Binding archive identity differs: " + record["path"])


def verify_manifest(root, manifest):
    """A complete software matrix cannot be inferred from a partial green run."""
    if manifest.get("schema") != 1 or manifest.get("status") != "passed":
        raise ValueError("Expected a completed schema-1 software matrix")
    if manifest.get("hardware") != {"arm32": "excluded", "arm64": "not-run"}:
        raise ValueError("Software matrix must not assert physical hardware acceptance")
    records = manifest["artifacts"]
    verify_artifacts(root, records)
    engines = [record for record in records if record["kind"] == "engine" and record.get("profile") == "compatibility"]
    actual = [(record["arch"], record["target"]) for record in engines]
    if set(actual) != MATRIX or len(actual) != len(MATRIX):
        raise ValueError("Exactly six compatibility engine artifacts are required")
    experimental = [
        (item["profile"], item["arch"], item["target"])
        for item in records
        if item["kind"] == "engine" and item.get("profile") != "compatibility"
    ]
    expected_experimental = (
        {("vulkan", arch, "template_debug") for arch in ("arm32", "arm64")}
        | {("embree", "arm64", target) for target in ("editor", "template_debug", "template_release")}
        if manifest.get("experimental_requested")
        else set()
    )
    if set(experimental) != expected_experimental or len(experimental) != len(expected_experimental):
        raise ValueError("Requested experimental engine matrix is incomplete")
    for repository in ("engine", "godot_cpp"):
        snapshot = manifest["sources"][repository]
        if canonical_digest(snapshot["tracked_files"]) != snapshot["tracked_files_sha256"]:
            raise ValueError("Source file inventory differs: " + repository)
        if not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", snapshot["revision"]):
            raise ValueError("Missing source revision: " + repository)
    if set(manifest["sdk"]) != {"arm32", "arm64"}:
        raise ValueError("Both SDK target identities are required")
    for arch, sdk in manifest["sdk"].items():
        if not sdk.get("target") or not sdk.get("link_inputs") or not sdk.get("compiler_version"):
            raise ValueError("Incomplete SDK identity: " + arch)
        if canonical_digest(sdk["packages"]) != sdk["packages_sha256"]:
            raise ValueError("SDK package inventory differs: " + arch)
        expected = "aarch64" if arch == "arm64" else "arm"
        if not sdk["compiler_machine"].startswith(expected):
            raise ValueError("SDK compiler identity differs: " + arch)
    revision = manifest["sources"]["engine"]["revision"]
    for engine in (record for record in records if record["kind"] == "engine"):
        if engine["embedded_revision"] != revision:
            raise ValueError("Engine artifact revision mismatch: " + engine["path"])
        if revision.encode() not in confined_file(root, engine["path"]).read_bytes():
            raise ValueError("Engine ELF does not contain its declared revision: " + engine["path"])
    apis = {record.get("arch"): record["sha256"] for record in records if record["kind"] == "api"}
    if set(apis) != {"x86_64", "arm32", "arm64"}:
        raise ValueError("Actual host and both target editor APIs are required")
    for record in records:
        if record["kind"] in ("native", "bindings_archive") and record.get("api_sha256") != apis.get(record["arch"]):
            raise ValueError("Binding artifact is not tied to its target API: " + record["path"])
    bindings = [
        (record.get("arch"), record.get("build_system"), record.get("target"))
        for record in records
        if record["kind"] == "native" and record.get("role") == "extension" and record.get("arch") in ("arm32", "arm64")
    ]
    expected = {
        (arch, system, target)
        for arch in ("arm32", "arm64")
        for system in ("scons", "cmake")
        for target in ("template_debug", "template_release")
    }
    if set(bindings) != expected or len(bindings) != len(expected):
        raise ValueError("Both binding build systems/configurations/architectures are required")
    archives = [
        (item.get("arch"), item.get("build_system"), item.get("target"))
        for item in records
        if item["kind"] == "bindings_archive"
        and item.get("arch") in ("arm32", "arm64")
        and item.get("api_coverage") == "full"
    ]
    if set(archives) != expected or len(archives) != len(expected):
        raise ValueError("Full godot-cpp archives are required for the binding matrix")
    for record in records:
        if record["kind"] == "native" and record.get("target") == "template_debug" and not record["elf"]["debug_info"]:
            raise ValueError("Debug native artifact has no source-level symbols: " + record["path"])
    groups = {}
    for record in records:
        if record["kind"] == "native":
            groups.setdefault((record["arch"], record["build_system"], record["target"]), []).append(record)
    for key, group in groups.items():
        by_name = {Path(item["path"]).name: item for item in group}
        if (
            set(by_name) != {"libaurora_probe.so", "libaurora_probe_middle.so", "libaurora_probe_leaf.so"}
            or len(group) != 3
        ):
            raise ValueError("Missing/duplicate private dependency chain: " + str(key))
        for owner, dependency in (
            ("libaurora_probe.so", "libaurora_probe_middle.so"),
            ("libaurora_probe_middle.so", "libaurora_probe_leaf.so"),
        ):
            if dependency not in by_name[owner]["elf"]["needed"]:
                raise ValueError("Missing private DT_NEEDED: " + owner)
        if any(item["elf"]["runpath"] != ["$ORIGIN"] for item in group):
            raise ValueError("Private binding lookup is not $ORIGIN-only")
    rpms = {(record.get("arch"), record.get("target")) for record in records if record["kind"] == "rpm"}
    if rpms != MATRIX:
        raise ValueError("Editor/debug/release RPMs for both architectures are required")
    gates = manifest.get("checks", {})
    for name in (
        "python",
        "cpp",
        "api",
        "native_chain",
        "sdk_native_chain",
        "rpm_payload",
        "regular_validation",
        "linux_wayland",
    ):
        if gates.get(name) != "passed":
            raise ValueError("Missing matrix check: " + name)
