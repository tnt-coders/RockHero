# Legato — the shipped model, and the graveyard of what it replaced

Status: **IMPLEMENTED 2026-08-11** (`006ae58e` the model, `2e840872` the toggle's save case,
`7f6c4517` the scenario tests). This file is the standing record of legato in RockHero: what the
chart stores, what every consumer reads, what the two verbs do, and why each of those is the shape
it is. The *ruling* that produced it is `legato-final-spec.md`, kept sentence-for-sentence as
signed; the analysis that killed the alternatives is `legato-simplicity-analysis.md`,
`legato-design-options-explained.md`, and `legato-tap-notation-final-pass.md`. Where this file and
the code disagree, the code is the truth and this file is the defect.

**The model in five lines.** `attack` stores `Legato` — the relational claim "this onset connects
to its same-string predecessor" — and `LeftTap`, the local statement "the fretting hand strikes this
from nowhere". **No direction is stored.** One resolver, `resolveLegato`
(`common/core/chart/chart_legato.h`), answers `{Hammer, Pull, Unjustified}` from the predecessor's
stored fields, and every surface, the gameplay build, the reader, and the `H` planner ask it rather
than reading a field. A claim nothing justifies draws, plays, and scores as the plain pick it sounds
like, and is flattened to `Pick` by the stateless settle sweep (`sweepUnjustifiedLegato`) at the
ruled settle events, at load, and by the document writer as it serializes.

Scope note: this concerns **legato only**, and that is a finding rather than an assumption — see
[Does this generalize?](#does-this-generalize) at the end.

## Stored model

`attack ∈ {Pick, Pinch, Legato, LeftTap, Tap, Pop, Slap, PickSlide}`. The two connection values are
`Legato` (relational) and `LeftTap` (local); `legatoClaimable` (`chart.h`) names the family `H` and
`Ctrl+H` move a note between — `Pick`, `Legato`, `LeftTap` — and every other attack is a
picking-hand or bass articulation whose onset is already fully described.

Serialization tokens are `"legato"` and `"leftTap"` (`chart_document.cpp`), swapped in place with no
back-compat and no version bump per the standing rule. An unknown token is a read error, so every
package written before the swap fails to load until re-imported — the deliberate tripwire, recorded
in `docs/tracking/backlog.md` beside the harmonic collapse's.

All other fields, D13's constants, spans, and nodes are unchanged by this model.

## Read model

`resolveLegato(note, predecessor, predecessor_effective_sustain, tempo_map) → LegatoMotion` is the
one authority, and the ONLY place a hammer-on and a pull-off are told apart. Its clauses are the
derivation table Option C settled (below), asked on every read instead of written once:

| Predecessor situation | Resolution |
|---|---|
| none on the string, a fret-hand harmonic, or no longer holdable at the onset (D13) | `Unjustified` |
| released fret **above** the note's stop, and the note carries no node (E12) | `Pull` |
| released fret **below** the note's stop | `Hammer` |
| released fret **equal** | `Unjustified` — there is no connection to record, and inventing one would be inventing data |

The hammer row carries no "somewhere to land" test, and deliberately: a released fret is validated
non-negative, so `released < note.fret` already forces the note's stop to fret 1 or above. The spec
asked for the clauses to be moved byte-identical, which is how the old rule's redundant
`(fret > 0 || node)` disjunct survived the move; with the transplant signed off it was deleted
(2026-08-11, review fix F10) and the row is now the whole rule.

`LeftTap` resolves to `Hammer` unconditionally and reads no predecessor at all. Every other attack
is answered as the hypothetical it is — asking about a `Pick` is how the `H` toggle finds the notes
a claim would justify — so the resolver deliberately never short-circuits on the note's own attack.
Resolution reads the predecessor's **stored** fields only, so there is no cascade: resolving one
note can never change what another resolves to, and one sweep pass is therefore enough.

Two rulings landed 2026-08-12 on the statement's edges. **F3 (the review's open question) is
closed as shipped**: plain `H` on a stored `LeftTap` converts it to `Legato` where the resolver
justifies a claim — the inferring verb overriding the stating one — and skips it where nothing
does; the clear half still never touches one. What made the conversion acceptable is the second
ruling: **the stored statement is visible.** A `LeftTap` wears its own charting mark in the 2D
lane — the tap letter on the LIGHT plate, fill polarity being the plate family's hand signature —
so which notes `H` will convert and which are deliberate statements is always readable. The mark
is editor-only by the charting-mark law (it states editor-verb behavior, never performance); both
3D surfaces keep the merged hammer-motion reading.

