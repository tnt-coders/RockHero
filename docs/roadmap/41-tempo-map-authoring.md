# Plan 41 — Tempo Map Authoring

**Status:** Ready — Phases 1–5 are executable now; Phase 6 (time-signature editing) is
decision-gated on open question Q1 below and must not start before it is answered.
Date: 2026-07-06. Baseline: `refactor @ 3c7febe0`.

## Goal

A charter starting from nothing but backing audio can author an accurate tempo map against the
waveform: place and drag beat anchors, rough in tempo by tapping along to playback, snap anchors
to detected audio onsets, and edit time signatures — all with full undo, live BPM feedback, and
no change to the persisted song format. Today a usable tempo map exists only if the project came
from a GP import; this plan removes that dependency and thereby unblocks the from-scratch
charting promise of docs/roadmap/40-chart-editing.md.

## Non-goals

- No song.json or chart format changes. Anchors stay on-beat `"<measure>:<beat>"` tokens with
  millisecond-grid seconds. Sub-beat anchors are explicitly out of scope; if a real song ever
  needs one, that is a format change routed through
  docs/roadmap/10-format-versioning-and-chart-identity.md.
- No create-project-from-raw-audio project flow. docs/roadmap/40-chart-editing.md owns that flow;
  `TempoMap::defaultMap` already provides the seed map this plan makes editable.
- No grid declutter or display-density work (docs/todo/tempo-grid-declutter-plan.md is a separate
  deferred item; its disposition is owned by docs/roadmap/00-roadmap.md).
- No timeline-origin change; docs/todo/timeline-origin-rethink.md stays deferred as recorded.
- No real-time onset or pitch detection — that is docs/roadmap/22-note-detection.md. Onset analysis
  here is offline, editor-assist only, and never persisted.
- No audio time-stretching: the tempo map warps the beat grid over fixed backing audio
  (docs/completed/tempo-map-implementation-plan.md, "Playback Interactions").
- No metronome click-track playback for auditioning the map. It is a natural follow-up; propose
  it separately rather than growing this plan.

## Constraints

Applicable subset of the roadmap constraint block, restated:

- (a) **Layering**: common never depends on editor or game code. Anything both products need is
  extracted to rock-hero-common first, as its own phase with tests. Tracktion headers stay
  isolated to rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: headers stay in `src/` until a consumer outside the library
  exists (docs/design/architectural-principles.md, "Placement Procedure for New Files").
- (c) **Naming firewall**: the commercial real-guitar game that inspired this project is never
  named; use "RS" or neutral phrasing. Charter (BSD 3-Clause) may be named.
- (f) **Undo**: RockHero-owned mementos/inverse edits through the editor-core undo history;
  Tracktion is never the product undo stack.
- (h) **Builds**: all build/test/lint commands go through `.agents/rockhero-build.ps1` (usage in
  `.agents/README.md`), never raw cmake/ctest/ninja. Intermediate phases run only the checks
  their changes determinately warrant; the final acceptance phase is the sanctioned bundle as
  separate invocations.

Plan-specific hard invariants (all already enforced by the reader and preserved by every phase):

- Tempo changes only at anchors; interpolation is metronome-linear inside each anchor span
  (docs/design/architecture.md, "Song Data Model").
- The warp-anchor storage model is deliberately kept — design within it, never around it.
- Anchor positions are on-beat `measure:beat` tokens; anchor seconds sit on the three-decimal
  millisecond grid; the first anchor is `1:1`; the terminal anchor is on a downbeat past content;
  anchors strictly increase in both grid position and seconds.

## Current state inventory

- Domain model: `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h`
  defines `TimeSignatureChange`, `BeatAnchor`, and `TempoMap` (warp-anchor grid, metronome-linear
  interpolation documented in the class comment, derived signature-segment/anchor index tables,
  `ForwardBeatTimeCursor` for monotonic scans, `quarterNoteBpmAtSeconds` for span tempo).
  `TempoMap::defaultMap(TimeDuration)` (impl at
  `rock-hero-common/core/src/timeline/tempo_map.cpp:177`) seeds a 4/4 120 BPM map covering an
  audio duration. Exact rational positions use
  `rock-hero-common/core/include/rock_hero/common/core/timeline/fraction.h`.
