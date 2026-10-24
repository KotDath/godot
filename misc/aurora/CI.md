# Aurora source and artifact matrix

The software matrix runs without deploying or controlling a device. Its manifest
cannot promote a compile/SDK result to physical acceptance. ARM32 hardware is
excluded from the current goal; ARM64 hardware has its own evidence in STATUS.md.

## Inputs and build scope

`ci.py build` requires clean engine and godot-cpp repositories. It makes new,
detached worktrees and measures every tracked file on the source host before and
after the run. Existing ignored custom settings, generated headers, object files
and native libraries are not copied into them. Builds run sequentially; SDK source
generation/restoration uses the existing build runner.

The default matrix contains:

- Linux editor with tests and ordinary version lookup.
- Editor/debug/release for ARM32 and ARM64, with source-host revision metadata.
- Opt-in Vulkan debug templates for both ARM architectures and Embree editor/
  debug/release for ARM64. `--no-experimental` explicitly selects only the baseline.
- API JSON from the host editor and both actual ARM editors executed headlessly
  in their SDK targets. An API dump is not a graphical/device test.
- Full godot-cpp static libraries and the dependency-chain example: SCons and
  CMake, debug/release, both ARM architectures. The example's trimmed profile is
  disabled for CI; the host preview library is also built. Every archive member
  is checked for its real ELF architecture. Debug SOs must contain source symbols.
- Eight headless SDK native-chain runs (both build systems/configurations/ARM
  architectures). Each must execute extension → middle.so → leaf.so and return 42.
  This tests that fixture's ABI, not every generated method or hardware behavior.
- Six unsigned RPMs: standalone editor packages and actual desktop CLI game
  exports for both architectures/configurations. Game inputs deliberately split
  native source directories and installed dependency directories. Extracted ELF,
  mapping, private RUNPATH closure and runtime receipt hashes are checked.
- Direct Regular-profile validation for every exact RPM. Exit 0 means passed;
  exit 2 means passed with remarks and is recorded separately. Complete logs are
  retained. These unsigned packages do not establish signing-certificate trust or
  installation acceptance.
- Existing Python/Aurora C++ tests plus a real Linux Wayland/OpenGL 3D/native-chain
  run under private KWin/D-Bus/runtime, with a captured viewport frame. Software
  GL is explicitly selected for that desktop regression.

## Local runner configuration

Requires Linux x86_64, Git, native C/C++ tools, binutils/RPM/cpio, KWin with virtual
backend, D-Bus, host Python/SCons, Aurora sfdk with both prepared targets, the
bootstrapped SDK Python/SCons and patchelf 0.18. No minimum Aurora version or
signing credentials are inferred. All paths must be visible through SDK sharing.

Keep a local JSON outside source control, for example:

```json
{
  "sfdk": "/path/to/AuroraOS/bin/sfdk",
  "sdk_python": "/shared/tools/sdk-python/bin/python3.12",
  "host_python": "/shared/tools/host-venv/bin/python",
  "patchelf": "/shared/tools/patchelf/bin/patchelf",
  "arm32_target": "YOUR_ARMV7HL_TARGET",
  "arm64_target": "YOUR_AARCH64_TARGET"
}
```

The host Python invocation keeps its venv path; resolving its symlink would lose
the venv's SCons installation. Target compiler, package inventory and selected
link inputs are observed inside each target, before and after the run. A changed
SDK or tracked source invalidates the result. This records provenance; it is not
a promise of bit-for-bit reproducibility or a cryptographically signed build.

```sh
python3 misc/aurora/ci.py build \
  --config /path/to/local-sdk.json \
  --godot-cpp /path/to/clean/godot-cpp \
  --output /path/to/new/shared-run

python3 misc/aurora/ci.py verify /path/to/new/shared-run
```

Worktrees/build products remain available for diagnosis. Remove a finished run's
worktrees with Git's worktree commands before deleting its remaining directories.
The public bundle consists of manifest.json, artifacts/ and logs/; source/build/
config directories are not required for offline artifact verification.

## Manifest and gates

The manifest binds engine/godot-cpp revisions and measured source inventories,
target API hashes, SDK compiler/sysroot package identities, selected link-input
hashes, explicit flags, command/exit logs and artifact SHA256/ELF/build IDs.
All binding archives/SOs retain their target API hash. File verification rejects
path escapes, symlinks, missing/duplicate records, byte replacement and incorrect
ELF identity. A partial/failed run cannot pass the complete-matrix verifier.

The software manifest records hardware as ARM32 excluded / ARM64 not-run.
Physical results must remain separate, with actual device/package/runtime
identity; modifying that field to claim hardware success is rejected.

## GitHub Actions

`aurora_builds.yml` runs Git/file/ELF identity controls on hosted Linux workers.
The SDK job requires a configured self-hosted Linux runner labelled aurora-sdk,
with `AURORA_CI_CONFIG` pointing to its local JSON. It is selected explicitly by
manual dispatch, or a trusted Aurora-branch push containing `[aurora-sdk-ci]`.
PRs never enter the SDK job. Tool paths/secrets are not committed to the workflow.

The job uses clean checkouts, executes the same local entrypoint, verifies the
complete bundle and uploads manifest/artifacts/full logs. Physical tests and
production signing are not performed by this software workflow.

Current validation: identity controls, both SDK API-dump preflights and isolated
Wayland/native42 preflight pass. The first complete fresh-source matrix is still
pending; do not describe the workflow definition alone as a successful CI run.
