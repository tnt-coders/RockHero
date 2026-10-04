# Plan 41 — Tempo Map Authoring

**Status:** Revised 2026-10-02 against `master @ 9e44e977`, after a simplicity review
(git history keeps the 2026-07-06 text); amended 2026-10-03 for sub-beat anchors and
whole-millisecond times (Non-goals, decisions 3 and 10). Phases 1–4 are
ready to build in order; Phase 6 (time-signature editing) follows on provisional 41-Q1 = A
(user, 2026-09-19), and G41-TS closes at its sighting. Phase 5 (onset snapping) and the tap-tempo
readout are deferred out of the first releasable editor.

## Goal

A charter starting from nothing but backing audio builds a beat grid that matches that audio,
from the keyboard first and the mouse second, inside the marker grammar every other marker kind
already follows: insert an anchor at a grid position, type the tempo of the span it opens, nudge an
anchor against a transient, delete one, and edit time signatures, all with one undo step per
gesture and no new key in the persisted song format. Today a usable tempo map exists only if the
project came from a GP import; this plan removes that dependency and unblocks the New chart entry
point (`docs/plans/roadmap/40-chart-editing.md` Phase 11).

## Non-goals

- No new song.json key and no chart format change. An anchor's position is the shared grid token
  every other position uses (`"measure:beat"`, or `"measure:beat+n/d"` between beats) and its
  time keeps the `"seconds"` key, written with three decimals from a whole number of
  milliseconds. A span's BPM is derived, never stored.
- *Ruled 2026-10-03 (user, after the Phase 3 sighting):* anchors may stand at any tick-lattice
  position, not only on a beat, and anchor time is held as whole milliseconds. This revision's
  first text kept anchors on-beat and put sub-beat anchors out of scope; every existing song
  loads unchanged under the new rule.
- No create-project flow; plan 40 Phase 11 owns it. `TempoMap::defaultMap` is the seed map this
  plan makes editable.
- No grid declutter (docs/plans/todo/tempo-grid-declutter-plan.md), no timeline-origin change
  (docs/plans/todo/timeline-origin-rethink.md), no audio time-stretching, no real-time detection
  (plan 22), no metronome click (plan 59 owns the count-in; a click track is a separate proposal).
- No grid lock. The 2026-07-09 toolbar lock guarded pointer authoring, which was retired
  2026-09-11; every tempo edit is now an explicit keystroke or drag, paused-only, one undo step
  (deleted by this revision, 2026-10-02).

## Constraints

- (a) **Layering**: common never depends on editor or game code; shared rules go to
  rock-hero-common first. Tracktion headers stay in rock-hero-common/audio implementation files.
- (b) **Public-header minimalism**: headers stay in `src/` until a consumer outside the library
  exists (docs/design/architectural-principles.md, "Placement Procedure for New Files").
- (f) **Undo**: RockHero-owned; every marker edit commits through `commitMarkerModel`
  (`rock-hero-editor/core/src/controller/marker_model_commit.h`).
- (h) **Builds** go through `.agents/rockhero-build.ps1`; each phase runs only the checks its
  changes warrant. clang-tidy runs only when the user asks.

Hard invariants, enforced by the reader and preserved by every verb: tempo changes only at
anchors and is constant inside a span; anchors stand on the tick lattice and their times are
whole milliseconds; the first anchor is `1:1`; the terminal anchor is on a downbeat (no sub-beat
offset) past content; anchors strictly increase in position and time.

## Current state (verified 2026-10-02)

- **Model**: `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h`
  (`BeatAnchor`, `TimeSignatureChange`, `TempoMap`, `TempoMap::defaultMap`; Phase 1 added
  `spanQuarterLength` and `terminalCovering`, Phase 3 deleted `quarterNoteBpmAtSeconds`). Since
  2026-10-03 a `BeatAnchor` is `{GridPosition position; int64 milliseconds}`: `seconds()` is the
  one conversion and `nearestAnchorMilliseconds` the one rounding.
- **Validation**: `validateTempoMap` is package-private
  (`rock-hero-common/core/src/package/rock_song_package_format.h:45`), called by read
  (`rock_song_package_read.cpp:420`) and write (`rock_song_package_write.cpp:581`). The tone
  track's rules are the precedent for sharing: `validateToneTrackRules` (`tone_track_rules.h`)
  is public and the package translates its errors.