- Persistence and validation: `rock-hero-common/core/src/package/rock_song_package_read.cpp`
  reads `tempoMap.timeSignatures` (~line 318) and `tempoMap.anchors` (~line 376), requires the
  `tempoMap` object (~line 435), and validates through `validateTempoMap` (~line 457), which is
  declared in the **private** header
  `rock-hero-common/core/src/package/rock_song_package_format.h:43`. Anchor tokens are rejected
  unless on-beat; seconds must sit on the millisecond grid.
- Producers: exactly two ways a tempo map comes into existence today. (1) GP import —
  `rock-hero-editor/core/src/project/gp_chart_builder.cpp` builds anchors from score sync points,
  snaps them to the millisecond grid (~lines 105–120), back-extrapolates a `1:1` lead-in anchor
  when sync points start late (~lines 197+), and extends a terminal downbeat anchor past the
  final bar. (2) `TempoMap::defaultMap`, currently used only by tests and the editor UI test
  harness (`rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/
  editor_view_test_harness.h:351`). **No editor action edits the tempo map**: the
  `EditorAction::Action` variant (`rock-hero-editor/core/src/controller/editor_action.h`, ~line
  470) carries transport, grid-note-value, tone, and plugin actions only.
- Display consumption: `rock-hero-editor/core/src/timeline/tempo_grid_geometry.cpp` (file-local
  `MeasureGridWalker` at line 43; note-value-authoritative, measure-anchored scan) with public
  header `rock-hero-editor/core/include/rock_hero/editor/core/timeline/tempo_grid_geometry.h`;
  `rock-hero-editor/ui/src/timeline/timeline_ruler.h` paints the tempo band (one marking per
  anchor span), signature band, measure numbers, and ticks; its `mouseDown` only seeks. Timeline
  geometry (seconds-to-pixels) is pure editor-core math per docs/design/architecture.md,
  "Editor UI".
- Undo infrastructure: `rock-hero-editor/core/src/controller/editor_undo_history.h` (`IEdit`,
  `EditorEditContext` carrying `common::core::Session&`, `EditorUndoHistory`); the pure
  inverse-command edit pattern to mirror is
  `rock-hero-editor/core/src/tone/tone_region_edits.h`; per-domain handler TU precedent is
  `rock-hero-editor/core/src/tone/tone_handlers.cpp` and
  `rock-hero-editor/core/src/project/project_handlers.cpp`.
- Engine independence: `rg TempoMap rock-hero-common/audio` matches test files only. Playback
  transport is absolute seconds
  (`rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h:100`,
  message-thread `position()` read). **Tempo-map edits require no engine change.**
- Offline analysis precedent: `analyzeAudioForGainNormalization` in
  `rock-hero-common/audio/include/rock_hero/common/audio/song/audio_normalization.h:83` decodes
  package audio on demand behind the common/audio boundary. There is no onset detection anywhere
  in the repo (`rg -i onset` hits docs and placeholder READMEs only) and no tap-tempo facility.
  `rock-hero-editor/audio` is a placeholder library (`src/placeholder.cpp`).
- Chart content addressed by the map: chart notes/shapes/fhps/sections and tone-track regions
  store exact `measure:beat` (+ rational fraction) position tokens, never seconds (see
  docs/in-progress/note-format-and-tablature-plan.md for the active note-format work). Time
  signature edits therefore have re-addressing implications — Phase 6.
- Keybind handling is scattered (`rock-hero-editor/ui/src/main_window/editor_view.cpp:618` et
  al.); docs/roadmap/46-editor-keybinds.md centralizes it later.

Verified against code on 2026-07-06, refactor @ 3c7febe0.

## Dependencies

- **Gates** docs/roadmap/40-chart-editing.md: its create-chart-from-raw-audio promise requires this
  plan's Phases 1–4 (anchor authoring + tap tempo) at minimum; Phase 5 (onset snapping) strongly
  recommended before serious from-scratch charting. Sequence 41 before the from-scratch phases
  of 40.
- docs/roadmap/10-format-versioning-and-chart-identity.md: no format change is proposed here. The
  tempo map is inside 10's identity-hash scope, so any tempo edit changes chart identity by
  design — that is correct behavior, not a conflict. A future sub-beat-anchor need routes
  through 10.
