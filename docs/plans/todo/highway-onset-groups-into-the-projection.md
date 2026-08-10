# Move onset grouping and repeat classification into the projection

**Status:** Ready, unstarted. Deliberately NOT folded into the 2026-08-10 review sweep — see
"Why this is its own plan".

**Found by** the 2026-08-10 deep review of the game side and the shared highway.

## What is wrong

`rock-hero-common/ui/src/highway/highway_renderer.cpp` rebuilds, per frame, a set of derivations
that are pure functions of the chart:

- **Onset grouping** (~`:2073-2140`): groups simultaneous notes, allocating and sorting a
  `std::vector<std::pair<int, int>>` of (string, fret) per group.
- **Repeat classification** (~`:2145-2285`): decides which strums draw as a half-height repeat box
  with no note heads, by walking the note stream **backward** looking for the run that anchors the
  repeat chain. The dead-chug branch has **no lower bound** — it walks back until it finds a run of
  two or more. A 200-note single-note solo followed by a fully-muted double-stop chug therefore
  revisits all 200 notes every frame, for as long as the chug is visible.
- **Covering-shape search** (~`:2230-2239`, `:2480-2484`): scans `state.shapes` from index 0 per
  group rather than advancing a cursor.
- **Span-hold take-over** (~`:2287-2300`): assigns each group the onset of the next note-showing
  strum.

None of it depends on the frame. Two consequences beyond the wasted work:

1. **The repeat rules have no tests at all**, because they live in the GPU path where nothing can
   reach them. They are also some of the fussiest rules in the renderer — three of the comments in
   that block record bugs already fixed there (a strict `>=` dropping a span's last strum, a
   rounding epsilon breaking the backward walk, a dead chug with fresh frets wrongly blanked).
2. **The take-over pass is window-dependent and should not be.** It runs over the VISIBLE groups, so
   the last visible group's hold cap is infinity even when a note-showing strum sits just past the
   window edge. It self-corrects as that strum scrolls in, which is why it has never been reported,
   but the value is wrong until then. Precomputing over the whole song makes it right and stable.

## What to do

Derive all of it once in `makeHighwayViewState` and carry it on `HighwayViewState`:

- A `HighwayChordGroupView` per onset group: `start_seconds`, `first`, `count`,
  `fretting_hand_count`, `any_accent`, `common_mute`, `all_full_muted`, `box_only`,
  `hold_cap_seconds`.
- A per-note group index, full length rather than visible-range-relative.
- The sorted (string, fret) pairs are **scratch** for the classification and should NOT be stored —
  check this before designing the struct, but no downstream site appears to read `group.frets` after
  classification finishes.

The renderer then reads groups and clamps them to its visible range instead of rebuilding them.

Expect roughly 210 lines to leave the deadline path and roughly 120 to arrive in headless core,
where the repeat rules finally become testable. **The tests are the point of the exercise**, not a
formality: port each of the fixed-bug comments into a case, since each names a real regression.

## Why this is its own plan

The 2026-08-10 review's other findings were local: a wrong comment, a duplicated rule, a missing
equality gate. This one is a refactor of the busiest file in the repository, and three things make it
materially riskier than the rest of that sweep:

- The group is consumed at about a dozen downstream sites, and several index it **relative to the
  visible range** (`note_group[index - first_note]`). Every one has to move to absolute indices, and
  an off-by-one there produces a wrong picture rather than a failed build.
- The highway render path has very little test coverage, so the usual safety net is thin exactly
  where the change lands.
- The window-dependence fix changes behaviour (correctly), so a reviewer cannot verify it by
  diffing for equivalence.

Folding that into a sweep whose other changes are verifiable by inspection would have made the whole
sweep harder to review, which is the opposite of the point. Do it deliberately, on its own, with the
tests written first where possible.

## Sequencing

Independent, but it **unblocks a product decision**: the highway currently draws a `box_only` group
with no note heads at all while the 2D tab lane shows every strum with full fret numbers, so the two
surfaces state different chords-per-bar side by side in the editor preview. That asymmetry cannot be
resolved while the classification is trapped in the renderer, because the 2D surface has no way to
read it. Once the rule is in core, either surface can.

## References

- `rock-hero-common/ui/src/highway/highway_renderer.cpp` — the block to move, and its downstream
  consumers at roughly `:2464`, `:2806`, `:3108-3145`, `:3610`, `:4094`, `:4373`.
- `rock-hero-common/core/include/rock_hero/common/core/highway/highway_view_state.h` — where the
  derived form belongs; `display_hold_ends` landed there on 2026-08-10 as the same kind of move and
  is the worked precedent.
- `docs/plans/roadmap/25-note-highway-3d.md` — the repeat-treatment spec the classification
  implements.
