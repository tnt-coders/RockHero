# The three surviving legato designs, precisely

Companion to `legato-simplicity-analysis.md` — the full statement of what each surviving design
actually is, how the same editing stories play out under each, what each deletes, what each
asks the user to sign, and what can still surprise them. Compiled from the designers' full
texts and the adversarial verifiers' corrections; nothing here is a recommendation.

The one-sentence versions:

- **MINIMAL-EXPRESSION** — build everything the settled plan promises, including the recalc
  window, but expressed ~3× cheaper than the abandoned WIP by moving re-derivation inside the
  one existing normalize walk.
- **RADICAL-MIN** — keep the stored model and repair engine exactly as shipped; never build the
  recalc window; delete the assist and the toggle window; one two-line guard protects a
  deliberate `Ctrl+H` structurally.
- **DERIVED-DIRECTION** — stop storing direction at all; store the author's intent (`Legato`,
  `LeftTap`); every surface derives Hammer/Pull at read time; the repair engine becomes
  impossible rather than unnecessary.

---

## 1. MINIMAL-EXPRESSION — the settled design, expressed minimally

**Stored data:** unchanged, byte-identical. Surfaces render `note.attack` directly; derivation
runs only inside planners, repair, and import — exactly as shipped.

**Verbs:** exactly as shipped today. `H` = the oracle toggle with the D14 assist (grows a
predecessor's tail when the hold test is the only blocker) and the eligible-subset clear;
H-H reverses exactly via the toggle window; `Ctrl+H` forces Hammer.

**What it builds — the recalc window, re-expressed.** The abandoned WIP died because it
re-derived AFTER planning: it reconstructed the post-edit stream, re-finalized, and replaced
the plan inside `applyChartEditPlan` — invalidating every caller that captures a plan before
applying, and duplicating ~35 lines of participation logic in the fret-entry widen (the
project's recorded recurring-defect class). This design deletes the second derivation site
instead: `normalizeChartLegato` gains a `recalc` key-span parameter (a flagged note re-derives
unconditionally inside the walk that already exists, ~12 lines); planners take a trailing span
and forward it, so **plans are final at the planner** — no merge, no signature break, every
plan-capturing caller stays correct. The widen becomes free (it already replans through the
ordinary planners). All three transient windows (fret entry, toggle, recalc) move onto one
`WindowProof {keys, history_position}` + `settleChartTransients(event)` chassis, which
structurally closes the shipped toggle window's missing seek settle. An explicit attack
statement (`H`, `Ctrl+H`, any attack verb) settles the window outright.

**Ledger:** deletes nothing functional from HEAD; net **≈ +90 lines** for the complete Layer 2
(the judge prices it +130–170 realistically), versus the WIP's ~+300. Signatures spent: none —
this is the only option that keeps every signed semantic exactly.

**Verifier's findings (survived, with wounds):** it survived every recorded kill, including
the stale-flag-destroys-`Ctrl+H` sequence, run twice. Three honest wounds: (a) the
"statement settles the whole window" simplification is a **real trade**, not the free
equivalence the designer claimed — flags live on collateral neighbours, so a statement on one
note also stops every *other* flagged note from restoring (the design's own sanctioned
fallback covers it: fresh `H` restores); (b) re-deriving "as written" could raise a Pull
across a scrape predecessor in one contrived multi-select case — needs one explicit decision
line; (c) the consumed-flags handoff between planning and apply is underspecified at exactly
the seam where the WIP died, and needs one deliberate shape.

**The point of it:** if the settled semantics are all wanted, this is the cheapest correct way
to get them. The judge's caveat: it is the best-known *expression* of the settled design, and
still the heaviest system on the table — the only survivor that adds net lines.

---

## 2. RADICAL-MIN — the shipped model, amputated

**Stored data:** unchanged, byte-identical; **no corpus re-import even required.** Its central
claim: merged notation `{Hammer, Pull}` with Pull pinned to derivation is the unique
*alias-free* three-state encoding of the two authored facts (fretting-hand-sounded;
hammer-asserted-over-pull) — every split encoding re-creates an alias zone.

**What it deletes:** the recalc window (never built — task #63 struck), the D14 assist
(~30 lines), the H toggle window + `EditorUndoHistory::dropTop` (~100 lines), and their settle
resets. **What it adds:** a two-line guard — `H` skips any note already stored Hammer/Pull —
plus refusal text for the hold-test case. Net **≈ −140 vs HEAD, ≈ −440 vs the settled
trajectory.** Zero legato-specific transient state; the stale-flag kill sequence becomes
*inexpressible* (there is no flag), and `H`'s apply press can no longer rewrite a deliberate
`Ctrl+H` Hammer to Pull (today it does, relying on the window to undo it — the guard makes the
destruction impossible rather than reversible).

**How you author each case:** ascending/descending runs → `H` (derives, as today). Left-hand
tap → `Ctrl+H` (as today). **Legato across a ≥1-beat gap → two steps: grow the predecessor's
tail (drag or the duration verb), then `H`.** The refusal when the tail doesn't reach must
*speak* — "the hold doesn't reach; drag the tail" — which rides the approved-but-unbuilt W3/W5
refusal channel. **The assist deletion must land WITH that channel, not before**, or
cross-bound `H` presses become silent no-ops.

**The residues, priced:** (a) the 5→3→2 story — retype a predecessor through a value that
breaks a Pull's justification, then onward to one that would justify it again — ends at Pick;
the mark does not come back until a fresh `H` (the plan's own words for this fallback:
"cleared stays cleared, no surprises, the 5→3→2 case disappoints"). (b) H-H stops being an
identity when a forced Hammer rides a mixed selection: press 1 extends the Picks, press 2
clears ALL stored legato *including the deliberate tap* — recovery is one visible, labeled
undo. (c) The verifier found a **fourth overturn the designer didn't acknowledge**: the guard
deletes the signed one-press re-derive symmetry (`H` on a forced Hammer re-deriving it to
Pull, test-pinned); under the guard that press means "Remove Legato" instead, and re-deriving
costs H-H through a Pick intermediate.

**Signatures spent (4):** Layer 2 (the settled plan's own restore wish), the D14 assist
(against its verb-authors-its-precondition precedent), ruling 4's toggle-restore (its premise
— grown tails — is deleted, but the H-H flattening above is a real narrowing), and the
re-derive symmetry. The stored model, D13, validation, import, and both surfaces: untouched.

---

## 3. DERIVED-DIRECTION — store the intent, derive the direction

**Stored data:** `attack ∈ {Pick, Legato, LeftTap, Tap, Pinch, PickSlide}`. `Legato` is the
authored relational statement "this onset connects to its same-string predecessor" — no
direction stored anywhere. `LeftTap` is the authored local statement "the fretting hand
strikes this from nowhere" (today's `Ctrl+H` meaning). Everything else — sustain as the
held-ness datum, D13's constants, spans, nodes — unchanged. Format changes in place; corpus
re-imports.

**Read model:** one total pure function, today's `derivedLegatoAttack` retargeted:
`resolveLegato(...) → {Hammer, Pull, Unjustified}`, clauses byte-identical (hold test, node
vetoes, released fret, E4 boundary). Computed once per chart revision into a projection table;
consumed by the tab lane, the highway, and the gameplay build. **Nothing stored can go stale
because nothing derived is stored** — the repair engine, the recalc window, and the staleness
hazard class are not deleted so much as made unrepresentable. An `Unjustified` `Legato`
sounds, scores, and draws as a plain Pick on both surfaces — the physical truth — plus a
hollow editor-chrome cue (both editor surfaces; the game never renders it); a publish lint
counts them.

**Verbs:** `H` toggles over {Pick, Legato, LeftTap} — riders (Tap/Pinch/scrape) skipped, no
derivation, no refusal, no assist, no dependency on the unbuilt refusal channel; it cannot
silently no-op because setting intent invents nothing. `Ctrl+H` = `planSetAttack(LeftTap)`,
same node-conversion guards. Cross-bound legato: `H`, then drag the tail — the hollow cue
appears instantly and fills when the tail reaches. Pull-from-a-scrape becomes directly
authorable (the resolver reads the slide-out). Legato tail-lock and break verb are dissolved
(shrinking a tail hollows the cue and destroys nothing; slides keep theirs).

**Judge's correction folded in:** the shipped toggle window + `dropTop` are **retained**
(~46 lines) so H-H on a mixed selection restores a deliberate `LeftTap` exactly — the
designer's original "exact inverse by construction" was arithmetically false for mixed
selections. Windows go 3→2, not 3→1. The window is hazard-free here: it only reverses its own
entry, and nothing derives into data.

**Validation and import:** validation shrinks to intra-note truths (E4 binds `Tap`/`LeftTap`);
`normalizeChartLegato` is deleted; one ~15-line flatten (attack stranded at sounding-position
0 → Pick) rides the entry. **No edit to a neighbour can ever make a chart invalid.** Import:
`left_hand_tapped → LeftTap` — stating exactly what the source file said, fixing the shipped
aliasing of GP's explicit left-hand-tap marking into bare Hammer; hopo destination → `Legato`
with a ~12-line completion filter (junk flags → Pick + conversion note).

**Ledger:** net **≈ −260 to −310 shipped lines, ~300 planned lines cancelled**, one relational
authority, zero repair machinery, mechanical compiler-forced enum churn. Effort ~1.5–2 weeks
including re-import and the one genuinely new design cell (the 3D hollow-cue chrome idiom).

**The residues, priced:** (a) **dormant intent is representable** — an unjustified `Legato`
persists in the file indefinitely and springs to a live mark when any later same-string edit
(including *inserting* a predecessor weeks later) re-justifies it. Cause-visible, data-inert,
undo-exact, cue-marked — every recorded killer (invisible cause, data mutation at a distance)
misses it — but it is "no surprises later" answered by chrome and a lint rather than by
structure. (b) Charts lose canonicality: byte-different charts can render identically.
(c) The alias residue: a resolved-Hammer `Legato` and a `LeftTap` draw identically and diverge
only under future edits — the same author-is-authority freedom the record already grants
`Ctrl+H`, now stored. (d) The D7 flip (pull-from-scrape) needs its own explicit re-ruling.
**Signatures spent (7):** per the ledger in `legato-simplicity-analysis.md`.

---

## 4. The same stories under each design

**S1 — The stuck mixed selection (the W5 bug, fixed this week).** All three keep it fixed:
MINIMAL-EXPRESSION and RADICAL-MIN via the shipped eligible-subset law; DERIVED-DIRECTION
trivially (the toggle is total — nothing is ever ineligible).

**S2 — 5→3→2 (justification breaks, then returns, across two separate edits).**
MINIMAL-EXPRESSION: the Pull's mark drops in edit one (repair, flag born) and **returns inside
edit two's own undo entry** (the recalc window's whole purpose). RADICAL-MIN: the mark drops
and **stays Pick** until a fresh `H`. DERIVED-DIRECTION: the mark drops and **returns
automatically** — no data changed on the note at any point; it was re-projection both times.

**S3 — The deliberate tap under fire (the recorded worst case).** `Ctrl+H` a tap; edit its
fret to 0 (repair/flatten); undo; retype to 4. MINIMAL-EXPRESSION: survives — the window dies
on undo (dual mechanism), verified twice. RADICAL-MIN: survives structurally — no flag exists,
and the guard keeps `H`'s apply press off the tap; but the *clear* press can still flatten it
(one labeled undo back). DERIVED-DIRECTION: structurally indestructible — the tap is stored
`LeftTap`; nothing re-resolves it, ever; with the retained window, H-H can't flatten it
either.

**S4 — Legato across the bound (the user's span-extension report).** MINIMAL-EXPRESSION: one
press — the assist grows the tail and derives, in one entry. RADICAL-MIN: two steps — grow the
tail, then `H`; the refusal text teaches the drag (requires the W3/W5 channel). DERIVED-
DIRECTION: `H` then grow — and the hollow cue makes the missing hold visible from the first
press, filling live as the tail reaches.

**S5 — Two-digit fret typing on a note that is a legato predecessor.** MINIMAL-EXPRESSION:
works; the widen forwards the flags and the round-trip repair drops out of the widened entry.
RADICAL-MIN: works as shipped. DERIVED-DIRECTION: trivially works — retyping a predecessor
never touches the successor's data at all.

**S6 — Import.** MINIMAL-EXPRESSION and RADICAL-MIN: unchanged — GP's explicit
`left_hand_tapped` marking continues to import as bare `Hammer` (the aliasing residue).
DERIVED-DIRECTION: `LeftTap` stated exactly; junk flags filtered with conversion notes.

**S7 — What "the mark moved" means.** Under MINIMAL-EXPRESSION and RADICAL-MIN a moving mark
is a **data change** — Layer 1 rewrote a note (possibly one you never selected), riding the
causing edit's undo entry. Under DERIVED-DIRECTION a moving mark is a **re-projection** — the
chart bytes did not change; undo of the neighbour's edit moves it back with zero derivation.

---

## 5. What each asks the user to accept

| | MINIMAL-EXPRESSION | RADICAL-MIN | DERIVED-DIRECTION |
|---|---|---|---|
| Stored format | unchanged | unchanged | Hammer/Pull → Legato/LeftTap |
| Corpus re-import | no | no | yes (routine) |
| Signatures to re-sign | 0 | 4 | 7 |
| Net lines vs HEAD | ≈ +90..+170 | ≈ −140 | ≈ −260..−310 |
| vs settled trajectory | baseline | ≈ −440 | ≈ −560 and format change |
| Transient windows | 3 (one chassis) | 1 (fret entry only) | 2 (fret entry + toggle) |
| Repair engine | kept | kept | gone (unrepresentable) |
| 5→3→2 restore | automatic, same entry | manual fresh `H` | automatic, re-projection |
| Deliberate tap safety | by settle machinery | by guard + one undo | by storage |
| Cross-bound authoring | one press (assist) | two steps + refusal text | H + drag with live cue |
| New obligations | window choreography | W3/W5 channel first | cue + lint + 3D chrome idiom |
| Dormant states | none (unrepresentable) | none (unrepresentable) | unjustified intent, cue-marked |
