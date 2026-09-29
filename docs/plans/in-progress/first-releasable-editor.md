# First releasable editor plan

Status: ACTIVE. Drafted 2026-09-19 from the user's release bar; hardened the same day against the
roadmap, every other document in this folder, and the previous sessions' task lists. This is the
umbrella plan for **G0** (task #252). It owns the ORDER, the SCOPE and the EXIT of the release; the
design of each step stays in the roadmap plan or design record named beside it, and nothing here
restates those documents' rules.

Every claim about current code below was verified on 2026-09-19, and the status claims were
re-verified 2026-09-24. Re-verify before acting on one — nothing keeps it true.

## Release bar

A charter can start from no project, create a song, author the chart and its supporting metadata,
and keep editing, without needing an importer or a hand-written file edit. **Everything the current
chart format accepts and the product claims to support is editable**, and everything authored
survives save, close, reopen and playback.

Three corollaries decide the hard cases:

1. **A supported fact that cannot be re-edited after reopen blocks the release.** So does one that
   is displayed or played back wrongly in the editor — a charter cannot trust what they cannot
   check.
2. **A fact the validator refuses is not supported.** Artificial and tapped harmonics are refused
   today (`chart_rules.cpp`, `ChartRepair::DisabledHarmonic`), so they are outside the bar. If they
   are re-enabled before release, their parked follow-ups become blockers at that moment.
3. **Game-side behaviour, importer fidelity and visual polish that does not change what a charter
   can author or verify are outside the bar**, however worthwhile.

Pinch harmonics stay in scope as the supported picking-hand harmonic. Showing or editing a pinch's
node is a separate product decision: both surfaces deliberately show the fret and the harmonic
identity today, not the node. If that changes, the pinch-node UI joins this plan.

## What already ships

Verified in the tree; listed so nobody rebuilds it. The remaining work is the phases below.

- Typed note entry, delete, fret typing, sustain resize, selection move, one-undo gesture bursts
  (plan 40 Phase 4; only pointer drag-move remains, see Decisions).
- Every supported note-local technique verb: mute, palm mute, vibrato (`V` / `Shift+V`, per leg,
  on heads and selected keyframes), tremolo, accent, ghost (`G`), pick slide, legato (`L`),
  left-hand tap (`Shift+T`), natural and pinch harmonics (`H` / `Shift+H` with the node picker).
- Slide and keyframe authoring: keyframe retype / fret shift / offset move, the slide-out as the
  release keyframe, keyframe create on the ring plane (`Alt`+digit, `Alt+Insert`), the bare
  digit's cut of a ring (`planCutRing`, `5d0cb384`), and the `Shift+L` tie / slide link (tasks
  G1–G3 and G6, all done).
- Sections (`Ctrl+M`) and tone regions (`Ctrl+T`) under the marker verb grammar, with tone parameter
  automation lanes.
- The grid-snap switch and the one placement quantum; the chart keybind-discovery menu; the
  rebindable command registry.

## Decision gates on the path

The work is blocked more by unsigned decisions than by code. Each gate is the user's to sign; the
phase it blocks cannot start without it.

| Gate | Where | Blocks |
|---|---|---|
| **G41-TS** — content policy for a beats-per-measure edit. PROVISIONALLY A (hold the beat, measures renumber), 2026-09-19; signed at its sighting | `docs/plans/roadmap/41-tempo-map-authoring.md` Q1 | Nothing now — Phase 1's time-signature work may start on A |
| **G43-METADATA** — 43-Q1..Q6. NARROWED by D1 (2026-09-19): Q1..Q5 left the release with plan 43 Phases 1, 3 and 5, so only Q6 (an authored chart version field; recommended: none) still stands | `docs/plans/roadmap/43-song-information-and-art.md` Phase 0 | Nothing in the release unless Q6 is ruled B — the four-field subset adds no format field |
| ~~**The keyframe ruling bundle + the bend display anchor + W9-F / W9-D / W9-G**~~ — CLOSED 2026-09-28 with Phase 3: W9-F resolved, W9-G ruled, the anchor moved to a watch item; bundle items 1–3 carried, not blocking | `technique-review-walkthrough.md` W9, Phase 3 below | Nothing |
| **G60-RULINGS** — 60-Q1..Q5, a law-by-law session | `docs/plans/roadmap/60-hand-markers.md` §9 | Phase 4 (Phase 0 of plan 60 is ungated) |
| **G52-RANGE-EDIT** — all of 52-Q1..Q8, individually | `docs/plans/roadmap/52-range-edit-operations.md` Phase 0 | Phase 5 |

Signing sessions are cheap and unblock the most; run them ahead of the phase that needs them rather
than at its door.

## Ordered path

### 0. Scope and inventory lock

