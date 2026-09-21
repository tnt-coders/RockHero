# Span Derivation, Ground Up — One Clean Thought

Status: LAW COMPLETE and BUILT. The law below is the derivation `deriveChartShapes` implements
(`rock-hero-common/core/src/chart/chart_shapes.cpp`); the MACHINE section states the shape of the
walk that carries it. The maintained plain-English spec for readers outside this document is rules
10 to 12b of `docs/developer/the-project-lifecycle.md`, which `chart_shapes.h` points at rather
than restating.

## THE LAW

One idea: a span is the statement "the hand holds this grip, from here to here." Everything else
is bookkeeping about that tenure.

### The chart

1. A note stores its onset, string, fret, and its RING — how long the string truly sounds.
   Rings are facts; nothing alters one.
2. A new ring on a string ends the previous ring there. Taps included — a plain tap is a new ring
   sounding where its own finger lands, and the stop the other hand holds beneath it participates
   fully in derivation as its claim (a tapped harmonic's claim is the fret it presses).
3. Spans are derived fresh on every read, never stored. Authored markers are a future tool.

### When a span exists

4. A span OPENS at an onset stating a grip: two or more stops struck or claimed at one slot.
5. Sound alone may ACCUMULATE a span with three or more overlapping members. The three-member
   minimum gates founding by sound alone and nothing else — growing a standing span has no
   minimum.
6. A LANDED TRAVEL is the one onset-less open: the entire grip held through the slide, at least
   one finger arrived, two members ringing past the landing (the statement threshold). The
   landed span is EMITTED if an event ever stated it (a restrike, a growth) or its tenure
   STRICTLY EXCEEDED the notated-distinguishability quantum. The comparison is strict rather
   than inclusive because the importer synthesizes every glide-into-restrike arrival exactly one
   quantum before the replacing onset, so the equality case IS the suppressed population. The
   quantum is the minimum sustain distance the presentation margins already use, read at the
   CLOSING onset — one constant deliberately shared with presentation and referenced as a
   duration, never a pixel. A never-stated landed span at or under the quantum
   states nothing and is dropped, so the chord name never flickers for a sliver. A
   held-but-never-restruck landed span IS emitted — item 5 means a restrike is not REQUIRED,
   never that tenure is waived. "Held through the slide" is END-INCLUSIVE at the landing
   instant: a member's ring dying exactly AT the landing belonged to the predecessor (the seam
   ownership), and the survivors ringing strictly past open the successor.
7. NOTHING ELSE opens a span. Strings that merely ring on past a break are tails (ring-out
   opens nothing).

   **A RING BELONGS ONLY TO THE SPAN IT WAS STRUCK IN.** Every member's onset lies inside its
   span. Once the span a ring was struck in has ended, it founds nothing and joins no grip —
   fretted, open or chimed alike — and only a restrike puts it back into a statement. A bracket
   announces a stop ONCE, in the span that struck it; from there the tail is what says the finger
   is still down. At a SLOT OPEN the coverage frontier is the test: a ring struck STRICTLY BEFORE
   it has its onset behind every span still to come and folds into nothing, while one struck at
   or after it is fresh evidence and folds in like any other member. This is a question about the
   ring's TENURE only — a node strike still states its node into the grip it is struck in (THE
   NODE GRIP is untouched).

   THE LANDING IS THE ONE SEAM A MEMBER CROSSES, and the one place the HAND still decides. A
   landing is the grip itself moving under fingers that slid and never lifted, so the survivors
   are the closing span's own FRETTED members: a ring that span never held was never its member,
   and a HAND-FREE ring — one the fretting hand presses nothing for, fret 0, covering the open
   string and equally the node a natural or open-string tap harmonic touches (`handFree`), since
   that finger lifts the instant the chime sounds — neither slid nor stayed. It hands nothing to
   the successor and counts toward no survivor threshold. The consequence is accepted by name: a
   one-string slide over a struck drone lands into no bracket.

   OF THE RINGS REFUSED AT EITHER SITE, ONLY THE OPEN STRING IS TEXTURE (`textureStop`): printed
   in the bracket of a later span it rings under, because an open string's 0 stays true for
   exactly as long as it sounds and claims no finger. Any other stop would claim one — a fretted
   ring's was already announced by the span that struck it, and a natural harmonic's node was true
   at the strike and false a moment after it, harmonics being fretted instantaneously — so every
   other stale ring is a plain tail: it prints in no later bracket and classifies nothing.

