# Aurora native libraries

Project GDExtensions are selected by Godot's feature tags, then exported through
an explicit resource-path map. No shared Linux loader or Wayland backend changes
are involved. The Aurora OS adapter handles installed paths; `package.py` handles
ELF inspection and relocation.

## Layout and mapping

The desktop exporter passes `--native-map` with a schema-1 JSON document:

```json
{
  "schema": 1,
  "libraries": [
    {
      "source": "/workspace/game/addons/example/bin/arm64/libexample.so",
      "project": "res://addons/example/bin/arm64/libexample.so",
      "target": "",
      "sha256": "<host SHA256 of the source file>"
    }
  ]
}
```

`source` is an SDK-visible input file. Its host digest detects stale/truncated
SDK shared-mount reads. The digest is not a signature. `project` is the exact
resource URI from the selected SharedObject; native files outside the project
must be copied into the project before exporting. `target` is Godot's dependency
export folder: empty preserves the project-relative path, `.` uses the export
root, and a relative folder explicitly relocates that dependency.

Project files are installed under `/usr/share/<app-id>/lib/project/`. For example:

| Resource | Export target | Installed path relative to application data |
| --- | --- | --- |
| `res://addons/example/bin/arm64/libexample.so` | empty | `lib/project/addons/example/bin/arm64/libexample.so` |
| `res://native source/arm64/libbridge.so` | `vendor/bridge/arm64` | `lib/project/vendor/bridge/arm64/libbridge.so` |
| `res://native source/arm64/libleaf.so` | `vendor/leaf/arm64` | `lib/project/vendor/leaf/arm64/libleaf.so` |

The installed `native-manifest.json` has schema 2, `project_libraries` mapping
resource URIs to these paths, and `files` containing ELF metadata. Host source
paths are not written into the installed map. Existing CLI `--private-library`
and `--native-dir` inputs remain in the flat `lib/` directory; this supports
manually packaged editors and the bundled SDK libxkbcommon. Direct dependencies
of the main executable must stay in that flat directory for its required RPATH.

The loader prefers an existing project-local library while editing. When it is
absent, an exported project map resolves the exact resource URI. A missing map
entry cannot silently match another plugin's basename. Earlier flat RPMs and
manual packages with no project map retain their original flat lookup.

## Linker contract

Every private DSO gets its own DT_RUNPATH: `$ORIGIN` plus relative directories
of its declared private DT_NEEDED dependencies. Thus a dependency in another
folder is found without making RUNPATH transitive or adding global loader paths.
The main ELF retains the validator-required application-private DT_RPATH.

All private basenames must remain unique across a package. A SONAME, when present,
must match its supplied filename. This deliberately also rejects duplicate
basenames without SONAME: directory isolation alone does not provide independent
ELF symbol namespaces with the engine's normal dlopen policy. Rename/relink
conflicting third-party libraries and their DT_NEEDED entries before export.
The packager does not silently rewrite SONAMEs or introduce dlmopen namespaces.
DT_NEEDED must contain library names, not build-machine paths. Public dependencies
must resolve in the selected target sysroot. No arbitrary SDK library is copied.

Resource and target directories may contain spaces and Unicode. Canonical relative
paths are required: no traversal, empty segments, control characters, RPM macros
or loader token/separator characters. Private library filenames keep the existing
portable ASCII SONAME contract. ARM32 hard-float and ARM64 checks remain mandatory.

## Reloading and updates

Godot's existing GDExtension reload mechanism and godot-cpp's object recreation
remain the authority for reload. Build with `use_hot_reload=yes` (SCons) or
`GODOTCPP_USE_HOT_RELOAD=ON` (CMake), and set `reloadable=true` in `.gdextension`.
Aurora godot-cpp suppresses GNU unique symbols when reload is enabled. Extension
code must still release its resources, stop threads and unregister callbacks
before unloading; incompatible ABI or registration changes require restarting.

A writable project directory does not prove that an installed Aurora security
profile permits loading code from it. Developer runs and normal launcher runs
need separate evidence. An installed RPM's libraries are immutable application
content: distribute a new signed RPM and restart the app for a package update.
Replacing that package while the old process runs is not a supported hot-reload
transport. Dependency-only changes are not automatically watched by Godot's
extension-library watcher; restart or explicitly manage the complete unload.

Reproducible nested-layout fixture: [native_layout_smoke/README.md](native_layout_smoke/README.md).
Actual build, device and reload acceptance is tracked in [STATUS.md](STATUS.md).