`chartResolutions(notes, shapes, tempo_map)` answers it for a whole stream in one forward walk
carrying the most recent note per string, and returns the per-note facts that travel together
because they are computed together: `saved_notes` (`savedChartNote`), `effective_sustains`
(`chartEffectiveSustains`), `legato`, and `predecessors` — the same-string predecessor index the walk
established, handed out rather than kept private so the `H` toggle asks its own hypothetical against
the same relation instead of re-deriving it with a backward scan per selected note. The tab lane, the
highway, the gameplay build, and the package reader all consume that one pass — computed per chart
revision, never per frame. Computing them separately is exactly how those four came to disagree about
one chart.

The sweep short-circuits before that pass when no note in the stream carries a `Legato` at all: it
runs at every settle event — every caret move, seek, selection change and playback start — and the
resolutions pass copies the whole stream twice before it can answer the same question.

Display draws the RESOLVED motion with the pre-existing mark geometry; an `Unjustified` `Legato`
draws and scores as a plain Pick. **No cue, no lint, no dormant-state chrome.** The no-indicator
choice rests on TRANSIENCE — a broken claim cannot cross a top-of-history settle or reach any file —
not on "marks always show stored data": mid-burst, a broken claim and a true Pick are
pixel-identical by design.

## The `H` toggle — the planner is the oracle, the resolver the only authority

`planSetLegato` (`editor/core/src/chart/chart_edits.h`) returns
`ChartLegatoPlan{plan, skipped, reason}`.

- **Domain:** selected notes in the `legatoClaimable` family; picking-hand riders (`Tap`, `Pinch`,
  `PickSlide`) are skipped in both directions.
- **The law is the shipped W5 oracle law.** The plan is built by asking `resolveLegato` per
  in-domain note — with the assist's grown tail applied first — and setting `Legato` exactly where
  the claim resolves. There is NO separate skip list: a note skips because the resolver says
  `Unjustified` for it, never because an enumerated rule here says so. (Illustrative consequences,
  not rules: no same-string predecessor → skip; equal released fret → skip; fret-hand-harmonic
  predecessor → skip; a noded note under a higher predecessor → skip, since the Pull clause vetoes
  nodes.)
- **If the plan is non-empty, apply it.** Only when applying would change nothing does the press
  mean clear, and the clear flattens the stored-`Legato` subset only — **a `LeftTap` keeps its
  attack through clear**, because `Ctrl+H` is the tap's sole author and plain `H` never destroys
  one. Measuring the press by what the PLAN does, rather than by what the selection already holds,
  is what keeps a rider note from stranding the toggle in apply mode forever.
- **The assist:** for a note being set whose predecessor's effective hold does not reach, the
  predecessor's tail grows to `(distance − margin)` in the same plan, but only when that makes the
  claim resolve and only within `sustainGrowthLimit` — one authority with the duration verb's own
  clamp, so the assist can never author what a manual drag could not. It SKIPS gesture-carrying
  predecessors (a scrape, or any note with a slide-out): that tail is the gesture's authored window,
  and manual drag remains the authoring path there.
