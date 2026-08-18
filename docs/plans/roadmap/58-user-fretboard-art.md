# 58 - User-provided fretboard art

Status: roadmap, not started. Decision shape settled with the user 2026-08-17 (see the trail
below); re-verify against the code before execution.

## What ships WITHOUT this plan (the default tier, implemented 2026-08)

Inlay markers are code-mapped: a single dot cell (`inlays.png`) drawn as world-square quads at
code-derived positions - the marker fret table, single/double law, and diameter live in the
renderer as one authority. Round and exactly seated at EVERY string count by construction,
including counts no art was ever authored for (a 9-string, say). This replaced the stretched
per-fret sheet whose hand-baked art rendered dots elliptical (4.8% at 6 strings, 57% at 4) and
carried four position bugs (2026-08-17 positional audit).

## What THIS plan adds: user-provided fretboard art as an override tier

Users supply fretboard skin images - wood grain, trapezoid/block inlay styles, an artistic
12th fret, full-neck art. Ruled with the user:

- ONE mechanism: an image mapped onto the board rect. No per-style code paths.
- PER STRING COUNT, by design: the board rect's aspect changes with string count, and no
  runtime mapping can adapt expressive art without distorting or cropping it - so each
  supported count is its own authored design (user ruling 2026-08-17: "user provided ones
  would need a separate design per string count").
- FALLBACK: a count with no user image falls back to the default dot tier - never to a
  stretched image for a different count.

## Contract questions to settle when this is built

- File naming/selection per count and where user files live.
- Whole-board single image vs per-fret cells (a single board-rect image is the friendlier
  authoring contract and supports art spanning frets; equal fret slots make either mapping
  linear).
- Whether a user skin suppresses the default dot tier or layers over it (a skin carrying its
  own inlay style must not get our dots drawn on top).
- Validation at load: dimensions/aspect against the target count's rect, alpha convention.
- Whether we ship generated per-count sheets as examples (a generator deriving them from the
  world constants - the chords.png precedent - keeps shipped examples exact and regenerable).

## Decision trail

- 2026-08-17: positional audit finds the stretched-sheet ellipse + four hand-seat bugs. A
  code-drawn dot fix is proposed; the user counters that trapezoids/blocks/artistic inlays
  need the image path and dislikes dots as a special case; a generator-derived sheet round is
  started; the user then flips the default - the mapped single dot IS the shipping tier
  (works at any count), and per-count user images become this roadmap feature.