This document is the inventory. What remains of Phase 0 is the user's pass over **Decisions** below
and the sign-off of the scope lists at the end. After that the only change to scope is a recorded
ruling.

### 1. New-project foundations

Build the path that creates an editable song without importing a Guitar Pro file.

- **Tempo map and time signatures** — plan 41 Phases 1–4 and Phase 6 (behind G41-TS). The `Ctrl+B`
  tempo-anchor and `Ctrl+/` meter chords follow the eight-step new-marker-kind checklist in
  `marker-verb-grammar.md`; the chip verbs (`Delete`, `Enter`, `Alt`+arrows) are inert until then.
  Plan 41 Phase 5 (offline onset snapping) is a charting aid, not a supported fact: out of the bar
  unless Phase 3's hand placement proves unusable without it.
- **The anchor grid lock.** `editing-interaction-model.md` makes the toolbar grid-lock interlock a
  ship-with-anchor-editing requirement; nothing implements it today. It lands with plan 41 Phase 3.
- **Song information, audio and art** — plan 43, or the subset the release needs: the fields
  required to save and reopen, song audio selection, album art. D1 decides how much of 43 is in.
- **Tuning, capo and cent offsets** — plan 40 Phase 10 (task G7). String count is
  `tuning.strings.size()`; removing a string deletes its notes in one compound undo and re-fits both
  surfaces' lane counts.
- **The New chart entry point** — plan 40 Phase 11, gated on plan 41. A blank project must write a
  first tone region starting at `1:1`: the reader refuses a package whose first tone change is late
  (`marker-verb-grammar.md`, review question 8), so the exit below fails without it.
- **Package write safety** for the new-project path. Re-verify what the atomic-replace work left
  open before relying on it; the todo file
  `docs/plans/todo/native-package-write-safety-followups.md` is a stub.

Exit: a new blank project can be created, saved, reopened and played with a valid timeline, a tone
and an arrangement in a chosen tuning.

### 2. Finish direct note editing

The verbs exist; what is missing is the editor telling the charter what it did, two
unenforced rulings, and a set of defects on supported material.

- **Build the refusal flash** (task #278; design record `refusal-flash.md`). This is the single
  prerequisite under everything else in this phase. The user ruled the surface on 2026-08-28: the
  selected elements a verb refused glow red a couple of times and stay unchanged, and the reason
  goes to the log — NOT a status line or toast, which is the proposal it replaced and the wording
  this entry carried until the ruling was recovered on 2026-09-21. `IEditorView::showNotice` is the
  modal load-time notice, not this. Three reports wait on it, and only the first exists in code:
  the legato verb's skip (`ChartLegatoPlan`, which already returns the refused notes with their
  reasons, `aa9b9491`, unwired), the harmonic picker's skip, and the mixed-validity report for
  technique and keyframe edits (`chart-span-and-selection-model.md` §9a). Until it lands, `L` on
  an ineligible selection is a dead key.
- **The slide tail lock — BUILT, and closed 2026-09-21** (W6, roadmap 40-Q5). This entry said a
  shortened sustain silently dropped the keyframes past its new end; that was already untrue when
  it was written. `planAdjustSustain` floors a shrinking ring at its last keyframe (`83f2afcd`,
  2026-09-09), the step that lands ON a keyframe stating a fret and nothing else — and a fret the
  path does not already hold there — makes it the release (`83c6bc5a`), and a released ring shrinks
  no further — the move verb is the fall's
  handle. Pinned end to end by "A ring lands on its last keyframe inside one gesture"
  (`test_chart_sustain_gesture.cpp`). The feedback question closed the same day: a visible bound
  is not a refusal, so the floor is silent (`refusal-flash.md`). TWO defects were found and fixed
  while closing it, both the same shape — a landing that cost the keyframe its meaning:
  - the landing was also taken on a keyframe carrying vibrato or a bend, which the
    release's bare-fret law then stripped, and a mid-hold vibrato keyframe left stating nothing new
    dissolved at the next settle (fixed 2026-09-21, `c9e72dbf`);
  - the landing was also taken on a keyframe repeating the fret already in force, which became a
    release falling toward the fret the string already holds: nothing drew it, the settle sweep
    dissolved it, and until then the released ring refused to shorten from the head either — the
    charter's tail stuck on a mark nothing shows (user report, fixed 2026-09-21).
  Both now hold the ring at the nearest grid line above the keyframe (user ruling, 2026-09-21),
  under one predicate the move verb can ask the same way
  (`common::core::ringEndMayLandOnLastKeyframe`).
