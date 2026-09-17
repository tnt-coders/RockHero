# Artificial Harmonic Authoring — the touch-over-a-stop verb

Status: **DESIGN RECORD, not scheduled.** Opened 2026-09-15, the day the user scoped harmonic
authoring to the natural and pinch families only and deferred the artificial (touch-over-a-stop)
and tapped family here. Nothing below is built. **The user has ruled NOTHING in this record beyond
the deferral itself** — every design statement is either a verified code fact, published-notation
precedent, or the music-notation expert's recommendation of 2026-09-15, and each is labelled as
such. The rulings the user still owes are listed at the bottom.

## Why this exists

The shipped `H` verb authors a *fret-hand* harmonic: the fret you type is the node, the note is
written with `fret = 0`, and the string speaks from the capo. That covers naturals. `Shift+H`
covers pinches. What neither covers is the family a player reaches most often after those two — a
finger pressing a real stop while another finger touches a node above it. The chart model already
stores that family, the importer already writes it, both surfaces already draw it, and the editor
can already destroy it. It simply cannot create it. This record holds the design for the missing
verb so the deferral does not lose it.

## What exists today (verified against the code)

- **One absolute node per harmonic note.** `ChartNote::harmonic_node` is an
  `std::optional<double>` giving the touch position in fret units on the string's own axis
  (`rock-hero-common/core/include/rock_hero/common/core/chart/chart.h:860`). Which hand damps it is
  read from the attack through `nodeIsOnNeck` (`chart.h:434`), which excludes only `Pinch`.
- **The validation bound is the string, not the neck.** `g_max_harmonic_node = 48.0` — exactly
  `12 * log2(16)`, the 16th partial's bridge-side node, with `g_max_harmonic_partial = 16` as its
  partial form (`rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h`) — and
  `harmonicNodeCeiling` narrows it to the neck only when the *fretting* finger is the one standing
  on the node. Import snapping is capped separately at `g_max_snapped_partial = 8`, measured from
  Guitar Pro's own label run; the natural picker offers the whole bound.
- **The artificial family is imported.** `rock-hero-editor/core/src/project/gp_chart_builder.cpp`
  lines 3111–3183 treat GP's `Pinch`/`Semi`/`Artificial`/`Tap` alike: the note keeps its pressed
  fret, GP's `HarmonicFret` is read as a *partial label* rather than a position, and the node is
  written as `physicalStopFret(note, capo) + offset` — the octave (offset 12) when no usable label
  exists or the labelled node would exceed `harmonicNodeCeiling`.
- **It is validated, retyped, drawn and cleared.** A retype moves the node with its stop, because a
  node is `stop + offset` and fret spacing is logarithmic
  (`rock-hero-editor/core/src/chart/chart_edits.cpp:1338-1353`). 2D labels the head through
  `tabNoteHeadText(note, fret_at_head)` (`rock-hero-common/ui/src/tab/tab_paint_core.cpp:1135`,
  described at `docs/developer/the-editor-2d-views.md:502-509`). 3D centres the fret-span line on
  the node via `harmonicMarkFootprint` (`docs/developer/the-3d-highway.md:199-216`).
  `planClearHarmonic` (`chart_edits.cpp`) removes it.
- **It is NEVER authored.** `rg -n "harmonic_node" rock-hero-editor/core/src` finds exactly three
  editor writers: `harmonicTouchNote`, which forces `fret = 0` before resolving the stop
  (`chart_edits.cpp:2059-2070`); the pinch arm of `planSetAttack`, which authors the octave at the
  physical stop and leaves the node off the neck (`chart_edits.cpp:1773-1776`); and the retype
  above, which only *moves* a node that already exists. No verb produces `fret > 0` plus an on-neck
  node.
- **2D drops the stop.** For a `fret > 0` harmonic the head carries the NODE and nothing on the
  head states the pressed fret (`tab_paint_core.cpp:1135`, `the-editor-2d-views.md:502-509`). The
  number a charter reads is the touch; the press is invisible.

