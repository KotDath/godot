#!/usr/bin/env python3
"""Package an Aurora editor or an exported game with its private native libraries.

Run inside the selected SDK/PSDK target. Shared by the desktop export platform
and device smoke tests. No signing credentials are read here.
"""

import argparse
import hashlib
import json
import posixpath
import re
import shlex
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path, PurePosixPath


def relative_native_path(value):
    """Canonical package-relative paths, never host paths or RPM macros."""
    if not isinstance(value, str) or not value or any(ord(char) < 32 or char in "\\:%$\x7f" for char in value):
        raise ValueError("Invalid native relative path: " + str(value))
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise ValueError("Invalid native relative path: " + value)
    return path


def read_native_map(path):
    if path is None:
        return []
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != 1 or not isinstance(document.get("libraries"), list):
        raise ValueError("Unsupported native input map")
    entries = []
    for item in document["libraries"]:
        project = item["project"]
        if not isinstance(project, str) or not project.startswith("res://"):
            raise ValueError("Native library must have a project resource path")
        relative = relative_native_path(project[6:])
        source = Path(item["source"]).absolute()
        if source.name != relative.name:
            raise ValueError("Native source and project filenames differ: " + project)
        target_folder = item.get("target", "")
        target = (
            PurePosixPath(source.name)
            if target_folder == "."
            else (relative_native_path(target_folder) / source.name if target_folder else relative)
        )
        digest = item.get("sha256")
        if digest is not None:
            if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
                raise ValueError("Invalid native source SHA256")
            if hashlib.sha256(source.read_bytes()).hexdigest() != digest.lower():
                raise ValueError("Native SHA256 mismatch: refresh the SDK shared file: " + str(source))
        entries.append((source, project, PurePosixPath("lib/project") / target))
    return entries


def native_layout(files, entries):
    """Keep CLI private libraries flat; exported project libraries keep their tree."""
    destinations = {path: PurePosixPath("lib") / path.name for path in files}
    project_paths = {}
    mapped_sources = set()
    for source, project, destination in entries:
        if source in mapped_sources and destinations[source] != destination:
            raise ValueError("One native source has conflicting installation targets: " + str(source))
        if project in project_paths and project_paths[project] != str(destination):
            raise ValueError("Conflicting native project mapping: " + project)
        mapped_sources.add(source)
        destinations[source] = destination
        project_paths[project] = str(destination)
    if len(set(destinations.values())) != len(destinations):
        raise ValueError("Native installation path collision")
    return destinations, project_paths


def private_runpath(path, info, files, destinations):
    by_name = {source.name: source for source in files}
    directories = {"$ORIGIN"}
    for dependency in info["needed"]:
        if dependency in by_name:
            relative = posixpath.relpath(destinations[by_name[dependency]].parent, destinations[path].parent)
            if relative != ".":
                directories.add("$ORIGIN/" + relative)
    return ":".join(sorted(directories))


def valid_origin_runpath(value):
    if value == "$ORIGIN":
        return True
    return value.startswith("$ORIGIN/") and not any(
        ord(char) < 32 or char in "\\:%$\x7f" for char in value[len("$ORIGIN/") :]
    )


def elf_info(path):
    with path.open("rb") as stream:
        raw = stream.read(52)
    if len(raw) != 52 or raw[:4] != b"\x7fELF" or raw[5] != 1:
        raise ValueError("Not a little-endian ELF: " + str(path))
    machine = int.from_bytes(raw[18:20], "little")
    arch = {(1, 40): "arm32", (2, 183): "arm64"}.get((raw[4], machine))
    if arch == "arm32" and not int.from_bytes(raw[36:40], "little") & 0x400:
        raise ValueError("ARM32 ELF must use the hard-float ABI: " + str(path))
    if arch is None:
        raise ValueError("Not an ARM ELF: " + str(path))
    output = subprocess.check_output(["readelf", "-d", str(path)], text=True)
    return {
        "arch": arch,
        "needed": re.findall(r"\(NEEDED\).*\[(.*?)\]", output),
        "soname": re.findall(r"\(SONAME\).*\[(.*?)\]", output),
        "runpath": re.findall(r"\((?:RUNPATH|RPATH)\).*\[(.*?)\]", output),
    }


