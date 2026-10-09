# A position is a number: the program on whole-note places

**Status:** Unscheduled follow-on to `docs/plans/roadmap/67-nothing-snaps-to-downbeats.md`,
recorded 2026-10-09 as that plan's §7 asked. It depends on plan 67's format (every stored
position a whole-note place) and gates nothing the first release needs. Re-verify every count
and file path against the code before executing — this is a `todo/` plan and may lag.

## The idea (plan 67 §1a, shape B')

The file already stores every position as a whole-note count. The program still addresses by
`GridPosition{measure, beat, offset}`, which depends on the meters before it, so a meter edit must
re-address every object in memory (`readdressTimeline`) and snapshot the whole song for undo even
though nothing moved. Under B' the program's address is the place too:

- `ChartNote::position`, `FretHandPosition::position`, `SongSection::position`,
  `ToneRegion::start`, `ToneAutomationPoint::position`, `BeatAnchor::position`, every selection
  and action payload become `Fraction` places;
- `GridPosition` becomes a presentation projection — ruler labels, the readout, prompts, the
  caret's text — computed by `gridPositionAtWholeNote` where shown, never stored;
- a meter edit changes the tempo map and NOTHING else, so it commits like a tempo edit
  (`TempoMapSnapshot`); the whole-song re-address and `SongTimelineSnapshot` survive only for
  Scale Tempo and the Note Lock, which move places;
- `advanceGridPositionByWholeNotes`, `gridDistanceInWholeNotes` and `measureLatticeAt`'s measure
  walk collapse into rational arithmetic; the hand-position derivation's "one spelling for every
  instant" becomes the only spelling anywhere.

## Why it is worth doing

Plan 64's F9 (about 10 MB of undo per meter entry at 50k notes) disappears for meter edits, and
the §6 yields of plan 67 — the derived short bar as a segment so a place has one address, the
meter lattice rule — stop being load-bearing in memory, because bars become a display projection
of a grid that has no measures in it.

## Cost (measured 2026-10-08, before plan 67 built)

`GridPosition` at 383 sites in 63 source files and 127 sites in 28 public headers; token fixtures
at 555 sites in 35 test files. Mechanical in most places (a fixture helper parses `"12:3"` into a
place through the fixture's map) but crossing every feature, so it should not run concurrently
with other work on the same files. It does not change the file format.
