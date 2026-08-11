# Design calls left open by the 2026-08-10 deep review

**Status:** Unstarted, deliberately. Each item below is a *design decision*, not a fix waiting to
be typed — the review verified the facts, and the backlog's own charter sends design-level work
here rather than keeping it beside the small fixes. Re-verify each claim against the code before
executing (this is a `todo/` plan and may lag the tree).

## 1. Split `Arrangement`'s persisted truth from its prepared state

`Arrangement::audio_duration` is filled at arrangement-prepare time by the engine
(`engine_song_audio.cpp`) and read by several callers, while the rest of the struct is persisted
package truth — so a reader cannot tell which fields a package actually carries and which appear
only after the engine has been asked. The fix is a shape split (a persisted arrangement and a
prepared view over it), which touches the package reader/writer, the session, and the engine's
prepare path. Decide the split's shape first; this is exactly the "datum stored once instead of
derived twice" family.

Related, smaller, and possibly folded in: `Arrangement::difficulty` is currently written by
nothing and read by no production code. Roadmap plan 11 (derived difficulty) already decides that
difficulty is *derived*, not authored — so when this split happens, the field either leaves the
persisted shape entirely or becomes prepared-side, per plan 11's decision.

## 2. Make `ScoringRuleset`'s version/constants agreement unrepresentable

The doc on `scoring_ruleset.h` says any constant change bumps the version; nothing enforces it,
and every score record is stamped with that version and claimed self-describing. A factory keyed
by a version enum (each enumerator returning its own frozen constant set) makes the lie
unrepresentable. Design the enum's growth story before writing it — score records persist, so the
mapping is an on-disk contract. Also give `timing_window`'s `speed_factor` division an enforced
domain while in there: the positive-domain precondition is documented but unvalidated, and at
zero the hit window collapses and the recorded delta becomes NaN.

## 3. Decide whether `EditorTheme` grows font and size roles

The theme seam carries colors only, so every font height in `editor/ui` is a literal by
construction (~30 color literals also remain outside the seam — that half is a sweep, tracked in
the backlog). Whether the theme should own typography roles is a design decision about the seam's
scope, not a cleanup; decide it before any font sweep, or the sweep just relocates the literals.