- **Already live** (no work here): `TempoAnchorSelection`, `MarkerRow::Tempo`, chip select, the
  `↑/↓` walk and `Tab` over the tempo row (`marker_row_handlers.cpp`), the letter
  `g_tempo_key = 'b'` and the `Ctrl+Shift+B` jump (`editor_command_registry.cpp`). `Delete`,
  `Enter` and `Alt+←/→` on a tempo chip were inert at this baseline; Phases 2–3 gave them their
  verbs.
- **Marker funnel**: `commitMarkerModel` (`marker_model_commit.h:64-91`) with the section
  (`section_handlers.cpp`) and hand-marker (`fret_hand_handlers.cpp`) kinds as precedents; the
  8-step new-kind checklist is `docs/plans/in-progress/marker-verb-grammar.md`, "What a NEW marker
  kind must do".
- **Engine caches derived from the map**: tone automation is stored musically in `song.json`
  (`docs/plans/completed/tone-parameter-automation-plan.md`, settled 2026-07-07); the engine holds
  a seconds-domain copy (`rebuildDerivedToneCurves`, `tone_automation_edits.cpp:66`) and a one-way
  host-tempo mirror for plugins (`ISongAudio::mirrorTempoMap`). Both are refreshed only at rig
  load (`project_handlers.cpp:586-597`). The funnel and `MarkerModelEdit::apply` refresh neither,
  so **a tempo edit without Phase 2's resync would play automation at the old times and give
  plugins the old tempo**. The 2026-07-06 claim "tempo-map edits require no engine change" was
  made false by tone automation.
- **Chip text**: `timeline_ruler.cpp:834-837` printed `quarterNoteBpmAtSeconds` to two decimals
  (Phase 3 replaced it with `tempoSpanBpmText`).
- **Zoom** reaches the core through `onTimelineZoomChanged(pixels_per_second)`
  (`editor_view.cpp:576`, `editor_controller.cpp:1574`).
- **Undo runs**: the chart's `Alt`+arrow moves already coalesce a run of presses on one selection
  into one entry, ended by any other commit point (`commitChartGestureStep`,
  `chart_handlers.cpp:2992`); marker moves push one entry per press.
- **Prompt**: the section rename uses `showThemedTextPrompt` (`editor_view.cpp:3618`).

## Decisions

Earlier decisions that stand:

1. The warp-anchor storage model is kept; design within it
   (docs/plans/completed/tempo-map-implementation-plan.md; docs/design/architecture.md,
   "Song Data Model").
2. Tempo is constant inside each span (metronome-linear); BPM is quarter-note-referenced.
3. Anchor time is held as whole milliseconds (user, 2026-10-03; until then seconds rounded to the
   millisecond grid), and the file writes it as `"seconds"` with three decimals.
4. The grid is TRUTH (user, 2026-09-19): content holds its musical position and moves in time
   when the map changes; nothing is re-addressed to keep a second.
5. Undo is RockHero-owned; Tracktion is a playback backend and stays tempo-inert apart from the
   one-way host mirror.
6. Anchor moves are free-time: never grid-snapped, because the anchor defines the grid.

Rulings of 2026-10-02 (user, after the simplicity review):

7. **Two verbs, two causes.**
   - **Time is the cause — `Alt+←/→` and the pointer drag MOVE the anchor.** Its seconds change,
     every other anchor stays pinned, and the two adjacent spans' BPMs follow. This is the
     audio-sync gesture.
   - **Tempo is the cause — typing a BPM RIPPLES.** The span the anchor opens takes the typed
     BPM; the anchor stays put; every later anchor, the terminal included, shifts by the same
     whole-millisecond amount, so every later span keeps its BPM exactly. This is the
     click-mapping gesture. It is the only typed-BPM behaviour and gets no second key.
   - The other two outcomes are reachable with these: to keep later anchors where they are,
     `Alt`+arrow the NEXT anchor (its span absorbs); to shift the whole map against the audio
     keeping every tempo, `Alt`+arrow `1:1` and retype span 1's BPM. A "ripple only up to the next
     synced anchor" rule was rejected: nothing in an anchor says whether it was synced, so it
     would need a stored flag.
