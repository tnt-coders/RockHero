# Arpeggio Authoring — The Held Shape a Note Stream Cannot State

Status: **STORAGE RE-DECIDED 2026-08-27 — option X, the silent member INSIDE the note stream.**
Option F (the fret-optional hold marker) shipped 2026-08-26, was corrected at its first sighting
the next morning, and was then replaced the same day by the user's own model: a silently-held
member is a NOTE with `attack: none`. Everything below option F's line stays in its original tense
deliberately — it records why each decision went the way it did, not what the code looks like
today — and the section immediately following is the one that supersedes the storage half of it.

## The substrate swap, 2026-08-27 — X, which this record killed on sight, is the answer

The record's own survey named the shape first and killed it fastest:

> ### X — A silent member inside the `notes` array (killed on sight)
> **Killed.** It pollutes *every* consumer that iterates notes … It also breaks the note invariant
> outright: `chart.h:427` — "Every note rings for some length, so zero is not an encoding."

Both objections were real and both were answerable, which the survey did not test:

- **The invariant.** "Every note rings" was never a statement about NOTES; it was a statement about
  STROKES, and the model had only stroke-notes to say it of. It is now attack-conditional in the
  same shape the pick slide's latent overrides already used: strictly positive on every attack that
  sounds, exactly zero on the one that does not, refused in both directions. That is a rule the
  reader, the writer and the validator each state once.
- **The pollution.** It is real and it is the price. Every reader that means "what SOUNDS" asks one
  named predicate (`silentHold`), and the sites are listed in the deletion inventory below. What
  the swap BUYS in return is larger than what it costs: one slot space instead of two (disjointness
  becomes slot uniqueness, which the stream owed anyway), one selection kind instead of two, one
  edit plan instead of a widened one, one array in every range verb, and the fret-absent form gone
  entirely — a note always carries a fret, so the α/β split that shaped the whole survey dissolves.

**The span law, ruled with it.** Membership is unchanged (two or more members at a slot, a hold
counts, a hold is never a strike). What is new is that a span whose members are ALL holds must be
JUSTIFIED by the content it fronts — a fretting-hand onset arriving at a claim's own stop, or a
picking-hand onset on one of its posture strings, taps at the very same instant included — because
such a span is authored in FRONT of that content by design. Until something arrives it waits,
unended, with its posture stated at an instant; from the arrival its extent is the content's. One
that closes with nothing having arrived DISSOLVES, which is the same nothing a lone member states.
The maintained spec is rule 12b in `docs/developer/the-project-lifecycle.md`.

**Known unrepresentable, flagged rather than solved** (user, 2026-08-27): a held fret on the very
string being tapped at the very same instant. Two facts, one slot — the hold and the tap would have
to share a `(position, string)` — so the charter states the hold one quantum early instead. That is
a choice the notation can express; it is recorded here so nobody re-derives it as a bug.

## The span law and the settle, 2026-08-27 — what a held stop must EARN

The swap above put the record inside the note stream. This section is the law that decides what such
a record is worth, ruled the same day, and it is what turns "a hold is saved" into "a hold states
something". The maintained spec for all of it is rule 12b in
`docs/developer/the-project-lifecycle.md`; what is here is why.

**Zero-sound spans are allowed, and must be justified.** Two held stops at one slot open a span at
their OWN timepoint — the user's framing is that these are *"usually authored in front of content
that already exists to show the hand position to hold"*, so the record has to be able to precede its
content. What keeps that from printing a posture over silence is the second half of the same
sentence: *"They would just require a matching note to follow otherwise the span would dissolve."*
Two things count as that matching content — a fretting-hand onset arriving on a claimed string at
that claim's own stop (the standing fret-match law, not a new one), or right-hand onsets on the
shape's posture strings, taps included at exactly the same timepoint. Same-instant authoring had to
work: requiring the hold to be planted a quantum early would be a convention no notation asks for,
and the charter would have to learn it for no reason.

**One corner the ruling and the format meet in, and it wants the user's word.** The derivation asks
for no plant offset, as ruled. Slot uniqueness asks for one anyway in exactly the justifying case: a
tap on a CLAIMED string at that claim's own instant would be two notes on one `(position, string)`,
which is the unrepresentable case flagged above. So the tap that justifies a hand-stated shape is
always at least a quantum after the stop it articulates, and the same-instant tap a charter can
actually write lands on a string the shape does not hold — where it justifies nothing, by the same
rule's own discrimination. The build states the general law (no offset is required of anything the
format can express) and the corner is recorded rather than papered over; the first build's test for
this case asserted on a stream carrying that very collision, which the validator refuses, and it now
taps an eighth later.

**Growth splits, for the authored member too.** A stop taken inside a SOUNDING shape on a string
that shape does not state is the hand in a different shape from that instant, so the span splits —
which is the same answer rule 11 already gives a strum that grows by a string, and it is the
2026-08-25 growth ruling read consistently rather than a new one. This overturns the join clause the
first build shipped ("holds landing under a shape still ringing join it rather than splitting it"),
and the overturn is the honest reading of *derivation splits; authoring joins*: what the authored
hold buys is control over WHERE the statement sits, not immunity from what a shape change means. A
charter who wants the stop stated from the shape's start authors it at the start — the case this
whole record exists for. Two exceptions, each for its own reason: a shape the hand alone stated is
ONE statement with no sound to date it by, so later fingers join it; and a stop on a string the shape
already states takes no new stop at all. The split's new span inherits the old one's articulation,
claims and remaining extent, so the two cover the ring end to end and a later identical strum
re-merges by ordinary span identity.

**A hold that states nothing is REMOVED.** This is the ruling that retires the invisible-inert edge
the first sighting flagged, and it removes it by construction rather than by adding a mark: a hold
that reaches no shape — joining no span, landing past its span's end, or restating a stop the shape
already states — changes no posture, draws nowhere and can be selected nowhere, so keeping it saves
a note the charter can neither see nor find. `sweepInertSilentHolds` is the legato settle's sibling
(stateless, judging only the stream it is handed) and runs where the invariant has to hold: the
normalizer's last stage on every load, and the editor's plan gate on every edit. It iterates to a
fixpoint, because one removal can take a span's second member and strand the holds that had joined
it.

Two consequences the verb wears:

- **The cascade rides the entry that caused it.** An edit that strands a hold takes it in the SAME
  undo entry, so one Ctrl+Z restores the pair. The alternative — a second entry — would make undo
  walk back through a deletion the user never asked for.
- **`N` refuses a press that would state nothing**, whole-plan and never per slot: a chord's members
  are legal together and illegal one at a time, so nothing here could be decided note by note. The
  press is silent, exactly as a technique toggle that applies to nothing is; the counted feedback
  both want is W5's channel.

**And that refusal IS option (a), which §3 below argued was self-defeating — OPEN, and it wants the
user's word.** The objection stands, and it is now a live limitation rather than a hypothetical.
Whole-plan refusal covers the route this ruling is really for: converting a chord that already
sounds, where the selection carries every slot into one plan and the stops are legal together. It
does nothing for two EMPTY slots, because no scope can hold two of those at once — `chartVerbSlots`
answers with the selected NOTES, or with the caret's single slot — so the first of the two holds
that would form a zero-sound span is still exactly the press that refuses. **A zero-sound span the
law allows therefore cannot be authored from scratch; the only route to one is converting sounding
notes.** Three ways out, none of them taken here because each is a design choice: accept the
limitation and say so in the keymap; let `N` author onto every string a multi-string caret gesture
names, so the pair arrives in one plan; or exempt this press's own product from the settle for the
length of its own entry. The third was deliberately NOT taken: the sweep is UNIFORM over the stream
on purpose, since scoping it to "what this press wrote" would make the invariant depend on
provenance the chart does not store and would leave a loaded chart's holds unswept.

**The verb's scope, corrected with it.** `N` is selection-scoped like every other chart verb, with
the typing family's caret fallback behind it (`chartVerbSlots`, where the empty-scope rule is now
written once). The caret anchor had been reasoned from the right premise — a hand fact is stated at
the position it holds, never reached from a later note — but it enforced that premise with the wrong
mechanism, and the cost was the verb's commonest use: converting a whole chord, which is exactly the
gesture the span law's zero-sound case is for. The fallback keeps the premise (an empty slot is
reachable only through the caret, and that is where the record lands) and drops the exception.


## The first sighting, 2026-08-27 — three rulings, and one of them rewrites rule 10

The verb reached the user's hands and did not survive the first minute intact. Three rulings came
back; all three are built, and the first is a correction to the *derivation*, not to the verb.

### 1. Rule 10 is rewritten: a shape is made of MEMBERS, not of strikes

> "'one sound + one marker can never open a span' is FUNDAMENTALLY incorrect. What IS true though
> is one lone marker should not be able to open a span. It requires at least 2 markers (no actual
> sound is required to open the span, just one or more sounds OR markers)"
> — user, 2026-08-27

The threshold this shipped with — "rule 10's two-string threshold still counts SOUNDING
fretting-hand members only" — was the pre-work list's item 1, answered as proposed and wrong. What
it produced at sighting: converting one member of a two-note chord left a lone note, no span
opened, the marker fell outside every span, and the fret the conversion had just copied onto it
vanished from every surface. The verb's most obvious use destroyed its own output.

