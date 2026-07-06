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

Earlier parallel plans were reconciled 2026-07-05: `docs/todo/tone-track-plan.md` was removed
(its remaining ideas contradicted the settled RackType, musical-position, and Tracktion-managed
persistence decisions), and `docs/todo/tone-automation-track-plan.md` was revised down to the
deferred automation-lane UX reference that still applies on top of the shipped tone track.

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
   schedule should bake to branch-gain automation on the audio timeline. The editor must not poll
   transport position and push it back to trigger switches. That would be a second clock, which
   the architecture's single-source-of-timing rule forbids. The only position told to the backend
   explicitly is a seek/scrub resync — and source verification shows even that may be unnecessary
   (see the verified mechanism notes below). Timing precision, verified against the vendored
   engine: automation is evaluated once per audio block at the block start
   (`Plugin::applyToBufferWithAutomation`, `plugins/tracktion_Plugin.cpp:656`), not per sample, so
   switch onsets quantize to the block (~3-10 ms at typical settings) and the per-sample
   `juce::SmoothedValue` ramp inside `VolumeAndPanPlugin` keeps the transition click-free. That
   meets the product bar ("instant, no dropout"); do not chase literal sample accuracy.

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

Row height and future automation sub-lanes (2026-07-05): the tone row is a fixed label-height
strip (30 px), not a half track — regions only need to show their name. When parameter-automation
authoring arrives, clicking a tone region should expand per-automation sub-lanes beneath the strip
(disclosure-style, like DAW automation lanes fold out of a track header) and collapse back to the
strip. Design the expansion then; the strip is the collapsed state it folds back to. That work is
explicitly sequenced after the note-storage format and tablature display (see Non-Goals).

Not first pass:

- Moving tone regions by dragging the body.
- Creating regions from empty-space clicks.
- Crossfade handles.
- Parameter automation curves (and the expand/collapse automation sub-lanes above).
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

Verified mechanism notes (2026-07-05 deep review against the vendored engine; re-verify only if
the submodule moves):

- **Branch gain must be a plugin.** `RackConnection` carries only endpoint IDs and pins, no gain
  (`plugins/internal/tracktion_RackType.h:14`). Terminate each branch with a gain stage:
  `tracktion::engine::VolumeAndPanPlugin` (automatable `volParam`, per-sample smoothed) or our
  `LiveRigGainPlugin` extended with an `AutomatableParameter` (today its gain is an atomic set
  from the message thread, not automatable, and `canBeAddedToRack()` returns false). This answers
  the tone-rack plan's open "which gain node" question in favor of a real per-branch plugin.
- **Curve authoring.** Bake the schedule with
  `parameter->getCurve().addPoint(time, value, curve_shape)` (0.0f = linear). Curve edits are
  message-thread ValueTree work. **Correction (2026-07-05 adversarial review):** for plugins
  *inside a rack*, adding/removing curve points does trigger a playback-graph rebuild —
  `RackType` watches its whole state subtree and calls `edit.restartPlayback()` on child
  add/remove (`plugins/internal/tracktion_RackType.cpp:1421`), and rack plugins' curve trees are
  descendants of the rack state. Mitigations, all source-verified: rebuilds coalesce through a
  1 ms timer (`model/edit/tracktion_Edit.cpp:1206`), the swap is lock-free with the old graph
  playing until the new one is ready
  (`tracktion_graph/tracktion_LockFreeMultiThreadedNodePlayer.cpp:243`), live plugins take the
  `initialiseWithoutStopping` path instead of re-preparing
  (`plugins/tracktion_Plugin.cpp:447`), and latency FIFOs transfer when node IDs match. Moving
  existing points does not rebuild. Expected inaudible, but spike it (below) before slice 5
  commits; the fallback if audible is hosting branch gains outside the rack or the hidden-track
  backend.
- **Smoothing time.** `VolumeAndPanPlugin::smoothingRampTimeSeconds` defaults to 0.05 s
  (`plugins/internal/tracktion_VolumeAndPan.h:78`) and is a settable public member; tune it at or
  below the baked ramp so the smoother does not stretch the authored 5-10 ms crossfade.
- **Automation read gate.** Playback follows curves only while
  `AutomationRecordManager::isReadingAutomation()` is true; it defaults to true and persists in
  the per-edit transport state (`model/automation/tracktion_AutomationRecordManager.cpp:79`).
  The rig setup must assert/restore this so a stray toggle cannot silently freeze tone switching.
