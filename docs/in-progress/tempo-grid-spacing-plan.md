# Tempo Grid Spacing Plan

Status: in progress. This is the active plan for making the editor tempo grid customizable beyond
the current beat-level grid.

## Goal

Allow the editor timeline grid and snap behavior to use configurable musical spacing. The current
4/4 behavior is a quarter-note grid because one tempo-map beat is a quarter note. The first
customizable version should support common power-of-two note-value presets plus free fraction
entry for finer or unusual grids, including triplet-style steps such as `1/12`.

This plan does not implement grid decluttering. The earlier
`docs/in-progress/tempo-grid-declutter-plan.md` can be deferred because tempo-map performance is no
longer blocking this work. The spacing implementation should still preserve the existing bounded
visible-range scan so decluttering can be layered in later if dense grids become visually noisy or
expensive at low zoom.

## Decisions

1. **Grid spacing is editor policy, not tempo-map data.** The tempo map owns authored musical time:
   time signatures, beat anchors, and seconds conversion. Grid spacing is a presentation and
   editing preference, so it belongs in editor grid geometry and editor view state.
2. **Store spacing relative to the tempo-map beat.** Internally, represent the grid step as an
   exact fraction of one tempo-map beat using the existing `common::core::Fraction` value type. The
   current grid is a step of `1/1` beat. In 4/4 that displays as `1/4`; in 7/8 it displays as
   `1/8`.
3. **Display user-facing note values.** The UI should label the current behavior as `1/4` in 4/4,
   not `1/1`. The displayed denominator is derived from the active time-signature denominator and
   the subdivisions-per-beat value.
4. **The first UI ships presets plus free fraction entry.** Quick-select presets cover `1/4`,
   `1/8`, `1/16`, `1/32`, `1/64`, and `1/128` in normal 4/4 usage, and a fraction field accepts
   any note value `n/d` (for example `3/16`, or `1/12` for a triplet grid) that passes validation
   after conversion to a beat-relative step.
5. **Numerators are first-class.** Spacing, entry, display, persistence, and grid generation all
   handle non-unit numerators. Validation only bounds the reduced fraction (numerator and
   denominator each in `[1, 128]`) and rejects non-positive values.
6. **Rendering and snapping share one setting.** The track grid, timeline ruler, and cursor snapping
   must consume the same spacing value so the visible grid and interaction grid never diverge.
7. **The first grid-size control lives in the transport strip.** A "Grid" note-value selector
   (power-of-two presets plus editable fraction text) sits beside the transport controls until a
   fuller chart-editing toolbar exists. Placing it inside the ruler row was rejected because a
   left-side cell would shift the ruler origin away from the timeline content and misalign ticks.
8. **Grid spacing persists per project in app-local settings.** The remembered spacing lives in
   `EditorSettings` keyed by project path, like the per-project resume cursor. It is not stored in
   the `.rhp` project file or `.rock` package, and there is no global last-used fallback in the
   first pass.

## Core Model Direction

Reuse `common::core::Fraction`
(`rock-hero-common/core/include/rock_hero/common/core/fraction.h`) as the spacing value instead of
adding a new rational type. `Fraction` was built for exact grid-relative musical durations and
normalizes to lowest terms with a positive denominator. Grid generation splits each step into a
whole beat plus an exact fractional remainder and resolves it through the tempo map's global-beat
query (`TempoMap::secondsAtGlobalBeatPosition`), so fractional beat positions stay exact instead
of accumulating floating-point step error.

The spacing value is the grid step measured in tempo-map beats:

- `Fraction{1, 1}` is the current whole-beat grid.
- `Fraction{1, 2}` is a half-beat step, displayed as `1/8` in 4/4.
- `Fraction{1, 3}` is a third-of-a-beat step, displayed as `1/12` in 4/4 (an eighth-note triplet
  grid).
- `Fraction{3, 4}` is a three-quarter-beat step, displayed as `3/16` in 4/4 (a dotted-eighth
  grid).

Editor-core policy around the type lives in the grid-geometry code, not in `Fraction`:

- A validation helper (for example `isValidTempoGridSpacing`) requires a positive fraction whose
  reduced numerator and denominator each fall in `[1, 128]`.
- `Fraction` default-constructs to `0/1`, which is a degenerate grid step. Every owner of a spacing
  value must initialize to `Fraction{1, 1}`, and the geometry entry points normalize invalid
  spacing to the whole-beat grid so rendering and snapping can never diverge and a corrupt stored
  value cannot blank the timeline.
- `Fraction` has no arithmetic operators, and none are needed: the k-th subdivision of a step
  `n/d` sits at `(k * n) / d` whole beats plus a `(k * n) % d` over `d` remainder, which feeds the
  tempo map's global-beat query directly with plain integer math.

Signatures should take `Fraction` directly with a clearly named parameter (for example
`grid_spacing_beats`); a wrapper struct would only restate what the type already means. The
important contract is that the grid geometry receives an explicit spacing value rather than
assuming whole beats.

## Display and Entry Rules

