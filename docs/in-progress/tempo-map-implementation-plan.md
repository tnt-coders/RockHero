# Tempo Map Implementation Plan

Status: in progress (planning). First slice is read-only tempo-map loading and display; tone
changes, tempo editing, and live-rig switching are deliberately out of scope.

## Goal

Add a project-owned tempo map to the song model, load and save it with the project/song package,
and display a beat/bar-aware timeline in the editor. This gives future tone-change authoring a
musical grid to snap and label against without making the tempo map the runtime source of truth for
instant rig switching.

Runtime playback and future rig switching must still schedule against the audio transport timeline
in seconds/sample time. The tempo map is authoring and conversion data layered over that timeline.

## Current Grounding

- `Song` currently owns metadata and arrangements only.
- Arrangement notes and transport positions use `TimePosition`/`TimeDuration` in seconds.
- `.rhp` projects write editor-only state to root `project.json` and shared song data to
  `song/song.json` through the native `.rock` package serializer.
- `Session::timeline()` is derived from the selected arrangement audio duration. Tempo map data
  should not define the playable timeline length.
- `EditorViewState::visible_timeline` is currently the full displayed timeline range, and the
  scroll/zoom state lives in `EditorView::TrackViewport`.
- `common/audio` owns the Tracktion `Edit`; Tracktion's `Edit::tempoSequence` supports tempos,
  time signatures, and time/beat conversion, but Tracktion types must stay behind the audio
  adapter boundary.

## Design Decisions

- **Store the tempo map on `Song`, not `Arrangement`.** The displayed arrangement lanes share one
  transport, tempo map, and time-signature sequence. Arrangement-specific audio is allowed, but the
  musical timeline is song-level.
- **Persist positions in absolute song seconds.** Rock Hero's backing audio is fixed, and the
  transport is the timing source. The tempo map derives musical positions from seconds; it does not
  remap audio.
- **Include time signatures in the first model.** A tempo-only model can draw beats, but it cannot
  label bars correctly. Start with minimal time-signature support rather than bolting it on later.
- **Default missing maps to 120 BPM, 4/4 at 0 seconds.** This keeps existing fixtures and project
  files loadable without a migration path. The default matches Tracktion's built-in default.
- **Keep Tracktion synchronization as an adapter step.** Core owns the persisted model and
  conversion rules. `common/audio` translates that model into `Edit::tempoSequence` on the message
  thread during project load/setup.
- **Keep first UI pass read-only.** The editor should render beat/bar grid lines and tempo/time
  signature markers, but not allow tempo editing yet.

## Target Core Model

Add small value types under `rock-hero-common/core/include/rock_hero/common/core/`:

```cpp
struct BeatPosition
{
    double beats{0.0};
};

struct TempoEvent
{
    TimePosition position;
    double bpm{120.0};
};

struct TimeSignatureEvent
{
    TimePosition position;
    int numerator{4};
    int denominator{4};
};

class TempoMap
{
public:
    [[nodiscard]] static TempoMap defaultMap();

    [[nodiscard]] const std::vector<TempoEvent>& tempos() const noexcept;
    [[nodiscard]] const std::vector<TimeSignatureEvent>& timeSignatures() const noexcept;

    [[nodiscard]] double bpmAt(TimePosition position) const noexcept;
    [[nodiscard]] BeatPosition beatAt(TimePosition position) const noexcept;
    [[nodiscard]] TimePosition timeAt(BeatPosition beat) const noexcept;
};
```

Validation rules:

- At least one tempo event and one time-signature event.
- The first event of each sequence is at exactly 0 seconds after normalization.
- Event positions are finite, non-negative, sorted, and unique within their sequence.
- BPM is finite and in Tracktion's supported range: 20 to 300 BPM.
- Time-signature numerator is positive; denominator is a power of two in the practical range
  1 through 32.
- Missing JSON creates `TempoMap::defaultMap()`. Malformed present JSON is a load error.

Keep the implementation simple and deterministic. Linear scans are acceptable for the first
version; a five-minute song has only hundreds of visible beats. Add caching only if profiling or
imported maps prove it is needed.

## Persistence Format

Store the map in `song.json`, alongside `metadata`, `audioAssets`, and `arrangements`:

```json
{
  "formatVersion": 1,
  "metadata": {
    "title": "Song",
    "artist": "Artist",
    "album": "",
    "year": 2026
  },
  "tempoMap": {
    "tempos": [
      { "positionSeconds": 0.0, "bpm": 120.0 },
      { "positionSeconds": 42.5, "bpm": 138.0 }
    ],
    "timeSignatures": [
      { "positionSeconds": 0.0, "numerator": 4, "denominator": 4 }
    ]
  },
  "audioAssets": [],
  "arrangements": []
}
```

