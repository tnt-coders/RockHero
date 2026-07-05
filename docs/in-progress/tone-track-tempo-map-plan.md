# Tempo-Mapped Tone Track Plan

Status: in progress. This is the active plan for making the tempo map visible and adding a simple
tone track that schedules whole-rig tone changes over the song grid.

## Goal

Add a visible `Tones` track below the backing waveform. The track shows time-bounded tone regions
that snap to the song tempo map. Each region references a package tone document, and playback
switches the audible tone at region boundaries.

The first version should prove the tone-track workflow without committing to note, tuning, chord,
or full plugin-automation design.

## Dependencies and related plans

This plan builds on `docs/in-progress/tempo-map-implementation-plan.md` and is sequenced after it:
it needs the persisted `TempoMap`, the required terminal anchor, and the `"<measure>:<beat>"` anchor
token grammar from that slice. Land tempo-map persistence first, then this.

Reconcile with the earlier `docs/todo/tone-track-plan.md` and
`docs/todo/tone-automation-track-plan.md` (both predate this direction). When this plan lands,
mark them superseded or note explicitly which later parameter-automation and rack work carries
forward, so stale parallel plans do not accumulate.

## Decisions

1. **The tempo map becomes visible first.** The editor should draw measure and beat grid lines on
   the shared timeline before tone-region editing is introduced.
2. **Tone regions are project-owned timeline data, not Tracktion clips.** Editor UI renders
   project-owned state and emits editor intents. Tracktion tracks, clips, racks, and plugins remain
   behind `rock-hero-common/audio`.
3. **Tone region endpoints are musical positions.** The first implementation should store and edit
   whole-beat endpoints using the same `"<measure>:<beat>"` token style as tempo-map anchors. The
   token parse/format helper from the tempo-map anchor slice is reused for region endpoints; this
   plan does not add a second `"<measure>:<beat>"` parser. Sub-beat snapping can be added later when
   the UI exposes a subdivision control.
4. **Seconds are derived from the tempo map.** Tone regions do not store absolute seconds. Runtime
   scheduling converts the musical endpoints through `TempoMap`.
5. **Instant switching means preloaded tones.** Playback must not instantiate plugins, load tone
   documents, or rebuild the audio graph at a tone boundary. All referenced tones should be loaded
   before playback can rely on seamless switching.
6. **The user-facing model is whole-tone switching.** A tone switch swaps the audible complete
   tone. The backend should make that seamless by switching or ramping preloaded tone slots, not by
   hard tearing down and rebuilding the live rig at the boundary.
7. **Switches are evaluated against the transport clock, not pushed from outside.** A region
   schedule should bake to branch-gain automation on the audio timeline, preferably with
   sample-accurate switch timing. The editor must not poll transport position and push it back to
   trigger switches. That would be a second clock, which the architecture's single-source-of-timing
   rule forbids. The only position told to the backend explicitly is a seek/scrub resync.

## Persistence Direction

Keep the first persistent shape arrangement-local. A song can have multiple arrangements later, and
each playable route may need its own tone schedule.

Draft shape:

```json
{
  "arrangements": [
    {
      "id": "6f725ad1-2653-4f0e-9a3a-c11bec3d2853",
      "part": "Lead",
      "audio": "backing",
      "toneTrack": {
        "regions": [
          {
            "id": "c0a801b6-5f9a-4b61-b0a6-0d24e90d2c5c",
            "name": "Clean Verse",
            "start": "1:1",
            "end": "9:1",
            "toneDocument": "tones/1f0db037-f410-4ee0-945c-9ac1ca0d91e5/tone.json"
          }
        ]
      }
    }
  ]
}
```

Compatibility rule:

- Existing `toneDocument` on an arrangement remains the legacy/default tone reference while the
  tone-track model is introduced.
- If an arrangement has no `toneTrack`, project load can synthesize one read-only/default region
  from `1:1` to the tempo map terminal anchor using the arrangement's `toneDocument`.