Display and entry share one unit: the note value, expressed as a fraction of a whole note. Presets
and the fraction field both use this unit, so what the user types is exactly what the control
shows. For time signatures where the tempo-map beat corresponds to the time-signature denominator,
the conversions are exact `Fraction` math in both directions:

```text
displayed_note_value = Fraction{spacing.numerator, spacing.denominator * ts.denominator}
spacing_in_beats     = Fraction{entered.numerator * ts.denominator, entered.denominator}
```

Both conversions reduce automatically because `Fraction` normalizes on construction.

Examples:

| Time signature | Grid step (beats) | Displayed/entered note value |
|----------------|-------------------|------------------------------|
| 4/4            | 1/1               | 1/4                          |
| 4/4            | 1/2               | 1/8                          |
| 4/4            | 1/4               | 1/16                         |
| 4/4            | 3/4               | 3/16                         |
| 4/4            | 1/3               | 1/12 (eighth-note triplet)   |
| 7/8            | 1/1               | 1/8                          |
| 7/8            | 1/2               | 1/16                         |

If a future tempo-map model distinguishes compound-meter beat units from the written denominator,
the display helper should use that beat-unit value instead of assuming the time-signature
denominator.

## Implementation Slices

1. **Core spacing value**
   - Take `common::core::Fraction` spacing parameters in editor-core grid geometry.
   - Add validation helpers (positive fraction, reduced numerator and denominator each in
     `[1, 128]`).
   - Keep the default equal to the current whole-beat grid (`Fraction{1, 1}`).

2. **Fractional grid generation**
   - Extend `TempoGridLine` with an explicit rank, such as `Measure`, `Beat`, and `Subdivision`.
   - Update `visibleTempoGridLines(...)` to accept spacing and emit fractional beat positions.
   - Preserve existing visible-span culling and merged-column behavior.
   - Promote measure starts over beats and beats over subdivisions when multiple positions collapse
     onto the same pixel column.

3. **Fractional snapping**
   - Build on the time-domain snap lookup from
     `docs/in-progress/timeline-snap-time-domain-plan.md`, which should land first so fractional
     search is written once against the time-domain API instead of the pixel-column one.
   - Pass the same spacing into the snap lookup and search neighboring fractional positions around
     the target click time.
   - Preserve the current tie behavior that chooses the earlier grid line.

4. **View-state plumbing**
   - Add the selected grid spacing to `EditorViewState`, initialized to `Fraction{1, 1}` because
     the `Fraction` default of `0/1` is a degenerate grid step.
   - Derive it in `EditorController`, initially from an in-memory default.
   - Pass it through `EditorView`, `TimelineRuler`, `CursorOverlay`, and timeline cursor placement.

5. **Editor UI control**
   - Add a grid-size control in the editor chrome where timeline editing controls belong.
   - Provide power-of-two presets for common note values through `1/128` as quick selections.
   - Provide free fraction entry in note-value units (`n/d`), normalized through `Fraction`
     reduction and validated against the spacing bounds after conversion to a beat-relative step.
   - Reject entries that fail validation and keep the previous spacing.

6. **Per-project preference persistence**
   - Persist the selected grid spacing per project in `EditorSettings` (the app-local
     `IEditorSettings` store), following the existing per-project cursor pattern: records keyed by
     the weakly-canonicalized project path inside one XML-valued property with format versioning.
   - Store both numerator and denominator so the settings format does not change when a numerator
     UI arrives.
   - Restore in `EditorController` when a project opens; fall back to `Fraction{1, 1}` for unknown
     projects or stored values that fail validation. Save through on every spacing change, since
     changes are rare.
   - Do not persist grid spacing in `.rhp` project data or `.rock` packages; it is per-user editor
     state, not authored song data.

## Tests

Add editor-core tests for:

- default spacing matching the current whole-beat grid;
- `1/2` beat and `1/4` beat subdivisions landing at expected columns;
- non-power-of-two manual spacing such as thirds or fifths of a beat;
- dense manual spacing preserving visible-span culling;
- merged columns preferring measure, then beat, then subdivision rank;
- snap lookup selecting fractional lines and resolving ties to the left;
- degenerate spacing (zero or negative, including the `Fraction` default of `0/1`) falling back to
  the whole-beat grid for both generation and snapping;
- note-value display and entry conversions reducing exactly, including `3/16` and triplet `1/12`
  cases in 4/4 and 7/8;
- per-project spacing round-tripping through `EditorSettings`, with malformed or out-of-range
  stored values falling back to the whole-beat default.

Add editor-ui tests for:

- the track grid and ruler using the same selected spacing;
- unmodified timeline clicks snapping to subdivisions;
- Ctrl-click/free placement remaining unchanged;
- the current 4/4 default displaying as `1/4`;
- fraction entry rejecting invalid or out-of-bounds values without changing the active spacing.

## Non-Goals

- No authored chart, note, chord, or tone-region position model changes.
- No tempo-map persistence changes.
- No dedicated dotted/triplet preset buttons; those grids are reachable through fraction entry.
- No declutter implementation in this slice.
- No attempt to make every entered fraction musically conventional.

## Open Questions

None; the numbered decisions above cover the open points this plan started with.