**The rule as built now.** A span OPENS at a slot holding **two or more members**, where a member
is a sounding fretting-hand onset at that slot OR a hold marker at it. One sound plus one held
finger opens a span; two held fingers with nothing sounding open one; a LONE member of either kind
opens none. A marker is still not a STRIKE — it closes no span, ends no posture, and markers
landing under a shape already held join it rather than splitting it — but "not a strike" was
carrying "not a member" for free, and that was the error.

**Extent for a zero-sound span (rules 12/12a), and why this is the coherent reading rather than a
guess.** A span runs as far as its members ring, and a hold marker rings for nothing. So a span
every one of whose members is a marker has no sounding evidence of duration at all: it runs from
its start to its start and states its posture at an instant, which is exactly where the bracket
that prints it draws. Its claims still resolve — a claim at the span's own start is inside it
whatever the length — so two fret-carrying markers state a posture nothing sounds, while two
fret-less ones have no later in-span note to take a fret from and stay inert, the same degrade
every other unresolvable claim takes.

The alternative considered and rejected was "a pure-marker span runs to the next event". It is
derivable and physically arguable (the hand holds until something else happens), but it is a
DIFFERENT rule from the sounding case, where a span dies with its members' rings however far away
the next onset is — a silent hold would persist where a sounding one does not. "Runs to where its
claims resolve" is the reading the user offered as the conservative fallback, and for a marker
carrying its own fret that resolution happens AT the span start, which is the zero-length answer.
A third option — deriving no span at all — was rejected because it puts the reported bug back for
the two-marker case.

**"No shape held" is asked of the RING, not of the walk's cursor — and that distinction is the
whole rule, not an edge.** A span stays OPEN across the silence after its members stop ringing,
because rule 11 lets a later identical strum rejoin it; the walk's "open" is therefore not
"inside". The build's first cut asked `!open`, which meant two markers opened a span only when no
span happened to be open — so for any chart whose most recent event was a chord not yet closed by a
single onset, the user's own two-marker case attached to a span that had ended beats earlier and
went inert there. The rule as it now stands opens a shape when no shape is still RINGING
(`position_beat < open->end_beat`, the same comparison the close's claim test uses), closing the
ringing-out span at its own ring — no margin trim, because nothing SOUNDS at a marker slot to keep
a distance from, and the ring has already ended at or before it.

The cost is that a marker-stated shape in the gap ends rule 11's merge across that gap: a chord,
two markers, then the identical chord again derives three spans where the markers' absence would
derive one. That is the honest reading — a hand shape stated between the strums is evidence the
hand moved — and it can only happen on a chart that authors markers, so the marker-free derivation
is untouched. The residual imprecision is the closing TRIM: a span closed by a later onset trims to
the minimum sustain distance before it, and the walk cannot know that trim when it passes a marker
slot, so a marker landing in the trimmed-off sliver still joins and goes inert. That sliver is one
minimum sustain distance wide, and closing it needs the second pass this rule was written to avoid.

### 2. Conversion preserves the fret — verified, and now pinned

`planToggleHoldMarker`'s convert case already stored `note_at->fret` on the marker. That was
correct and is unchanged; what it lacked was a test that would notice if it stopped. There is one
now, and it asserts the whole chain rather than the field: the marker carries the note's fret AND
the derived posture states it AND the bracket has a place to draw.

### 3. The dot dies — the bracket IS the marker

> "There should be no dot visible when we press N... The bracket marker IS the data point that we
> can select and modify."
> — user, 2026-08-27

So the editor's authoring dot is deleted outright, and with it the "the mark's look is unsigned"
item below. What shows a hold marker is the **arpeggio bracket** the paint core already draws at
its span's start on its string:

- Brackets draw for every posture string at the span start, marker strings included, which is what
  arpeggio notation is: "converting ONE note of a chord to a marker should add bracket notation to
  ALL notes and markers at the start of that span" (user).
- The resolved stop prints AT that bracket. A marker's string never sounds at the span start (slot
  disjointness guarantees it), so the posture-smart rule's silent-string case applies and the digit
  is centred inside the bars; the outboard slot is the tap-displacement case. Showing the stop in
  two registers is fine — what is refused is storing it twice.
- The marker's **hit target** moved from the dot to the bracket's bars, and the selection ring with
  it. The projection publishes the bracket's instant per marker
  (`ChartShapes::marker_shapes` → `HoldMarkerViewState::bracket_seconds`), so the box comes from
  the derivation that placed it rather than from a second search.

**One trade this creates, wanting the user's word.** The hit test resolves markers BEFORE note
heads, which is the one place the lane's "topmost drawn wins" rule is departed from: the paint core
draws brackets under the heads. A marker's bracket never wraps a head of its own string (that
string is silent at the span start by construction), so the only thing the priority takes is a head
a little later on the same string whose box overlaps the bracket — and that is exactly the note a
fret-less marker takes its stop from, so the overlap is ordinary rather than exotic at wide zooms.
The reason to keep the marker first is that the bracket is its ONLY affordance: yielding leaves it
a two-pixel bar, while the head keeps every column the bracket does not reach. The alternative —
heads first, strict topmost-wins — is a one-line change if the user prefers the pixels to decide.

**The edge this creates, flagged and taken deliberately:** a marker that resolves to no posture now
draws NOWHERE. It is still saved and still reachable through Ctrl+Z or another `N` at its slot, but
it cannot be seen. The three options were (a) refuse authoring where no span forms, (b) a minimal
editor-only mark for the inert case, (c) invisible-with-undo. **(a) is self-defeating** — the first
of the two markers that would form a pure-marker span is exactly the press it would refuse — and
**(b) is the dot back under another name**, against the letter of the ruling. So **(c) is the
interim, and it is stated loudly here and in `docs/tracking/watch-items.md`**, whose trigger is a
charter reporting a marker that appears to do nothing. The likeliest remedy is a mark shown only
while the caret sits on the slot, which cannot be mistaken for notation.

**Superseded the same day, and the objection above outlived it.** The span law ruled a FOURTH way —
a hold that states nothing is removed, so there is no inert record to see or lose — which retired
the watch item. But the refusal that ruling needs at the verb is option (a) after all, and (a)'s
objection is unchanged by the reversal: it is now a live limitation rather than a hypothetical, and
it is stated under "And that refusal IS option (a)" in the span-law section above.

### 4. A selected bracket takes a typed fret

> "If you select a note that is just a bracket (no onset) you should be able to set the fret number
> for that bracket (and the span should update accordingly). It should NOT alter the frets of other
> notes that were in the span."
> — user, 2026-08-27

`planRetypeFrets` now takes a marker snapshot beside the note snapshot, and the pending-entry model
is unchanged: a typed digit STATES a stop, so set-exact writes it onto a marker whether or not it
carried one; a transpose SHIFTS a stated stop and passes over a fret-less marker, whose stop the
note it reads from already carries. This answers open item 2 below for the digit path and item 4
outright.

The coherence half is a COMPOSITION, not a new rule:

> "If you currently have a span and you modify the bracket to a different note than the next note
> on that string IN the span it should split the span to keep things coherent"

Side ruling (ii)'s condition 1 already refuses to carry a span across a lone re-pick at a stop the
claim contradicts. Retyping the bracket is what makes them contradict, so the span splits with
nothing in the fret verb knowing that spans exist. Verified end to end and pinned by a test that
watches one span become two while every note's fret stays put.

**What shipped** (uncommitted at the time of writing; three stages, all verified green):

| Half | Where | State |
|---|---|---|
| `Chart::hold_markers` / `"holdMarkers"`, reader, writer, validator, normalizer | `chart.h`, `chart_document.cpp`, `chart_rules.cpp` | built |
| Read-time resolution + the arrival flip (`ChartShape::silent_member`) | `chart_shapes.cpp` | built |
| Side ruling (ii), the lone re-pick that keeps the span | `chart_shapes.cpp` | built |
| The `N` verb, v2 as proposed below (caret-anchored, three cases) | `chart_edits.cpp`, `chart_handlers.cpp`, the keybind surfaces | built |
| Selection widened to `(kind, slot)`; move and delete read both arrays | `chart_selection.h`, `chart_edits.cpp` | built |
| ~~The editor's own authoring mark (2D lane overlay)~~ | `tab_view.cpp` | **DELETED 2026-08-27** — the bracket is the mark |
| Member rule 10, per-marker span publication, bracket hit target and ring | `chart_shapes.cpp`, `chart_projection.cpp`, `tab_layout_manifest.cpp`, `chart_hit_testing.cpp` | built 2026-08-27 |
| A typed fret on a selected bracket, through the pending-entry model | `chart_edits.cpp`, `chart_handlers.cpp`, `tab_view.cpp` | built 2026-08-27 |

**Still open, and each is a sign-off rather than unbuilt wiring:**

1. ~~**The shipped verb semantics want the user's nod.**~~ **SIGHTED 2026-08-27.** The chord is
   final (`N`, re-signed from `Shift+A` 2026-08-25) and v2's three cases stand; what the sighting
   changed is the derivation under them and the mark over them, recorded above.