### When a span runs and ends

8. A span RUNS UNTIL ITS GRIP BREAKS, and only these break it:
   - A MEMBER QUITS — any posture member, carried texture included. The grip technically changed,
     and there is ONE kind of member: LAW III's classifies-never-bounds rider is overruled, with
     the import's contradiction cut of co-terminating let-ring rings at grip changes as what keeps
     this from fragmenting passages.
   - A CONTRADICTION — a statement naming a different stop on a string the grip states or the
     hand audibly holds (Law A). It is a graded witness, because EVIDENCE OUTRANKS ASSERTION: the
     hand's SOUNDING stop and the span's SOUNDED stops always witness (same-stop is the tie
     doctrine, even beside a claim that dated a move away); a differing CLAIM against a carried
     claim always witnesses (assertion against assertion is the charter re-authoring the hand);
     and a differing STRIKE against a carried claim witnesses only where the grip is ESTABLISHED
     (the span has sounded members) and the string is silent — against a still-assembling silent
     statement the strike is evidence arriving, not contradiction: it joins the assembly in place
     (LAW II). The seam ownership stands: a ring dying where a
     new grip begins belongs to its own span; onsets at a seam belong to the opener.
   - THE STATEMENT-CHARACTER SPLITS: a span's statements keep ONE character — whole or in parts —
     and the walk splits where the character turns, so the class is a fact of the span's founding
     rather than a retroactive verdict on everything it ever contained. Two directions.

     THE ABSORPTION RULE governs both directions below, because it is what DECOUPLES the two laws
     a whole-grip stroke used to state at once. THE BOX LAW is display and UNCONDITIONAL:
     simultaneously struck notes wear a chord box, spans included (the drone-under-stabs
     boxes-inside-brackets precedent), and the display reads only the co-struck group's own
     fretting-hand count. THE SPAN LAW is structure and CONDITIONAL: a whole-grip stroke is a span
     BOUNDARY, and a box-class statement, only where it STANDS ALONE. Where same-hold material
     sounds IN PARTS within the stroke's ring extent — the next slot strikes a PROPER SUBSET of
     the stroke's stops at those same stops, and some member the subset does not restate is still
     sounding ITS OWN STOP strictly past that slot — the stroke is ABSORBED: the standing span
     flows through it, its members fold in as same-stop restatements, and it wears its box inside
     the span. The evidence is never the ring-divergence of the stroke's own members; it is what
     FOLLOWS within the rings, so the judgment is PENDING at the stroke and the next slot settles
     it — one predicate over the stored stream, read by all three sites that ask it. A MID-TRAVEL
     channel holds nothing, so a chord slide with transit picks is never absorbed.

     Parts -> chord (the unison restatement): a stroke striking EVERY stop the span states is
     the whole grip said in unison — a chord statement WHERE IT STANDS ALONE. It closes a
     SOUNDS-IN-PARTS span (the arpeggio's grip strummed whole is a chord), and it closes ANY
     span when the stroke also strikes a string never stated — a strict superset states the
     whole chord AND MORE, a new statement, never growth (a rung dyad followed by the full
     chord strummed is two statements, not a dyad quietly growing into a figure that later
     texture brackets whole).

     Chord -> parts: a stroke sounding PART of what a never-in-parts span STATED — some of its
     own stops, not all — is the statement coming apart, so the chord span closes there and
     the partial founds the parts span through the ordinary slot open, which carries the
     still-ringing members in as texture and births it in parts. The bracket covers exactly
     the ground that sounds in parts. Where the partial is the stroke's own next slot and the
     stroke is still sounding under it there is no split at all — the stroke is absorbed, the
     span flows, the class turns in place, and the box a strum earned survives literally, as a
     box drawn inside the bracket. This direction cuts everywhere else. A stroke touching only
     strings the span never stated is NOT this direction: it states nothing about the span's
     own stops coming apart, so it is the statement still assembling — growth. Below the
     slot-open thresholds the partial founds nothing and the notes ride bare.

     Three guards, each with its own ground: a LANDING SUCCESSOR arrives stated by no event, so
     its FIRST sounding defines its character in place — a lone re-pick turns it parts where it
     stands, splitting nothing; a member MID-TRAVEL means transit, and transit turns NOTHING —
     no split (the glide is not the figure coming apart; the close belongs to the landing, rules
     8 and 10) and no class turn either, a chord slide with transit picks being chord frames
     joined by slide lines rather than an arpeggio bracket, so the box the chord earned survives
     its own slide out; and CLAIM-CARRYING spans stand outside both directions for now — a strike
     at one is evidence arriving against the claims (LAW II), not a character turn, and their
     class still turns in place, the one surviving retroactive flip.

     THE PARTIAL SLIDE: a slot's statement is divided by its own notated rings when a held
     member's ring ends STRICTLY BEFORE a co-struck glide arrives — that member's sound dies
     while the statement is still in flight, so the figure necessarily sounds in parts from
     that very slot. Against a never-in-parts span it is the statement coming apart AT the
     slot even where it restates the whole grip: the box closes there and the slot founds the
     parts figure, dated at its own onset. A span such a slot FOUNDS is likewise born in
     parts. A voicing-shift slide whose held strings ring the whole transit stays one
     statement (the chug that slides up is still a chug), and a whole-grip travel — every
     struck channel gliding — is the chord slide instead ([D2]), box-classed to its landing.

     What continues is exactly the chug chain: a never-in-parts span restruck at precisely its
     own grip — the chug merge and the pinned heads stand. The FOUNDING slot never splits (no
     span stands at its own open, though a partial-slide founding is born in parts), and
     partials against an already-parts span ride the bracket that already covers them.
   - Nothing else. Any other restatement of the same grip — restrikes, re-picks, chugs — is
     the same span CONTINUING (rule adjacency-scoped: a genuine silent gap breaks via the quit
     arm, and the restrike after a gap opens fresh). A stop the grip lacks GROWS the span in
     place — growth IS accumulation (digits print in the opening bracket; growth arrives by
     plucks, claims and carries — never by a whole-grip superset strum, which the split above
     takes as a new chord statement; NO disjoint-grip guard: absorption can only union grips
     whose sounds genuinely overlap, because a dead ring fires the quit arm first). Fingers
     traveling together with the grip held CARRY the statement; the break lands where the new
     grip establishes.
9. The stored close is where the grip actually broke — the breaking event's onset, or where the
   statement ran out. Never a display value.
10. A slide with the grip held: the span covers the glide and ends at the landing, where the
    successor opens (rule 6) — the two tile exactly. Grip broken mid-slide: the span ends at
    the break, survivors ring out as tails, a landing reached after the break opens nothing.

### Tails and holds

11. A span may HIDE a member's tail, never shorten one. THE CURTAIN IS UNIVERSAL, so no span is
    needed for the verdict at all: **EVERY technique-free fretting-hand tail RESTS, from its own
    last always-visible landmark, unless it is still STATING at its end.** Judged per member, over
    open board exactly as under a bracket. Coverage is not part of the verdict, so
    `presentedChartNotes` takes no spans.

    WHERE THE VERDICT BINDS — THE EXECUTION FORM. Resting is the HIGHWAY'S form, not the presented
    stream's. The law judges and publishes the verdict but EMPTIES nothing: the presented stream
    carries every member's rules-1-to-4 tail — the EXECUTION FORM, literally the normal note
    presentation (rings not LONGER than the kept-sustain bound clipped by rule 3, which measures the
    ring in seconds through the tempo map and states its value once at
    `g_minimum_kept_sustain_seconds`; margin-trimmed). The 2D lane draws that form ALWAYS — the lane
    is the charter's exact-duration surface. The 3D board suppresses resting ribbons at rest —
    structure reads at distance — and draws each only inside a SLIDING WINDOW rising from the hit
    line,
    `g_tail_reveal_lead_whole_note` deep at the note's own meter and tempo (THE TUNABLE — a note
    value, never a pixel; its initializer is the one statement of its value, and no prose restates
    it): fully lit at the line, fading to nothing at the window's outer edge, the ink materializing
    continuously as it scrolls in. It is the window, never a whole-tail fade. THE LOOK is a steep
    power-curve falloff over the window — a long faint premonition, the real ink condensing only
    near the line — with the ribbon's tessellation anchored to the window's own sixteenths so the
    curve stands still on screen while the tail slides through it. The curve's exponent lives in
    the renderer's own multiply chain and is restated in no prose.

    A tail still stating at its end draws at all distances, a finished statement's stated portion
    rides outside the curtain, and a handover's whole ribbon draws because its transfer finishes at
    the takeover. Taps stand outside the law on both sides. The hold channel reads
    the VERDICT rather than tail emptiness, so restoring the resting tails cannot re-release the
    pins. THE HOLD IS THE TENURE: every live fretting-hand member whose tail RESTS or was never
    earned, covered by a span, is held to the span's reach — resting and rule-3-emptied members
    alike, the restrike
    interior included, because coverage past a member's ring IS the renewal record: the restrike
    replaced the sound, never the finger, so the board pins what is held through the whole tenure.
    There is no strum-size gate; a lone covered member is a grip member. (A ring CAN die strictly
    inside its own span: same-grip RENEWAL carries the span past a replaced ring's death, so
    restrike interiors — chug chains, re-picked steps — die inside their own span.)

    The STORED-RING FLOOR is keyed on COVERAGE, which is the opposite of keying it on the verdict:
    a resting member raises to its stored ring — and then to the span's reach — exactly where a
    span DOES cover it, and a lone resting note, which under the universal curtain is every plain
    note on open board, is never reached and holds for the tail it presents. Keyed on the verdict
    the floor would run a lone note's head pin out to its untrimmed stored ring and into the next
    note's margin, which is why it lives in the covered-group walk (`chartHolds`).

    THE CLOSER IS NOT SPECIAL: a span hides ALL its tails except the explicit exceptions above.
    The consequences are the headline visual change and are accepted — plain sustained chords and
    quarter-note chug chains go ribbonless (rails, repeat boxes, and holds state the tenure;
    Alt/selection/caret reveal the close), and every co-terminating let-ring figure rests whole,
    closer included. STRING's vacuity (growth-in-place makes every sounding string a posture
    member) and END's vacuity (every sounded member bounds, and renewal covers the own-restrike
    arm) stand as PROOFS beside the law, both conditional on no non-bounding member class ever
    returning.

