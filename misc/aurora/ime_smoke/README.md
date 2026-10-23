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
