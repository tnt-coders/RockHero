# One floor light: the fret-hand glow on proof of a grip

*Ruled 2026-09-25. Supersedes the "fret-hand glow on PROOF OF A GRIP" entry in
`docs/tracking/backlog.md`, which now points here. The plan was drafted by the simplicity-expert
agent from a clean slate, reviewed against the tree, and its decisions ruled by the user the same
day; every ruling is recorded in section 1 so none is re-litigated during the build.*

## 1. Goal and rulings

Today the fretting hand's window light — the floor glow under the hand window, its lane-border
ribbons and its fret-line tier — is lit the whole time a placement exists. The picking hand's
light is lit only around the notes that prove that hand is on the neck: it rises over one arrival
margin before the onset (crowding-clamped so it never reaches back past the previous light's
release), holds to the release, and fades over 0.1 s.

The fretting hand's light becomes the same kind of thing, and the two become ONE light with two
evidence sources: one stretch type, one envelope, one rise / release / decay, one emission through
the floor, the ribbons and the fret-line tier. The hands differ only in what their evidence
producers accept and where each hand's window stands. Any place the build would leave the right
hand on its own code path is a defect.

**Rulings (user, 2026-09-25):**

- **Evidence.** The fretting hand is lit where the chart proves it holds something: every note
  whose onset is not a right-hand onset (fretted, open, dead, natural harmonic, legato, left tap),
  every span, and a right-hand onset whose held stop is pressed. A bare tap proves nothing. Open
  strings are lit by ruling — an open note is drawn as a bar spanning the window, and a dark
  window under it would read as a floating bar.
- **The rest tolerance (F1).** Released at the drawn end, a fretted note's light would dip at every
  margin trim and strobe through a sustainless chug riff (25% between 16ths). So the fretting
  hand's evidence merges across any gap SHORTER THAN A REST TOLERANCE, measured in SECONDS
  (`g_hand_rest_seconds`, one constant, starting value 1.0). Seconds, not beats, by the user's
  ruling and the precedent: whether a dark gap reads as a flicker or as a rest is a readability
  question, and the minimum sustain distance — the same question for tails — sighted more
  consistently across slow and fast songs once it was time-based; this light's own rise and decay
  are already in seconds; and the merge then needs no tempo map. The value is a sighting knob.
  The tolerance is the ONLY threshold; it is a
  parameter of the merge, not a second mechanism, and it lets both hands share one release rule
  (`noteReleaseAt`: the drawn end, or the last pitched keyframe before a drawn slide-out) with no
  second "hold" authority. **The picking hand goes through the same merge with the same tolerance
  in the first build** — the user wants the simplest state sighted before any exemption: a dense
  tap run then reads as one light travelling the run instead of a pulse per strike. If the
  sighting rejects that, the exemption is a per-hand tolerance (zero for the picking hand), a
  parameter and never a branch; the pulse-per-strike look is the fallback, not the target.
- **The motion dim (D2)** is universal: a light moving across lanes dims by its slope, whichever
  hand moves it.
- **The soft edge (D3)** is drawn in full for both hands. The tap light today ends in a hard 50%
  line exactly on its edge (it draws only the slots it touches, and the shader's band straddles
  the edge), which is the "missing side highlight" the user saw beside a tapped chord.
- **Sampling density (D4)**: one policy, `highwayGlideSliceCount`, for the light AND for the rails
  and open tails that sample the same window curve; `windowSampleTimes` is deleted.
- **The tiers (D5)** follow one formula at every (line, time) for both hands — section 2.4. No
  second "lines a light ever covers" rule.
- **A box's sides (D6)** are its hand's window at `max(onset, now)`, the strummed rule verbatim,
  for tapped boxes too.
- **Strike pops (D7)**: a single note pops its own two fret lines and a boxed strike pops its box's
  two sides, for both hands, in the hand's strike colour — amber for the fretting hand (unchanged),
  WHITE for the picking hand, single taps included. No derived right-hand spans: a tapped box is
  already derived per onset from the tap group, and a span object would be a second representation
  of the same fact, invisible on the 2D lane.
- **Tests may change.** This is a functionality change; right-hand test values that D3, D5 and D6
  move are re-pinned deliberately, never silently.

## 2. Design

### 2.1 Data: one stretch type

