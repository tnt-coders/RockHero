# Sustain Tail Display Policy: Discussion Note

**Status:** Discussion note, not executable. Written 2026-09-09 from the tail-threshold
conversation after Plan 62 was drafted. Re-verify the code and the design documents before using
this as implementation input.

## Problem

`ChartNote::sustain` is chart truth: how long the string actually rings. The primary 2D and 3D
surfaces cannot simply draw every stored ring, because dense Rocksmith-style "show every tail"
notation becomes visually bad, especially in fast songs. At the same time, suppressing short tails
globally by note value makes fast songs cleaner but can hide intentional short holds that a charter
may want the player to see.

The shipped rule is time based and signed on sighting: an effect-free ring must last longer than
the kept-sustain bound, a duration of 250 ms (`g_minimum_kept_sustain_seconds`), to earn a drawn
tail. The previous baseline was note-value based, `> 1/8`, which looked good for many songs but not
all. The minimum sustain distance is a duration too, 100 ms
(`g_minimum_sustain_distance_seconds`), held below the bound so every earned tail keeps some ink.
The bound carries a watch item in `docs/tracking/watch-items.md`; Plan 62 is kept only as its
remedy.

## Principles

- Stored sustain remains exact and is never shortened just because display chooses not to draw it.
- Presented tail is notation and gameplay instruction, not a complete dump of stored truth.
- Actual-ring reveal remains the editor truth-inspection escape hatch.
- The game/scorer should read the same presented tail that the player saw.
- A local exception should author intent, not a second duration.
- Avoid per-note data unless it represents something the automatic rule cannot derive.

## Main Options

### Option A: Fixed Note-Value Bound

Keep one project-wide musical threshold, such as `> 1/8`.

This is conceptually simple and musically consistent: an eighth note means the same thing in every
song. Its weakness is the actual failure being observed: visual density and player perception are
not constant across tempo. An eighth-note tail at 75 BPM and an eighth-note tail at 212 BPM are the
same notation value but not the same screen or timing event.

This is the previous baseline, and the fallback if the sighting rejects Option C.

### Option B: Per-Chart Or Per-Song Note-Value Bound

Store a threshold such as `> 1/8` or `> 1/4` with the chart or song. Plan 62 currently proposes a
per-song value in `song.json`; a per-chart version would live on `Chart`, because each arrangement
owns one chart and rhythm/lead/bass parts may have different density needs.

This is robust and author-controlled, but it is more implementation surface: persistence, editor
UI, undo, view-state plumbing, tests, and documentation. It also asks charters to make a setting
choice before proving that a global perceptual rule is insufficient.

If built, chart-level is probably more correct than song-level. The same song can have a dense
rhythm arrangement and a spacious lead arrangement.

### Option C: Global Real-Time Tail Threshold

Replace "draw tails longer than a note value" with "draw tails whose stored ring lasts longer than
N milliseconds." For example, a tail earns display when its real duration is greater than 250 ms.

This directly targets the actual readability problem. At slow tempo, an eighth-note hold lasts
long enough to read and shows. At fast tempo, the same eighth-note hold may be too short to read as
a meaningful hold and hides. The value stays global across every project, but the consistency is
perceptual rather than musical.

This is the shipped rule, signed at 250 ms. It is also the simplest serious experiment:

- no package-format change;
- no settings UI;
- no undo;
- no per-chart/song policy plumbing;
- only presentation-threshold calculation and tests should move.

The conceptual cost is that the same note value can display differently at different tempos. That
cost may be acceptable because the player experiences the highway in time and screen distance, not
as abstract notation duration. The tempo map is piecewise constant between anchors, so the verdict
can only change at an anchor and never inside a run.

Plan 62's configurable-bound work now waits on the watch item's trigger.

### Option D: Chart-Wide Derived Note-Value Bound

Compute one threshold for the chart from representative tempo or density, then apply that note
value uniformly across the chart. For example, a fast chart might derive `> 1/4`, while a slow chart
derives `> 1/8`.

This avoids mid-song display flips and preserves a note-value rule once the chart starts drawing,
but it introduces a heuristic: representative tempo, edge cases near thresholds, and density
measurement all become policy. It is probably more complex than the global millisecond rule and
less directly author-controlled than an explicit chart setting.