8. **The nudge step is one on-screen pixel at the current zoom, at least 1 ms, with no modifier.**
   Zoom is the precision control. `Ctrl` stays off placement verbs (retired 2026-08-23) and
   `Ctrl+Alt+←/→` stays unbound on markers. A step onto or past a neighbour, or `1:1` below 0 s,
   is REFUSED, never clamped — the marker law.
9. **One held nudge is one undo entry, for every marker kind.** A run of `Alt+←/→` presses on one
   selected marker coalesces into one entry, however fast or slow the presses, until any other
   commit point (another edit, a selection change, an undo or redo, a save) — the rule notes
   already use. No timed settle window: a timer would split the slow, careful run. A run that walks
   back to where it began leaves no entry.
10. **`Ctrl+B` follows `Ctrl+M`.** On an empty slot it inserts an anchor, pinning the time the map
    already gives that slot (time-neutral to 0.5 ms), selects it and opens no prompt. On a slot
    where an anchor stands it selects that anchor and opens the BPM prompt, selected beforehand or
    not. `Enter` and a double-click on a selected chip open the same prompt. The cursor is read at
    the placement quantum — the grid note value with snap on, the 1/3840-whole-note tick with snap
    off, the tone chord's rule — and the ruler's "Add Tempo Anchor" reads its click the same way
    (user, 2026-10-03; until then both were floored to the beat). Inert while playing, with no
    song, and at or past the terminal. No digit-to-prompt for now.
11. **The chip prints what was typed.** It prints the shortest decimal (0–2 places) whose span
    length lies within ±1 ms of the stored one, so typing 137 prints 137 and an insert into a span
    of a typed tempo leaves both halves printing it. (A span whose stored length is already most of
    a millisecond off its printed value can split into a half printing more decimals; no tolerance
    removes that, found in the Phase 1 review.)
12. **The terminal grows, never shrinks.** A faster typed BPM can pull the terminal before the end
    of the audio; the ripple then extends it to the first downbeat at or past the latest
    arrangement's audio end, at the last span's tempo.
13. **Mouse drag is the primary method for users** and is in scope (Phase 4), built after the
    keyboard sighting. It uses the move transform and commits once on release.
14. **Deferred**: the tap-tempo readout and onset snapping are out of the first releasable editor.

## Open questions

- **Q1 — Time-signature content policy.** PROVISIONALLY A (user, 2026-09-19): hold the global
  beat, measures renumber. Phase 6 builds A; G41-TS closes at its sighting. (B: keep literal
  tokens — destructive; C: forbid meter edits once content exists — hostile.)
- **Q2 — Onset analysis source** (deferred Phase 5). A: a self-written spectral-flux kernel in
  common/core (recommended); B: a third-party library; C: editor-local.
- **Q3 — answered 2026-10-02.** Tap tempo, when built, is a measurement: a live BPM readout the
  charter applies by typing. Tapping happens during playback, where marker edits are closed.

## Dependencies

- **Unblocks** plan 40 Phase 11 (New chart), which is gated on this plan's Phases 1–3.
- Plan 10: no new format key (a sub-beat anchor uses the shared grid token). A tempo edit changes
  chart identity by design.
- Plan 42: content validity after a meter edit reports through 42's rule set (Phase 6).
- Plan 59: the count-in reads the same `TempoMap`; no coupling.

## Phased implementation

Every new file follows the placement procedure; comments follow
docs/design/documentation-conventions.md. Each phase is a coherent commit (or small series), gets
a simplicity review before its commit, and verifies with a build and the touched tests.

### Phase 1 — Rules and pure transforms

- **1a (common/core).** Promote the tempo-map validation to
  `include/rock_hero/common/core/timeline/tempo_map_rules.h` (`validateTempoMapRules`, a
  project-owned error), with the package translating it exactly as the tone track does.
  Behaviour-preserving. Add a span-indexed accessor on `TempoMap` (the quarter-note length of
  span *i*), which also replaces the ruler's seconds → `upper_bound` → index round trip.
