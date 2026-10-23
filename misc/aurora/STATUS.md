# Aurora port status — 2026-10-23

Branch: `4.7.2_auroraos`; engine base: upstream `ed1daf0bf` (4.7.2-stable).
This is an experimental port, not a production release.

## Verified

- Pinned Python 3.12.12 and SCons 4.9.1 bootstrapped in SDK tooling, then used
  under both ARM targets. Upstream build requirements remain unchanged.
- SDK 5.2.1.200: ARM32/ARM64 debug templates and editors compile. ARM32 and ARM64 release
  templates compile. Raycast/Embree is disabled
  in the initial profile; Wayland, GLES and PulseAudio are enabled.
- godot-cpp dependency-chain example: SCons and CMake builds pass for ARM32 and
  ARM64. Compiler target guards reject a mislabeled host toolchain. Every private
  DSO has exactly `$ORIGIN` RUNPATH. Linux x86_64 preview library also builds.
- Desktop Linux editor builds with the new **Aurora OS** export platform.
  `--export-debug` creates Regular-valid ARM32 and ARM64 RPMs. ARM64
  `--export-release` and ARM32 `--export-release` also produce valid RPMs. The packager is embedded in the
  editor, so export does not depend on a source checkout.
- ARM64 debug and release RPMs produced by that desktop exporter install
  through `audb` on KVADRA ARM64. It renders a rotating
  cube and text using GLES 3.2 / Mali-G610, loads extension → middle.so → leaf.so
  and returns 42. The on-screen touch counter and log recorded button presses.
- Aurora ARM64 editor opens a writable copy of the same project and shows its
  3D viewport without environment overrides (test RPM 4.7.2-8). The XDG-only
  embedding proxy is disabled by default for Aurora.
- Linux X11 graphical smoke test under Xvfb/llvmpipe passes and returns 42 from
  the host GDExtension. Linux Wayland hardware regression test is still pending.
- A real export with a mismatched template architecture fails without producing
  an RPM. Five packaging boundary tests pass. The embedded PCK survives RPM processing
  byte-for-byte. Exported ARM packages contain only the selected target DSOs.

Regular validator: no errors, one non-Silica desktop warning. Test RPMs use
SDK public Regular development credentials. Signature dumps and device
installation results were checked; root-CA verification was not performed
(no root certificate supplied). These are not production signatures.

## Mobile runtime update

- KVADRA ARM64: four fixed orientations render upright; landscape corner buttons
  match touch coordinates. Physical sensor rotation reported all four transforms.
- Rotating during a held touch emits a canceled release in the previous coordinate
  system. Input is transformed when queued, before subsequent orientation changes.
- Native text-input-v2 keyboard opens; actual keyboard taps produced `q`, `qe`,
  then `q` after Backspace. Switching to Russian produced `qй`, `qйу`, then
  `qй` after Backspace. UTF-8 commit/preedit conversion is implemented;
  prediction, selection and full editor text entry still need testing.
- Minimize/reopen produces pause (2015) and resume (2014), suspends/resumes
  drawing, and leaves touch and keyboard input working (test RPM 4.7.2-5).
- Updated debug templates compile for ARM32 and ARM64; ARM32 hardware validation
  is pending. The updated Linux editor builds with `use_static_cpp=no`; its GLES
  X11 smoke test under Xvfb/llvmpipe again loads the dependency chain and returns 42.
- See [mobile_smoke/README.md](mobile_smoke/README.md) for architecture,
  reproduction and limits. The updated ARM64 editor also compiles; device editor
  interaction with this update is not yet retested. ARM32 editor and release
  artifacts still need rebuilding to include this update.

## Runtime findings

KVADRA provides wl_shell, qt_surface_extension, text-input-v2, viewporter,
compositor v4 and output v2; neither XDG shell nor text-input-v3 is present.
The platform uses capability detection, not OS version checks.

The Aurora shell adapter permits the first buffer without XDG's initial
configure handshake and supports Qt close events. GLES handles native Wayland
pointers correctly on EGL 1.4. Missing XKB Compose files no longer cause a crash.
Native subwindows are disabled under legacy shell; editor dialogs are embedded.
The XDG-only game embedding proxy is disabled for Aurora.

The normal export template path override restriction remains enabled. Games
use an embedded PCK. Main ELF is `/usr/bin/<app-id>` with the validator-required
DT_RPATH `/usr/share/<app-id>/lib`. Private DSOs use their own DT_RUNPATH
`$ORIGIN`; basename collisions and incomplete DT_NEEDED closures fail export.
libxkbcommon is bundled with its license because it is not Regular public API.

## Remaining work

- Full IME surrounding-text synchronization, predictive edits/selection
  and editor text entry; lock/unlock and broader lifecycle testing; audible
  playback/resource routing. Multiwindow, XR and boot-splash rotation are unverified.
- ARM32 hardware tests (no ARM32 test device assigned).
- One-click deployment through audb, remote-debug launch flags, signing UI,
  Windows/macOS host packaging and broader RPM layout support.
- PSDK and BT build validation, Vulkan, raycast/occlusion culling, hot reload.
- Shared Wayland directory extraction: current implementation reuses linuxbsd
  sources with small hooks, while Aurora-specific shell policy is isolated in
  `platform/auroraos/legacy_shell.*`.

## Reproduction and SDK mount caveat

See [README.md](README.md) for builds and [EXPORT.md](EXPORT.md) for desktop
export. The SDK fakeowner shared mount can cache stale file sizes after host
edits. `build.py` refreshes changed Git-visible files and generated source
headers through sfdk before starting SCons. It skips ignored binaries and tool
caches. Do not run two engine SCons builds concurrently in the same worktree.