2. **A fret-carrying marker's authored fret still does not TRANSPOSE — narrowed, not closed.** The
   digit path is answered (a typed fret now writes onto a selected bracket, ruling 4 above). What
   remains is Alt+Shift's shape-preserving shift: it moves a marker's STATED stop when the marker
   is itself selected, and it does NOT reach a fret-carrying marker that merely sits inside a chord
   whose notes are being transposed — the barre stays where it was. That is the uniform-scope law
   working as written (a verb acts on the selection), not a special case, but the user has not
   ruled on whether a transpose should carry the shape's silent members along. The cost sheet below
   reads differently now that the population is every converted marker rather than only the
   never-sounded ones.

   The same premise runs one step further, and it is the other half of what wants ruling: F's
   storage rule says the fret is "absent where a later note in the span supplies it", and **nothing
   enforces that** — validation is slot-scoped (deliberately, see the disjointness note under F)
   and the normalizer only clamps past-board, so a converted α marker's fret sits beside the note
   that supplies it as a second, independently editable copy. The derivation now refuses to let the
   two disagree in the one place it can see them — a lone re-pick at a different stop no longer
   joins the span (side ruling (ii), condition 1) — but the record is still free to hold both.
3. ~~**The mark's look is unsigned.**~~ **CLOSED 2026-08-27 by deletion.** There is no mark to
   sign: the dot is gone and the arpeggio bracket is what a hold marker looks like, selects as, and
   is typed into. The cost is the invisible-inert edge recorded above and in
   `docs/tracking/watch-items.md`.
4. ~~**A typed digit with only markers selected does nothing.**~~ **CLOSED 2026-08-27** — it now
   states the bracket's stop, with the pending box drawn on the bracket like any head's.

~~One law moved to make room~~ — and it moved back the same week. The uniform-scope law
(`docs/plans/in-progress/editing-interaction-model.md`) briefly gained its first exception for `N`;
the exception is **retired 2026-08-27** (see "The span law and the settle" below), because what the
verb actually needed was the typing family's ordinary caret FALLBACK, not a scope of its own.

Written 2026-08-24 against `master` and reworked the same day against an adversarial review that
re-verified every code citation and re-ran the corpus scan independently. The review's verdict on
the first draft was that it **did not survive as written**: the framing axis was right, the code
citations were right, but the recommended option authored a fret that is derivable in the case the
user actually reported, the edit-consistency surface was unexamined, and the corpus table did not
reproduce. Every correction is folded in below. Corpus counts are the re-run's, not the first
draft's.

## The scenario

The fretting hand takes a full shape at an onset and holds it, but one member string is not struck
until later. The user, 2026-08-25:

> the only way we can guarantee that we can show arpeggio notation at the INITIAL onset of the
> shape when a note in that shape doesn't show up until later in the note highway but the shape
> should be held before then.

Nothing sounds on that string before its strike. There is no ring to cross the onset, no onset to
group with, no tail to fold in. The note stream contains no trace of the finger being down.

Two variants, and they are not the same problem:

- **(α) sounds later.** The member is struck at some later onset. A note exists; only its
  *earlier* presence is missing. **This is the case the user reported.**
- **(β) never sounds.** The member is held for the whole span and never picked — the ordinary
  full-barre-under-a-four-string-pattern case. No note exists at all.

The distance between α and β is the whole design. In α a note already carries the fret; in β
nothing does. Whether β must be expressible at all was this document's one fork — **it is now
ruled in**, so both cases ship, and the α/β split becomes a seam *inside* the chosen mechanism
rather than a choice *between* mechanisms.

## The fork, ruled — and the rulings around it

Three things are decided and are not re-litigated below. The first decides the document.

**(β) stays in scope — RULED 2026-08-25 (user).** The question was whether the chart must be able
to state a fretting-hand member that never sounds anywhere in the span. Verbatim:

> Yes it must. sometimes you hold the shape and never play a note in it. That is a real case.

**This collapses the recommendation to option F, the fret-optional hold marker** — and specifically
not the first draft's fret-carrying "silent stop" (option D), which authors a derivable fret in
case α. Every conditional the rest of this document was written around is closed by that sentence.

**Option A's storage does not lose; it survives inside F.** A marker with no fret **is** the
relational claim A described — same storage of a *when* and not a *what*, same read-time
resolution, same graceful degrade when nothing justifies it — and it is exactly what F stores in
case α. What the ruling buys, and what A alone could never have given, is the authored fret in the
one place no note can ever state it. (A's *gesture* — reach for the late note — did not survive;
see the rejected v1 under the verb proposal. Only its record shape did.) So the option survey below
is now the record of *why* F, not a menu, and the edit-consistency scoring is read as F's cost
sheet rather than as a tiebreaker.

**The chord is final: `N`** — first signed `Shift+A` on 2026-08-25 and **RE-SIGNED to plain `N`
the same day** (user: *"N could stand for something like 'Note Type'? … This is a common enough
thing that I like having a bare key for it, not Shift modified"*; `N` verified unclaimed in the
matrix and the registry). `Shift+A` reverts to its heavy-accent reservation, and the keymap matrix
carries both signings. The verb is unbuilt, but the chord is not provisional and is not to be
re-opened when the verb lands. The brief `Shift+A` tenancy had displaced the heavy-accent
reservation
(`:247`), whose plan of record moved to a plain-`A` emphasis cycle (`:244`) on the ground that a
magnitude is a step on an axis, not a sibling technique. §8's "a hotkey" therefore has its answer,
and the letter-map question is closed.

**Growth by a new string keeps splitting** (2026-08-25, user ruling; closes side question (i)
below). The warrant is the user's, and it is stronger than the first draft's: *the
partial-shape-then-add-finger gesture is legitimate and common.* Identical notes carry both hand
intents, so the derivation's default must notate the literal notes and the verb must author the
exception. This is not a side ruling — it is this document's thesis in the user's words, and it is
why no read-time rule can ever recover the fact.

## What the derivation does today

`deriveChartShapes` (`rock-hero-common/core/src/chart/chart_shapes.cpp`) walks onset groups and
keys an open span by its **articulation vector** — each string's whole presented note with position
and duration neutralised (`articulationOf`, `:31`). A span continues only while the vector is
identical (`:205`); anything else closes it (`:212`) and opens a new one.

Three branches decide an onset's effect, on the count of *fretting-hand* strikes in it:

| Branch | Line | Effect on the open span |
|---|---|---|
| `struck == 0` (tap-only onset) | `:167` | **transparent** — the span survives untouched |
| `struck >= 2` (chord) | `:175` | continue if the articulation vector matches, else **close + reopen** |
| `struck == 1` (lone note) | `:226` | **close**, unconditionally |

Ring-through lives inside the `struck >= 2` branch only (`:178`–`:187`): a string with no strike at
this onset whose previous note's **stored** ring crosses it folds its articulation in, so the held
note joins the posture. It folds only the most recent note per string.
`chartShapeArrivals` (`:247`) then classifies the span, asking the **presented** ring: fewer than
two notes at the start (`:282`), any picking-hand onset inside the span (`:289`), or a posture
string ringing unstruck at the start (`:307`) all make it an arpeggio.

### Which sub-cases the rings actually cover

Take an opening strum of two or more strings with long rings, and ask what happens when the missing
member finally sounds.

1. **Late member struck alone, others still ringing.** `struck == 1` → `:226` **closes the span at
   the exact onset the shape is completed.** The late note joins no posture, gets no bracket, and
   the span that preceded it never mentioned its fret. This is the motivating case, and the
   derivation's answer is worse than "incomplete": the span dies at the moment of interest.
