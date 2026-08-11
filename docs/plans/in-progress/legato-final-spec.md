# Legato — the final ruled specification (DERIVED-DIRECTION, as amended)

**Status: RULED 2026-08-11 through direct user walkthrough; red-teamed (4 adversarial lenses +
judge, all findings folded in below); awaiting implementation go.** This document consolidates
every amendment from the ruling conversation and supersedes the model sections of
`legato-authoring-model.md` (rewritten to match after implementation). Analysis trail:
`legato-simplicity-analysis.md`, `legato-design-options-explained.md`,
`legato-tap-notation-final-pass.md`.

## Stored model

`attack ∈ {Pick, Legato, LeftTap, Tap, Pinch, PickSlide}`. `Legato` is the authored relational
statement "this onset connects to its same-string predecessor"; `LeftTap` is the authored local
statement "the fretting hand strikes this from nowhere" (the old `Ctrl+H` meaning). **No
direction is ever stored.** Serialization tokens swap in place; no back-compat, no old-token
acceptance. All other fields, D13's constants, spans, and nodes are unchanged.

## Read model

One total resolver — today's `derivedLegatoAttack` retargeted, clauses byte-identical:
`resolveLegato(note, predecessor, predecessor_effective_sustain, tempo_map) →
{Hammer, Pull, Unjustified}`. `LeftTap` resolves to the hammer motion unconditionally.
Resolution reads the predecessor's STORED fields only (no cascade). A shared helper computes
per-note resolutions in one O(n) same-string walk (saved forms + `chartEffectiveSustains`),
living in common/core beside the resolver, consumed by the tab lane, the highway, the gameplay
build, and the reader — computed per chart revision, never per frame. Display draws the
RESOLVED motion with today's mark geometry; an `Unjustified` `Legato` draws and scores as a
plain Pick. **No cue, no lint, no dormant-state chrome.** The no-indicator choice is justified
by TRANSIENCE (broken claims cannot cross a top-of-history settle or reach any file), not by
"marks always show stored data" — mid-burst, a broken claim and a true Pick are pixel-identical
by design.

## The `H` toggle (final law — the planner is the oracle, the resolver the only authority)

- Domain: selected notes with attack ∈ {Pick, Legato, LeftTap}; picking-hand riders (Tap,
  Pinch, PickSlide) skipped in both directions.
- **The law is the shipped W5 oracle law, restated for the new model.** Build the apply plan by
  asking `resolveLegato` per in-domain note — with the assist's grown tail applied first —
  setting `Legato` exactly where the claim resolves. There is NO separate skip list: a note
  skips because the resolver says `Unjustified` for it, never because an enumerated rule says
  so. (Illustrative consequences, not rules: no same-string predecessor → skip; equal released
  fret → skip; fret-hand-harmonic predecessor → skip; a noded note under a higher predecessor →
  skip, since the Pull clause vetoes nodes.) **If the plan is non-empty, apply it. Only when
  applying would change nothing does the press mean clear, and the clear flattens the
  stored-`Legato` subset only — a `LeftTap` keeps its attack through clear** (Ctrl+H is the
  tap's sole author; plain H never destroys one).
- **The assist:** for a note being set whose predecessor's effective hold does not reach (gap
  at/past the kept-sustain bound), grow the predecessor's tail to `(distance − margin)` in the
  same plan when that makes the claim resolve and the growth is within `sustainGrowthLimit`.
  The assist SKIPS gesture-carrying predecessors (a scrape, or any note with a slide-out): the
  tail is that gesture's authored window, and the assist must not reshape it — manual drag
  remains the authoring path there.
