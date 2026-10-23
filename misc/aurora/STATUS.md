# Aurora port status — 2026-10-23

Branch: `4.7.2_auroraos`; engine base: upstream `ed1daf0bf` (4.7.2-stable).
This is an experimental port, not a production release.


## Atomic IME commits (point 4, first part)

Aurora text-input-v2 now queues whole strings to the focused text control on the
main thread. CodeEdit/TextEdit preserve multiline text and tabs and group a
selection replacement into one undo operation. LineEdit strips escape characters,
respects its length limit, and emits changes for equal-length replacements.
Queued commits retain their recipient and focus generation; unsupported backends
continue their existing key-event path. Empty commits only clear composition.

KVADRA ARM64 test editor RPM `ru.kotdath.godoteditor` 4.7.2-8 passed 13 ordered
native Maliit checkpoints: Unicode insertion, multiline/tab preservation,
selection replacement, one-step undo, same-length LineEdit replacement, escape
filtering, native key/backspace, and switching to read-only. The pre-change RPM
reproduced loss of newlines/tabs with the same input. Emoji codepoints survive
exactly; the fixture's default font lacks some emoji glyphs. This is a standalone
fixture run from the installed editor binary; it is not a new sandbox claim.

ARM64 and ARM32 editor builds pass with this change.
`misc/aurora/ime_smoke` contains the fixture and log verifier. Full surrounding-text
synchronization, prediction/replacement ranges and composition/reset coverage are
still pending. ARM32 hardware and Linux Wayland runtime regression remain pending.
Aurora APM can retain an old `current` version while the editor is running; stop it
and launch through RuntimeManager before checking an updated package.
The SDK currently sees a stale Git worktree ref (`7281b98b6`) outside this source
worktree, so the embedded Git revision is not reliable provenance for these local
builds. The test identifies the exact RPM release and `/proc/PID/exe`; SDK Git
metadata synchronization remains a build reproducibility follow-up.

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
- Initial Aurora ARM64 editor opens a package-backed copy of the same project and shows its
  3D viewport without environment overrides (test RPM 4.7.2-8). The XDG-only
  embedding proxy is disabled by default for Aurora.
- Linux X11 graphical smoke test under Xvfb/llvmpipe passes and returns 42 from
  the host GDExtension. Linux Wayland hardware regression test is still pending.
- A real export with a mismatched template architecture fails without producing
  an RPM. Eight packaging boundary tests pass. The embedded PCK survives RPM processing
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
  workflow has now been tested as described below. ARM32 editor and release
  artifacts still need rebuilding to include this update.

## Prioritized continuation

Priority: **6, 5, 7, 1, 2, 3, 4, 8, 9, 10, 11, 12, 13**; see
[IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md). No shared Wayland extraction.

- Vulkan opt-in (`vulkan=yes extra_suffix=vulkan`) compiles for ARM32 and ARM64 with bundled
  headers/glslang and dynamic Volk loading. GLES remains the default.
- On KVADRA, the signed probe confirms missing libvulkan.so/.so.1 and no Vulkan
  entry points in the installed Mali library. Hardware WSI/renderer validation
  needs a compatible vendor runtime or another Aurora device; see [VULKAN.md](VULKAN.md).
- The Vulkan-enabled ARM64 engine, explicitly launched with `--rendering-method
  mobile --rendering-driver vulkan`, falls back to GLES on KVADRA and displays the
  mobile fixture (RPM 4.7.2-8). This is a fallback test, not Vulkan rendering.
- Added four-second stereo tone and lifecycle timing to the mobile fixture.
  PulseAudio routes Godot's stream to physical sink 3 (rockchipes8326c), uncorked
  and unmuted; start/finish events succeed. Physical audibility, headphones and
  lock/unlock are still pending. System stream volume was low and was not changed.
- Reproduced SDK fakeowner truncation of a host-updated PCK (3648 versus 5376
  bytes). Packaging now counts copied bytes and accepts the host SHA256, supplied
  by the desktop exporter, to reject stale/truncated reads. Refreshed the SDK copy;
  RPM 4.7.2-8's embedded PCK matches the host source byte-for-byte and runs.

- ARM64 Embree opt-in compiles after an Aurora-only third-party compiler flag.
  On KVADRA the occlusion fixture reports **102 → 2 → 102** rendered objects;
  the host negative control without the module reports 102 → 102 → 102 and fails.
  ARM32 remains unsupported by upstream Embree; the default profile is unchanged.
- Native Godot TCP remote debugging passes on KVADRA: connect, breakpoint, next,
  continue, and restart/reconnect; both debug runs preserve the occlusion PASS.
  See [debugger_smoke/README.md](debugger_smoke/README.md). This verifies a developer
  launch, not normal launcher/sandbox context or one-click deploy. Profiler data,
  variable inspection UI, Remote Scene Tree and native GDB are not yet validated.
- Desktop Linux editor rebuild and a real ARM64 debug export pass with host PCK
  SHA256 verification. Test RPMs remain public-development-signed only. The small
  Vulkan probe also has an empty-private-directory validator warning; game RPMs
  have only the non-Silica warning. No root-CA trust verification was performed.

## SDK deploy integration

