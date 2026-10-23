# Plan 60c — Grips: naming what a hold holds

## Status

**Sub-plan of `docs/plans/roadmap/60-the-hand.md`. NOT STARTED; begins after plan 60b
(`60b-holds.md`) is sighted.** Recorded 2026-10-16 when grips were split out of 60b. Re-verify
against the code before execution.

## Scope

- Grip templates: chart-scoped, one stop per string with a required finger (`1`–`4`, `T`), saved
  in the chart's `grips` and referenced by id from a hold's `grip` field (60b's third alternative
  beside unnamed and `none`).
- What a named template adds to 60b's validity predicate (a note on a string the template states
  versus one it leaves out).
- An authored grip may state an open string with a fretted note above it on the same string (a
  hold of the open stop under, say, a 4): legal for an authored hold, never inferred by the
  derivation, which ends a span where a finger lands on a string ringing open.
- A named grip may state stops the notes never sound (a multi-fret grip over a single note, say),
  so a finger can lift mid-sustain and end the grip without changing any sound. A named-grip hold
  may therefore end where the notes show no close: its end is the charter's statement. An unnamed
  hold keeps 60b's rule that its end must fall where the walk would close. So `Ctrl+H` over a range
  whose end is invalid for an unnamed hold opens the grip picker instead of refusing, giving the
  charter the chance to state a grip that makes the end valid. It refuses at once only where no grip
  could: every string sounds across the end, leaving no silent string for a finger to lift from
  inaudibly. Finer impossibilities (no free finger, reach) are refused when the grip is confirmed,
  and cancelling the picker refuses. In 60b, which has no grips, such a `Ctrl+H` is refused.
- The grip picker and the "New Grip Template..." fingering dialog: `Ctrl+H` and `Enter` on a
  derived span or hold open it; naming a grip is never required to author a hold.
- The export gate: export refuses while a span has no named grip, with the toolbar count. Save
  never warns.
- **Re-evaluate the glide split (user, 2026-10-22).** Since `135371bb` every stored fret keyframe
  lands a glide and opens a span of its own (shape rule 4), so a grip gliding through several
  keyframes with no onset between them derives a span per keyframe. That is technically correct
  but noisy where the grip slides quickly: My Chemical Romance - Helena 85:2-85:4, a dyad 6/8
  sliding 7/9, 9/11, 7/9, 6/8 at eighths, is five spans. Creed - What If 102:1-104:1 (1 to 7 to
  11, three spans) reads right. Kept for now; decide when grips are authored. The fingering is what
  makes the question answerable: the same fingers are provably held throughout a slide, so the
  first grip determines and locks in the fingering of every grip it slides into. Options recorded:
  keep the split at every keyframe; split only at the next onset (which also merges What If's 7
  and puts the hand's move inside the span, so row "spans crossed by an FHP shift" would exempt
  slide carriage again); or split only where the grip rests at least a named minimum.

## Parts bin

Branch `plan-60b-grip-holds` built all of the above for the first 60b design: the template types,
reader and validators (`de9e2305`), the picker and dialog (`5ff62510`), the export gate and count
(`edb34cf3`). Reuse what fits the stated-extent hold; its fit, merge and close rules do not
carry over.