```cpp
// rock-hero-common/core/include/rock_hero/common/core/highway/highway_light.h  (new)
struct HighwayLitStretch          // WHEN one light is lit
{
    double start_seconds{0.0};    // full from here...
    double release_seconds{0.0};  // ...through here, then decays
    double rise_seconds{0.0};     // the rise ending at start_seconds, already crowding-clamped
    // operator== hand-written with std::is_eq per field (CI's -Wfloat-equal), as HighwayHandArrival's.
};
```

WHERE a light stands is not part of the stretch: it is a hand's track in the board's one motion
element, `std::vector<HighwayHandArrival>` read through `highwayHandWindowAt`, which both hands
already share.

Each hand is the same pair of lists — ONE track and the stretches lit over it:

```cpp
struct HighwayHandLight            // one per hand, the same shape for both
{
    std::vector<HighwayHandArrival> track;   // where the hand's window stands, over time
    std::vector<HighwayLitStretch> lit;      // when it is lit: disjoint, ascending
};
// HighwayViewState: HighwayHandLight fret_hand; HighwayHandLight pick_hand;
```

- **Fretting hand:** `track` is today's `HighwayViewState::fret_hand` moved into the pair.
- **Picking hand:** `track` is the tap onsets' paths concatenated in time order, with any arrival
  of an earlier onset that falls after the next onset's start dropped — the hand has moved on, the
  same statement the fretting window makes when it shifts while a note rings. (Today each onset
  carries its own path so overlapping taps at different frets keep two lights; under one track the
  later strike takes the window. Sighting item 2.) `HighwayTapOnsetViewState` then carries only
  the STRIKE's facts — `seconds`, `fret_low`, `fret_high`, `count` — for the box and the pop; its
  `path`, `release_seconds` and `rise_seconds` move into `pick_hand`. In Phase 1 (bit-identical)
  the onset still holds its own path and one composed `HighwayLitStretch light`; the track moves
  in Phase 2.

The render-side unit is `(const HighwayHandLight&, const HighwayLitStretch&, const
HandLightStyle&)`. An evidence item before merging is a `HighwayLitStretch` whose rise is not yet
clamped — no second type.

### 2.2 Functions: one authority per rule, all in core, all pure

```cpp
// highway_light.h / highway_light.cpp (new)
double highwayLightLevel(const HighwayLitStretch&, double seconds, double decay_seconds);
    // THE envelope: 0 before the rise, linear rise, 1 through the hold, linear decay after
    // release. Today's tapLightEnvelope, moved verbatim.
HighwayLitInterval highwayLitInterval(const HighwayLitStretch&, double decay_seconds);
    // THE lit interval [start - rise, release + decay]. Every skip test and range bound asks this;
    // today it is spelled by hand at about eight renderer sites.
HighwayLitStretch foldLitEvidence(std::span<const HighwayLitStretch>);
    // THE fold: start = earliest start, release = latest release, rise = widest margin among the
    // items within g_onset_match_epsilon of that start.
HighwayLitStretch crowdedAfter(HighwayLitStretch, const HighwayLitStretch& previous);
    // THE crowding clamp: a stretch never rises back past the previous stretch's release.
std::vector<HighwayLitStretch> mergeLitEvidence(std::vector<HighwayLitStretch>, double rest_seconds);
    // THE merge: sort by start; gather each run whose next start lies within rest_seconds of the
    // run's latest release; fold each run; crowd each after the one before. Result: disjoint,
    // ascending.
```

*Amended after Phase 1's simplicity pass:* once the merge lands, both producers call only
`mergeLitEvidence`, so `foldLitEvidence` and `crowdedAfter` become private steps of it in
`highway_light.cpp`'s anonymous namespace — the fold a single pass over a sorted run and the crowd
`min(rise, gap)` with the gap asserted non-negative, since the merge only splits at a gap of at
least the tolerance. Phase 1 keeps them public for the tap producer and their own spec tests.

Kept, one renamed: `memberReleaseAt` → `noteReleaseAt` (a note's release, not a chord member's;
file-local in `highway_projection.cpp`, where both producers live); `marginBefore`;
`highwayHandWindowAt`; `highwayHandWindowLineCoverage`.

### 2.3 The two producers (`highway_projection.cpp`) — the only place the hands differ

- **Margin rise per note.** The loop at `makeHighwayViewState` (today gated on `rightHandOnset`)
  computes one margin rise for EVERY note. One loop feeds both producers.