- **Closed 2026-09-23: the same hole one verb over.** A ring truncated onto a keyframe carrying a
  vibrato sheds that vibrato by the channel table; the clip now reports the shed as a lost statement
  and the plan gate refuses (`clipPayloadsToSustain`, `finalizePlan`), for every verb. A bend
  there stays, the curve's last value.
  The three SILENT-release routes beside it are CLOSED (user ruling, 2026-09-21): a landing on a
  keyframe repeating the fret in force, a `clipPayloadsToSustain` that turns a TRAVELLING release
  silent by erasing the junction it travelled from, and a digit typed under `Alt` at a ring's exact
  end naming the fret already in force (now `chartEntryTarget`, `5d0cb384`). A release that says
  nothing is authoring state under the keyframe commit law — never written, and dissolved when its
  note leaves focus (`dissolveSilentKeyframes`); `dissolveSilentRelease` went with `dc20b003`
  (2026-09-22). The `Alt` press at a ring's end naming the fret in force plants such a statement
  and is a refusal-flash consumer (`refusal-flash.md`).
- **Inserting a note can no longer clip a neighbour's keyframes silently** (closed 2026-09-23):
  the plan gate's `normalizeSustainOverlaps` reports a truncation that lost an authored statement
  and `finalizePlan` refuses on it for every verb, insert and move alike, in both directions
  (`ring-ends-and-authoring-planes.md`, the alongside item). What remains is the screen's half: a
  refusal the screen does not explain, which is `refusal-flash.md`'s (the flash lights the refused
  selection).
- **#277 re-ruled 2026-09-25: a tap at a claimed stop is LEGAL** — it proves the hand lifted, and
  the span holding that fret splits at the tap. The derivation absorbs it today (a right-hand tap
  states nothing to the posture, so the claim carries through; a left-hand tap at the fret joins
  the span), so this is a derivation fix on supported material: the contradiction block in
  `chart_shapes.cpp` gains a break for a tap on the string whose fret equals the standing claim,
  for both hands; `chartHeldStops`' default follows; tests and a census row; the law texts amended
  (listed in `chart-ruleset.md`).
- **The split product is STRUCK — RULED 2026-09-25 (user) and built the same day.** `Shift+L`'s
  split head stores `Pick`, inheriting the origin's payloads; the unstruck-tie proposal is closed,
  because a tie is never stored (it is one longer ring, which is what the join produces), and a
  charter who wants legato at the cut presses `L` on the product.
- **Harmonic display follow-ups #2 and #4** (`harmonic-display-followups.md`). #2 is the satellite
  background. #4 (one span across a tap pulled off to its plant) was NARROWED 2026-09-19 by the
  re-ruled hold-under law and must not contradict 60-Q1..Q5, so rule it with or before the G60
  session. **#9 (two chords are two boxes) is BUILT** — closed the same day by that law: a derived
  pull-off landing stop states nothing to the grip unless the string is demonstrably already at
  it (`chart-ruleset.md`, THE HOLD-UNDER LAW).
- **The hold-under law's other half moved to Phase 4.** The derivation may no longer ASSERT a
  finger it cannot prove, so the charter must be able to ASK for it. A note-level `held` on
  ordinary fretting-hand notes was considered the same day and SET ASIDE in favour of the authored
  span (see Phase 4, "an authored span swallows what validly fits"): the fact is span-level, and
  one authored fact beats two that must agree. `held` stays legal exactly where it is today.
- **The harmonic verbs — SIGNED 2026-09-24.** P5, withdrawn on 2026-09-18 when `H` / `Shift+H`
  went back into active work, was re-sighted and signed; their keymap rows are signed with it.
- **The claimed stop (`held`) has no 3D face** (`chart-ruleset.md`, Open; the renderer says so at
  `highway_renderer.cpp`). An authored, supported fact the 3D preview does not state. Rule: build
  the mark, or record the divergence deliberately — see D4.
