# 2D Bend Display Study — how bend and bar keyframes draw in a 15 px lane

Status: **PROPOSED 2026-08-26, whammy scope RULED 2026-08-26 (see below).** This is the study
record assembled for a user ruling: the constraint arithmetic, the precedent survey, the
candidate trail with its kills, one recommended display language with its traded-away constraints
named, the whammy forward-compatibility statement, the `B` verb, the 3D-parity obligation, and
the sighting checklist. The bend-clause recommendations await the user's eyes on the rendered
sheets; the measurements are facts, the judgments are not.

## Ruling 2026-08-26 — the whammy clause is provisional by design

The user ruled on the bar-channel scope: settle a sensible baseline now, and **when whammy
support is actually added, a real, in-detail analysis must be re-run to determine what looks
right** — nothing about the bar's drawn look is signed today. `whammy-bar-support.md` (or the
plan that lands the bar channel) must treat that analysis as a blocking prerequisite, not a
formality: re-bake the collision figures against the then-current lane geometry and chip fonts,
and re-litigate the rail treatment with eyes, not carried assumptions.

What that splits this document into:

- **Stands now, sighting-gated:** survivor 1 — the bend clause (§6's rules R1–R7 and R9 as they
  apply to the bend channel). It needs no law change and is the operative input to the unified
  keyframe model's editor stage.
- **Stands now, cost-free:** the five forward-compatibility reservations (§7). They reserve axis
  room, the zero-line convention, and mark vocabulary; they draw nothing today and are what makes
  the future bar extension possible without a redesign. Reserving is not designing.
- **Demoted to starting hypotheses for the future analysis:** survivor 2's specifics — the
  1.0 px/st down-scale, the rail-gap crossing treatment (R10), the quiet-lean dive fill (R8), the
  deliberate downward H1 break, and the 2D-metric/3D-saturating parity redefinition. The blocking
  JUCE text-width measurement that gated signing the down-scale moves into that analysis with
  them. The KINK/COLLISION sheet rows become evidence for that future study, not items on
  today's sighting checklist.

