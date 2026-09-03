# Plan 59 — Playback Count-In

## 1. Status

Roadmap, not started. Authored 2026-09-02 against `master @ 66088d03`. **Ungated** — the shape is
settled (the rejected lead-in region is recorded in §11) and 59-Q1..Q3 are build-session questions
carrying recommendations, not a decision gate. Re-verify the inventory below against the code
before execution.

## 2. Goal

Starting playback optionally counts the player in: **N measures (default 2) of metronome click at
the cursor's tempo and time signature, sounding before the transport rolls.** It works at **any**
cursor position — the song start, mid-song, mid-measure — and it changes only *when* audio starts,
never *where*. The transport begins exactly where the cursor already was.

## 3. Non-goals

- **No metronome during playback.** A persistent click track is a separate feature; it is the
  natural second consumer of whatever click renderer 59-Q1 picks, and nothing here should make it
  harder to add later.
- **No visual countdown.** The game's song-start count-in was answered visual-only
  (docs/plans/roadmap/27-in-song-flow-results-profiles.md, 27-Q2); this plan is audible and
  editor-side.
- **No recording.** The editor has no record path today; see 59-Q2.
- **No game-side count-in and no loop pre-roll.** Those stay with plans 27 and 28 — but as
  *consumers* of this plan's two authorities, never as second derivations (§6).
- **No tempo-map edits.** The count-in reads the map; it never writes it.
- **No new timeline content.** Nothing is inserted into the song, the chart, or the Tracktion edit.
- **No change to the marker model, the play position, or playback follow.**

## 4. Constraints

