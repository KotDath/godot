"""Real Git/file/ELF negative controls for the artifact gate."""

import argparse
import json
import shutil
import subprocess
import tempfile
import unittest
import venv
from pathlib import Path

from ci_artifacts import artifact_record, confined_file, source_snapshot, verify_artifacts, verify_manifest


class DriverTests(unittest.TestCase):
    def test_python_venv_invocation_path_keeps_its_prefix(self):
        from ci import Matrix

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            environment = root / "venv"
            venv.EnvBuilder(with_pip=False, symlinks=True).create(environment)
            executable = environment / "bin/python"
            matrix = Matrix(
                argparse.Namespace(
                    output=root / "matrix",
                    experimental=False,
                    arm32_target="example-armv7hl",
                    arm64_target="example-aarch64",
                    sdk_python=executable,
                    host_python=executable,
                )
            )
            prefix = subprocess.check_output([matrix.python, "-c", "import sys;print(sys.prefix)"], text=True).strip()
            self.assertEqual(Path(prefix), environment)


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / "source.c").write_text("int value(void) { return 42; }\n")
        self.git("add", "source.c")
        self.git("-c", "user.name=CI", "-c", "user.email=ci@example.invalid", "commit", "-qm", "source")

    def git(self, *arguments):
        return subprocess.check_output(["git", "-C", str(self.root), *arguments], text=True)

    def test_tracked_bytes_are_measured_and_dirty_source_rejected(self):
        snapshot = source_snapshot(self.root)
        self.assertEqual(snapshot["revision"], self.git("rev-parse", "HEAD").strip())
        self.assertEqual(len(snapshot["tracked_files"]), 1)
        (self.root / "source.c").write_text("int value(void) { return 43; }\n")
        with self.assertRaisesRegex(ValueError, "not clean"):
            source_snapshot(self.root)
        self.git("add", "source.c")
        self.git("-c", "user.name=CI", "-c", "user.email=ci@example.invalid", "commit", "-qm", "changed")
        changed = source_snapshot(self.root)
        self.assertNotEqual(snapshot["revision"], changed["revision"])
        self.assertNotEqual(snapshot["tracked_files_sha256"], changed["tracked_files_sha256"])

    def test_untracked_build_override_is_not_clean_source(self):
        (self.root / "custom.py").write_text("target = 'different'\n")
        with self.assertRaisesRegex(ValueError, "not clean"):
            source_snapshot(self.root)

    def test_symlink_identity_is_the_link_and_not_external_contents(self):
        (self.root / "link").symlink_to("source.c")
        self.git("add", "link")
        self.git("-c", "user.name=CI", "-c", "user.email=ci@example.invalid", "commit", "-qm", "link")
        record = next(item for item in source_snapshot(self.root)["tracked_files"] if item["path"] == "link")
        self.assertEqual(record["mode"], "120000")
        (self.root / "link").unlink()
        (self.root / "link").symlink_to("/outside/source.c")
        with self.assertRaisesRegex(ValueError, "not clean"):
            source_snapshot(self.root)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def test_bytes_cannot_be_replaced_even_with_the_same_size(self):
        path = self.root / "artifact"
        path.write_bytes(b"original")
        record = artifact_record(self.root, path, "test-data")
        verify_artifacts(self.root, [record])
        path.write_bytes(b"replaced")
        self.assertEqual(path.stat().st_size, record["size"])
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            verify_artifacts(self.root, [record])

    def test_missing_duplicate_and_escaping_artifacts_fail(self):
        path = self.root / "artifact"
        path.write_bytes(b"data")
        record = artifact_record(self.root, path, "test-data")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            verify_artifacts(self.root, [record, record])
        for name in ("../outside", "/etc/passwd", "folder/../artifact", "./artifact"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                confined_file(self.root, name)
        (self.root / "link").symlink_to(self.root / "artifact")
        with self.assertRaisesRegex(ValueError, "symbolic"):
            confined_file(self.root, "link")
        path.unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            verify_artifacts(self.root, [record])

    @unittest.skipUnless(shutil.which("cc") and shutil.which("readelf"), "Native C tools required")
    def test_real_host_elf_cannot_be_labelled_as_arm(self):
        source = self.root / "test.c"
        source.write_text("int main(void) { return 0; }\n")
        binary = self.root / "test"
        subprocess.run(["cc", str(source), "-o", str(binary)], check=True)
        record = artifact_record(self.root, binary, "native")
        verify_artifacts(self.root, [record])
        if record["elf"]["arch"] == "x86_64":
            with self.assertRaisesRegex(ValueError, "architecture mismatch"):
                artifact_record(self.root, binary, "native", arch="arm64")
        record["elf"]["machine"] = 0
        with self.assertRaisesRegex(ValueError, "ELF identity"):
            verify_artifacts(self.root, [record])

    def test_partial_matrix_and_hardware_claims_cannot_pass(self):
        manifest = {
            "schema": 1,
            "status": "passed",
            "hardware": {"arm32": "excluded", "arm64": "not-run"},
            "artifacts": [],
        }
        # JSON round trip matches a downloaded manifest, not in-memory identity.
        with self.assertRaisesRegex(ValueError, "six"):
            verify_manifest(self.root, json.loads(json.dumps(manifest)))
        manifest["hardware"]["arm64"] = "passed"
        with self.assertRaisesRegex(ValueError, "physical"):
            verify_manifest(self.root, manifest)
        manifest["status"] = "running"
        with self.assertRaisesRegex(ValueError, "completed"):
            verify_manifest(self.root, manifest)

    @unittest.skipUnless(
        shutil.which("cc") and shutil.which("ar") and shutil.which("readelf"), "Native archive tools required"
    )
    def test_all_archive_members_are_checked_instead_of_its_filename(self):
        objects = []
        for index in range(2):
            source = self.root / f"source{index}.c"
            source.write_text(f"int value{index}(void) {{ return 42; }}\n")
            obj = source.with_suffix(".o")
            subprocess.run(["cc", "-c", str(source), "-o", str(obj)], check=True)
            objects.append(obj)
        archive = self.root / "pretend-arm64.a"
        subprocess.run(["ar", "rcs", str(archive), *map(str, objects)], check=True)
        from ci_artifacts import archive_record

        actual = archive_record(archive)
        self.assertEqual(actual["elf_members"], 2)
        record = artifact_record(self.root, archive, "bindings_archive", arch=actual["arch"])
        verify_artifacts(self.root, [record])
        wrong = "arm64" if actual["arch"] != "arm64" else "arm32"
        with self.assertRaisesRegex(ValueError, "architecture mismatch"):
            artifact_record(self.root, archive, "bindings_archive", arch=wrong)


if __name__ == "__main__":
    unittest.main()
