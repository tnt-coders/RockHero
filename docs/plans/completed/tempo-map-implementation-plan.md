# Tempo Map Persistence Implementation Plan

Status: in progress. This is the current song-format slice for the tone-system work. It covers the
song-level tempo map plus arrangement audio and tone-document references only.

Playable note, chord, tuning, technique, import, and gameplay chart storage is future work. Keep
those decisions out of this slice; they are now owned by
`docs/plans/in-progress/note-format-and-tablature-plan.md`.

## Goal

Persist the settled tempo map shape in `song.json` so tone automation and future grid-aware features
share one musical timeline without committing to a note model yet.

## Format

The tempo map lives on `Song` in `song.json`:

```json
{
  "tempoMap": {
    "timeSignatures": [
      { "measure": 1,  "numerator": 4, "denominator": 4 },
      { "measure": 17, "numerator": 7, "denominator": 8 },
      { "measure": 25, "numerator": 4, "denominator": 4 }
    ],
    "anchors": [
      { "position": "1:1",  "seconds": 1.840 },
      { "position": "17:1", "seconds": 28.057 },
      { "position": "33:1", "seconds": 48.500 }
    ]
  }
}
```

Arrangement entries in the same `song.json` carry only routing data:

```json
{
  "arrangements": [
    {
      "id": "6f725ad1-2653-4f0e-9a3a-c11bec3d2853",
      "part": "Lead",
      "audio": "backing",
      "toneDocument": "tones/1f0db037-f410-4ee0-945c-9ac1ca0d91e5/tone.json"
    }
  ]
}
```

`toneDocument` is optional. There is no arrangement document path, note array, chord template
library, or tuning field in this slice.

## Decisions

1. **The tempo map is a warp-anchor grid.** Time signatures are change points carried forward from
   their `measure`. Anchors pin sparse on-beat positions to absolute seconds.
2. **Anchor addresses are position tokens.** An anchor `position` is exactly `"<measure>:<beat>"`.
   Anchors do not carry sub-beat offsets.
3. **Absolute seconds appear only on anchors.** Other beat and measure times are derived by
   interpolation through `TempoMap`.
4. **Anchor seconds persist at millisecond precision.** Writer output uses exactly three decimal
   places; read validation rejects non-finite or off-grid seconds.
5. **Arrangements do not own chart storage yet.** They carry `id`, `part`, `audio_asset`,
   `audio_duration`, and `tone_document_ref`; note/tuning decisions are deferred until needed.

## Core Model

`rock-hero-common/core` owns these current values:

```cpp
struct TimeSignatureChange { int measure{1}; int numerator{4}; int denominator{4}; };
struct BeatAnchor { int measure{1}; int beat{1}; double seconds{0.0}; };
class TempoMap;

struct Arrangement
{
    std::string id;
    Part part{Part::Lead};
    DifficultyRating difficulty;
    AudioAsset audio_asset;
    TimeDuration audio_duration;
    std::string tone_document_ref;
};
```

## Validation

- `tempoMap` is required in `song.json`.
- `timeSignatures` is non-empty, starts at measure 1, uses strictly increasing measures, positive
  numerators, and power-of-two denominators.
- `anchors` contains at least a start and terminal anchor.
- The first anchor is `1:1`; the terminal anchor is on a downbeat.
- Anchor positions are positive on-beat `measure:beat` tokens whose beat exists in the active
  measure.
- Anchors are strictly increasing by grid position and by seconds.
- Anchor seconds are finite, non-negative, and on the three-decimal storage grid.
- Arrangement entries require non-empty `id`, `part`, and `audio`; `id` must be a canonical UUIDv4.
- `toneDocument`, when present, must be `tones/<uuid>/tone.json`, safe, and present in the package
  or workspace.

## Implementation Steps

1. Keep the public core model limited to tempo map, arrangement audio, and tone-document references.
2. Read and write `tempoMap.timeSignatures` and `tempoMap.anchors` using the settled shape above.
3. Stop reading and writing arrangement note/tuning document files.
4. Keep save-time tone-document validation before generated package writes and audio imports for
   the arrangement being processed.
5. Keep focused tests for malformed tempo maps, writer formatting, arrangement references, and
   missing tone documents.

## Playback Interactions

Recorded for context; not part of this persistence work.

- **Grid alignment does not stretch the source audio.** The tempo map warps the beat grid over
  fixed backing audio; it does not time-stretch the backing clip.
- **Whole-track practice speed is a separate, realtime feature.** Slowing or speeding the backing
  track to practice guitar along with it time-stretches the backing clip live (pitch preserved). It
  uses Tracktion's realtime elastique path, not an offline render. This is why backing playback runs
  with the Tracktion proxy disabled (`Engine::setActiveArrangement` calls `setUsesProxy(false)`):
  proxy-on would re-render and stall on every speed change. See
  `docs/plans/in-progress/tracktion-proxy-startup-playback-regression.md`.

## Non-Goals

- No note, chord, tuning, technique, or gameplay chart persistence.
- No note-display UI.
- No tone-change events or plugin-automation track authoring.
- No import/export model for external tab or chart formats.