- **Layering.** The schedule is pure math and lives in `rock-hero-common/core`; the audible half
  sits behind the existing `rock-hero-common/audio` transport port; Tracktion headers stay in
  common/audio sources (docs/design/architectural-principles.md — "Ports and Adapters", "Core
  Position", "Time Must Be a Dependency").
- **Deadline path.** Click rendering happens on the audio callback: no allocation, no locks, no
  blocking work. The schedule is computed on the message thread and handed over pre-baked
  (docs/design/architecture.md — "Timing and Latency").
- **The click is a timing reference**, so it must carry the *same* output-latency compensation as
  the backing track (docs/plans/roadmap/13-audio-device-settings-and-calibration.md). A click that
  leads or lags the backing by the device offset teaches the player the wrong beat.
- **Never document state.** The count-in enters no undo history and no package — the same category
  as the resume cursor, grid density, and zoom.
- **Builds** through `.agents/rockhero-build.ps1`, as separate invocations, only where a change
  determinately warrants the check.

## 5. Current-state inventory (verified 2026-09-02 @ `66088d03`)

- **Nothing like this exists.** No metronome, click, or count-in anywhere in RockHero code. The
  word "metronome" appears only in tempo-map prose (the quarter-note axis) and the ruler's tempo
  chips. "Count-in" does appear once, in
  `rock-hero-common/core/include/rock_hero/common/core/song/audio_asset.h:46` — but as prose about
  *imported* audio, explaining why a synced backing track usually carries a negative
  `start_offset` (the recording opens with silence or a count-in the score omits). It names a
  property of somebody else's recording, not a feature of ours, so it is nothing to build on.
- **The play path** is `EditorController::Impl::performActionImpl(EditorAction::PlayPause)`
  (`rock-hero-editor/core/src/controller/editor_controller.cpp:2115`). Its play branch already
  resolves everything the count-in needs: it seeks to the armed caret through
  `TempoMap::secondsAtNote`, disarms the marker, clears the selection, settles legato, calls
  `activateToneAtCursor()`, and only then calls `m_transport.play()`. **The count-in inserts
  between the resolved start position and the roll** — it needs no position logic of its own.
- **`ITransport`**
  (`rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h`) has
  play/pause/stop/seek, playback speed, and the loop region, on a message-thread-only contract with
  a coarse `TransportState` snapshot plus listener. There is no count-in surface and no
  "about to start" state.
- **`TransportViewState`**
  (`rock-hero-editor/core/include/rock_hero/editor/core/transport/transport_view_state.h`) is three
  booleans: `play_pause_enabled`, `stop_enabled`, `play_pause_shows_pause_icon`. Counting-in is a
  fourth fact for this struct, not a new one somewhere else.
- **`TempoMap`** (`rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h`)
  already answers every question the schedule asks: `timeSignatureAtSeconds` (:126),
  `quarterNoteBpmAtSeconds` (:152), `beatPositionAtSeconds` (:138), `beatsPerMeasureAt` (:159),
  `secondsAtBeat` (:183), `globalBeatIndex` (:167), `beatAtGlobalIndex` (:174).
- **There is no negative time to rewind into.** Every seek is clamped into loaded content
  (`TimeRange::clamp`,
  `rock-hero-common/core/include/rock_hero/common/core/timeline/timeline.h:117`), and the song has
  no content before its origin. This is the fact that decides the mechanism: the count-in cannot be
  a rewind, so it must occupy real time before the roll (§7 decision 1).
- **Tracktion ships a count-in, and it is the wrong one** — verified in the vendored engine:
  - `Edit::CountIn { none, oneBar, twoBar, twoBeat, oneBeat }`
    (`external/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.h:708`) — a
    fixed five-value enum capped at two bars. There is no N.
  - Its length comes from `tempoSequence.getTimeSig(0)->numerator`
    (`.../model/edit/tracktion_Edit.cpp:2501-2510`) — the edit's **first** time signature, not the
    one at the cursor. In a meter-changing chart it counts the wrong number of beats.
  - It is implemented as a **negative pre-roll**: `prerollStart = ts.toTime(currentBeat -
    (numCountInBeats + 0.5))`, after which the playhead rolls in with loop times set to
    `{startTime, max}` (`.../playback/tracktion_TransportControl.cpp:1483-1517`). The transport
    really rolls across the span before the start point, so whatever lives there sounds — and at
    the song start that span is off the front of the timeline.
  - The clicks come from `ClickGenerator` (`.../playback/graph/tracktion_ClickNode.h:15-40`), which
    renders wave or MIDI clicks from `edit.tempoSequence` — but only while the **play head is
    rolling**, because the node that drives it returns before generating anything otherwise:
    `if (playHead.isUserDragging() || ! playHead.isPlaying()) return;` in `ClickNode::process`
    (`.../playback/graph/tracktion_ClickNode.cpp:291-296`). (`ClickGenerator::isPlaying()` at
    `:219-222` answers a different question — whether a click *sample* is mid-playback — and says
    nothing about the graph.) A count-in that holds the transport still therefore gets no clicks
    out of Tracktion at all, which is the second half of why 59-Q1 cannot simply adopt it.
  - Our edit's tempo sequence is a faithful **write-only mirror of the RockHero `TempoMap`, tempi
    and signatures both** (`rock-hero-common/audio/src/tracktion/tempo_mirror.h`, written at
    `tempo_mirror.cpp:100-145`, called from
    `rock-hero-common/audio/src/engine/engine_song_audio.cpp:242`). So Tracktion's click
    *times* would be right; only its count-length rule and its roll-backwards mechanism are wrong.
    That split is what makes 59-Q1 a real choice rather than a formality.

## 6. Dependencies and the ownership split

- **docs/plans/roadmap/27-in-song-flow-results-profiles.md** — 27-Q2 chose a visual song-start
  count-in for the game; 27-Q3's **resume pre-roll** keeps the audible click. Downstream consumer
  of this plan's schedule.
- **docs/plans/roadmap/28-practice-mode.md** — 28-Q5's loop pre-roll ("one full measure, min 1.5 s,
  count-in click on by default") is this schedule at a loop start with N = 1. Downstream consumer.
- **docs/plans/roadmap/47-editor-loop-selection.md** — its non-goal list currently points *every*
  count-in and pre-roll at plan 28. That pointer predates this plan. The split is: **the editor
  playback count-in is here; the game and practice pre-rolls stay theirs and call these
  authorities.** Amend 47's line when 47 next executes, and do not restate the schedule rule in
  either place.
- **docs/plans/roadmap/20-game-architecture-and-render-stack.md** Phase 2 — the game resource pack
  (`resources/sfx/`, count-in click asset). Game-side only; the editor has no resource pack, which
  is half of 59-Q1.
- **docs/plans/roadmap/13-audio-device-settings-and-calibration.md** and
  **docs/plans/roadmap/48-editor-audio-setup.md** — the output-latency offset the click must share
  with the backing track.
- **docs/plans/roadmap/12-playback-clock.md** — during the count-in the transport is *not* rolling,
  so the clock keeps publishing the cursor and every clock consumer (timeline cursor, 3D preview,
  tab lane) correctly sits still. Stated here so nobody adds a second "about to play" time source.
- **docs/plans/roadmap/41-tempo-map-authoring.md** — the count-in reads the map. A tempo edit
  *during* a count-in is not a case: the count-in is a brief modal moment and its schedule is
  frozen when it starts.
- **docs/plans/roadmap/46-editor-keybinds.md** and
  **docs/plans/roadmap/53-editor-keyboard-and-pointer-completion.md** — any key or toggle this plan
  ships registers on the command registry, never scattered.

## 7. Decisions already made (the settled shape)

1. **The count-in is a property of starting playback, not a region of the song.** It occupies real
   time before the transport rolls and inserts nothing anywhere. This is exactly what lets it work
   at any cursor position, including the origin (§11 is the alternative that could not).
2. **Cursor-frozen tempo and meter, real-downbeat phase.** The beat *period* comes from
   `quarterNoteBpmAtSeconds` at the cursor; *beats per measure* from the signature at the cursor;
   the *phase* from the cursor's own measure downbeat as the map reports it, stepped backwards in
   frozen beats. Two facts make this the only rule that works everywhere: at the song start there
   is no map before the origin to read at all, and inside a ritardando (or across a meter change
   just behind the cursor) the beats the player must hear are the ones they are about to play, not
   the ones being left behind. Where nothing changes inside the count-in window — the overwhelming
   majority — this degenerates exactly to "the N real measures before the cursor".
   *Rejected:* a phantom grid anchored to the cursor itself (accents at cursor − k·M beats), which
   would accent a beat that is not the music's beat 1.
3. **A mid-measure cursor is counted in from a real downbeat.** The count runs N measures plus the
   cursor's offset into its own measure, so accents stay on real downbeats and the player comes in
   on the beat they are actually sitting on — what a human counting them in would do.
4. **The count-in changes when audio starts, never where.** The play position is resolved exactly as
   it is today, before the count-in begins.
5. **One schedule authority, in common/core.** `makeCountInSchedule(tempo_map, cursor, measures)` is
   pure and total; the editor, plan 27's resume pre-roll, and plan 28's loop pre-roll all call it
   rather than each deriving beats. A second derivation would be precisely the "one rule stated
   twice" defect this project keeps finding.
6. **One transport authority for "audio is rolling, and from where".** The count-in start is a
   transport-port operation and the counting-in fact rides in `TransportState`, so view state,
   enabledness, and any future consumer learn it from the snapshot they already read.
   *Rejected:* a separate count-in player orchestrated by a message-thread timer in editor core —
   two components would then have to agree by hand about when playback begins, and message-thread
   jitter would land the roll off the beat the player just heard.
7. **Cancellable, and cancelling costs nothing.** Play/Pause, Stop, or a seek during the count-in
   cancels it and leaves the cursor exactly where it was. Nothing was consumed, so there is no
   state to restore.
8. **N = 2 measures by default**, and **N = 0 means no count-in** — the same code path with an empty
   schedule, so there is no separate "enabled" flag that could disagree with N.

## 8. Open questions for the build session

1. **59-Q1 — click sound source.** (A) Reuse Tracktion's click assets through our own scheduler
   (`Click::getClickWaveFile` / MIDI click notes, `tracktion_ClickNode.h:15-20`) — free and already
   present, but it inherits engine-owned files and an engine settings path that is not ours.
   (B) Ship an editor-side sample pair (accented / unaccented) — the editor has no resource pack, so
   this question is really "where does an editor asset live, and who licenses it". (C) Synthesize
   the tick in code: a short enveloped burst, two pitches for accent and beat — no asset, no path,
   deterministic under test. **R: C for the editor**, with the shared-asset question deferred until
   plan 20's pack gains an editor-visible home. Either way the **accent on beat 1 is not optional**:
   a count-in with no accent does not tell the player where the measure is.
2. **59-Q2 — do recording and practice modes differ?** The editor has no record path today, and the
   game's pre-rolls belong to plans 27 and 28. **R: make the count-in a parameter of the start**
   (measures, plus on/off, supplied by the caller) rather than a global mode, so a future record
   button, plan 27's resume, and plan 28's wrap each pass their own N with no second policy object.
   Confirm at sign-off that a practice-loop wrap wants this same schedule (28-Q5's "one full
   measure" is just N = 1) and that the wrap's own policy is decided there, not here.
3. **59-Q3 — the setting surface for N.** (A) An app-local `IEditorSettings` value like grid, zoom,
   and the resume cursor — the plan-47 Q3 precedent: one flat key per family, a composite token when
   a family carries several scalars. (B) A transport-strip toggle for on/off beside the setting for
   N. (C) A modifier on the play key (plain Space plays, Shift+Space counts in) with no persistent
   state at all. **R: A for N plus B's toggle for on/off**, with C recorded as a compatible
   addition — but **decide the default explicitly**: a count-in on every Space press is intrusive
   while authoring, so the toggle's default state is part of this answer.

## 9. Phased implementation

Command forms (from `.agents/README.md`, run from the repository root):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -Targets all
powershell -NoProfile -ExecutionPolicy Bypass -File .\.agents\rockhero-build.ps1 -RunTouchedTests
```

### Phase 1 — The schedule, pure, in common/core

`CountInSchedule` — the clicks (offset from the pre-roll start, accented or not) and the total
pre-roll duration — plus `makeCountInSchedule(tempo_map, cursor, measures)` implementing §7
decisions 2, 3, and 8. Headless, no audio, nothing audible yet.

Tests, each pinning a decision rather than an implementation: cursor at the origin; cursor
mid-measure (count = N measures plus the offset, accents on real downbeats); a meter change
immediately behind the cursor (frozen meter proven — the count uses the cursor's numerator); a
tempo change immediately behind the cursor (frozen period proven); an odd meter; N = 0 yielding an
empty schedule and zero duration; N = 2 as the default shape.

### Phase 2 — Port surface and adapter

The count-in start on the transport port, the counting-in fact in `TransportState`, and the audio
side: 59-Q1's click source rendered from pre-baked buffers on the callback, sample-accurate
placement from the schedule, the roll beginning exactly one beat after the final click, and the
same latency compensation the backing track gets. Every transport test double gains the new surface
— plan 47 records **six** doubles at its time of writing; count them again rather than trusting
that number. Adapter tests: schedule-to-sample placement, the handoff into the roll, and cancel.

### Phase 3 — Editor wiring

`PlayPause` calls the count-in start instead of `play()` when the count-in is on;
`TransportViewState` gains the counting-in fact and the controls render it; cancel semantics per §7
decision 7 (Play/Pause, Stop, and seek); the cursor and the 3D preview deliberately stand still
(§6, plan 12). `activateToneAtCursor()` already runs before the roll, so the first note sounds
through the right tone with no new ordering.

### Phase 4 — Setting surface

59-Q3's answer: persistence, the command-registry entry for any toggle or key, and the chosen
default.

### Phase 5 — Acceptance

The sanctioned bundle as separate invocations (build, touched tests, clang-tidy at the user's
trigger, pre-commit), plus the one check that cannot be automated: **by ear**, with the count-in on,
the last click and the first beat of playback are one beat apart — at the song start, mid-song, and
at a mid-measure cursor.

## 10. Runtime performance

Building the schedule is O(N × beats-per-measure) on the message thread, once per play. The click
renderer reads one pre-baked buffer per click on the audio callback and allocates nothing. Neither
belongs to a per-frame path.

## 11. The rejected alternative: a lead-in region at the song origin

Considered and rejected before this plan was written: a **non-editable two-measure lead-in region**
placed at the song origin and carried as real timeline content.

It was rejected because it needs several special states, all for one region:

- **ruler-only display** — it is not chart content, so no lane may draw it as though it were;
- **a non-editable span** in a timeline whose spans are otherwise all editable;
- **the first tone active across it but not automatable** — the tone lanes must not extend into it;
- **forced silence** — nothing in the song may sound there.

And in exchange for those four, it only helps at the song start. Press play in the middle of a song
— where a charter spends nearly all their time — and it does nothing at all. The count-in reaches
every position with **no new states**: it stores nothing, displays nothing, and reads a tempo map
that already exists. The region adds four special cases and the count-in adds none, so the burden
fell on the region to be the *more correct* design, and it was the less correct one.

## 12. References

- `rock-hero-editor/core/src/controller/editor_controller.cpp:2115` — the play path the count-in
  inserts into.
- `rock-hero-common/audio/include/rock_hero/common/audio/transport/i_transport.h` — the port that
  gains the count-in start.
- `rock-hero-common/core/include/rock_hero/common/core/timeline/tempo_map.h` — every query the
  schedule needs.
- `rock-hero-common/audio/src/tracktion/tempo_mirror.h` — why Tracktion's own click times would be
  correct even though its count-in is not.
- `external/tracktion_engine/modules/tracktion_engine/model/edit/tracktion_Edit.h:708`,
  `.../model/edit/tracktion_Edit.cpp:2501`,
  `.../playback/tracktion_TransportControl.cpp:1483`,
  `.../playback/graph/tracktion_ClickNode.h:15` — the engine count-in and click generator examined
  in §5.
