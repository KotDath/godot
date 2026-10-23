"""Check generated Wayland outputs survive failed SDK builds."""

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import methods

spec = importlib.util.spec_from_file_location("aurora_build", Path(__file__).with_name("build.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class ProvenanceTests(unittest.TestCase):
    def test_host_metadata_forwarded_inside_sdk_argv(self):
        revision = "a" * 40
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".git").write_text("gitdir: /outside/worktrees/test\n")
            with patch.object(build.subprocess, "check_output", return_value=revision + "\n1234\n") as git:
                provenance = build.source_provenance(root)
                self.assertEqual(git.call_args.kwargs["cwd"], root)
        original = ["/SDK with spaces/sfdk", "engine", "exec", "sb2", "-t", "target", "python", "-m", "SCons"]
        command, environment = build.with_source_provenance(original, "sdk", provenance)
        self.assertEqual(command[:4], original[:3] + ["env"])
        self.assertIn("GODOT_BUILD_GIT_HASH=" + revision, command)
        self.assertEqual(command[-6:], original[-6:])
        self.assertEqual(environment["GODOT_BUILD_GIT_TIMESTAMP"], "1234")
        self.assertEqual(original[3], "sb2")

    def test_source_archive_does_not_use_parent_repository(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(build.subprocess, "check_output") as git:
            self.assertEqual(build.source_provenance(Path(directory)), {})
            git.assert_not_called()

    def test_direct_runner_keeps_argv_and_sets_environment(self):
        original = ["python", "-m", "SCons"]
        command, environment = build.with_source_provenance(original, "direct", {"GODOT_BUILD_GIT_HASH": "b" * 40})
        self.assertEqual(command, original)
        self.assertEqual(environment["GODOT_BUILD_GIT_HASH"], "b" * 40)

    def test_override_ignores_stale_sdk_git(self):
        provenance = {"GODOT_BUILD_GIT_HASH": "c" * 40, "GODOT_BUILD_GIT_TIMESTAMP": "1234"}
        with patch.dict(os.environ, provenance), patch.object(methods.subprocess, "check_output") as git:
            self.assertEqual(methods.get_git_info(), {"git_hash": "c" * 40, "git_timestamp": "1234"})
            git.assert_not_called()

    def test_timestamp_is_decimal_not_a_cpp_octal_literal(self):
        provenance = {"GODOT_BUILD_GIT_HASH": "c" * 40, "GODOT_BUILD_GIT_TIMESTAMP": "01234"}
        with patch.dict(os.environ, provenance):
            self.assertEqual(methods.get_git_info()["git_timestamp"], "1234")

    def test_default_git_lookup_is_unchanged(self):
        previous = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".git").mkdir()
            (root / ".git/HEAD").write_text("d" * 40 + "\n")
            try:
                with patch.dict(os.environ, {}, clear=True), patch.object(methods, "base_folder", root):
                    with patch.object(methods.subprocess, "check_output", return_value="1234"):
                        self.assertEqual(methods.get_git_info(), {"git_hash": "d" * 40, "git_timestamp": "1234"})
            finally:
                os.chdir(previous)

    def test_rejects_incomplete_or_nonliteral_metadata(self):
        for revision, timestamp in [
            ("c" * 40, None),
            (None, "1234"),
            ("not-a-hash", "1234"),
            ("c" * 40, "-1"),
            ("c" * 40, "1;bad"),
            ("c" * 40, "١٢٣"),
            ("c" * 40, str(2**64)),
        ]:
            with self.subTest(revision=revision, timestamp=timestamp), patch.dict(os.environ, {}, clear=True):
                if revision is not None:
                    os.environ["GODOT_BUILD_GIT_HASH"] = revision
                if timestamp is not None:
                    os.environ["GODOT_BUILD_GIT_TIMESTAMP"] = timestamp
                with self.assertRaises(ValueError):
                    methods.get_git_info()


class ProtocolRestorationTests(unittest.TestCase):
    def test_restores_host_variant_after_failed_build(self):
        run = subprocess.run

        def sdk_run(command, **kwargs):
            # Exercise the actual removal/restoration scripts without an SDK.
            return run([sys.executable, *command[4:]], **kwargs)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            protocols = root / "platform/linuxbsd/wayland/protocol"
            protocols.mkdir(parents=True)
            header = protocols / "sample.gen.h"
            source = protocols / "sample.gen.c"
            original = b'#include "../dynwrappers/wayland-client-so_wrap.h"\n'
            header.write_bytes(original)
            source.write_bytes(b"host source\n")
            module = root / "modules/modules_enabled.gen.h"
            module.parent.mkdir()
            module.write_bytes(b"#define MODULE_RAYCAST_ENABLED\n")
            with patch.object(build.subprocess, "run", side_effect=sdk_run):
                with self.assertRaisesRegex(RuntimeError, "build failed"):
                    with build.sdk_generated_sources("sfdk", root):
                        self.assertFalse(header.exists())
                        self.assertFalse(source.exists())
                        self.assertFalse(module.exists())
                        header.write_bytes(b"direct linking\n")
                        raise RuntimeError("build failed")
            self.assertEqual(header.read_bytes(), original)
            self.assertEqual(source.read_bytes(), b"host source\n")
            self.assertEqual(module.read_bytes(), b"#define MODULE_RAYCAST_ENABLED\n")

    def test_empty_checkout_needs_no_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(build.subprocess, "run") as run:
                with build.sdk_generated_sources("sfdk", Path(directory)):
                    pass
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