- **Picking hand — `makeHighwayTapOnsets` and `makePickHandLight`.** The onset producer groups
  the right-hand onsets struck together (the onset epsilon) and builds each group's path
  (`memberPositionAt`, unchanged). The light producer concatenates those paths into
  `pick_hand.track` (section 2.1), gathers one item per tapped member `{start = group onset,
  noteReleaseAt(member), margin_rise[member]}`, and runs the SAME `mergeLitEvidence` with the same
  tolerance. In Phase 1 the gather is folded and crowded per onset, which reproduces today's
  arithmetic exactly (every item starts at the onset); the merge across onsets arrives in Phase 2
  with the track. Today's per-strike pulse ("the light pulses with each strike while the
  brightened edges bridge the gaps") is the fallback if the merged light is rejected at sighting,
  reached by a per-hand tolerance of zero.
- **Fretting hand — `makeFretHandLight(const ChartViewState&, std::span<const double>
  margin_rise)`.** Evidence items:
  - every note with `!rightHandOnset(attack)`: `{start, noteReleaseAt(note), margin_rise[i]}`;
  - every right-hand onset with `note.held.value_or(0) > 0`, same extent — a tap lit through its
    claim. No authorship tier is needed: a DEFAULT held stop is above 0 only under a covering span,
    which is evidence anyway, and a bare tap's default is 0;
  - every span: `{start_seconds, drawn_end_seconds, 0}` — a span never OPENS a stretch (it starts
    at a note onset, which carries the rise, or tiles onto its predecessor as a carry-opened
    successor). Section 4 tests that claim; if it fails, the fix is a rise taken from the note at
    the span's start, never a silent 0.
  - Result: `mergeLitEvidence(items, g_hand_rest_seconds)`.

**The hands differ only in what a note proves.** The fold, the merge, the crowding clamp, the
envelope and the emission are stated once and run for both hands over the same shape. The picking
producer accepts right-hand onsets; the fretting producer accepts everything else plus claimed taps
and spans. Nothing else about a hand is a code path.

### 2.4 Emission: one path through every layer (`highway_renderer.cpp`)

```cpp
struct HandLightStyle { double warm_mix; ArgbColor strike_color; };
constexpr HandLightStyle g_fretting_hand_light{.warm_mix = 0.0, .strike_color = g_hit_glow_color};
constexpr HandLightStyle g_picking_hand_light{.warm_mix = g_tap_light_warm_mix, .strike_color = 0xFFFFFFFF};

template <typename Visit> void Impl::forEachLight(double from, double to, double decay, Visit&&) const;
    // visit(const HighwayHandLight&, const HighwayLitStretch&, const HandLightStyle&)
    // Two hands, one loop: the fretting hand first (keeps today's paint order), then the picking.
```

**THE brightness rule.** At any fret line and any instant, for any layer:

```text
brightness(line, t) = max over lights of  coverage(highwayHandWindowAt(path, t), line)
                                        × highwayLightLevel(stretch, t, layer_decay)
                                        × motionDim(path, t)
```

- **Floor** (`drawFloorLight`, replacing `drawHandWindowLight` and `drawTappingHandLight`): per
  visited light, today's tap-light sample list — the lit interval's ends, start, release, the
  arrivals inside the interval, and each leg's slices by `highwayGlideSliceCount` — with the alpha
  at each sample `highwayLightLevel × motionDim`, the extent `highwayHandWindowAt(path, t)`, the
  lane tint mixed by `style.warm_mix`, and the spill lane drawn on both sides so the soft band
  fades fully (D3).
- **Ribbons, bright tier:** the rule at `now` with the ribbon decay (0.45 s).
- **Ribbons, mid tier:** the rule along z — at each z-slice's time — with the floor decay. This
  replaces the `HandWindow` slices built per frame in `draw()`.
- **Fret-line active tier:** the rule at `now` with the floor decay.
- **Range bound:** one `LitIndex { release_prefix_max, max_rise_seconds }` per list, built once per
  chart, and one `litRange(...)`, generalizing `litTapOnsetRange` / `tap_end_prefix_max` /
  `max_tap_rise_seconds`.
