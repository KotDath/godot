# Building the Aurora platform

See [STATUS.md](STATUS.md) for actual test coverage and [PLAN.md](PLAN.md) for
the architectural plan. Commands below run at the Godot repository root.
The host Python/SCons are build tools, not target runtime dependencies.

## SDK bootstrap and compilation

Install target development packages `wayland-devel`, `wayland-egl-devel`,
`libxkbcommon-devel`, `pulseaudio-devel` in each selected SDK target first.
Use an x86_64 target to bootstrap the tools which run on the Build Engine:

```sh
python3 misc/aurora/bootstrap.py --help
python3 misc/aurora/bootstrap.py --download-only --prefix .aurora-tools/sdk-x86_64 --cache .aurora-cache
sfdk engine exec sb2 -t AuroraOS-5.2.1.200-x86_64 -m sdk-build \
  python3 misc/aurora/bootstrap.py --offline --prefix .aurora-tools/sdk-x86_64 --cache .aurora-cache
python3 misc/aurora/build.py --sdk-target AuroraOS-5.2.1.200-aarch64 \
  --arch arm64 --target editor \
  --host-python .aurora-tools/sdk-x86_64/bin/python3.12 -- module_raycast_enabled=no
```

For ARM32 select an `armv7hl` target and `--arch arm32`. Targets are explicit
parameters, not hardcoded OS compatibility requirements. `--target` also
accepts `template_debug` and `template_release`. Pass `--sfdk /path/to/sfdk`
if it is not on PATH. `--dry-run` prints the exact command.

The runner uses sfdk to enter the SDK and SB2 to select the target without
mutating global sfdk settings. It does not yet integrate `sfdk build` recipes.
Linux and Aurora use different generated Wayland protocols, module lists and
editor registration/documentation outputs. The SDK runner temporarily regenerates
these configuration-dependent sources and restores the host variants even after
a failed build. Run builds sequentially in a checkout; use
separate worktrees for concurrent host and target builds.

`--environment psdk` expects an already-entered PSDK; `direct` expects explicit
cross-toolchain options. These two paths have not been build-tested.

## Desktop export

The editor includes the **Aurora OS** export platform for ARM32/ARM64 RPMs.
See [EXPORT.md](EXPORT.md) for presets, SDK settings and GDExtension layout.

## Standalone RPM packaging

`package.py --help` describes editor and exported-game inputs. Run it under the same
SB2 target as the binary. Pass only one architecture's directory as
`--native-dir`. Include the target `libxkbcommon.so.0` with `--private-library`
and its license using `--license`. Run Regular validation before installation.
Only editor builds accept an unpacked `--project`: normal export templates
keep upstream path override restrictions and use `--pck`. The GUI exporter
creates that PCK with Godot's standard export pipeline.

Use patchelf 0.18.0: SDK patchelf 0.11 was observed to remove the executable's
build-id. Tested static host distribution:
https://github.com/NixOS/patchelf/releases/download/0.18.0/patchelf-0.18.0-x86_64.tar.gz
SHA256: `ce84f2447fb7a8679e58bc54a20dc2b01b37b5802e12c57eece772a6f14bf3f0`.
Pass the extracted host executable with `--patchelf`.

Device commands:

```sh
audb -d phone package install /absolute/path/to/signed/package.rpm
audb -d phone shell '/usr/bin/ru.kotdath.godotprobe --headless --path /usr/share/ru.kotdath.godotprobe/project --script res://probe.gd 2>&1; echo EXIT:$?'
```

SDK public Regular credentials are for development only. Keep keys outside
source control. Do not substitute direct SSH or change device security policy.