- docs/roadmap/12-playback-clock.md (soft): Phase 4 tap capture works against
  `ITransport::position()` today; if 12's clock lands first, use it for lower-jitter tap
  timestamps. Not blocking either direction.
- docs/roadmap/42-chart-validation.md: content-validity rules after time-signature edits
  (out-of-range addresses, coverage gaps) belong to 42's reusable rule set; Phase 6 coordinates
  with it rather than inventing a parallel validator.
- docs/roadmap/46-editor-keybinds.md: the tap key and anchor nudge keys register through the
  centralized keybind system when it lands; until then they live in `EditorView::keyPressed`
  like every other shortcut.
- docs/in-progress/tone-track-tempo-map-plan.md: active tone work touches the same Session,
  action variant, and handler TUs. Start Phase 2 only after the in-flight tone handler/UI work
  is committed, to avoid churning shared files (reference only — active work is never absorbed).

## Decisions already made

Restated inline with sources; a fresh session must not re-litigate these.

1. **The tempo map is a warp-anchor grid and that storage model is kept.** Time signatures are
   change points carried forward; anchors pin sparse on-beat positions to absolute seconds
   (docs/completed/tempo-map-implementation-plan.md, "Decisions" 1–3;
   docs/completed/tempo-map-storage-shape-discussion.md; docs/design/architecture.md,
   "Song Data Model").
2. **Tempo changes only at anchors; interpolation is metronome-linear.** The quarter-note rate is
   constant inside each anchor span; meter changes between anchors re-slice beat durations
   (docs/design/architecture.md, "Song Data Model"; `tempo_map.h` class comment).
3. **Anchor seconds persist at millisecond precision**; writer emits exactly three decimals and
   the reader rejects off-grid seconds (docs/completed/tempo-map-implementation-plan.md,
   "Decisions" 4; docs/design/architecture.md, "Song Data Model" — precision above the
   onset/latency floor is intentionally avoided).
4. **Structural invariants**: first anchor `1:1`; terminal anchor on a downbeat; strictly
   increasing positions and seconds; positive numerators; power-of-two denominators
   (docs/completed/tempo-map-implementation-plan.md, "Validation"; enforced in
   `rock_song_package_read.cpp`).
5. **Grid alignment never stretches audio**; whole-track practice speed is a separate realtime
   feature (docs/completed/tempo-map-implementation-plan.md, "Playback Interactions").
6. **Undo is RockHero-owned** (constraint (f)); the editor-core `IEdit` inverse-command /
   full-state-memento pattern is the required mechanism (precedent:
   `rock-hero-editor/core/src/tone/tone_region_edits.h`).
7. **Grid generation is note-value-authoritative and measure-anchored** via the private
   `MeasureGridWalker` (docs/completed/timeline-ruler-review-fixes-plan.md, Phase 7); this plan
   only feeds it new maps, never reworks it.
8. **Timeline coordinate math is pure editor-core**, not Tracktion's tempo sequence
   (docs/design/architecture.md, "Editor UI"); Tracktion stays a playback backend.
9. **Pointer gestures follow the editor-wide interaction model** (settled 2026-07-09,
   `docs/in-progress/editing-interaction-model.md`): Alt+click inserts an anchor at a beat;
   anchor drags are always free-time (never grid-snapped — dragging an anchor *is* the
   audio-sync gesture); a toolbar grid lock, default locked, gates anchor insert/move/delete;
   Esc cancels an in-flight drag; edits commit once per gesture.

## Open questions for the user

Mirror each into docs/roadmap/00-roadmap.md "Decisions needed".

- **Q1 — Time-signature edit content policy (gates Phase 6).** Changing beats-per-measure
  re-buckets global beats into measures, so every downstream `measure:beat` token (anchors, chart
  notes, shapes, fhps, sections, tone regions) means something different afterward. Options:
  - **A. Preserve global-beat positions (recommended).** Re-address every position token through
    the old map's global-beat axis into the new bucketing. Content keeps its exact absolute time
    against the audio (anchors keep their seconds and their global beats, so interpolation is
    unchanged); measure numbers shift downstream. Deterministic, time-preserving, and testable as
    a pure transform. The terminal anchor may need re-seating on the nearest following downbeat
    (same trick the GP builder already uses).
  - **B. Preserve literal `measure:beat` tokens.** Content silently moves in time, and beats past
    the new numerator become invalid and need clamping. Destructive for populated charts; only
    defensible on empty projects.
  - **C. Forbid time-signature edits once downstream content exists.** Safe but hostile: charters
    iterate meter while charting, and from-scratch projects would have to finalize meter first.
  - Recommendation: **A**, implemented as a pure transform with property tests (see Phase 6).
