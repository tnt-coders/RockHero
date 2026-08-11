# Legato ground-up simplicity analysis — the pass that produced the shipped model

**Status: IMPLEMENTED 2026-08-11.** The user ruled for DERIVED-DIRECTION out of this analysis, and it
shipped the same day (`006ae58e`, `2e840872`, `7f6c4517`). The ruling as signed is
`legato-final-spec.md`; the record of what shipped is `legato-authoring-model.md`. Everything below
is the analysis as it stood before the ruling — the candidate field, the kills, and the ledger — kept
because the kills are why the shipped design has its shape. The one line now out of date is the
paused-implementation note at the end of this header: the recalc window was cancelled unbuilt (task
#63) rather than paused.

**Analysis record (pre-ruling): ANALYSIS COMPLETE 2026-08-11, NOTHING RULED.** The user directed a ground-up
re-derivation of the simplest correct legato design, with sunk cost explicitly excluded: "the
final design should be the absolute simplest with all things considered... even if we were to
rip out EVERYTHING we have already done and re-implement from the ground up." Fourteen agents
ran it: three inventory passes (mechanisms, a signed-vs-derived constraint ledger, the recorded
kill sequences and corpus arithmetic), five independent ground-up designs under distinct
premises, one adversarial verifier per design armed with every recorded kill, and a judge.
No code changes until the user rules. Implementation of the Layer 2 recalc window (task #63)
is paused; its half-built attempt is preserved out of tree as cost evidence.

## The five candidates and their fates

| Design | Premise | Fate |
|---|---|---|
| **DERIVED-DIRECTION** | Stop storing Hammer/Pull; store authored intent (`Legato`, `LeftTap`), derive direction at read time | **Survived — winner** (with verifier corrections folded in) |
| **RADICAL-MIN** | Keep the shipped stored model; amputate the recalc window, the D14 assist, the toggle window | **Survived — the counter-pick** spending no user-personal signatures |
| **MINIMAL-EXPRESSION** | Keep every settled semantic; re-express the recalc window minimally | Survived, but the only survivor that ADDS net lines — best expression of the settled design, not the simplest correct one |
| **COMMIT-POINT** | Repair only at save/import, never per keystroke | **Killed** (stuck-toggle breaks the signed W5 law; save story self-contradicts) |
| **CONNECTED** | The tail IS the connection (D15's data half revisited) | **Killed** (growth clamp arithmetically forbids its own connection spelling; grid-coincidence destroys deliberate severs) |

## The deciding fact

Every hard mechanism in the current system — the 161-line repair engine, the toggle window,
the spec'd recalc window, the D14 assist, the entire stale-flag hazard class the plan's own
kill sequence records — exists because **a stored `Hammer` cannot say whether it was derived
or deliberate**. DERIVED-DIRECTION is the only surviving design that deletes that cause; the
other two manage it. It is also the only design satisfying the project's own derived-over-
authored rule for legato: connectedness (the relational choice) is authored; direction
(derivable) is derived. The shipped model stores the derivable half and cannot express the
authored half.

## The winner: DERIVED-DIRECTION, corrected

- **Data model:** `attack ∈ {Pick, Legato, LeftTap, Tap, Pinch, PickSlide}`. `Hammer`/`Pull`
  are replaced by `Legato` (authored: "connects to its same-string predecessor") and `LeftTap`
  (authored: today's `Ctrl+H` meaning). No stored direction, no connection datum, no flags.
  D13's constants unchanged. Format changes in place; corpus re-import routine.
- **Read model:** one total pure function — `derivedLegatoAttack` retargeted as
  `resolveLegato(...) → {Hammer, Pull, Unjustified}`, clauses byte-identical (D13 hold, E12
  node veto, E4 boundary, released-fret direction). Resolution reads only the predecessor's
  STORED fields, never its resolution — no cascade. Computed once per chart revision into a
  projection table (grid-perf precedent). An unjustified `Legato` sounds, scores, and reads as
  Pick on both surfaces — the physical truth — plus a hollow editor-chrome cue; the game never
  renders it; a publish lint reports them.
- **Verbs:** `H` toggles over {Pick, Legato, LeftTap} — a TOTAL verb: no derivation, no
  refusal, no assist, no dependency on the unbuilt refusal channel. The shipped toggle window
  + `dropTop` are RETAINED so a second press reverses the first exactly (honors ruling 4).
  `Ctrl+H` = `planSetAttack(LeftTap)`, sole tap author, same node-conversion guards.
  Cross-bound legato: `H`, then drag the predecessor's tail — the cue fills when the tail
  reaches the margin. Pull-from-a-scrape becomes directly authorable.
- **Validation:** intra-note only; one ~15-line flatten (sounding-position-0 attack → Pick)
  rides the entry. `normalizeChartLegato` is deleted; E5/E12/E19 become resolver clauses,
  stated once. No neighbor edit can ever invalidate a chart.
- **Import:** `left_hand_tapped → LeftTap` (fixes the shipped wrong-Hammer residue); hopo
  destination → `Legato`; one completion-time filter (resolves-Unjustified → Pick + conversion
  note; junk flags → Pick + note). The settle-me-Pull machinery is deleted.
- **Undo:** trivially bit-exact — derivation writes nothing, so nothing derived can be stored,
  so nothing can go stale. The recalc window is cancelled unbuilt. The recorded two-digit-entry-
  dies-on-legato-predecessors live bug vanishes structurally.
- **Deleted:** `normalizeChartLegato` (161 lines) and the repair-engine concept; the relational
  validation walk; `planSetLegato`'s derivation/oracle/assist body (~137 → ~25 lines); the
  import settle machinery; the recalc window (~300 planned lines) unbuilt; the legato halves of
  tail lock / break verb / 40-Q5; two live bug classes rooted in repairs-inside-plans.
  Net ~−260 shipped lines and ~300 planned lines cancelled.
- **Estimated effort:** ~1.5–2 weeks including corpus re-import and the one genuinely new
  design cell (the 3D hollow-cue chrome idiom).

## Signed rulings the winner overturns — each needs a fresh signature

1. **A1 / Question B** (user-personally-ruled): the enum split returns — but with NO stored
   direction and merged display, so the recorded kill's priced costs (two-T mark, second
   spelling, 3D atlas cell) never arise. The 559-line recalc WIP cost postdates that ruling.
2. **Option C's refusal row** (equal/absent → refuse): this `H` invents nothing — no direction
   is ever written — and the chart honestly shows the claim unjustified.
3. **D14 assist**: deleted; the hollow cue makes the missing hold visible and the fix is one
   discoverable drag (~1% of pairs). Cheap, undo-exact add-back if re-signed.
4. **A4.2's legato half (tail lock)**: intent persists and regrow restores, so refusal would
   block a visible, reversible edit. The lock narrows to slides.
5. **D7's derivation half**: pull-from-a-scrape becomes authorable — the verb writes intent
   only; the resolver reads the slide-out per already-signed released-fret semantics.
6. **E5-family as validity**: relational validity demoted from gate to resolution — a conscious
   redefinition, since no observable surface can ever obtain an unphysical transition.
7. **A6's mechanism**: no repair engine at import; the completion filter keeps A5's substance
   (import must not launder junk flags into authored intent).

Not overturned: D13 entire, A3's rejection, one-reading-on-both-surfaces (the cue is chrome,
not notation), ruling 4 (honored via the retained window), released-fret semantics, D16/E25,
undo exactness (strengthened).

## The strongest argument against the winner — the honest fork

DERIVED-DIRECTION makes "unjustified legato claim" a representable, file-resident, permanent
state. The shipped design's deepest virtue is that this state is UNREPRESENTABLE: every commit
point settles the chart, cleared stays cleared, nothing dormant exists. Under the winner, a
hollow note authored months ago springs to a live mark when a neighbor edit re-justifies it —
cause-visible, data-inert, undo-exact, cue-marked, but still answered by chrome and a lint
rather than by structure. Charts lose canonicality (byte-different, render-identical). Two of
the user's own principles — "derived over authored" and "illegal states unrepresentable" —
collide exactly at this field, and only the user can rank them.

**If unrepresentability wins the collision, the correct pick is RADICAL-MIN instead:** keep the
shipped stored model and Layer 1 exactly; never build the recalc window; delete the D14 assist,
the toggle window, and `dropTop`; add a two-line guard so `H` never touches a note already
stored legato (protecting a deliberate `Ctrl+H` from the apply path). −140 lines from HEAD,
−440 versus the settled trajectory, far fewer signatures spent. Its priced residue: 5→3→2
stays Pick until a fresh `H` (the plan's own "shipped fallback... disappoints" case), and H-H
stops being an identity when a forced Hammer rides the selection (one visible labeled undo
recovers). Its verifier found four overturns, not the three it acknowledged — the fourth being
the signed one-press re-derive symmetry (`H` on a forced Hammer re-deriving to Pull), which the
guard deletes; and its assist deletion must land WITH the refusal-feedback channel (W3/W5),
not before, or cross-bound `H` presses become silent no-ops.

## Standing defect found regardless of ruling

The shipped H toggle window (committed in W7) is missing its ruled seek/playback settle — a
plain transport seek can leave the window armed. Real at HEAD, verified by two independent
agents. The fix's shape depends on the ruling (chassis under DERIVED-DIRECTION; deletion under
RADICAL-MIN), so it waits for it.

## The three-way decision

- **DERIVED-DIRECTION (corrected)** — the absolute simplest correct design found; spends seven
  signatures; accepts representable dormant intent.
- **RADICAL-MIN** — the simplest design keeping the settled stored model; spends ~four
  signatures; accepts the 5→3→2 disappointment and narrower H-H identity.
- **Settled trajectory (build the recalc window)** — the heaviest correct system on the table;
  spends no signatures; the analysis's verdict is that its cost was reached stepwise and never
  priced whole.