## The notation precedent (the expert's findings, 2026-09-15)

- Published tab distinguishes natural from artificial **by label, not by number**. Hal-Leonard-style
  legends put "A.H." (or "T.H." for a tapped one) above **two** fret numbers — the stop first, then
  the touch. A natural is "Harm." over one number, or a diamond head.
- **Guitar Pro ships two commands, not one.** `Y` authors the natural, where the typed fret IS the
  touch; a separate artificial dialog takes the pressed fret and the harmonic point as two inputs.
- **Soundslice's editor supports natural harmonics only**, and says so in its documentation.
- The consequence the expert drew: RockHero's `H` is GP's `Y` gesture, not an invention. What is
  missing is not a fix to `H` — it is `H`'s **sibling verb**.
- Convention is **genuinely silent** on three things this design must decide, and the expert asked
  that they be labelled as invention wherever they are settled: how an editor should *gesture*
  harmonic entry at all; row ordering and preselection inside a picker; and whether a scrolling
  surface can state a press-and-touch pair at a single onset.

## The recommended design (the expert's recommendation, 2026-09-15)

**Verb.** Keep `H` natural-only. Add a touch-over-a-stop verb on its own chord — `Alt+H` suggested,
with the keymap matrix and the naming expert owning the final word, since `Shift+H` is already the
pinch. The verb **never rewrites the fret**: it opens the node picker on the ladder above
`physicalStopFret(note, capo)` and writes `harmonic_node = stop + offset`. For a stop at 5 the
expert sketched the ladder as 17, 12, 10, 9, 8.16, 7.67 …; the exact positions come from
`harmonicNodeCandidates`, which the natural verb and the importer already share. On an **open**
string that ladder collapses to the natural ladder. (The expert's sketch predates the natural
picker's bound of 16, under which frets 1, 11 and 13 name nodes of their own; under the stop verb
they are ordinary stops carrying a playable ladder either way.)

**Stored model: NO change.** One absolute node with the note's fret as the stop already covers
every case — natural (`fret = 0`, node on the open ladder), artificial (`fret > 0`), tapped
(`attack: Tap`, the stop riding `held`, `physicalStopFret` resolving it), pinch (node off the neck)
and capo. The expert killed stop+offset and stop+partial storage: a second frame buys nothing the
one absolute number does not already carry, and a stored partial does not *name* a node — it would
force snapping to computed ideals, which the format forbids.

**Surfaces.** The head keeps the **node**. But the **stop must also be recoverable on both
surfaces** for a `fret > 0` harmonic. 2D: the published compound — the stop beside the bracketed
touch, the shape legends print as "A.H. 5 17" — with the exact form a question for the UI design
expert. 3D: the node-centred fret-span line becomes a **stop→node** line, saying "press here, touch
there". Leaving 3D node-only once authoring exists would let the surfaces diverge, which the
project forbids as a standing rule.

**Picker rows.** Absolute touch positions with the partial ordinal beside them — the vocabulary the
shipped natural picker (`SetChartHarmonicNode`, whose partial became a `std::optional<int>` on
2026-09-16) already uses. Preselect the **octave (2nd partial)**:
it is the importer's own default and the easiest partial to ring at any stop. Offset rows,
partial-only rows and pitch-only rows were all killed as row *labels*; a **sounding-pitch secondary
column** beside the position is legitimate.

**Ladder length.** The expert recommends offering through the **8th–9th partial** on the stop verb
while the validation bound stays 16: above the 9th, adjacent nodes sit within about 7 mm under a
10–15 mm fingertip. The bound itself admits 79 nodes. Recorded deliberately for comparison: for the
**natural** picker the user ruled the *whole* bound (partials 2–16), rows sorted by partial, lowest
first. The two verbs' list rules should therefore be compared on purpose, not diverge by accident.