- **1b (editor/core).** One header beside `timeline/tempo_grid_geometry.h` holding pure functions
  over a `TempoMap`, each returning `std::expected` with a typed error and revalidating through
  the rules:
  - `insertTempoAnchor(map, position)` — pins the position's current time rounded to a whole
    millisecond; refuses an occupied position or one at or past the terminal.
  - `moveTempoAnchor(map, position, milliseconds)` — refuses onto or past a neighbour and `1:1`
    below 0.
  - `setTempoSpanBpm(map, position, bpm, audio_end)` — the ripple, with terminal growth (decision
    12); refuses a non-positive or non-finite BPM and a result the rules refuse.
  - `deleteTempoAnchor(map, position)` — refuses `1:1` and the terminal; neighbours stay pinned,
    so it is the exact inverse of an insert.
  - `spanBpmText(map, span)` — decision 11; the chip and the prompt's prefill both read it.
- **Tests.** `test_tempo_map_rules.cpp` (every invariant violated once; package tests stay green
  untouched); `test_tempo_map_edits.cpp`: insert is time-neutral; a ripple leaves every later
  span bit-identical and grows the terminal only when it must; typed N prints N; an insert's
  halves print the parent's value; every refusal; every result passes the rules.

### Phase 2 — Editor-core verbs

- **`TempoMapSnapshot`** through `commitMarkerModel`: capture / applyTo / normalize / validate
  (`validateTempoMapRules`) / equality, and `Session::setTempoMap`, which advances the revision
  the tab and highway projection caches key on. No inverse-command edits (the 2026-07-06 Phase 2
  is deleted); the verbs live in `tempo_handlers.cpp` beside their actions, as the section's do.
- **`syncTempoDerivedAudio()`** — the one authority for the engine's tempo-derived caches. It
  keeps the map it last pushed; when the session's map differs it calls
  `rebuildDerivedToneCurves` and `mirrorTempoMap` through `pushTempoDerivedAudio()`. Called from
  the funnel's shared tail and once when an undo or redo transition completes; rig load calls the
  push unconditionally, before the project's audio is marked ready. Phase 6's meter edits inherit
  it.
- **The verbs**, through the existing dispatches (checklist steps 1–8): a published `Ctrl+B`
  chord target (nothing / restate-with-prompt / insert, the section's shape), a set-tempo arm in
  `restateTarget()`, and tempo arms in `Delete` and `MoveSelection`. The move step reads the
  reported zoom: `max(1 ms, round(1000 / pixels_per_second) ms)`, 1 ms before any zoom is
  reported. A landed move ends with `followMovedMarker`, so the cursor rides the anchor.
- **Run coalescing for every marker kind** (decision 9): generalize the chart's gesture
  authority over the funnel, so a marker move continuing a run on the same selection widens the
  top entry (`replaceUndoTop`) instead of pushing; any other commit point ends the run.
- **The cursor keeps its musical place** (decision 4): the paused cursor's authority is the
  transport's seconds, so Insert, Set Tempo and Delete capture the cursor's musical position before
  the commit and seek back to it after; undo and redo already seek through their focus, and a move
  rides its anchor.
- **Tests** (controller suites): each verb and refusal; `Ctrl+B` twice prompts; undo and redo
  restore the map exactly AND re-derive the automation curves and host tempo (the
  `ConfigurableSongAudio.last_mirrored_tempo_map` fixture); a held run is one entry for a
  section, a hand marker and a tempo anchor; a run ends at a selection change.

### Phase 3 — Keyboard UI, then the sighting

- Register the `Ctrl+B` author chord from `g_tempo_key` (`markerAuthorChord`).
- The BPM prompt (`showThemedTextPrompt`), opened by the published verb from `Ctrl+B`, `Enter`
  and a double-click; prefilled from `spanBpmText`.
- Chip text from `spanBpmText`, deleting the ruler's private formatting.
- Ruler right-click rows over the tempo row — "Add Tempo Anchor", "Set Tempo…", "Delete Tempo
  Anchor" — disabled while playing, as the section menu's are.
- Keymap, marker-grammar and developer-guide rows (below).
- **Sighting**, in the running editor: from a default map, `Ctrl+B` at bar 1 and type 137; the
  chip prints 137 and the grid follows the click. Insert a mid-song anchor and nudge it onto a
  transient at a close zoom, then at a far zoom; undo the held nudge in one step. Type a BPM
  mid-map and confirm later spans keep their tempo. Delete an anchor. Confirm tone automation
  plays at its musical place after each edit and after undo.

### Phase 4 — Pointer chip drag

The primary method for most users (decision 13). Dragging a tempo chip previews the move,
commits ONE `moveAnchorTo` on release, and `Esc` cancels with no history. The same refusals hold;
a refused drop snaps back. JUCE's drag threshold keeps a chip click a click. Settle by sighting
whether the selected anchor's line over the waveform needs emphasis (the track already draws
every beat line, but a sub-beat anchor stands on none). Sighting: drag an anchor onto a transient at two zooms; undo is one step.

