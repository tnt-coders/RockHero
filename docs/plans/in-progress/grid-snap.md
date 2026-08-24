# Grid snap

Status: **settled, 2026-08-23** (every point ruled by the user). This replaces the `Ctrl` fine
tier, which is deleted outright.

## Goal

Give the editor one session-scoped **grid snap** switch, and make one **placement quantum** the
only fact any verb consults when it quantizes a time position. Off-grid authoring stops being a
per-verb modifier composition and becomes a mode you deliberately enter — and almost never should.

## The session fact

A boolean, **default on**, owned by the editor controller for the life of a project session:

- **Never persisted.** Not to editor settings, not into the project package. The grid *value*
  keeps its per-project app-local record; snap does not.
- **Resets to on whenever a project boundary is crossed** — every open, close, and import that
  establishes a grid session, through the one `resetGridSession` seam that sets the grid value and
  the snap switch together so no path can move one and forget the other. App launch is the same
  rule, expressed by the member's initializer. (The open live-rig failure teardown establishes no
  grid session — it leaves the grid value alone too — so it is not a reset site; the next
  successful open is.)

The reset is the point, not an omission. A user who turns snap off to place one grace note should
not find it still off a week later; the friction and the reset are what keep the mode rare.

## One placement quantum

> effective quantum = snap on ? the session grid note value : 1/3840 of a whole note

Every verb that **quantizes a time position** reads that one fact, with no per-verb opt-out:

- note insert position (pointer and caret)
- note move / nudge
- the sustain gesture's steps
- tone-change marker placement
- tone-region boundary drags and automation-point placement / nudge
- timeline seek and caret clicks, caret arrow stepping, time-selection endpoints

1/3840 of a whole note is 1/960 of a quarter note — the standard MIDI PPQ tick, and exactly the
step the old `Ctrl` tier moved by in x/4. 3840 rather than 4096 because triplet grids need the
factor of 3. It is a note value like any other, so the existing lattice arithmetic
(`snapGridPosition`, `adjacentGridPosition`, `adjacentTempoGridPosition`, and the measure lattice
underneath them) walks it with no new math.

## The quantum is not the grid value

This distinction is the plan's sharpest edge, and the one a later reader is most likely to blunt.

- The **grid value** stays displayed and changeable while snap is off. It remains the visual
  reference lattice the editor draws, and it remains the **musical unit** the editor authors in.
- The **quantum** is only ever a *position* rule.

So a verb that needs a musical **duration** keeps reading the grid value. The concrete case is
`planInsertNote`'s `default_sustain`: a placed note's ring is the session grid step, because that
is the note value the user is writing in. A 1/3840 default ring would be absurd, and no snap state
may produce one.

Read any ambiguous site this way: *is the number a place on the timeline, or a length?* Places take
the quantum; lengths take the grid value.

It follows that **"walkable" and "selectable" are two questions, and each gets its own predicate**
(corrected 2026-08-24, review finding). Admitting the tick to `isValidTempoGridNoteValue` is
required — otherwise the geometry layer's normalization silently swaps the quantum for the default
grid the moment snap goes off — but that predicate was also the only gate on the free-text grid box,
so widening it let a user type `1/3840` and make the tick the *drawn* lattice, which
`visibleTempoGridLines` then walks line by line across the visible span. Snapping would not have
noticed (it is a binary search); drawing would. `isSelectableTempoGridNoteValue` keeps the 1/128
bound for anything applied as the session grid — the box and the per-project restore — while the
tick still passes the geometry layer. Not one rule stated twice: one rule per question.

## What the toggle looks like

`Ctrl+G`, a registered rebindable command like every other editor verb, listed under Grid & Zoom.

No new widget announces the state. Instead the two surfaces that already say "grid" stop claiming
to bind while snap is off, both derived from the one session fact:

- the 2D lane's grid lines dim, through the editor's one quieting rule: **halve the mark's contrast
  against the ground it sits on**;
- the grid readout beside them strikes through its **value** with a horizontal rule, and changes
  nothing else.

Grid spacing stays selectable while snap is off — the readout says "not binding right now", not
"unavailable".

