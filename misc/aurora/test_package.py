"""Boundary checks for RPM native dependency validation (no SDK required)."""

import hashlib
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path, PurePosixPath
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("aurora_package", Path(__file__).with_name("package.py"))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class EmbeddedPackTests(unittest.TestCase):
    def test_runtime_receipt_hashes_final_pack_and_nested_private_library(self):
        with tempfile.TemporaryDirectory(prefix="aurora receipt ") as directory:
            stage = Path(directory)
            executable = stage / "usr/bin/org.example.game"
            library = stage / "usr/share/org.example.game/lib/plugin dir/ёж.so"
            executable.parent.mkdir(parents=True)
            library.parent.mkdir(parents=True)
            executable.write_bytes(b"stripped ELF")
            library.write_bytes(b"patched private ELF")
            pck = stage / "export.pck"
            pck.write_bytes(b"GDPC" + b"resources" * 150000)
            package.append_pck(executable, pck)
            receipt = package.runtime_receipt(
                stage, [executable, library], "org.example.game", "1.0.0", "1.godot.42", "arm64"
            )
            self.assertEqual(receipt["version"], "1.0.0")
            self.assertEqual(receipt["release"], "1.godot.42")
            self.assertEqual(
                receipt["files"],
                {
                    "/usr/bin/org.example.game": hashlib.sha256(executable.read_bytes()).hexdigest(),
                    "/usr/share/org.example.game/lib/plugin dir/ёж.so": hashlib.sha256(
                        library.read_bytes()
                    ).hexdigest(),
                },
            )
            self.assertNotIn(directory, json.dumps(receipt))
            pck.write_bytes(b"GDPCchanged game")
            executable.write_bytes(b"stripped ELF")
            package.append_pck(executable, pck)
            changed = package.runtime_receipt(
                stage, [executable, library], "org.example.game", "1.0.0", "1.godot.42", "arm64"
            )
            self.assertNotEqual(
                changed["files"]["/usr/bin/org.example.game"], receipt["files"]["/usr/bin/org.example.game"]
            )

    def test_rejects_truncated_sdk_view_of_pack(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "game"
            pck = Path(directory) / "game.pck"
            payload = b"GDPC" + b"resource data"
            executable.write_bytes(b"ELF")
            pck.write_bytes(payload[:-3])
            with self.assertRaisesRegex(ValueError, "PCK SHA256 mismatch"):
                package.append_pck(executable, pck, hashlib.sha256(payload).hexdigest())

    def test_footer_uses_copied_bytes_with_stale_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "game"
            pck = Path(directory) / "game.pck"
            prefix = b"ELF placeholder"
            payload = b"GDPC" + b"resources" * 150000
            executable.write_bytes(prefix)
            pck.write_bytes(payload)
            # Models fakeowner reporting the old size after a host-side export.
            with patch.object(Path, "stat") as stat:
                stat.return_value.st_size = 3648
                package.append_pck(executable, pck, hashlib.sha256(payload).hexdigest())
            result = executable.read_bytes()
            size, magic = struct.unpack("<Q4s", result[-12:])
            self.assertEqual(magic, b"GDPC")
            self.assertEqual(result[-12 - size : -12], payload)
            self.assertEqual(result[: -12 - size], prefix)

    def test_invalid_pack_leaves_executable_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "game"
            pck = Path(directory) / "game.pck"
            executable.write_bytes(b"original")
            pck.write_bytes(b"not a PCK")
            with self.assertRaisesRegex(ValueError, "Not a Godot PCK"):
                package.append_pck(executable, pck)
            self.assertEqual(executable.read_bytes(), b"original")


class NativeValidationTests(unittest.TestCase):
    def test_arm32_requires_hard_float(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.so"
            header = bytearray(52)
            header[:6] = b"\x7fELF\x01\x01"
            header[18:20] = (40).to_bytes(2, "little")
            path.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "hard-float"):
                package.elf_info(path)
            header[36:40] = (0x05000400).to_bytes(4, "little")
            path.write_bytes(header)
            with patch.object(package.subprocess, "check_output", return_value=""):
                self.assertEqual(package.elf_info(path)["arch"], "arm32")

    def test_rejects_private_basename_collisions(self):
        info = {"arch": "arm64", "needed": [], "soname": [], "runpath": []}
        with patch.object(package, "elf_info", return_value=info):
            with self.assertRaisesRegex(ValueError, "collision"):
                package.validate_native([Path("one/probe.so"), Path("two/probe.so")], "arm64")

    def test_rejects_wrong_architecture(self):
        info = {"arch": "arm32", "needed": [], "soname": [], "runpath": []}
        with patch.object(package, "elf_info", return_value=info):
            with self.assertRaisesRegex(ValueError, "Architecture mismatch"):
                package.validate_native([Path("probe.so")], "arm64")

    def test_transitive_library_requires_own_origin(self):
        infos = {
            "app": {"needed": ["middle.so"], "runpath": []},
            "middle.so": {"needed": ["leaf.so"], "runpath": []},
            "leaf.so": {"needed": [], "runpath": ["$ORIGIN"]},
        }

        def info(path):
            return {"arch": "arm64", "soname": [], **infos[path.name]}

        files = [Path(name) for name in infos]
        with patch.object(package, "elf_info", side_effect=info):
            with self.assertRaisesRegex(ValueError, "Missing.*ORIGIN"):
                package.validate_native(files, "arm64")
            infos["middle.so"]["runpath"] = ["$ORIGIN"]
            self.assertEqual(len(package.validate_native(files, "arm64")), 3)

    def test_rejects_build_machine_runpath(self):
        info = {"arch": "arm64", "needed": [], "soname": [], "runpath": ["/home/developer/lib"]}
        with patch.object(package, "elf_info", return_value=info):
            with self.assertRaisesRegex(ValueError, "Only private"):
                package.validate_native([Path("probe.so")], "arm64")

    def test_accepts_origin_relative_input_and_rejects_host_search(self):
        self.assertTrue(package.valid_origin_runpath("$ORIGIN/../Общие библиотеки"))
        self.assertFalse(package.valid_origin_runpath("/usr/lib"))
        self.assertFalse(package.valid_origin_runpath("$ORIGIN/$LIB"))

    def test_rejects_absolute_dt_needed(self):
        info = {"arch": "arm64", "needed": ["/host/build/libprivate.so"], "soname": [], "runpath": []}
        with patch.object(package, "elf_info", return_value=info):
            with self.assertRaisesRegex(ValueError, "DT_NEEDED must be a library name"):
                package.validate_native([Path("game")], "arm64")


class NativeLayoutTests(unittest.TestCase):
    def test_preserves_project_tree_and_explicit_dependency_target(self):
        with tempfile.TemporaryDirectory(prefix="aurora native ") as directory:
            root = Path(directory)
            extension = root / "libentry.so"
            dependency = root / "libleaf.so"
            extension.write_bytes(b"extension")
            dependency.write_bytes(b"dependency")
            document = {
                "schema": 1,
                "libraries": [
                    {
                        "source": str(extension),
                        "project": "res://addons/Мой plugin/libentry.so",
                        "sha256": hashlib.sha256(extension.read_bytes()).hexdigest(),
                    },
                    {
                        "source": str(dependency),
                        "project": "res://addons/original/libleaf.so",
                        "target": "vendor/native",
                    },
                ],
            }
            input_map = root / "native.json"
            input_map.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
            entries = package.read_native_map(input_map)
            destinations, paths = package.native_layout([extension, dependency], entries)
            self.assertEqual(paths["res://addons/Мой plugin/libentry.so"], "lib/project/addons/Мой plugin/libentry.so")
            self.assertEqual(paths["res://addons/original/libleaf.so"], "lib/project/vendor/native/libleaf.so")
            runpath = package.private_runpath(
                extension, {"needed": ["libleaf.so"]}, [extension, dependency], destinations
            )
            self.assertEqual(runpath, "$ORIGIN:$ORIGIN/../../vendor/native")
            self.assertEqual(
                package.private_runpath(dependency, {"needed": []}, [extension, dependency], destinations), "$ORIGIN"
            )
            extension.write_bytes(b"truncated SDK view")
            with self.assertRaisesRegex(ValueError, "Native SHA256 mismatch"):
                package.read_native_map(input_map)

    def test_rejects_traversal_and_ambiguous_targets(self):
        for path in (
            "../escape.so",
            "/absolute.so",
            "one//file.so",
            "one/./file.so",
            "one/%macro.so",
            "one/$LIB/file.so",
            "C:/file.so",
        ):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "Invalid native relative path"):
                package.relative_native_path(path)
        source = Path("/source/libentry.so")
        entries = [
            (source, "res://one/libentry.so", PurePosixPath("lib/project/one/libentry.so")),
            (source, "res://two/libentry.so", PurePosixPath("lib/project/two/libentry.so")),
        ]
        with self.assertRaisesRegex(ValueError, "conflicting installation targets"):
            package.native_layout([source], entries)
        other = Path("/other/libentry.so")
        entries[1] = (other, "res://two/libentry.so", entries[0][2])
        with self.assertRaisesRegex(ValueError, "installation path collision"):
            package.native_layout([source, other], entries)

    def test_flat_cli_libraries_keep_origin_layout(self):
        files = [Path("/tmp/libmiddle.so"), Path("/tmp/libleaf.so")]
        destinations, paths = package.native_layout(files, [])
        self.assertEqual(paths, {})
        self.assertEqual(destinations[files[0]], PurePosixPath("lib/libmiddle.so"))
        self.assertEqual(package.private_runpath(files[0], {"needed": ["libleaf.so"]}, files, destinations), "$ORIGIN")


if __name__ == "__main__":
    unittest.main()
