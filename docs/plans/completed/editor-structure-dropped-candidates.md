# Editor Structure — Dropped Candidates

Status: completed decision record for intentionally dropped process. These are steps from the
original master plan that are **not worth formalizing** on a solo, early-stage project. They are
recorded here so the decision is explicit and so a future reader does not re-add them by default.

Dropping the *formal process* does not forbid the underlying good judgment. The point is to delete
the bureaucratic deliverable, not to act carelessly — the judgment each step encoded still happens
naturally while editing code.

---

## 1. Formal Codebase-Wide Type Inventory And Friction Scoring

**What it was.** A process step that generated an inventory of every production type across all
`core`/`audio`/`ui` modules (and `app/` composition types), recording for each one its name, path,
public/private status, owning module, apparent vs. actual role, whether name/folder/namespace reveal
the role, and a proposed action — then scored each type's readability friction on a 0–3 scale and
produced a standing findings table as a deliverable in `docs/plans/in-progress/`.

**Why dropped.**

- It is process ceremony with a large up-front cost and little payoff for one developer who already
  knows the codebase.
- The useful part — noticing that a name or folder misstates a type's role — happens naturally while
  touching that code during the active extraction work. The active plan already lists the specific
  editor names worth fixing; a formal scored inventory adds overhead without adding decisions.
- A static inventory document goes stale the moment the code moves, creating exactly the kind of
  parallel-doc maintenance burden the consolidation was meant to remove.

**If it ever matters.** If the codebase grows enough that a new contributor genuinely cannot find
types, do a one-time, *editor-scoped* pass at that moment — not a standing cross-module deliverable.

## 2. Namespace Audit As A Standing Stage

**What it was.** A dedicated stage to audit, after names and folders were settled, whether any new
namespace should be introduced — with a candidate role-namespace vocabulary to evaluate against.

**Why dropped.**

- The expected outcome was already "no new role namespaces," and adding role namespaces
  (`ports`, `view_state`, `controllers`) is an explicit non-goal. A whole stage to confirm a non-goal
  is overhead.
- The project is already nested by product and module (`rock_hero::editor::core`,
  `rock_hero::common::audio`). That is sufficient. A new namespace should only ever appear when it
  represents a real semantic domain (e.g. a future game scoring or tone-graph domain), and that
  decision belongs to whoever builds that domain, at that time — not to a structural audit now.

**If it ever matters.** Introduce a namespace inline, with justification, when a genuine semantic
subdomain appears. No standing audit is needed to permit that.

---

## Net Effect

What survives from the original taxonomy ambition is the lightweight, useful part, kept in
`editor-runtime-extraction-plan.md`: clear role-based naming, editor `controllers/` / `view_state/` /
`workflows/` folders, and `editor/ui` source grouping — applied to the modules actually being worked
in, while they are being worked in. The heavyweight cross-module cataloguing and the namespace
audit are dropped.
