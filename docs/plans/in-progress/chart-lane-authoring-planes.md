# The chart lane's authoring grammar: two planes

Status: DESIGN, signed 2026-09-22; to build after the arrow-and-reveal fix lands. Supersedes the
digit row, the `Alt`+digit row and the `Insert` row of `keymap-matrix.md`, and the 2026-09-11
rulings "a digit strictly inside a ring is a point" and "nothing single-press cuts a ring".

## The rule

Bare keys act on **notes**. `Alt` reveals the **stored ring** and acts on it. Where a ring covers
or ends at the slot, an `Alt` key addresses that ring; where none does, it means what the bare key
means. So the two never compete: at a slot holding both a head and a previous ring's end, the bare
key is the head and the `Alt` key is the ring, with no selection and no Esc in between.

## The keys

| Key | Says | Empty slot | On a head | Inside a ring | At a ring's end |
|---|---|---|---|---|---|
| **digit** | a note here, at the typed fret | places the head | retypes it | places the head; the ring ends against it | places the head; the ring ends against it |
| **`Insert`** | a note here, at the fret in force | places the head | retypes it | places the head; the ring ends against it | places the head; the ring ends against it |
| **`Alt`+digit** | a point on the ring here, at the typed fret | = digit | = digit | states an interior point | states the end statement |
| **`Alt+Insert`** | a point on the ring here, at the fret in force | = `Insert` | = `Insert` | a silent point, to be given a bend or shake | the end statement at the fret in force |

**The fret in force on the string at a slot**, one definition for both `Insert` chords: inside a
ring, the ring's path at that offset; past a ring's end, the fret that ring released or arrived
at; with no note before it, 0. So `Insert` after a fret-5 note places a 5 — the repeated note —
and `Alt+Insert` on its tail states the point that says nothing yet.

## The cut, and its one refusal

A bare digit or `Insert` inside a ring places the head and the ring ends against it: exact
adjacency, the resting place every abutting note already has. If the cut would delete a statement
inside the ring, it refuses — the guard the move verb has (`moveErasesStatement`), now insert's
too — and the refusal flash shows which note refused. An end statement is the end's own and rides
back to the new end, as under any truncation. The cut is visible (a head appears where you
typed) and one undo.

`Shift+L` keeps its own job: splitting a ring at a point into a glide and a claimed continuation,
which a cut is not.

## Navigation at a shared instant

The arrows honour the object walk's order: at a slot holding both a ring's end statement and a
head, Right lands on the end statement first, then the head; Left mirrors; `Tab` and `Shift+Tab`
the same. An empty ring end is never a stop. A step that lands on a grid position with no object
lands on the slot alone.

## The reveal

Only `Alt` shows the stored form. Selection and the caret peek show the presented form, so a
clicked end chip stays where it was drawn. Every verb that authors on or moves the ring is an
`Alt` chord — `Alt`+arrows, `Alt`+wheel, `Alt`+digit, `Alt+Insert` — so the true geometry is
visible whenever it is acted on. A bare digit on a slot that looks blank but lies inside a tail
the presentation clipped yields a visible head, never a silent point.

## The flash's consumers so far

The legato verb's counted skip; a cut that would delete a statement; a `Shift+L` split refused.
Recorded in `refusal-flash.md`.

## Known cost

On AltGr layouts (German, Polish, Brazilian) AltGr+digit reaches the editor as `Alt`+digit and
would state a point where the charter typed a character. The composed-character filter must learn
that case before this ships to those layouts.

## Build

One change set: the digit dispatch (`chartCaretDigitTarget` and the pending fret entry's targets),
insert's guard through `moveErasesStatement`, `Alt`+digit and `Alt+Insert` as the ring plane with
the note-plane fallback, bare `Insert` as the note plane, and the keymap, `keyboard-input.md` and
`the-editor-2d-views.md` restated. Tests at controller level for every cell of the table, the cut's
refusal, and the shared-instant addressing. Then the sighting.

## Still open, separately

Where each surface draws a ring's end statement: `ring-end-display.md`.
