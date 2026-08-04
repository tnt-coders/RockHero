# Plan 55 — Pick-Slide Notation

**Status**: Phases 1–2 complete 2026-08-03 (note-carried model, format, rules, import — built and
green). Phase 3 executes next behind one open sight gate: the 55-Q1 head pick from mockup sheets.
55-Q2 (authoring) largely dissolved into ordinary note verbs; its remainder folds into plan 40.
Baseline `master @ 84bdfe32`.

## Goal

Pick slides (pick scrapes) are a first-class notated technique: a **note whose attack is a
scrape** — right-hand, fret-hand-transparent, occupying its string and participating in every
note rule — with lossless Guitar Pro import, ordinary note authoring, and a dedicated visual
language on the 3D highway and the 2D tab.

## The decision trail (2026-08-02/03)

1. **The borrowed-vocabulary experiment failed on sight.** Mapping pick slides onto existing
   note vocabulary (full mute + tremolo + unpitched trail-off) was built and REVERTED the same
   day: GP notates the gesture on fret-0 dead strings, so the trail-off had no travel and read
   as flat muted bars (Van Halen import, measure 20).
2. **It is a right-hand technique** (user rule): the fretting hand is uninvolved, so nothing
   about it may move, anchor, or dip the FHP window — the tap-transparency precedent, now the
   shared `rightHandOnset` predicate in the generator.
3. **The gesture carries a neck-position path** (user extension, superseding "direction is the
   only semantic content"): editable like regular slides — down to a neck position, then up, in
   one chain — so it stores a start fret plus a `SlideWaypoint` path whose frets are pick
   coordinates. Direction is derived per segment. The path's last offset IS the span (a scrape
   cannot ring past its path without sitting still), so no separate duration can disagree.
   GP encodes only direction, so import synthesizes a default path the user reshapes.
4. **The visual answer is the composition, interrogated and kept.** Evidence gathered on the
   way: the 4,100-arrangement local reference corpus shows **no standard composite** in its
   chart format (816 tremolo+unpitched suspects scatter across 8+ encodings; the
   tap+tremolo+slide triple covers ~31%; zero fret-0 unpitched slides); the printed-tab
   convention is an X notehead + **wavy slanted line** ("P.S." text is optional reinforcement,
   rejected here as unaggressive); BandFuse never charted scrapes; a dedicated "noise ribbon"
   counter-proposal was rejected. The composition holds up because the path makes the slide
   component literally true.
5. **Pick slides are NOTES, not a parallel stream** (user call, reversing the same-day separate
   `Chart::pick_slides` stream, which was built, then deleted). The deciding arguments: a
   scrape MUST occupy its string (it silences what it crosses — collision and sustain
   truncation are required, not hazards); slid tapped notes already established right-hand fret
   semantics inside the note stream; converting notes to/from scrapes should be trivial; and a
   separate stream re-implements the note machinery (selection, undo, range ops, distance
   rules) that scrapes need anyway. Model: **`NoteAttack::PickSlide`** — the attack axis is the
   honest home (structurally excludes the other attack kinds) and the tab's attack-icon slot
   becomes the natural 2D marker.
   - **Override, not forbid** (user design): other techniques may hold values in memory while
     the attack is a pick slide — switching back restores them within the session — but
     projections suppress them and the document writer omits them, so a saved scrape is always
     clean and validation rejects a hand-made file that is not.
   - **Accepted trades**, chosen deliberately over the stream's structural purity: technique
     exclusions are validation conjunctions (`InvalidPickSlide`), `sustain == slides.back().offset`
     is a validated invariant rather than an impossibility, transposition/retype needs a
     pick-slide special case (editor phase), and scrape-through-scrape follows the shared
     deliberate-hold rule instead of a bespoke one-hand hard cap.
   - The interim two-stream binding-timeline refactor was deleted with the stream: pick-slide
     notes ride the original trim scan natively. The one trim twist that remains: the path is
     gesture geometry like a slide-out, so the margin trim compresses its final point instead
     of flooring the tail on it.

## Non-goals

