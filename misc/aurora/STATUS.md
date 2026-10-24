# Aurora port status — 2026-10-24

Branch: `4.7.2_auroraos`; engine base: upstream `ed1daf0bf` (4.7.2-stable).
This is an experimental port, not a production release.

Hardware inventory: `phone` (192.168.2.44) and `usb-serial-phone` (192.168.3.5)
are USB/Wi-Fi connections to the **same KVADRA ARM64 tablet**. They do not count
as two physical test devices. Real ARM32 hardware has not been supplied yet.

## Protected-key signing and cancellable SDK jobs (point 2)

SDK commands now run in a worker while the editor pumps its progress UI. A
small embedded Python supervisor runs inside the Linux SDK; the desktop editor
does not need host Python. It owns the command's process group and terminates
it on Cancel, stdin EOF, heartbeat expiry or transport SIGHUP/SIGTERM,
escalating to SIGKILL for an uncooperative child. The group leader remains unreaped until the final group
signal, preventing a reused process-group ID from being targeted.

Encrypted PEM signing uses a masked modal LineEdit, or the ephemeral
`GODOT_AURORA_KEY_PASSPHRASE` environment value for headless automation. The
passphrase travels over stdin into the signing child's environment, never argv,
presets or editor settings. Secret-command output is suppressed. The prompt
belongs to the active modal export window. SDK progress selects the GUI directly
instead of relying on EditorNode's startup command-line flag, which can be set
before a Wayland window first becomes drawable. Original Linux code is unchanged.

Validation: **29 Python cases** pass, including packaging/build regressions;
the real SDK smoke checks exact exit/Unicode, secret suppression, SIGTERM-ignoring
child cancellation, an unaffected unrelated SDK job, client EOF/lease loss and
SIGHUP/SIGTERM cleanup of the owned child.
It checks the outer interpreter/SDK exit as well as the inner command result.
The SDK's Python 3.8 initially exposed a buffered-stdin daemon-thread shutdown
abort; the final reader uses raw polling and is stopped/joined before exit.

The actual export dialog passes all five acceptance scenarios: correct headless
password, wrong password, GUI prompt cancellation, GUI confirmation and packaging
cancellation followed by a successful retry **in the same editor process**.
Failures/cancellation preserve the previous output; temporary staging is cleaned;
the ignoring child is no longer running. Final host RPM bytes independently dump
the public SDK Regular development signature. No root-CA verification is claimed.
The GUI screenshot confirms masking, and logs/presets/settings contain no test
passphrase. Linux and both ARM editor builds pass; the 8-case / 111-assertion
Aurora C++ regression also passes. ARM compilation does not establish hardware
acceptance of the desktop export UI.

This closes protected-key input and in-command SDK-engine cancellation. A local
`deploy`/`device exec` cancellation only stops the sfdk transport: resolving an
already submitted device operation and recovering its PID after connection loss
remain pending. Windows/macOS export hosts are still disabled. Reproduction:
[signing_smoke/README.md](signing_smoke/README.md).

## Deferred documentation work after editor shutdown

Added lifetime checks before loading/regenerating script documentation: queued
callbacks cannot restart workers after the document, EditorNode or filesystem
owner has been destroyed. The targeted negative control crashed with SIGSEGV
before the guard. The final regression passes 3 assertions and invokes both
methods directly and through the deferred message queue without editor owners.
The test avoids initializing EditorPaths/Settings or creating source-tree files.

Linux builds; three cold imports plus three warm editor starts exit cleanly, and
the 7-case / 101-assertion Aurora IME regression still passes. Both ARM editor
builds pass with source-host provenance. This fixes the demonstrated teardown
callback failure. The earlier intermittent full import/quit abort was not reproduced
in three additional cold pre-fix runs; no upstream runtime attribution is made.

## Source-host build version metadata (point 13, first part)

