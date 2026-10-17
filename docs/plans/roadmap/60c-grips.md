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
- The grip picker and the "New Grip Template..." fingering dialog: `Ctrl+H` and `Enter` on a
  derived span or hold open it; naming a grip is never required to author a hold.
- The export gate: export refuses while a span has no named grip, with the toolbar count. Save
  never warns.

## Parts bin

Branch `plan-60b-grip-holds` built all of the above for the first 60b design: the template types,
reader and validators (`de9e2305`), the picker and dialog (`5ff62510`), the export gate and count
(`edb34cf3`). Reuse what fits the stated-extent hold; its fit, merge and close rules do not
carry over.
