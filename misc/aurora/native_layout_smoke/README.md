# Nested GDExtension RPM smoke

Build `examples/aurora_dependency_chain` in the adapted godot-cpp checkout for
Linux x86_64, Aurora ARM64 and ARM32. Stage a new project:

```sh
python3 misc/aurora/native_layout_smoke/prepare.py \
  --libraries /path/to/godot-cpp/examples/aurora_dependency_chain/project/bin \
  --output /path/to/new-native-layout-project
```

The host preview keeps the three libraries together for ordinary `$ORIGIN`
loading. ARM inputs put the extension in `addons/nested/bin/<arch>` and its
dependencies in `native source/<arch>` (a path containing a space). Explicit
dependency targets install middle and leaf into separate `vendor` directories.

Import the generated project with the desktop editor, create Aurora export
presets and export both architectures with matching templates and SDK targets.
Inspect the RPM's `native-manifest.json`, ELF class/ARM32 hard-float flags and
each DSO's RUNPATH. No Linux x86_64 library may enter either ARM package. The
map must retain original resource keys even for relocated dependencies.

After extracting the RPM with `rpm2cpio`/`cpio`, verify the installed ELF metadata
and that each private dependency is actually reachable through its own RUNPATH:

```sh
python3 misc/aurora/native_layout_smoke/verify.py /path/to/extracted-root --arch arm64
```

Sign/install ARM64 on a real device, launch normally and confirm **Nested
GDExtension chain = 42** and `user://native-layout.json` with `value=42`. Record
the exact installed RPM/executable and check `/proc/PID/maps` for all three
mapped library locations. Collect the fixture's files inside its app mount
namespace before stopping the process. ARM32 hardware testing is separate.

Failure controls: a wrong-architecture file, an omitted leaf, duplicate basename
or conflicting SONAME must fail before producing a distributable RPM. This
fixture checks layout and transitive loading; it does not establish hot reload.
