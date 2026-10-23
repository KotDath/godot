"""Check generated Wayland outputs survive failed SDK builds."""

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("aurora_build", Path(__file__).with_name("build.py"))
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


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