- **Defects on supported material**, each a release blocker under corollary 1 unless D5 rules
  otherwise: ~~open strings ringing ~49 beats under the let-ring phrase cap (#271)~~ — FIXED
  2026-09-19 by bounding the phrase at its marked run, census re-pinned 2026-09-20 `4a10e833`,
  re-sighted and closed 2026-09-24; the all-palm-muted chord repeat box dropping other marks
  (#267); ~~pick-slide turnaround easing (#268)~~ — closed 2026-09-24 (`eb5aaa3c`, curve
  `3648d500`); ~~chord bend/vibrato direction per onset group (#274)~~ — closed 2026-09-24
  (`ea2e5156`); **undo not resyncing the
  audible tone while paused** (`docs/tracking/backlog.md`; the fake's per-tone half is done, the
  harness half blocks).

Exit: every supported note-local field has an editor verb, reports what it skipped, round-trips
through save and reopen, and draws the same fact on both surfaces.

### 3. Bend authoring

**CLOSED 2026-09-28 on the user's sighting** ("bend editing has landed and actually looks quite
clean"). The G9 bundle (tasks #261–#264), item by item:

- **Bend authoring itself** — `B` and `V`, BUILT 2026-09-27 and SIGHTED 2026-09-28. Bare `B` on a
  covered slot plants a bend point, on a selection states each anchor, `Alt+B` the ring twin, with
  a quarter-step picker (`keymap-matrix.md` `B` rows). Bare `V` / `Shift+V` on a covered slot
  plants a fret-less point carrying the width of the leg it begins, refused on a travel leg; no
  `Alt` twin, since a ring's exact end has no leg to vibrate.
- **Both deferred sightings SIGNED 2026-09-28**: the end bend abutting a same-string head
  (`ring-ends-and-authoring-planes.md` phase 1b) and the chord bend direction shipped by
  `ea2e5156`.
- **The end bend at a shared instant** — the one defect the sighting found. The ending ring's
  chips used to drop below the envelope at an instant a same-string head owns, which put them in
  the band above the next string's head, onto that head's own pre-bend chip. They now keep their
  ordinary height and end short of the head's square (`ringChipLimit`, `423e2bf9`). The user
  also asked whether a head and a ring ending on it at one instant should be forced to agree on a
  bend value; RULED NO — a bend peaking as the next note is struck unbent is real playing, and
  Guitar Pro writes it routinely. Whether to DROP the end chip where the head's own bend repeats
  its value is unruled (the user is unsure) and waits as a watch item.
- **W9-F** — RESOLVED: the slide-out rework made it stale. Unpitched travel exists only as the
  slide-out terminal, which 2D marks with its fret chip and no head, while a pitched stop wears a
  linked head. **W9-G** — RULED 2026-09-28: the mute X is the ATTACK's mark and stays on the
  onset only (`technique-review-walkthrough.md`).
- ~~**A keyframe that states no fret draws nothing** (W13)~~ — RESOLVED 2026-09-27: a fret-less
  keyframe inherits the fret in force (storage always did; the view dropped it), every keyframe is
  published and reachable with the mark of what it states, and no vibrato change may stand
  mid-slide (`technique-review-walkthrough.md` W13).
- **Keyframe bundle items (7) and (8)** — legato-merged bend chains gaining the onset chip, and a
  bend across a junction gaining its glow arrival — closed on the same sighting ("things are
  sighting pretty well in general"). Item (4), the disconnect's split product, was RULED
  2026-09-25 (struck, stored `Pick`).
- **Moved to `docs/tracking/watch-items.md`** (2026-09-28, the user: the current graphics display
  bends well): the bend display anchor (`highway-note-art-state.md`, Open decision 1), the 2D
  look-and-feel study (`docs/plans/todo/bend-display-study.md`, with the parked
  `2d-bend-waypoint-redesign.md`), and the per-string bend segment display in 3D
  (`docs/plans/completed/fret-hit-light-effect.md`, open decision 4).
- **Carried, not blocking bends**: keyframe bundle items (1) the trim floor's value (the floor is
  gone with the ring-ends plan; `g_minimum_slide_window` lives in the editor's
  `pick_slide_defaults.h`), (2) the coincident-onset vibrato overwrite and (3) the importer's
  vibrato-anchor wording — 2 and 3 may be overtaken by per-leg vibrato (`cfe83edc` / `21f96ed2`);
  re-verify before ruling. All three stay listed as open sign-offs in
  `docs/plans/todo/unified-waypoint-model.md`.

Exit: bends can be created, adjusted, displayed and saved with the same confidence as slides.

### 4. Hand markers, FHPs and spans

Execute `docs/plans/roadmap/60-hand-markers.md` as release work. It supersedes plan 40 Phase 8's
template, shape and FHP halves, and it is the union of the former plans 60 and 61 — so the tabled
tasks #280 (span marker seam), #281 (boundary gestures), #282 (FHP derivation) and #284 (the
pull-off feasibility law, now rule 12) are all this phase, and #283 (the materialize-markers
stopgap) is superseded by plan 60's own sequencing and should be closed. Design records: `fhp-derivation-algorithm.md`,
`span-derivation-ground-up.md`, `docs/plans/todo/span-marker-redesign.md`,
`docs/plans/todo/chord-dictionary.md`.

- Run the G60 law-by-law ruling session.
- Phase 0 — strict uniqueness for stored `fhps` (ungated; can land now).
- Phases 1–2 — the derived 11-rule tracker and stream. Rule 12 (ruled 2026-09-18) is not satisfied
  by today's generator.
- Phase 3 — the hand marker record, the `Ctrl+H` / `Ctrl+Shift+H` chords, the one hand row, pointer
  selection, the boundary gestures.
- Phase 4 — span-free zones and the settlement census, including re-signing the four derivation rows
  against plan 60's correction polarities and the FHP counter rows that were left UNSIGNED on
  purpose for this bundle.