**What the natural verb became on 2026-09-16, and what this plan inherits.** `H` is no longer a
toggle: its law is **offer every CHANGE the selection allows, and ask only where there is more than
one**, and WHICH ROWS CHANGE ANYTHING IS THE PLANNER'S ANSWER: every node row its label names is
shown (a ticked row that changes nothing included), and the **"No harmonic"** row LEADS them, ruled
off by a separator, only where the clear itself changes something — hence the optional partial. The
preselected row is what the old toggle would have done ("No
harmonic" when every member carries a fret-hand harmonic, else the lowest partial that changes
something), the row ticked is the node the ANCHOR member is touching,
and consecutive choices FOLD into one undo entry through the shared gesture authority rather than
reversing on a second press. The clear also split by hand that day: `planClearHarmonic` writes only
fret-hand carriers and the pinch clears through its own `planClearPinchHarmonic`, which is the seam
this plan's stop verb inherits. The pinch is expected to carry a node/partial eventually and to adopt
the same law; this plan's stop verb should be designed against it rather than against the toggle it
replaced.

**Attack.** The touch verb leaves the attack alone. Tapped harmonics stay two presses; `T` and
`Shift+T` own that axis.

## Alternatives the expert killed

1. **One key branching on the fret.** "Type 12, press `H`" would author an artificial at node 24
   instead of the octave natural — the single most common harmonic in the repertoire, lost to a
   branch.
2. **One key with a union picker.** Two physically different acts (touch-only and press-plus-touch)
   producing two different writes, presented in one list.
3. **A stored `natural | artificial` kind field.** Fully recoverable from `fret`, `harmonic_node`
   and `attack`, and free to disagree with the node it is stored beside — a second authority on a
   fact the model already determines.

## Defects this plan would close

- **The one-way door.** An imported artificial harmonic can be *cleared* (`planClearHarmonic`,
  `chart_edits.cpp`) but never authored and never restored. A charter who clears one has
  destroyed data the editor cannot rewrite. Live today.
- **2D drops the stop.** For a `fret > 0` harmonic the pressed fret appears nowhere on the head
  (`tab_paint_core.cpp:1135`). Live today on imported charts — roadmap 57 cites a real
  third-partial artificial with its node at 24.02.

## Rulings for the user

Each with the expert's recommended answer; none of these is decided.

1. **One key or two?** — Recommended: two. `H` stays natural-only; the stop verb is its sibling.
2. **Which chord for the stop verb?** — Recommended: `Alt+H`, subject to the keymap matrix and the
   naming expert.
3. **Ladder length on the stop verb** — Recommended: through the 8th–9th partial, with the
   validation bound unchanged at 16, and compared against the natural picker's ruled 2–16.
4. **The 2D form** — Recommended: the published compound (stop beside bracketed touch); exact shape
   to the UI design expert.
5. **The 3D form** — Recommended: the fret-span line becomes stop→node.
6. **Does the touch verb write `attack: Tap`?** — Recommended: no; the attack axis stays with `T` /
   `Shift+T`.

## Relation to other records

- `docs/plans/in-progress/technique-review-walkthrough.md` — W15 walked the shipped `H` / `Shift+H`
  verbs and the node picker. This record is the family W15 did not cover.
- `docs/plans/in-progress/technique-compatibility-and-hardening.md` — its harmonic sections hold
  the node-range evidence, the label-window rule and the tap-harmonic representability finding this
  design rests on.
- `docs/plans/in-progress/keymap-matrix.md` — the `H` and `Shift+H` rows, ruled 2026-09-15 with
  the natural picker. A new chord lands there, not here.
- `docs/plans/roadmap/57-positions-past-the-drawn-board.md` — the node-past-the-board question, and
  the source of the 24.02 artificial cited above.
- `docs/plans/todo/tap-harmonic-display.md` — the tapped sibling's display question, adjacent but
  separate.