**The readout marks the number, never the control** (corrected 2026-08-24, sighting). The readout
first shipped quieted under a veil across the whole strip with the strike run the width of the
combo box, and the sighting rejected every part of that: the veil "darken[ed] an area AROUND the
'Grid' word and the dropdown which look[ed] a bit odd"; dimming the box made it "appear
'unavailable' which is kind of misleading because the grid can still be adjusted but it's just for
reference with grid snap off"; and crossing "the section with the drop down arrow itself makes it
look kind of like the arrow is not supposed to be clicked anymore which is unintuitive". All three
are the same error — marking the *control* to say something about the *value*. So the indicator is
now a one-pixel horizontal rule fitted to the value text's own glyphs, like a pen through a printed
price, while the caption, the box, and the arrow render exactly as they do with snap on. The
strike's geometry comes from the combo box's own text label rather than from restated layout, so it
tracks the digits as the value changes width.

**Horizontal, not diagonal** (corrected 2026-08-24). The mark first shipped as a thin diagonal and
that was wrong twice over. A diagonal through a figure is the *prohibition* grammar — the slash of a
"no" sign — which says the value may not be used, when it may; the horizontal rule is the
*strikethrough* grammar, which says a figure is no longer in force while leaving it perfectly
readable, and that is exactly the state. The diagonal's angle also fell out of the value's own
width, so "1/4" was struck steeply and "1/128" nearly flat: one fixed state drawn as a varying mark.
At zero degrees every value is struck alike.

The rule is drawn on **one whole pixel row**, which is load-bearing rather than fussy. The digit
band's centre is fractional, so a 1px line laid there spreads over two rows at about half coverage
each — measurably, and visibly, the dimmed treatment this indicator exists to avoid, reached by
accident instead of by choice. Rounding puts all of the ink on the single row nearest that centre
(row 16 of the 32px strip, at every value), and the row is derived from the font's line box rather
than from the characters, so it does not move as the number gets wider. The mark takes its colour
from the combo box's own `ComboBox::textColourId`, not from `EditorTheme`, because its correctness
condition is "the same ink as the digits underneath" — the theme's `primary_text` merely happens to
be the same white today.

The two surfaces still differ in *form* for the reason the quieting rule gives: the lane's dots are
the editor's own marks, so it quiets them; the readout is a JUCE control whose chrome and text the
parent cannot re-color, and compositing over borrowed chrome is precisely what produced the
"unavailable" reading. A mark is the only honest thing a parent can add to a control it does not
draw.

**The ground is the mark's own, never a named constant** (corrected 2026-08-24, review finding).
The tab lane quiets by leaning its ink toward `0xff101010` because that near-black *is* that
surface's ground and translucency there would reveal the tail, lane line, and chord fill underneath
a note. Neither premise holds on the timeline canvas: it paints three different row backgrounds
under one grid (the tone row's `0xff1a1e25` is lighter and bluer than the near-black, so a
subdivision dot pre-mixed toward it lands *on top of* that band and disappears), and the layering
contract puts nothing but those backgrounds under the dots. So the grid dots quiet by drawing
**translucent**, which is the same halving with the compositor supplying each band's real ground.
Chrome the parent cannot re-color — the grid readout's combo box — is not quieted at all; it is
marked instead, per the ruling above.

The toggle is also **forwarded to the 3D preview window**, alongside the grid-size pair. That
window forwards the caret verbs because paused preview follows the marker, and the arrows step the
*quantum*: with snap off, one press crawls a single tick and the grid-size commands can no longer
change that, so leaving the toggle out would strand the caret until the user went back to the
authoring window.

## What is deleted

The `Ctrl` fine tier, in full:

- the fine step branches in the selection move, the sustain gesture, the lane nudge, and the
  pointer/click placement seams;
- the four `Ctrl+Alt+arrow` move commands and the two `Ctrl+Alt+Shift+arrow` sustain commands;
- the fine placement grid itself (`fineGridPositionForBeat` and its 1/960-beat denominator) and the
  cursor placement mode that let `Ctrl` bypass the grid;
- `ChartSustainStep`'s optional grid note value — a recorded step now always carries the note value
  it snapped by, which is the quantum at the moment of that step. The gesture replay machinery is
  unchanged.

With snap off, one wheel detent or one arrow press *is* one tick, so the time axis loses nothing.

## Scope exclusions

- **No new `Ctrl` behavior.** The modifier is freed, deliberately, and stays free. In particular
  the "sustain grows across the next onset under `Ctrl`" idea is out of scope here.
- **No persistence, no settings entry, no project-format field.** Session-only means session-only.
- **The value axis is not a position axis.** Automation values keep their single step (one discrete
  state, else 0.01); the 0.001 tier goes with the rest of the fine tier rather than surviving as
  the last `Ctrl` composition.
- **The ruler keeps its solid ticks.** It is a ruler, not the lane; only the lane's grid quiets.
