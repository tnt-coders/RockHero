# Note: tone-automation grid-warp follow/stay toggle

Status: deferred note, not a plan. The user expects to likely never want this; it is recorded only
to show the current design does not paint us into a corner.

## Context

Tone parameter automation stores musical positions (`measure:beat+fraction`) as the persisted
truth in `song.json`, and rebuilds Tracktion's seconds curve as a derived cache (see
`docs/plans/in-progress/tone-parameter-automation-plan.md`, storage decision C2). Warping the grid
(moving tempo anchors) therefore moves automation with the grid — "follow" behavior — because
musical positions resolve through the edited tempo map.

## Why a future follow/stay toggle stays cheap

A REAPER-style per-edit choice ("follow the grid" vs "stay at absolute time") needs only a
position-conversion step at the moment a tempo edit is applied, because both representations are
already first-class in the pipeline:

- **Follow (current behavior):** keep the stored musical positions; rebuild derived seconds
  curves from the new tempo map. No data changes.
- **Stay:** before applying the tempo edit, resolve each point's seconds under the *old* map
  (`secondsAtGridPosition`), then after the edit re-quantize those seconds back to musical
  positions under the *new* map and store them. The model stays musical either way; "stay" is a
  one-shot conversion, not a second storage format.

Nothing else changes: persistence, undo mementos (point lists), the seconds-based
`IToneAutomation` adapter, and the derived-curve rebuild are all agnostic to which choice made
the musical positions. The same conversion approach extends to note positions if that ever
becomes warpable.

If this is ever picked up, decide then whether the toggle is per-application, per-edit-gesture
(REAPER's model), or per-item, and re-verify the tempo-edit flow that exists at that time.