- **Counted-skip DATA, reporting deferred** (W5's second half, as amended 2026-08-11 by the
  independent review's F1): `ChartLegatoSkip` carries the dominant reason and `ChartLegatoPlan`
  the count, and both are pinned by `test_chart_edits.cpp` for every refusal class. Nothing is
  *shown*: `H` is silent when it applies nothing, at parity with `Ctrl+H` and the pick-slide
  toggle. The rendering half (`chartLegatoSkipText`) was deleted because the view's only reporting
  seam is a modal `showThemedWarningBox` titled "Could not complete request" — so an `H` on a
  phrase's first note, the commonest press in charting, opened a dialog to announce that nothing
  had failed. **This is where W3's refusal channel is still needed** (tasks #33/#35): the payload
  is built and waiting for a non-modal surface. The count deliberately counts only notes the
  resolver REFUSED — a note already carrying the claim is unchanged, not skipped, which is what
  lets the caller tell "nothing left to claim, so this press means clear" from "this press had
  nothing to say".
- **The toggle window:** `H` again with the same selection and history top reverses the previous
  press exactly, grown tails included — H-H leaves no trace. **The save case (ruled 2026-08-11):**
  when the proof holds but the entry is the reachable clean state, the reversal pushes the exact
  inverse as a NEW entry (`"Revert Legato"`) instead of dropping — the tail still comes back and the
  toggle stays genuine, while "return to clean" stays truthful and the session is correctly dirty.
  The window dies only on the deliberate context switches of the settle set, where the second press
  means the ordinary law and `Ctrl+Z` is the exact revert.

**One record, three readers — a simplification the spec did not ask for.** The spec named a
`ChartLegatoToggleEntry`; what shipped keeps the *plan* once, in `m_chart_notes_top`
(`ChartNotesTopEntry{plan, history_position}`), and the toggle window stores only the armed keys
(`m_chart_legato_toggle`). The settle sweep folds into that record, the toggle reverses it, and the
multi-digit fret widen reverses it to rebuild its pre-entry stream — so no two verbs can disagree
about what the burst did, because there is no second copy to keep in step. The history position IS
the proof of ownership: any other push, undo, or redo moves the cursor and retires the record.

The fret widen was the third reader only from 2026-08-11 (review fix F4): it had kept its own
`ChartFretEntry::applied_plan`, the same fact re-stated, hand-synchronized at three sites. It now
keeps just a `pushed` flag — "the record is MY push" — which is not redundant with the record: a
first digit the planner refuses still arms the window and has pushed nothing to reverse. A widen that
finds itself pushed with no record retires the window instead of guessing, which makes the sweep's
own `m_chart_fret_entry.reset()` the ruled behaviour rather than the only thing standing between a
committing fold and a widen against a plan that no longer exists.

## `Ctrl+H`

`planSetAttack(LeftTap)` — the sole author of the left-hand tap, no predecessor needed, the E4 gate
(`nothingToStrike`: a fret or a node to strike) and the shipped node-conversion guards. Label
"Left-Hand Tap", command id value retained. There is deliberately **no force-Pull** — pulling off
to a higher fret is physically impossible, so its absence is principled rather than an omission.

## Validation, and the two flatten sites

Validation is **intra-note only**. E4 binds `Tap` and `LeftTap`; the geometry rules are unchanged;
the relational E5/E12/E19 content lives solely as resolver clauses, so a claim they refuse reads as
a plain pick rather than refusing a document. Two flattens, split by class:

1. **In-plan, intra-note** (`finalizePlan`): an attack stranded at sounding-position 0 by its own
   note's edit flattens to `Pick`, riding that entry. Immediate, because the gate still binds
   intra-note truths mid-burst.
2. **At settle, relational — the settle sweep** (`sweepUnjustifiedLegato`, stateless): at every
   settle event, sweep the chart for `Legato` notes whose claim no longer resolves and flatten them
   to `Pick` in one batch. No window state, no flagged keys, no proofs. A `LeftTap` is never
   touched: its claim is local, so nothing can withdraw it.

### Commit shape

`settleChartLegato` mutates the model only while the history cursor sits on top. There it folds via
`replaceTop` when the top is this burst's chart-notes entry and not the clean state (so one `Ctrl+Z`
restores the edit and the claim together); otherwise it pushes its own labeled entry, which at
top-of-stack truncates nothing. At a mid-stack resting point — reachable only through undo — the
sweep **DEFERS**: bytes stay untouched, the claim displays as its resolution, and the redo branch
survives.

The invariant is therefore exactly: **`Unjustified` cannot survive a settle at the top of history.**
Every project write additionally serializes the RESOLVED form (`chartDocumentText` sweeps a copy),
so **every file ever written satisfies the invariant unconditionally**, with no history mutation at
write time — the same memory-richer-than-file philosophy the scrape latents already use.

**A sweep that commits anything — fold or push — closes both coalescing windows** (the fret entry
and the toggle window); a sweep that finds nothing leaves them armed. A fold changes entry content
without moving the history position, so an armed window's proof would otherwise pass and reverse or
widen a plan that no longer exists.

Which of the two it was is asked of the SWEEP, not of the diff (2026-08-11, review fix F5).
`planSettleLegato` reports exactly one emptiness — "the sweep found nothing to flatten" — and the plan
it returns is allowed to be empty itself, which happens when the flatten put the stream back exactly
where the pre-burst base had it. Reading the diff's emptiness as "nothing to settle" conflated the
two: in that second case the model kept an `Unjustified` claim at the top of history, breaking the
stated invariant, and left both windows armed over it. Unreachable today (the only verb that writes a
claim writes it where the resolver justifies it), but it was the guard's shape that was wrong, not its
luck.

