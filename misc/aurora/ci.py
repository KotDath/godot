#!/usr/bin/env python3
"""Build/verify a clean-source Aurora software matrix without deploying devices."""

import argparse
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from ci_artifacts import artifact_record, canonical_digest, git, sha256, source_snapshot, verify_manifest

ROOT = Path(__file__).resolve().parents[2]
TARGETS = ("editor", "template_debug", "template_release")
LIBRARIES = ("libaurora_probe.so", "libaurora_probe_middle.so", "libaurora_probe_leaf.so")
SANITIZE = ("PYTHONPATH", "PYTHONHOME", "SCONS_CACHE", "CC", "CXX", "AR", "RANLIB", "CFLAGS", "CPPFLAGS", "LDFLAGS")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


class Matrix:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.env = {key: value for key, value in os.environ.items() if key not in SANITIZE}
        self.env.update(
            XDG_CONFIG_HOME=str(self.output / "config"),
            XDG_DATA_HOME=str(self.output / "data"),
            XDG_CACHE_HOME=str(self.output / "cache"),
        )
        self.document = {
            "schema": 1,
            "status": "running",
            "experimental_requested": args.experimental,
            "hardware": {"arm32": "excluded", "arm64": "not-run"},
            "sources": {},
            "sdk": {},
            "checks": {},
            "artifacts": [],
            "commands": [],
            "controller": {
                "ci_sha256": sha256(Path(__file__)),
                "identity_helper_sha256": sha256(Path(__file__).with_name("ci_artifacts.py")),
            },
        }
        self.engine = self.output / "sources/engine"
        self.cpp = self.output / "sources/godot-cpp"
        self.api = self.output / "artifacts/extension_api.json"
        self.apis = {
            "x86_64": self.api,
            **{arch: self.output / "artifacts/api" / arch / "extension_api.json" for arch in ("arm32", "arm64")},
        }
        self.host = self.engine / "bin/godot.linuxbsd.editor.x86_64"
        self.targets = {"arm32": args.arm32_target, "arm64": args.arm64_target}
        # Preserve a venv's invocation path; resolving its Python symlink would
        # select the system prefix and lose the installed SCons environment.
        self.sdk_python = str(args.sdk_python.absolute())
        self.python = str(args.host_python.absolute())

    def save(self):
        write_json(self.output / "manifest.json", self.document)

    def run(self, name, command, *, cwd=None, accepted=(0,), env=None, timeout=7200):
        command = list(map(str, command))
        log = self.output / "logs" / (name + ".log")
        log.parent.mkdir(parents=True, exist_ok=True)
        print("RUN:", name, flush=True)
        record = {
            "name": name,
            "argv": command,
            "cwd": str(cwd or self.engine),
            "log": log.relative_to(self.output).as_posix(),
        }
        self.document["commands"].append(record)
        self.save()
        start = time.monotonic()
        with log.open("w") as output:
            result = subprocess.run(
                command,
                cwd=cwd or self.engine,
                env=env or self.env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
        record.update(returncode=result.returncode, seconds=round(time.monotonic() - start, 3))
        self.save()
        if result.returncode not in accepted:
            raise RuntimeError(f"{name} failed ({result.returncode}); inspect {log}")
        print("PASS:", name, record["seconds"], flush=True)
        return log

    def add(self, path, kind, **metadata):
        record = artifact_record(self.output, path, kind, **metadata)
        self.document["artifacts"].append(record)
        self.save()
        return record

    def copy(self, source, destination, kind, **metadata):
        path = self.output / "artifacts" / destination
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, path)
        return self.add(path, kind, **metadata)

    def sdk(self, arch, command):
        return [
            str(self.args.sfdk),
            "engine",
            "exec",
            "env",
            "-u",
            "SCONS_CACHE",
            "-u",
            "PYTHONPATH",
            "-u",
            "PYTHONHOME",
            "XDG_CONFIG_HOME=" + str(self.output / "config" / ("sdk-" + arch)),
            "XDG_DATA_HOME=" + str(self.output / "data" / ("sdk-" + arch)),
            "XDG_CACHE_HOME=" + str(self.output / "cache" / ("sdk-" + arch)),
            "sb2",
            "-t",
            self.targets[arch],
            "-m",
            "sdk-build",
            *map(str, command),
        ]

    def prepare(self):
        # Refuse ambiguous input checkouts; generated worktrees start without
        # ignored custom.py, object files, project libraries or build caches.
        for name, original, destination in (
            ("engine", self.args.engine, self.engine),
            ("godot_cpp", self.args.godot_cpp, self.cpp),
        ):
            if git(original, "status", "--porcelain", "--untracked-files=all"):
                raise ValueError("Input repository is dirty: " + str(original))
            revision = git(original, "rev-parse", "HEAD")
            destination.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(
                ["git", "-C", str(original), "worktree", "add", "--detach", str(destination), revision], check=True
            )
            self.document["sources"][name] = source_snapshot(destination)
        if self.engine.joinpath("misc/aurora/ci.py").read_bytes() != Path(__file__).read_bytes():
            raise ValueError("CI controller must be committed in the measured engine revision")
        self.save()
        self.run("sfdk-version", [self.args.sfdk, "--version"])
        self.run("validator-version", [self.args.sfdk, "engine", "exec", "rpm-validator", "--version"])
        self.run("validator-help", [self.args.sfdk, "engine", "exec", "rpm-validator", "--help"])
        self.document["host_tools"] = {
            "python": {"path": self.python, "sha256": sha256(self.python)},
            "sdk_python": {"path": self.sdk_python, "sha256": sha256(self.sdk_python)},
            "patchelf": {"path": str(self.args.patchelf), "sha256": sha256(self.args.patchelf)},
        }
        self.run(
            "host-python-scons",
            [self.python, "-c", "import sys,SCons;print(sys.version);print(SCons.__version__);print(SCons.__file__)"],
        )
        for arch in ("arm32", "arm64"):
            # Run all observations within the selected target, not the engine
            # host's package database or compiler. No package installation.
            script = r"""
import hashlib, json, pathlib, subprocess, sys
def out(args): return subprocess.check_output(args).decode().strip()
inventory = sorted(out(["rpm","-qa","--qf","%{NAME}\t%{EPOCHNUM}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\n"]).splitlines())
inputs = {}
for name in ("crt1.o","libstdc++.so","libgcc_s.so.1"):
    value = out(["gcc","-print-file-name="+name])
    path = pathlib.Path(value)
    if not path.is_file(): raise RuntimeError("Missing target toolchain input: "+value)
    inputs[name] = {"path":value,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
import SCons
print(json.dumps({"compiler_machine":out(["gcc","-dumpmachine"]),
"compiler_version":out(["gcc","-dumpfullversion"]), "compiler_search":out(["gcc","-print-search-dirs"]),
"sysroot":out(["gcc","-print-sysroot"]), "rpm_arch":out(["rpm","--eval","%{_arch}"]),
"python":sys.version, "scons":SCons.__version__, "packages":inventory,"link_inputs":inputs}))
"""
            log = self.run("sdk-" + arch, self.sdk(arch, [self.sdk_python, "-c", script]))
            identity = json.loads(log.read_text())
            expected = "aarch64" if arch == "arm64" else "arm"
            if not identity["compiler_machine"].startswith(expected):
                raise ValueError("SDK compiler architecture differs: " + arch)
            identity["target"] = self.targets[arch]
            identity["packages_sha256"] = canonical_digest(identity["packages"])
            self.document["sdk"][arch] = identity
        self.save()

    def build_host(self):
        self.run(
            "host-editor",
            [
                self.python,
                "-m",
                "SCons",
                "platform=linuxbsd",
                "arch=x86_64",
                "target=editor",
                "tests=yes",
                "use_static_cpp=no",
                "cache_path=",
                "-j" + str(self.args.jobs),
            ],
        )
        version = self.run("host-version", [self.host, "--headless", "--version"]).read_text().strip()
        revision = self.document["sources"]["engine"]["revision"]
        if revision[:9] not in version:
            raise ValueError("Host editor version does not identify the clean source revision")
        self.copy(self.host, "host/godot", "host")
        self.run(
            "python-tests",
            [self.python, "-m", "unittest", "discover", "-s", "misc/aurora", "-p", "test_*.py"],
            timeout=300,
        )
        self.document["checks"]["python"] = "passed"
        self.run("cpp-tests", [self.host, "--headless", "--test", "--test-case=*Aurora*"], timeout=300)
        self.document["checks"]["cpp"] = "passed"
        self.api.parent.mkdir(parents=True, exist_ok=True)
        self.run("dump-api", [self.host, "--headless", "--dump-extension-api"], cwd=self.api.parent, timeout=120)
        api = json.loads(self.api.read_text())
        if (api["header"]["version_major"], api["header"]["version_minor"]) != (4, 7):
            raise ValueError("Unexpected engine API version")
        self.add(self.api, "api", arch="x86_64")
        interface = self.engine / "core/extension/gdextension_interface.gen.h"
        self.copy(interface, "gdextension_interface.h", "interface")
        self.save()

    def build_engine(self):
        jobs = [
            (arch, target, "compatibility", ["module_raycast_enabled=no"])
            for arch in ("arm32", "arm64")
            for target in TARGETS
        ]
        if self.args.experimental:
            jobs += [
                (arch, "template_debug", "vulkan", ["vulkan=yes", "module_raycast_enabled=no"])
                for arch in ("arm32", "arm64")
            ]
            jobs += [("arm64", target, "embree", ["module_raycast_enabled=yes"]) for target in TARGETS]
        revision = self.document["sources"]["engine"]["revision"]
        for arch, target, profile, flags in jobs:
            suffix = "" if profile == "compatibility" else ".ci_" + profile
            options = flags + ["tests=no", "cache_path="]
            if suffix:
                options.append("extra_suffix=ci_" + profile)
            self.run(
                "engine-" + arch + "-" + target + "-" + profile,
                [
                    self.python,
                    "misc/aurora/build.py",
                    "--sfdk",
                    self.args.sfdk,
                    "--sdk-target",
                    self.targets[arch],
                    "--arch",
                    arch,
                    "--target",
                    target,
                    "--jobs",
                    str(self.args.jobs),
                    "--host-python",
                    self.sdk_python,
                    "--",
                    *options,
                ],
            )
            binary = self.engine / ("bin/godot.auroraos." + target + "." + arch + suffix)
            version = self.run(
                "version-" + arch + "-" + target + "-" + profile,
                self.sdk(arch, [binary, "--headless", "--version"]),
                timeout=60,
            ).read_text()
            if revision[:9] not in version or revision.encode() not in binary.read_bytes():
                raise ValueError("ARM ELF version differs from source: " + str(binary))
            self.copy(
                binary,
                "engine/" + binary.name,
                "engine",
                arch=arch,
                target=target,
                profile=profile,
                flags=options,
                embedded_revision=revision,
            )
        self.save()

        for arch in ("arm32", "arm64"):
            api = self.apis[arch]
            api.parent.mkdir(parents=True)
            editor = self.output / "artifacts/engine" / ("godot.auroraos.editor." + arch)
            self.run(
                "dump-api-" + arch,
                self.sdk(arch, [editor, "--headless", "--dump-extension-api"]),
                cwd=api.parent,
                timeout=180,
            )
            header = json.loads(api.read_text())["header"]
            if (header["version_major"], header["version_minor"], header["version_patch"]) != (4, 7, 2):
                raise ValueError("Target editor API version differs")
            self.add(api, "api", arch=arch, execution_scope="SDK target/emulation, no device")
        self.document["checks"]["api"] = "passed"
        self.save()

    def bindings(self):
        example = self.cpp / "examples/aurora_dependency_chain"
        for arch in ("x86_64", "arm32", "arm64"):
            targets = ("template_debug",) if arch == "x86_64" else ("template_debug", "template_release")
            for target in targets:
                flags = [
                    "platform=" + ("linux" if arch == "x86_64" else "auroraos"),
                    "arch=" + arch,
                    "target=" + target,
                    "custom_api_file=" + str(self.apis[arch]),
                    "build_profile=",
                    "debug_symbols=" + ("yes" if target == "template_debug" else "no"),
                ]
                if arch == "x86_64":
                    flags.append("use_static_cpp=no")
                command = [
                    self.python if arch == "x86_64" else self.sdk_python,
                    "-m",
                    "SCons",
                    *flags,
                    "-j" + str(self.args.jobs),
                ]
                self.run(
                    "cpp-scons-" + arch + "-" + target,
                    command if arch == "x86_64" else self.sdk(arch, command),
                    cwd=example,
                )
                archives = list((self.cpp / "bin").glob("libgodot-cpp.*." + target + "." + arch + ".a"))
                if len(archives) != 1:
                    raise ValueError("Expected one complete SCons binding archive")
                self.copy(
                    archives[0],
                    "bindings/scons/" + arch + "/" + target + "/" + archives[0].name,
                    "bindings_archive",
                    arch=arch,
                    target=target,
                    build_system="scons",
                    api_sha256=sha256(self.apis[arch]),
                    api_coverage="full",
                )
                for name in LIBRARIES:
                    self.copy(
                        example / "project/bin" / arch / name,
                        "bindings/scons/" + arch + "/" + target + "/" + name,
                        "native",
                        arch=arch,
                        build_system="scons",
                        target=target,
                        role="extension" if name == LIBRARIES[0] else "dependency",
                        api_sha256=sha256(self.apis[arch]),
                    )
        for arch, processor in (("arm32", "armv7"), ("arm64", "aarch64")):
            for target, configuration in (("template_debug", "Debug"), ("template_release", "Release")):
                directory = self.output / "build" / ("cmake-" + arch + "-" + target)
                install = self.output / "artifacts/bindings/cmake" / arch / target
                self.run(
                    "cpp-cmake-configure-" + arch + "-" + target,
                    self.sdk(
                        arch,
                        [
                            "cmake",
                            "-S",
                            example,
                            "-B",
                            directory,
                            "-DCMAKE_SYSTEM_NAME=Linux",
                            "-DCMAKE_SYSTEM_PROCESSOR=" + processor,
                            "-DCMAKE_BUILD_TYPE=" + configuration,
                            "-DGODOTCPP_TARGET=" + target,
                            "-DGODOTCPP_CUSTOM_API_FILE=" + str(self.apis[arch]),
                            "-DGODOTCPP_BUILD_PROFILE=",
                            "-DPython3_EXECUTABLE=" + self.sdk_python,
                        ],
                    ),
                    cwd=self.cpp,
                )
                self.run(
                    "cpp-cmake-build-" + arch + "-" + target,
                    self.sdk(arch, ["cmake", "--build", directory, "-j" + str(self.args.jobs)]),
                    cwd=self.cpp,
                )
                self.run(
                    "cpp-cmake-install-" + arch + "-" + target,
                    self.sdk(arch, ["cmake", "--install", directory, "--prefix", install]),
                    cwd=self.cpp,
                )
                archives = list(directory.rglob("libgodot-cpp*.a"))
                if len(archives) != 1:
                    raise ValueError("Expected one complete CMake binding archive")
                self.copy(
                    archives[0],
                    "bindings/cmake/" + arch + "/" + target + "/" + archives[0].name,
                    "bindings_archive",
                    arch=arch,
                    target=target,
                    build_system="cmake",
                    api_sha256=sha256(self.apis[arch]),
                    api_coverage="full",
                )
                for name in LIBRARIES:
                    self.add(
                        install / "lib" / name,
                        "native",
                        arch=arch,
                        build_system="cmake",
                        target=target,
                        role="extension" if name == LIBRARIES[0] else "dependency",
                        api_sha256=sha256(self.apis[arch]),
                    )
        # No build system may replace private lookup with a build-machine path.
        for record in self.document["artifacts"]:
            if record["kind"] == "native":
                if record["elf"]["runpath"] != ["$ORIGIN"]:
                    raise ValueError("Unexpected private RUNPATH: " + record["path"])
                if any("/" in name for name in record["elf"]["needed"]):
                    raise ValueError("Absolute DT_NEEDED: " + record["path"])
        for arch in ("arm32", "arm64"):
            for system in ("scons", "cmake"):
                for target in ("template_debug", "template_release"):
                    project = self.stage_project(arch, target, tag="sdk-" + system, system=system, nested=False)
                    editor = self.output / "artifacts/engine" / ("godot.auroraos.editor." + arch)
                    log = self.run(
                        "sdk-native-chain-" + arch + "-" + system + "-" + target,
                        self.sdk(arch, [editor, "--headless", "--path", project, "--quit-after", "2"]),
                        timeout=180,
                    )
                    if "AURORA_GDEXTENSION_CHAIN_OK" not in log.read_text():
                        raise ValueError("Target native chain did not return 42 inside SDK: " + str(project))
        self.document["checks"]["sdk_native_chain"] = "passed"
        self.save()

    def stage_project(self, arch, target, tag="", system="scons", nested=True):
        project = self.output / "projects" / (arch + "-" + target + ("-" + tag if tag else ""))
        source = self.cpp / "examples/aurora_dependency_chain/project"
        project.mkdir(parents=True)
        for name in ("project.godot", "main.gd", "main.tscn"):
            shutil.copy2(source / name, project / name)
        libraries = {}
        dependencies = {}
        for native_arch in ("x86_64", arch):
            selected = "template_debug" if native_arch == "x86_64" else target
            build_system = "scons" if native_arch == "x86_64" else system
            paths = [f"bin/{native_arch}/{name}" for name in LIBRARIES]
            destinations = ["", ""]
            if nested and native_arch != "x86_64":
                paths = [
                    f"addons/nested/bin/{native_arch}/{LIBRARIES[0]}",
                    f"native source/{native_arch}/{LIBRARIES[1]}",
                    f"native source/{native_arch}/{LIBRARIES[2]}",
                ]
                destinations = [f"vendor/bridge/{native_arch}", f"vendor/leaf/{native_arch}"]
            tag_name = "linux.x86_64" if native_arch == "x86_64" else "auroraos." + native_arch
            libraries[tag_name] = "res://" + paths[0]
            dependencies[tag_name] = {"res://" + paths[index + 1]: destinations[index] for index in range(2)}
            for name, relative in zip(LIBRARIES, paths):
                record = next(
                    item
                    for item in self.document["artifacts"]
                    if item["kind"] == "native"
                    and item["arch"] == native_arch
                    and item["build_system"] == build_system
                    and item["target"] == selected
                    and Path(item["path"]).name == name
                )
                destination = project / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.output / record["path"], destination)
        descriptor = '[configuration]\nentry_symbol="aurora_probe_init"\ncompatibility_minimum="4.7"\n\n[libraries]\n'
        descriptor += "".join(key + "=" + json.dumps(value) + "\n" for key, value in libraries.items())
        descriptor += "\n[dependencies]\n"
        descriptor += "".join(key + "=" + json.dumps(value) + "\n" for key, value in dependencies.items())
        (project / "probe.gdextension").write_text(descriptor)
        return project

    def packages(self):
        revision = self.document["sources"]["engine"]["revision"]
        rpm_arches = {"arm32": "armv7hl", "arm64": "aarch64"}
        for arch in ("arm32", "arm64"):
            for target in TARGETS:
                package_id = "org.godotengine.auroraci" + arch + target.replace("template_", "").replace("_", "")
                destination = self.output / "artifacts/rpm" / arch / target
                destination.mkdir(parents=True)
                binary = self.output / "artifacts/engine" / ("godot.auroraos." + target + "." + arch)
                if target == "editor":
                    self.run(
                        "rpm-editor-" + arch,
                        self.sdk(
                            arch,
                            [
                                self.sdk_python,
                                self.engine / "misc/aurora/package.py",
                                "--binary",
                                binary,
                                "--binary-sha256",
                                sha256(binary),
                                "--arch",
                                arch,
                                "--name",
                                package_id,
                                "--version",
                                "4.7.2",
                                "--release",
                                "1.ci." + revision[:9],
                                "--bundle-xkbcommon",
                                "--patchelf",
                                self.args.patchelf,
                                "--permissions",
                                "Audio;UserDirs;Internet",
                                "--output",
                                destination,
                            ],
                        ),
                    )
                    rpm = (
                        destination
                        / rpm_arches[arch]
                        / (package_id + "-4.7.2-1.ci." + revision[:9] + "." + rpm_arches[arch] + ".rpm")
                    )
                else:
                    project = self.stage_project(arch, target)
                    options = {
                        "binary_format/architecture": arch,
                        "custom_template/debug": str(
                            self.output / "artifacts/engine" / ("godot.auroraos.template_debug." + arch)
                        ),
                        "custom_template/release": str(
                            self.output / "artifacts/engine" / ("godot.auroraos.template_release." + arch)
                        ),
                        "package/unique_name": package_id,
                        "package/version": "4.7.2",
                        "package/release": "1.ci." + revision[:9],
                        "package/permissions": "Audio;UserDirs",
                        "package/license": "MIT",
                        "sdk/sfdk": str(self.args.sfdk),
                        "sdk/target": self.targets[arch],
                        "sdk/python": self.sdk_python,
                        "sdk/patchelf": str(self.args.patchelf),
                        "signing/enabled": False,
                        "remote_deploy/enabled": False,
                    }
                    config = '[preset.0]\nname="Aurora CI"\nplatform="Aurora OS"\nrunnable=true\nexport_filter="all_resources"\n\n[preset.0.options]\n'
                    config += "".join(key + "=" + json.dumps(value) + "\n" for key, value in options.items())
                    (project / "export_presets.cfg").write_text(config)
                    self.run(
                        "import-" + arch + "-" + target,
                        [self.host, "--headless", "--editor", "--path", project, "--import"],
                        timeout=300,
                    )
                    rpm = destination / (package_id + ".rpm")
                    flag = "--export-debug" if target == "template_debug" else "--export-release"
                    self.run(
                        "export-" + arch + "-" + target,
                        [self.host, "--headless", "--path", project, flag, "Aurora CI", rpm],
                        timeout=600,
                    )
                # Explicit Regular-profile contract; packages remain unsigned.
                log = self.run(
                    "regular-" + arch + "-" + target,
                    [self.args.sfdk, "engine", "exec", "rpm-validator", "-p", "regular", rpm],
                    accepted=(0, 2),
                    timeout=180,
                )
                validation_code = self.document["commands"][-1]["returncode"]
                self.add(
                    rpm,
                    "rpm",
                    arch=arch,
                    target=target,
                    profile="regular",
                    signed=False,
                    validation_log=log.relative_to(self.output).as_posix(),
                    validation_returncode=validation_code,
                    validation_status="passed" if validation_code == 0 else "remarks",
                )
                self.inspect_rpm(rpm, arch, target, package_id)
        self.document["checks"]["regular_validation"] = "passed"
        self.document["checks"]["rpm_payload"] = "passed"
        self.save()

    def inspect_rpm(self, rpm, arch, target, package_id):
        root = self.output / "unpacked" / (arch + "-" + target)
        root.mkdir(parents=True)
        query = (
            self
            .run("rpm-metadata-" + arch + "-" + target, ["rpm", "-qp", "--qf", "%{NAME}\\n%{ARCH}\\n", rpm], timeout=30)
            .read_text()
            .splitlines()
        )
        expected_arch = "armv7hl" if arch == "arm32" else "aarch64"
        if query != [package_id, expected_arch]:
            raise ValueError("RPM identity differs")
        producer = subprocess.Popen(["rpm2cpio", str(rpm)], stdout=subprocess.PIPE)
        try:
            result = subprocess.run(
                ["cpio", "-idm", "--quiet", "--no-absolute-filenames"],
                cwd=root,
                stdin=producer.stdout,
                capture_output=True,
                timeout=120,
            )
            producer.stdout.close()
            if producer.wait(timeout=10) or result.returncode:
                raise ValueError("RPM extraction failed")
        finally:
            if producer.poll() is None:
                producer.kill()
                producer.wait()
        from ci_artifacts import elf_record

        data = root / "usr/share" / package_id
        native = json.loads((data / "native-manifest.json").read_text())
        private = {Path(name).name: data / name for name in native["files"] if name.startswith("lib/")}
        for name, expected in native["files"].items():
            installed = root / name.lstrip("/") if name.startswith("/") else data / name
            actual = elf_record(installed)
            if any(actual[key] != value for key, value in expected.items()):
                raise ValueError("Installed ELF/manifest mismatch: " + name)
            for dependency in actual["needed"]:
                if dependency not in private:
                    continue
                candidates = []
                for value in actual["runpath"]:
                    for directory in value.split(":"):
                        if directory != "$ORIGIN" and not directory.startswith("$ORIGIN/"):
                            raise ValueError("Unexpected private RPM search path")
                        candidates.append(
                            (installed.parent / directory.removeprefix("$ORIGIN").lstrip("/") / dependency).resolve()
                        )
                if private[dependency].resolve() not in candidates:
                    raise ValueError("RPM RUNPATH does not reach private dependency: " + dependency)
        if target != "editor":
            if native["schema"] != 2 or len(native["project_libraries"]) != 3:
                raise ValueError("Expected exact GDExtension dependency mapping")
            receipt = json.loads((data / "runtime-receipt.json").read_text())
            for name, digest in receipt["files"].items():
                if sha256(root / name.lstrip("/")) != digest:
                    raise ValueError("RPM active-runtime receipt differs: " + name)
            for project_path, installed in native["project_libraries"].items():
                if f"/{arch}/" not in project_path or installed not in native["files"]:
                    raise ValueError("RPM contains the wrong architecture mapping")

    def desktop(self):
        # The actual fixture executes its extension and draws 3D under a private
        # compositor; no existing desktop session or device is controlled.
        project = self.stage_project("arm64", "template_debug", tag="desktop")
        script = project / "main.gd"
        source = script.read_text()
        source = source.replace("var cube: MeshInstance3D", "var ci_frames := 0\nvar cube: MeshInstance3D", 1)
        source += '\n\tci_frames += 1\n\tif ci_frames == 60:\n\t\tawait RenderingServer.frame_post_draw\n\t\tvar error := get_viewport().get_texture().get_image().save_png(OS.get_environment("AURORA_CI_SCREENSHOT"))\n\t\tprint("AURORA_CI_FRAME:", DisplayServer.get_name(), ":", error)\n\t\tget_tree().quit(error)\n'
        script.write_text(source)
        self.run("native-chain", [self.host, "--headless", "--path", project, "--quit-after", "2"], timeout=60)
        if "AURORA_GDEXTENSION_CHAIN_OK" not in (self.output / "logs/native-chain.log").read_text():
            raise ValueError("Host native chain did not return 42")
        self.document["checks"]["native_chain"] = "passed"
        self.run(
            "linux-wayland",
            [
                self.python,
                self.engine / "misc/aurora/ci_wayland.py",
                "--editor",
                self.host,
                "--project",
                project,
                "--output",
                self.output / "artifacts/wayland",
            ],
            timeout=150,
        )
        self.add(self.output / "artifacts/wayland/frame.png", "screenshot")
        self.document["checks"]["linux_wayland"] = "passed"
        self.save()

    def finish(self):
        # Measure SDK package/toolchain identities again, after every build and
        # validator has completed. A mid-run SDK update invalidates the matrix.
        for arch in ("arm32", "arm64"):
            observation = next(item for item in self.document["commands"] if item["name"] == "sdk-" + arch)
            log = self.run("sdk-final-" + arch, observation["argv"])
            after = json.loads(log.read_text())
            after["target"] = self.targets[arch]
            after["packages_sha256"] = canonical_digest(after["packages"])
            if after != self.document["sdk"][arch]:
                raise ValueError("SDK changed during matrix: " + arch)
        for name, path in (("engine", self.engine), ("godot_cpp", self.cpp)):
            after = source_snapshot(path)
            if after != self.document["sources"][name]:
                raise ValueError("Tracked source changed during matrix: " + name)
        if self.document["controller"]["ci_sha256"] != sha256(Path(__file__)):
            raise ValueError("CI controller changed during matrix")
        known = {item["path"] for item in self.document["artifacts"]}
        for path in sorted((self.output / "artifacts").rglob("*")):
            if path.is_file() and path.relative_to(self.output).as_posix() not in known:
                self.add(path, "auxiliary")
        for path in sorted((self.output / "logs").glob("*.log")):
            self.add(path, "evidence")
        self.document["status"] = "passed"
        verify_manifest(self.output, self.document)
        self.save()
        print("PASS: clean-source software matrix; physical ARM64 remains not-run", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("directory", type=Path)
    build = commands.add_parser("build")
    build.add_argument("--engine", type=Path, default=ROOT)
    build.add_argument("--godot-cpp", type=Path, required=True)
    build.add_argument("--config", type=Path, help="Optional local SDK runner JSON, outside source control")
    build.add_argument("--sfdk", type=Path)
    build.add_argument("--sdk-python", type=Path)
    build.add_argument("--host-python", type=Path)
    build.add_argument("--patchelf", type=Path)
    build.add_argument("--arm32-target")
    build.add_argument("--arm64-target")
    build.add_argument("--jobs", type=int, default=12)
    build.add_argument("--experimental", action=argparse.BooleanOptionalAction, default=True)
    build.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.action == "verify":
        verify_manifest(args.directory.resolve(), json.loads((args.directory / "manifest.json").read_text()))
        print("PASS: manifest and complete software artifact set")
        return
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    configurable = ("sfdk", "sdk_python", "host_python", "patchelf", "arm32_target", "arm64_target")
    settings = json.loads(args.config.read_text()) if args.config else {}
    if set(settings) - set(configurable):
        parser.error("Unknown SDK configuration fields")
    for name in configurable:
        if getattr(args, name) is None:
            setattr(args, name, settings.get(name))
        if not getattr(args, name):
            parser.error("Missing --" + name.replace("_", "-") + " or corresponding local configuration")
    for name in ("engine", "godot_cpp", "sfdk", "sdk_python", "host_python", "patchelf"):
        value = Path(os.path.abspath(getattr(args, name)))
        setattr(args, name, value.resolve() if name in ("engine", "godot_cpp") else value)
    matrix = Matrix(args)
    try:
        matrix.prepare()
        matrix.build_host()
        matrix.build_engine()
        matrix.bindings()
        matrix.packages()
        matrix.desktop()
        matrix.finish()
    except BaseException as error:
        matrix.document["status"] = "failed"
        matrix.document["failure"] = str(error)
        matrix.save()
        raise


if __name__ == "__main__":
    main()