Keep this as a fallback if a global millisecond rule feels too locally inconsistent but per-chart
manual settings feel too tedious.

### Option E: Per-Section Policy

Let regions of one chart choose different thresholds.

This is powerful but likely wrong for the first solution. It makes the visual grammar change inside
a chart and adds a new authoring surface. Do not build this unless real songs prove that a single
chart-wide or global perceptual rule cannot handle slow/fast sections.

## Note-Level Exception

There is still a separate question: what if a charter deliberately wants a suppressed tail to show
at any length?

The clean answer is a per-note presentation intent, not a second sustain:

```cpp
enum class TailDisplayOverride
{
    Auto,
    ForceShown,
};
```

`Auto` follows the global or chart-level tail rule. `ForceShown` means: draw this note's stored
ring even though the automatic engraving rule would suppress it.

Constraints:

- `ForceShown` bypasses only editorial suppression, such as the kept-sustain bound.
- It does not bypass structural truth: no tail for `attack: none`, no invented ring, no tail past a
  same-string re-strike, no refusal of collision or payload rules.
- If a forced tail is visible in the game, it is scored as visible. Drawn equals asked.
- In an onset group, one forced member should probably make the whole group earn tails, matching the
  existing group verdict. Otherwise one visible tail among same-strum siblings can falsely imply
  that only that string sustains.

Do not add `ForceHidden` yet. Hiding a tail the automatic rule would show removes a gameplay
instruction and needs a stronger design reason than visual preference.

## Minimum Sustain Distance

The minimum sustain distance is a different rule from tail earning. Tail earning asks whether a
ring deserves a visible tail at all. Minimum sustain distance is ink spacing: how much gap a drawn
tail, slide end, span, or hand-window ramp keeps before the following mark.

The margin IS time-aware now, and the shape this plan proposed — a note-value baseline with a
millisecond floor under it — was not what shipped. The margin is simply 100 ms, measured back from
the onset being protected through the tempo map and floored onto the chart's tick lattice, so it is
exact across a tempo anchor and never shorter than the duration. No note-value ladder and no two
quantities to reconcile.

This remains a spacing rule. It should not become another way to decide whether a tail exists.

## Reveal Lead

The 3D board's tail-reveal lead (`g_tail_reveal_lead_whole_note`, a quarter of a whole note) is the
third member of this family: a note-value distance that therefore lasts a different time at every
tempo. It should probably adopt the same time-based calculation once the tail threshold and the
margin have settled, but that is a sighting, not a conclusion. A reveal that runs longer in slow
songs and shorter in fast ones may be the right feel: the window is a fraction of the board's
scroll, and a slow song's board has more room to materialize ink in. Sight it after the other two,
never in the same change.

## UI Placement If Configuration Is Built

If the project eventually needs a configurable threshold, it should not live in the View menu. This
is not a local display preference; it affects the saved chart and the game/scoring surface.

Best likely placement:

- chart-level value: a Chart Settings surface reached from a gear/settings control near the
  arrangement selector;
- song-level value, if retained instead: Song Information, because it travels with `song.json`;
- compact read-only chip near the arrangement selector only when non-default, such as `Tails > 1/4`.

For the note override, expose `Tail display: Auto / Force shown` in a selected-note inspector or
context menu. The persistent visual indicator should be editor chrome only: a small pin/notch/dot
near the tail root or head edge. The game should just show the resulting tail, not an override
badge.

## Recommended Sighting Order

1. Prototype a global real-time tail earning threshold behind a constant only.
2. Sight a small set of values across the corpus: 200 ms, 250 ms, 300 ms, maybe 350 ms.
3. Compare slow, medium, and fast songs on both the 2D lane and 3D board.
4. Only if the global millisecond rule fails, revisit Plan 62 as chart-level configuration rather
   than song-level configuration.
5. Add `ForceShown` only after the base threshold is chosen, because the override's need depends on
   which automatic rule wins.
6. The minimum sustain distance is already a duration; nothing is left to sight for it here.

## Current Lean

Sight the global real-time tail threshold first. It is the lowest-complexity rule that addresses the
actual problem while preserving one consistent value across projects. If it feels right, Plan 62's
per-song/per-chart configuration may be unnecessary. If it feels wrong because note values changing
visibility across tempo is too surprising, then build configuration deliberately, probably per
chart rather than per song.