- The synthesized default region is runtime-only: it is **not** written back to `song.json`. A
  legacy arrangement that is loaded and re-saved keeps its bare `toneDocument` and gains no
  `toneTrack` until the user actually authors one. This keeps load-to-save from rewriting untouched
  projects (see `docs/todo/native-package-write-safety-followups.md`).
- Once the tone track is authored and saved, tone changes should live in `toneTrack.regions`.

## Core Model Direction

Add small project-owned value types in `rock-hero-common/core` only for the tone-track surface:

```cpp
struct ToneGridPosition
{
    int measure{1};
    int beat{1};
};

struct ToneRegion
{
    std::string id;
    std::string name;
    ToneGridPosition start;
    ToneGridPosition end;
    std::string tone_document_ref;
};

struct ToneTrack
{
    std::vector<ToneRegion> regions;
};
```

`ToneGridPosition` is intentionally not a note/grid/chord model. It exists only to place tone
regions on the tempo map, and it serializes through the same `"<measure>:<beat>"` token helper as
`BeatAnchor`. If sub-beat snapping becomes necessary, extend this type then rather than bringing
back broad chart-position machinery now.

## Validation

- Region IDs are canonical UUIDs.
- Region names may be empty in the data model, but the UI should display a fallback such as the
  referenced tone name or `Tone`.
- `start` and `end` are valid beat positions in the active tempo map.
- `start` is strictly before `end`.
- `end` resolves at or before the tempo-map terminal anchor.
- Regions are sorted by `start` (and the writer emits them in `start` order).
- Regions do not overlap in the first implementation.
- Gaps are allowed, but playback behavior for gaps must be explicit.
- `toneDocument` uses the existing canonical tone document path rules and must exist in the
  package/workspace.

Gap behavior for the first version:

- A gap holds the previous region's tone, so the prior tone rings through the gap.
- For v1, gap holds should render as a visually distinct continuation of the prior region so the
  authored boundary is still visible.
- Before the first region, the audible tone is the arrangement's default tone when the legacy
  `toneDocument` exists; otherwise, use silence or an empty rig.

## Editor UI Scope

First pass:

1. Draw tempo-map measure and beat grid lines over the shared track viewport.
2. Add a visible `Tones` row below the backing waveform.
3. Render an empty tone row when no tone regions exist.
4. Render tone regions from editor-core view state.
5. Select a tone region.
6. Drag region edges to resize.
7. Snap resized endpoints to tempo-map beats.
8. Keep transport cursor, horizontal zoom, and scrolling shared with the waveform.

Not first pass:

- Moving tone regions by dragging the body.
- Creating regions from empty-space clicks.
- Crossfade handles.
- Parameter automation curves.
- Tone slot library UI.
- Sub-beat grid controls.

## Runtime Audio Direction

The current `ILiveRig::loadLiveRig()` API loads one active tone document and is not sufficient for
timeline switching. Add a separate audio boundary when runtime tone switching is implemented.

`prepareToneTimeline` does the real work up front: it builds the multi-tone graph, preloads every
referenced tone, and bakes the region schedule into branch-gain automation on the audio timeline.
After that, the backend schedules switching against the transport timeline, preferably through
sample-accurate automation. There are no per-frame calls. `setToneTimelinePosition` exists only so
a seek/scrub can resync the active tone to a new playhead position; it is not the playback switch
path.

Likely shape:

```cpp
struct ToneSwitchRegion
{
    common::core::TimeRange time_range;
    std::string tone_document_ref;
};

class IToneTimelinePlayer
{
public:
    // Builds the multi-tone graph, preloads every referenced tone, and bakes the region schedule
    // into transport-evaluated branch-gain automation. After this, switching is automatic.
    virtual std::expected<void, LiveRigError> prepareToneTimeline(
        const std::filesystem::path& song_directory,
        std::span<const ToneSwitchRegion> regions) = 0;

    // Resyncs the active tone after a seek/scrub. Not used during continuous playback.
    virtual std::expected<void, LiveRigError> setToneTimelinePosition(
        common::core::TimePosition position) = 0;
};
```

