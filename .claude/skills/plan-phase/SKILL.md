---
name: plan-phase
description: Execute exactly one phase of a docs/plans/ roadmap plan — re-verify the plan's baseline stamp against the code first, implement only that phase, run that phase's exact verification commands, and update the roadmap status. Use when asked to execute or resume a numbered phase of a named plan file.
argument-hint: "<plan-path> <phase-number>"
---

# plan-phase

Execute one — and only one — phase of a plan under `docs/plans/`. Arguments: the repo-relative
plan path (e.g. `docs/plans/12-playback-clock.md`) and the phase number.

## 1. Read the required context

- The named plan file, in full.
- `CLAUDE.md`.
- Every `docs/design/*.md` document the plan cites (at minimum the ones its Constraints and
  phase text reference). Do not substitute memory of these documents for reading them.

## 2. Re-verify the Current-state inventory BEFORE implementing

Every plan's Current state inventory ends with a stamp:
`Verified against code on <date>, refactor @ <hash>`. Plans are allowed to go stale between
writing and execution, so trust the code, not the plan:

- Use `rg -n` to confirm the inventory's file paths, type names, and behavioral claims that this
  phase depends on still hold in the working tree.
- If anything is stale: update the plan's inventory section FIRST (correct paths/names/claims,
  refresh the stamp with today's date and the current `refactor` HEAD hash), note each correction
  explicitly in the plan, and reconcile the phase text with the corrected reality before writing
  any code. If a correction invalidates the phase's design, STOP and surface that instead of
  improvising.

## 3. Execute ONLY the named phase

- Implement exactly the scope that phase defines — no other phases, no adjacent cleanups, no
  opportunistic refactors. Out-of-scope discoveries get noted in the report, not fixed.
- Honor the plan's Constraints section (layering, naming firewall, builds through
  `.agents/rockhero-build.ps1`, etc.) and the phase's stated public-header impact and testing
  plan.
- If the phase is marked "assumes outcome X" behind an unclosed decision gate, STOP — gated
  phases must not start before their gate closes.

## 4. Verify with exactly that phase's commands

Run the verification commands the phase itself lists (its `rockhero-build.ps1` invocations) —
no more, no fewer. Do not substitute raw `cmake`/`ninja`/`ctest`, and do not escalate to the
full acceptance bundle unless this IS the plan's final acceptance phase (then use the
`verify-rockhero` skill).

## 5. Update the roadmap status

Update the plan's status entry in `docs/plans/00-roadmap.md` (and the plan's own Status line if
the plan tracks per-phase progress): which phase completed, date, and any correction made in
step 2. Keep the edit minimal — status, not prose.

## 6. STOP on open questions

If executing the phase surfaces an open question, an unresolved decision, a design-doc conflict,
or a gate condition — stop and present it to the user with options and a recommendation. Never
silently decide. This includes questions already listed in the plan's "Open questions for the
user" section and new ones discovered during implementation.

## Report

End with: phase executed, files touched, verification commands run and their results, roadmap
status change, inventory corrections (if any), and open questions surfaced (if any).