- **Q2 — Onset analysis source and placement.** Options:
  - **A. Self-written spectral-flux kernel (recommended).** Pure math (windowed flux + adaptive
    peak picking) in `rock-hero-common/core`, decode adapter in `rock-hero-common/audio`
    following the `analyzeAudioForGainNormalization` precedent. No new dependency; the kernel is
    reusable by docs/roadmap/22-note-detection.md and docs/roadmap/23-detection-verification-harness.md
    for offline work, and 22's real-time detectors can later supersede it.
  - **B. Third-party DSP dependency (e.g. aubio).** More mature pickers, but adds a Conan
    dependency (availability unverified), a license to audit against AGPLv3, and an integration
    surface for what is a snapping assist, not a scoring path.
  - **C. Editor-local kernel in editor/core.** Smallest surface, but if 22/23 later want offline
    onset extraction the code moves anyway, violating the extract-to-common-first discipline.
  - Recommendation: **A**.
- **Q3 — Tap-tempo application semantics.** Options:
  - **A. Constant-BPM fit (recommended).** Taps define a quarter-note pulse; on apply, the fitted
    BPM and first-tap downbeat produce/update anchors: on a still-default map this sets the `1:1`
    anchor seconds plus a re-seated terminal anchor (global rough-in); on an authored map it
    applies to the enclosing anchor span only. One undoable edit; least anchor clutter; refined
    afterward by dragging and onset snapping.
  - **B. Anchor-per-tap.** Every tap on a downbeat mints an anchor. Captures drift in one pass
    but bakes message-thread key jitter into many anchors and floods the map.
  - Recommendation: **A** now; add a per-measure downbeat capture mode later only if real
    charting shows the need.

## Phased implementation

Common notes for all phases: every new file follows the placement procedure of
docs/design/architectural-principles.md ("Placement Procedure for New Files"); comments/Doxygen
follow docs/design/documentation-conventions.md; all commands run from the repo root. Each phase
is a coherent commit (or small series) with imperative subjects.

### Phase 1 — Shared tempo-map rules and pure anchor edit transforms

- **Scope:**
  - 1a (common/core): promote `validateTempoMap` from the private package header to a public
    header `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map_rules.h`
    (new TU `src/timeline/tempo_map_rules.cpp`), returning a project-owned typed error instead of
    the package-private error type where needed; the package reader delegates to it.
    Behavior-preserving — the extraction exists so live editing and package reading share one
    validator (constraint (a): extract to common first, as its own step with tests).
  - 1b (editor/core, private): pure transforms in
    `rock-hero-editor/core/src/timeline/tempo_map_edits.{h,cpp}`:
    - `insertAnchor(map, measure, beat)` — pins the currently interpolated seconds at that beat,
      rounded to the millisecond grid; typed failure on collision or off-map addresses. Inserting
      an anchor is time-neutral by construction (no audible/visible shift until dragged).
    - `moveAnchorSeconds(map, measure, beat, seconds)` — clamps into the exclusive
      (previous, next) anchor window on the millisecond grid; first anchor clamps at >= 0.
    - `deleteAnchor(map, measure, beat)` — refuses the first (`1:1`) and terminal anchors.
    - `reseatTerminalAnchor(map, audio_duration)` — guarantees a terminal downbeat anchor past
      both content and audio (mirrors `defaultMap`/GP-builder logic); used by tap apply and
      Phase 6.
    - `spanBpmPreview(map, measure, beat)` — quarter-BPM of the spans adjacent to an anchor, for
      live drag readouts (wraps `quarterNoteBpmAtSeconds`).
    All return `std::expected` with a typed error enum; every mutating transform revalidates via
    `tempo_map_rules` before returning.
- **Public-header impact:** +1 public common/core header (`timeline/tempo_map_rules.h`) — its
  outside consumer is the editor. Editor transforms stay `src/`-private (constraint (b)).
