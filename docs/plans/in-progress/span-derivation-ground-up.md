# Span Derivation, Ground Up — One Clean Thought

Status: LAW SIGNED (user 2026-09-04, "Those rules sound right"), three holes under discussion
one at a time; five machine designs in flight; Fable consolidates, sieges, then builds the
machine as one change set. The exemplar for the target shape is the tail law rebuild
(`afda7a2e`): rules falling out of structure, never one arm per ruling.

## THE LAW — signed 2026-09-04

One idea: a span is the statement "the hand holds this grip, from here to here." Everything
else is bookkeeping about that tenure.

### The chart

1. A note stores its onset, string, fret, and its RING — how long the string truly sounds.
   Rings are facts; nothing alters one.
2. A new ring on a string ends the previous ring there. Taps included — a tap is a new ring
   sounding the stop the other hand holds.
3. Spans are derived fresh on every read, never stored. (Authored markers come later.)

### The span

4. A span OPENS when a grip is stated: an onset of two or more sounding strings, a claim slot,
   or a landed travel (entire grip held through the slide, two members enough). Sound alone may
   accumulate an arpeggio span with three or more members. Nothing else opens one.
5. A span RUNS UNTIL ITS GRIP IS BROKEN: a member quits, a finger provably moves, or a new grip
   replaces it. Restating the same grip — restrikes, re-picks, chugs — is the same span
   continuing; never a new span. Fingers traveling TOGETHER with the grip held CARRY the
   statement rather than breaking it — the break lands where the new grip establishes (signed
   with the slide verification, 2026-09-04). GROWTH CONTINUES: adding a stop to a held grip
   breaks nothing — growth IS accumulation (signed 2026-09-04; the user: growth continuing was
   already the understood design). The added member's digit prints in the opening bracket,
   exactly as accumulation's late-piling members already do. The ≥3 minimum gates only FOUNDING
   by sound alone; growing a standing span — however founded — has no minimum, because the span
   already exists. A strum that supersets the held grip is growth plus restatement: the span
   continues, grown, and the strum's own mark rides the repeat-box family.
6. The stored close is where the grip actually broke — the breaking event's onset, or where the
   statement ran out. Never a display value.

### Tails

7. A span may HIDE a member's tail, never shorten one. All-or-nothing per stroke, judged after
   every other tail rule.
8. A tail hides exactly when ITS OWN SPAN accounts for the whole ring and the ring says nothing
   of its own. Three outs, the only three: entering before the span, leaving after it, or
   carrying information (technique, a handover) — those draw whole.
9. Taps and silent holds stand outside the tail law on both sides.
10. A hidden member holds its own stored ring.

### Display

11. Rails draw the tenure (one minimum-note-distance short of a breaking head; Alt, selection,
    and the caret reveal the true close). Repeat boxes restate the grip at interior restrikes;
    the box or bracket sits at the first STATING onset, deferred past a landing; in 3D the
    ribbons and hand window carry a glide — no duration mark. The chord name changes at a
    landing once names exist (chord dictionary). The hidden-head mark awaits one paired 2D+3D
    design; the published hidden bit stays wired.

Gone as CONCEPTS under this law: the figure, the seam tie-break, the STRING/END/CROSSING
conjuncts, the crossing scan, the carry-from-ring-out, the restrike merge choreography.

## The three holes (discussed with the user one at a time, in this order)

1. GROWTH — CLOSED (signed 2026-09-04): growth continues, folded into rule 5. The historical
   split (grow_span_here's close-and-reopen) was an implementation artifact of immutable
   per-span postures, not a design need; "growth keeps splitting" (2026-08-25) is narrowed to
   the arpeggio-authoring context it came from and re-examined there at span-marker time.
2. THE JUNCTION SURVIVOR — CLOSED (user signed (a), 2026-09-04): a ring's "own span" is the
   span it was struck under, period. A ring outliving its span draws whole — junction survivors
   and static rings crossing a landing show their tails, which is the ruled preference (a ring
   surviving a grip change is news). Rule 8 therefore reads literally — one span, one coverage
   question — and the FIGURE concept leaves the tail law entirely: SpanCoverage::figure, the
   cross-span stretch walk, and the ring-end seam query all become deletable at the rebuild
   (own-span coverage is reaching(onset) plus ring_end <= the own close). All five machine
   designs argued the other way; their headline counterexample (slide members crossing their
   own landing) is PRESENCE-protected either way, and the remaining discriminating populations
   are exactly the ink the user wants. Abutment/tiling remains a derivation fact for display
   (bracket deferral, name changes), not a tail-law unit.
3. BOX RAILS — CLOSED (verified 2026-09-04): drawShapeSpan (tab_paint_core.cpp:1854) draws
   full-extent top and bottom rails for EVERY span, classes differing only in color; the 3D
   board states chord duration through hold pinning. Duration ink survives tail hiding on both
   surfaces with no display change needed.

## What must not move (behavioral gates)

- The landing figure end to end: first span to the landing (morphing in 3D), successor at the
  landing under the 2-member statement threshold (the ≥3 minimum is accumulation-only), no ink
  at the landing, box/bracket at the first onset, name-change seam ready. Includes the whole
  slide family: grip held to the landing (full figure), a member released mid-travel (span ends
  at the break, survivors are free tails, a late landing opens nothing), and an accumulated
  arpeggio grip slid whole (same rules as a chord grip).
- Junction behavior (Laws A/B outcomes) pending hole 2's ruling; the reel fixtures are the
  record.
- The tail law's verdicts except where the signed reduction changes them BY DESIGN (the
  figure-era census numbers are void; re-measure against grip-tenure spans).
- Census: landing-born successors byte-identical; ring-out-born spans to zero with their tails
  reappearing as ink; every other row explained.

## Process

1. Five independent ground-up machine designs (running; lenses: minimal-state, invariants,
   event-algebra, edge-first, deletion-first).
2. Fable consolidation into one machine; the three holes resolved with the user first.
3. Refinement + adversarial siege against the consolidated design and the law itself.
4. Fable implements as one change set: each rule pinned by a discriminating test, the census
   gates above, the old machinery deleted whole.

Landed independently (correct under any design): the seam fix `ea9237d6` (superseded when the
figure layer dies, deleted with it), the span reveal `30e88a68` + caret arm `b231af6a`, the
holds scope `208b36d3`.
