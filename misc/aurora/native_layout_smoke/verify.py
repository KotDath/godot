#!/usr/bin/env python3
"""Inspect an extracted native-layout fixture RPM, without executing ARM code."""

import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location("aurora_package", Path(__file__).resolve().parents[1] / "package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path, help="Extracted RPM filesystem root")
parser.add_argument("--arch", choices=("arm32", "arm64"), required=True)
parser.add_argument("--application-id", default="ru.kotdath.godotnested")
args = parser.parse_args()
data = (args.root / "usr/share" / args.application_id).resolve()
manifest = json.loads((data / "native-manifest.json").read_text(encoding="utf-8"))
assert manifest["schema"] == 2
assert len(manifest["project_libraries"]) == 3
private = {Path(name).name: name for name in manifest["files"] if name.startswith("lib/")}
for name, info in manifest["files"].items():
    installed = args.root / name.lstrip("/") if name.startswith("/") else data / name
    assert info["arch"] == args.arch, name
    assert package.elf_info(installed) == info, f"Installed ELF differs from manifest: {name}"
    if not name.startswith("lib/"):
        continue
    for dependency in info["needed"]:
        if dependency not in private:
            continue
        expected = (data / private[dependency]).resolve()
        candidates = []
        for value in info["runpath"]:
            for search in value.split(":"):
                assert package.valid_origin_runpath(search), search
                candidates.append(
                    (installed.parent / search.removeprefix("$ORIGIN").lstrip("/") / dependency).resolve()
                )
        assert expected in candidates, f"Private RUNPATH does not reach {dependency} from {name}"
for project, installed in manifest["project_libraries"].items():
    assert project.startswith("res://") and f"/{args.arch}/" in project, project
    assert installed.startswith("lib/project/") and installed in manifest["files"], installed
    assert (data / installed).is_file(), installed
print(f"PASS: {args.arch} ELF/manifest match, 3 exact project mappings and private RUNPATH closure")