- **Testing:** new `rock-hero-common/core/tests/test_tempo_map_rules.cpp` (direct rule coverage:
  every invariant violated once); new `rock-hero-editor/core/tests/test_tempo_map_edits.cpp`
  (insert time-neutrality, clamp windows, millisecond grid, first/terminal protection, terminal
  re-seat across meter changes, validity after every operation). Existing package read/write
  tests must stay green untouched.
- **Exit criteria:** package reader delegates to the public rules with zero behavior change; all
  transform properties proven by tests.
- **Verification** (three separate invocations; the third because new TUs and a new public
  header are lint-relevant):

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
  powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
  ```

### Phase 2 — Editor actions, handlers, and undo integration

- **Scope:** wire anchor authoring through the editor-core MVC:
  - Extend `EditorAction` (`rock-hero-editor/core/src/controller/editor_action.h`) with
    `InsertTempoAnchor{measure,beat}`, `MoveTempoAnchor{measure,beat,seconds}`,
    `DeleteTempoAnchor{measure,beat}`; availability rules in
    `editor_action_availability.cpp` (project open, not mid-busy — mirror tone actions).
    Anchors are addressed by their `measure:beat` position, never by raw index; handlers resolve
    to an index at apply time and fail typed if missing.
  - New per-domain handler TU `rock-hero-editor/core/src/timeline/tempo_handlers.cpp`
    (precedent: `tone_handlers.cpp`), applying Phase 1 transforms to `Session` song state,
    marking the project dirty, and refreshing timeline view state through the existing
    `i_editor_view` listener flow. No engine calls — the engine does not consume the tempo map
    (verified in the inventory).
  - Inverse-command edits `rock-hero-editor/core/src/timeline/tempo_anchor_edits.{h,cpp}`
    mirroring `tone_region_edits.h`: move stores before/after seconds; insert stores the minted
    position; delete stores the full removed anchor (constraint (f)).
  - Anchor-only edits never rewrite chart content: notes and regions keep their musical
    addresses and shift in absolute time with the warp — that is the model working as designed.
- **Public-header impact:** none intended; view-state additions (anchor list for the ruler) go in
  existing public editor/core timeline view-state headers only if the UI cannot derive them from
  the `TempoMap` it already receives via `setGrid` (prefer zero new surface).
- **Testing:** editor-core controller tests alongside existing suites: action dispatch,
  undo/redo round-trips restore the exact map (field equality via `TempoMap::operator==`), dirty
  marking, availability, typed failures for stale addresses.
- **Exit criteria:** anchors are insertable/movable/deletable headlessly with full undo fidelity.
- **Verification:** build + touched tests + clang-tidy (new TUs), same three invocations as
  Phase 1.

### Phase 3 — Anchor UI: place and drag against the waveform

- **Scope:** interaction in the editor UI, all coordinate math through existing pure geometry:
  - `rock-hero-editor/ui/src/timeline/timeline_ruler.{h,cpp}`: draw anchor handles in the tempo
    band (the band already derives one marking per anchor span); hit-testing; horizontal drag
    previews `MoveTempoAnchor` and commits **one** edit on mouse-up (no per-pixel undo spam);
    Escape during drag abandons the preview. Gestures follow
    `docs/in-progress/editing-interaction-model.md`: Alt+click on a beat tick inserts an anchor
    at that beat (ghost handle + `CopyingCursor` while Alt is held); anchor drags are **always
    free-time** — no grid snap, the anchor defines the grid — rounded to the millisecond grid;
    Delete/context menu removes the hovered anchor. While dragging, show the two adjacent span
    BPMs from `spanBpmPreview`.
  - **Grid lock** (the interaction model's anchor interlock): a toolbar toggle, default
    **locked**, that disables anchor insert, move, and delete — cursor feedback shows the lock
    and menu items disable with the reason. Anchors are the one object class whose drags are
    free-time by design, so Ctrl-friction cannot protect them; the lock is the deliberate
    unlock-sync-relock gate. Lives in editor view state + the toolbar/transport strip; not
    persisted per-project.
  - `rock-hero-editor/ui/src/timeline/track_viewport.cpp` / `arrangement_view.cpp`: optional
    vertical anchor guide lines over the waveform row so drags are judged against transients by
    eye (the whole point of "against the waveform").
  - Keyboard nudge (left/right = +/- 1 ms, with a coarse modifier) for the selected anchor, wired
    through `EditorView::keyPressed` until docs/roadmap/46-editor-keybinds.md centralizes it.
- **Public-header impact:** none; all files are ui `src/` privates.
- **Testing:** editor/ui tests through
  `rock-hero-editor/ui/tests/include/rock_hero/editor/ui/testing/editor_view_test_harness.h`
  (headless JUCE 8 rendering — use `SoftwareImageType` as existing pixel tests do): handle
  hit-tests, drag produces exactly one undo entry, Escape abandons, insert lands on-beat,
  guides align with ruler anchor x positions. Controller-level commit-on-release tests live in
  editor-core.
- **Exit criteria:** a GP-imported map can be visibly corrected by dragging anchors against the
  waveform; undo/redo behaves as one step per gesture.
- **Verification:** build + touched tests + clang-tidy.

### Phase 4 — Tap-tempo capture

- **Scope (assumes Q3 = A; re-scope the apply step if the user picks B):**
  - Pure capture model `rock-hero-editor/core/src/timeline/tap_tempo.{h,cpp}`: consumes
    monotonically increasing tap timestamps, maintains a windowed constant-BPM fit
    (least-squares over tap index vs seconds), exposes BPM estimate + jitter, resets after a
    configurable silence gap (default 2 s). Timestamps come from
    `ITransport::position()` read on the message thread at key-event time — jitter of a few
    milliseconds is acceptable because taps are a rough-in refined by Phases 3 and 5;
    docs/roadmap/12-playback-clock.md can later supply lower-jitter reads.
  - Actions: `TapTempo` (accumulate during playback) and `ApplyTapTempo` (single undoable edit):
    on a still-default map, set the `1:1` anchor to the first-tap downbeat and re-seat the
    terminal anchor from the fitted BPM (`reseatTerminalAnchor`); on an authored map, refit only
    the enclosing anchor span. Handlers live in `tempo_handlers.cpp`.
  - UI: live BPM/jitter readout near the transport while capture is active; tap key via
    `EditorView::keyPressed` until plan 46.
- **Public-header impact:** none.
- **Testing:** pure fit tests (synthetic tap trains with jitter and outliers → BPM within
  tolerance; gap reset; short-train refusal), apply-transform tests (default-map rough-in
  produces a valid two-anchor map covering the audio; span-scoped apply leaves other spans
  bit-identical), undo round-trip.
- **Exit criteria:** from a bare default map, playing the song and tapping yields a serviceable
  starting map in one gesture.
- **Verification:** build + touched tests + clang-tidy.

### Phase 5 — Offline onset analysis and snapping (assumes Q2 = A)

- **Scope:**
  - Pure kernel in `rock-hero-common/core`
    (`include/rock_hero/common/core/analysis/onset_detection.h`, `src/analysis/…`): spectral
    flux over caller-supplied mono PCM frames + adaptive-threshold peak picking → onset times in
    seconds. Header is public because its consumer (`rock-hero-common/audio`) is outside the
    library. Pure standard C++, deterministic, no JUCE.
  - Decode adapter free function in `rock-hero-common/audio` (new
    `include/rock_hero/common/audio/song/audio_onsets.h` beside `audio_normalization.h`,
    following the `analyzeAudioForGainNormalization` precedent):
    `analyzeAudioOnsets(path) -> std::expected<std::vector<double>, …>`; JUCE decode stays in
    the implementation file.
  - Editor integration: compute onsets for the active arrangement's audio once, off the message
    thread via the existing editor tasks/busy infrastructure (no modal overlay — background
    fill, feature simply inactive until ready); cache in session-scoped editor state (never
    persisted, never written into packages). Snapping toggle: when enabled, anchor drags
    (Phase 3) and tap-derived anchor seconds (Phase 4) snap to the nearest onset within a
    +/-30 ms window (tunable constant), then to the millisecond grid. Optional faint onset ticks
    on the waveform row behind an on/off toggle.
- **Public-header impact:** +1 common/core header, +1 common/audio header — both justified by
  cross-library consumers (constraint (b)).
- **Testing:** kernel unit tests on synthetic signals (clicks/plucked-envelope ramps at known
  times; assert detection within one hop); adapter test decoding a small self-generated FLAC
  fixture (CI-safe — the commercial-content corpora stay local-only and are never used in
  tests); editor tests for snap-window behavior and toggle-off passthrough.
- **Exit criteria:** dragging near a transient lands on it; tap rough-ins tighten measurably on
  the fixture; disabling the toggle restores raw drag behavior exactly.
- **Verification:** build + touched tests + clang-tidy.

### Phase 6 — Time-signature editing (decision-gated on Q1; phases below assume outcome A)

Do not start before Q1 is answered. If the user picks B or C, replace the transform step with
the corresponding policy and re-derive the tests; the UI step survives all outcomes.

- **Scope:**
  - Actions `SetTimeSignature{measure,numerator,denominator}` and
    `RemoveTimeSignature{measure}` with format-rule validation (measure >= 1, positive numerator,
    power-of-two denominator — same rules the reader enforces, now shared via
    `tempo_map_rules.h`).
  - Pure re-addressing transform `rock-hero-editor/core/src/timeline/signature_edits.{h,cpp}`:
    map every position token (anchors; per-arrangement chart notes, shapes, fhps, sections; tone
    regions) to its global-beat position (+ fraction) under the old signature list, then
    re-address under the new one. Anchors sit on integer global beats, so re-bucketing always
    yields valid on-beat tokens; anchor seconds are untouched, so **the transform is
    time-preserving**: every item resolves to identical absolute seconds before and after. The
    old terminal downbeat may stop being a downbeat under the new bucketing —
    `reseatTerminalAnchor` repairs it deterministically.
  - Undo for signature edits is a **full-state memento** of the tempo map plus all
    position-bearing content slices of the open song (bounded at worst a few hundred KB;
    constraint (f) explicitly prefers coherent full-state restore over granular replay).
  - UI: click/context on the signature band opens a small numerator/denominator popover at that
    measure; measure renumbering downstream is automatic because ruler labels derive from the
    map; flash-highlight the renumbered range once so the shift is visible rather than silent.
  - Coordinate with docs/roadmap/42-chart-validation.md: post-edit content validation (if any
    residual issues are possible under the chosen outcome) reports through 42's rule set, not a
    plan-local validator.
- **Public-header impact:** none intended (transform and edits stay `src/`-private).
- **Testing:** property tests over randomized maps + content (round-trip A→B→A restores tokens
  exactly; time-preservation invariant: resolved seconds identical within exact fp equality
  since anchor seconds and quarter positions are untouched); terminal re-seat cases; memento
  undo restores the full song slice field-equal; UI popover tests in the harness.
- **Exit criteria:** meter can be edited on a fully populated chart with zero audible/temporal
  content shift and single-step undo.
- **Verification:** build + touched tests + clang-tidy.

## Final acceptance phase

Run from the repo root as separate invocations, in this order, after the last phase lands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets clang-tidy
pre-commit run --all-files
```