2. **Late member struck inside a chord onset, others ringing.** Ring-through folds the ringing
   members in, so the articulation vector *grows* by one entry → mismatch at `:205` → **split**
   (the user's confirmed fact). Result: a first span whose posture omits the held fret, then a
   second span that is correctly an arpeggio. The picture is right from the second onset onward and
   wrong at the start — which is precisely what the user asked to fix.
3. **Every member struck at every onset.** One span, chord box. Works, and is the case the
   derivation was written for.
4. **Rings do not reach.** Short chugs, dead strings (E25 removes a dead note's presented tail), or
   a same-string re-strike clamp. Then even case 2's partial cover is unavailable: nothing connects,
   and every onset is its own span or no span at all.
5. **Right-hand taps over a held chord.** Works — but only because tap onsets are *declared*
   invisible to the grouping (`:167`, rule 11). That is the tell: **the derivation already has a
   mechanism for "hand holds, strings sound at staggered times", and it keys it on *which hand
   strikes* rather than on the hand's continuity.** The scenario is the same musical fact with a
   pick instead of a tapping finger, and the mechanism does not reach it.

A truly one-note-at-a-time broken chord derives no span at all: a posture needs two simultaneous
fretting-hand strikes (rule 10), and the guide says so — "no other arpeggio grouping is derived
(broken-chord grouping waits for the corpus-informed pass)"
(`docs/developer/the-project-lifecycle.md:325`). **This limit survives every option below.** See the
recommendation.

> **Superseded in part, 2026-08-27.** Rule 10 now counts members rather than strikes, so a charter
> CAN open a span on a one-note-at-a-time figure by stating its held members with markers. What
> still holds is the derivation's own limit: nothing in the note stream alone opens such a span.

## Why it is underivable

The derivation reads sound. The fact is about the hand. A finger resting on a fret makes no sound,
produces no onset, and extends no ring, so no function of the note stream can distinguish:

- a hand holding a six-string shape and picking four of it, from
- a hand holding four strings and moving to the fifth later.

Both stream identically — which is exactly the user's 2026-08-25 warrant for keeping the split. The
only recoverable signals are proxies — the fret falls inside the current hand window, the shape
matches a known voicing — and each is an *inference that asserts a claim*, wrong exactly when the
player re-fingers. The project already draws that line: inference of this kind belongs to a
**generator that writes an authored record at import** and that the user can correct
(`generateFretHandPositions`, `gp_chart_builder.cpp:987`, called at `:2218`), never to a read-time
derivation, which must state only what the data says.

There is a second, sharper piece of evidence that the datum is missing rather than merely
inconvenient. The arrival rule contains a compromise it was forced into:

> A posture string that is merely silent at the start (a partial strum of the shape) does not make
> an arpeggio. — `chart_shapes.h:141`

That rule exists **only because "merely silent" and "known held" are indistinguishable today.**
Supplying the missing fact does not add a branch to a clean design; it removes an ambiguity the
current design had to paper over. Under the "simplicity yields only to correctness" test, that is
the good direction: the yield buys back a compromise instead of creating one.

## The display contract is already built

Nothing new is needed on the drawing side, and this is the document's strongest verified claim.
`ShapeViewState::strings` declares the contract (`chart_view_state.h:306`):

> The whole held posture, stated whether or not a note sounds on the string: a posture is a claim
> about the fretting hand, not about what is struck.

The path is complete end to end. `chart_projection.cpp:202`–`:231` pushes **every** non-null
`ChartPosture::frets` entry into `ShapeViewState::strings` — a fret with nothing sounding on it is
already carried, not filtered. `tab_paint_core.cpp:2158`–`:2162` then documents the
`head == nullptr` default in as many words: *"the silent-string case: the posture keeps the centre
a fret number belongs in."* And the settled display rule
(`docs/plans/in-progress/arpeggio-posture-display-options.md:18`, SETTLED 2026-08-14) already
answers the un-sounding case: at the span start, a posture string where **nothing sounds** prints
the posture fret **centred in the bracket at fret-number size**. The highway draws the same posture
spatially as floor rails on the hand window's fret lines.

Two consequences for any option below:

- **The storage problem reduces to putting an entry in `ChartPosture::frets` at the span's start**
  and keeping the span open. No renderer changes.
- **But that is one layer, not the whole cost.** The posture digits are gated on the arpeggio
  classification (`tab_paint_core.cpp:2116`, exactly `if (!shape.arpeggio) continue;`). So the
  chosen mechanism must also flip the arrival, or the fact is stored and never shown — and *whether
  the flip is derivable or must be authored is itself a discriminator between the options*. It is
  scored in each option's cost column, not deferred to the questions list.

## The prior ruling, and how much of it is left

**§8 of `docs/plans/in-progress/chart-span-and-selection-model.md` — "Arpeggio conversion —
SETTLED" (2026-07-17), at `:189`–`:195`:**

> A hotkey converts an in-line placed note into an unplayed shape member: it adds the string/fret to
> the span's template without adding a played note. Under template-relative classification this
> flips the span to arpeggio automatically, and the existing posture rendering (unsounded template
> members) displays it. This resolves the previously tabled "display a fuller shape than the notes
> play" case without a dedicated template editor.

The first draft pivoted on "§8's verb is still the right verb; only its storage needs a new home."
That over-reads it. Read together with §2 (`:31`–`:42`), §8 rested on **four** things, and **three
are dead**:

1. **The verb** — a hotkey promoting a placed note to an unplayed member. **Survives**, and it now
   has its key (`N`, re-signed from `Shift+A` 2026-08-25).
2. **An authored template stored in the chart.** **Deleted** by note-sustain stage C, 2026-08-22
   (`chart_document.cpp:471`–`:476` rationale, `:477`–`:482` refusal).
3. **Extent by belonging to the span** — the template owned the whole span. **Deleted with it**,
   which is why the extent question below exists at all.
4. **Template-relative classification** — §2 `:32`: "a span is a chord span iff every onset group
   inside it sounds the full template". **Deleted**, replaced by the ring-based arrival rule, which
   is why the arrival flip is a cost at all.

So §8 is **dissolved on storage, on extent, and on classification**, and it cannot simultaneously be
treated as *binding on scope*. What actually survives is the verb, its now-signed key, and the fact
that the user once wanted case β. Treating §8 as still binding β while declaring its premise dead
would have been selective in exactly the direction that keeps a bigger recommendation alive — so
the scope question was put back to the user rather than inherited, **and the user ruled β in on its
own merits (2026-08-25), not on §8's authority.** The scope claim now rests on the fresh ruling;
what §8 still contributes is only its verb, which the two-mood proposal below preserves intact.

## The import angle

Measured against the local Guitar Pro corpus, re-scanned independently (`score.gpif` extracted from
each archive and grepped directly). Aggregate counts only — no song or artist is named anywhere in
this document, per the corpus firewall.

| GPIF construct | Where it sits | Corpus | Read by our importer? |
|---|---|---|---|
| `<Arpeggio>Up\|Down</Arpeggio>` | **Beat** element | 3 occurrences, 2 songs | No |
| `<LetRing />` | **Note** element | 1061 occurrences, 46 songs | No |
| `<Diagram` + `<Fingering>` | track-level `DiagramWorkingSet` | 52 / 49, 5 songs | No |
| `<Chord>N</Chord>` beat reference | **Beat** element | **0 corpus-wide** | n/a |
| `<Brush>` | — | **absent** | n/a |

Denominator: **115 `.gp` files across 102 song folders.** The first draft said "102 `.gp` files" —
it scanned one file per folder and missed 13. It also printed the let-ring tag as `<LetRing/>`,
a spelling that occurs **zero** times; the real tag carries a space, `<LetRing />`. Both are
corrected above. Importer blindness is confirmed by a repo-wide grep outside `build/` and
`external/`: zero hits for `LetRing`, `<Arpeggio`, `DiagramWorkingSet`, or `"Brush"`.

What this establishes:

- **Guitar Pro cannot state the scenario.** Its arpeggio is a *rolled strum on a simultaneous
  chord*: the sole `<Arpeggio>Down</Arpeggio>` beat inspected lists six note ids on one beat at one
  position and the mark says "roll them". Onsets are not staggered in the data. There is no
  construct for "held shape, staggered strikes".
- **`<LetRing />` is a sound statement, not a hand statement.** It says a note rings past its
  written value; it maps to `sustain`, which is MIDI truth. It must never be read as "the hand is
  there" and it can never be inflated to force notation — that is the sustain model's core premise
  (`chart.h:418`).
- **The chord diagram is the authored posture, and it is provably a dictionary.** `<Diagram>`
  carries per-string fret plus fingering, named, in a track-level collection — exactly the shape
  `chart_shapes.h:26` predicts for names and fingerings ("a dictionary keyed by a posture"). The
  first draft observed that no beat references one; the re-scan **proves** it: the beat-level
  `<Chord>N</Chord>` reference occurs zero times corpus-wide, and every one of the 78 `<Chord>`
  occurrences is the harmony spelling *inside* a `<Diagram>` item under the diagram/chord working
  sets. It is a palette, not a timeline fact — not a hold statement, and it does not solve this.
- **The synthesis hook is live and has a precedent.** If the importer ever honours `<Arpeggio>` by
  spelling a rolled chord out into staggered onsets, it would be doing exactly what the tremolo
  spell-out already does (`gp_chart_builder.cpp:485`–`:560`, one beat → N onsets, flowed through
  positions, ties and tail rules). The difference is fatal: a tremolo stroke re-strikes **all**
  strings, so every stroke keeps `struck >= 2` and the span survives. An arpeggio spell-out strikes
  **one** string per stroke, so every stroke lands on `:226` and closes the span. **An arpeggio
  spell-out is not implementable without the fact this document is about** — and if the fact exists,
  the spell-out is exactly what writes it.
- **§2's importer obligation already presumed such a source field** ("trim templates to the struck
  strings unless the source marks the hand shape as an arpeggio — verify the exact source field in
  the converter tool when implementing", `:39`–`:42`). If the external converter's source format
  does carry a fuller-than-struck chord template with an arpeggio marking, the authored fact is
  precisely its import target, and the converter is the first producer.

Net: no GP import path today; a concrete, named future one; and the external converter is the
likelier first producer. The mechanism must be writable by a generator, not only by a hand.

## The options

Every option must supply one thing: a posture member for a string that does not sound where the
notation must show it. The survey was written against the span's *start*, which is the case that
provoked it; the chosen option turned out to be more general than that (see the caret-anchored
verb's consequence under F). They differ in what they store, where, what can go stale, and what an
unrelated edit does to it.

Letters A–E keep the meaning they had in the first draft, so a reader holding that draft is not
lost. **X** and **F** are new: X is the strawman the first draft ruled out only implicitly, and F is
the option the first draft's space jumped straight over.

### X — A silent member inside the `notes` array (killed on sight)

A `ChartNote` with a "not struck" flag, sitting in `chart.notes` with the real notes. Named
explicitly because it is the first thing a reader proposes, and because §8's own wording
("converts an in-line placed note into an unplayed shape member") reads like exactly this.

- **Killed.** It pollutes *every* consumer that iterates notes — playback, scoring, hit-testing,
  presentation, projection — each of which would need a new "…unless it is silent" guard, and each
  of which is a place the guard can be forgotten with no compile error.
- It also breaks the note invariant outright: `chart.h:427` — "Every note rings for some length, so
  zero is not an encoding." A member that never sounds has no ring to state, so it can only be
  encoded by weakening the one invariant that keeps `notes` honest.
- Leaving this unnamed invites the re-walk this document exists to prevent.

### A — A membership claim on the late note

The late note carries a flag meaning "the shape this joins was already held". No reference, no id,
no fret — resolved at read time exactly like `NoteAttack::Legato`, which is "a relational CLAIM and
nothing more… read back from the predecessor by `resolveLegato` and is never stored, so no
neighbour edit can leave a stale direction behind" (`chart.h:86`), and which degrades to a plain
pick when the chart does not justify it (`chart_legato.cpp:33`–`:37`, `:85`).

- **Verb.** Select the late note, press the verb's chord. You author on the object you are looking at.
- **Format.** One boolean on the note. Nothing else. No second array.
- **Derivation.** At `:226`, a lone onset carrying the claim joins the open span instead of closing
  it, and its fret folds into the open span's posture. The posture keying (`:200`) moves from span
  open to `close_span` (`:102`), so the vector is built once when it is complete — a net
  simplification of the existing code, not an addition.
- **Arrival flip is derivable.** Once the late note is inside the span, "a posture string whose
  first sounding note inside the span is later than the span start" is a statement about the
  presented stream alone. `chartShapeArrivals` needs no new input and `ChartPosture` needs no
  provenance. This is a real advantage and the first draft never stated it.
- **Dangling.** None by construction. There is no reference. If no span is open on that string, the
  claim is unjustified and the note draws as the plain note it sounds like — the legato precedent,
  verbatim.
- **Edit consistency.** Free on every axis (see the scoring below): the flag rides move, copy,
  paste, transpose, quantize and delete without a single verb learning anything.
- **Import.** A spell-out writes the flag on every stroke after the first. Clean.
- **Rule strained.** None seriously. It stores *only a relational choice*, which is what the
  derived-over-authored rule reserves for authoring.
- **Limit, and how the ruling resolved it.** **A cannot express (β):** with no note there is nothing
  to carry the flag. The first draft called that "fatal" on the strength of a §8 scope claim whose
  premise the same draft declared dead, which was not a sound reason. The sound reason arrived
  2026-08-25, when the user ruled β in on its own merits — so A cannot stand alone. **It is not
  discarded.** Everything in this section is still live: F's α half *is* A, stored as a fret-absent
  marker, and every advantage listed above is inherited there unchanged.

### B — Authored posture-hold spans return

Bring back a stored span with a stored posture, as `shapes`/`chords` were before stage C.

- **Honest weighing:** stage C deleted these because "a document carrying them states a second,
  unverifiable copy of something the notes already say" (`chart_document.cpp:471`). That reasoning
  is untouched by this scenario. The scenario needs **one** fact the notes cannot say, not the whole
  posture; storing the posture stores the other five strings twice, and the recurring-defect rule
  ("a rule stated twice") fires on sight.
- **Its one real advantage, stated plainly because the rules require it:** B is the only option that
  gives a single intent carrier owning the whole span *including its extent*. That is exactly what
  every per-onset option gives up and then pays for in the extent question below. It is not enough
  to save B, but it is the reason the extent question is hard.
- **Recommend against.** Listed so the ruling is on the record and the option is not re-walked.

### C — A hand-data channel, FHP-style

A stored channel of hand statements — "from position P the hand holds shape S" — beside
`fret_hand_positions` in the format (`chart_document.cpp:511`).

- **Buys:** it is arguably the truthful home. It *is* hand data, exactly like an FHP; FHPs are
  already stored, already import-generated, already user-correctable, so the precedent is complete
  and proven.
- **Costs:** a *shape* channel stores a full fret vector, which restates the frets the notes already
  give for the sounding members — B's defect wearing a different hat. Narrowing it to only the
  non-sounding members makes it honest, but then it is no longer "a shape": it is a list of
  individual held stops.
- **Where it actually lands:** narrowed, C is not D — it is **F with extent-by-succession**, because
  the FHP shape it borrows (`chart.h:789`: "where the hand sits on the neck **from this point on**",
  `{position, fret, width}`, no duration) is precisely the candidate answer to the extent question.
  The first draft collapsed C into D by assuming a hand channel must carry a full vector, and lost
  that answer in the process.
- **Interaction with FHP derivation:** an FHP is a *window* (`fret`, `width`); a posture is a
  *stop per string*. They are different resolutions of the same subject and must not be merged: the
  phrase-aware generator picks windows from note frets, and a held stop is one more fret it should
  see. Any held-stop record must therefore be an *input* to FHP generation, not a second producer of
  windows.

### D — The fret-carrying silent stop (superseded by F)

A shape member the hand takes without sounding it, stored as its own record keyed by
`(position, string)` — the same key the note stream uses — **always carrying a fret**. This was the
first draft's recommendation. It is recorded here with its defect so the ruling is on the record.

- **Format.** A new top-level array beside `notes` and `fhps`:
  `{ "position": "...", "string": n, "fret": f }`. No sustain field, no attack, no technique fields
  — so "a silent member that rings" is **unrepresentable**, and `notes` keeps its invariant intact
  (`chart.h:427`).
- **Its whole defence was one invariant:** *a stop states only what no note states, and is refused
  where a note already states it.* **That invariant is scoped to the same `(position, string)`, and
  it says nothing about the same string at a later position inside the same span** — which is case
  α, the case the user reported.
- **So in α the chart carries two independently editable statements of one fret:** the stop at P,
  and the note at P+n. Nothing keeps them equal. Concretely, `planRetypeFrets`
  (`chart_edits.h:154`) transposes the *selected notes'* frets; a stop is not in `chart.notes`, so
  transposing the chord silently leaves the stop on the old fret and the posture becomes a lie.
  That is the project's recurring-defect shape — a rule stated twice — and D walks into the very
  rule this document fires at B and C.
- **And the second fret is not needed.** "The hand was already on this string at the span start"
  *implies* the fret: if the finger had been on a different stop and moved, the hand was not holding
  the shape. In α the authored datum is a **when**, not a **what**.
- **The FHP analogy it claimed does not hold as stated.** An FHP is a *window* notes fall inside; it
  is never contradicted by a note, only under-fit. A stop carrying a fret **is** contradicted
  whenever α's two copies diverge. The analogy holds on "authored hand data, generator-written,
  user-correctable" and breaks on exactly the restatement axis D was defending.
- **Superseded by F**, which is D with the fret made optional and therefore absent in α.

### F — The fret-optional hold marker (DECIDED 2026-08-25)

A record in its own array:

> `{ position, string, fret: optional }` — the fret is **required only** where nothing sounds on
> that string anywhere inside the span (β), and **absent** where a later note in the span supplies
> it (α).

Meaning: *at this position the fretting hand takes this stop, silently.* One mechanism, both cases,
and the smaller authored surface in the case that was actually reported. Note the record says
nothing about a span — it is anchored to a `(position, string)` and nothing else. The span
relationship is entirely read-time, which is what the caret-anchored verb below makes visible and
what keeps the marker free of stored relational state.

- **Verb.** §8's verb with its final chord, `N`, acting at the caret on whatever the slot
  holds — see *The verb* below, which is a proposal and not yet ruled.
- **Format.** One array. In α the record is `{position, string}` and carries **no fret at all**, so
  there is nothing to diverge from the note that supplies it. In β the fret is present because no
  note exists to state it — and, crucially, **no note can ever contradict it**, which is exactly
  the property that made the FHP analogy sound in the first place. F repairs the analogy D broke.
- **Derivation, and why it needs the same restructure A does.** The α marker's fret is resolved from
  the first sounding note on that string inside the span — which is not known while the span is
  open. So the posture keying moves from span open (`:200`) to `close_span` (`:102`), and the vector
  is built once, when the span is complete. **That is the same move option A needs, and it is a net
  simplification of existing code in both cases** — the posture is keyed once instead of per onset.
- **Disjointness, correctly scoped.** The normaliser refuses an *authored fret* where any note
  inside the span sounds on that string (that is what makes β's fret the irreducible residue), and
  a marker that resolves to nothing at all is **inert** — it draws nothing, exactly like an
  unjustified legato claim, rather than being refused at load. This matters because span extent is
  derived: the normaliser does not derive spans today, and making marker validity span-relative
  would give it a shape-derivation dependency. Inert-and-harmless follows the project's own
  precedent and avoids that.
- **Cost — the arrival flip, and it is β's cost specifically.** In α the flip is derivable exactly
  as under A ("first sounding note inside the span is later than the span start"). In β nothing
  sounds, so `chartShapeArrivals(presented_notes, shapes, postures, tempo_map)` cannot see the fact
  at all and `ChartPosture` carries `std::vector<std::optional<int>> frets` with **no provenance** —
  nothing distinguishes a fret that came from a strike, a ring-through, or a marker. So β forces one
  of two unattractive choices, and **which one is unresolved**:
  - a **new input** to `chartShapeArrivals` plus a per-span re-scan of the marker array — a second
    place that must independently agree with the derivation's fold about which strings are held
    (a rule stated twice, again); or
  - **provenance on `ChartPosture`** — which changes the posture dedup key at
    `chart_shapes.cpp:199`–`:200` (`posture_indices.try_emplace(frets, …)`: two spans with equal
    frets but different provenance become one posture or two, and neither answer is obviously right)
    and re-widens the posture that stage C just narrowed.
- **Cost — a second array, and it is not free.** F inherits most of D's edit-consistency costs; it
  removes exactly one of them (transpose divergence). See the scoring below. This is what the
  ruling bought and what it cost: had β been ruled out, none of that edit surface would have been
  worth paying, because A gives α for nothing. β is the only thing on the other side of the scale,
  and the user put it there.
- **Import.** A spell-out or the external converter writes markers for the members not yet struck —
  fret omitted where the stroke that supplies it is in the same span. The FHP generator reads
  resolved stops as extra frets.
- **Rule strained.** It is authored data about the hand, three days after stage C deleted authored
  data about the hand. The defence is narrower than D's and is what makes it hold: **a fret is
  authored only where no note could ever state it.**

#### The verb — v2, caret-anchored: BUILT 2026-08-26 exactly as proposed here

**Built, not yet used.** The storage above was decided first; this was proposed and then built
unchanged, so what is still open is the user's nod on how it FEELS, not what it does. The three
cases below are the shipped `planToggleHoldMarker`.

**v1 was rejected by the user, 2026-08-25**, and the rejection is worth keeping because it sharpens
the model. v1 proposed one chord disambiguated by the selected note's position relative to the
shape's opening onset — author β by converting a note at the onset, author α by selecting the
*late* note and having it grow a marker backwards to the shape's start. The user:

> This doesn't sound like it would feel right. It really feels like something that would be
> defined at the START of the onset manually, not later.

That is right, and not merely on feel: the α gesture asked the charter to state a fact about
position P while looking at position P+n, and let one keystroke write a record somewhere the caret
was not. **v2 deletes the disambiguation, deletes the gesture on the late note, and anchors
everything at the caret.**

`N` acts at the caret, on whatever that slot holds:

1. **Empty armed slot ⇒ author a fret-absent hold marker** at that `(position, string)`. It says
   "the hand takes this stop here, silently" and states no fret, because it does not need to: a
   later in-span note on that string supplies it at read time. Unjustified — no note ever arrives
   on that string in the span — it is **gracefully inert**, drawn nowhere and refused nowhere,
   exactly the degrade an unjustified legato claim already takes (`chart_legato.cpp:33`–`:37`).
2. **A note at the slot ⇒ convert it.** The note leaves the stream and the marker carries its
   fret. This is July §8's flow verbatim — "converts an in-line placed note into an unplayed shape
   member" — and it is now **the only fret-carrying path there is.** It must be place-then-convert
   for a concrete reason: **the only fret-stating flow the editor has is note insertion.** No other
   gesture means "fret 5 on the A string", so the charter types the fret as a note where the finger
   goes and then promotes it.
3. **An existing marker ⇒ remove it**, restoring the note it came from when it was a conversion.
   The symmetric toggle, two-state like every other mark, with no third state to explain.

Every hand fact is therefore authored at the position it holds, by a charter looking at that
position. There is no note-position-relative rule to learn and no action at a distance.

**The generalization this falls out of, recorded as a consequence.** Once the verb is caret-
anchored, **a marker is not inherently a span-start datum.** It states "the hand takes this stop
here, silently" *wherever it is placed* — the span-start case is simply the common one, not the
definition. That is strictly more general than the framing this document was built on, and it pays
for itself immediately: **a mid-span silent arrival needs no extra rule.** A finger that comes down
on a new string partway through a held shape — after the opening strum, before the string is ever
picked — is the same record at a different position, and the derivation folds it into the posture
by the same path. Nothing special-cases the span's first onset.

Read together with the ruling that growth by a new string keeps splitting, the division of labour
is complete and symmetric: the derivation splits wherever the hand's continuity is unprovable, and
one caret-anchored verb joins wherever the charter says so — at the span's start or anywhere
inside it.

### E — Infer it from the hand window

No new data: assume a later note's fret is held from the span start when it lies inside the current
FHP window and the shape is otherwise stable.

- **Costs:** it manufactures a claim, and is wrong exactly where re-fingering happens — the case a
  player most needs to see, and the case the user's 2026-08-25 ruling says is common. As a
  *read-time* rule it violates the derivation's contract. As an *import-time generator* writing a
  marker the user can correct, it is not an alternative to F but a producer for it. Listed for
  completeness and for that reading.

## Edit consistency — the axis the first draft omitted

The first draft's "Dangling: nothing to dangle" is true of *references* and irrelevant to the
actual risk, which is positional coupling across a second array. For an authoring feature this is
the dominant cost axis, and it is the axis on which A's storage half costs nothing and β's array
costs real work.

**First, what the caret anchor kills.** Under the v2 verb the marker stores **no reference to a
span, no id, and no reference to any note** — only a `(position, string)` and an optional fret. Its
entire relationship to a shape is computed at read time. So **no edit anywhere can leave stale
relational state**, because none is stored: the worst an unrelated edit can do is leave a marker
unjustified, and an unjustified marker is gracefully inert. That is the legato precedent holding
end to end, and it removes the whole class of concern the first draft gestured at with "dangling".
What remains below is **ordinary positional coupling** — the same coupling a note already has with
its own position — which is verb plumbing, not a correctness hazard.

- **`ChartNoteKey` is `{position, string}`** (`chart_selection.h:23`), and its doc comment states
  its warrant explicitly (`:20`): *"Unique by chart validation (one note per (position, string))."*
  Any second array keyed the same way reuses that identity space for a second object kind. Either
  `ChartSelection` becomes a note-or-marker selection — touching `replaceWith` (`:66`, `:72`),
  `toggle` (`:88`), `applyBox` (`:95`), the group-click, and the documented key→index **linear
  merge against the note stream** (`:54`) — or markers are unselectable, which contradicts the
  verb's own premise and the editor-visibility question below.
- **Undo has no home for the convert case.** The undo entry is `ChartNotesEdit final : IEdit`
  (`chart_edits.h:565`) carrying `ChartNotesEditPlan { removed, inserted }` of
  `std::vector<ChartNote>` (`:33`), applied by `applyChartNotesChange` (`:554`). Converting a
  placed note into an unplayed member removes a note **and** adds a marker in one gesture. That
  crosses both arrays, so it is neither a `ChartNotesEdit` nor a hypothetical `ChartMarkersEdit`; it
  needs a composite edit or a widened plan. The other two verb cases — authoring on an empty slot,
  removing a marker — touch the marker array alone and need no composite. The first draft asked
  which *key* the verb gets and never asked what the *edit* is.
- **Move leaves the marker behind, and it goes quiet rather than wrong.** `planMoveNotes`
  (`chart_edits.h:121`) operates on `ChartNoteKey`s over `chart.notes`. Drag the opening chord and
  the marker stays at the position the charter put it, which under the caret anchor is arguably
  what the charter said — but no span opens there any more, so it falls silently inert and the
  notation quietly loses the hand fact. This is the softest form of the problem (nothing becomes
  *false*, only invisible), and the fix is ordinary: the move verb should carry markers along with
  the notes it moves, which means teaching it a second array.
- **Move must also learn a new refusal.** `planMoveNotes` today refuses a destination "occupied by
  an unmoved note" (`:108`). Disjointness makes a marker an occupied slot too. Unstated in the first
  draft.
- **Copy/paste, quantize, string-shift** — every range verb must be taught about the second array or
  the markers desync from the notes they belong to.

Scored:

| Surface | A — claim on the note | D — fret-carrying stop | F — fret-optional marker |
|---|---|---|---|
| `ChartNoteKey` identity | untouched | collides | collides |
| Undo plan shape | unchanged | crosses both arrays | crosses both arrays |
| `planMoveNotes` | rides along | orphans it; new refusal | goes quiet; new refusal |
| `planRetypeFrets` | rides along | **silent divergence in α** | nothing to diverge |
| Range verbs (paste, quantize) | free | each verb taught | each verb taught |
| Arrival flip | derivable | authored | derivable in α, authored in β |
| Unjustified after an edit | draws as a plain note | dropped | inert, draws nothing |

"Unchanged" on the undo row means `ChartNotesEditPlan` needs no widening at all; "authored" on the
arrival row means the new input or the `ChartPosture` provenance described under F.

**With the fork ruled, the table is no longer a tiebreaker — it is F's cost sheet.** A pays nothing
on any row; F pays four rows and buys β; D pays five and buys nothing A and F do not. The four rows
F pays are now build-plan work items, not arguments.

## Recommendation

**Decided, not conditional: the storage is option F, the fret-optional hold marker.** The fork it
hung on was ruled 2026-08-25 — β stays in scope — so this section states a decision.

The principle it rests on is the first draft's, and it survives: **the datum is a fact about the
fretting hand, and the note stream is a record of sound.** Everything the sound record can say stays
derived — the posture's struck members, the ring-through members, the span extent, the box/bracket
classification. What F adds to that principle is the part D missed: **in α the sound record already
says the fret**, so the authored datum is a *when*, not a *what*, and a fret is authored only in β,
where no note could ever contradict it. That is the narrowest possible retreat from
derived-over-authored, and it is the only version of the retreat the FHP precedent actually
supports.

**Option A is not a rejected alternative — it is F's α half.** A fret-absent marker is precisely the
relational claim A proposed, so every one of A's advantages is inherited: no fret to go stale, a
derivable arrival flip, and degrade-to-plain-note when nothing justifies it. The ruling did not
choose F *over* A; it added the one case A structurally could not carry, and F is the smallest
record that carries both.

**What the ruling cost, stated plainly.** β is the whole reason a second array is worth its edit
surface. Had β been ruled out, A would have delivered the reported case for nothing — no
`ChartNoteKey` widening, no composite undo, no verb re-education, no authored fret anywhere. That
those four rows are now on the bill is not a defect in F; it is the price of the case the user says
is real, and it is the yield-is-a-finding note this document owes: the extra complexity buys exactly
one thing, and it buys nothing else.

**Do not adopt B** (its deletion rationale is unchanged), **D** (superseded by F), or **X** (it
breaks the note invariant and pollutes every note consumer). **E is a producer for F, not a rival.**

**Carry this limit into the ruling, whichever branch wins.** (Half of it was lifted 2026-08-27: a
marker is not a strike but IS a member, so a shape CAN open on held members — two of them, or one
beside a sounding note. What survives is that the note stream alone never opens one.) Neither
option delivers the general broken chord unaided. A posture needs two simultaneous fretting-hand
strikes (rule 10 as written then), and a hold marker is not a strike (see the pre-work list). Both
options cover exactly one shape: **one opened by a ≥2-string strike and completed later.** Side
question (ii) below extends that span across later lone re-picks, but it cannot *open* one either. A
passage picked strictly one string at a time from its first note — never two together — therefore
still derives no span at all and remains parked behind the corpus-informed pass
(`the-project-lifecycle.md:325`). Measured against the user's word *"guarantee"*, that gap belongs
in the ruling, not buried in the survey.

## The derivation side question

Independent of any verb, two things in the walk deserve their own ruling. One of them now has one.

**(i) A posture growing by a new string keeps splitting — RULED 2026-08-25 (user).** Recorded above
under "Rulings already made". The reason is the user's: the partial-shape-then-add-finger gesture is
legitimate and common, identical notes carry both intents, so the default notates the literal notes
and the verb authors the exception. The derivation's split is not a defect; it is the derivation
refusing to assert what it cannot know, and the authored fact is precisely what converts a split
into a growth. Derivation splits; authoring joins.

**(ii) A lone re-pick of a string already in the open posture should not close the span.**
Recommend **change**. Today `:226` closes the span for any single-string onset. But when the lone
onset's string is already a posture member with matching articulation, and at least one other member
is still ringing (presented), the hand demonstrably has not left the shape — and *every fact needed
to know that is already in the stream*. This is derivable, needs no authoring, and it is the true
one-note-at-a-time broken chord over a held shape.

(i) and (ii) compose exactly with the verb. With (ii) in place, a marker authored at the shape's
onset puts the late string **into** the posture, so the late strike is a re-pick of a posture member
and joins the span instead of killing it. Neither is a patch for the other: (ii) fixes what the
stream can prove, the marker supplies what it cannot. Note also what (ii) does **not** solve — a
string that was never in the posture — which is the clean boundary between the derivable and the
authored, and the reason the authored surface stays as small as one marker.

**(ii) is a ruling, not a fix, and it reaches further than extent.** The obvious visible change is a
chord box whose extent now covers a following single-string pick of one of its own members instead
of ending at it. The second-order change is **classification**: lengthening the span moves
`span_end` (`chart_shapes.cpp:292`), which widens the `held_under_right_hand` scan at `:294`–`:300`.
A tap that previously fell *after* a span can now fall *inside* it and flip a box to an arpeggio. So
(ii) can change how an existing chart *reads*, not merely how far its bracket runs.

**(ii) SIGNED by the user 2026-08-26** — and in their reading it was never in question: "repicking
something that is already within the span definitively continues the span unless you pick something
OUTSIDE the span. … I thought that was already understood." The rule operationalizes "within the
span" two ways, both confirmed as the intended reading: a re-pick at a *different stop* is outside
(the hand moved — rule 11's precedent), and the span must still be audibly held (another member's
presented ring is the witness; a lone strike after the shape went silent starts fresh rather than
resurrecting a span across silence). The second condition is **settled-for-now, not fully signed**
(user, same day: "I'm not 100% sure on this one but I think we can settle on this for now and
decisively rule later if it looks off") — it carries a watch item in
`docs/tracking/watch-items.md` whose trigger is a real figure reading as wrongly split at a
re-pick.

**Built 2026-08-26, and the build found it is not optional after all.** This
record calls (i) and (ii) composable with the verb; they are stronger than that. **α is
structurally unreachable without (ii).** Any onset that adds a string to an open span either splits
it (a chord: the articulation vector grows) or closes it (a lone note: the `struck == 1` path), so
before (ii) no note could ever be "inside the span" to supply an absent fret. The marker work alone
therefore ships **β only**; (ii) is what makes the case the user actually reported work, and
without it the α resolution path is unreachable code. That is this record's own "the yield is a
finding" note, arriving from the opposite direction: the extra ruling did not add complexity, it
made half the feature reachable.

Measured on the local corpus (aggregate only, no names): across two packages carrying 419 and 1230
notes and no hold markers at all, (ii) changed exactly **one span's extent, by one beat**,
reclassified nothing and split or merged nothing. **That sample is too thin to quote as safety**
(the review's verdict, accepted): two packages of a ~100-package corpus, neither chosen for
containing the figure (ii) targets, zero markers exercised, and the census rig was deleted, so the
number is not reproducible. The ruling stands on the synthetic evidence and the user's word; the
record may not claim "no existing chart reads differently" until a checked-in `[.local-corpus]`
hidden-tag census (plan 23's shape, `ROCKHERO_CORPUS_DIR`, wanted twice now) runs the full corpus
counting three things — spans whose extent moved, arrivals that flipped, and spans containing a
lone re-pick at all; the third says whether the sample could detect anything.

## Pre-work the build plan must settle — ALL SEVEN ANSWERED BY THE BUILD

Two entries left this list when they were ruled: **the fork** (β stays in scope — the storage
shape is settled) and **the chord** (`N`, re-signed from `Shift+A`). Both are recorded in the rulings section.
Nothing remaining here blocked the storage shape; every item was wiring, and every item was to be
answered before the first line of the build plan was written.

**Each question below keeps its original wording, with the answer the build settled appended.**
Four were answered as this record proposed; item 4 was answered with a third option this record did
not list, and item 3 chose the middle candidate rather than the one weighed first.

1. **Does a hold marker count toward rule 10's two-string threshold?** One struck string plus one
   marker — is that a shape, or a single note beside a held finger? Proposed: **no**, a marker is
   not a strike; a shape still needs two sounding fretting-hand members. The corollary is the limit
   carried into the recommendation: a shape can never open on markers alone, so a marker always
   attaches to a shape opened by sound.
   **ANSWERED as proposed — and OVERTURNED at the first sighting, 2026-08-27.** "Not a strike" was
   right; "therefore not a member" was the error, and the answer as proposed is exactly what made
   converting one member of a two-note chord destroy its own output. The rule now counts MEMBERS,
   sounding or held: see the sighting section at the top of this record. The test named here was
   rewritten with it.
2. **Does a lone re-pick need matching articulation to join (side question (ii))?** A palm-muted
   re-pick of a held chord member: same hand position, different articulation. Rule 11 splits chords
   on articulation; is a lone re-pick the same question or a different one?
   **ANSWERED: yes, the same question** — a member the SOUND states must be re-picked identically,
   per rule 11's precedent and this record's own (ii) wording. A member the chart HOLDS silently
   has no articulation to match, because nothing ever sounded it, so there the authored claim is
   the whole test. That second half is what makes α reachable at all — see the (ii) ruling above.
   **The claim being the whole test means all of it**: a claim that carries a fret states where the
   finger is, so a lone re-pick at a DIFFERENT stop is a different hand and splits, exactly as the
   sound branch splits on a changed articulation. Only a fret-absent claim is tested by string
   alone, because there the re-pick is what supplies the fret. Without that half the authored stop
   would override the note contradicting it — the bracket printing the claim's fret at the span
   start while the note inside it sounds another, which is option D's silent divergence arriving
   through the door slot-scoped disjointness leaves open.
3. **What ends a run of held members?** Three candidates, and the first draft considered only two:
   - **Per onset** — the marker repeats at every strum. Compact to define, but it converts one hand
     fact into **N authored copies that must agree by hand**, and by the derivation's own rule ("a
     strum that drops it splits") a forgotten copy silently changes the notation. That is the
     recurring-defect shape again.
   - **Carry until the span closes** — compact to author, but it introduces a second extent concept
     beside the span, and the span already owns extent.
   - **Extent by succession — the FHP precedent** (`chart.h:789`: "where the hand sits on the neck
     **from this point on**"; `FretHandPosition` is `{position, fret, width}` with **no duration**).
     A held member states itself from its position until the next statement on that string. This
     needs no second duration axis *and* no repetition, and it is the narrow form of option C.
     **This is the candidate the first draft missed**, and it should be weighed first.

   **ANSWERED: span-scoped — the middle candidate, not the FHP one weighed first.** A marker
   contributes to exactly the one span its position falls inside, for that span's whole extent. The
   objection to it ("a second extent concept beside the span") turned out to be backwards: the
   marker introduces no extent of its own at all — the span owns extent and the marker is simply
   inside it or not — whereas extent-by-succession WOULD have added a second axis, one that runs
   per string and past every span boundary. Nothing is authored twice and nothing leaks into a
   later span; succession stays adoptable later without a format change, because this is purely
   read-time. A marker in the gap after the shape stopped ringing is inert: the bracket its fret
   would print under never reaches it.
4. **How does the arrival learn about a β marker?** Carried here from F's cost column, where the
   cost is stated: `chartShapeArrivals` cannot see markers and `ChartPosture` carries no
   provenance, so β needs either a new input to the arrivals function or provenance on the posture
   (which moves the dedup key at `chart_shapes.cpp:199`–`:200`). What is open is only *which*; that
   the flip must happen is settled by the display gate at `tab_paint_core.cpp:2116`. Note this is a
   β-only cost: in α the flip stays derivable from the presented stream.
   **ANSWERED with a third option this record did not list, and it is better than both it did:**
   the derivation PUBLISHES the fact on the span it resolved the markers into
   (`ChartShape::silent_member`). Neither unattractive choice is taken — the arrivals function gets
   no new input and `ChartPosture` gets no provenance, so the dedup key at `:199`–`:200` is
   untouched. One authority states its own result; a second marker scan beside the arrival would
   have been the rule stated twice and free to disagree. (The cheaper inference "a posture string
   neither struck nor presented-ringing at the start must have come from a marker" was considered
   and rejected: it is *almost* true, but a dead note whose stored ring folds through would falsely
   flip existing marker-free charts.)
5. **Editor visibility and selection.** An authored marker must be visible and selectable in the
   editor's own lane even where the display rule would not print it (outside an arpeggio bracket, or
   before the span resolves), or it becomes invisible state — but selectability is what forces the
   `ChartNoteKey` widening scored above. Is the charting-mark law the right home (editor-only mark,
   merged surfaces elsewhere), as it is for the `LeftTap` light-T?
   **ANSWERED: yes, the charting-mark law.** The mark is an editor-only 2D overlay; every other
   surface shows the marker's EFFECT through the posture. The widening landed as
   `ChartNoteKey` → `ChartSlotKey` plus a `ChartSelectableKind`, so the selection unit is
   `(kind, slot)` and growth is an enumerator plus one arm — **for a selectable a SLOT names.**
   The record originally added "which is what the unified waypoint model reuses"; that promise is
   withdrawn as overstated. A note's waypoints are many per note and identified by
   `(slot, offset)`, so `(kind, slot)` cannot name two of them, and every mutation plus each
   `slotsFor` arm is written over `std::vector<ChartSlotKey>`. The waypoint model inherits the kind
   axis and the per-kind operand shape, not a free extension point — it will widen the key itself.
6. **What is the undo entry?** The convert case removes a note **and** inserts a marker in one
   gesture, so a single entry spans both arrays and fits no existing `IEdit`
   (`ChartNotesEdit` carries only `ChartNotesEditPlan`, which carries only
   `std::vector<ChartNote>`). Composite edit, or a widened plan carrying both arrays? The proposed
   verb makes this unavoidable rather than hypothetical: authoring an empty slot and removing a
   marker touch the marker array alone, but **convert** and its restoring toggle cross both.
   **ANSWERED: the widened plan, not a composite.** `ChartEditPlan` carries one
   `ChartArrayChange` per authored array and one `reversed()`, and the apply rebuilds both arrays
   on copies before swapping either in, so a failed precondition anywhere leaves the chart wholly
   untouched. The reason is the one this record keeps firing on elsewhere: a composite would need
   an order between its halves, and an order is a rule two sides must agree on by hand.
7. **Who writes the first one?** Hand-authoring only, or does the external converter emit markers
   from its source format's fuller-than-struck chord templates on day one (§2's unverified field)?
   If the converter emits them, the format lands before the verb does.
   **ANSWERED: hand-authoring only.** The verb shipped and no producer writes a marker; the
   converter's §2 field is still unverified. Nothing in the format or the reader presumes a
   generator, so a converter that later emits markers needs no format change.

## Grounding index

**Every line number below is from 2026-08-24, BEFORE the build, and the build moved most of them.**
They are kept because they ground the record's argument in the code as it stood when the decision
was made; they are not a map of the current tree. Four names in the editor entries were renamed by
the build and the old spellings no longer exist anywhere: `ChartNoteKey` → `ChartSlotKey` (paired
with `ChartSelectableKind` into `ChartSelectionKey`), `ChartNotesEditPlan` → `ChartEditPlan`,
`ChartNotesEdit` → `ChartEdit`, `applyChartNotesChange` → `applyChartChange`, `planMoveNotes` →
`planMoveSelection`, and `planDeleteNotes` → `planDeleteSelection`.

- `rock-hero-common/core/src/chart/chart_shapes.cpp` — `:31` articulation key, `:102` `close_span`,
  `:167` tap transparency, `:175` chord branch, `:178`–`:187` ring-through (stored ring, most recent
  note per string), `:199`–`:200` posture dedup key, `:205` span merge, `:212` split, `:226`
  lone-onset close, `:247` arrivals, `:282`/`:289`/`:307` the three arpeggio triggers, `:292`
  `span_end`, `:294`–`:300` the right-hand scan window (ii) widens.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_shapes.h` — `:26` postures are
  derived and carry no name/fingering, `:141` the "merely silent" compromise.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart.h` — `:86` the legato claim
  precedent, `:298` `rightHandOnset`, `:418`–`:431` sustain is MIDI truth and strictly positive
  (`:427` the zero-ring invariant), `:789`–`:799` `FretHandPosition` and its extent-by-succession
  shape.
- `rock-hero-common/core/include/rock_hero/common/core/chart/chart_view_state.h:306` — the posture
  contract the derivation cannot currently satisfy (the field itself is `:310`).
- `rock-hero-common/core/src/chart/chart_projection.cpp:202`–`:231` — every non-null posture fret is
  pushed into `ShapeViewState::strings`; a silent posture string already reaches the view.
- `rock-hero-common/ui/src/tab/tab_paint_core.cpp:2116` — `if (!shape.arpeggio) continue;`, the gate
  the arrival flip must pass; `:2158`–`:2162` — the documented silent-string default.
- `rock-hero-common/core/src/chart/chart_document.cpp:471`–`:476` — stage C's deletion rationale;
  `:477`–`:482` the loud-refusal tripwire; `:511` the `fhps` channel shape.
- `rock-hero-editor/core/src/chart/chart_selection.h` — `:20`/`:23` `ChartNoteKey` and its
  uniqueness warrant, `:54` the linear-merge invariant, `:66`/`:72`/`:88`/`:95` the growth verbs.
- `rock-hero-editor/core/src/chart/chart_edits.h` — `:33` `ChartNotesEditPlan`, `:108`/`:121`
  `planMoveNotes` and its occupied-slot refusal, `:154` `planRetypeFrets`, `:554`
  `applyChartNotesChange`, `:565` `ChartNotesEdit`.
- `rock-hero-editor/core/src/project/gp_chart_builder.cpp:485`–`:560` — the tremolo spell-out, the
  precedent for synthesising staggered onsets; `:987`/`:2218` the FHP generator.
- `docs/developer/the-project-lifecycle.md:273`–`:333` — rules 10 to 12a, the maintained spec
  (`:325` the parked broken-chord grouping).
- `docs/plans/in-progress/chart-span-and-selection-model.md` — `:31`–`:42` §2 classification and the
  importer obligation, `:189`–`:195` §8 arpeggio conversion (the prior ruling).
- `docs/plans/in-progress/arpeggio-posture-display-options.md:18`–`:25` — the settled display rule
  that already handles an un-sounding posture string.
- `docs/plans/in-progress/keymap-matrix.md:244`/`:246`/`:247` — the `A` emphasis-cycle plan of
  record, the arpeggio hold re-signed to `N`, and the restored heavy-accent reservation.