- **Rack plugins follow automation like track plugins.** Each rack plugin is processed through a
  `PluginNode` calling `applyToBufferWithAutomation`
  (`playback/graph/tracktion_RackNode.cpp:397`), so branch-gain curves inside the rack play back
  with no extra wiring.
- **Seek resync is automatic.** When stopped or scrubbing, parameter streams follow
  `TransportControl::getPosition()` (`plugins/tracktion_Plugin.cpp:676`), and the live-input
  graph keeps processing while stopped, so branch gains snap to the playhead without an explicit
  position push. Keep `setToneTimelinePosition` out of the first backend implementation; add it
  only if a real resync gap shows up under test.
- **Latency equals the worst branch — unless compensation is disabled, which it should be.**
  The graph auto-inserts `LatencyNode`s at sum points so parallel branches stay aligned
  (`tracktion_graph/nodes/tracktion_SummingNode.h`, `tracktion_ConnectedNode.h`), which would
  make the rack's end-to-end monitoring latency the maximum branch latency at all times.
  **Amendment (2026-07-05 adversarial review):** `Edit::setLatencyCompensationEnabled(false)` is
  a public, reachable API (`model/edit/tracktion_Edit.h:539`, plumbed to
  `TransformOptions::disableLatencyCompensation`, which both rack join nodes honor). With
  compensation off, live monitoring latency equals the *active* branch's own latency; branches
  are misaligned only during the 5-10 ms crossfade between two different tones, which is a brief
  tone-to-tone phase smear, not a timing error. PDC aligns recorded material in mixes; this
  product has one live path plus a backing stem, so the default should be **compensation off**.
  The setting is edit-global. Keep the per-tone reported-latency surfacing/warning regardless;
  reporting accuracy still depends on each plugin's `getLatencySeconds()`.
- **Paused tone preview keeps the cursor in place via per-curve bypass.** Seek/stop
  follow-through is automatic because parameter streams track the transport position while
  stopped — which also means a naively-set branch gain would be overwritten on the next block.
  The engine's lever for this: every `AutomationCurve` has a `bypass` flag ("this curve is
  disabled, having no effect on the AutomatableParameter",
  `model/automation/tracktion_AutomationCurve.h:39`); a bypassed curve makes the parameter hold
  its directly-set value (`updateFromAutomationSources` falls back to `currentParameterValue`,
  `tracktion_AutomatableParameter.cpp:1182`). So while **paused**, selecting a region away from
  the cursor enters preview: bypass the branch-gain curves, set the selected branch's gain
  directly (still per-sample smoothed), cursor untouched. Pressing Play (or seeking) exits
  preview: clear bypass, the baked schedule wins, and the editor's existing play-snap moves the
  selection back to the region under the cursor. Bypass is a property change, not a child
  add/remove, so toggling it does not trigger the rack graph rebuild.
- **Optional finer switch timing exists.** `PluginManager::canUseFineGrainAutomation`
  (`playback/graph/tracktion_PluginNode.cpp:18`) grants approved plugins 128-sample sub-block
  automation. Registering it for just the branch-gain plugins drops switch quantization from
  block size to ~2.9 ms at 44.1 kHz. Not required to meet the product bar; note for later.
- **CPU parking facts, for the deferred profiling day.** There is no "loaded but frozen" state:
  `setProcessingEnabled(false)` *deletes* the hosted instance
  (`plugins/external/tracktion_ExternalPlugin.cpp:944`), and `setEnabled(false)` saves no CPU on
  latency-reporting external plugins because they keep processing to feed the latency-matched
  bypass path (`tracktion_PluginNode.cpp:102`). If parking is ever needed, it is
  `setEnabled(false)` under zero branch gain for zero-latency plugins only, re-enabled while
  still silent. Design any future mitigation from these facts.
- **"No rebuild at boundaries" is the bar, not "no rebuilds ever."** A hosted plugin changing
  its own latency triggers `edit.restartPlayback()` regardless of our design
  (`tracktion_ExternalPlugin.cpp:87`); the coalesced lock-free swap above is the engine's normal
  operating mode and the system must tolerate it during playback.

Preloading every referenced tone keeps several full plugin chains (amp sims can be heavy) loaded and
processing at once. This is an accepted, **currently unmeasured** tradeoff, consistent with the
architecture's pre-activated silent-plugin pattern. It is not a known problem. Do not design a
tone-count cap or CPU ceiling for it now. Revisit only if profiling on real songs shows it matters,
using the CPU-parking facts recorded in the verified mechanism notes above.

### Adversarial review outcome (2026-07-05)

A source-level adversarial review compared the rack design against every credible alternative.
Verdict: **amend, keep the rack** (amendments are folded into the notes above). Ranking:

