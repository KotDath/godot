# Aurora IME smoke

Run this project on a device with the native keyboard. It records JSON lines in
`user://ime-results.jsonl` and stdout (`AURORA_IME`). A device input tool is optional;
`audb text --stdin` delivers a single native Maliit commit for reproducible tests.

1. Tap the CodeEdit and insert `func привет():\n\tprint("Юникод 😀")\n` as one commit.
   Check exact newlines/tabs in the JSON, then **Undo multiline**: it must be empty.
2. Insert text again, use **Select multiline**, replace it with another multiline
   commit, then Undo: the entire original text must return in one operation.
3. Tap LineEdit, insert Unicode text, then **Select single** and replace it with a
   different string of the same length. Check the change notification and Undo.
4. Insert newlines/tabs into LineEdit: they must be stripped, matching normal paste.
5. Use **Read only** and attempt further input: neither value may change.

Buttons keep keyboard focus. Check the recorded values rather than just the visual
layout. This fixture covers atomic commits, not prediction/preedit or surrounding
text, which require additional protocol tests.

For the exact recorded sequence expected by `verify.py`, run steps in this order:
LineEdit `Привет` → select → `Пока!!` → undo; CodeEdit original → undo → original
→ select → `замена\n\tстроки 👩‍💻\n` → undo; LineEdit select → `A\n\tБ😀\n` → undo;
then type `q` on the native keyboard, Backspace, and enable Read only. Attempt
Backspace/Enter again. Verify stdout or the JSON-lines file with:

```sh
python3 misc/aurora/ime_smoke/verify.py /path/to/ime-results.jsonl
```

Emoji codepoints are checked in the log. Missing glyphs in the default font are
independent of IME text preservation.

## Surrounding text and password checks

Start the fixture with `WAYLAND_DEBUG=client` and capture stdout/stderr. Enter
`А😀Б ёж` using `audb text --delay 50`: this waits for Maliit to acknowledge the
text and cursor after every character. Use **Move caret**, insert more Unicode,
then enable **Password** and insert dummy text with delay 0 (password context is
intentionally hidden, so per-character context acknowledgement is unavailable).
Disable Password, focus CodeEdit, and enter multiline text with a tab.

```sh
python3 misc/aurora/ime_smoke/verify_context.py /path/to/wayland-trace.log.gz
```

The verifier accepts plain/gzipped traces from this controlled fixture. It checks
the 4000-byte limit, UTF-8 cursor/anchor boundaries, multiline context, and that
password-mode requests contain no surrounding text or cursor positions. The
**Reset** button restores editable, non-secret fields. For the original 13-step
verifier, save the log immediately after Read only, before starting more scenarios.


## Composition, prediction and geometry

The fixture logs `AURORA_IME_GEOMETRY` when the window size or keyboard overlap
changes, and `AURORA_IME_PREEDIT` when native composition changes. In landscape
on KVADRA the keyboard overlap should be about 520 pixels, not the 2000-pixel
native surface height. Cursor rectangles in the wire trace must have the font
height after rotation; they must not remain the old fixed 1×10 rectangles.

Use the native predictive layout to type a word, select a suggestion, move the
caret into an existing word and replace it. Test selection in both controls,
switching fields during composition, Backspace, and Undo. A predictive layout can
use range commits without emitting preedit; record which protocol events actually
occur. For a composing input method also check its preedit display, hidden caret,
selection/style ranges and fallback on focus loss. Repeat after scrolling,
resizing and rotation. Dummy password input must remain absent from surrounding
text. The C++ cases cover fallback differences, empty fallback/cancellation,
metadata-only changes, malformed style byte boundaries, and all four rectangle
transforms independently of installed keyboards.


Native composition records are additionally written to `user://ime-native.jsonl`
with `kind` (`state`, `preedit`, `geometry`) and `data`. This preserves the original
atomic-commit verifier's input format in `ime-results.jsonl`. Installed sandbox
paths may be visible only inside the application mount namespace; collect the
fixture's own files before stopping it. Do not infer correctness from a keyboard
gesture alone: compare the actual preedit received with the committed fallback.

The installed Maliit `PreeditTestHandler.qml` can be selected through a temporary
copy of the English layout and a separate layout config. Save `active_layout`,
`enabled_layouts` and `previous_layout` first, use unique test filenames and remove
only those files afterward. Restore active/enabled layouts, restart the keyboard,
then restore `previous_layout` last: changing the active layout can otherwise
asynchronously overwrite it. Verify all three values against the snapshot. Keep
keyboard-side QML outside this runtime project and outside its application RPM.

On the current KVADRA, the stock test handler produced `q`, `qq`, `qqq` after Q/E
in Godot. A separate Qt 5.6 TextInput reference with the same layout finished as
`Qqq` (the first key used auto-capitalization). Thus that sequence is not a valid
Godot-only regression test. It still exercises native preedit delivery. Real
language-IME acceptance and prediction selection must be recorded separately.

For a controlled focus-loss check, start a fresh fixture process after selecting
the test layout, activate its app card explicitly, focus the empty LineEdit and
tap Q. Confirm a non-empty `preedit` record before swiping the application to the
background. Its next committed state must match the **received** preedit, followed
by an empty preedit. Return to the card and press **Undo single** once. Collect
`ime-native.jsonl` and check:

```sh
python3 misc/aurora/ime_smoke/verify_composition.py /path/to/ime-native.jsonl
```

This scenario uses the stock test handler's matching display/fallback strings.
Different or empty fallbacks are covered separately by the C++ tests. Restarting
Maliit while a field is already active did not reliably re-establish the test
handler's context on KVADRA; do not count a run without non-empty preedit as a pass.