- Phase 5 — templates, the dictionary, the template editor and the fingering coupling. This also
  closes the named gap left by the `N` rip-out (stating a fretting-hand stop where nothing sounds),
  the arpeggio-bracket posture verb's dictionary dependency, and forced chord naming if D6 wants it.
- Phase 6 — the let-ring hand coupling.
- **An authored span swallows what validly fits** (user direction 2026-09-19, from the hold-under
  re-ruling; to be signed in the G60 session and built with plan 60's boundary gestures). The
  derivation states a held stop only where it can prove one; everything past that is the
  charter's to say, and they say it ONCE, with the span's extent — never with a second per-note
  field. By default an authored span's edge stops where the derivation would split. An EXTEND verb
  walks the edge outward and checks each note it meets: a note FITS where it sounds on a string
  the grip leaves free, restates the grip's stop, or sounds ABOVE the stop the grip holds on its
  string (never an open string or a fret-hand harmonic over a gripped stop, never through the
  travelled range — `travelsThroughFret`). A swallowed note of the third kind has its held stop
  DERIVED from the span — the grip's entry beneath it — so the satellite, the bracket and the
  hand all read one authored fact. A note that does not fit refuses the extension there.
  - Acceptance case, sighted: the Torn intro's span fronts at `3:3+1/2` since the re-ruling,
    because the G-string 7 at `3:3` is its string's first note. Extending the authored span back
    to `3:1` swallows it (7 above the grip's 5) and restores the front.
  - Forcing a SPLIT where the derivation proved a held stop is the same tool run the other way:
    author the boundary. There is deliberately no way to delete a derived held stop on the note —
    "no finger here" would be a third state of one field.
  - A hand-authored grip may name EITHER stop of a note that has two (the sounding fret or the
    held one beneath it); every other note has one.
  - Invariant to hold in the G60 session: the span decides extent and which provable stops it
    prints; it must never become a second way to assert what `held` already asserts on a tap.
- **Span-end margin** (`note-sustain-model.md` item #59) and the **PROVISIONAL arpeggio
  shrink-split** (`chart-span-and-selection-model.md` §5) are both met by this work; rule #59 in the
  G60 session.

Exit: FHPs and spans are authored through the hand marker surface, not imported-only data — and the
acceptance criterion carried from the accumulation sighting holds: **author a span over crossing
material, delete it, and every ribbon keeps its EXACT original length.**

### 5. Bulk editing

Plan 40 Phase 9 and plan 52 (task G8), behind G52-RANGE-EDIT.

- Copy / cut / paste / delete over a selection and over a range; transpose; select-all.
- One clipboard codec in the document grammar, shared by both plans; position rebasing across
  signature changes; collision handling; cross-arrangement rules.
- Materialized context at the range start where the signed rules require it — FHPs and tone.
- "Paste arms the marker" is a working answer awaiting ratification at the gate.
- Plan 52 consumes plan 47 Phases 2–3 (the ruler-drag time selection). The keyboard half of the
  grid-locked `TimeSelection` already shipped, so the pointer half of 47 is pulled in — and with
  it **plan 47 Phase 4, the audible loop region** the selection feeds (in scope by D3).

Exit: a full song can be charted and revised without repeated one-object reconstruction.

### 6. Validation and release hardening

- **Plan 42** chart validation report, including degenerate-span flagging.
- Load / save checks for every authored stream. **Delete the removed-spelling rows** in
  `chart_document.cpp` (`"mute"`, `"harmonic"`, `"touch"`, `"accent"`, `"slideOut"` and the rest):
  they are fail-loudly tripwires, not compatibility, and their own comment says each goes once the
  packages carrying it are re-imported — so re-import the local corpus and delete them before
  release rather than shipping them. The external converter still emits `"accent"`
  (`docs/plans/completed/note-emphasis-axis.md` item 7), so its output must be fixed or retired
  first, or the row's "re-import" advice is untrue on that path.
- Sighting passes: keyboard rows, marker rows (task #298 the marker grammar end to end; #301
  P10's feel questions and #270's remainder were signed 2026-09-24), hand markers, ~~bends~~
  (sighted 2026-09-28, Phase 3), New Chart, the loop region, and the keybind dialog's in-action review (plan 46 Phase 3, by D3).
- User documentation for the authoring workflow. `editing-interaction-model.md` defers the
  user-facing keybind docs "while the grammar is still being tuned" — this is where that ends.
- Doc consolidation: `keymap-matrix.md` dissolves into `editing-interaction-model.md` when plan 53
  completes, and the grid-snap stale-row banners in both get folded in. The two-kind selection law
  is stated in full in three documents; leave one statement.

Exit: invalid authored states are refused or repaired through one authority, and the charter can
find what went wrong without a modal dead end.

### 7. Acceptance pass

Create a release-candidate chart from scratch using only the editor. It must include:

- tempo and time-signature changes;
- tuning, capo and cent offsets;
- sections;
- tone regions and automation;
- notes with sustains, slides, keyframes, bends, mutes, accents, ghosts, tremolo, vibrato, legato,
  taps, pick slides and supported harmonics;
- FHPs, hand markers, spans and templates;
- copy / paste / transpose edits;
- save, close, reopen and playback verification, including undo across each kind.

The release is blocked by any supported chart fact in that chart that cannot be edited again after
reopen, or that either surface states wrongly.

**Optional, decided here: pointer drag-move (D2).** Once the acceptance chart exists the user
rules whether drag-move ships in the first release or becomes the first post-release item. It
blocks nothing above. If it goes in, it shares edge auto-scroll and the drag threshold with plan
47's ruler-drag time selection (Phase 5), so build the two against one seam.

## Decisions for the user

Open calls this plan cannot make. Each has a recommendation; none is settled until signed.

- **D1 — Plan 43 and plan 10's migration ladder. HALF RULED 2026-09-19 (user): no backward
  compatibility is wanted BEFORE the first release, so plan 10 is deferred to just after it — the
  moment people hold packages that must survive an editor update — and plan 43 does not wait on
  it: 43 adds its `song.json` fields in place. The refusals the readers carry today are not
  compatibility: the removed-spelling rows in `chart_document.cpp` exist to fail loudly and are
  deleted once the corpus is re-imported, and the artificial / tapped harmonic refusal is
  forward-looking, because those forms are planned. Plan 10 is marked Deferred in place and sits first in the
  roadmap's Stage 7, so it is the first thing picked up after this release. **RULED 2026-09-19
  (user), the other half:** exported packages must carry COMPLETE metadata, so the first
  release ships with **export disabled altogether**, and export is re-enabled when plan 43 lands
  in full (the new fields, the art codec and the export readiness gate together). In the release:
  plan 43 Phases 2 and 4 narrowed to the four fields the format already carries (title, artist,
  album, year), plus switching the export action off. Out until plan 43 lands: Phases 1, 3
  and 5, and with them 43-Q1..Q5. Save, Save As and reopen are untouched — they are the release
  bar's round trip.
- **D2 — Pointer drag-move** (`docs/plans/todo/tab-pointer-drag-editing.md`). **RULED 2026-09-19
  (user): not in the bar, not out either — it sits at the END of the plan as an optional item (see
  Phase 7), and the user decides whether it ships in the first release once the editor is taking
  its final shape.** Sized the same day: a medium build with no chart law, format or derivation
  in it. The move verb already replays a run from the keys it started on as one plan and one undo
  entry, and the lane already reports Down / Drag / Up, so the controller half is small; the cost
  is the pointer half — the drag threshold, live preview, edge auto-scroll, Esc cancel, and the
  sighting rounds a pointer gesture takes. No grab zone competes: heads are the lane's only
  targets and tail-drag resize is dropped. The todo file was rewritten against the code the same
  day.
- **D3 — Surfaces the plan was silent on. RULED 2026-09-19 (user).** OUT: the plugin-chain keyboard
  model (plan 53 Phase 5; tone design, not a chart fact, and it leaves `Enter` on a tone region
  meaning "retone" for the release), automation-lane point multi-select and marquee (plan 53
  Phase 6), playback count-in (plan 59), editor theme presets (plan 45). IN: **the audible loop
  region** (plan 47 Phase 4) — it joins Phase 5 beside the ruler-drag time selection that feeds
  it — and **the keybind dialog's in-action review** (plan 46 Phase 3; code-complete, needs only
  the user's review), which joins Phase 6's sighting passes.
- **D4 — The claimed stop's missing 3D face. RULED 2026-09-19 (user):** a tap's `held` gets NO
  mark of its own on the highway. It is stated in 3D only through a span's bracket — the posture
  it is a member of. So there is no 3D mark to build. **Refined the same day while sighting the
  pull-off defect:** taps resolve exactly as legato does. A DERIVED landing fret — under a tapped
  source or a picked one — states nothing, because the finger is not PROVABLY planted when the
  source sounds; its only effect is that an ornament above a stop the grip ALREADY holds never
  seams. An AUTHORED `held` is the charter stating the finger is there, and attaches to the span
  as today. Built with the pull-off span fix in Phase 2.
- **D5 — Which listed defects block. RULED 2026-09-19 (user): all of them.** #271 (open-string
  ring, fixed the same day, closed 2026-09-24) and undo-not-resyncing the tone make authored
  material play wrongly (the section-insert position, listed here at the ruling, was already fixed
  by `14be7ce5`, 2026-09-13); #267 is the one display defect on a supported technique left, and
  it is fixed before release — none is ruled tolerable; #268 and #274 closed 2026-09-24.
- **D6 — Forced chord naming** with a name-suggestion algorithm
  (`chart-span-and-selection-model.md` §3). **RULED 2026-09-19 (user):** it belongs to the template
  and chord-dictionary work (Phase 4, plan 60 Phase 5), and a span WITHOUT a template is acceptable
  in the first release — which ships with export disabled (D1). **The condition rides export's
  return:** when export is re-enabled, every span must carry an explicit fingering and a chord
  name, and export is refused until the chart is CLEAN — fully labeled, fully fingered, and free
  of validation findings. So the export readiness gate (plan 43 Phase 5) is not a metadata check
  alone: plan 42's chart findings and the span-completeness check join the same blocker list.
- **D7 — Minimum-sustain-distance override. RULED 2026-09-19 (user): deferred.** The release ships
  the default. Tails now draw from the specified note duration, and the current tail display
  ruleset reads well across every chart sighted, so nothing is asking for an override.
- **D8 — "Rebase". CLOSED 2026-09-19 (user): a leftover name, no work.** Every use of the word in
  the docs is the offset re-basing that merges, the tie join and paste already do; no user-facing
  verb was ever meant, and the word is deleted from `chart-ruleset.md`'s open verbs. The real
  adjacent question was ruled the same day: **the grid is TRUTH** — content holds its musical
  position and its TIME moves when the tempo map is edited (recorded at plan 41's Q1).
- **D9 — CLOSED AS SHIPPED 2026-09-19 (user), with a watch item** (`docs/tracking/watch-items.md`,
  "The edit position dissolves at every multi-select"): if it sights as a problem later it is
  rethought then. The question as it stood: does the edit position survive multi-select? (task
  #272, deliberately reopened.) Shipped
  behaviour dissolves the armed slot at every multi-select and keeps time and string; the reopened
  idea is a SECOND indicator for the edit position, which is the one-rule-in-two-places shape the
  project hunts, and an L-sized build if it flips. Recommendation: rule it closed as shipped before
  Phase 2 so it is not re-litigated mid-build.

## Explicitly out of scope

- Artificial and tapped harmonics, and with them harmonic follow-ups #1, #3, #5, #6, #7 and #8, the
  tap-harmonic head emphasis, and `docs/plans/todo/artificial-harmonic-authoring.md`.
- Pinch node display and editing; the heavy-accent tier (`Shift+A`); whammy (`W`); strum direction;
  the `;` accent alias.
- Everything game-side: detection (including the note-emphasis detection touchpoint), scoring
  (including arpeggio-span scoring), the game's box-chain walk, menus, the 2D game view.
- The Guitar Pro importer's fidelity follow-ups, the external converter's default ring and tap
  canonization (the converter's `"accent"` key is the one exception, under Phase 6).
- The format-shape step in `technique-compatibility-and-hardening.md` (scrape variant, path-derived
  scrape sustain) — a representation refactor that authors no new fact.
- The hidden-head mark (2D + 3D pair, task #279), head-atlas mipmapping (plan 56), highway theming
  (plan 54), fret hit-light tuning, the camera lead (#269), the FOV pin (#273), the head-mark order
  audit (#275), the floor-law breaches and consolidation (#276), the triple-projection cost watch item.
- The smooth-scroll camera (parked), the cross-platform port, C++26.
- Plan 62 (per-song kept-sustain bound) — superseded; it survives only as a watch item's remedy
  (`docs/tracking/watch-items.md`).

## Task-list audit (2026-09-19)

All five surviving session task stores were reconciled against the repo. Nothing of substance was
ever dropped: every task that vanished between sessions was either built (W3 pending fret entry,
W4 / E25, the scrape path translation, the shared view-state fold, slide / keyframe authoring) or is
recorded. Of the 33 tasks still open in the newest store, the release-required ones are named in
the phases above by number; the rest are out of scope and listed below. Five items lived ONLY in a
task store and are now in the repo: the eight keyframe rulings (Phase 3), the reopened multi-select
question (D9), the floor-law breaches, the head-mark audit and the parked planted / held rename
(`docs/tracking/backlog.md`), and the `ChartStop` ordering watch item
(`docs/tracking/watch-items.md`). Tasks #289 and #290 duplicate existing registry entries; #285 is
a trigger-gated watch item already in `watch-items.md`; #286's converter decision is out of scope
apart from its `"accent"` key.

## Companion documents in this folder

Everything else in `docs/plans/in-progress/` is one of these. None is a second plan; this file is
the only one that orders work.

| Document | Role | Feeds |
|---|---|---|
| `00-start-here.md` | Cold-open snapshot for the next session | — |
| `technique-review-walkthrough.md` | LIVE decision queue (W5, W6, W9-D/F/G, W10 default, W13 display) | Phases 2, 3 |
| `harmonic-display-followups.md` | Follow-ups #2, #4 live; #9 built; the rest parked | Phase 2 |
| `refusal-flash.md` | Task #278's ruled direction, build shape and open questions F1–F7 | Phase 2 |
| `derived-shift-slide.md` | SIGNED and BUILT record; key half superseded by the ring-ends plan | Phase 2 |
| `ring-ends-and-authoring-planes.md` | The ring's-end display and the lane's two authoring planes; both phases BUILT and signed 2026-09-24 | Phase 2 |
| `keyframe-and-burst-ground-up.md` | Unsigned PROPOSAL, ground-up review of keyframes, the release and the undo burst | Phase 3 |
| `highway-note-art-state.md` | 3D note-art state record; holds the bend anchor decision | Phase 3 |
| `fhp-derivation-algorithm.md` | Evidence and the five rulings behind 60-Q1..Q5 | Phase 4 |
| `span-derivation-ground-up.md` | The built span law; cited from `chart_shapes.cpp` | Phases 2, 4 |
| `arpeggio-posture-display-options.md` | SETTLED display record; cited from `tab_paint_core.cpp` | Phase 4 |
| `chart-ruleset.md` | The five laws; the code is the authority | all |
| `technique-compatibility-and-hardening.md` | The signed technique matrix; cited from `chart_rules.h` | Phases 2, 3 |
| `chart-span-and-selection-model.md` | Span, selection and marker model; dissolves into 40 / 42 / 52 | Phases 2, 4, 5, 6 |
| `editing-interaction-model.md` | The interaction grammar; design-side truth | Phases 1–5 |
| `keymap-matrix.md` | The signed keymap and plan 53's tracking artifact | Phases 1–5 |
| `marker-verb-grammar.md` | Marker grammar + the new-marker-kind checklist; awaiting review | Phases 1, 4, 6 |
| `legato-authoring-model.md`, `legato-final-spec.md` | The legato record and its signed ruling | Phase 2 (counted skip) |
| `note-sustain-model.md` | Stored-actual / presented model; cited from three headers | Phase 4 (#59) |
| `note-format-and-tablature-plan.md` | Format rationale, reference only; live spec is `docs/developer/file-formats.md` | — |
| `developer-guide-completion.md` | Standing coverage registry — belongs in `docs/tracking/`, see note | — |

Most of these are signed records that production code cites by path, so they stay where they are
even though they are no longer "in progress" in the lifecycle sense. `developer-guide-completion.md`
is a registry that never completes; moving it to `docs/tracking/` also means amending the "two
standing registries" sentence in `CLAUDE.md`, so it waits for the user's word.

## Next action

1. ~~The user's pass over **Decisions** D1–D9~~ — done 2026-09-19; every entry above carries its
   ruling. ~~The pull-off span defect~~ — built 2026-09-19 as the re-ruled hold-under law, sighted
   and signed 2026-09-20. ~~The two things it surfaced~~ — both done 2026-09-20: the
   zero-length-span bug, then **the carried-ring founding rule**, ruled as one sentence for every
   ring ("a note belongs to the span that contains its onset"; `chart-ruleset.md`, with the
   reopening trigger in `docs/tracking/watch-items.md`), and the corpus census re-pinned after it
   with every row made able to fail.
2. Done 2026-09-21..24, unplanned here but on the path: the tick lattice (`a6455969`,
   `ce0db3a2`), the derived shift slide (`3e5fceae`, `dc20b003`), the ring-ends plan's two phases
   and its alongside gate (`da74d79b`..`5d0cb384`, `df5ef1ab`), per-leg vibrato (`cfe83edc`), the
   span-law fixes (`902da3de`, `9d095609`, `25640d82`), the pick-slide turnarounds (`eb5aaa3c`).
   Ring-ends phases 1 and 2 were sighted and signed 2026-09-24; the 1b item that waited on bends
   was signed 2026-09-28.
3. **Current.** Land the ungated work while gates are signed: the refusal flash
   (Phase 2), plan 60 Phase 0, plan 41 Phases 1–2, tuning / capo (plan 40 Phase 10). Two small rulings are cheapest
   signed before Phase 2's verbs are wired — both RULED 2026-09-25: #277 (a tap at a claimed
   stop is legal; the derivation split is a Phase 2 build) and the split product (struck, built).
4. Schedule the signing sessions in the order their phases arrive: G41-TS closes at its own
   sighting (G43 is narrowed to Q6 by D1), the bend bundle closed 2026-09-28, then G60-RULINGS (carrying #4,
   #59 and the authored-span extend law) and G52-RANGE-EDIT.
