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