def validate_native(files, arch):
    manifest = {}
    for path in files:
        info = elf_info(path)
        if info["arch"] != arch:
            raise ValueError("Architecture mismatch: " + str(path))
        if path.name in manifest:
            raise ValueError("Native file name collision: " + path.name)
        if any(not valid_origin_runpath(entry) for value in info["runpath"] for entry in value.split(":")):
            raise ValueError("Only private $ORIGIN-relative RUNPATH is allowed: " + str(path))
        if info["soname"] and info["soname"] != [path.name]:
            raise ValueError("Supply a file named after its SONAME: " + str(path))
        manifest[path.name] = info
    for name, info in manifest.items():
        for dependency in info["needed"]:
            if not re.fullmatch(r"[A-Za-z0-9_+.-]+", dependency):
                raise ValueError("DT_NEEDED must be a library name, not a path: " + dependency)
            if dependency in manifest:
                if name != files[0].name and not info["runpath"]:
                    raise ValueError("Missing $ORIGIN RUNPATH in " + name)
            else:
                # Resolve only target system libraries. No host libraries or
                # implicit copying of arbitrary files from the developer PC.
                libdir = "lib64" if arch == "arm64" else "lib"
                candidates = [
                    Path(directory) / dependency for directory in ("/" + libdir, "/usr/" + libdir, "/lib", "/usr/lib")
                ]
                if not any(path.exists() and elf_info(path)["arch"] == arch for path in candidates):
                    raise ValueError("Unresolved target dependency {} of {}".format(dependency, name))
    return manifest


