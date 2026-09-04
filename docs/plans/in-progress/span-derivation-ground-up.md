# Span Derivation, Ground Up — One Clean Thought

Status: LAW COMPLETE (every rule and every consequence signed by the user, 2026-09-04, items
walked one at a time); the consolidated MACHINE below is the build contract. Next: adversarial
siege against this document, then Fable builds it as one change set. The exemplar remains the
tail-law rebuild (`afda7a2e`): rules falling out of structure, never one arm per ruling.

## THE LAW — final, all rulings folded

One idea: a span is the statement "the hand holds this grip, from here to here." Everything else
is bookkeeping about that tenure.

### The chart

1. A note stores its onset, string, fret, and its RING — how long the string truly sounds.
   Rings are facts; nothing alters one.
2. A new ring on a string ends the previous ring there. Taps included — a tap is a new ring
   sounding the stop the other hand holds, and its held fret participates fully in derivation.
3. Spans are derived fresh on every read, never stored. Authored markers are a future tool.

### When a span exists

4. A span OPENS at an onset stating a grip: two or more stops struck or claimed at one slot.
5. Sound alone may ACCUMULATE a span with three or more overlapping members. The three-member
   minimum gates founding by sound alone and nothing else — growing a standing span has no
   minimum.
6. A LANDED TRAVEL is the one onset-less open: the entire grip held through the slide, at least
   one finger arrived, two members ringing past the landing (the statement threshold). The
   landed span is EMITTED if an event ever stated it (a restrike, a growth) or its tenure
   reached the minimum distinguishable distance — the notated-distinguishability quantum, a
   musical constant, not a display read. A never-stated landed span shorter than that states
   nothing and is dropped (the ratified glide-into-restrike edge, and the chord name never
   flickers for a sliver). A held-but-never-restruck landed span IS emitted — it is what states
   the chord-name change at the landing (user, item 5).
7. NOTHING ELSE opens a span. Strings that merely ring on past a break are tails (ring-out
   opens nothing).

### When a span runs and ends

8. A span RUNS UNTIL ITS GRIP BREAKS, and only these break it:
   - A MEMBER QUITS — any posture member, carried texture included (user, item 3: "the grip
     technically changed"; this knowingly OVERRULES LAW III's classifies-never-bounds rider —
     there is ONE kind of member, and the import's contradiction cut co-terminating let-ring
     rings at grip changes is what keeps this from fragmenting passages).
   - A CONTRADICTION — a statement naming a different stop on a string the grip states or the
     hand audibly holds (Law A). The seam ownership stands: a ring dying where a new grip
     begins belongs to its own span; onsets at a seam belong to the opener.
   - Nothing else. A restatement of the same grip — restrikes, re-picks, chugs — is the same
     span CONTINUING (rule adjacency-scoped: a genuine silent gap breaks via the quit arm, and
     the restrike after a gap opens fresh). A stop the grip lacks GROWS the span in place —
     growth IS accumulation (digits print in the opening bracket; a superset strum is growth
     plus restatement wearing the repeat-box family; NO disjoint-grip guard: absorption can
     only union grips whose sounds genuinely overlap, because a dead ring fires the quit arm
     first — user, item 1). Fingers traveling together with the grip held CARRY the statement;
     the break lands where the new grip establishes.
9. The stored close is where the grip actually broke — the breaking event's onset, or where the
   statement ran out. Never a display value.
10. A slide with the grip held: the span covers the glide and ends at the landing, where the
    successor opens (rule 6) — the two tile exactly. Grip broken mid-slide: the span ends at
    the break, survivors ring out as tails, a landing reached after the break opens nothing.

### Tails and holds

11. A span may HIDE a member's tail, never shorten one. All-or-nothing per stroke, judged after
    every other tail rule. A tail hides exactly when ITS OWN SPAN — the span it was struck
    under, nothing else (user: the junction survivor draws; the figure concept is GONE from the
    tail law) — covers the whole ring, and the ring says nothing of its own. Three outs:
    entering before the span, leaving after it, technique/handover. Taps and silent holds stand
    outside on both sides. A hidden member holds its own stored ring.

### Display (unchanged by the rebuild)

12. Rule 12a's margin is display-only at the projection; Alt, selection, and the caret reveal
    the true close. Repeat boxes restate the grip at interior restrikes; the box or bracket
    sits at the first STATING onset, deferred past a landing; the chord name changes at a
    landing once names exist; no 3D duration mark; the hidden-head mark awaits its 2D+3D pair.

