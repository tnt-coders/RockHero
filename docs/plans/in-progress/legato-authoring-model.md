# Legato Authoring Model — hammer-on, pull-off, and the left-hand tap

Status: **FULLY SETTLED 2026-08-09 — every open question ruled; recalc window awaiting
implementation** (plan 40 Phase 5; the technique enforcement half shipped 2026-08-09 —
validation, the finalize gate, `normalizeChartLegato` at import and in every plan — and the
toggle's eligible-subset UX plus the `Ctrl+H` force verb shipped 2026-08-10, leaving Phase 5 only
the recalc window). The deep review established the design — the recalc
window that **settles on any undo or redo**, making the binding constraint (*undo must always
restore exactly the pre-action state*) hold by construction — and the walkthrough then closed all
five user calls and question B (Option C: strict derivation, merged notation, `Ctrl+H` the sole
author of the left-hand tap). Plan 40 Phase 5's first verb (`H`) shipped in `4a98da55` with the
naive derivation; its defects are recorded in
[What the shipped verb gets wrong](#what-the-shipped-verb-gets-wrong) and are all fixed.

**What is built.** `planSetLegato` judges the predecessor's **released** fret, requires it still
holdable at the onset, refuses a fret-hand-harmonic predecessor, and declines to derive across a
scrape — all of it by delegating to the one shared authority, `derivedLegatoAttack`
(`chart_rules.h`). Layer 1's repair is `normalizeChartLegato` (`legato_normalize.h`), which runs in
the planners' shared `finalizePlan` step and again at Guitar Pro import completion.

**What remains**, all of it plan 40 Phase 5: Layer 2's recalc window. The toggle's
eligible-subset UX shipped 2026-08-10 (the controller asks `planSetLegato` itself whether applying
would change anything — the oracle, never a restated predicate — and only then clears, targeting
just the legato subset), and the `Ctrl+H` force verb shipped the same day: `ChartForceHammer`
binds the chord to `planSetAttack(Hammer)`, whose written-form validity check yields the ruled
domain from the one rule authority — sole refusal the no-node open string, the open-string
pinch's graze refusing to re-hand, and the tap harmonic's strike point carrying into E13's
hammer form.

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

### 1. The derivation, and a force verb (Option C — RULED 2026-08-09)

Plain `H` infers only what the frets justify; the left-hand tap is `Ctrl+H`'s to state:

| Situation | Result |
|---|---|
| predecessor on the same string releasing a **higher** fret | **Pull** |
| predecessor releasing a **lower** fret — and sounding position positive | **Hammer** (a genuine, fret-justified hammer-on) |
| predecessor **equal or absent** | **refuse** — the frets justify nothing; no tap is invented (Option C's one change from the earlier table, which inferred `Hammer` here) |
| sounding position 0 (`fret == 0`, no node) | **refuse** — nothing to press, nothing to strike |

**`Ctrl+H` forces `Hammer`** and is the **sole author of the left-hand tap**, valid across its
verified domain (E4: positive sounding position — see the matrix verification section below),
including overriding a derived Pull (the author knows the predecessor was damped). It fits the
grammar: `Ctrl` means *precision* — "I will state it exactly, do not infer" — and a left-hand tap
is precisely a statement the fret relationship cannot infer. There is deliberately **no
force-Pull** — pulling off to a higher fret is physically impossible, so its absence is
principled. Option C captures the rejected notation split's entire deliberateness benefit:
equal/absent-predecessor Hammers exist only via `Ctrl+H`, deliberate by construction.

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
sustain pass, over the whole candidate stream, and repairs only **impossibility**.

The repair states no direction rule of its own: it asks `derivedLegatoAttack` what the predecessor
justifies and writes that. Its own decisions are only *when* the stream's attack is impossible, and
which repairs may reach for a derived direction at all:

| Invalid state found in the candidate stream | Repair |
|---|---|
| `Pull` with no same-string predecessor, or predecessor moved off-string/later | → `Pick` |
| `Pull` whose predecessor is no longer holdable at the onset (D13) | → `Pick` — a disconnected tail is a proven release |
| `Pull` whose predecessor's released fret is **equal** | → `Pick` (no direction ≠ left-hand tap) |
| `Pull` whose predecessor's released fret is **lower** | → `Hammer` (a genuine hammer-on now) |
| `Pull` whose predecessor is a fret-hand harmonic (E19) | → `Pick`, always. A touch holds nothing to hand over, so it disqualifies the predecessor outright — neither direction survives it, and no lower fret can rescue one |
| `Pull` which itself carries a node (E12) | → `Hammer` when a lower released predecessor justifies one, else `Pick` |
| `Pull` whose predecessor is a scrape | **left alone when still justified** — pull-from-a-scrape is valid data (D7), tested against the scrape's released fret, the slide-out's. Repairs only when that test fails, like any value change |
| `Hammer` with sounding position 0 — fret 0 and no node (E4) | → `Pull` if a still-held higher predecessor exists, else `Pick` |
| `Tap` with sounding position 0 — fret 0 and no node (E4) | → `Pick`, always. E4 binds both striking attacks, but a tap is a picking-hand articulation no predecessor can convert into a pull-off. (Not a hypothetical: a junk `Tapped` flag on an open string is real Guitar Pro data, and it used to fail E4 at validation and take the whole song's import down.) |
| `Hammer` in any other configuration | **untouched** — always possible as a left-hand tap; a deliberate `Ctrl+H` survives |

"Released fret" of a predecessor (`releasedFret`) = a scrape's slide-out fret, else the fret of its
last slide waypoint when it carries one, else its `fret` (user call 3 below) — where the finger
*ends*, never where the note began. Repairs ride the same undo entry as the edit that exposed them,
exactly like 40-Q2-B truncations — one plan, one entry, exact inverse. `ChartNotesEdit` replays
stored values, so undo/redo restore both halves atomically with **no derivation at undo time**.

Imported charts are repaired **at import** (user ruling, 2026-08-09): the importer runs this same
normalization at import completion, because import is a commit point and "valid at every commit
point" applies to it too. The finalize-step sweep stays whole-stream as the invariant keeper, but
it never finds imported junk — a chart is never invalid in the first place, and the
first-edit-repairs-old-data surprise cannot happen.

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

## `Ctrl+H`'s validity domain, verified against the full matrix

Checked rule-by-rule at the user's foundational-soundness request (2026-08-09), because the
left-hand tap is stored as `Hammer` and therefore inherits every Hammer cell:

- **E4 is the only intra-note gate.** E13 (node + Hammer, the rare hammer-form tap harmonic),
  E24 (the muted tap), E17 (slide payloads), tremolo (H1 rejected) and accent (H3) all allow.
  The load-bearing fact is the **superset relation**: a hammer-on needs an ascending ringing
  predecessor, a left-hand tap needs only something to strike, so the shared value's boundary is
  the tap's — `fret > 0 || harmonic_node.has_value()`. The one place BOTH forms are invalid is
  exactly that boundary: the open string with no node, `Ctrl+H`'s sole matrix-grounds refusal.
- **No relational gate, deliberately — including over a higher predecessor.** If the higher note
  genuinely rings, sounding the lower fret means the upper finger lifts and the release IS a
  pull-off, so a "tap" there is not a distinct event. But *genuinely rings* is not in the data
  (the settled trap: sustain is the drawn tail, not the physical ring), and a **damped** higher
  predecessor followed by a struck lower note is a real, common left-hand tap — descending
  staccato tapping runs. Forbidding Hammer under a higher predecessor would reject genuine
  music. Therefore **`Ctrl+H` may override a derived Pull into Hammer**: the author-is-authority
  precedent exactly — only the author knows whether the predecessor was ringing. The repair
  rules already protect the override (a Hammer on a descending pair is untouched), and a later
  plain `H` re-derives it back to Pull — the correct symmetry between the inferring verb and the
  stating one.
- **Two conversion guards, refined by the unified node law as they shipped:** on a **pinch**,
  the node survives only where its meaning does — at a real stop it stays (the picking hand damps
  the same point under either attack: the tapped-harmonic gesture), while an **open-string**
  pinch's bridge-side graze is not a strikeable place, so the node is stranded and the E4 gate
  refuses the note (the attack-away-from-pinch hazard — kept, that graze position would silently
  become a fret-hand node). On a **scrape**, it is ordinary attack replacement, the gesture
  leaving with the attack per `planSetAttack`. A **tap harmonic's** node is a struck contact
  point either hand can deliver, so `Ctrl+H` carries it into the hammer form — the re-handing is
  the verb's stated meaning, not a silent re-read — bounded by the neck ceiling on the written
  form.

## The release-inference refinement (D13, signed and shipped 2026-08-09)

Option C said "still ringing is unknowable from data." The sustain conventions bound that:
unknowable only at close range. Two shared quantities make the far case provable —
`g_minimum_kept_sustain_beats` (grid_arithmetic.h; the notated ring below which import drops an
effect-free tail) and the minimum-sustain-distance margin (the exact end every held tail trims
to). At onset gaps of the bound or more, a held-through predecessor necessarily carries a tail
reaching the margin before the onset, so a shorter (or absent) tail proves the string was
released and legato from it is not real.

`predecessorHoldReaches` (grid_arithmetic) is the one statement of the test, and it gates all
three layers identically: E5 validation rejects the pull, `normalizeChartLegato` repairs an
orphaned Pull to a plain pick (never Hammer — hammering after a release is the left-hand tap,
`Ctrl+H`'s domain), and the `H` verb skips the note as underivable. Under the bound nothing
changes: tails that short are legitimately absent, so derivation stays fret-only.

Two consequences worth naming. A sustain edit that disconnects a tail repairs its dependent
Pull in the same undo entry (the shared finalize runs the repair after the sustain change), and
authoring legato across a gap at or past the bound writes the connection — the tail IS the
held-ness datum. Since 2026-08-10 the `H` verb writes it itself (the D14 assist: when the hold
test is the only blocker, the plan grows the predecessor's tail to the margin point and derives
the direction in one entry, bounded by the duration verb's own growth clamp); dragging the tail
first remains equivalent. `Ctrl+H` is untouched: a left-hand tap needs no predecessor at all.

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
(Shipped 2026-08-09: all seven planners now funnel, `planDeleteNotes` and `planRetypeFrets`
included — the latter gained the chart and the tempo map, and lost its local fret caps to the
gate.)

The inventory against every verb, under the value-based rule:

| Edit | Invalidates? |
|---|---|
| Delete predecessor | yes |
| Move predecessor off-string/after N; move N off-string/before predecessor | yes |
| Move a **third** note into or out of the P…N gap on that string | **yes — the generalization the earlier six-item list missed**: anything changing which note is the immediate predecessor re-tests the tuple |
| Retype either fret to equal/invert (including the wheel fret shift) | yes |
| Retype N to fret 0 without a node (E4) | yes |
| Insert between (typed digit, Alt+click, Insert) | yes |
| `planSetAttack(PickSlide)` on the predecessor | **yes**, value-based: the released fret becomes the slide-out's and the tuple re-tests — a pull that stays justified survives (valid pull-from-scrape data, and the organic route for authoring it: pull first, scrape after), an inverted one repairs |
| Future harmonic verb: node set on the predecessor (E19) or on N (E12); node cleared on a fret-0 Hammer (E4) | **yes — new**, three cells |
| Phase 6 L-merge (absorbed note changes predecessor identity) | yes, via the value rule |
| Phase 6 split | **no** under the value rule — the tail keeps the fret; this is why the rule is value-based |
| Phase 7 waypoint edits changing the predecessor's last waypoint fret | yes, iff released-fret semantics are adopted (user call 3) |
| Paste / range move / range delete (plan 52) | reduce to the classes above; covered by the funnel |
| Sustain edits | **yes since D13** — the predecessor's hold IS part of the tuple past the kept-sustain bound, so a shrink that disconnects a tail invalidates the legato it justified. (The row's original "no" rested on "predecessor need not still sound", which D13 narrowed to "unknowable only within the bound".) The tail-lock ruling then makes the explicit duration verb REFUSE such a shrink rather than repair it, so the repair path here covers only the implicit truncations. |
| Bend, vibrato, tremolo, accent, mute edits | no — none enters the tuple (a fully-muted predecessor is still a press) |
| Tuning capo/cent edits | no; a future string-count edit reduces to move-off-string |
| Undo/redo themselves | **never** — they replay stored plans and bypass the planners, which is required for exactness |

## What the shipped verb gets wrong

**ALL FIXED by the 2026-08-09 enforcement pass (D12)** — the scrape-predecessor skip, the
released-fret judgment, the folded E-rules, and the pinch node clearing all shipped with the
finalize gate. Kept for the record; the defects below describe `planSetLegato` as of `4a98da55`:

- **Scrape predecessor.** The derivation reads `previous->fret` even when the predecessor is a
  `PickSlide`, whose fret is where the scrape *starts* — its released fret is the slide-out's. D7
  settled the split (2026-08-09): pull-from-a-scrape is **valid data** (executable with gain,
  authoring-only — Guitar Pro cannot write it), but the `H` derivation never *creates* it — the
  one derivation-vs-validity gap. Its organic authoring route is edit order: author the pull,
  then scrape the predecessor; the value-based repair keeps what stays justified. Every other
  predecessor derives normally: `Tap` (the tapping finger is a real press), and **fully-muted
  notes too** — muted legato is standard funk and R&B vocabulary, so requiring a modifier for it
  would surprise exactly the charts that use it most (the user's revision that dissolved the
  briefly-proposed `Shift+H` tier; that analysis stays on file in the walkthrough doc as the
  candidate if a dedicated affordance is ever warranted).
- **Onset fret vs released fret.** The derivation compares the predecessor's onset fret; a
  predecessor that slid 5→7 followed by a pull-off to 5 is musically a pull from 7, but the verb
  sees 5 vs 5 and refuses. User call 3 below.
- **E-rules absent.** The verb consults `harmonic_node` nowhere: it can derive Pull into a harmonic
  (E12) and Pull from a fret-hand harmonic (E19). The full derivation folding the rules: **Pull**
  iff a valid higher releasable predecessor exists and N carries no node; else **Hammer** iff
  `fret > 0 || harmonic_node.has_value()`; else refuse. Pressing `H` on a **pinch** needs its own
  guard: converting the attack away from `Pinch` silently reinterprets an off-neck node (24.0) as a
  fret-hand node — teleporting the hand to fret 24 — so the verb must clear or refuse. Resolved
  2026-08-09: it clears (the node leaves with the picking-hand attack that owned it).
- **Doc fact corrected:** the coalescing window is **750 ms** and controller-owned
  (`g_fret_entry_window_ms`); `EditorUndoHistory::replaceTop` is only the splice mechanism, and it
  refuses when a reachable clean marker sits on the top entry — a mid-window save breaks coalescing
  by design. The recalc window never hits that refusal because each recalc edit is its own entry.

## Remaining user calls

1. ~~**Recalculating-state chrome**~~ — **RULED 2026-08-09 (user): none.** The honest live marks
   are the feedback (the pull-off symbol visibly dropping to plain and rising to hammer-on IS
   the signal); no outline or tint while the window is open. Reaffirmed in the same exchange:
   the window has **no timer** — it lives exactly until a settle event (selection change, seek,
   Esc, playback, undo/redo), the A2 timer having been rejected precisely because
   typing-speed-dependent behavior confuses.
2. ~~**Empty-selection scope after a delete**~~ — **RULED 2026-08-09 (user): delete clears
   everything; no effects that are not explicit.** And it is free: deleting the selected notes
   *changes the selection*, so the ordinary settle-on-selection-change rule already closes the
   window — the rejected option was an *exception* to keep it alive, so the ruling deletes a
   special case rather than adding one. The delete-then-retype flow costs one explicit `H`.
3. ~~**Released-fret semantics**~~ — **RULED 2026-08-09 (user): adopted.** Derivation judges
   against the fret the predecessor's finger releases from — the last pitched waypoint (a 5→7
   slide hands over 7, so a following 5 is a genuine pull), and a scrape's slide-out fret (D7).
   The 2D relationship drawing follows the same rule; Phase 7 waypoint edits join the
   invalidating set.
4. ~~**Question B**~~ — **RULED 2026-08-09: Option C accepted; the notation split rejected.**
   Three options were weighed:
   - **A (status quo):** plain `H` *infers* a left-hand tap for equal/absent predecessors —
     inventing an articulation from nothing, and in quiet tension with the settled repair rule,
     which refuses exactly that inference on edit.
   - **B (full split, `LeftTap` + notation):** its unique benefit — `Hammer` strictly requiring
     an ascending predecessor — is killed twice. The **alias zone**: with a predecessor present,
     a fretting finger striking the string is the same motion and the same sound whether the
     string was ringing (hammer-on) or not (tap), so `LeftTap`-with-a-predecessor is a second
     spelling of one event — un-policeable duplication, worse than an invalid state. And the
     readability gain is ~zero: a hammer mark with no predecessor is visibly from-nowhere, so
     the tab's context already disambiguates — while the costs (format field, importer, every
     attack switch, the two-T mark collision, a 3D cell, a `Ctrl+H` replacement) are all real.
     Detection cannot hear the difference, so gameplay can never need it.
   - **C (user's intermediate — strict derivation, merged notation):** one stored value, one
     mark. Plain `H` infers only what the frets justify — ascending → hammer-on, descending →
     pull-off, **equal or absent → refuse**. `Ctrl+H` is the sole author of the deliberate
     left-hand tap (any positive sounding position). C captures B's entire deliberateness
     benefit with none of its costs — equal/absent-predecessor Hammers exist only via `Ctrl+H`,
     deliberate by construction, so the error class B's validation would catch is empty. It
     resolves the derivation/repair tension (neither ever invents a tap), and it is
     grammatically pure: Ctrl already means "state exactly, do not infer." The delta from the
     settled table is one row: equal/absent goes from Hammer-as-tap to refuse.
5. ~~**Layer 1's whole-stream sweep**~~ — **RULED 2026-08-09 (user): repair at IMPORT, so a chart
   is never invalid in the first place.** The importer runs the same Layer 1 normalization at
   import completion — import is a commit point, and "valid at every commit point" applies to it
   too. There is real data for it: the hopo branch can produce a fret-0 no-node `Hammer` from
   junk open-to-open hopo flags, which E4 rejects once enforced. The finalize-step sweep stays
   whole-stream as the invariant keeper, but never finds imported junk — the first-edit
   action-at-distance the earlier design documented as a surprise now cannot happen.

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