def append_pck(executable, pck, expected_sha256=None):
    """Count copied bytes: SDK fakeowner can report a stale source st_size."""
    with pck.open("rb") as source:
        if source.read(4) != b"GDPC":
            raise ValueError("Not a Godot PCK: " + str(pck))
        source.seek(0)
        size = 0
        digest = hashlib.sha256()
        with executable.open("ab") as destination:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                destination.write(chunk)
                size += len(chunk)
                digest.update(chunk)
            if expected_sha256 and digest.hexdigest() != expected_sha256.lower():
                raise ValueError("PCK SHA256 mismatch: SDK may see stale/truncated shared files; refresh the SDK copy")
            destination.write(struct.pack("<Q4s", size, b"GDPC"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--project", type=Path, help="Unpacked development project (editor binary only)")
    source.add_argument("--pck", type=Path, help="Embed a game PCK without enabling path overrides")
    parser.add_argument("--pck-sha256", help="PCK SHA256 calculated on the export host; detects stale SDK reads")
    parser.add_argument("--strip", default="strip", help="Target strip executable for embedded-PCK packages")
    parser.add_argument("--native-dir", type=Path)
    parser.add_argument("--native-map", type=Path, help="Export host's resource-path/source/target map (schema 1)")
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--private-library", type=Path, action="append", default=[])
    parser.add_argument("--patchelf", default="patchelf")
    parser.add_argument("--license", type=Path, action="append", default=[])
    parser.add_argument("--engine-license", type=Path)
    parser.add_argument("--engine-copyright", type=Path)
    parser.add_argument("--bundle-xkbcommon", action="store_true")
    parser.add_argument("--title", default="Godot Aurora Probe")
    parser.add_argument("--summary", default="Godot Aurora application")
    parser.add_argument("--rpm-license", default="MIT")
    parser.add_argument("--permissions", default="Audio;UserDirs")
    parser.add_argument("--arch", choices=("arm32", "arm64"), required=True)
    parser.add_argument("--name", default="ru.kotdath.godotprobe")
    parser.add_argument("--version", default="4.7.2")
    parser.add_argument("--release", default="1")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.pck_sha256 is not None and (not args.pck or not re.fullmatch(r"[0-9a-fA-F]{64}", args.pck_sha256)):
        parser.error("--pck-sha256 requires --pck and a 64-digit hexadecimal digest")
    for value in (args.name, args.version, args.release):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+]*", value):
            parser.error("Invalid package name/version/release")
    if any("\n" in value or "\r" in value or "%" in value for value in (args.title, args.summary, args.rpm_license)):
        parser.error("Package metadata must be single-line text without RPM macros")
    if not re.fullmatch(r"[A-Za-z0-9;]*", args.permissions):
        parser.error("Permissions must be semicolon-separated names")
    if args.bundle_xkbcommon and "libxkbcommon.so.0" in elf_info(args.binary)["needed"]:
        candidates = [Path(directory) / "libxkbcommon.so.0" for directory in ("/usr/lib64", "/usr/lib")]
        library = next((path for path in candidates if path.exists() and elf_info(path)["arch"] == args.arch), None)
        licenses = list(Path("/usr/share/licenses").glob("libxkbcommon*/LICENSE"))
        if library is None or len(licenses) != 1:
            parser.error("Target libxkbcommon and its license are required for a Regular package")
        args.private_library.append(library)
        args.license.append(licenses[0])
    version_text = subprocess.check_output([args.patchelf, "--version"], text=True)
    version_match = re.search(r"patchelf (\d+)\.(\d+)", version_text)
    if not version_match or tuple(map(int, version_match.groups())) < (0, 18):
        parser.error("patchelf >= 0.18 is required to preserve ELF build IDs")
    files = [args.binary.resolve()]
    files += [path.absolute() for path in args.private_library]
    if args.native_dir:
        files += sorted(path.resolve() for path in args.native_dir.iterdir() if path.is_file())
    entries = read_native_map(args.native_map)
    files += [source for source, _, _ in entries]
    if args.probe:
        files.append(args.probe.resolve())
    files = list(dict.fromkeys(files))
    source_manifest = validate_native(files, args.arch)
    private_files = [
        path for path in files if path != args.binary.resolve() and not (args.probe and path == args.probe.resolve())
    ]
    destinations, project_paths = native_layout(private_files, entries)
    for path in private_files:
        if path.name in source_manifest[args.binary.resolve().name]["needed"] and destinations[
            path
        ].parent != PurePosixPath("lib"):
            raise ValueError("Main executable dependencies must use the flat private lib directory: " + path.name)
    output = args.output.resolve()
    if "%" in str(output):
        parser.error("RPM output path must not contain RPM macro characters (%)")
    output.mkdir(parents=True, exist_ok=True)
    rpm_arch = {"arm32": "armv7hl", "arm64": "aarch64"}[args.arch]
    appdir = "usr/share/{}/lib".format(args.name)
    datadir = "usr/share/" + args.name
    with tempfile.TemporaryDirectory(prefix="aurora-rpm-", dir=output) as temporary:
        top = Path(temporary)
        stage = top / "stage"
        native = stage / appdir
        native.mkdir(parents=True)
        for path in private_files:
            installed = stage / datadir / destinations[path]
            installed.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, installed)
            installed.chmod(0o644)
            runpath = private_runpath(path, source_manifest[path.name], private_files, destinations)
            subprocess.run([args.patchelf, "--set-rpath", runpath, str(installed)], check=True)
        data = stage / datadir
        data.mkdir(parents=True, exist_ok=True)
        licenses = data / "licenses"
        licenses.mkdir()
        root = Path(__file__).resolve().parents[2]
        shutil.copy2(args.engine_license or root / "LICENSE.txt", licenses / "Godot.txt")
        shutil.copy2(args.engine_copyright or root / "COPYRIGHT.txt", licenses / "Godot-thirdparty.txt")
        for index, path in enumerate(args.license):
            shutil.copy2(path, licenses / (str(index) + "-" + path.name))
        if args.project:
            shutil.copytree(args.project, data / "project", ignore=shutil.ignore_patterns("bin", ".godot", "*.uid"))
        bindir = stage / "usr/bin"
        bindir.mkdir(parents=True)
        launcher = bindir / args.name
        shutil.copy2(args.binary, launcher)
        launcher.chmod(0o755)
        # Aurora's validator currently requires DT_RPATH on the main binary.
        # Private DSOs still carry their own DT_RUNPATH=$ORIGIN.
        subprocess.run([args.patchelf, "--force-rpath", "--set-rpath", "/" + appdir, str(launcher)], check=True)
        if args.pck:
            with args.pck.open("rb") as stream:
                if stream.read(4) != b"GDPC":
                    raise ValueError("Not a Godot PCK: " + str(args.pck))
            # RPM's strip/debug extraction must not truncate the appended PCK.
            # Strip first, then append using Godot's self-contained footer.
            subprocess.run([args.strip, "--strip-unneeded", str(launcher)], check=True)
            append_pck(launcher, args.pck, args.pck_sha256)
        installed_manifest = {
            "schema": 2,
            "project_libraries": project_paths,
            "files": {"/usr/bin/" + args.name: elf_info(launcher)},
        }
        for path in private_files:
            installed = stage / datadir / destinations[path]
            installed_manifest["files"][str(destinations[path])] = elf_info(installed)
        (data / "native-manifest.json").write_text(json.dumps(installed_manifest, indent=2) + "\n")
        if args.probe:
            libexec = stage / "usr/libexec" / args.name
            libexec.mkdir(parents=True)
            shutil.copy2(args.probe, libexec / "wayland-probe")
            (libexec / "wayland-probe").chmod(0o755)
        launch_args = " --path /{}/project".format(datadir) if args.project else ""
        desktop = stage / "usr/share/applications"
        desktop.mkdir(parents=True)
        (desktop / (args.name + ".desktop")).write_text(
            "[Desktop Entry]\nType=Application\nName={title}\n"
            "Exec={}{}\nIcon={}\nX-Nemo-Application-Type=generic\n\n[X-Application]\n"
            "OrganizationName={}\nApplicationName={}\nPermissions={permissions}\n".format(
                args.name,
                launch_args,
                args.name,
                args.name.rsplit(".", 1)[0],
                args.name.rsplit(".", 1)[1],
                title=args.title,
                permissions=args.permissions,
            )
        )
        for size in (86, 108, 128, 172):
            icons = stage / "usr/share/icons/hicolor" / (str(size) + "x" + str(size)) / "apps"
            icons.mkdir(parents=True)
            shutil.copy2(Path(__file__).resolve().parent / "icons" / (str(size) + ".png"), icons / (args.name + ".png"))
        for directory in stage.rglob("*"):
            if directory.is_dir():
                directory.chmod(0o755)
            elif not str(directory).startswith(str(stage / "usr/bin")) and not str(directory).startswith(
                str(stage / "usr/libexec")
            ):
                directory.chmod(0o644)
        # Private libraries are shipped here: keep system dependency generation,
        # remove only these exact private SONAMEs from public Provides/Requires.
        private_names = [
            path.name
            for path in files
            if path != args.binary.resolve() and not (args.probe and path == args.probe.resolve())
        ]
        if not all(re.fullmatch(r"[A-Za-z0-9_+.-]+", name) for name in private_names):
            raise ValueError("Unsupported private library filename")
        # Bracket expressions survive RPM macro expansion without extra levels
        # of backslash escaping (which otherwise silently break the filter).
        private_regex = (
            "^(" + "|".join(name.replace(".", "[.]").replace("+", "[+]") for name in private_names) + ")([(].*)?$"
        )
        spec = top / "package.spec"
        spec.write_text(
            """Name: {name}
Version: {version}
Release: {release}
Summary: {summary}
License: {license_id}
BuildArch: {arch}
AutoReqProv: yes
{strip_policy}
%global __provides_exclude {private_regex}
%global __requires_exclude {private_regex}

%description
{summary}

%install
mkdir -p "%{{buildroot}}"
cp -a {stage_shell} "%{{buildroot}}/"

%files
%defattr(644,root,root,755)
%attr(755,root,root) /usr/bin/{name}
{probe_files}
%defattr(644,root,root,755)
/{datadir}
/usr/share/applications/{name}.desktop
/usr/share/icons/hicolor/*/apps/{name}.png
""".format(
                name=args.name,
                version=args.version,
                release=args.release,
                arch=rpm_arch,
                stage_shell=shlex.quote(str(stage) + "/."),
                summary=args.summary,
                license_id=args.rpm_license,
                datadir=datadir,
                private_regex=private_regex,
                strip_policy="%global debug_package %{nil}\n%global __strip /bin/true" if args.pck else "",
                probe_files=("%attr(755,root,root) /usr/libexec/" + args.name) if args.probe else "",
            )
        )
        subprocess.run(
            [
                "rpmbuild",
                "-bb",
                "--target",
                rpm_arch,
                "--define",
                "_topdir " + str(top),
                "--define",
                "_rpmdir " + str(output),
                str(spec),
            ],
            check=True,
        )
    print(output / rpm_arch / "{}-{}-{}.{}.rpm".format(args.name, args.version, args.release, rpm_arch))


if __name__ == "__main__":
    main()