### Settle events, precisely

Every *user-initiated* selection replacement wherever it enters — `setSelection`, caret arming,
Ctrl+click, double-click, marquee (follow-the-edit selection rewrites inside `applyChartEditPlan`
are NOT settles: they would collapse the burst); every project write verb (Save, Save-As, Publish);
arrangement/project switch, sweeping the chart being departed; `Esc` as the last step of every
press, whichever rung consumed it; transport seek; playback start. At any shared event the sweep
runs after that event's own state changes complete. **Undo/redo are deliberately NOT settle events** —
mid-stack states may transiently hold broken claims, which is what the deferral above protects.

**Only the CURRENT arrangement is swept**, and in practice that covers the song — the spec's "every
arrangement it writes" is stronger than the code needs: every load settles every chart it reads,
switching arrangements settles the one departed and resets the undo stack, and the writer resolves as
it serializes.

It is not, however, an induction, and the exception is named rather than glossed (2026-08-11, review
fix F8): the switch's settle DEFERS at a mid-stack cursor, so *break a claim, undo once, switch* leaves
the departed chart holding an `Unjustified` claim in memory that no later event can reach. Accepted —
the consequence is display-only and no FILE can carry one regardless, because the writer resolves as
it serializes.

### Load paths

The reader runs the sweep at load — import completion and plain open alike — surfacing the same
conversion notes on every path; an open that converted anything marks the session dirty, since
memory no longer equals disk. The game reader discards the notes channel, and the game can never see
`Unjustified` anyway: it only loads written files, which are resolved by construction.

## Undo

Entries store values; replay is bit-exact with zero derivation at undo time. Mid-burst undo needs no
special handling: the sweep judges only current state, and defers mid-stack. The 5→3→2 story is pure
re-projection — the note's bytes never change unless a top-of-history settle happens while the claim
is broken, and then one undo of the folded entry restores the edit and the claim together.

## Tails

**Tails are never deferred and never display-divergent under this model**: the assist writes tail
growth as ordinary plan data in the same entry, and the settle sweep touches attacks only.