- **Strike pops** (`drawStrikeGlow`): `boxSidesAt(path, onset, now) = highwayHandWindowAt(path,
  max(onset, now))` is the ONE function the box panel and the pop both ask (D6), so a gliding tapped
  chord's box and pop follow its light exactly as a strum's follow the window. Pops are collected
  per hand (two line-slot arrays) so a shared line max-resolves within a hand and the two hands'
  strips add. Colour per hand from `HandLightStyle::strike_color` (D7). The pop clamp is one rule —
  a pop clamps against the next pop landing on the same strips — stated once.
- **Constants renamed** for what they are: `g_floor_light_release_seconds` (a duration) →
  `g_floor_light_decay_seconds`; `g_tap_ribbon_decay_seconds` → `g_ribbon_decay_seconds`. The decay
  belongs to the LAYER, never to the hand.

### 2.5 Deleted

`drawHandWindowLight`, `drawTappingHandLight`, `tapLightEnvelope`, `tapPathLines`, `litTaps`,
`litTapOnsetRange`, `tap_end_prefix_max`, `max_tap_rise_seconds`, `HandWindow`,
`FrameScratch::hand_windows`, `FrameContext::hand_windows` and their build loop in `draw()`,
`WindowLightSlice`, `windowSampleTimes`, the tapped-box side computation in the panel code (now
`boxSidesAt`), the per-hand strike branches. Anonymous-namespace helpers that lose their last
caller MUST go in the same change: `-Wunused-function` fails CI.

## 3. Phases

Each phase builds, passes its suites, gets the simplicity-expert pass, and commits on its own.

### Phase 1 — pure refactor; the right hand's output is bit-identical

1. **Core.** Add `highway_light.h/.cpp` (`HighwayLitStretch`, `highwayLightLevel`,
   `highwayLitInterval`, `foldLitEvidence`, `crowdedAfter`) and `tests/test_highway_light.cpp` to
   the core source and test lists (a determinate reason to reconfigure). Rename `memberReleaseAt` →
   `noteReleaseAt`. `makeHighwayTapOnsets` gathers, folds and crowds through the new functions.
2. **Struct.** Compose `HighwayLitStretch light` into `HighwayTapOnsetViewState`; respell readers
   mechanically (three test initializers, ~20 test reads, values unchanged). Sweep designated
   initializers by SIBLING field: `rg '\.fret_low = '`, `rg '\.release_seconds = '`,
   `rg '\.rise_seconds = '`.
3. **Renderer.** `highwayLightLevel` for `tapLightEnvelope`; `highwayLitInterval` at every
   hand-spelled skip test (including the arrival numbers' `repeat_in_lit_run`); `LitIndex` +
   `litRange` for `litTapOnsetRange`; the constant renames.

**Proof.** (a) Spec tests pin the moved envelope with `WithinULP` / `is_eq` at the rise midpoint,
the start, the hold, the release, `release + decay/2` and `release + decay`; the fold, crowding and
interval functions get their own. (b) All 12 right-hand cases in `test_highway_projection.cpp`, the
camera cases and the renderer smoke sweep pass with unchanged asserted values. (c) Optional, never
committed: hash the tap-light pass's vertex bytes over 600 frames of the smoke "families" fixture
before and after; Phase 1 changes no sample time and no alpha arithmetic.

### Phase 2 — the fretting hand's evidence and the one light

1. **Core.** `HighwayHandLight` for both hands (`fret_hand` moves into its pair; `pick_hand` is
   built from the onset paths), `mergeLitEvidence`, `makeFretHandLight`, `makePickHandLight`, the
   ungated margin-rise loop, `g_hand_rest_seconds`. The renderer reads `pick_hand` for the tap light
   in this step so the tree stays green; the fretting light is still always on until step 2.
2. **Renderer floor.** `drawFloorLight` over `forEachLight`; the universal motion dim; the spill
   lanes; delete `drawHandWindowLight`, `drawTappingHandLight`, `WindowLightSlice`.
3. **Renderer tiers.** Ribbons and fret lines read the brightness rule; delete `HandWindow`,
   `hand_windows`, `tapPathLines`.
4. **Strike pops.** `boxSidesAt`, per-hand slots, per-hand colour, the one clamp rule.
5. **Docs.** `docs/developer/the-3d-highway.md` "The two floor lights" → "The floor light"; the
   backlog entry closed with the commit reference; `harmonic-display-followups.md`'s `tap_onsets`
   mention re-checked.

### Phase 3 — one sampling policy

`drawHandShapeRails` and the open tails sample the window through `highwayGlideSliceCount`;
`windowSampleTimes` is deleted. The fretting window's fast margin morphs get coarser (about 20
slices instead of about 81 over five frets in 0.05 s — a few frames of travel); sight the rails.