- **Counted-skip feedback ships with this implementation** (W5's second half, minimal form): an
  H press that skipped notes reports the count and the dominant reason through the existing
  view reporting seam, so an all-skipped press is never a dead key.
- **The toggle window** (shipped `ChartLegatoToggleEntry` + `dropTop`) is kept: `H` again with
  the same selection and history top reverses the previous press exactly, grown tails included.

## `Ctrl+H`

`planSetAttack(LeftTap)` — sole author of the left-hand tap, no predecessor needed, E4 gate
(`fret > 0 || harmonic node`), the shipped node-conversion guards. Label: "Left-Hand Tap"
(command id value retained).

## Validation and the two flatten sites

Validation is **intra-note only** (E4 binds Tap and LeftTap; geometry rules unchanged). The
relational E5/E12/E19 content lives solely as resolver clauses. Two flattens, split by class:

1. **In-plan, intra-note** (~15 lines in `finalizePlan`): an attack stranded at
   sounding-position 0 by its own note's edit flattens to Pick, riding that entry. Immediate,
   because the gate still binds intra-note truths mid-burst.
2. **At settle, relational — the settle sweep** (stateless): at every settle event, sweep the
   chart for `Legato` notes whose claim no longer resolves and flatten them to Pick in one
   batch. No window state, no flagged keys, no proofs.

**Commit shape (red-team D1):** the sweep mutates the model only while the history cursor sits
on top. There it folds via `replaceTop` when the top is this burst's chart-notes entry and not
the clean state; otherwise it pushes its own labeled entry (at top-of-stack a push truncates
nothing). At a mid-stack resting point — reachable only through undo — the sweep DEFERS: bytes
stay untouched, the claim displays as its resolution, and the redo branch survives. The
invariant reads: **`Unjustified` cannot survive a settle at the top of history.** Every project
write additionally serializes the RESOLVED form — a `Legato` whose claim does not resolve
serializes as Pick, through the existing latent-override saved-forms seam — so **every file
ever written satisfies the invariant unconditionally**, with no history mutation at write time
(the same memory-richer-than-file philosophy scrape latents already use).

**A sweep that commits anything — fold or push — closes both coalescing windows (the fret
entry and the toggle window); a sweep that finds nothing leaves them armed** (red-team D3:
a fold changes entry content without moving the history position, so an armed window's proof
would otherwise pass and reverse a stale plan).

**Settle events, precisely (red-team D4):** every *user-initiated* selection replacement
wherever it enters (follow-the-edit selection rewrites inside `applyChartEditPlan` are NOT
settles — they would collapse the burst); every project write verb (Save, Save-As, Publish),
settling every arrangement it writes; arrangement/project switch, sweeping the chart being
departed; Esc runs the sweep as the last step of every press, whichever rung consumed it;
transport seek; playback start. At any shared event the sweep runs after that event's own
state changes complete. Undo/redo are deliberately NOT settle events (mid-stack states may
transiently hold broken claims; see commit shape).

**Load paths (red-team D5):** the reader runs the sweep at load — import completion and plain
open alike — surfacing the same conversion notes on every path; an open that converted
anything marks the session dirty (memory no longer equals disk). The game reader discards the
notes channel; the game can never see `Unjustified` (it only loads written files, which are
resolved by construction).

## Undo

Entries store values; replay is bit-exact with zero derivation at undo time. Mid-burst undo
needs no special handling: the sweep judges only current state, and defers mid-stack. The
5→3→2 story is pure re-projection — the note's bytes never change unless a top-of-history
settle happens while the claim is broken, and then one undo of the folded entry restores the
edit and the claim together.

## Tails

**Tails are never deferred and never display-divergent under this model**: the assist writes
tail growth as ordinary plan data in the same entry, and the settle sweep touches attacks
only. The one stored≠displayed tail in the system predates this design: the span-held chord
member (3D draws it held to the span's end while storing sustain 0 — the recorded W9-A
divergence; 2D draws bare heads for the same chart). Ruled direction: do NOT materialize
span-implied holds into stored sustains — that stores a derivable datum which goes stale when
the span changes, the exact sin this design removes. The span stays the one authored datum;
member holds stay derived. The W9-A fix is display-side unification (2D adopts the same
derived span-hold rendering through the shared projection helper) and is in scope.

## Tail lock

The legato halves of the W6 family are dissolved (ruled 2026-08-11): the tail lock, the break
verb's legato half, and 40-Q5's legato indicator all narrow to slides-only. Shrinking a
connecting tail drops the mark live (projection), is repairable within the burst by regrowing,
and settles as one folded, exactly-undoable batch. Slides keep lock, break, and 40-Q5
(waypoints are real data).

## Import

`left_hand_tapped → LeftTap` (verbatim intent — fixes the shipped aliasing); hopo destination
→ `Legato`; the completion-time settle sweep converts junk (equal/absent-predecessor flags,
open-string tap claims) to Pick with conversion notes. The settle-me-Pull machinery and the
import-completion normalize call are deleted.

## Sequencing note (red-team decision 5)

W4/E25's future muted-tail trim interacts with the resolver: today a full-muted tail counts at
its stored value for the hold test, so muted-tail legato resolves; when W4 trims muted tails,
dependent claims corpus-wide would flatten at next load. Recorded as a watch item: W4's trim
must run as an editor plan operation so the flatten rides the trim's entry, never a silent
load-time conversion.

## Deleted outright

`normalizeChartLegato` and the whole relational repair engine; the Layer 2 recalc window
(cancelled unbuilt, task #63); the derivation/oracle-restatement toggle machinery; the old
D14 assist body (rewritten into the new toggle planner); the cue/lint/chrome obligations;
relational validity rows; the importer settle dance. Kept: `predecessorHoldReaches` + both
constants, `chartEffectiveSustains`, `sustainGrowthLimit`, `finalizePlan`'s funnel, the fret
entry window, the toggle window + `dropTop`.

## Signature ledger (final, red-team-corrected)

Overturned and re-signed by this ruling: Question B's storage half (the enum split, merged
display kept); D7's derivation half (pull-from-a-scrape resolvable — but the assist never
grows a gesture carrier's tail); ruling 2's legato lock half, ruling 3's legato break-verb
half, and 40-Q5's legato half (all narrowed to slides); the notation pass S1–S5. Restored
rather than overturned: Option C's refusal substance — via the resolver as the one authority,
not an enumerated list; the W5 oracle toggle law — kept verbatim in the new model; A5/A6's
substance (the sweep at commit points + resolved serialization); C3's spirit (relational
flattens ride the burst's entry where the cursor allows, defer otherwise). D10's no-indicator
justification is transience, not stored-data display. Not touched: D13 entire, released-fret
semantics, D16/E25, the forward-H rejection, undo exactness (strengthened).
