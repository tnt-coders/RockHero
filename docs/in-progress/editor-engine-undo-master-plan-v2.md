# Editor Engine And Undo Master Plan v2 (superseded)

**Superseded by `editor-engine-undo-master-plan-v3.md`.**

v2 assumed the current live-rig adapter shape and implicitly pre-committed undo to RockHero mementos.
The undo-ownership investigation (`undo-ownership-analysis.md`) showed that the arguments against
leaning on Tracktion's undo were largely artifacts of current engine implementation choices (lazy
structural-plugin creation, imperative monitoring-route rebuilds) rather than fundamentals, and that
VST3 parameter undo *is* achievable through Tracktion via a gesture-settle state flush.

v3 therefore front-loads behavior-preserving **baseline engine cleanup** (eager structural plugins,
and routing centralization — B2-lite by default or B2-full only if delegation is chosen) that is
valuable on its own merits, adds explicit **evaluation gates**, and makes the undo **mechanism** an
evidence-based decision taken on the cleaned base. See
`editor-engine-undo-master-plan-v3.md` for the active plan, `editor-undo-plan.md` for undo detail, and
`undo-ownership-analysis.md` for the source-level analysis.
