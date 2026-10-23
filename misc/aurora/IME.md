# Aurora IME editing context

The Aurora adapter supports `text-input-unstable-v2` without introducing a new
shared Wayland backend. The generic DisplayServer interface registers three internal
C++ callbacks for a text client: an atomic edit, a context query and completion
of a native composition before its context disappears. LineEdit and
TextEdit supply them; CodeEdit inherits TextEdit's implementation. Other backends
retain their existing input delivery and never query the new context callback.
The interface is not bound to scripts or added to the GDExtension API.

## Data flow

The main thread queries the focused control after buffered input has been applied.
It supplies a bounded context dictionary:

- `text`: committed text around the primary caret/selection, excluding preedit;
- `cursor`, `anchor`: Unicode character positions within that text;
- `origin`: document position `(column, line)` of the context's first character;
- `multiline`, `secret`, `available`, `completion`: editing capabilities and privacy constraints.

At most 1000 Unicode characters are copied, so the UTF-8 representation fits the
protocol's 4000-byte limit. TextEdit walks only the bounded document range, including
line breaks. It does not copy a complete source file on every frame. Passwords,
oversized selections and multiple carets advertise no surrounding text. A password keyboard type also
sets privacy independently of whether the LineEdit draws masked characters. Ordinary
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

Deletes preceding preedit are queued before the preedit update. Preedit carries
internal metadata alongside its display string: `commit_text`, `caret_visible`,
and `styles` (half-open Unicode ranges and named decorations). The fallback may
be different from the displayed composition, or empty. Each control retains its
own metadata until apply/cancel, so switching native clients cannot commit another
field's text. A metadata-only update also notifies the focused control. Backends
without metadata preserve their original apply behavior. Native surface leave and
window focus loss finish the cached composition before clearing it. The finish
callback is focus-scoped and idempotent: a delayed old client cannot close a new
client's IME. External context changes still reset/drop stale requests. Negative or invalid byte
cursor positions hide the caret; an omitted cursor means the end of preedit.

Style ranges are converted only at UTF-8 boundaries. Controls render default and
underline spans, emphasize active/selected spans, dim inactive spans, tint selected
spans and mark incorrect spans in red. `none` leaves the range undecorated. The
text shaper supplies bidirectional/wrapped selection geometry. These decorations
do not switch fonts or synthesize a wavy error underline. Native styling is bounded
to 1024 spans; unusually large preedit strings retain the existing basic renderer.

LineEdit/TextEdit already refresh IME placement while drawing. They now supply the
actual caret rectangle through an optional internal DisplayServer method; other
backends retain their existing point API. Aurora transforms all four rectangle
corners before converting to surface units. Unchanged rectangles do not generate
extra protocol requests. Keyboard overlap also retains both dimensions: on the
portrait-native KVADRA a landscape keyboard arrives as a 520×2000 surface area and
becomes a 2000×520 overlap in Godot coordinates. Taking the untransformed height
would incorrectly reserve almost the entire editor. ScriptEditor uses the rotated
height and viewport scale to place its code viewport above the keyboard. Hiding
the keyboard targets the active text-input window rather than assuming window 0.

Normal text advertises completion/correction hints when bounded context is available.
CodeEdit opts out through a protected C++ capability method; TextEdit retains
natural-language prediction. Password controls never advertise completion. This
policy changes only Aurora's context consumer, not other backends' input paths.

Remaining work in roadmap point 4 includes device acceptance of composition and
prediction, dialog avoidance, and candidate placement through orientation changes.
Pure conversion/control tests do not establish that a composing input method has
been exercised on the device.

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