## 4. Tests

Core tests beside the tap cases in `test_highway_projection.cpp`; merge and envelope tests in
`test_highway_light.cpp`. Every double compare through `WithinULP`, `Approx` or `std::is_eq`.

- **Merge:** overlapping evidence is one stretch; evidence a gap shorter than the tolerance apart is
  one stretch; a gap of the tolerance or more yields two; the rise comes from the items at the
  start only; `crowdedAfter` clamps the second stretch's rise to the gap; input order does not
  change the result.
- **Evidence:** a lone open note is lit with a margin rise; a bare tap leaves `fret_hand_light`
  empty while `tap_onsets` still holds it; a tap with an authored held stop is lit; a tap under a
  covering span is lit; a pick slide without a claim is dark; a left tap and a natural harmonic
  are lit; a drawn slide-out releases at the last pitched keyframe and an undrawn one at the ink
  end; chord heads with emptied tails under a span stay lit to `drawn_end_seconds`.
- **A span never opens a stretch:** carry-opened successors at a landing and at a member's death.
- **Picking hand under the same rule:** a dense tap run inside the tolerance is ONE stretch over
  one track; two tap onsets a rest apart are two; the track leaves an earlier onset's position at
  the later onset's start when the two overlap.
- **The `9731dcee` discriminator:** chords at measures 1 and 3 under spans with measure 2 empty
  (longer than the tolerance) give TWO stretches; the second starts at the returning chord with a
  one-margin rise and never reaches back through the rest. The old failure mechanism (a running
  `lit_until` folded before the silence was measured) cannot recur in a sort-gather-fold, but the
  test guards any future implementation that extends first. Compute the expected seconds from the
  fixture, not from the old literals.
- **Renderer smoke:** `CHECK_FALSE(families.fret_hand_light.empty())`, plus a no-placements frame
  (the nut window lights under evidence).
- **Right hand:** the Phase 1 invariants stay green through Phase 1; the D3 / D5 / D6 changes in
  Phase 2 each get a test and a deliberate re-pin.

## 5. Runtime

- **Merge: once per chart revision**, in `makeHighwayViewState` (the derive-once site). One pass
  over notes and spans (both ascending), one sort of the concatenation, one sweep. Never per frame.
- **Range index: once per chart load**, beside `sustain_prefix_max`.
- **Per frame:** four layers × two binary searches per hand, visiting only the lights that reach
  the span, no allocation (the floor reuses `scratch.window_times`). Worst case, a span-less
  16th-note chug riff, is ~1000 quads over a 3 s span — the order of today's tap light in a dense
  tap run. Submit one batch per hand as the two passes do today (batches are uint16-indexed).
  **Measure** on a dense chug chart and a tapping chart through
  `.agents/rockhero-build.ps1 -Preset relwithdebinfo` and report the frame time.
- Audio thread untouched.

## 6. CI blind spots this change touches

`HighwayLitStretch::operator==` hand-written with `std::is_eq` (its floats are its own); every
`HighwayTapOnsetViewState` initializer changes shape — sweep by sibling field; `note.held.value_or(0)`,
never `*note.held`; deleted anonymous-namespace helpers; `-Wshadow` in the new tests (no lambda
parameter named like an enclosing local); the merge's stretch count uses branches, not `+=`;
`mergeLitEvidence` takes its vector by value on purpose (it sorts) and returns it by value.

## 7. Sightings the build owes

1. The rest tolerance's value (`g_hand_rest_seconds`), on a chug riff, a legato run, and a
   phrase with a real rest.
2. The picking hand under the same tolerance: a dense tap run as one travelling light instead of
   a pulse per strike, and overlapping taps at different frets under one track. Rejecting either
   is the per-hand tolerance of zero (section 1), never a branch.
3. The tapped chord's sides after D3 and D6, against a strummed chord over the same frets.
4. The universal motion dim on a tapped glide.
5. An open-string tap harmonic's box and pop at its node (the only harmonic tap validation still
   allows).
6. The rails after Phase 3.
7. Beat-bar wings, rails and floor footprints still clip to the window when it is dark; not
   covered by any ruling — if they read as floating, that is a new item.
