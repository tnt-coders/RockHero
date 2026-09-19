# First releasable editor plan

Status: ACTIVE. Drafted 2026-09-19 from the user's release bar; hardened the same day against the
roadmap, every other document in this folder, and the previous sessions' task lists. This is the
umbrella plan for **G0** (task #252). It owns the ORDER, the SCOPE and the EXIT of the release; the
design of each step stays in the roadmap plan or design record named beside it, and nothing here
restates those documents' rules.

Every claim about current code below was verified on 2026-09-19. Re-verify before acting on one —
nothing keeps it true.

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
- Every supported note-local technique verb: mute, palm mute, vibrato, tremolo, accent, ghost (`G`),
  pick slide, legato (`L`), left-hand tap (`Shift+T`), natural and pinch harmonics (`H` /
  `Shift+H` with the node picker).
- Slide and keyframe authoring: keyframe retype / fret shift / offset move, the slide-out as the
  release keyframe, keyframe create through the `Insert` ghost, and the `Shift+L` tie / slide link
  (tasks G1–G3 and G6, all done).
- Sections (`Ctrl+M`) and tone regions (`Ctrl+T`) under the marker verb grammar, with tone parameter
  automation lanes.
- The grid-snap switch and the one placement quantum; the chart keybind-discovery menu; the
  rebindable command registry.

## Decision gates on the path

The work is blocked more by unsigned decisions than by code. Each gate is the user's to sign; the
phase it blocks cannot start without it.

| Gate | Where | Blocks |
|---|---|---|
| **G41-TS** — content policy for a beats-per-measure edit | `docs/plans/roadmap/41-tempo-map-authoring.md` Q1 | Phase 1 (time signatures) |
| **G43-METADATA** — 43-Q1..Q6 | `docs/plans/roadmap/43-song-information-and-art.md` Phase 0 | Phase 1 (song information) |
| **Plan 43's dependency on plan 10** | 43 Phase 0 exit waits on plan 10 Phases 0 and 2 (the format migration ladder) | Phase 1 — see Decisions, D1 |
| **The keyframe ruling bundle + the bend display anchor + W9-F / W9-D / W9-G** | `technique-review-walkthrough.md` W9, `highway-note-art-state.md` Open decisions | Phase 3 |
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

- **Build the non-modal notice channel** (task #278). This is the single prerequisite under
  everything else in this phase. `IEditorView::showNotice` is the modal load-time notice, not this.
  Four payloads are built and waiting on it: the legato verb's counted skip (`ChartLegatoPlan`), the
  harmonic picker's skip reason, the locked-slide-tail refusal (40-Q5, W6), and the mixed-validity
  count for technique and keyframe edits (`chart-span-and-selection-model.md` §9a). Until it lands,
  `L` on an ineligible selection is a dead key.
- **The slide tail lock** (W6, roadmap 40-Q5 — unsigned). Today a sustain shortened past a keyframe
  drops every fret, bend and vibrato statement beyond the new end (`clipPayloadsToSustain`, by
  design) and tells the charter nothing; only undo brings them back. Unreported loss of authored
  data on supported material is the case corollary 1 exists for. Sign 40-Q5
  — refuse below the last keyframe, or clip and report the loss — and build whichever it is. The
  report half rides the notice channel above; the lock half had no task of its own and was nearly
  lost when the old W6 task was merged into #278.
- **The tap-at-claimed-stop refusal** (task #277) — ruled invalid by construction, enforced
  nowhere: `chart_rules.cpp` never consults `chartClaimedStops`, so the state can be authored and
  saved. The open design call is placement (validator, load repair, or planner refusal).
- **Rule the unstruck-tie default** for `Shift+L`'s split product — still a PROPOSAL in
  `technique-review-walkthrough.md` W10 and `keymap-matrix.md`.
- **Harmonic display follow-ups #2, #4 and #9** (`harmonic-display-followups.md`). #2 is the
  satellite background. #4 (one span across a tap pulled off to its plant) and #9 (two chords are
  two boxes) are marked LIVE there and change span derivation for ordinary supported material — #9
  moves the corpus census. #9 carries one open predicate to rule first. Neither may contradict
  60-Q1..Q5, so rule them with or before the G60 session.
- **Re-sight the harmonic verbs.** P5 was withdrawn on 2026-09-18 when `H` / `Shift+H` went back
  into active work; their keymap rows stay PROVISIONAL until a fresh sighting signs them.
- **The claimed stop (`held`) has no 3D face** (`chart-ruleset.md`, Open; the renderer says so at
  `highway_renderer.cpp`). An authored, supported fact the 3D preview does not state. Rule: build
  the mark, or record the divergence deliberately — see D4.
- **Defects on supported material**, each a release blocker under corollary 1 unless D5 rules
  otherwise: open strings ringing ~49 beats under the let-ring phrase cap (task #271 — fixing it
  AMENDS signed law #186); the all-palm-muted chord repeat box dropping other marks (#267);
  pick-slide turnaround easing (#268); chord bend/vibrato direction per onset group (#274); the
  section insert resolving its position on prompt ACCEPT rather than key press
  (`docs/tracking/backlog.md`); **undo not resyncing the audible tone** (`marker-verb-grammar.md`,
  "Not done, deliberately" — blocked on the live-rig test fake).

Exit: every supported note-local field has an editor verb, reports what it skipped, round-trips
through save and reopen, and draws the same fact on both surfaces.

### 3. Bend authoring

The G9 bundle (tasks #261–#264). Rulings first, then the verb.

- **Bend display anchor** — name the constant, sight, sign (`highway-note-art-state.md`, Open
  decision 1; two couplings flagged there: `highwayBentNoteY`'s saturation guard and vibrato's tuned
  depth).
- **The keyframe ruling bundle** (task #262), signed in one pass with the anchor since both decide
  drawn bend geometry. No other document enumerates the eight, so they are listed here: (1) the trim
  floor's value, still borrowing `g_minimum_slide_window` in `chart_presentation.cpp`; (2) the
  coincident-onset vibrato overwrite; (3) the importer's vibrato-anchor wording; (4) the
  disconnect's unstruck-tie default, marked UNSIGNED in `chart_edits.cpp` (the same ruling Phase 2
  names); (5) W9-F; (6) W9-G; (7) legato-merged bend chains gain the onset chip; (8) a bend across a
  junction gains its glow arrival.
- **W9-F with W9-D's glyph** — how 2D says *pitched* versus *falls away*; 3D already dims an
  unpitched run and 2D does not, so the surfaces diverge today. **W9-G** (does a mute restate at
  each slide junction) rides the same session.
- **A keyframe that states no fret draws nothing** (W13), so no pointer reaches a bend-only
  keyframe: `KeyframeViewState::fret` is non-optional. This must be answered before bends can be
  authored on the keyframe substrate.
- **Bend segment display in 3D** — per-string, not per-slot; built once and reverted
  (`docs/plans/completed/fret-hit-light-effect.md`, open decision 4).
- **Bend authoring itself** — the `B` verb at the armed caret on a covered slot (plan 40 Phase 7),
  and `V` for a vibrato keyframe by the same grammar.

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
  grid-locked `TimeSelection` already shipped, so only the pointer half of 47 is pulled in; the
  audible LOOP is not release scope (D3).

Exit: a full song can be charted and revised without repeated one-object reconstruction.

### 6. Validation and release hardening

- **Plan 42** chart validation report, including degenerate-span flagging.
- Load / save checks for every authored stream; **old-package refusals checked against scope** —
  in particular the armed `"accent"` key tripwire in `chart_document.cpp`, whose "re-import" advice
  is untrue for packages from the external converter, which still emits `"accent"`
  (`docs/plans/completed/note-emphasis-axis.md` item 7), and the retired `"harmonic"` / `"touch"` /
  `"slideOut"` tripwires.
- **The minimum-sustain-distance override** (`chart-span-and-selection-model.md` §10, OPEN). The
  blanket clamp is a restriction a charter meets constantly; rule whether the release ships the safe
  default or the override (D7).
- Sighting passes: keyboard rows, marker rows (#301 P10's three feel questions, #270's batch
  remainder, task #298 the marker grammar end to end), the release keyframe and slide margin work
  marked UNSIGHTED in the walkthrough, hand markers, bends, New Chart.
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

## Decisions for the user

Open calls this plan cannot make. Each has a recommendation; none is settled until signed.

- **D1 — Plan 43 waits on plan 10's migration ladder.** The project's standing rule is no legacy or
  migration code and formats changing in place, which plan 10 Phase 2 contradicts. Recommendation:
  cut the dependency — plan 43 adds its `song.json` fields in place, and plan 10 stays out of the
  release. Also decide how much of 43 is in: recommended Phases 1, 2 and 4 (fields, workflow,
  dialog); the art codec (3) if album art is a supported field; the export gate (5) out.
- **D2 — Pointer drag-move** (`docs/plans/todo/tab-pointer-drag-editing.md`). The keyboard moves
  everything already. Recommendation: out of the bar; it authors no new fact.
- **D3 — Surfaces the plan is silent on.** Recommendation: all out, stated so they stop dangling —
  the plugin-chain keyboard model (plan 53 Phase 5; tone design, not a chart fact, and it leaves
  `Enter` on a tone region meaning "retone" for the release), the audible loop region (plan 47
  Phase 4), automation-lane point multi-select and marquee (plan 53 Phase 6), playback count-in
  (plan 59), editor theme presets (plan 45), the keybind dialog's in-action review (plan 46 Phase
  3 — code-complete, needs only the user's review and is cheap to close).
- **D4 — The claimed stop's missing 3D face.** Build a mark, or record a deliberate divergence. The
  surfaces-must-not-diverge rule argues for building it; rule it with the G60 session since the hand
  marker work touches the same stop.
- **D5 — Which listed defects block.** Recommendation: #271 (open-string ring), undo-not-resyncing
  the tone and the section-insert position are blockers — they make authored material play or land
  wrongly; #267, #268 and #274 are display defects on supported techniques and block under
  corollary 1 unless the user rules them tolerable for a first release.
- **D6 — Forced chord naming** with a name-suggestion algorithm
  (`chart-span-and-selection-model.md` §3). Recommendation: out; validation flags an unnamed chord.
- **D7 — Minimum-sustain-distance override.** Recommendation: ship the safe default, leave the
  override design deferred, and revisit on the first real chart that needs it.
- **D8 — "Rebase"** is listed in `chart-ruleset.md`'s open verbs without a definition. If it means
  re-timing a chart against an edited tempo map it is Phase 1 work; say which.
- **D9 — Does the edit position survive multi-select?** (task #272, deliberately reopened.) Shipped
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
| `harmonic-display-followups.md` | Follow-ups #2, #4, #9 live; the rest parked | Phase 2 |
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
| `note-sustain-model.md` | Stored-actual / presented model; cited from eight headers | Phase 4 (#59) |
| `note-format-and-tablature-plan.md` | Format rationale, reference only; live spec is `docs/developer/file-formats.md` | — |
| `developer-guide-completion.md` | Standing coverage registry — belongs in `docs/tracking/`, see note | — |

Most of these are signed records that production code cites by path, so they stay where they are
even though they are no longer "in progress" in the lifecycle sense. `developer-guide-completion.md`
is a registry that never completes; moving it to `docs/tracking/` also means amending the "two
standing registries" sentence in `CLAUDE.md`, so it waits for the user's word.

## Next action

1. The user's pass over **Decisions** D1–D9.
2. Land the ungated work while gates are signed: the non-modal notice channel (Phase 2), plan 60
   Phase 0, plan 41 Phases 1–2, tuning / capo (plan 40 Phase 10).
3. Schedule the three signing sessions in the order their phases arrive: G41-TS with G43, the bend
   bundle, then G60-RULINGS (carrying #4, #9, #59 and D4) and G52-RANGE-EDIT.
