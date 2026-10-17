# Fret-hand `none` positions: where the hand leaves, and stretches with no window

Plan 60b was since redesigned around holds with stated extents (`docs/plans/roadmap/60b-holds.md`),
so this plan's references to the old hold record (`HandHold`, `HandHoldGrip`, holds with no end) are
stale.

**Status:** Unscheduled idea, recorded 2026-10-15 from the user's report and a design review the
same day, and revised the same day to the user's `none`-only shape (the `departs` sketch it
replaces is kept below as the alternative considered). Nothing here is built or ruled: the shape
below is a PROPOSAL, and the open questions are open. It sits beside plan 60b's holds
(`docs/plans/roadmap/60b-holds.md`) and should not start until 60b's verbs and marks land, since it
mirrors their record. Re-verify every anchor against the code before executing; this is a `todo/`
plan and may lag.

## The problem in plain words

An authored fret-hand position says where the hand is and how wide it reaches, and then it
"stands until the chart contradicts it". Nothing in it says when the hand LEAVES. So the charter
cannot state two things that a player's hand really does:

- **Move during a stretch the notes do not force.** Open strings ringing over a position never
  morph the window, even though the fretting hand is free to travel there. In the user's My
  Sacrifice test project, measure 4, the recording audibly carries the hand sliding along the
  strings between the authored positions at fret 2 and fret 10. That travel should draw as a
  morph, but today the window simply jumps to 10 where the next figure fronts.
- **Have no position at all.** Where nothing is held or played, the honest statement is "the hand
  is nowhere in particular". Hand-position rule 4 deliberately refuses to derive a rest break,
  which is right for the derivation, but leaves the charter no way to state one either.

The user's own diagnosis (2026-10-15): this may come from positions having no way to store their
end.

## Today's record

`FretHandPosition` in `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` is
`{position, fret, end_fret?}`, saved as `positions[] {at, fret, endFret?}`. `end_fret` is a FRET
ceiling for the window's width, not a time edge. Under "Authored positions" in
`docs/design/rulesets/hand-position-rules.md`, a position takes its own instant and every later
instant that fits its window, up to the next authored position or slide arrival; the numbered
rules resume at the first instant that does not fit. The position's tenure therefore ends only
where the notes end it, and never where the charter says.

## The proposal: a position is a window or `none` (the user, 2026-10-15)

```text
{start, window | none}        window = {fret, end fret?}
```

No time edge is added. **A `none` IS the departure**: the position before it stands until the next
record, as every position already does, so it ends at the `none`'s start.

- **Its meaning is the existing tenure rule with an empty window.** A `none` covers no fret, so it
  takes no instant: it stands until the first instant that demands the hand, or the next authored
  position. Open strings and taps demand nothing (rule 1), so they never end it. There the numbered
  rules resume exactly as they do at the song's start, where no window stands either (the
  derivation's standing window is already "nothing" there): rules 5, 6 and 11 have no window to
  keep, so a run equal to the window before the `none` is emitted again (the hand came back); the
  slide rule has no window to shift, so rules 3 and 7 to 9 place the arrival; and rule 10 fronts
  as for a first run, never reaching back past the `none`. Rule 3 already closes a run at an
  authored position. Rules 4 and 10 need no new clause.
- **The coverage rule needs no new clause.** A window that covers nothing is refused wherever a
  stop is gripped at its place: a fretted onset struck there, a ring's hold standing there (a
  slide's or a slide-out's held stop included, since each holds to its arrival or ring end), or a
  grip hold. So a `none` cannot stand inside a hold whose grip is held, nor while a fretted note
  still rings; inside a 60b `"none"` hold it may. The hand cannot be nowhere while it frets.
- **The motion is the existing morph.** Across the `none` the window morphs from the window
  standing before it to the next event's window: the drawn track's Morph approach, its ramp
  starting at the `none` instead of one minimum sustain distance before the arrival. A `none`
  closer than that margin states a late, quick move. It lands where the next event fronts: an
  authored position where it stands, a derived window at rule 10's front. It is NOT a slide-out
  ride: that one follows a note's rail on the release curve and ends at speed, and a `none` has no
  rail. So `FretHandRide`, `deriveFretHandRides` and its landing rule are untouched.