- **Hidden parallel AudioTracks** (one per tone, shared live input via multi-target
  `InputDeviceInstance::setTarget`, per-track volume automation) is a *working* second place and
  the named fallback: same switching mechanism and timing, and track-level curve rebakes avoid
  the rack's rebuild-on-bake wart, but N hidden tracks leak into the edit model, input-target
  changes rebuild the graph, and the "one instrument route, not N pseudo-tracks" modeling
  argument from the tone-rack plan still holds. Rewrite target only if the rebake spike fails.
- **Custom multi-chain hosting plugin**: rejected — re-implements instance creation, state
  capture (today's `captureActiveRig` walks `tracktion::ExternalPlugin` state), plugin windows,
  latency propagation, and hides internal chains from Tracktion automation, killing future
  per-tone parameter automation.
- **Plugin enable/bypass as the switch**: rejected — `setEnabled` is a plain CachedValue, not
  automatable (message-thread push = second clock), and the transition is a hard per-block flip
  with no crossfade (the engine's own comment concedes it, `tracktion_PluginNode.cpp:252`).
- **Two-rigs-resident**: not a mechanism — there is no prepared-but-unconnected state; keep as a
  possible future resident-set policy applied at seek/load time, never at boundaries.
- **Engine-native modes**: `setLowLatencyMonitoring` shrinks the device buffer and disables
  chosen plugins but reallocates the playback context (audible) — a user-facing toggle, not a
  switch path; `TimedMutingNode` is internal-only and ramp-free; serial per-tone RackInstances
  with wet/dry would *sum* rack latencies (`RackReturnNode` delays dry to match wet) — strictly
  worse.

Spikes to run at the start of slice 5, before committing to the bake shape:

1. Bake/rebake curve points while playing with a latency-heavy plugin in another branch; record
   output and diff for discontinuities (validates the coalesced-rebuild correction).
2. Two branches with step curves; record and measure the composite fade envelope (baked ramp x
   `smoothingRampTimeSeconds`), then tune the smoothing time.
3. With latency compensation off, crossfade between two branches with mismatched latency and
   confirm the tone-to-tone phase smear is inaudible.

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
   - Added `ToneTrackView` (editor/ui `tone/`), hosted by `TrackViewport` below the waveform as
     a fixed label-height strip (initially half a primary track; compacted 2026-07-05, see the
     row-height note under Editor UI Scope). The row is intentionally transparent so the shared tempo grid
     shows through between regions; the synthesized default region renders as a dim read-only
     continuation while authored regions render as filled labeled blocks.

3. **Selection and resize** (complete)
   - Selection: clicking an authored region body selects it (`SelectToneRegion` action, controller
     state, `selected` view-state flag, brighter render); the synthesized default region ignores
     the pointer. Selection clears on project close/open.
   - Resize: edge drags snap to whole tempo-map beats, clamp against neighbors and the terminal
     anchor in the view, and commit through `ResizeToneRegion`; the controller revalidates through
     the shared `validateToneTrackRules` (promoted from the package format unit into public
     `common/core/tone/tone_track_rules` with its own `ToneTrackError` domain; the package format
     translates it) and records a `ToneRegionResizeEdit` inverse command in the settled undo
     history. `EditorEditContext` gained the session so tone edits can restore endpoints.
   - Interaction routing: the cursor overlay gained a hit-test pass-through so region clicks reach
     the tone row while empty row space keeps click-to-seek, plus a transient full-height
     `TimelineSnapGuide` with a `measure:beat` readout drawn during edge drags (the DAW-standard
     alignment cue chosen over grid-over-content rendering).

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
- No plugin parameter automation authoring. Sequencing decision (2026-07-05): coarse whole-rig
  tone switching (slice 5) is the minimum usable product; user-facing parameter automation
  (authoring UI, per-parameter curves, sub-lanes, undo/persistence of user curves) is deferred
  until after the note-storage format and the tablature-over-waveform display (waveform
  optionally hidable behind the tab) are sorted out. Do not conflate it with slice 5's baked
  branch-gain curves — those are internal switching plumbing the user never sees, and slice 5
  does not depend on any of the deferred work.
- No Tracktion clips or tracks exposed to editor UI/core.
- No graph editor.
- No promise to unload inactive tones for CPU savings.
- No hard live-rig teardown at tone boundaries.

## Open Questions

- What grid subdivision should be added after whole-beat resizing proves useful?
- Should tone documents be reusable named tone slots before runtime switching, or can region-level
  tone document references carry the first version?