Do not put this in root `project.json`; that file is editor-only state such as the selected
arrangement. The tempo map is durable song content needed by both the editor and the game.

Do not bump `song.json` `formatVersion` solely for this optional field. A missing field has a
well-defined default. Only bump the version for a future incompatible schema change.

## Implementation Steps

1. **Core values and conversion**
   - Add tempo-map value headers and `tempo_map.cpp`.
   - Add `Song::tempo_map` defaulting to `TempoMap::defaultMap()`.
   - Add pure tests for default values, validation, `bpmAt`, seconds-to-beats, beats-to-seconds,
     tempo changes, and time-signature lookup.

2. **Song package persistence**
   - Read optional `tempoMap` in `rock_song_package.cpp`.
   - Write `tempoMap` from `Song` in `buildSongDocumentForSave`.
   - Preserve default behavior when old `song.json` files omit `tempoMap`.
   - Add package round-trip tests and negative malformed-map tests.

3. **Import support**
   - Keep imported songs valid with the default tempo map first.
   - Accept tempo maps written by external converters into `.rhp` or `.rock` song data.
   - Keep converter-specific source parsing outside this repository; RockHero validates and loads
     the resulting native tempo map only.

4. **Editor state exposure**
   - Add tempo-map state to `EditorViewState`, sourced from `session().song().tempo_map` when a
     project is loaded.
   - Keep `Session::timeline()` derived from selected arrangement audio duration.
   - Do not mark projects dirty for loading or displaying the map; future tempo editing will be an
     undoable project edit and will mark the project dirty.

5. **Editor UI display**
   - Add a read-only timeline ruler/grid component in `rock-hero-editor/ui`.
   - Host it in `TrackViewport` above the arrangement waveform and below the cursor overlay.
   - Draw bar lines, beat lines, and compact tempo/time-signature markers using the current
     content width and timeline range.
   - Keep density decisions presentation-local, but use `TempoMap` conversion helpers for all
     musical timing math.

6. **Tracktion adapter synchronization**
   - Use `ISongAudio::prepareSong` as the song-level timing setup point and update its
     documentation accordingly. It already receives the full `Song`, so no new public audio port is
     needed for this slice.
   - Translate `TempoMap` to `Edit::tempoSequence` inside `common/audio` on the message thread
     before playback starts.
   - Keep `setActiveArrangement` focused on selecting the prepared arrangement audio.
   - Add adapter tests around the project-owned boundary. Do not expose Tracktion tempo types in
     public core/editor headers.

## Testing Plan

- `rock-hero-common/core/tests/test_tempo_map.cpp`
  - default map is 120 BPM, 4/4 at 0 seconds.
  - validation rejects bad BPM, duplicate positions, negative positions, and invalid denominators.
  - conversion works across tempo changes in both directions.
  - bar/beat lookup respects time-signature changes.

- `rock-hero-common/core/tests/test_rock_song_package.cpp`
  - tempo maps round-trip through `song.json`.
  - missing `tempoMap` loads as the default map.
  - malformed present `tempoMap` fails with `InvalidSongDocument`.

- `rock-hero-editor/core/tests`
  - loaded view state carries the session song tempo map.
  - imported songs receive either parsed tempo data or the default map.

- `rock-hero-editor/ui/tests`
  - the ruler/grid component appears for loaded projects and is hidden for empty projects.
  - default 120 BPM maps beat/bar lines to expected x positions at the default zoom.
  - zooming and restored cursor focus still behave as they do now.

- `rock-hero-common/audio/tests`
  - the engine accepts prepared songs with valid tempo maps.
  - Tracktion synchronization remains behind `ISongAudio`/`Engine` and does not alter public model
    contracts outside the intended `prepareSong` documentation update.

## Non-Goals

- No tempo-map editing UI.
- No snapping behavior yet.
- No note-event conversion or chart authoring changes.
- No tone-change events, plugin automation authoring, or live-rig switching.
- No audio stretching, tempo remapping, or arrangement-duration changes caused by tempo edits.
- No durable `docs/design/` updates until the implementation shape proves itself and the user
  confirms the design is a durable architecture decision.

## Exit Criteria

- `Song` owns a validated, defaulted tempo map.
- `.rhp` and `.rock` song content persist the map in `song.json`.
- Existing projects without a `tempoMap` still open with a default grid.
- The editor shows a read-only beat/bar timeline for loaded projects.
- Tracktion tempo sequence setup is either implemented behind `common/audio` or explicitly left as
  the next adapter slice before any beat-aware automation work begins.