### Deferred — tap-tempo readout (out of the first releasable editor)

A pure least-squares fit over tap times read from the transport during playback, reset after a
2 s gap, shown as a live BPM readout; the charter pauses and types it (Q3). Needs a key that works
while playing, chosen by the user when built. No edit verb of its own.

### Phase 5 — Offline onset analysis and snapping (deferred; assumes Q2 = A)

*Deferred 2026-10-02: out of the first releasable editor. Re-verify against the code before
building; the snap applies to Phase 4's drag and Phase 2's nudge, not to tap-derived anchors.*


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
    +/-30 ms window (tunable constant), then to a whole millisecond
    (`nearestAnchorMilliseconds`). Optional faint onset ticks on the waveform row behind an on/off
    toggle.
- **Public-header impact:** +1 common/core header, +1 common/audio header — both justified by
  cross-library consumers (constraint (b)).
- **Testing:** kernel unit tests on synthetic signals (clicks/plucked-envelope ramps at known
  times; assert detection within one hop); adapter test decoding a small self-generated FLAC
  fixture (CI-safe — the commercial-content corpora stay local-only and are never used in
  tests); editor tests for snap-window behavior and toggle-off passthrough.
- **Exit criteria:** dragging near a transient lands on it; tap rough-ins tighten measurably on
  the fixture; disabling the toggle restores raw drag behavior exactly.
- **Verification:** build + touched tests + clang-tidy.

### Phase 6 — Time-signature editing (on provisional Q1 = A; phases below assume outcome A)

*Re-verify before building (2026-10-02): the actions and memento below predate the marker funnel.
The meter is a marker kind (`Ctrl+/`), so it goes through the 8-step checklist and
`commitMarkerModel` with a snapshot spanning the map and every position-bearing slice;
`syncTempoDerivedAudio` (Phase 2) already covers its engine refresh; and `reseatTerminalAnchor`
below means re-seating the terminal on a downbeat, a pure function beside Phase 1's transforms.*


May start on provisional 41-Q1 = A (user, 2026-09-19); G41-TS closes at this phase's sighting. If
that sighting picks B or C instead, replace the transform step with the corresponding policy and
re-derive the tests; the UI step survives all outcomes.