The exact API can change, but the important boundary is stable:

- editor/core owns tone-region policy and converts musical positions to seconds;
- common/audio owns tone-document loading, plugin/rack construction, preloading, and seamless
  switching;
- the audio thread (transport clock) drives the actual switch; nothing outside pushes position to
  trigger it;
- UI never sees Tracktion objects.

Backend implementation direction:

- Start simple inside `rock-hero-common/audio`.
- Preload every tone document referenced by the active arrangement's tone track.
- Keep inactive seamless tones loaded and ready.
- Bake region boundaries to branch-gain automation and switch with a very short ramp (5-10 ms),
  evaluated by the audio thread against the transport.
- Do not rebuild the audio graph at the moment of a region boundary.
- Prefer a RackType-backed multi-tone graph when implementing true seamless switching.

Preloading every referenced tone keeps several full plugin chains (amp sims can be heavy) loaded and
processing at once. This is an accepted, **currently unmeasured** tradeoff, consistent with the
architecture's pre-activated silent-plugin pattern. It is not a known problem. Do not design a
tone-count cap or CPU ceiling for it now. Revisit only if profiling on real songs shows it matters,
e.g. by bypass-ramping inactive branches rather than only zeroing their gain.

## Implementation Slices

1. **Grid visibility** (complete)
   - Expose tempo-map view state through editor core.
   - Add shared time/grid-to-pixel helpers.
   - Draw measure and beat grid lines in the track viewport.
   - Add editor UI tests for grid visibility and viewport scaling.

2. **Read-only tone row** (complete)
   - Added `ToneTrack`/`ToneRegion`/`ToneGridPosition` in `common/core/tone/` (the approved
     `tone/` feature folder used across libraries).
   - Parse/write `toneTrack.regions` through the shared `"<measure>:<beat>"` token codec, which
     was promoted into `rock_song_package_format` so anchors and regions share one parser;
     structural validation (`validateToneTrack`) is shared by reader and writer.
   - The legacy `toneDocument` default region is synthesized at view-state projection time
     (`editor/core/src/tone/tone_track_projection`), never stored, so untouched projects never
     gain a persisted tone track on re-save.
   - Added public `ToneTrackViewState` and the `tone_track` slice on `EditorViewState`.
   - Added `ToneTrackView` (editor/ui `tone/`), hosted by `TrackViewport` below the waveform at
     half a primary track height. The row is intentionally transparent so the shared tempo grid
     shows through between regions; the synthesized default region renders as a dim read-only
     continuation while authored regions render as filled labeled blocks.

3. **Selection and resize**
   - Add editor-core selection state.
   - Add resize intents for region start/end.
   - Snap resized endpoints to valid beat positions.
   - Validate ordering and non-overlap.
   - Route resize edits through the existing editor-core memento/command history (the settled undo
     mechanism); do not add a new undo system.

4. **Save/load behavior**
   - Persist authored tone regions.
   - Keep tone document reference validation on save/publish/import.
   - Add package tests for tone-track JSON and compatibility with legacy `toneDocument`.

5. **Runtime switching**
   - Add an audio boundary for prepared tone timelines.
   - Convert regions to seconds through `TempoMap`.
   - Preload referenced tones before playback switching (dedupe by `tone_document_ref`).
   - Bake region boundaries to transport-evaluated branch-gain automation; switching is automatic as
     the transport plays. Reserve an explicit position call for seek/scrub resync only.
   - Add adapter tests for preload/switch policy with fakes first, then focused Tracktion tests.

## Non-Goals

- No note, chord, tuning, or gameplay chart storage.
- No general grid-position system beyond tone-region endpoints.
- No plugin parameter automation authoring.
- No Tracktion clips or tracks exposed to editor UI/core.
- No graph editor.
- No promise to unload inactive tones for CPU savings.
- No hard live-rig teardown at tone boundaries.

## Open Questions

- What grid subdivision should be added after whole-beat resizing proves useful?
- Should tone documents be reusable named tone slots before runtime switching, or can region-level
  tone document references carry the first version?
