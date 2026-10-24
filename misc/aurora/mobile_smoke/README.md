# Aurora mobile smoke test

Open this directory as a Godot project or export it with the Aurora OS exporter.
Use a rebuilt debug template; install the signed development RPM through `audb`.
The fixture logs `MOBILE_ORIENTATION`, `MOBILE_SIZE`, `MOBILE_CORNER`,
`MOBILE_TOUCH`, `MOBILE_TEXT` and `MOBILE_LIFECYCLE` to stdout.

## Checks on a real device

1. Select each fixed orientation, check upright text and all four corner buttons.
2. Select Sensor and physically rotate through all four positions. Check viewport
   dimensions and touch coordinates after every rotation. Test restricted sensor
   landscape and portrait modes too.
3. Hold a touch while changing orientation: expect one canceled release at the
   old coordinates and no stuck button. The fixture accepts mode integers 0–6
   through `user://orientation.txt` to automate this test.
4. Tap the LineEdit and type using the native keyboard. Check Backspace,
   multilingual preedit/commit, hide/show and focus changes.
5. Minimize and reopen the app. Expect focus events and application paused/resumed
   notifications (2015/2014), no stuck touches, and a responsive viewport.

`audb` touch coordinates follow physical display rotation. A compositor screenshot
can be normalized to the application's locked orientation, so coordinates copied
from such screenshots may need conversion before injecting input.

## Architecture

`platform/auroraos/orientation.h` selects the logical orientation and publishes a
coherent atomic snapshot of native dimensions and transform. The Wayland adapter
updates the logical window size and cancels old touches on transitions. Queued
input is converted with the snapshot that was current when it arrived.

The native EGL framebuffer retains its native dimensions. GLES rotates the final
blit; the copy shader's Y inversion is accounted for in the rotation sign. Direct
to screen rendering falls back to an intermediate target so it cannot bypass this
step. The renderer does not call DisplayServer or take its Wayland mutex.

`text_input_v2.*` adapts the protocol to the existing Godot IME/input messages.
It discovers capabilities at runtime and leaves text-input-v3 to the existing
backend when available. No Qt runtime dependency or OS version check is added.
Qt extended-surface visibility is an enum: Hidden=0 and Minimized=3 suspend the
application; Windowed=2, Maximized=4 and FullScreen=5 resume it.

The IME adapter now synchronizes bounded surrounding text, cursor/selection and
preedit state; password fields hide their context. See [IME.md](../IME.md) and
[STATUS.md](../STATUS.md) for implemented operations and the remaining language,
candidate-UI and hardware acceptance. Boot splash, XR, external displays and
multiwindow rotation remain outside the verified matrix.

Math checks (host, no SDK):

```sh
g++ -std=c++17 -Wall -Wextra -I . -I platform/linuxbsd \
  misc/aurora/test_orientation.cpp -o /tmp/aurora_rotation_math
/tmp/aurora_rotation_math
```

References: [Qt visibility enum](https://doc.qt.io/qt-6.8/qwindow.html#Visibility-enum),
[QtWayland content orientation](https://wiki.libsdl.org/SDL2/SDL_HINT_QTWAYLAND_CONTENT_ORIENTATION),
[Wayland protocol](https://wayland.freedesktop.org/docs/html/apa.html).

## Recorded device results — 2026-10-23

KVADRA ARM64, SDK 5.2.1.200, test package `ru.kotdath.godotmobile-4.7.2-5`:

- Minimize: visibility 3 → notification 2015; reopen: visibility 4 → 2014.
  A corner-button tap and keyboard input worked after resuming.
- Native English/Russian keyboard taps: `q → qe → q → qй → qйу → qй`.
- Earlier iterations of the same fixture verified four fixed transforms, physical
  sensor rotation through transforms 3/0/1/2, and canceled touch on rotation.
- Regular validator: no errors, one non-Silica warning. Installed through audb
  using SDK public development signing credentials, not production credentials.

ARM32: debug template compilation passes; no hardware supplied yet.

## Audio and lifecycle diagnostics

The “Play stereo tone (4 seconds)” button plays 440 Hz at -18 dB: two seconds
left, then two seconds right, with short fades. A changed `user://audio.txt` value
starting with `play` triggers the same test. `MOBILE_AUDIO` records start/end and
the selected driver; lifecycle records include monotonic time and maximum frame
gap. A completed stream or a PulseAudio sink route does not prove audible sound.
Physical listening, headset changes and lock/unlock remain separate checks.
