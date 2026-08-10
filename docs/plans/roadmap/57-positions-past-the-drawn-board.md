# 57 — Positions past the drawn board

**Status:** Decision-gated (G57-BOARD, 57-Q1..Q4). Nothing executable until 57-Q1 is measured.

**Authored** 2026-08-10, out of the deep code review. An interim fix shipped the same day; this plan
is the real resolution.

## The problem

Three constants describe "how far along a string a thing can be", and they disagree:

| Constant | Value | Meaning |
|---|---|---|
| `g_highway_fret_count` (`highway_metrics.h`) | 24 | fret slots the 3D board draws |
| `g_max_fret` (`chart_rules.h`) | 30 | highest fret chart validation accepts |
| `g_max_harmonic_node` (`chart_rules.h`) | 48.0 | highest harmonic node chart validation accepts |

So the chart domain is legal in places the board cannot draw, in two independent ways:

1. **A harmonic node past fret 24.** A node is a point on the string, not a fret, and the string
   keeps having nodes past its last fret — the review found a third-partial artificial harmonic with
   its node at 24.02, cited as real Guitar Pro data.
2. **A plain fret from 25 to 30.** No harmonic needed. Ordinary chart content that validation accepts
   and the board has no slot for.

Both were silently broken: the note drew off the viewport entirely while the 2D lane showed it
normally, so the two surfaces disagreed about whether the note existed on screen.

## What shipped as the interim (2026-08-10)

- `highwayDrawnSoundingPosition` in `highway_view_state.h` is now the single authority for where a
  note draws on the 3D board, and it **holds a node at the board's edge**. Every 3D consumer reads it
  — the renderer's `noteFretboardX`, the camera's framing scan, the tap station chain, and the
  tap-onset fret light — so the board and the camera can no longer frame different places.
- The **fret 25–30 half was deliberately left unclamped**, because clamping would draw a note at a
  fret the chart did not ask for. It is the same mismatch and wants the same single decision rather
  than a second quiet clamp.
- The resulting 2D/3D asymmetry is **decided, not latent**: 2D prints a node past the board as its
  number, 3D draws the note at the last fret.

The interim is safe and cheap. It is not the answer, because a node held at the edge is a note drawn
at the wrong place — just a visible wrong place instead of an invisible one.

## The candidate resolutions

**A. Shrink the domain to the board.** Lower `g_max_fret` to 24 and `g_max_harmonic_node` to 24, and
refuse or repair anything past it on import. This is the simplicity-first option and would **delete**
the problem rather than add a display mechanism: one number, no new geometry, no new art, and the two
surfaces agree by construction. It is only available if the corpus does not actually use those
positions — which is 57-Q1, and is measurable rather than arguable.

**B. Extend the drawn board.** Lay out 30 fret slots instead of 24. Costs: every slot narrows or the
board grows, and the inlay atlas covenant is written against the board's fret count
(`inlay_columns * inlay_rows >= g_face_fret_count`), so the art has to grow with it. Does **not**
solve nodes, which run to 48.

**C. A compressed zone past the last fret.** Draw positions past 24 in a squeezed region at the board
edge. Solves both halves and keeps every position visible, at the cost of a second, non-uniform
mapping on the fret axis — which `highwayFretLineX` is currently a single linear authority for, and
which the lefty mirror rides structurally.

**D. Annotate rather than place.** Keep the head at the board edge and add a mark saying it belongs
past it (an arrow plus the node number, matching what 2D already prints). Cheapest honest option;
admits the board cannot show the position instead of pretending.

## Open questions

- **57-Q1 (measure first, blocks everything).** Does the corpus contain any harmonic node above 24,
  or any fret above 24? Run against the local Guitar Pro corpus and the converted `.rock` corpus.
  If both answers are no, take **A** and close this plan. If nodes exceed 24 but frets do not, the
  fret half takes **A** and only the node half needs a display mechanism. Record the counts, not
  just the yes/no — a handful of notes in one file argues differently from a systematic pattern.
- **57-Q2.** If a display mechanism is needed, which of **B**, **C**, **D**? Note that **D** composes
  with the shipped interim (it is the interim plus a mark), so it is the cheapest path from here.
- **57-Q3.** Should the 2D lane change at all? It prints the node as a number today and has no board
  to run out of, so it may need nothing — in which case the asymmetry recorded above becomes
  permanent and should move from "decided pending this plan" to just "decided".
- **57-Q4.** If **A** is taken, what does import do with a node or fret past the new cap — refuse the
  note, drop the harmonic and keep the note, or clamp? The importer's existing rule is that it SHEDS
  what it cannot represent (a whole song must not die for one note) while the editor verbs REFUSE, so
  the shed path already has a shape to follow, and `harmonicNodeCeiling` already exists as the
  per-note bound.

## Sequencing

Independent of everything else on the roadmap. 57-Q1 is a corpus measurement that can be done at any
time and may close the plan outright, so do that before scheduling any of it.

## References

- `rock-hero-common/core/include/rock_hero/common/core/highway/highway_view_state.h` —
  `highwayDrawnSoundingPosition`, the interim authority and the place the cap is documented.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` — `soundingPositionAt`, the
  unbounded chart-space answer this caps for display.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_rules.h` — the domain constants
  and `harmonicNodeCeiling`.
- `docs/plans/roadmap/56-head-atlas-mipmapping.md` — shares the atlas-contract dependency if **B** is
  taken.