Reproduced a provenance mismatch: the host was at `753bc0cdc7`, while an SDK query
and the previous ARM64 template still contained `7bdb350151`. The build runner now
reads HEAD/timestamp on the source host and forwards an explicit validated pair
inside the SDK engine. It does not rewrite Git refs. Without the optional pair,
the common version helper retains normal Git lookup for Linux and other builds.
This identifies HEAD; uncommitted-source attestation is still separate work.

Validation: 9 Python runner/lifecycle-metadata cases pass, including stale-Git
override, unchanged default lookup, literal SDK argv, incomplete/invalid metadata,
timestamp bounds and decimal normalization. ARM64 and ARM32 editor rebuilds both
contain the exact source commit `e57c3527024f2ba676fff47f733d6dfa73c93322` and its
timestamp `1792795063`. The Linux editor built without override also reports that
commit; its real Wayland 3D/native-chain regression returns 42. CI/artifact manifests
and the complete final six-artifact matrix are not yet complete.

## Native library trees and reload acceptance (point 8, software part)

The desktop exporter now sends a schema-1 source/resource/target map with host
SHA256 for every selected project DSO. The packager preserves plugin directories,
honours explicit dependency targets, and writes a schema-2 installed manifest.
Every DSO receives its own `$ORIGIN`-relative RUNPATH to its private dependencies.
The Aurora loader uses the exact resource URI; a missing mapped entry cannot
silently select another plugin by basename. Earlier flat editor/RPM layouts remain
compatible. Private basenames/SONAME identities remain globally unique; there is
no new ELF namespace or automatic SONAME rewriting. See [NATIVE_LIBRARIES.md](NATIVE_LIBRARIES.md).

Validation: Linux editor and ARM64/ARM32 debug templates build; 13 Python packaging
cases and 10 C++ mapping assertions pass. The new `native_layout_smoke` fixture
returns 42 on the host. Its ARM packages place extension, bridge and leaf in three
different directories, including source paths with spaces and explicit targets.
Both `ru.kotdath.godotnested` 1.0.0-1 RPMs pass Regular validation with the existing
non-Silica warning. Extracted final RPM ELF metadata matches the manifests,
all three resource mappings exist, and each private RUNPATH reaches its dependency.
ARM32 hard-float and absence of host DSOs are checked. Four real SDK negative
controls reject wrong architecture, incomplete closure, duplicate basename and
mismatched SONAME before producing an RPM. Final packages were signed with public
SDK test credentials, with signature dumps checked; no root-CA verification.
Local artifacts: `.aurora-tools/native-layout-rpms/signed-final/`.

The godot-cpp fork's `a7790bf` adds an opt-in hot-reload fixture. Six desktop Linux
reloads switch between two code revisions while preserving the existing ObjectID
and stored state; no old deleted DSO mappings remain. SCons builds both revisions
for each ARM, and CMake builds revision 1 for both. All six ARM outputs have the
correct ABI, exactly `$ORIGIN` RUNPATH and no GNU UNIQUE symbols; CMake propagates
hot-reload accounting/options to the extension and bindings.

Still pending: executing the nested package and hot reload on Aurora, including
normal sandbox versus developer launch. The one KVADRA tablet became unreachable
through both USB/Wi-Fi aliases after reporting 2% battery and Charging. Neither
device check is counted as passed. ARM32 hardware remains excluded from the goal.

## Native composition focus loss and input policy (point 4, fourth part)

Fixed a real device failure where leaving the app cleared an uncommitted native
composition without applying its cached fallback. The internal text-client
interface now also registers a finish callback. Surface leave and window focus
loss finish native composition once; callbacks from an old client cannot close
the next field's IME. Backends without native fallback metadata keep their existing
focus policy. Text-input enable is sent on activation, not every context update.

Normal LineEdit/TextEdit fields now advertise completion/correction when context
is available. CodeEdit opts out through a protected C++ capability method.
Password keyboard type hides context even if the LineEdit is not visually masked.