- **Measure-3-style figures**: dead notes with ordinary slide-out flags are LEFT-hand muted
  hand slides and never reclassify (pinned by regression test).
- **Detection** (plan 22's matrix) and a **right-hand placement cue** (kin to 25-Q5).

## Shipped (Phases 1–2, 2026-08-03)

- `NoteAttack::PickSlide` + attack token `pickSlide`; writer omits overridden technique keys on
  scrape notes; rules: no other techniques in a saved document, non-empty always-traveling path
  (consecutive neck positions strictly differ, start fret included), path end == sustain.
- Import: carriers (flags 64/128) convert in place — shed the mute, gain the attack and the
  corpus-derived default path (**down 17 → 3, up 3 → 17**; ~70% of corpus down-slides start at
  fret 13+ and ~80% end at or below fret 7). The lowest-string simultaneous carrier survives
  with the longest span; conflicting directions drop with a conversion note. Scrapes then ride
  the ordinary distance rules (margin trims both directions, deliberate holds) — pinned by
  tests — and `rightHandOnset` keeps the FHP generator, span derivation, ring rules, and
  anchoring blind to them (transparency pinned by the carrier-vs-rest FHP equality test).

## Decision gates

- **55-Q1 — the head (sight gate, mockup sheets pending).** Two finalists, each composed with
  the settled tail below; the sheets render both over board and tab backgrounds:
  - (A) **Narrow whitened attack triangle over a regular string-colored head** — extends the
    shipped "narrower + whiter = intensified" grammar (full vs palm mute); digit placement
    trivial in 2D; weakest channel is small-marker discrimination at scroll speed.
  - (B) **X-shaped head** (the X *is* the head, string-colored) — the print convention's
    unpitched notehead; whole-silhouette read at distance; must stay visually distinct from
    the full-mute X *overlay* on dead notes; 2D digit sits in a small box over the X.
- **Settled rendering (both surfaces)**: the path draws in the unpitched slide language with a
  **wavy/serrated "noise" texture** across the travel direction — the print convention's wavy
  line, which is also tremolo-appearance-turned-vertical — static for now (if tails ever
  animate, they animate as a family pass across all tail types); per-leg target chips in 2D;
  no text labels. New colors enter plan 54's theme struct.
- **55-Q2 — authoring.** Mostly dissolved by the note-carried model: scrapes select, move,
  delete, and undo as ordinary notes. Remaining verbs for plan 40 Phase 5's technique surface:
  the attack toggle to/from `PickSlide` (synthesizing the default path on entry, restoring
  overridden techniques within the session on exit) and path-waypoint editing via the ordinary
  slide-editing patterns. Transposition/retype gains its pick-slide special case there.

## Remaining phases

3. **Projection + 2D tab.** Projections suppress overridden techniques on scrape notes (the one
   override seam) and mark path waypoints unpitched/unlinked; the tab draws the 55-Q1 head,
   the wavy path diagonals, and per-leg chips. Pixel tests.
   **Exit.** Tab renders a down-then-up chain per the mockup pick. **Verify.** Build; common
   core/ui + editor ui suites.
4. **Highway treatment.** The 55-Q1 head at the anchor lane and start-position X (numberless —
   the board's X axis places the start spatially); wavy shimmer tail following the waypoint
   path; no FHP-window coupling; no floor furniture. Sight-iterate on Van Halen measure 20.
   **Exit.** User signs the treatment on sight. **Verify.** Build; sight pass; plan 54 color
   coordination.
5. **Editor verbs + acceptance.** 55-Q2's remainder; then the acceptance bundle.
   **Exit.** Acceptance below in full. **Verify.** Build, full suites, pre-commit, the
   two-measure sight pass.

## Acceptance

- Van Halen measure 20 renders the chosen treatment; measure 3 imports byte-identical.
- A chart round-trips scrape notes (latent overrides cleared on save); the editor can author,
  reshape, and remove them, and toggling the attack away and back within a session restores
  overridden techniques.
- FHP output is identical with scrape notes present or absent (the transparency invariant, as
  a test).