Manual acceptance script (local corpus, never CI): open a GP-imported package, drag one anchor
against a clear transient, undo, redo; start from a default-map project, tap in the tempo, snap
two anchors to onsets, add a 7/8 measure mid-song (post-Q1), save, reopen, confirm the reader
accepts the package unchanged (`formatVersion` untouched at 1).

## Rollback/abort notes

- Every phase is additive and format-neutral: no migration, no load-normalizer changes, no
  package rewrites on open. Reverting any phase's commits fully removes it; saved packages
  produced meanwhile remain valid version-1 packages readable by older builds.
- Phase 1a is the only common/core surface change; if the shared-rules extraction fights the
  package error model, fall back to a thin internal forwarding shim and keep the public header
  minimal — do not fork validation logic in the editor.
- Phase 3 drag risk is contained by design: previews live in view state only; the model mutates
  once on mouse-up; Escape abandons with zero history impact.
- Phase 5 output is derived, cached, and never persisted — the feature can be deleted outright
  with no package or settings impact. If kernel quality disappoints on real material, ship
  Phases 1–4 without snapping and revisit Q2 option B.
- Phase 6 is the risky phase: a re-addressing bug corrupts charts silently. Land it only behind
  the property-test suite above; the full-state memento makes user-level recovery a single
  Ctrl+Z. If the time-preservation invariant cannot be made provably green, STOP and re-raise Q1
  rather than shipping an approximate transform.