This restarts `2d-bend-waypoint-redesign.md` (PARKED 2026-08-05 — "I don't really like any of our
options for this right now") on the substrate decided in `unified-waypoint-model.md`, which
removed storage from this study's scope: keyframes are per-channel-optional statements
`{offset, fret?, bend?, vibrato?}`, the bend channel interpolates between bend-stating keyframes,
fret-less keyframes exist, and the generalized dissolve law governs editing. **Whammy storage is
explicitly NOT designed here** (`whammy-bar-support.md` owns that); what is designed here is a
display language that must visibly extend to bar work when it lands.

Measurements were re-derived 2026-08-26 from `rock-hero-common/ui/src/tab/tab_lane_layout.cpp`,
`rock-hero-common/ui/src/tab/tab_paint_core.cpp`,
`rock-hero-common/ui/include/rock_hero/common/ui/tab/tab_lane_layout.h`,
`rock-hero-editor/ui/src/timeline/track_viewport.h`, and
`rock-hero-common/core/include/rock_hero/common/core/highway/highway_metrics.h` /
`highway_tail.h`, and re-checked against the source while writing this record.

## Sighting artifacts

Rendered sheets and bakes live outside the repository, in
`C:/__MAIN__/Coding/__scratch__/rockhero-perf/bend-study/`:

- `master_sheet_1x.png` (2576×925) — the wide round: 7 candidates × 5 figures at true 1x.
- `master_sheet_4x.png` (2598×1670) — 4x NEAREST crops of the bend figure and the whammy figure.
- `survivor_sheet_1x.png` (1710×1646) — **the sheet to sight**: 10 figure rows including today's
  baselines, with the measurement table burned into the bottom margin.
- `survivor_sheet_4x.png` (2744×2196) — 20 NEAREST crops of every prove-or-break zone.
- `bend_candidates_bake.py`, `survivor_final_bake.py` — the bakes, with the shipped geometry
  asserted inside (they hard-fail if the lane geometry drifts).
- `candidate_measurements.json`, `survivor_measurements.json` — every number below, machine
  readable; `geometry-fact-sheet.md` and `derive_geometry_facts.py` hold the band derivation.

## 1. The problem, in the user's words

> "There is not much space in 2D to make it look right and we need to also consider how we may
> make it look okay in 2D once whammy bar support eventually lands as well. There seems to be no
> good solution so I need a deep analysis to figure this one out."

And earlier, the worry that opened the file:

> "a new FULL SIZED waypoint at every change to a bend curve could look cluttered."

The framing sets the admission test this study accepted: **a candidate that cannot say how it
will draw a dive-and-return in the same band is incomplete.** Dives are downward pitch
excursions, often far larger than any bend (a full dive exceeds an octave), joined by returns,
scoops, dips, and flutters — and a bar event can overlap a fretted bend, so the pitch-offset axis
eventually becomes bipolar and asymmetric.

## 2. The constraint map

### 2.1 The band, measured

All at the shipped reference density (editor: 237 px / 6 strings = 39.5 px lane pitch;
`note_height` capped at 25 px by `TabLaneStyle::max_note_height`). The cap is what makes these
constants of the *design* rather than of the window — a bigger monitor does not improve one of
them.

| Quantity | Value | Derivation |
|---|---|---|
| Lane pitch (string row) | 39.5 px | 237/6; `tabLaneCenterY` |
| Note head | 26 px | `headSize() = note_height + 1` |
| Tail height | 19 px | `odd(note_height × 3/4)` |
| Tail outer envelope | 15 px | `tailSpan` half = round((19/3 + 1)×2)/2 = 7.5 |
| Tail rails | 2 px each | `tail_edge_size = round(19/8)` |
| **Tail interior (the technique band)** | **11 px** | envelope − 2 rails |
| Technique stroke | 2 px | `g_technique_line_thickness` |
| **Stroke-center travel for 3 whole steps** | **9 px** | `rest_y − full_y` = 11 − 2 |
| One whole step | 3 px | 9 / 3 |
| One half step | 1.5 px | |
| One quarter-tone (the universal quantum) | **0.75 px — sub-pixel** | |
| The 96.6%-of-corpus bend (≤ 1 whole step) | ≤ 3 px of rise | corpus scan 2026-08-05 |
| Vibrato sine | amplitude ±4.31 px, stroke 2.375 px, period 19 px | `drawVibratoSine` |
| Bend chip font | 10 px — `max(10, note_height/4)`, below the 12 px must-read floor | |
| Text cutoff | `note_height < 9` ⇒ `draw_text` false; all chips vanish | |
| Free inter-lane margin | 24.5 px between envelopes = **12.25 px per side** | 39.5 − 15 |
| Free margin beyond a head | 6.75 px per side, shared with the neighbor | (39.5 − 26)/2 |
| Worst chip poke today | 20.0 px above center — 0.25 px past the own half-lane (19.75) | bake |
| Zoom | horizontal only: 316 px/s default, 1264 px/s max | `track_viewport.h` |

One sentence: **the entire pitch-excursion axis is 9 px, its quantum is sub-pixel, the common
case uses a third of it, and no zoom, window size, or monitor buys a single additional vertical
pixel.**

### 2.2 Hard constraints

**Geometry.**

- **H1 — The 11 px interior is the only sanctioned home for technique marks.** `tailInterior` is
  "the one definition of where a technique mark may live"; the sine and the bend polyline
  compress into it rather than clip. A mark leaking past the rails "reads as leaking out of the
  sustain" (`drawSlideLines` comment).
- **H2 — Vertical resolution is fixed forever.** Zoom is x-only; `max_note_height` caps the band
  regardless of window height. Zooming — the charter's universal "let me see better" verb — buys
  nothing on the one starved axis.
- **H3 — The inter-lane margin is not free.** Bend chips and slide chips already colonize it from
  *both* adjacent strings, heads reach within 6.75 px of the lane boundary, and the worst chip
  already pokes 0.25 px past its own half-lane. Published tab's "put bend ink above the staff"
  escape does not exist between abutting lanes.
- **H4 — Text degrades to nothing.** Below 9 px note height there are no chips at all, so any
  candidate whose magnitude lives only in text has a state where magnitude is silently
  unreadable.

**Signed decisions** (violating one is not a design choice; it is reopening a ruling).

- **H5 — Zero at the tail-box floor; up is bend** (user, 2026-08-04).
- **H6 — Fixed linear scale, 3 whole steps at the ceiling, no compression, no clamping inside the
  range, no rescale under a drag, heights comparable across notes** (user, 2026-08-05).
- **H7 — Bend values ≥ 0** (W9-K, ratified 2026-08-25, explicitly "the whammy-channel
  pre-answer"): bar work arrives as a *separate* channel, so the display must eventually compose
  two pitch functions that superpose physically.
- **H8 — Thin-line color-typing is dead** (Szafir, IEEE TVCG 2017: 14–19.5 ΔE needed at 1–2 px
  for even 50% discrimination). Slide, bend, and any future bar line share `Ink::TechniqueLine`
  white; distinctions ride shape and structure. Color is trustworthy only at token width.
- **H9 — W9-D: every pitched keyframe draws its own head, sized to the tail** (~15 px, down from
  today's 26 px linked heads). W9-F (pitched-vs-falls-away glyph) is open and will add junction
  ink.
- **H10 — One bend-shape authority**: `highwayBendSemitonesAt` (monotone cubic Hermite,
  `highway_tail.h`); 2D hoists a shared evaluator and never re-derives.

**Vocabulary and theme.**

- **H11 — Colors are `EditorTheme`/`Ink` roles**; a needed color with no role is a finding, not a
  hex. `EditorTheme::invalid` red is reserved for pending/rejected input and was chosen so
  luminance alone carries it — a 2 px vertex physically cannot carry that signal, only a token
  can.
- **H12 — 2D quiets by opaque lean toward `0x101010`**; ghosts are 0.5-opacity flattened note
  groups, legible at head size and invisible at line weight.

**Interaction** (the redesign's own signed machinery).

- **H13 — Keyframes become selection citizens**: hit targets ≥ 24 px or ≥ 24 px spacing
  (WCAG 2.5.8), hover-revealed padding, per-point verbs, retype-by-digit through the W3 pending
  model. Vertices on the bend scale can sit 1.5 px apart vertically.
- **H14 — The generalized dissolve law**: a pending point must be visibly *pending* for the life
  of a gesture, then vanish without residue; it commits iff it changes the path function or the
  state.
- **H15 — Chip-per-point does not scale.** Today every bend point pushes a ~10 px chip, and the
  keyframe model multiplies point kinds. Real data bounds this (74% one point, 20% two) but
  composite gestures are exactly where the design is judged.

**Parity.**

- **H16 — Surfaces must not diverge** in what they *say*. 3D's bend reading is metric (half step
  = one string gap; ceiling ≈ 2.86 gaps, `highwayBendLiftY`). 2D-only charting marks are
  precedented (LeftTap's light T), so selection affordances may stay 2D — but the pitch story
  must be tellable on both.
- **H17 — The 3D floor is the origin and nothing draws beneath it** (`highwayBentNoteY`
  saturates). The lift law accepts negative offsets, but a dive from a low lane has under one gap
  of drawable depth. **The bipolar problem is not a 2D problem; 3D has its own hard floor.**
- **H18 — The future axis is bipolar and asymmetric**: up ceiling +6 semitones, down excursions
  routinely past −12. Linear symmetric treatment needs 27 px of travel in a 15 px envelope —
  arithmetically impossible, before taste enters.

### 2.3 Conflict pairs — why every obvious design violates at least one

- **C1 — Metric fidelity (H6) × the band (H1/H2).** A linear uncompressed 0–3-step scale in 11 px
  puts the quantum below one pixel and the common bend at ≤ 3 px. Today's design already
  sacrifices glanceable magnitude to the chips. "Make the curve taller" is barred by H1;
  "auto-fit per note" by H6.
- **C2 — Zero-at-floor (H5) × the bipolar future (H18).** The signed zero anchor leaves zero
  downward pixels; mid-interior zero halves the up-scale to 0.75 px/half-step and reopens H5;
  compressing only the down side makes a −12 dive draw shallower than a +2 bend.
- **C3 — Selectability (H13) × clutter × occlusion.** Selectable points want size; the band is
  15 px; and a head at a vertex covers the most informative pixels on the curve — the direction
  change it anchors.
- **C4 — One white line (H8) × the merged keyframe list.** Slides and bends now share one point
  list and produce one composite pitch function; two crossing 2 px white lines in 11 px are
  undifferentiable.
- **C5 — Chips as the only magnitude channel × density (H15) × margin (H3) × degradation (H4).**
  The one channel that says "how much" collides horizontally at fast gestures, leaks into
  contested margin, and vanishes below the text cutoff.
- **C6 — Vibrato occupancy × bend occupancy.** The sine's amplitude *is* the interior; bend +
  vibrato is the common composite (5× bend + slide); today they overprint incoherently.
- **C7 — Pending visibility (H14) × salience order.** 0.5 opacity distinguishes a pending 26 px
  head; it cannot distinguish a pending vertex on a 2 px line — and making pending points
  brighter inverts the salience order the dissolve law implies.
- **C8 — Dive depth (H18) × the 3D floor (H17) × parity (H16).** "Mirror the lift downward" dies
  at the 3D floor from any low lane, so a metric dive is impossible on *both* surfaces — pushing
  every honest candidate toward a symbolic dive language that must then coexist with a metric
  bend language.
- **C9 — Zoom relief (H2) × the vertical problem.** Every horizontal cure exists; no vertical
  cure does. A candidate that "solves" clutter by assuming high zoom has not solved the starved
  axis.
- **C10 — Comparability (H6) × the DAW answer.** Auto-fit lanes, resizable lanes, per-note
  scaling — the entire DAW toolkit for cramped automation — is signed away, for the good reason
  that a drag which rescales its own reference is unusable.
- **C11 — Fret-less keyframes × the head vocabulary.** A bend-only anchor has no fret to state; a
  numbered head would lie. The new object needs a token that says "anchor, not stop", in 15 px,
  distinguishable from heads, ghosts, and selection states.

**How the map is used:** a candidate is scored by walking C1–C11 plus one mandatory scenario — a
dive-and-return overlapping a fretted bend, drawn in this band, and its 3D telling — recording
which constraint it pays and at what measured cost.

## 3. Precedent — what every other system does when the axis runs out

### 3.1 The four surveys, compressed

**Standard published tablature.** Topology and direction only, as a fixed symbol family: bend =
upward-curving arrow, release = downward arrow, pre-bend = vertical rise, dive-and-return = a
large **V** with the depth number at the vertex, scoop = check-mark before the note, flutter =
horizontal wave plus text, spans bracketed by "w/ bar". Magnitude is entirely textual — "½",
"full", "1½" — and **position is never proportional to pitch**: a full bend and a half bend draw
the same arrow. Vertical crowding never arises because all bend and bar ink lives *outside* the
staff, a margin abutting lanes do not have (H3).

**Guitar Pro.** The Bend window and the Tremolo Bar window are the same point-list editor with a
bipolar axis: x = duration in fixed subdivisions, y in quarter-tones, draggable points, preset
gallery. Magnitude is fully metric *because the surface is huge* — hundreds of pixels for the
range we give 9. In the score itself GP does not attempt the metric curve: bends collapse to the
paper vocabulary, and the bar renders as straight angled segments with signed numbers at each
direction change. **GP solved the cramped-lane problem by not solving it** — two representations,
metric on a big on-demand surface, symbolic in the lane. Our premise (keyframes in the lane, no
separate bend editor) deliberately removes that escape hatch; that is the accepted cost this
study exists to pay, and no surveyed product pays it.

**Whammy/bar conventions.** V-with-number, check-mark scoop, wave-plus-text flutter and "w/ bar"
span text are stable across legends. The one metric nuance in the genre is *straight line =
constant rate, curved arc = varying rate* — slope encodes speed, never absolute depth. **Depth
beyond the staff is never drawn to scale, anywhere**; only the number changes between a dip and
an octave bomb. Scoops use pre-onset x-space, a horizontal zone bends never touch and our lane
leaves empty. **Bar-over-fretted-bend has no established convention** — the Dorico forum thread
is engravers improvising, so whatever we choose there we invent: a freedom (nothing learned to
break) and a risk (nothing to lean on).

**DAW pitch-automation lanes.** Curve shape at a glance in a dedicated lane with a drawn zero
line; exact magnitude on demand via hover/drag tooltip; small square points that grow on hover;
unselected content dims. MIDI pitch-bend lanes are center-zero symmetric, and where the musical
range is asymmetric the lane stays geometrically symmetric while the asymmetry lives in the
labels. When space runs out, lanes resize, collapse, or — the closest analog to our tail-interior
curve — **Cubase Note Expression draws the envelope inside the note rectangle when small and
opens a transient magnified editor anchored to the note on demand**. The industry's answer to
"too small to edit in" is a temporary magnified view of the *same objects*, never a different
notation. **Melodyne** is the outlier worth naming: pitch drift draws as a thin curve against the
semitone row grid itself — which is exactly the choice our 3D highway already made (half step =
one string gap).

Sources: Total Guitarist whammy-bar tab notation; Guitar Instructor tab notation legend (PDF);
Semiosis Music Publishing whammy semantics; Wikipedia "Dive bomb (guitar technique)"; Steinberg
forums "notating whammy bar dip on chord"; Guitar Pro 8 user guide and its quarter-tone
tremolo-bar feature page; Guitar Chalk advanced tab symbols.

### 3.2 Transferable ideas

1. **Split glance from demand:** topology by shape, magnitude by text at semantic anchors only —
   peaks and direction changes, never every point. `charterBendText` already speaks the
   vocabulary.
2. **When a metric axis runs out of pixels, every surveyed system goes symbolic, not compressed.**
   Nobody draws a to-scale dive. A small fixed symbol family costs zero vertical pixels, and
   direction-and-story marks are exactly what H8 says thin marks *can* carry.
3. **Keep zero visibly anchored; never center-zero a 96.6%-unipolar axis.** Precedent handles
   asymmetry in labels and scale, not proportional geometry.
4. **The transient magnified view anchored to the object** (Cubase's bubble) is the honest escape
   from C1 — provided it magnifies the *same* in-lane keyframes and acquires no verbs of its own.
5. **Pitch can share the position axis** (Melodyne rows; our own 3D gap law): the string grid is
   the one metric pitch ruler both surfaces already own.

### 3.3 Traps

1. **The metric-lane trap:** no surveyed system draws position-proportional pitch in under ~40
   px; attempting it in 9 is the root cause the 2026-08-05 sheets kept re-hitting.
2. **The center-zero trap:** halves bend resolution the day the bar channel lands (C2).
3. **The chip-per-point trap:** labeling every vertex; engraving labels semantic anchors only.
4. **The vertex-occlusion trap:** tokens sitting on the curve hide the curvature they anchor.
5. **The two-vocabularies drift trap:** if 2D goes symbolic while 3D stays metric, parity must be
   *explicitly* redefined as same topology + same numbers, or the surfaces silently diverge in
   what they claim (H16).
6. **The hidden-editor trap:** any on-demand magnified surface that acquires its own objects or
   verbs has reinvented the modal bend window the keyframe decision exists to kill.

## 4. The candidate trail

### 4.1 What was rendered

Seven candidates × five figures at true 1x on `master_sheet_1x.png`, with 4x crops of the bend
figure and the whammy figure on `master_sheet_4x.png`. A–D were the specified set, E–G the
assumption-breakers:

- **A** — a full-size head at every point (the user's stated fear, rendered as the baseline).
- **B** — 5 px dots on the curve, 15 px mini-heads at fret-stating points.
- **C** — bare curve, hover-only 24 px handles.
- **D** — peak-only chips (a labeling policy, not a token scheme).
- **E** — symbolic: fixed wedges for bends, print-convention V for dives; magnitude by number.
- **F** — bipolar overflow: up unchanged in-band, down below the bottom rail into the margin.
- **G** — thermometer fill: magnitude by filled area rather than line position.

Shared measured facts across every metric candidate: half-vs-full-step separation is **1.5 px
under a 2 px stroke** (the footprints overlap, so geometry alone never discriminates the corpus's
common magnitudes); the corpus-common bend rise is 3.0 px; the crowded figure spaces points
12.6 px apart.

| Cand | Budget, bend / whammy | Smallest discriminable at 1x | Chips / collisions / worst |
|---|---|---|---|
| A | 26 px / 26 px | head digit 12 px; curve occluded ±13 px per vertex | 12 / 6 / 8.4 px |
| B | 15 / 15 | 5 px dot (sub-target; H13 wants 24) | 12 / 6 / 8.4 px |
| C | 15 (+24 px transient ring) / 15 | 1.5 px step separation — sub-stroke | 12 / 6 / 8.4 px |
| D | 15 / 15 | as C | 6 / 0 / 0 |
| E | 15 / 35 | 10 px number; geometry carries direction only | 6 / 0 / 0 |
| F | 15 / 29.5 | up 1.5 px/st; down 1.0 px/st → −12 dive = 12 px | 6 / 0 / 0 |
| G | 15 / 29.5 | visible fill ≈ displacement − stroke ≈ **1 px** at a common bend | 6 / 0 / 0 |

Two map-falsification checks both **stood**: the 3 px common-bend rise does not read as a pitch
contour at 1x without its chip (C1), and per-point chips 12.6 px apart overlap by 8.4 px (C5).

### 4.2 The kill list

- **A — full heads at every point. KILLED by C3, C11, and W9-D itself.** At 12.6 px point
  spacing, 26 px heads overlap by 13.4 px — an unreadable disc train — and each head occludes
  ±13 px of curve centered on the direction change it anchors. The fret-less mid-slide point
  wears the digit "7", which is false (the sounding position is between 7 and 9): a mark stating
  a fact that is not the fact being stated is the visual form of the no-code-that-lies rule.
  Independently, W9-D signs ~15 px heads, so 26 px is not a legal reading of the ruling A
  renders. **The user's clutter fear is now a number, and the number kills.**
- **E — symbolic wedges and V. KILLED by H6 (signed).** E draws a ½-step and a 3-step bend with
  identical geometry, abandoning comparability wholesale. No evidence in this round forces the
  reopen, because F obtains a metric, glanceable dive *without* touching H5/H6/H7. Labeled per
  the broken-vs-unfamiliar test: E is not broken, it is a deliberate abandonment of a signed
  decision — killed on authority, not taste, and the user can reopen it if the sighting demands.
- **G standalone — thermometer fill. KILLED by C1 arithmetic.** Visible fill at the common 3 px
  rise is about 1 px; the 2 px stroke eats it. It discriminates nothing until ~2 whole steps,
  i.e. the 0.1% of bends.
- **B standalone — dots plus mini-heads. KILLED by C5.** It keeps chip-per-point: 12 chips, 6
  colliding pairs, 8.4 px worst overlap.
- **C standalone — bare curve, hover-only handles. KILLED by C1 + C5 + H4 at rest.** Rest-state
  magnitude rides colliding chips, and below the text cutoff the lane says nothing about
  magnitude, silently. Invisible handles also carry a discoverability risk (a missing signifier,
  not a missing affordance).
- **D — not killed, and not a candidate.** It is a labeling *policy* — the engraving convention —
  and it is absorbed whole. Its measured result is the round's cheapest real win.

### 4.3 What the dead donated

Nothing was discarded without harvest, which is why the survivors are one language rather than
one winner:

- **B donates the token scheme**: the 5 px dot is the only honest C11 answer on the sheets (a dot
  cannot lie about a fret), and its 15 px mini-head at fret-stating points is simply W9-D drawn
  correctly.
- **C donates the hover-revealed 24 px padding ring** — how H13 sanctions sub-target tokens. Once
  B's always-visible dots mark the handle positions, C's discoverability objection dissolves: the
  dot is the signifier, the ring is the revealed padding.
- **D donates the peak-only chip policy** (12 chips / 6 collisions → 6 / 0 measured).
- **E donates the glance asset** — "a bend happened, upward" — harvested as a fixed-size onset
  presence mark, which H6 does not govern because H6 governs the curve's scale, not a presence
  cue.
- **G donates the dive-lobe fill**, where excursions are finally large enough for area to work.
- **F survives as the bar clause** of the proposed language.

## 5. PROPOSED display language — "one centerline, chips say how much, tokens say what"

### 5.1 The rules

Ten renderer rules. Each is a proposal; none is ruled.

- **R1 — One composite pitch centerline.** A single 2 px `Ink::TechniqueLine` polyline per note,
  equal to the fret ramp composed with the bend channel through the one shared evaluator
  (`highwayBendSemitonesAt` hoisted; 3D's `makeHighwayTailSampleTimes` shape). Slide and bend are
  never drawn as separate lines. Satisfies C4 and H10 by construction — one authority stated
  once, per the recurring-defect rule.
- **R2 — The scale.** Up: 1.5 px/semitone, zero at the bottom rail, values ≥ 0 — H5/H6/H7
  untouched. Down (reserved for the bar channel): **1.0 px/semitone** into the inter-lane margin.
  The figure is chosen by collision arithmetic, not taste: at 1.5 the dive chip overprints the
  neighbor's chip by 6.0 × 3.5 px; at 1.0 the chips clear by 2.5 px; at 0.75 they clear by 5.5 px
  but a −4 dip renders at 3 px, indistinguishable from a −2. Asymmetric up/down scales are
  sanctioned by precedent (asymmetry lives in scale and labels, never center-zero geometry) and
  by H7's two-channel structure; they do not touch H6, which governs the bend channel only.
  **Carried condition: the 2.5 px clearance sits inside font uncertainty — the chip widths were
  measured against a Verdana/Meiryo 10 px raster, not the JUCE typeface. One JUCE text-width
  measurement is a blocking prerequisite to signing 1.0.**
- **R3 — Token vocabulary.** 15.6 px mini-heads at fret-stating keyframes (W9-D as signed); 5 px
  dots at fret-less/bend-only points, because a dot cannot state a false fret (C11); a
  hover-revealed 24 px padding ring on every token (H13). Below full density the dot scales with
  tail height (5 → ~3 px) — a fixed 5 px dot is 1.67× the entire 3 px cutoff band.
- **R4 — Chips at non-zero extremes only.** Peaks and non-zero direction-change extremes chip;
  zero valleys are chipless because the rail *is* the drawn zero (H5). Measured payoff: the
  crowded figure goes from 12 chips / 6 colliding pairs to 6 / 0, and saturation moves out to
  note spacing below 22 px (~70 ms at default zoom). On dive lobes the chip sits beside the
  vertex, not under it — under-vertex placement clips a sustaining lower neighbor by ~1.5 px.
- **R5 — Onset presence chevron.** A fixed-size direction glyph at the head carrying existence
  and direction only. At full density it sits above the head (apex 18.5 px above center, inside
  the 19.75 px half-lane). At the text cutoff it moves **inside the empty head interior** — H4
  has dropped the digit, so the slot is free — because above-head clears the neighbor envelope by
  only 0.25 px, i.e. touches after antialiasing.
- **R6 — Vibrato during bend: the sine draws only at zero bend.** At nonzero bend the curve owns
  the interior and vibrato is stated by a 12 × 5 px token. Measured reasons: riding the raised
  centerline gives 2.81 px amplitude (65%) at one step and **zero at three steps — degenerate**;
  clipping instead of compressing scallops 27–50% of columns, which reads as a raster fault at
  4x. A rule with a degenerate state at legal input is a wrong rule. This resolves C6 by
  statement rather than geometry, and is part of the parity redefinition in §8.
- **R7 — Pending and invalid.** A pending point is dot + ring + *quieted* chip (measured text
  luma 146 vs 255, chip background 38 vs 60 — the project's quiet vocabulary at chip size, where
  it is legible). An invalid pending value turns the chip text `EditorTheme::invalid` red and is
  **display-only: it never deforms the curve** — the polyline rides the channel's interpolated
  value until settle. The line and the dot never need pending variants of their own (C7).
- **R8 — Quiet-lean fill on dive lobes only.** The existing tail-fill role at quiet lean (Δluma
  11.5 against the lane; no new color role, H11). Measured to read only at ≥ 2 steps — kept
  anyway, because dives *are* the large excursions; dropped if it sights as a selection state.
- **R9 — Ramp headroom rule.** The slide ramp's rise shrinks by the note's maximum held-bend
  deflection, so the composite never leaves the interior. Without it the composite ends 0.46 px
  above the interior top and leaks into the top rail over its last 11 columns. H1 stays intact on
  the top side: the up direction has no truth-telling reason to leak.
- **R10 — Rail-gap crossing.** Where the composite crosses the bottom rail (bar channel only),
  the rail opens over the stroke∩rail span plus a 2 px margin. A literal "1 px gap" is
  geometrically impossible — the spans measure 38 px. The gap makes zero-crossing a drawn event
  and interrupts the line exactly at the two-slope kink (3.3° at down 1.0), mooting the
  kink-as-fake-vertex misread. Subject to the sighting verdict in §9.

### 5.2 The three trades, stated plainly

The user is right that no clean solution exists. The language still pays three constraints; each
is proposed as the cheapest payable, and none is hidden:

- **C1 is not solved; it is re-homed.** Glanceable magnitude rides chips, not geometry, for 96.6%
  of bends: the smallest geometry-discriminable rise is 1.33 steps, and at the text cutoff
  magnitude is unreadable — the chevron rescues existence and direction only. This is least
  costly because every alternative violates something worse: a taller curve breaks H1, auto-fit
  breaks H6 (signed), symbolic wedges break H6 (signed), and the metric-lane trap is arithmetic
  rather than taste. The cutoff density is a navigation state, not an editing state; editing
  happens where chips exist. The escape hatch — a Cubase-shape transient magnified view of the
  *same* keyframes, no new verbs — stays on the shelf and graduates to a requirement only if
  sighting item 1 fails.
- **H1 is broken deliberately, downward only.** A dive is pitch leaving the fretted register, so
  the leak states the truth; R10 gates it. Whether it *reads* as statement or error is the
  user's sighting call, and it is refused cheaply now or expensively later.
- **H16 is redefined, not violated** — §8.

Also recorded rather than buried: at down 1.0 the common −4 dip bottoms about 2 px below the
envelope and largely hides inside the rail opening, so common bar dips are chip-carried and only
large excursions earn metric geometry. That is the engraving world's allocation, arrived at by
measurement instead of by convention.

## 6. PROPOSED whammy forward-compatibility — five reservations

The bar channel lands as an extension rather than a redesign **iff** these hold from today. None
of them designs storage; all of them are display reservations.

1. **The down direction below the bottom rail belongs to the bar channel, permanently.** No
   future feature may place chips, badges, or marks in the 12.25 px per-side sub-envelope margin.
   The bend channel never draws there — H7 guarantees it.
2. **The bottom rail is the zero line, permanently** (H5), and R10's rail gap is the reserved
   zero-crossing mark. Never center-zero the axis: it halves resolution for a 96.6%-unipolar
   channel.
3. **Down scale reserved at 1.0 px/semitone** (pending the chip-width verification). A
   dive-and-return draws metrically to −12 semitones: 12 px of deflection, line bottom 17.5 px
   below center, clearing the neighbor envelope (32.0 px below center) by 14.5 px, with the
   beside-vertex chip clearing it by 9.0 px. **Beyond −12 (a slack floating bridge) the drawn
   line saturates at the −12 depth and the signed chip carries the number** — the same
   saturate-and-let-the-number-speak rule 3D's floor already imposes, so both surfaces share one
   saturation law.
4. **Mark vocabulary reserved:** signed chips (the minus glyph) belong exclusively to the bar
   channel — bend chips stay unsigned under H7; the downward chevron is the bar's onset mark,
   mirroring the bend's upward one; the pre-onset x-zone before the head, which bends never
   touch, is reserved for scoop ink; `W` stays reserved in the keymap for the signed-value
   window. Flutters join later as a fixed-size symbol in the token register and need no axis
   room, so nothing is reserved for them.
5. **Bar-over-fretted-bend needs no new drawing rule:** the channels superpose into the same
   composite centerline (R1) and per-channel values ride chips at extremes. The mandatory
   scenario is already rendered under exactly these rules (the kink and collision rows). There is
   no established convention to break here, so this is the unfamiliar-not-broken case, and
   superposition is the physically true reading.

The only storage-adjacent flag this study carries: whether the format constrains bend to
quarter-tone multiples, or only entry validates it.

## 7. PROPOSED `B` verb

Grammar constraints honored: the technique-letter map (`B` reserved for bend), the Shift-plane
statement (Shift names a letter's second claimant, never a semantic operator), the digits/insert
grammar (bare digits at a caret or selection mean **fret** — W10 ruling 2, W3's multi-digit
pending window, red when invalid), the uniform-scope law, and the generalized dissolve law.

**The core resolution.** Bare digits already belong to the fret channel everywhere, so bend
magnitude cannot ride bare digits: `B` is the channel prefix. **`B` arms a bend-value window, and
the digits that follow write the bend channel of exactly the selected object(s).** One grammar
per channel — digit → fret-stating, `B` + digits → bend-stating — and when the bar channel lands,
`W` mirrors the shape with a signed window. One grammar, three channels, no new concepts.

**Scopes (uniform-scope law).**

- **Head selected:** `B` + value sets the *onset* bend — pre-bend authoring for free, since the
  model already says a pre-bend is a nonzero onset bend.
- **Keyframe(s) selected:** sets/retypes each selected keyframe's bend statement, exactly like
  digit retype across a multi-selection.
- **Caret on a tail with no keyframe there:** `B` creates a **pending fret-less keyframe** at the
  caret — stating nothing about position, so the slide interpolates through unkinked — and arms
  the window on it. This mirrors W10's caret-on-tail-plus-digit, channel for channel.
- **Bare `B` with no payload:** mutates nothing; settle dissolves the pending point without
  residue. `B` is not a toggle — `V` toggles because vibrato is boolean; bend is scalar and a
  toggle has no value to claim.
- **`Shift+B`:** left unbound. No second claimant exists — "release" is not a verb here, it is a
  bend value typed like any other.

**Magnitude entry, typed and dragged, in steps.** The unit is **whole steps with quarter
fractions**, matching `charterBendText`'s display vocabulary glyph for glyph (½, 1, 1½, "full").
This is match-to-the-real-world, and it closes a live mode-error trap: with semitone entry a
charter typing `1` for "full bend" would author a half step. What you type is what the chip
shows. Legal values are multiples of 0.25 in [0, 3]; conversion to the semitone-native evaluator
happens once at the seam.

**Pending model (W3).** The value is provisional inside the multi-digit window and renders live
on the pending chip in the display vocabulary. Out of range, off-quantum, or a minus sign turns
the chip text `EditorTheme::invalid` red — chip-sized, so luminance carries it — and **an
unsettled invalid value is display-only and never deforms the polyline** (R7). Minus is inert-red
under H7; it belongs to the future `W` window.

**Settle.** The generalized dissolve law is the sole commit rule: the point commits iff it
changed the path function or the state. Every good behavior falls out with no special cases —
typing the value the channel already interpolates to at that offset dissolves; typing `0` on a
never-bent tail dissolves; typing `0` after a peak commits, because the channel holds flat past
the last statement, so a zero statement creates the release ramp; a hold point at the peak's own
value before a later release is not collinear with its neighbors and commits. **Clearing** is the
same gesture run backward: `B` on a bend-stating keyframe, window committed empty, removes the
bend statement, and the empty-keyframe refusal plus the dissolve law then erase a point that
states nothing. Escape cancels the window and the pending point with no residue.

**Drag.** Vertical drag on a token, snapped to the quarter-tone quantum, with **input gain
decoupled from the render scale**: at 1.5 px/semitone the quantum is 0.75 px of curve motion, so
a position-faithful drag would demand sub-pixel mouse precision. Map pointer travel at a usable
gain (≥ 4 px per quarter-tone) while the curve moves at its fixed H6 scale — the scale never
rescales, only the gain is non-unity, which is the standard fine-drag shape. The live chip is the
feedback channel the eye actually tracks, because the curve's response is sub-stroke by
measurement.

**Keyboard nudge (proposal, needs keymap sign-off).** `Alt+↑/↓` on a keyframe-only selection =
bend ±¼ step. The slot is provably free — string moves are refused for keyframe-only selections
(signed 2026-08-13) and fret transpose lives on `Shift+Alt+↑/↓` — and it imports the automation
lanes' meaning of `Alt+↑/↓`, "move value", which a keyframe exactly is.

**Glance-vs-demand allocation this verb commits to.** At a glance: existence and direction (onset
chevron, curve slope, the chip's bend glyph) and rough magnitude (curve height, comparable across
notes per H6). On demand: exact magnitude (peak chips, hover elsewhere), handles (dots always,
padding on hover), pending state (gesture-scoped, chip-carried). The player's surface never
receives text.

## 8. PROPOSED 3D-parity obligation

The redefinition to put before the user for signature: **parity = same pitch topology, same
timing, same numbers where numbers exist — not same geometry.** Under it the highway minimally
owes:

1. **The same pitch function.** The shared evaluator (H10) feeds both surfaces; 3D's lift stays
   metric per the string-gap law. Every direction change 2D draws occurs at the same time
   coordinate in the 3D lift. No second derivation, ever.
2. **Bend existence and direction at onset** — the onset chevron already exists in 3D; R5 is its
   2D twin.
3. **When bar lands: downward lift to the floor, saturating there** (H17), with return timing
   preserved. Depth beyond the floor is not shown; the player's glance budget cannot spend on
   magnitude precision, and 2D's chip carries the number for the charter. The 2D saturate-at-−12
   rule (§6.3) makes the two saturations one law rather than two.
4. **What 3D explicitly does not owe:** keyframe tokens, dots, rings, chips, pending states — all
   charting marks, 2D-only by the LeftTap precedent. And vibrato-during-bend stays metric in 3D
   (the sine rides the lifted tail, which has room there) while 2D states it by token — a
   geometry divergence inside the same statement, which is precisely what this redefinition
   covers.

## 9. The sighting checklist — what only the user can rule

Verdicts belong to `survivor_sheet_1x.png` at 1x; `survivor_sheet_4x.png` is for confirmation
only. Each item carries its falsifier.

Per the 2026-08-26 whammy ruling above, items 2, 3, and 6 (the bar-clause rows) are no longer on
today's checklist — they move into the future whammy analysis as its starting evidence. Items 1,
4, 5, 7, and 8 remain live.

1. **Cutoff triptych — pick the chevron slot.** Recommended: in-head, because above-head clears
   the neighbor by 0.25 px and touches after antialiasing. Falsified if the in-head chevron reads
   as a fret glyph or vanishes. Then the residual question only the user can answer: *is
   existence + direction enough at cutoff density?* If magnitude must be readable there, the
   transient magnified view graduates from shelf to requirement.
2. **Rail triptych (plain cross / gap / fade)** — does the crossing read as *departure* (a
   statement) or *leaking* (an error)? This is the H1-break verdict. If all three read as error,
   the metric dive dies and the symbolic-dive question (the E reopen) returns to the user.
3. **Shallow-dip behavior at down 1.0** — a −4 dip sags into the rail opening and is chip-carried.
   Acceptable, or does it force down 1.5? If it forces 1.5, the fix is chip relocation, not
   scale, because 1.5 overprints the neighbor chip by 6.0 × 3.5 px.
4. **Pending vs committed chips** — distinguishable at 1x (luma 146 vs 255, background 38 vs 60)?
   Falsified if not; the fix is a stronger pending mark, never a brighter one.
5. **Vibrato row** — confirm the clipped-sine treatment reads as a fault, which makes the token
   rule (R6) the answer. If clipping reads acceptably at ≤ 1 step, decide whether a hybrid (sine
   when it fits, token when it does not) is worth a second rule.
6. **Dive fill row** — magnitude, or a selection-highlight misread?
7. **Dot size at cutoff** — fixed 5 px vs the ~3 px scaled variant.
8. **Composite bent-slide row** — accept the falsifier-confirmed fact that a mid-slide bend
   statement rides its dot and chip: the measured deflection is 1.5 px, sub-stroke. Recorded, not
   hidden.

## 10. Signatures required, and open questions carried

Signatures are distinct from sightings — these are rulings, not looks:

- **(a)** The parity redefinition (§8) — **deferred to the whammy analysis** per the 2026-08-26
  ruling; it exists only to license the bar clause.
- **(b)** The deliberate H1 break, downward only (R2, R10) — **deferred to the whammy analysis**.
- **(c)** Down scale 1.0 px/semitone — **deferred to the whammy analysis**, and the blocking JUCE
  chip-width measurement moves with it (the 2.5 px clearance sits inside font uncertainty).
- **(d)** The `Alt+↑/↓` keymap slot for bend nudge.
- **(e)** The storage-vs-entry quarter-tone flag: does the format constrain bend to quarter-tone
  multiples, or does only entry validate it?

Open questions carried out of this study:

- **Chip widths are UNVERIFIED against the JUCE typeface** — every collision number involving a
  chip is provisional until one measurement replaces the raster approximation. This is the single
  blocking item.
- **Whether the cutoff density needs magnified editing at all** (sighting item 1's residue) — if
  yes, the Cubase-shape transient view must be specified so it acquires no verbs of its own.
- **W9-F (pitched-vs-falls-away glyph) and W13 junction techniques** will add ink at junctions
  after the keyframe model lands; the token register in R3 must absorb them without a second
  vocabulary.
- **The `W` window's exact grammar** waits on `whammy-bar-support.md`; only the reservations in
  §6 are claimed here.
- **The scoop's pre-onset x-zone** is reserved but undesigned.

Everything above is a proposal. The study's own recommendation is that the language of §5, the
reservations of §6, the verb of §7, and the redefinition of §8 advance as **one package**, since
its three trades are interlocked — but the package is worth nothing until the sheets are sighted,
and the simpler alternative (ship the bend clause alone and let the bar clause wait for the
format) was rejected only because the down-direction reservation costs nothing today and becomes
a breaking redesign once the margin is colonized or the zero line drifts.
