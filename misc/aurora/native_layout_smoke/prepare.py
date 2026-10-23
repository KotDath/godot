#!/usr/bin/env python3
"""Stage the godot-cpp dependency chain in a deliberately split plugin tree."""

import argparse
import shutil
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--libraries", type=Path, required=True, help="Built godot-cpp example's project/bin directory")
parser.add_argument("--output", type=Path, required=True, help="New project directory")
args = parser.parse_args()
if args.output.exists():
    parser.error("Output must be a new directory")
args.output.mkdir(parents=True)
for name in ("project.godot", "main.gd", "main.tscn"):
    shutil.copy2(Path(__file__).parent / name, args.output / name)
config = ["[configuration]", 'entry_symbol="aurora_probe_init"', 'compatibility_minimum="4.7"', "", "[libraries]"]
dependencies = []
for arch in ("x86_64", "arm64", "arm32"):
    tag = "linux.x86_64" if arch == "x86_64" else "auroraos." + arch
    entry = f"addons/nested/bin/{arch}/libaurora_probe.so"
    # The host preview keeps $ORIGIN neighbours; ARM inputs deliberately use
    # separate source directories and explicit export destinations.
    bridge = (
        f"addons/nested/bin/{arch}/libaurora_probe_middle.so"
        if arch == "x86_64"
        else f"native source/{arch}/libaurora_probe_middle.so"
    )
    leaf = (
        f"addons/nested/bin/{arch}/libaurora_probe_leaf.so"
        if arch == "x86_64"
        else f"native source/{arch}/libaurora_probe_leaf.so"
    )
    for source, target in (
        ("libaurora_probe.so", entry),
        ("libaurora_probe_middle.so", bridge),
        ("libaurora_probe_leaf.so", leaf),
    ):
        destination = args.output / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(args.libraries / arch / source, destination)
    config.append(f'{tag}="res://{entry}"')
    bridge_target = "" if arch == "x86_64" else "vendor/bridge/" + arch
    leaf_target = "" if arch == "x86_64" else "vendor/leaf/" + arch
    dependencies.append(f'{tag}={{"res://{bridge}": "{bridge_target}", "res://{leaf}": "{leaf_target}"}}')
(args.output / "probe.gdextension").write_text("\n".join(config + ["", "[dependencies]"] + dependencies) + "\n")
