"""Boundary checks for RPM native dependency validation (no SDK required)."""

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("aurora_package", Path(__file__).with_name("package.py"))
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


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


if __name__ == "__main__":
    unittest.main()
