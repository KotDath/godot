# Aurora IME editing context

The Aurora adapter supports `text-input-unstable-v2` without introducing a new
shared Wayland backend. The generic DisplayServer interface registers two internal
C++ callbacks for a text client: an atomic edit and a context query. LineEdit and
TextEdit supply them; CodeEdit inherits TextEdit's implementation. Other backends
retain their existing input delivery and never query the new context callback.
The interface is not bound to scripts or added to the GDExtension API.

## Data flow

The main thread queries the focused control after buffered input has been applied.
It supplies a bounded context dictionary:

- `text`: committed text around the primary caret/selection, excluding preedit;
- `cursor`, `anchor`: Unicode character positions within that text;
- `origin`: document position `(column, line)` of the context's first character;
- `multiline`, `secret`, `available`: editing capabilities and privacy constraints.

At most 1000 Unicode characters are copied, so the UTF-8 representation fits the
protocol's 4000-byte limit. TextEdit walks only the bounded document range, including
line breaks. It does not copy a complete source file on every frame. Passwords,
oversized selections and multiple carets advertise no surrounding text. Ordinary
commits still work, including normal multicaret insertion; extended context edits
are disabled when a faithful context cannot be represented.

The Wayland thread keeps a copy of the context. `ime_context.h` converts protocol
byte offsets to Unicode positions and builds an edit dictionary. An edit contains
`text`, and optionally its context snapshot, a half-open replacement `range`, and
post-edit `selection` positions `(cursor, anchor)` within the resulting context.
`delete_surrounding_text` counts bytes outside the selection. `cursor_position`
uses byte offsets relative to the cursor after insertion. Malformed ranges,
overflows, and offsets inside UTF-8 sequences are rejected and reset the IME.

The main thread applies an edit only while its control and focus generation remain
current. Extended edits also require an unchanged context snapshot. A stale edit
triggers a fresh protocol reset instead of applying its byte range to unrelated
text. Each replacement is one undo operation; LineEdit also preserves its length
limit and strips characters it cannot display.

## Reset and preedit

Switching clients or changing text/caret outside an IME edit sends a protocol
reset. A `wl_display_sync` barrier discards input from before the reset, as required
by the protocol. Queued commit and preedit messages retain their window and focus
generation. Leaving the surface invalidates old messages. Reset callbacks are
destroyed when the seat disappears.

Deletes preceding preedit are queued before the preedit update. The adapter uses
the end of preedit as the default cursor and converts highlight/selection ranges
from UTF-8 bytes. Godot currently represents a single preedit selection range.

Remaining work in roadmap point 4: commit fallback supplied by preedit, hidden
preedit caret and richer styling, continuous cursor rectangle/candidate placement,
editor layout above the keyboard, and device coverage with a composing input
method. These are not covered by plain Russian/English commit tests.

## Validation

Build the desktop editor with `tests=yes`, then run:

```sh
bin/godot.linuxbsd.editor.x86_64 --headless --test --test-case='*AuroraIME*'
bin/godot.linuxbsd.editor.x86_64 --headless --test --test-case='*TextEdit*'
```

The native fixture and trace verifier are in `misc/aurora/ime_smoke/`. They exercise
Maliit acknowledgements, UTF-8 cursor positions, multiline text and password wire
privacy. A trace check does not replace device testing of prediction/preedit.

Protocol reference: [vendored text-input-v2](../../platform/auroraos/protocol/text-input-unstable-v2.xml).
Qt's reference handling is in [QWaylandTextInput](https://github.com/qt/qtwayland/blob/5.15/src/client/qwaylandinputcontext.cpp)
and [the shared event builder](https://github.com/qt/qtwayland/blob/5.15/src/shared/qwaylandinputmethodeventbuilder.cpp).