Validation: 7 Aurora IME cases / 101 assertions and 14 TextEdit cases / 4236
assertions pass. Linux, ARM64 and ARM32 editor builds pass; Linux Wayland renders
the 3D GDExtension fixture and its native dependency chain returns 42. ARM32 device
execution remains excluded. Test RPM `ru.kotdath.godotime` 4.7.2-3 passed Regular
validation with the existing non-Silica warning, was signed with SDK test
credentials and installed on KVADRA. `/proc/PID/exe` confirmed that release.
The native test received preedit `q`, committed exactly `q` after backgrounding,
cleared composition, then returned to an empty LineEdit after one Undo. CodeEdit
also preserved `# Код 😀\n\tpass` as one commit and one Undo. Local evidence:
`.aurora-tools/ime-evidence/ime-native3c-final.txt` and `ime-native3c-*.png`;
the checked-in `ime_smoke/verify_composition.py` verifies the focus-loss sequence.

Composition was exercised with the **stock Maliit test handler**, not a qualified
language IME. A Qt TextInput comparator also reproduced that handler's unusual
Q/E sequence (`Qqq` versus Godot's `qqq`), so that gesture is not a Godot-only
failure. All three keyboard preferences were restored to their snapshots and
temporary layout files removed. Real prediction selection, candidate placement
after rotation, dialog avoidance and preedit replacing an existing selection
still require work. USB and Wi-Fi device aliases refer to one physical tablet.

## IME composition and keyboard geometry (point 4, third part)

Implemented native preedit fallback metadata retained by each LineEdit/TextEdit,
including empty fallback, cancellation, metadata-only updates, hidden caret and
UTF-8-validated style spans. Native LineEdit fallback now emits its text change and
creates an undo state; the new test initially exposed the missing Undo. Other
backends keep their previous fallback behavior. Decorations use shaped Unicode
ranges; active/selected, inactive, none and incorrect spans have distinct display.
They do not synthesize bold/italic fonts or a wavy underline.

Caret rectangles now carry font height, rotate all corners and respect surface
scale. Unchanged rectangles no longer spam protocol updates. The adapter preserves
both keyboard overlap dimensions before rotation: the KVADRA trace's 520×2000
surface overlap becomes a 2000×520 buffer overlap. ScriptEditor uses the resulting
height in viewport units. Keyboard hide targets the active native text window.
Architecture and limits: `misc/aurora/IME.md`.

Validation so far: 5 Aurora IME cases / 72 C++ assertions pass; TextEdit regression
passes 14 cases / 4235 assertions. The fixture script runs headless without errors.
The Linux editor renders the 3D extension fixture through the host KDE Wayland
compositor on AMD Radeon 8060S and returns 42 from its native dependency chain.
This is a basic Wayland runtime regression, not interactive Linux IME acceptance.
ARM64 and ARM32 editor builds and editor RPMs 4.7.2-11 pass Regular validation
with the existing non-Silica warning. Both packages were signed using public SDK
test credentials (no root-CA trust verification). ARM64 was installed through APM;
ARM32 device execution remains excluded from the current goal. `/proc/15326/exe` confirmed that exact
release. The native fixture reported a 2000×1200 window and 520-pixel keyboard
height, a rotated 43×1 caret rectangle, native `helo` key entry, acknowledged
Cyrillic/emoji replacement, and Undo. The trace is retained locally at
`.aurora-tools/ime-evidence/ime-preedit11.log.gz`.

The normal sandbox editor (`/proc/16383/exe`, release 11) opened the persisted
script project. Its code viewport ended above the keyboard and returned to its
previous height after focus moved to the scene tree. Local screenshots are in
`.aurora-tools/ime-evidence/editor11-*.png`. This does not establish avoidance for
all dialogs. The installed predictive keyboard produced empty preedit events
but no visible suggestions in this configuration; non-empty composition,
prediction/range correction, and candidate placement across rotation remain open.
The later fourth part above adds completion/correction policy and records a
controlled native composition test. Real language acceptance remains separate.

One earlier `--headless --editor --import --quit` run aborted during shutdown after
reporting an absent EditorNode singleton from script-doc cache loading. Starting
the same project with `--headless --editor --quit-after 120` completed normally.
The later teardown checks above reproduce and fix a late-callback failure and
record subsequent clean import/start runs; the old failed run remains a failure.

## IME context and range edits (point 4, second part)

The new internal DisplayServer text-client callbacks supply a bounded committed
text context, Unicode cursor/anchor and origin. Aurora converts UTF-8 byte ranges,
queues selection replacement/deletion and post-commit cursor moves, and rejects
stale snapshots or invalid scalar boundaries. External text/caret changes use a
protocol reset and `wl_display_sync` barrier. Preedit events are focus-scoped;
preedit defaults to an end cursor and supports the protocol highlight range.
Passwords advertise empty text and hidden/sensitive purpose. Oversized selections
and multiple carets keep ordinary insertion, with extended context edits disabled.
See `misc/aurora/IME.md` for the contract and limits.

Linux C++ tests: 3 Aurora IME cases / 47 assertions pass; TextEdit regression:
14 cases / 4235 assertions pass. ARM64 and ARM32 editor builds pass. KVADRA final test RPM
4.7.2-10 passed the original 13 native editing checkpoints and per-character
Maliit acknowledgements for Cyrillic/emoji, insertion after a caret move, and
multiline/tab text. Its captured trace passed 145 bounded-context checks,
including password wire privacy. Range replacement and post-commit selection are
covered by C++ tests; prediction/composing input methods still need hardware coverage.
The final package passed Regular validation with the existing non-Silica warning
and uses SDK public development signing credentials (no root-CA verification).

Preedit fallback/styling/hidden caret and keyboard geometry continue in the third
part above. Composing-language acceptance, prediction and dialog avoidance remain open.

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
`misc/aurora/ime_smoke` contains the fixture and log verifier. The context implementation adds surrounding-text and replacement ranges;
remaining composition and geometry coverage is listed separately. ARM32 hardware and Linux Wayland runtime regression remain pending.
Aurora APM can retain an old `current` version while the editor is running; stop it
and launch through RuntimeManager before checking an updated package.
At that stage the SDK saw a stale Git worktree ref (`7281b98b6`) outside this source
worktree, so that test identifies the exact RPM release and `/proc/PID/exe` instead
of trusting the embedded Git revision. The later source-host metadata change above
fixes the version identifier for new builds; it does not relabel those old RPMs.

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
  the host GDExtension. Basic Linux Wayland runtime regression now passes as recorded above; interactive
  Linux IME coverage remains pending.
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
- Protected-key signing and SDK-engine cancellation are now verified above.
  Still pending: device operation connection-loss recovery, normal launcher/sandbox acceptance,
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
  The later IME changes above fix multiline commits and add bounded surrounding
  text/selection and range edits. Composing input methods, keyboard occlusion of
  dialogs, broader editor operations and ARM32 hardware remain pending.

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

- Remaining IME preedit fallback/styling/geometry, composing-language hardware coverage
  and keyboard avoidance; lock/unlock and broader lifecycle testing; audible
  playback/resource routing. Multiwindow, XR and boot-splash rotation are unverified.
- ARM32 hardware tests (no ARM32 test device assigned).
- Device deployment outcome/recovery after cancellation or connection loss;
  Windows/macOS host packaging and Aurora execution of nested RPM layouts.
- PSDK and BT build validation, Vulkan hardware presentation, ARM32 occlusion
  backend, broader Embree performance/editor checks, Aurora hot-reload acceptance.
- Keep Linux changes minimal: reuse existing Wayland sources with small guarded
  hooks; do not extract a new shared Wayland layer (user decision).
- Detailed implementation and priority order: [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md).

## Reproduction and SDK mount caveat

See [README.md](README.md) for builds and [EXPORT.md](EXPORT.md) for desktop
export. The SDK fakeowner shared mount can cache stale file sizes after host
edits. `build.py` refreshes changed Git-visible files and generated source
headers through sfdk before starting SCons. It skips ignored binaries and tool
caches. Do not run two engine SCons builds concurrently in the same worktree.