Notes on import interplay (verified, not assumed): imports author ZERO claims (the roll is an
accumulation figure played fast, spelled at exact GP tick timing — 2 lattice quanta per tick, no
rounding); the let-ring law cuts at grip contradictions only ("a first-time string is no
contradiction") with the last-of-series audibility cap; voices exist only in the importer.

## THE MACHINE — the consolidated build contract

Synthesis of the five designs (minimal-state, invariants, event-algebra, edge-first,
deletion-first), which converged on one shape; divergences were resolved by the user's item
rulings above.

- THE EVIDENCE OUTLIVES SPANS. One per-string table (the hand) owned by the walk: the current
  stop (empty mid-travel — a finger between stops is on none, which is how staggered slides
  refuse themselves), how far the string sounds (renewed by any sounding onset, either hand —
  the tap chains a statement through), when the current stop's statement began (the tie
  doctrine: a same-stop restrike whose predecessor's ring reaches it inherits), and whether
  this stop DISPLACED a different sounding one (the dating clamp's whole state). This one
  table replaces `ring_chain`, `ringing[]`, `grip_established[]`, and `SoundingGrips` — the
  recurring defect was this one missing object, rebuilt as partial copies.
- THREE EVENT KINDS — a slot, a landing, an expiry — with one fixed within-instant order:
  STATEMENTS BEFORE EXPIRIES. That order is why a same-grip restrike is a non-event (the
  renewal lands before the expiry is evaluated), and it is the same law `SpanCover::reaching`
  encodes at the seam (the opener wins for onsets). Expiries state nothing, which is rule 7 as
  scope: the walk never asks the opening law at one.
- THE BREAK VERDICT, total, no member classes: a statement contradicting a stated-or-sounding
  stop → break; any posture member's evidence out unrenewed → break; a stop the grip lacks →
  grow in place; else continue. No founding modes, no bearing filter, no overlap arithmetic,
  no `lone_repick_continues`, no `slotJoinsShape`, no merge choreography.
- THE OPEN, total: `own >= 2 || total >= 3` (rule 4/5 as one disjunction — `SpanFounding`
  never exists). The front: one floor `max(covered, junction bound over stated strings)`, and
  members date it from the earliest onset at or after the floor. All members bound; the reach
  is one minimum over member coverage (ring end, or a travel's landing).
- THE LANDING: discovered at the reach where a travel arrives with two or more members ringing
  strictly past; front at the landing, bracket deferred, claims carried (the fingers slid,
  they never lifted). Emitted per law rule 6's stated-or-tenure test — the quantum read is
  musical, and rule 12a's display trim stays wholly at the projection.
- LAW II (justification of hand-alone spans) unchanged; posture dedup unchanged; publishes
  position, sustain (musical close), stated_extent, closing_onset. `carry_opened` and
  `founding` are not published (no production consumers today; the census re-derives
  landing-born as stated_extent == 0 at a predecessor's close).
- THE TAIL LAW moves to own-span form in presentation: hidden iff the covering span at the
  note's ONSET (`SpanCover::reaching`) covers the ring end within its own close, and the ring
  states nothing of its own; scope and the stroke atom unchanged; holds unchanged. The figure
  id, the cross-span stretch walk, and `stillReaching` DELETE with it (the seam question
  becomes unaskable). `chartHolds`/`chartHeldStops` keep the onset query.
- DELETED WHOLE from the walk: the carry successor's ring-out arm, the settle hand-off's
  ring-out half, `grow_span_here`, `lone_repick_continues`, `slotJoinsShape`,
  `statementInForce`, `extendRingChain`, the dual-end `RingChain`, `extent_inert` and every
  member-class site, `SpanFounding`, the display-margin `drawable_until`, the six-spellings
  close plumbing. Estimated ~2,373 → 700–1,000 lines at project comment density.

## Gates (census + pins, measured against a re-signed baseline — #158 folds in here)

- Ring-out-born spans → zero; their tails and the junction survivors' tails reappear as drawn
  ink (both ruled).
- The landing family end to end: 2-note slide restored; staggered landing refuses via
  mid-travel silence; broken-grip slide frees its survivors; held-never-restruck landed span
  emitted with its name seam; glide-into-restrike dropped by tenure, not by a display margin.
- The junction/dating figures (Laws A/B, the reel fixtures): outcomes stand; the clamp is the
  hand table's displaced bit.
- Same-grip restrike chains: one span (the largest single census delta, expected and ruled).
- Growth: one span through additions; the strummed-pair + late-stop split DELIBERATELY
  INVERTS to continuation (2026-08-25's "growth keeps splitting" narrowed to the
  authored-marker context, re-examined at span-marker time).
- Roll figures: unchanged (no claims exist to merge).
- Drone-under-stabs: brackets now end where the drone's audible life ends (item 3's ruled
  consequence); the import's co-termination bounds the fragmentation; census counts it.
- Every conjunct and scope clause of the tail law re-pinned in own-span form with
  discriminating pairs; spot-proofs by clause-breaking.

## Process

1. DONE: five designs; consolidation (this document); every hole and divergence ruled by the
   user (items 1–5, growth, junction survivor, box rails, rule-12 gap scope).
2. NEXT: adversarial siege against THIS document — attack the law for incoherence and the
   machine for gate violations; findings route to the user only where a rule conflicts.
3. THEN: Fable builds it as one change set; per-rule discriminating tests; census gates; the
   old machine deleted whole; docs (`chart-ruleset.md` law block, developer guide) moved in
   the same change.
