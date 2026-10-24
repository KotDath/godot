# Aurora window activation and editor return

The Aurora adapter requests activation when a focusable legacy toplevel is first
shown, and handles explicit `Window.grab_focus()` / debugger foreground requests.
It uses `qt_extended_surface.raise` when the compositor advertises that protocol.
No Qt library or audb dependency is added to the engine.

## Implementation

Policy lives in `platform/auroraos/window_activation.cpp`. The existing Wayland
display server has small `AURORAOS_ENABLED` hooks; no backend is moved out of
`linuxbsd`, and normal Linux keeps its existing foreground/focus behavior.

Initial activation is queued after window class/title/role and graphics setup.
It is sent once the legacy compositor has reported concrete Qt visibility
(Hidden, Windowed, Minimized, Maximized or Fullscreen; Automatic is unspecified).
The upstream [Lipstick raise handler](https://github.com/sailfishos/lipstick/blob/master/src/compositor/lipstickcompositor.cpp#L618)
ignores requests for unmapped surfaces, so sending a raise before the first
attached buffer is insufficient. A background surface is not guaranteed a frame
callback either. The visibility notification supplies the acknowledgement used
by this adapter; physical Aurora acceptance remains required below.

There is no periodic raise. Uncreated, unshown, `NO_FOCUS` and native popup
windows do not request activation. Multiple pending requests collapse to one;
changing to a nonfocusable flag or destroying the surface cancels it. For XDG
surfaces the initial compositor policy remains unchanged; explicit Aurora
foreground requests use the existing optional XDG activation backend.

An activation request never fabricates input focus or visibility. Aurora's
`window_is_focused()` reads the actual keyboard-focus ID of any live keyboard
seat, under the existing Wayland mutex. Missing windows/keyboard capabilities
return false. Mouse hover is independent of keyboard focus. Qt visibility events
continue to control suspension; raising a minimized window does not clear that
state before the compositor reports it visible.

After main-loop initialization, a Wayland **editor** connects the local run bar's
`stop_pressed` signal to an Aurora callback. The editor setting
`run/auroraos/restore_editor_on_stop` defaults to true; disabling it suppresses
this automatic return. The callback requests the main editor window foreground
when the editor observes its local run stopping. Games, Project Manager and
headless tools have no such connection. Connections are checked for duplication
and belong to the display-server Object; no PID-based polling/killing is added.

```mermaid
sequenceDiagram
    participant Game
    participant Run as EditorRunBar
    participant Aurora as Aurora adapter
    participant Shell as Compositor
    Game->>Run: local run stops
    Run->>Aurora: stop_pressed
    Aurora->>Shell: qt_extended_surface.raise (if enabled)
    Shell-->>Aurora: keyboard enter + visibility
    Note over Aurora: report observed focus; resume using visibility
```

## Observed compositor contract — 2026-10-24

The standalone C probe is **not Godot** and does not exercise the new editor
callback/getter. It uses SHM buffers and the same legacy/Qt protocol, without a
pointer object. The test links the actual native session supervisor and drives
launch/status/Stop through real `sfdk device exec`. audb supplies device setup
and binary upload; the uploaded binary SHA256 is checked before executing it.

All four mode checks pass on the local **x86_64 SDK emulator**:

- No child raise: this compositor automatically focuses the new child.
- Raise before the child's first attached buffer: automatic focus can mask an
  ignored request, so this is a diagnostic control, not proof of activation.
- Raise on the first concrete visibility notification: the adapter's selected
  timing, checked independently of a frame callback.
- Raise after the first frame: another timing control; a background child may
  never receive this callback.

In all four cases, closing the child leaves the parent minimized/unfocused.
One explicit parent raise restores keyboard focus and visibility 4 (Maximized).
The test requires a new focus event after that request when restoration was
needed. This supplies the reason for an explicit editor-return policy. The
emulator's automatic initial child focus does **not** reproduce or prove a fix
for the earlier KVADRA foreground issue.

The visibility-mode child records `attached → visibility 4 → raise → frame →
keyboard enter`. The parent records keyboard leave/visibility 3, then a new
keyboard enter/visibility 4 after its explicit request. There is one child raise
and two parent raises (initial plus explicit restore); both clients finish with
code 0. Probe executable SHA256:
`ed58e4e6abbc587f2ea3cbc5a297cdc8f53f7945b05b19751ed51d51366b2bfe`.
The local event timelines and summary are in
`.aurora-tools/window-activation-visibility-contract/`.

Both probe clients are stopped through their own native session tokens in
`finally`; the uploaded executable/request files are removed after confirmed
cleanup. Session tombstones/logs remain under the SDK user's private cache so
delayed requests remain fenced. Other applications are never stopped.

## Reproduction

Requires Linux host Python, C++17 target tools, native `wayland-scanner`, working
sfdk/audb profiles for the same already-running, unlocked device, and
`wayland-devel` in the exact selected SDK target/snapshot. For example:

```sh
sfdk tools target package-install AuroraOS-5.2.1.200-x86_64.default wayland-devel
python3 misc/aurora/window_activation_smoke.py \
  --sfdk /path/to/AuroraOS/bin/sfdk \
  --sdk-target AuroraOS-5.2.1.200-x86_64.default \
  --device 'Aurora OS Emulator 5.2.1.200' \
  --audb-device emulator \
  --output /path/to/new-shared-test-output
```

Names are examples, not minimum OS versions. The runner records device info,
machine architecture, binary SHA256, compiler logs and event timelines. Its
summary explicitly says `engine_tested=false`. Do not run SDK source refresh or
another engine SCons build concurrently in the same checkout.

## Engine acceptance still required

All six ARM editor/debug/release artifacts compile with this adapter and have
the expected ELF class/machine. The Linux editor builds with
`tests=yes use_static_cpp=no`; its existing Aurora suite passes 15 cases / 195
assertions. A separate Linux Wayland/OpenGL run under a private KWin virtual
compositor, D-Bus and runtime directory captures a real 3D frame and returns 42
through the example's native dependency chain, then exits cleanly. Mesa llvmpipe
software rendering is used. This checks Linux regression, not the Aurora adapter.
Local evidence: `.aurora-tools/window-activation-linux-final/` and
`.aurora-tools/window-activation-evidence/`. These are working-tree builds;
point 13's final clean-source artifact manifest remains separate.

Install/update the rebuilt Aurora editor while its previous instance is stopped,
then use the normal launcher and repeat the
[editor smoke](editor_smoke/README.md). Check automatic game foreground, actual
`Window.has_focus()` transitions, natural game exit and toolbar Stop returning
to the editor, a breakpoint bringing the editor forward, and disabling the
return setting. Repeat after minimize/lock/unlock and verify other windows are
not repeatedly raised. Inspect `--verbose` activation messages with the actual
running executable/RPM identified.

Physical ARM64 acceptance of these engine paths remains pending while KVADRA is
unreachable. Real ARM32 hardware is excluded from the current goal. App-cover
previews and the broader multiwindow/IME/lifecycle matrix are separate work.
