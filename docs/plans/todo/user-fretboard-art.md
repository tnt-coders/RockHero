# User-provided fretboard art (inlay skins)

Status: TODO - deferred until the feature is scheduled. Decision shape recorded 2026-08-17;
re-verify against the code and regenerate details before executing.

## The decision already made (with the user, 2026-08-17)

Fretboard inlays stay ONE mechanism: an image mapped onto the board. No code-drawn special
case for dots - the requirement to support real inlay vocabularies (trapezoids, blocks, an
artistic 12th fret, full-neck art spanning frets and strings) makes the image path the single
authority, and a parallel geometric path for dots alone would be extra machinery for one style.

Exactness is preserved the way chords.png already preserves it: the SHIPPED sheet is
GENERATOR-DERIVED. A deliberately unversioned script (scratch generators area, chords.png
precedent) knows the world constants (fret slot 1.1, string grid height count x 0.35, the
8x4 cell mapping) and bakes every marker at its exact seat with the aspect pre-compensated
for the target board, so dots render round on screen and hand-seat bugs are impossible.
Regenerate when metrics change. The 2026-08-17 positional audit found four hand-seat bugs and
a count-dependent ellipse (4.8% at 6 strings, 57% at 4) in the hand-authored sheet - the
generator retires that whole class.

Because the drawn board rect's aspect changes with string count, art is PER STRING COUNT:
separate images per count, authored (or generated) for that count's rect. The artist owns the
aspect; the code never stretches art across counts.

## Contract questions to settle when this is built

- File naming and selection: e.g. inlays-4.png / inlays-5.png / inlays-6.png / inlays-7.png,
  chosen by the chart's string count.
- Fallback when a count's file is missing (user ruling 2026-08-17): a DEFAULT tier that maps
  a single dot cell at code-derived positions - the generator's placement law executed at
  runtime - so an unusual count with no authored art (a 9-string, say) still gets round,
  exactly seated markers instead of a stretched sheet. Shipped generated sheets cover the
  common counts; the default tier covers everything else. The placement law (marker fret set,
  doubles, diameter, vertical law) then exists in the runtime table and in the generator -
  keep both pointing at each other, and keep the law small enough to verify at a glance.
- Whole-board single image vs today's per-fret cells: a single board-rect image is the
  friendlier authoring contract and supports art that spans frets (vine inlays); today's equal
  fret slots make the mapping linear either way. Decide with the feature.
- Whether a user skin can declare it carries its own markers (suppressing any shipped overlay)
  - only relevant if markers ever split from the skin; today they are one sheet.
- Where user files live and how they are validated (dimensions, alpha convention - the sheet
  carries real alpha, unlike notes.png).

## Current state (2026-08-17)

The generator and the regenerated 6-string sheet are being produced now (fixing the audit's
dot findings in the existing format with zero renderer changes). The bass-count sheet lands
with the bass-appearance roadmap item (RM-6), which per-count sheets are the mechanism for.