- **Scope:**
  - Actions `SetTimeSignature{measure,numerator,denominator}` and
    `RemoveTimeSignature{measure}` with format-rule validation (measure >= 1, positive numerator,
    power-of-two denominator — same rules the reader enforces, now shared via
    `tempo_map_rules.h`).
  - Pure re-addressing transform `rock-hero-editor/core/src/timeline/signature_edits.{h,cpp}`:
    map every position token (anchors; per-arrangement chart notes, shapes, fhps, sections; tone
    regions; **plus the position tokens added since this list was written — the armed chart
    caret's grid address, in-session and in its persisted `caret:` marker token, plan 47's
    loop-selection GridPosition pair, and (2026-09-14) the editor's own marker selections
    (`TempoAnchorSelection`'s GridPosition, `TimeSignatureSelection`'s measure, a
    `SongSectionSelection`'s position) together with `ChartCursor::column`, which the
    author-at-cursor grammar makes a marker-verb input** (2026-07-18 fold-in audit: without these
    the caret and loop land on different musical content after a signature edit while everything
    else is time-preserved)) to its global-beat position (+ fraction) under the old signature list,
    then
    re-address under the new one. Anchors re-address on the whole-note axis exactly like all other
    content and stay on the tick lattice (since 2026-10-03 an anchor may stand between beats, so
    an on-beat token is no longer guaranteed or needed); anchor times are untouched, so **the
    transform is time-preserving**: every item resolves to identical absolute seconds before and
    after. The
    old terminal downbeat may stop being a downbeat under the new bucketing —
    `reseatTerminalAnchor` repairs it deterministically.
  - Undo for signature edits is a **full-state memento** of the tempo map plus all
    position-bearing content slices of the open song (bounded at worst a few hundred KB;
    constraint (f) explicitly prefers coherent full-state restore over granular replay).
  - UI: click/context on the signature band opens a small numerator/denominator popover at that
    measure; measure renumbering downstream is automatic because ruler labels derive from the
    map; flash-highlight the renumbered range once so the shift is visible rather than silent.
  - **Insert chord (2026-09-12, re-ruled 2026-09-14):** `Ctrl+/` reads the cursor and never the
    selection. A meter standing on the cursor's measure downbeat — the downbeat of the measure the
    cursor is IN, read from its tick — is RESTATED, reopening the numerator/denominator prompt;
    otherwise one is INSERTED there. Inert while playing and with no song. The click/context
    popover above is the pointer form of the same two verbs, and `Ctrl+Shift+/` is the signature's
    select chord. The whole phase runs on provisional Q1 = A; G41-TS closes at its sighting.
  - Coordinate with docs/plans/roadmap/42-chart-validation.md: post-edit content validation (if any
    residual issues are possible under the chosen outcome) reports through 42's rule set, not a
    plan-local validator.
- **Public-header impact:** none intended (transform and edits stay `src/`-private).
- **Testing:** property tests over randomized maps + content (round-trip A→B→A restores tokens
  exactly; time-preservation invariant: resolved seconds identical within half a tick (see the
  tick-lattice note below), since anchor seconds are untouched); terminal re-seat cases; memento
  undo restores the full song slice field-equal; UI popover tests in the harness.
  - **The tick lattice (2026-09-23).** Every stored position must be a whole number of ticks
    (1/3840 of a whole note) and validation refuses one that is not. A global-beat position
    "plus fraction" re-read under a larger denominator is not: an odd tick of a quarter-note beat
    is half a tick of an eighth-note one. The transform therefore re-addresses on the whole-note
    axis (`wholeNotePosition` / `advanceGridPositionByWholeNotes`, `grid_arithmetic.h`), which a
    denominator change does not bend, and rounds any residue through `nearestTick`; the
    time-preservation invariant then holds to within half a tick rather than exactly, which the
    property tests state. A denominator the lattice cannot hold (512 and up) is already refused
    by the package validator.
- **Sighting (added 2026-09-23, from the lattice follow-up's own sighting, which did not get a
  close look at this):** with a signature change authored, look closely at content that crosses
  the change: move a note carrying a bend or slide across the barline both ways and confirm its
  ring length and keyframe timing stay put musically; with snap off, tick-step a keyframe that
  sits in the later meter; convert a short note just before the change into a pick slide and
  confirm it rings a quarter note; place notes on an odd grid (`1/28`) on both sides of the
  change and confirm consecutive placements touch. Each of these was refused or off the lattice
  before `a6455969`, and the meter edit is the first verb that can create such content without
  an import.
- **Exit criteria:** meter can be edited on a fully populated chart with zero audible/temporal
  content shift and single-step undo.
- **Verification:** build + touched tests + clang-tidy.

## Final acceptance

After the last release phase (4), as separate invocations: a build, the touched tests, and
`pre-commit run --all-files`; clang-tidy when the user asks. Manual script (local corpus, never
CI): open a GP-imported package, nudge one anchor onto a transient, type a BPM on another, undo
both and confirm tone automation returns to its timing; from a default-map project, `Ctrl+B` at
`1:1`, type the click's BPM, drag one anchor; save, reopen, and confirm the reader accepts the
package unchanged.

## Rollback/abort notes

- Every phase is format-neutral: no migration, no load normalizer, no package rewrite on open.
  Reverting a phase's commits removes it; packages saved meanwhile stay valid.
- Phase 1a is the only common/core surface change; if the shared rules fight the package error
  model, keep a thin translating shim, never a forked validator.
- Phase 4's drag previews in view state only and commits once; `Esc` leaves no history.
- Phase 6 is the risky phase: a re-addressing bug corrupts charts silently. Land it only behind
  its property suite; if the time-preservation invariant cannot be made provably green, STOP and
  re-raise Q1 rather than ship an approximate transform.
