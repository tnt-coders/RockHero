# Tone Automation Track Plan

Status: **SUPERSEDED 2026-07-07** by `docs/plans/in-progress/tone-parameter-automation-plan.md`, the active,
decisive plan (formal juce-tracktion-expert review complete). This document is kept only as the
older UX sketch; do not execute from it — the newer plan resolves its open questions and commits to
an editable-lane design keyed to the shipped item-1 tone model.

Prior status: deferred UX reference, revised 2026-07-05. The tone track itself now exists
(`ToneTrackView` renders persisted tone regions with selection and resize; see
`docs/plans/in-progress/tone-track-tempo-map-plan.md`), so this document no longer plans that
component — it keeps the automation-lane UX that layers on top of it: per-automation sub-lanes
expanding beneath the compact tone strip when a region is clicked. That work is sequenced after
the note-storage format and tablature display per the tone plan's Non-Goals. Sections below that
described building the tone row from scratch were removed as implemented; naming differs from the
shipped code (`ToneRegion`, `ToneTrackViewState`) wherever this document says "clip".

## Goal

Display authored tone changes over the song timeline as project-owned tone clips in the track
viewport. Selecting a tone clip reveals detailed automation lanes for that clip without squeezing
full automation editing into every clip rectangle.

The visual model should make tone authoring feel like editing song structure, not like exposing
Tracktion's internal tracks, clips, racks, or plugin graph.

The user-facing lane label should be `Tones`. `ToneClip` or `ToneRegion` can remain an internal
model name because the value is still a time-bounded region that references a reusable tone slot.

## Relationship To Tone Rack Plan

This plan describes the editor-facing timeline UX. `tone-rack-plan.md` describes the backend tone
topology and the preferred RackType-based implementation.

The two plans should stay aligned around one rule:

- The editor may show tone clips, tone slots, and automation curves.
- The editor should not model tones as visible Tracktion tracks or Tracktion clips.
- The audio backend can compile the project-owned tone timeline into Tracktion racks,
  plugins, branch gains, and automation.

## Core Concepts

**Tone slot**: A reusable sound definition such as Clean, Crunch, or Lead. A tone slot owns or
references the plugin chain/routing needed to produce that sound.

**Tone clip**: A project-owned timeline region that references a tone slot by stable ID. A clip
does not duplicate the plugin chain. If several clips reference the same Lead tone slot, editing
the Lead slot updates the shared sound definition for all of them.

**Tone gain**: The clip-level gain or blend curve used to fade a tone in, fade a tone out, or
crossfade between neighboring tones. This belongs visually on the tone region itself because it
defines the region's audible shape over time.

**Clip automation**: Curves that apply over the selected clip's time range. These can target tone
slot parameters, plugin bypass-like state, wet/dry mix, wah position, or other exposed parameters.
Detailed plugin parameter automation belongs in expanded automation lanes below the tone row rather
than inside every tone rectangle.

**Automation lane**: A row revealed under the tone clips row for a selected clip or selected
parameter set. The lane uses the same horizontal song timeline as the waveform and tone clips, but
it only draws active curve data inside the selected clip's span.

**Signal chain panel**: The bottom control panel remains the place to edit tone slots, plugin
chains, selected parameters, and future graph/routing details. The track viewport shows when tone
events and automations happen.

## Visual Model

Default overview:

```text
Backing Audio    | waveform waveform waveform waveform waveform
Tones            | [Clean Verse] [Crunch Chorus] [Lead Solo] [Clean Outro]
```

Selected clip with expanded automation:

```text
Backing Audio    | waveform waveform waveform waveform waveform
Tones            | [Clean Verse] [Crunch Chorus] [Lead Solo] [Clean Outro]
Automation       |                         Wah Position    /\/\____
                 |                         Delay Mix       ___/----
                 |                         Amp Gain        ----\___
```

The automation lanes are full-width timeline rows so time alignment remains obvious. The curves
are visually tied to the selected clip by drawing only inside the selected clip span and by using a
subtle selected-region background across the expanded lanes.

