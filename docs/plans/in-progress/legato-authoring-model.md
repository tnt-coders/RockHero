# Legato Authoring Model — hammer-on, pull-off, and the left-hand tap

Status: **DEEP REVIEW COMPLETE 2026-08-08 — recommendation ready for the user's ruling.** The
in-depth review the user parked this for has run: candidate A4 survives, but only in one specific
form (the recalc window **settles on any undo or redo**), and in that form the binding constraint —
*undo must always restore exactly the pre-action state* — holds by construction rather than by
discipline. The full design is below, with the sequences that killed every alternative. Plan 40
Phase 5's first verb (`H`) shipped in `4a98da55` with the naive derivation; its defects are recorded
in [What the shipped verb gets wrong](#what-the-shipped-verb-gets-wrong). Nothing beyond that commit
is implemented.

Scope note: this concerns **legato only**, and that is a finding rather than an assumption — see
[Does this generalize?](#does-this-generalize) at the end.

## Why this exists

`planSetLegato` derives hammer-versus-pull from the fret relationship to the previous note on the
same string, once, at authoring time. Every input to that derivation stays editable afterwards, so
the stored attack can drift out of agreement with the data that justified it. The user also spotted
that the derivation's *refusals* are wrong, because `NoteAttack::Hammer` is overloaded — and later
set the constraint that decided the whole design: undo/redo must always be exact, never leaving
state different from before the action.

## The load-bearing fact: Hammer and Pull are not symmetric

Already recorded in the importer (`gp_chart_builder.cpp`, the `left_hand_tapped` branch):

> A left-hand tap is the fretting hand hammering the note from nowhere (no pick stroke), which the
> hammer-on states accurately — no separate notation. **Always a hammer, never a pull: nothing is
> released to sound it.**

So in this project's model a left-hand tap **is** a hammer-on, `NoteAttack::Tap` is the right-hand
tap only, and the two readings of `Hammer` are deliberately indistinguishable. Therefore:

- **`Hammer` needs no predecessor.** Its precondition is a positive sounding position:
  `fret > 0 || harmonic_node.has_value()` (E4 as amended — the node form is what admits the
  fret-0 tap harmonic, and node positivity is already guaranteed by the enforced range rule).
- **`Pull` requires a preceding note on the same string at a higher fret**, because something must
  be released to sound it. Its *target* may be fret 0: pulling off to an open string is ordinary.

## Settled before the review

### 1. The derivation, and a force verb (user: "I think I agree")

| Situation | Result |
|---|---|
| predecessor on the same string releasing a **higher** fret | **Pull** |
| predecessor lower, equal, or absent — and sounding position positive | **Hammer** (hammer-on or left-hand tap, indistinguishable by design) |
| `fret == 0`, no node, no higher predecessor | **refuse** — neither articulation is physically possible |

**`Ctrl+H` forces `Hammer`** (user's proposal), valid iff the sounding position is positive,
covering the rare descending left-hand tap. It fits the grammar: `Ctrl` means *precision* when
placing, and "I will state it exactly, do not infer" is that idea applied to a typed verb. There is
deliberately **no force-Pull** — pulling off to a higher fret is physically impossible, so its
absence is principled.

### 2. The toggle measures the eligible subset (user: "I also think I agree")

The shipped toggle computes `all_legato` over the whole selection, so a selection containing a note
that can never be legato (an open string) can never satisfy it. Fix: compute the toggle over the
**eligible subset** — if every note that *can* be legato already is, clear. That guarantees a second
press undoes the first. Rejected alternative: "if any is legato, clear" — it breaks extending legato
across a group.

### 3. No distance bound, and other strings are already handled

The scan takes the last earlier note on the **same string**, so an intervening note on another
string is correctly ignored. Do **not** bound the derivation on time distance: a far descending
predecessor is probably a left-hand tap, `Ctrl+H` states that in one keystroke, and a threshold
would be a magic constant guessing at what the user can say exactly.

**Trap to avoid:** do not require `Pull`'s predecessor to still be *sounding*. Charts legitimately
carry zero-sustain notes where a pull-off is correct — `sustain` is the drawn tail, not the physical
ring — so that check would break ordinary charts.

## The recommended design (review outcome, 2026-08-08)

Two layers. The bottom layer is A1 (impossibility-only normalization), always on; A4 is a transient
window layered on top of it. Together they satisfy both of the user's wishes — *restore the legato
when the relationship becomes valid again* and *no surprises later* — which no single mechanism did.

### Layer 1 — always-on impossibility normalization, in the planners

Every chart-note planner finalizes through a shared normalization that runs after the 40-Q2-B
sustain pass, over the whole candidate stream, and repairs only **impossibility**:

| Invalid state found in the candidate stream | Repair |
|---|---|
| `Pull` with no same-string predecessor, or predecessor moved off-string/later | → `Pick` |
| `Pull` whose predecessor's released fret is **equal** | → `Pick` (no direction ≠ left-hand tap) |
| `Pull` whose predecessor's released fret is **lower** | → `Hammer` (a genuine hammer-on now) |
| `Pull` whose predecessor cannot be released — a scrape, or a fret-hand harmonic (E19) — or which itself carries a node (E12) | → `Hammer` if a genuine lower fretted predecessor justifies it, else `Pick` |
| `Hammer` with sounding position 0 — fret 0 and no node (E4) | → `Pull` if a valid higher predecessor exists, else `Pick` |
| `Hammer` in any other configuration | **untouched** — always possible as a left-hand tap; a deliberate `Ctrl+H` survives |

"Released fret" of a predecessor = the fret of its last pitched slide waypoint when it carries one,
else its `fret` (user call 3 below). Repairs ride the same undo entry as the edit that exposed them,
exactly like 40-Q2-B truncations — one plan, one entry, exact inverse. `ChartNotesEdit` replays
stored values, so undo/redo restore both halves atomically with **no derivation at undo time**.

Whole-stream scope means the first edit after an import carrying invalid legato also repairs that
imported data, riding the edit's entry — the same action-at-distance `normalizeSustainOverlaps`
already ships, and the "chart valid at every commit point" rule already means. Stated so it is a
documented property, not a surprise.

Layer 1 alone is the shipped fallback: cleared stays cleared, no surprises, the 5→3→2 case
disappoints.

### Layer 2 — the recalc window

Controller-owned transient state, shaped exactly like the existing multi-digit fret-entry window
(`ChartFretEntry`): the flagged note keys, the selection key-set immediately after the birth edit,
and the undo-stack cursor position. **This is the precedent to copy verbatim** — the fret-entry
window is already transient state made undo-safe by validity checks (`entry.keys ==
chartSelection().notes() && snapshot().position == entry.history_position`, with the recorded
rationale "any interleaved edit or undo moves the position and kills the window"), not by teardown
discipline.

| State of a legato note | Meaning |
|---|---|
| **Settled** (default) | Attack is authored data. Only an edit that makes it *impossible* (Layer 1) changes it. |
| **Recalculating** | Its justification was disturbed by the birth edit; while the window is open, every further chart edit re-derives its attack and folds the change into that edit's plan — same entry. |

- **Birth — at the invalidating edit** (the doc's earlier second reading, now confirmed): when
  Layer 1 repairs notes inside a plan, the planner reports the repaired keys and the controller
  flags them, scoped to the post-edit selection.
- **Participation:** before building any subsequent chart plan, the controller checks the flag's
  validity (selection key-set unchanged, history position unchanged). Valid → the flagged keys pass
  into finalize, which re-derives each flagged note's attack; any change rides that edit's entry and
  the stored history position refreshes. Invalid → the flag clears before planning.
- **Settle** (flag dies; no chart mutation, no undo entry): any selection replacement through the
  `setSelection` funnel, any caret re-arm that changes the key-set (plain press, Ctrl-toggle,
  double-click chord, marquee, arrow/measure/row stepping), the empty-lane click, `Esc` at any
  marker rung past gesture-cancel, playback start, a plain transport seek (explicit clear — the
  selection check alone won't catch it), project/arrangement switch, and **any undo or redo**.
  A caret *riding* a single-note nudge is deliberately not a settle — the move is a participating
  edit. Save/markClean does not interact with the flag.
- **A note that settles as `Pick` is out** — no longer legato, so no future disturbance flags it;
  rejoining takes a fresh `H`. This falls out structurally: flags are only born on notes that were
  `Hammer`/`Pull` when disturbed. A note that settles as `Hammer`/`Pull` is an ordinary legato note
  again, and a later invalidating edit may birth a new flag — re-entrant per disturbance, never per
  note.
- **Invalidation is defined on the value of the justification tuple** (predecessor exists on the
  string; released fret vs the note's fret; predecessor releasable; the note's own sounding
  position and harmonic state) — **never on predecessor identity**. Then a Phase 6 split that
  interposes a same-fret tail note correctly does not invalidate, and every future verb is judged
  by one rule instead of a list.

### The undo contract, and why the guarantee holds by construction

- An undo entry contains **chart data only**: the plan's removed/inserted note values, including
  every attack repair the edit caused. Never the flag.
- **Undo/redo replay stored plans and never derive.** The chart after undo is bit-exact the
  pre-action chart; after redo, bit-exact the post-action chart. (`chart_edits.h` states the
  contract: "undo round-trips are exact by construction.")
- **Any undo or redo settles the window** — structurally via the history-position check, plus an
  explicit clear for hygiene, the same dual mechanism the fret-entry window uses.

Why settling on undo is *required*, not merely safe — the invariant and its one hole: at every
committed history point inside the flag's lifetime, the flagged note's attack equals what
re-derivation would produce (the mechanism itself wrote it), so a stale flag firing there is a
no-op. The point *before* the flag's birth is not covered — a deliberate `Ctrl+H` puts an attack
there that derivation would never produce. The kill sequence for the flag-survives-undo variant:

1. P=7, N=3, `Ctrl+H` on N — forced Hammer (deliberate descending left-hand tap).
2. Retype N's fret to 0 → E4 repair to `Pull` rides the entry, flag born.
3. **Undo** — chart restores N to fret 3, forced Hammer.
4. Retype N 3→4 — disturbs nothing under the rules — but a surviving stale flag fires, re-derives
   7>4, and rewrites the deliberate Hammer to `Pull`: an attack change with no visible cause,
   destroying an explicit user assertion.

So the flag dies on undo. The only divergence this leaves is that the *window* is closed after
undo where the original timeline had it open — exactly the semantic the fret-entry window already
shipped (type `1`, undo, type `2` → fret 2, never 12), and the correct one: Ctrl+Z means "that was
wrong," not "keep recalculating."

The multi-digit interaction composes: a widen replans from the pre-entry base with the flag's keys
passed through, so a repair that round-trips (5→1→12 with a pull at 3) drops out of the widened
entry entirely; `replaceTop` does not move the cursor, so the flag survives a widen. The one
delicate integration point is that the incremental live-chart step must funnel through the same
normalize as the widened plan so both land on the identical stream — a test must pin that.

### What the user sees

Marks always show the live chart truthfully: 5→3 visibly drops the pull-off mark to plain, 3→2
raises the hammer-on mark. **Keep the flicker; add no extra chrome initially.** It is the honesty
rule the fret-entry window already ships (each keystroke applies immediately so the notation always
shows the value being typed), and the flicker *is* the feedback that the edit broke and re-formed
the legato. A subtle recalculating cue is a legitimate later addition if real use shows confusion —
editor chrome like the insert ghost, not notation.

## Rejected alternatives, ranked, each with the sequence that kills it

1. **A4, flag survives undo** — the kill sequence above: destroys a deliberate `Ctrl+H`.
2. **A4, flag stored inside undo entries** — violates the pure-history memento shape (the history
   "never applies editor side effects"; `EditorEditContext` deliberately has no interaction state),
   and a restored flag would be scoped to a selection undo does not restore — dead-on-arrival or
   wrongly scoped. All cost, no value: inside its lifetime a restored flag is inert anyway.
3. **A3, stored intent — REJECTED by the user, and the review found the stronger reason:** intent
   persists forever, so derivation is live forever — edit the predecessor next week and the
   direction flips. A3 **reintroduces the 10-minute surprise by construction.** Clearing intent on
   invalidation to prevent that reduces A3 to A1 plus a wasted format field. (It is, ironically,
   perfectly undo-consistent — so if it ever returns it returns for other reasons, and it should
   not.)
4. **A2, settle timer** — 5→3, pause 800 ms, →2 produces Pick; the same keystrokes inside the
   window produce Hammer. Same keys, different chart, invisible cause.
5. **Recalc scoped to the coalescing window** — A2 wearing the `replaceTop` costume: the entry's
   open lifetime is a 750 ms typing clock, so the timer objection applies unchanged, and the window
   is strictly narrower than A4 for no gain.
6. **Explicit affordance** ("legato broken — press H to restore" hint, no implicit state) — honest
   and undo-trivial, but scope-mismatched: `H` acts on the selection and the disturbed note is not
   in it. Kept as the fallback if the recalc window confuses in practice.
7. **A1 alone** — not killed, demoted: it is the bottom layer. Standalone it fails the restore wish.

No fifth option beats A4-settle-on-undo, and the space is closed on both sides: no format change
forces the state to be transient; the undo constraint forces transient state to be undo-inert; the
only undo-inert transient shapes are validity-checked windows or no state at all; and among those,
A4's selection scope is the only lifetime that matches user intent rather than a clock.

## Question B: the left-hand tap as its own concept — DEFER

Independent of A4, with one interaction: A4 is about the *lifecycle* of re-derivation, B about the
*ontology* of `Hammer`, and A4 works identically with or without B. B would remove the one
asymmetric normalization row and the `Ctrl+H` hazard class at its root — but costs a format change
through plan 10 (the cost class that helped kill A3), notation on both surfaces (the white-box `T`
vs the black lettered-plate `T` is an unresolved legibility question, 3D is an unknown atlas cell),
and churn through every exhaustive attack switch.

**The importer comment's warning is weaker than it reads.** The hand-window, chord-span, and
floating behaviors all key off `rightHandOnset(attack)` (`chart.h`), consumed at chord-posture
formation, span closing, slide anchoring, FHP anchoring, arpeggio derivation, and the right-hand
light rise. A `LeftTap` value is a *left*-hand onset, so `rightHandOnset(LeftTap) == false`
preserves every one of those behaviors automatically. The real costs are the format change,
serialization, the attack switches, the importer branch, the E-rule table, and two notation cells.

Against the user's own test — adopt only "if it genuinely simplifies the editing workflow" — B
removes one table row while adding a verb, a mark collision, and an unknown 3D cell. **Defer;
`Ctrl+H` covers the descending left-hand tap.** Revisit only if plan 10 opens the format for other
reasons, and re-derive B with the E-rule table together then.

## The invalidating edits, exhaustively

The enumeration question is dissolved structurally: with Layer 1 in the shared finalize step, every
present and future planner gets the repair by construction — "did we miss a verb" becomes "cannot."
(Two planners bypass `finalizePlan` today and must be funneled: `planDeleteNotes` and
`planRetypeFrets`, the latter not even receiving the chart. Same-TU refactor.)

The inventory against every verb, under the value-based rule:

| Edit | Invalidates? |
|---|---|
| Delete predecessor | yes |
| Move predecessor off-string/after N; move N off-string/before predecessor | yes |
| Move a **third** note into or out of the P…N gap on that string | **yes — the generalization the earlier six-item list missed**: anything changing which note is the immediate predecessor re-tests the tuple |
| Retype either fret to equal/invert (including the wheel fret shift) | yes |
| Retype N to fret 0 without a node (E4) | yes |
| Insert between (typed digit, Alt+click, Insert) | yes |
| `planSetAttack(PickSlide)` on the predecessor | **yes — new**: a scrape has no fretted press to release |
| Future harmonic verb: node set on the predecessor (E19) or on N (E12); node cleared on a fret-0 Hammer (E4) | **yes — new**, three cells |
| Phase 6 L-merge (absorbed note changes predecessor identity) | yes, via the value rule |
| Phase 6 split | **no** under the value rule — the tail keeps the fret; this is why the rule is value-based |
| Phase 7 waypoint edits changing the predecessor's last waypoint fret | yes, iff released-fret semantics are adopted (user call 3) |
| Paste / range move / range delete (plan 52) | reduce to the classes above; covered by the funnel |
| Sustain edits | no — settled (predecessor need not still sound) |
| Bend, vibrato, tremolo, accent, mute edits | no — none enters the tuple (a fully-muted predecessor is still a press) |
| Tuning capo/cent edits | no; a future string-count edit reduces to move-off-string |
| Undo/redo themselves | **never** — they replay stored plans and bypass the planners, which is required for exactness |

## What the shipped verb gets wrong

Defects in `planSetLegato` as of `4a98da55`, to fix when this design is implemented:

- **Scrape predecessor.** The derivation reads `previous->fret` even when the predecessor is a
  `PickSlide`, whose fret is picking-hand travel rather than a press — it can derive a Pull from a
  scrape. E5 must exclude right-hand-onset predecessors from justifying a Pull. (`Tap` predecessors
  remain valid: pulling off from a tapped note is the standard tapping figure — the tapping finger
  is a real fretboard press.)
- **Onset fret vs released fret.** The derivation compares the predecessor's onset fret; a
  predecessor that slid 5→7 followed by a pull-off to 5 is musically a pull from 7, but the verb
  sees 5 vs 5 and refuses. User call 3 below.
- **E-rules absent.** The verb consults `harmonic_node` nowhere: it can derive Pull into a harmonic
  (E12) and Pull from a fret-hand harmonic (E19). The full derivation folding the rules: **Pull**
  iff a valid higher releasable predecessor exists and N carries no node; else **Hammer** iff
  `fret > 0 || harmonic_node.has_value()`; else refuse. Pressing `H` on a **pinch** needs its own
  guard: converting the attack away from `Pinch` silently reinterprets an off-neck node (24.0) as a
  fret-hand node — teleporting the hand to fret 24 — so the verb must clear or refuse, a decision
  for the matrix enforcement pass.
- **Doc fact corrected:** the coalescing window is **750 ms** and controller-owned
  (`g_fret_entry_window_ms`); `EditorUndoHistory::replaceTop` is only the splice mechanism, and it
  refuses when a reachable clean marker sits on the top entry — a mid-window save breaks coalescing
  by design. The recalc window never hits that refusal because each recalc edit is its own entry.

## Remaining user calls

1. **Recalculating-state chrome** — recommended: none initially; the honest live marks are the
   feedback. Add a subtle cue only if real use shows confusion.
2. **Empty-selection scope after a delete** — recommended: let the flag live with the empty
   post-delete scope, so the natural delete-then-retype-at-the-armed-caret flow re-derives N against
   the replacement note, one-shot; the alternative (empty scope = no scope) is simpler and costs one
   extra `H` in that flow.
3. **Released-fret semantics** — recommended: last pitched waypoint (the musical truth and what tab
   notation means) over onset fret; if adopted, the 2D relationship drawing should follow the same
   rule, and Phase 7 waypoint edits join the invalidating set.
4. **Question B** — recommended: defer (above).
5. **Layer 1's whole-stream sweep** — recommended: global (precedent-consistent with sustain
   normalization); the alternative is edit-scoped repair, which leaves imported invalid legato
   standing until touched.

## Does this generalize?

Checked against every remaining Phase 5 field: **no, and that is load-bearing for sequencing.**

| Field | Neighbour-dependent? |
|---|---|
| `mute`, `vibrato`, `tremolo`, `accent` | no — unconditional |
| `harmonic_node` | no — its constraints (range, node > fret, pinch requires one) are intra-note |
| `attack`: `Tap` / `Slap` / `Pop` / `Pick` / `Pinch` | no |
| `attack`: `PickSlide` | no — intra-note and already enforced by the chart rules |
| `attack`: `Hammer` / `Pull` | **yes — the only one** |

Legato is the only articulation whose meaning references another note, so this blocks none of the
other Phase 5 verbs. It **does** land on Phase 6 (L-link merge/split is explicitly relational) and
Phase 7 (slide waypoints reference target frets), which is the argument for settling it here as
precedent rather than later under pressure.