The one stored≠displayed tail in the system predates this design: the span-held chord member (stored
sustain 0, drawn held to the span's end). The ruled direction is that span-implied holds are NOT
materialized into stored sustains — that would store a derivable datum which goes stale when the
span changes, the exact sin this design removes. The span stays the one authored datum; member holds
stay derived. **The W9-A divergence is closed display-side:** the tab lane carries
`display_hold_ends` resolved from the same `chartEffectiveSustains` authority the highway resolves
`HighwayViewState::display_hold_ends` from, so both surfaces draw one chart the same way.

**And the derived hold is bounded like a stored one (2026-08-11, review fix F2).** Unifying the two
surfaces first shipped it uncapped, which made the lane draw a ribbon to the span's end whatever sat
in between: a sustainless chord under a four-beat span with the same string restruck at beats 2 and 3
drew string 1's tail through both later heads and out the far side — teeth and vibrato sine included —
a picture 40-Q2-B guarantees no *stored* sustain can produce (`normalizeSustainOverlaps` truncates at
the next same-string onset). `chartEffectiveSustains` now imposes that same bound on the derived hold,
in the one authority both surfaces read rather than lane-side. The cap provably cannot change
`predecessorHoldReaches`: the onset it measures to IS the successor whose claim reads that hold, and a
hold reaching exactly an onset reaches it. Hit testing was the other half — `tabNoteLayout` took the
note's `end_seconds` while the paint pass drew to `display_hold_ends`, so every span-extended ribbon
was drawn and unclickable; the manifest now takes the same hold end the paint pass does.

## Tail lock

The legato halves of the W6 family are dissolved: the tail lock, the break verb's legato half, and
40-Q5's legato indicator all narrow to slides-only. Shrinking a connecting tail drops the mark live
(projection), is repairable within the burst by regrowing, and settles as one folded,
exactly-undoable batch. Slides keep lock, break, and 40-Q5 — waypoints are real data.

## Import

`left_hand_tapped → LeftTap` verbatim (which fixed the shipped aliasing — it also gives the right
downstream behavior for free, since a left-hand onset anchors the fret hand and closes chord spans);
a hopo destination → `Legato`; the completion-time settle sweep converts relational junk — an equal-
or absent-predecessor hopo flag — to `Pick`, counted in the import log.

The two junk classes are cleaned at **different moments, deliberately**. A strikeless striking
attack (a `Tapped` flag on an open string, real Guitar Pro data) is intra-note, so
`nothingToStrike` flattens it *early*, before the chord-shape and hand-window passes read the
attack — those passes treat a two-hand tap as a picking-hand onset that anchors nothing, so a song
would otherwise be shaped around an attack that cannot survive validation. Nothing later could fix
it either: the sweep never touches a local claim, exactly because no neighbour can withdraw one.
The relational junk needs the whole stream, so it waits for the completion sweep.

The settle-me-Pull machinery and the import-completion normalize call are deleted.

## Sequencing note — W4's muted-tail trim

Today a full-muted tail counts at its stored value for the hold test, so muted-tail legato resolves.
When W4/E25 trims muted tails, dependent claims corpus-wide would flatten at next load. Recorded as
a watch item (`docs/tracking/watch-items.md`): W4's trim must run as an editor plan operation so the
flatten rides the trim's entry, never a silent load-time conversion.

---

# Why the model has the shape it has

The physics below was argued out before the storage question was settled, and every part of it
survived the change of mechanism unchanged. It is still binding.

## The load-bearing fact: the two motions are not symmetric

Recorded first in the importer (`gp_chart_builder.cpp`, the `left_hand_tapped` branch): a left-hand
tap is the fretting hand hammering the note from nowhere (no pick stroke), and it is **always a
hammer, never a pull: nothing is released to sound it.** Therefore:

- **The hammer motion needs no predecessor.** Its precondition is a positive sounding position:
  `fret > 0 || harmonic_node.has_value()` (E4 as amended — the node form admits the fret-0 tap
  harmonic).
- **The pull motion requires a preceding note on the same string at a higher released fret**,
  because something must be released to sound it. Its *target* may be fret 0: pulling off to an open
  string is ordinary.

This asymmetry is exactly why the final model stores TWO values. `Legato` is the relational half
(needs a predecessor to mean anything) and `LeftTap` the local half (needs only somewhere to
strike), so the two readings of one overloaded value — which the stored-direction model had to keep
distinguishing by hand — no longer share a value at all.

## Released-fret semantics (RULED 2026-08-09: adopted)

"Released fret" of a predecessor (`releasedFret`) = a scrape's slide-out fret, else the fret of its
last slide waypoint when it carries one, else its `fret` — where the finger *ends*, never where the
note began. A 5→7 glide hands over 7, so a following 5 is a genuine pull. The 2D relationship
drawing follows the same rule, and Phase 7 waypoint edits therefore change what a claim resolves to.

## No distance bound, and other strings are already handled

The scan takes the last earlier note on the **same string**, so an intervening note on another
string is correctly ignored. The resolver is deliberately **unbounded in time**: a hammer-on from a
note eight bars back is musically odd, but a predecessor still holding is a predecessor, and a
threshold would be a magic constant guessing at what the user can state exactly (`Ctrl+H` states the
far descending case in one keystroke).

**Trap to avoid:** do not require the pull's predecessor to still be *sounding*. Charts legitimately
carry zero-sustain notes where a pull-off is correct — `sustain` is the drawn tail, not the physical
ring — so that check would break ordinary charts. D13 below is the exact, provable narrowing of it.

## The release-inference refinement (D13, signed and shipped 2026-08-09)

Option C said "still ringing is unknowable from data." The sustain conventions bound that:
unknowable only at close range. Two shared quantities make the far case provable —
`g_minimum_kept_sustain_beats` (`grid_arithmetic.h`; the notated ring below which import drops an
effect-free tail) and the minimum-sustain-distance margin (the exact end every held tail trims to).
At onset gaps of the bound or more, a held-through predecessor necessarily carries a tail reaching
the margin before the onset, so a shorter (or absent) tail proves the string was released and legato
from it is not real. Under the bound nothing changes: tails that short are legitimately absent, so
resolution stays fret-only.

`predecessorHoldReaches` (`grid_arithmetic`) is the one statement of the test, and both readers use
it identically: the resolver's disqualifying clause, and the `H` assist's only-blocker test. Two
consequences worth naming. A sustain edit that disconnects a tail drops the dependent claim's mark
live and the sweep flattens it at the burst's end. And authoring legato across a gap at or past the
bound writes the connection — the tail IS the held-ness datum — which is what the D14 assist does in
one entry; dragging the tail first remains equivalent.

## `Ctrl+H`'s validity domain, verified against the full matrix

Checked rule-by-rule at the user's foundational-soundness request (2026-08-09). It was verified when
the left-hand tap was stored as `Hammer` and therefore inherited every Hammer cell; with `LeftTap`
its own value the conclusion is unchanged and now structural.

- **E4 is the only intra-note gate.** E13 (node + the hammer form, the rare hammer-form tap
  harmonic), E24 (the muted tap), E17 (slide payloads), tremolo (H1 rejected) and accent (H3) all
  allow. The one place both forms are invalid is exactly E4's boundary: the open string with no
  node, `Ctrl+H`'s sole matrix-grounds refusal.
- **No relational gate, deliberately — including over a higher predecessor.** If the higher note
  genuinely rings, sounding the lower fret means the upper finger lifts and the release IS a
  pull-off. But *genuinely rings* is not in the data, and a **damped** higher predecessor followed
  by a struck lower note is a real, common left-hand tap (descending staccato tapping runs).
  Forbidding it would reject genuine music. So `Ctrl+H` may override a standing claim: only the
  author knows whether the predecessor was ringing. Under the new model this is stronger than a
  precedent — `LeftTap` is a different value, so nothing can withdraw it, and plain `H` can never
  produce it.
- **Two conversion guards, from the unified node law.** On a **pinch**, the node survives only where
  its meaning does — at a real stop it stays (the picking hand damps the same point under either
  attack: the tapped-harmonic gesture), while an **open-string** pinch's bridge-side graze is not a
  strikeable place, so the node is stranded and the E4 gate refuses the note. On a **scrape** it is
  ordinary attack replacement, the gesture leaving with the attack. A **tap harmonic's** node is a
  struck contact point either hand can deliver, so `Ctrl+H` carries it into the hammer form — the
  re-handing is the verb's stated meaning, not a silent re-read — bounded by the neck ceiling.

## What changes what a claim resolves to

Still the right inventory, judged at a different moment: every row below describes an edit that
changes what a claim resolves to, and **none of them repairs anything**. The claim stays as
authored, reads as a plain pick while nothing justifies it, and either becomes justified again or is
flattened by the sweep at the next settle. The old "did we miss a verb" question is dissolved twice
over — the sweep judges the whole stream and knows nothing about verbs.

| Edit | Changes the resolution? |
|---|---|
| Delete predecessor | yes |
| Move predecessor off-string/after N; move N off-string/before predecessor | yes |
| Move a **third** note into or out of the P…N gap on that string | yes — anything changing which note is the immediate predecessor re-asks the resolver |
| Retype either fret to equal/invert (including the wheel fret shift) | yes |
| Retype N to fret 0 without a node | **depends on which value N holds, and the split is the model working**: a `LeftTap` is stranded (nothing to strike), which is intra-note, so `finalizePlan` flattens it immediately; a `Legato` may be untouched, because a pull-off ONTO an open string is ordinary — it stops resolving only if the predecessor is now equal or lower |
| Insert between (typed digit, Alt+click, Insert) | yes |
| `planSetAttack(PickSlide)` on the predecessor | yes: the released fret becomes the slide-out's — a claim that stays justified survives, an inverted one stops resolving |
| Harmonic verb: node set on the predecessor (E19) or on N (E12); node cleared on a fret-0 note | yes, three cells |
| Phase 6 L-merge (absorbed note changes which note is the predecessor) | yes |
| Phase 6 split | **no** — the tail keeps the fret, which is why the rule is value-based rather than identity-based |
| Phase 7 waypoint edits changing the predecessor's last waypoint fret | yes, under released-fret semantics |
| Paste / range move / range delete (plan 52) | reduce to the classes above |
| Sustain edits | **yes since D13** — the predecessor's hold is part of the answer past the kept-sustain bound |
| Bend, vibrato, tremolo, accent, mute edits | no — none enters the answer (a fully-muted predecessor is still a press) |
| Tuning capo/cent edits | no; a future string-count edit reduces to move-off-string |
| Undo/redo themselves | **never** — they replay stored plans and bypass the planners, which is required for exactness |

## Does this generalize?

Checked against every remaining Phase 5 field: **no, and that is load-bearing for sequencing.**

| Field | Neighbour-dependent? |
|---|---|
| `mute`, `vibrato`, `tremolo`, `accent` | no — unconditional |
| `harmonic_node` | no — its constraints (range, node > fret, pinch requires one) are intra-note |
| `attack`: `Tap` / `Slap` / `Pop` / `Pick` / `Pinch` | no |
| `attack`: `PickSlide` | no — intra-note and already enforced by the chart rules |
| `attack`: `Legato` | **yes — the only one**, and `LeftTap` is deliberately not |

Legato is the only articulation whose meaning references another note, so this blocks none of the
other Phase 5 verbs. It **does** land on Phase 6 (L-link merge/split is explicitly relational) and
Phase 7 (slide waypoints reference target frets), which is the argument for having settled it here
as precedent rather than later under pressure.

---

# Rejected alternatives — the graveyard

Every entry below was a real candidate, and several were signed before being overturned. They are
kept because the kills are the reasons the shipped model is shaped as it is; the full arguments live
in `legato-simplicity-analysis.md` (the ground-up pass that ranked five designs),
`legato-design-options-explained.md` (the three finalists, line-by-line), and
`legato-tap-notation-final-pass.md` (the notation half, S1–S5).

## Stored direction itself — the root of everything below

The chart stored `Hammer` versus `Pull`, derived once at authoring time from the fret relationship.
Every input to that derivation stayed editable afterwards, so the stored attack could drift out of
agreement with the data that justified it, and each of the mechanisms below exists to chase that
drift. Storing the claim instead deletes the drift rather than managing it: nothing derived is
stored, so nothing can go stale. This is the change of 2026-08-11.

## Layer 1 — the always-on impossibility repair engine (`normalizeChartLegato`)

**DELETED.** A shared normalization in every planner's finalize step, running after the sustain
pass, repairing *impossibility* over the whole candidate stream: a `Pull` with no same-string
predecessor became `Pick`, one whose predecessor released a lower fret became `Hammer`, a `Pull`
into a node became `Hammer`-or-`Pick`, a `Hammer` stranded at sounding-position 0 became `Pull`-or-
`Pick`, and so on for nine rows. It never stated a direction rule of its own — it asked the
derivation what the predecessor justified and wrote that.

**The kill:** every *relational* row of that table is now a clause of `resolveLegato`, and the state
it repaired is simply *read* as a plain pick. The batch flatten happens at settle events instead
(`sweepUnjustifiedLegato`), and the only repair left inside `finalizePlan` is the last row's kind —
intra-note, E4's strike requirement. 161 lines deleted, with the concept.

## Layer 2 — the recalc window

**CANCELLED UNBUILT** (task #63; it never existed in code). Controller-owned transient state shaped
like the multi-digit fret window: the *flagged* notes whose justification a birth edit disturbed,
the selection key-set immediately after that edit, and the undo-stack cursor position. While the
window lived, every further chart edit re-derived the flagged notes' attacks and folded the change
into that edit's plan; it settled — with no chart mutation and no undo entry — on any selection
replacement, caret re-arm changing the key-set, `Esc`, playback start, seek, project/arrangement
switch, and any undo or redo.

**The kill:** with no direction stored, every read already IS the re-derivation, so flags, birth,
participation and settle have nothing to do. What the design needed it for is covered by two much
smaller things: the resolver (display and scoring follow the chart live) and the settle sweep (a
claim the chart cannot justify stops existing at the burst's end). Its undo-exactness argument
survives as the reason **the sweep defers at a mid-stack cursor**, and its transient-state shape
survives as the toggle window's.

Its own variants were ranked and killed inside the same analysis, and each kill still applies to
anything shaped like it:

1. **Flag survives undo.** Kill sequence: `Ctrl+H` a descending left-hand tap; retype the note's
   fret to 0 so the E4 repair rides the entry and a flag is born; **undo**; retype 3→4, which
   disturbs nothing — and a surviving stale flag fires, re-derives, and rewrites the deliberate
   assertion. An attack change with no visible cause.
2. **Flag stored inside undo entries.** Violates the pure-history memento shape (the history never
   applies editor side effects; `EditorEditContext` deliberately carries no interaction state), and
   a restored flag would be scoped to a selection undo does not restore. All cost, no value: inside
   its lifetime a restored flag is inert anyway.
3. **A3, stored intent.** Rejected by the user, and the review found the stronger reason: intent
   persists forever, so derivation is live forever — edit the predecessor next week and the
   direction flips. It reintroduces the ten-minute surprise *by construction*. Clearing intent on
   invalidation reduces A3 to Layer 1 plus a wasted format field. (Ironically it is perfectly
   undo-consistent, so if it ever returns it returns for other reasons — and it should not.)
4. **A2, a settle timer.** 5→3, pause 800 ms, →2 produces one result; the same keystrokes inside the
   window produce another. Same keys, different chart, invisible cause.
5. **Recalc scoped to the coalescing window.** A2 wearing `replaceTop`'s costume: the entry's open
   lifetime is a 750 ms typing clock, so the timer objection applies unchanged, and the scope is
   strictly narrower for no gain.
6. **An explicit affordance** ("legato broken — press H to restore"). Honest and undo-trivial, but
   scope-mismatched: `H` acts on the selection and the disturbed note is not in it.
7. **Layer 1 alone** — not killed, demoted: it was the bottom layer, and standalone it failed the
   restore wish. Both are gone now, and the resolver satisfies the restore wish by construction: a
   claim becomes justified again the instant the chart justifies it, with no mechanism at all.

## Question B's notation half — a second mark for the left-hand tap

**Storage half adopted, notation half still rejected.** `LeftTap` did become its own value (the
format field came free: the same change replaced the old tokens in place). What stayed rejected is
giving it its own notation: one merged mark, drawn from the RESOLVED motion, so the two-`T`
collision and the unknown 3D atlas cell never arrived. The readability argument holds — a hammer
mark with no predecessor is visibly from-nowhere, so the tab's context already disambiguates — and
detection cannot hear the difference, so gameplay can never need it.

The old **alias-zone** objection (a `LeftTap` with a predecessor being a second spelling of a
hammer-on) is answered rather than inherited: the resolver reports the hammer motion for it
unconditionally, and the `H` planner is forbidden from producing one, so the two spellings differ in
exactly one thing that is real — whether the author asserted the strike locally.

## D15 — connected legato: stored connection plus forward-addressed `H`

**Rejected, and re-signed by the final ruling.** The display half (a connector from origin to
destination) died with the 2026-08-09 user ruling that rejected the 2D connector: it would diverge
the surfaces the player and the charter read. The data half — requiring a connecting tail at all
gaps — died on corpus arithmetic: 73.8% of real legato pairs are a sixteenth apart or closer, where
the spacing margin makes the maximum representable connection tail exactly ZERO, so the rule is
vacuous yet undrawable for most legato; above that it converts routine sustain edits into silent
legato loss for 26.2% of pairs (against 1.0% under D13). Forward-addressed `H` died on eight
concrete kills, three of them against shipped mechanisms (the selection-follow rule enlarging the
verb's own scope each press; the growth clamp turning unreachable extensions into mutating no-ops;
the multi-digit fret window settling on the hot path).

## The derivation-restatement toggle, and the enumerated skip list

An `H` planner that restated eligibility as its own predicate list — one rule spelled in the verb
and again in the derivation. Killed as a rule stated twice: the planner asks the resolver instead,
which is why there is no list to keep in step. Option C's refusal *substance* is restored through
that one authority rather than through an enumeration.

## The cue, the lint, and the dormant-state chrome

A tint, outline, or lint advisory for a claim nothing currently justifies. Rejected 2026-08-09 and
re-signed 2026-08-11 on the stronger justification: TRANSIENCE. A broken claim cannot cross a
top-of-history settle or reach any file, so mid-burst it is pixel-identical to a plain pick *by
design*, and the honest live marks are the feedback. The window has no timer either — the A2 timer
was rejected precisely because typing-speed-dependent behavior confuses.

## Relational validity rows, and the "permanent validation" ceiling

The matrix doc's ceiling section argued that E5/E6 must stay validation forever, because a per-note
type cannot express a relational invariant. The premise is true and the conclusion no longer
applies: those rows left validation by a different route — they became derived *reads*, so there is
no invariant left to express. What cannot be made structural is unchanged; what can be made
**underivable** turned out to be the better question.