- **With nothing after it**, no window stands to the end of the song, and there is nothing to
  morph toward.
- **It mirrors 60b's record shape, not its behaviour.** A hold's `"none"` WALLS: no span may form
  inside it until its end, the next struck chord or the next hold. A position's `none` YIELDS: the
  first demand ends it, as
  what does not fit ends every position's tenure. The difference is principled, not drift: the
  derivation never needs the hand to be nowhere, and the notes can force a hand back.
- **What it cannot say:** that the hand left early but arrived somewhere other than where the next
  window starts. The user judges that not a real case. Every other departure is a `none` at the
  instant the hand leaves.

**Acceptance figure:** My Sacrifice, measure 4. A `none` placed where the hand leaves the
authored 2 makes the audible slide to the authored 10 draw as a morph over the whole stretch.

### The derived products (sketch)

- **The hand events** state what stands from each front: a window, or nothing. The derivation's
  `standing` is already a `std::optional<FretWindow>`, nothing at the song's start, so a `none`
  only returns it there. Readers of an event's window then meet the nothing case: the chips, the
  drawn track and the camera. The rides' standing lookup provably never does: a slide-out departs
  at an instant, which ends any `none`, and its held stop refuses a `none` inside it.
- **The drawn track** takes the ramp start of the arrival after a `none` from the `none`. Where a
  `none` stands exactly at a slide-out's end, the ride still lands on its ridden window and the
  morph leaves from there, so "an event at the ride's end" must mean an event with a window.