Tone regions should show clip-level tone gain directly in their own rectangles. That includes
fade-in ramps, fade-out ramps, and short crossfade shapes at transitions. This is distinct from
detailed plugin parameter automation, which should live in expanded lanes below the tone row.

Tone regions may eventually show a compact summary inside the rectangle, such as a small
automation marker or sparkline, but the full editable parameter curves should live in expanded
lanes below the tone row.

## Interaction Model

- Clicking a tone clip selects it and updates the signal-chain panel context.
- Clicking empty tone-track space can eventually create a new tone clip at that time.
- Dragging a clip body can eventually move the clip.
- Dragging clip edges can eventually trim the clip's start or end time.
- Selecting an automation target in the signal-chain panel reveals its lane under the selected
  clip.
- Selecting multiple automation targets can reveal a small stack of lanes under the tone clips row.
- Transport cursor, horizontal zoom, and scrolling remain shared with the backing waveform.

The first implementation should keep editing intentionally limited. Selection and read-only
display are enough before adding trim, move, split, or curve editing.

## State Model Direction

The UI should receive framework-free view state from editor core:

```text
ToneAutomationViewState
  clips[*]
    id
    tone_slot_id
    name
    color
    time_range
    selected
    has_automation
  expanded_automation[*]
    target_id
    target_name
    clip_id
    points[*]
      time
      value
```

This state is intentionally project-owned. It must not expose Tracktion `Track`, `Clip`,
`RackType`, `Plugin`, `AutomatableParameter`, or raw plugin-host objects to editor UI code.

Persistence reality (2026-07-05): tone regions persist in `song.json` under the arrangement's
`toneTrack`, and tone/VST state persists through Tracktion-managed tone files. Automation-curve
persistence for user-authored curves is an open decision for this deferred work.

## Remaining Implementation Scope

The tone row, region rendering, selection, and resize shipped via
`docs/plans/in-progress/tone-track-tempo-map-plan.md` slices 2-3. What remains for this plan:

1. Expand/collapse per-automation sub-lanes beneath the tone strip when a region is clicked
   (disclosure-style; the 30 px strip is the collapsed state).
2. Read-only expanded automation lane rendering for selected region data.
3. Draw simple tone gain and fade/crossfade shapes on tone regions.
4. Keep actual automation editing out of the first pass.

## Non-Goals For The First Pass

- No Tracktion clip display.
- No direct Tracktion automation editing from UI code.
- No arbitrary graph editor in the track viewport.
- No nested plugin containers in the timeline UI.
- No overlapping tone clips except simple future crossfade representation.
- No plugin parameter discovery UI inside the tone track itself.
- No full plugin parameter automation editor squeezed inside tone rectangles.
- No persistence format commitment beyond the existing evolving tone timeline boundary.

## Crossfade Representation

Tone clip transitions should eventually show short visual ramps at clip boundaries. The simplest
model is:

- clips remain logically adjacent and non-overlapping;
- each clip can carry fade-in and fade-out durations;
- the audio backend compiles those edges into branch gain ramps.

If true overlapping clips become necessary for blend authoring, that should be a later explicit
feature. The first design should keep one primary tone clip active at each timeline position.

## Testing Strategy

- Unit-test editor-core selection and view-state projection without JUCE.
- Unit-test time-to-pixel and clip hit-testing helpers if they are extracted from the component.
- Add focused editor UI tests for component visibility, layout, and selection callback wiring.
- Adapter-test audio compilation from tone clips to branch gain automation separately from UI.
- Avoid requiring real plugins or audio devices for normal tone-automation tests.

## Open Questions

- Should clip automation points be stored in absolute song time or local clip-relative time?
- Which tone parameters should be exposed first: active tone slot, branch gain, wet/dry mix, or
  plugin parameters?
- Should selecting a tone clip automatically select its tone slot in the signal-chain panel?
- How should the UI represent clips whose referenced tone slot has been deleted or failed to load?
- How much automation summary should appear inside the clip rectangle before it becomes visual
  clutter?