- The desktop exporter now supports optional Regular signing and Remote Deploy
  through sfdk, with device/key/certificate paths in local Editor Settings.
  Run and Stop use SDK device commands; no audb dependency is added to the product.
- Linux editor builds. A real editor-menu integration run on registered
  `KVADRA_T (ARM 64bit)` exports, validates, signs, installs and launches the
  GDExtension test project. It renders the 3D scene, returns 42 from the native
  dependency chain, records touch input, connects to the PC debugger and stops
  its own process. Device PID termination was independently checked. A second
  deployment updates successfully, reconnects to the debugger, and preserves the
  in-game persisted launch counter (1 → 2). An ARM32 preset on the ARM64 device
  is rejected before export/installation. Standalone signed headless export
  also passes and retains the preset release (the development suffix is Run-only).
- SDK deployment requires a complete RPM filename. Aurora rejects the same
  package version/release on reinstall, so Remote Deploy adds a unique development
  release suffix without changing the saved preset. Temporary packages are cleaned.
- SDK invocations use argv-based pipes: the captured-output OS::execute Unix path
  would otherwise expand shell variables on the host. No original Linux code was
  changed for this fix.
- Signature: SDK public Regular development certificate; no root-CA trust check.
  Regular validation has the existing non-Silica warning only. Installation may
  require confirming the SDK dialog on the device.
- Still pending: interactive encrypted-key signing, cancellation while an SDK
  command runs, connection-loss recovery, normal launcher/sandbox acceptance,
  ARM32 hardware, and native GDB. This is a developer launch through SDK.
- Setup and limits: [EXPORT.md](EXPORT.md); opt-in integration fixture:
  [deploy_smoke/README.md](deploy_smoke/README.md).

## Device editor workflow

- ARM64 editor RPM `ru.kotdath.godoteditor` 4.7.2-6 on KVADRA: GUI project
  creation/icon import, scene creation/save, script attachment, Latin/Cyrillic
  CodeEdit input, Run Current Scene, rendering and Stop pass. A project created
  outside the sandbox was copied into the app's persistent project directory,
  imported through Project Manager, edited/saved under the normal launcher,
  reopened after an RPM update and run again. The saved script comment persists.
- The normal launcher/firejail run starts a separate game process, connects the
  loopback debugger, forwards the script output and displays Remote Scene Tree.
  Stop terminates that process (checked independently using its device PID).
  The shell currently requires manually choosing the child app card to foreground
  the game; automatic activation and app-cover previews remain incomplete.
- An EditorPlugin calls the installed GDExtension → middle.so → leaf.so chain
  and gets 42 both in a developer launch and the normal sandbox. This verifies
  native calls in tool context, not native hot reload or compilation on device.
- Fixed Project Manager's narrow window and the editor's shifted viewport:
  legacy shell maintains maximized/fullscreen state, and rejected client resize
  requests report the actual logical rectangle back to Window. The regression
  plugin confirms a 2000×1200 root viewport before/after a 1024×600 resize request.
  Linux behavior is unchanged behind Aurora-only hooks; no shared layer extracted.
- Installed RPM applications now keep XDG data/config/cache under their application
  ID. The editor defaults projects to `~/.local/share/<application-id>/projects`.
  Earlier experimental files are left in their original locations; import/copy
  them explicitly. Standalone binaries retain ordinary XDG storage paths.
- Editor RPMs need `Internet` permission for local TCP debugger/language-server
  sockets. Without it the normal sandbox rejects socket creation. The tested
  editor uses `Audio;UserDirs;Internet`; game presets keep their explicit permissions.
- Fixed alternating SDK/Linux builds reusing incompatible generated protocol and
  module/export/doc files. The SDK runner regenerates configuration-dependent
  files and restores host outputs, including on failure. Two runner boundary
  tests pass, ARM64 editor and subsequent Linux editor builds pass; eight existing
  packaging tests also pass. Concurrent builds still require separate worktrees.
- Reproduction and opt-in plugin: [editor_smoke/README.md](editor_smoke/README.md).
  Multiline IME commits still discard newline/tab; Enter-key editing works.
  Surrounding text/selection/prediction, keyboard occlusion of dialogs, broader
  editor operations and ARM32 hardware remain pending.

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
  and multiline editor text insertion; lock/unlock and broader lifecycle testing; audible
  playback/resource routing. Multiwindow, XR and boot-splash rotation are unverified.
- ARM32 hardware tests (no ARM32 test device assigned).
- Broader deployment recovery/cancellation and encrypted-key signing UI;
  Windows/macOS host packaging and broader RPM layout support.
- PSDK and BT build validation, Vulkan hardware presentation, ARM32 occlusion
  backend, broader Embree performance/editor checks, hot reload.
- Keep Linux changes minimal: reuse existing Wayland sources with small guarded
  hooks; do not extract a new shared Wayland layer (user decision).
- Detailed implementation and priority order: [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md).

## Reproduction and SDK mount caveat

See [README.md](README.md) for builds and [EXPORT.md](EXPORT.md) for desktop
export. The SDK fakeowner shared mount can cache stale file sizes after host
edits. `build.py` refreshes changed Git-visible files and generated source
headers through sfdk before starting SCons. It skips ignored binaries and tool
caches. Do not run two engine SCons builds concurrently in the same worktree.