### Display

12. Rule 12a's margin is display-only at the projection; Alt, selection, and the caret reveal
    the true close. Repeat boxes restate the grip at interior restrikes; the box or bracket
    sits at the first STATING onset, deferred past a landing; the chord name changes at a
    landing once names exist; no 3D duration mark; the hidden-head mark awaits its 2D+3D pair.

Notes on import interplay: imports author ZERO claims (the roll is an accumulation figure played
fast, spelled at exact GP tick timing — 2 lattice quanta per tick, no rounding); the let-ring law
cuts at grip contradictions only ("a first-time string is no contradiction") with the last-of-series
audibility cap; voices exist only in the importer.

## THE MACHINE

The shape of the walk, from the five designs that converged on it (minimal-state, invariants,
event-algebra, edge-first, deletion-first).

- THE EVIDENCE OUTLIVES SPANS. One per-string table (the hand) owned by the walk, holding: the
  note whose statement the string carries, from which the CURRENT STOP is read (empty mid-travel —
  a finger between stops is on none, which is how staggered slides refuse themselves); TWO reach
  columns — `covers`, the fretting hand's own reach, written only by a member strike and capped at
  a travel's landing, THE ONLY input to the span's reach and close, and `sounds`, renewed by any
  sounding onset of either hand, THE ONLY input to renewal and continuity, so the tap chains a
  statement through without ever moving a close; `stated_since`, when the current stop's statement
  began (the tie doctrine: a same-stop restrike whose predecessor's ring reaches it inherits); and
  `foreign_until`, the end of the string's last FOREIGN sound — the latest instant it audibly
  sounded a stop other than its current grip's, which is the dating clamp's whole state. A
  displacement is the special case whose foreign end is the displacing strike, and a foreign ring
  dead into silence bounds at its own end, so nothing resets it. Splitting the reach in two is what
  lets the table outlive spans, and it is why a ring can contradict a grip that no longer records
  it. TWO QUERY WINDOWS over the one stop, named so the builder cannot collapse them: the
  CONTRADICTION and DISPLACEMENT witnesses read end-INCLUSIVELY (the same-string clamp puts a
  displaced ring's end exactly on the displacing strike — read strictly, the junction is
  invisible), while MEMBERSHIP, the fold-in, and the open count read STRICTLY (a ring ending at a
  slot crosses no slot; the inclusive reading births zero-length spans).
- THREE EVENT KINDS — a slot, a landing, an expiry — with the within-instant law in three
  sentences. LANDINGS RESOLVE FIRST (the predecessor's close and its successor stand before the
  slot is judged — settle before branch); EVERY VERDICT — break, contradiction, displacement, the
  foreign-sound record — is evaluated against the PRE-INSTANT table; then statements apply, and a
  string's expiring evidence is renewed iff THIS slot sounds THAT string (either hand), so expiries
  fire per string against the renewed table. A grip that breaks RELEASES the instant's onsets to
  the ordinary opening law (the seam ownership: onsets at a seam belong to the opener; nothing
  prints twice, nothing is swallowed). A same-grip restrike is a non-event because every expiring
  string is renewed; the mixed chug renews only the restruck string; the let-ring boundary chord
  renews nothing, so the break precedes it and it opens fresh. Expiries state nothing, which is
  rule 7 as scope: the walk never asks the opening law at one.
- THE BREAK VERDICT, total, no member classes: a statement contradicting a stated-or-sounding
  stop → break; any posture member's evidence out unrenewed → break; a stop the grip lacks →
  grow in place; else continue. No founding modes, no bearing filter, no overlap arithmetic, no
  merge choreography.
- THE SLOT OPEN: `own >= 2 || total >= 3`, rules 4 and 5 as one disjunction. THE LANDING OPEN is
  its own one-line law, not a case of the slot law: read literally the disjunction would refuse the
  gated 2-note slide, since a landing has own = 0. Rule 6 opens at two survivors because a
  landing's members were already ESTABLISHED members of the span that just closed — the 3-minimum
  gates members ARRIVING staggered, and nothing arrives at a landing. The front: one floor
  `max(covered, foreign-sound end over stated strings)`, and members date it from the earliest
  onset at or after the floor — `covered` is this dating floor and never a reach input, and the
  asymmetry is the one thing separating the front from the reach. All SOUNDED members bound (a
  claim lends no ring: it neither bounds the reach nor feeds the quit arm, so a span of claims alone
  runs no distance — LAW II is what governs hand-alone spans); the reach is one minimum over
  sounded-member `covers`. THE INVARIANT:
  every span with a sounding member is strictly positive — an offset-zero keyframe arrival is the
  one hazard, and the no-zero-length-travel fixture guards it.
- THE LANDING: discovered at the reach where a travel arrives with two or more members ringing
  strictly past; front at the landing, bracket deferred, claims carried (the fingers slid, they
  never lifted). Emitted per rule 6's stated-or-tenure test — the quantum read is musical, and rule
  12a's display trim stays wholly at the projection.
- LAW II (a claim is a member its own carrier sounds; hand-alone spans stand at their instant) and
  posture dedup are unchanged. THE PUBLISH LIST is
  exactly nine fields, complete: `position`, `sustain` (the musical close), `stated_extent`,
  `closing_onset`, `posture`, `silent_member`, `sounds_in_parts`, `bracket_position` and
  `landing_opened`. `landing_opened` is kept because its one honest census key is itself — the
  `stated_extent` proxy fails both ways on pinned fixtures — and it has exactly one cause now that
  ring-out opens nothing. There is no `founding` field: with growth answering the arriving-stop
  question for everyone, how a span was born stops being a fact anything reads. Publication rides
  the push: emit is the one writer of claim reaches, which is what makes both the LAW II drop and
  the tenure drop safe.
- THE TAIL LAW is presentation's own last pass, per rewritten rule 11, and reads no span at all.
  The atom is the MEMBER, judged per note; the landmark is where the ring stops stating anything of
  its own. `chartHolds` and `chartHeldStops` keep the onset query, and they are the two readers of
  `SpanCover` — whose one query is `reaching`.

## Pinned figures

The discriminating fixtures the law is pinned by. Each is a figure, not a count: a census delta is
a measurement, and only the figure states what the law is supposed to do.

- Ring-out-born spans do not exist; the tails and junction survivors that would have been theirs
  are drawn ink.
- The landing family end to end: the 2-note slide opens; a staggered landing refuses via
  mid-travel silence; a broken-grip slide frees its survivors; a held-but-never-restruck landed
  span is emitted with its name seam; a glide into a restrike is dropped by tenure, not by a
  display margin.
- The junction and dating figures (Laws A/B, the reel fixtures): the clamp is the hand table's
  foreign-sound end, and the displacement junction is its special case.
- Same-grip restrike chains derive as one span.
- Growth: one span through additions; the strummed-pair-plus-late-stop figure is a CONTINUATION,
  which inverts the older growth-keeps-splitting reading and is re-examined at span-marker time.
- Roll figures derive from the notes alone, no claims involved.
- Drone-under-stabs: the bracket ends where the drone's audible life ends; the import's
  co-termination is what bounds the fragmentation.
- The tail law's clauses — TIME, PRESENCE, scope — are pinned with discriminating pairs and
  clause-breaking spot-proofs; the STRING and END vacuity proofs each carry a comment beside the
  law.
- Rings crossing INTO an abutting successor, landing tiles included, draw.
- The covered-ground carry fixture is discriminating: the carry's early death breaks the span.
- The partially-staggered slide is its own figure: two arrivals together open the landed pair at
  threshold 2 while the third, mid-travel, states nothing and its later lone landing opens nothing.
- The cross-meter tenure pin: a 4/4 glide closing on an x/8 downbeat reads the quantum at the
  closing onset's measure.

## The hold-under exemption

A pull-off's landing stop (`chartPlantedStops`) proves a finger at the RELEASE and no earlier, so it
states nothing by itself: a source states the fret it sounds (re-ruled 2026-09-19; the law text is
in `chart-ruleset.md`, THE HOLD-UNDER LAW). What survives is an EXEMPTION, asked of the one
authority `gripStatement(note, planted, down)`, which admits the landing stop only where it equals
the stop under judgment. Every site passes the evidence it already holds: `plants_under` passes the
stop being compared (what the string still held, the standing grip's entry, or a carried claim's
fret), and `grip_statement_of` passes the STANDING span's own entry for the string
(`gripped_before`: sounded, or claimed where nothing sounded it, and empty where no span stands),
so a source above a stop the grip holds states it and never rewrites its entry, while a stroke
that founds a span states exactly what it strikes. A span stops standing the moment a stated
member's sound dies unrenewed (`in_force`), so a CHOKED grip member ends the exemption with the
span — the derivation does not guess that the finger stayed. The second arm, `planted_under`,
reads the verdict rather than re-asking: a strike's answer is recorded once, at the strike, in
`StringHand::stated_beneath`, because the strike itself replaces the evidence it was judged
against — gated on the same `sounding_before` witness the displacement reads, so a dead source
exempts nothing. The claim witness takes the strike arm only — no finger sounds a carried claim.
The pair reaches the verdicts, the statement-began column and the foreign-sound floor; a tapped
source's derived claim is admitted at the slot read under the same proof, an authored one always. The character split's arithmetic counts a stated string
as touched only where the strike RESTATES the span's own stop — a provable no-op before the law,
and what keeps the ornament from reading as the statement coming apart.

THE GRIP COLUMN HOLDS PLACES, NOT FRET NUMBERS. Every stop the walk carries — the strikes,
`stated_here`, the open span's grip, the landing table, the posture — is a `ChartStop`, the
`(fret, node)` pair a note already spells, so a natural harmonic states its NODE through the one
channel reader (`statedStopFrom` wraps its answer in `frettingStopAt`) and node 5 is neither fret 5
nor the open string. Claims and plants stay pressed frets and are lifted into the column through
`frettedStop` / `gripStatement`. Exactly ONE harmonic clause exists in the walk and it lives in that
one function (RULED 2026-09-18): a harmonic over a PRESSED stop states that stop rather than a
finger a pull-off plants beneath it, and the grip column and both hold-under arms read it alike.
Every other split a harmonic makes is the ordinary contradiction law reading a stop that can no
longer say a node is fret 0, and a node on an unstated string grows the span like any new stop.