- **Span extents** cut where a morph starts (the projection's move starts), so the `none`'s
  ramp start joins them; the coverage rule has already ended every hold by then.

### The alternative considered: `departs`

The first sketch made a position `{start, departs?, window | none}`, with `departs` a ceiling on
the position's tenure and the gap to the next window a hand transition. It lost because in every
case it can state, a `none` at the departure states the same thing, and it costs more: a second
time edge; a forward obligation that notes after the departure be covered by "the ride or the next
window", against the coverage rule's "notes after its place never make a position invalid", with
no defined answer for notes after the departure that the old window still covers (the `none` has
one: they end it, and the derivation places them); a ride landing rule and a third ride source;
a rule 10 clause; and two ways to say "no window here" (a departure's gap and a `none`). The
`none`-only shape deletes all of it, because its meaning is a rule that already exists.

## Silence

60b's one silence rule for holds and positions carries over unchanged, read through record
equality:

- A `none` restating the `none` standing immediately before it, with no derived event between
  them (only open strings or silence), is silent and dissolves.
- A `none` after a window is never silent; neither is a window after a `none`, even one equal to
  the window before the `none`.
- A `none` before the first demand restates no authored statement, so it is a kept pin. It is not
  always inert: where open strings precede it, it stops rule 10's first front reaching back to
  the chart's first note.
- "Prune Hand Positions" (`withoutRedundantPositions`) never drops a `none`: the derivation never
  produces one (rule 4), so removing one always changes the events.

## Open questions

- **Where a derived window lands after a `none`.** Rule 10 unchanged fronts it at its figure's
  leading open strings, not its first fretted note, so the morph ends there. Leaning: keep rule 10
  as it is, and let a charter who wants the hand to land on the fretted head author the arrival
  there. A `none` clause in rule 10 would undo what the rule measured.
- **The dim on the highway.** "The fretboard dims while no window stands." The highway already
  dims a hand's light along a Morph leg (`motionDim` in
  `rock-hero-common/ui/src/highway/highway_renderer.cpp`), but that dim scales with the leg's
  travel and steepness. A `none` that returns to the window it left, a long slow one, or one with
  nothing after it travels little or nowhere, so a travel-scaled dim would barely show it or not
  at all: a board drawing a window the charter said does not stand. So the dim must read the
  `none` itself, not the travel. Leaning: the leg out of a `none` is its own kind of approach,
  which the one dim authority reads at a stated depth. Also open: whether the opening scroll,
  where the derivation stands nothing and the highway shows the first window early, dims too; and
  what the window does after a `none` with nothing following it.
- **What an explicit `none` looks like in the 2D view** (the user, 2026-10-15). The 2D tab draws
  position chips only today. Plan 60b's holds have the same open question for the not-held
  stretch, a hold whose grip is `"none"`: PROPOSED there to read by PATTERN, hatched or dashed
  rails in a neutral EditorTheme color, to be chosen from rendered options at a sighting
  (`docs/plans/roadmap/60b-holds.md`, the not-held stretch's look). Recommendation: ONE shared
  visual language for "none" across the span row and the position row, so a charter learns it
  once, settled together with the ui-design-expert and a single sighting of both rows.
- **No duration bar is needed for positions** (the user, 2026-10-15, withdrawing an idea of the same
  day to draw each position as a bar to its tenure's end). Positions are always continuous: a chip
  holds until the next chip, and it stays pinned to the left edge while scrolled, so a `none` chip
  already reads as covering everything up to the next one. Holds differ — a hold may end with no
  hold after it, which is why a span draws its end.
- **The type.** A sum, so "a window that is no position" is unrepresentable. `std::optional` of
  the stated window is the smallest that fits; 60b's `HandHoldGrip` is a variant because a third
  alternative (a grip id) is coming, and positions foresee none.
- **The format.** How `positions[]` spells a `none`, mirroring 60b's `"grip": "none"` keyword;
  `endFret` is meaningless on one.
- **Verbs.** How a charter authors a `none`; moving and deleting one as any position; what the
  end-fret keys (`SetHandPositionEndFret`, `ClearHandPositionEndFret`) do on a `none`, which has
  no window.
- **A demand the coverage query may miss.** `forEachGrippedStop` visits onsets struck at a place
  and holds standing there. A demand stated by a keyframe on an unpressed stop (hand-position rule
  13, a behind-the-nut bend of an open string) may be neither. Verify; a `none` at such a place
  must be refused, or the derivation would emit a `none` and a window at one front.

## Touchpoints

- **The record:** `FretHandPosition` in
  `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` (beside `HandHold` and
  `HandHoldGrip`, the shape it echoes).
- **The format:** the `positions` reader and writer in
  `rock-hero-common/core/src/chart/chart_document.cpp`, and the `positions[]` row in
  `docs/developer/file-formats.md`.
- **The derivation:** the authored-position branch of `deriveFretHandEvents` in
  `rock-hero-common/core/src/chart/chart_fret_hand.cpp`, and `FretHandEvent` in `chart.h`; the
  coverage check reads `positionCovers` in `rock-hero-common/core/src/chart/chart_rules.cpp`.
  `deriveFretHandRides` and `FretHandRide` are untouched.
- **The rules:** `docs/design/rulesets/hand-position-rules.md`, "Authored positions" (the `none`
  sentence) and "The coverage rule" (a `none` covers nothing); rule 4 may point at the `none` as
  the charter's way to state a rest. A ruleset change needs the user's confirmation first.
- **The verbs:** the hand-position entries of `MarkerVerb` in
  `rock-hero-editor/core/include/rock_hero/editor/core/timeline/marker_verb.h`, their handlers in
  `rock-hero-editor/core/src/chart/fret_hand_handlers.cpp` and
  `rock-hero-editor/core/src/timeline/marker_row_handlers.cpp`, the undo snapshot in
  `rock-hero-editor/core/src/chart/fret_hand_positions_snapshot.cpp`, and the command registry in
  `rock-hero-editor/ui/src/keybinds/editor_command_registry.cpp`.
- **The marks:** the drawn track (`trackArrivals`, the ramp starts and move starts,
  `ChartViewState::fret_hand_track`) and the chips (`FhpViewState`) in
  `rock-hero-common/core/src/chart/chart_projection.cpp`, carried to the highway by
  `rock-hero-common/core/src/highway/highway_projection.cpp`; the camera's active placement in
  `rock-hero-common/core/src/highway/highway_camera.cpp`; the motion dim in
  `rock-hero-common/ui/src/highway/highway_renderer.cpp`; the 2D position chips
  (`tabFhpChipText`, `drawTabFhpChip`) in `rock-hero-common/ui/src/tab/tab_paint_core.cpp`.
