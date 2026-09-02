# Span Markers — Sound Founds, Claims Attach, Markers Define

Status: **DESIGN RECORD, not scheduled.** Opened 2026-08-31 out of the review-blocker walk, for a
dedicated future session. Nothing here is built; nothing here is signed as law. What IS signed is
the founding principle below, as the redesign's PREMISE — the thing a future session starts from
rather than re-argues. The shipped model is unchanged and stays described by
`docs/developer/the-project-lifecycle.md` and `docs/plans/in-progress/chart-ruleset.md`.

## The founding principle

> **Sound founds. Claims attach. Markers define.**

Three clauses, and each one takes something away from the model as it stands:

- **Sound founds.** A statement comes into existence only by something SOUNDING. Today a pair of
  silently-held fingers opens a span by itself — two claims at a slot meet the member threshold —
  so a span can exist that nothing ever played.
- **Claims attach.** A claim can JOIN a standing statement, JUSTIFY it, and COUNT toward it, but it
  can never CONSTITUTE one. It is a fact about where a finger is, hung on a statement made by
  sound; it is not itself the statement.
- **Markers define.** Deliberate span authoring stops being a shape conjured out of silent holds
  and becomes an explicit MARKER record — a span the charter drew, saying so in the file, rather
  than a span the derivation inferred from records that state something else.

The reason this is worth a session rather than a patch: nearly every awkward rule in the current
derivation is downstream of claims being able to found. The one-count opening law has to count
three kinds of member because claims are one of them; the inert-claim sweep exists because a claim
can state a span into being and then be left stating nothing; and the display law has to answer
"where does a claim's face go" separately for a claim that founded a span and one that joined it.

## What it deletes

Named at the walk, and listed here as the deletion inventory a build would work through. Each is a
consequence of the principle, not an independent wish:

1. **`NoteAttack::None`** leaves the attack enum. A note that does not sound is not a note; the
   grip it was standing in for is stated by a marker's template instead. With it goes the
   attack-conditional `sustain` rule (`sustain` strictly positive on every attack that sounds,
   exactly zero on the one that does not) and the "is this note a sound?" predicate every consumer
   currently asks.
2. **The silent-hold claims machinery** — the claim arm of the opening law, claim-founded spans,
   the claim's own dating rule, and the growth split that reads claims alone.
3. **`sweepInertClaimedStops`.** Markers are never inert: a marker states a span because the
   charter drew it, so there is no such thing as one that reached nothing and has to be removed on
   load. The sweep's REMAINING scope — the stored `held` fields on right-hand onsets — is a
   separate question, taken up under "Sequencing" below.
4. **The E25 muted-tail residue.** A dead note stores a ring nobody hears and still classifies and
   carries as a member at a statement boundary (`chart_shapes.cpp`, the fold-in that reads the
   STORED stream). Under "sound founds" that participation is residue. Exactly what falls out —
   the stored ring itself, or only its membership in a founding — is the first thing a build here
   has to settle, and it is NOT settled by this record.
5. **The `N` verb's fake-note coaxing.** Today stating "a finger is on fret 5 of the A string"
   means authoring a note there and converting it, because note insertion is the editor's only way
   to say a fret — the empty-slot case even plants a fret-0 hold purely so the charter has
   something to type a digit into. A marker with a template states the grip directly, so the
   coaxing has nothing left to do.
6. **The POSTURE GAP** (2026-08-31, ruled frozen under the N-verb stop-loss): a claim that
   JUSTIFIED a span reaches it in the ledger but its fret never joins the span's posture, so no
   bracket digit prints for it. The affected population is exclusively silent-hold-founded
   spans, which this deletion removes — the gap dies with them, unfixed by design.
7. **The residual Blocker-3 defect class.** Blocker 3 was a claim that justified a span while the
   ledger recorded it as reaching nothing; the fix round publishes the answered claim's reach so
   the bookkeeping closes. The whole CLASS — a span existing by claims alone, and therefore a claim
   whose reach has to be tracked at all — is unrepresentable once no span exists by claims alone.

## The template

The marker DEFINES the span. The span's TEMPLATE states the grip — **including stops nothing
sounds.** That last clause is why the template exists at all rather than the marker being a bare
bracket: hold an A minor shape and play three of its strings, and two of the stops are real
statements about the hand that no sounding record carries. Today a silent hold carries them; once
`NoteAttack::None` is gone, the template is the only place they can live, and a model without one
would simply be unable to write that figure down.

Save-file shape (user, 2026-08-31 — format minimalism ruled): **a marker stores its location
and nothing else.**

```json
{ "position": <grid position> }
```

One record, one field, one semantic: a marker is a FORCED STATEMENT BOUNDARY at its location —
any standing span closes there, the front of whatever follows anchors there (no backdating across
a marker), and the ordinary opening law decides everything else. The earlier `kind` enum dissolved
because that one semantic covers both defining and splitting; the earlier inline `template` field
is deliberately NOT stored, under the rule this ruling sets for the whole redesign: **no field
enters the save file before its writer exists.** The template stops' only writer is the template
editor, which is queued work — the field lands with it, in place, when it does.

THE NAMED SEQUENCING COST: until the template editor lands, the unsounded-stop statement has no
home — deleting `NoteAttack::None` removes today's only way to author the hold-the-silent-string
figure, and a location-only marker cannot state it. Either the `None` deletion ships together
with the template editor, or the figure lapses in the gap. Decide this deliberately in the
dedicated session; do not let it be discovered by a charter.

A premise worth stating because it was asked (user, 2026-08-31): **no template storage exists
today, anywhere.** A derived span references nothing — its grip is computed from the notes on
every read, and the word "template" in current code prose names that derived posture. So a
location-only marker leaves nothing dangling; the reference question only exists once templates
become storable.

Open, and deliberately not answered here: whether the template is a full grip or only the stops
nothing sounds (the derived remainder being derivable either way); whether a marker can state a
span END or only its start; how a template interacts with a member whose fret the notation
already derives; and — when the template editor makes templates storable — whether they attach
INLINE on the marker (simplest, no reuse) or as REFERENCED first-class records (the tone-catalog
UUID pattern: authored once, referenced by many spans). RULED FURTHER (user, same day): they take the chord
dictionary's shape and FOLD INTO IT as one entity — one grip library, referenced by markers,
browsed by the dictionary (`docs/plans/todo/chord-dictionary.md`), matched against derived spans.
Two separate stores that both know what an A-minor grip is would be the two-authorities defect at
design scale.

**How a template reaches a span that has no save-file footprint** (user question, answered by the
model's own standing pattern): the arrow points the other way. Authored records anchor to the
TIMELINE; derivation picks them up by position — exactly how a claim resolves to whatever span
its beat falls inside, how tone regions attach by GridPosition, and how the marker itself works.
A derived span needs no identity because identity-by-position IS its identity. The reference
rides the marker; the span whose front the marker forces wears it; edits reshape the span but the
marker keeps forcing its boundary, so the reference follows by construction. Worst case is a
reference standing over silence: inert but visible and deletable via the marker's tell — never a
dangling id, no reference lifecycle, no GC.

**The resolution law** (REVISED same day — the user killed the auto-match): a template is REAL
AUTHORED INFORMATION, because the grip UNDERDETERMINES it — the same fret set is fingered
differently in different contexts and can carry different names, so a fret-keyed dictionary match
at read would be authoring a guess. The standing law already says so: "only relational choices
are authored," and which fingering/name a context takes is precisely a relational choice (it is
also why stage C deleted the old posture name/fingers fields — authored data with no proper home
until this redesign). So: `resolvedTemplate(span)` = the authored reference on the marker at its
front if present; otherwise NOTHING — the anonymous derived grip, digits only, no name, no
fingering, no guess. Derivation never assigns a template. The dictionary's grip-matching survives
in exactly one place: AUTHORING-TIME SUGGESTION — the picker sorts entries matching the derived
grip to the top, a filter for a human choosing, never an assignment. Ordinary spans persist
nothing; authored references exist only for the relational choices: this span wears THAT named
grip, or states stops nothing sounds.

**The invalidation law** (user, same day): editing notes inside a marked span can contradict the
referenced template, and the coherent behaviour is neither refusal nor silent deletion. A note
edit is NEVER refused for the template's sake (notes are primary; enrichment never blocks
charting), and the reference is NEVER silently deleted (a fret edit is ambiguous — the charter
may be fixing a typo TOWARD the template; contrast held-clearing, where authoring the pull-off
was itself a statement about the held). Instead, at the moment of contradiction the ENRICHMENT
STOPS DRAWING — name and fingering vanish, the surface falls back to the honest anonymous digits,
no contradicted name ever stands — and the marker's tell enters a CONTRADICTED state: the
charting-surface FLAG, resolved by the charter re-picking or deleting the reference. Undoing the
edit restores harmony by itself, because the reference was untouched. CONTRADICTION DEFINED: any
SOUNDED stop in the span differing from the template's stated fret on that string, including a
sounded string the template does not state; the played strings being a SUBSET of the template is
not a contradiction — it is the point of templates.

## The template editor (queued work)

The home of **span-wide fret editing**, which the 2026-08-31 fix round removed from the tab lane
and which is queued here rather than dropped. The reason it could not live where it was tried: at a
bracket digit, typing a number ALREADY means INSERT A NOTE at the caret, so a span-wide write-
through has to steal that keystroke, and every scrap of dual-scope machinery in that build existed
only to decide which of the two a press had meant. In a template editor a span's grip is edited AS
a grip, nothing competes for the digits, and the collision cannot arise.

Also its natural home: editing tap-held values in bulk, if that is ever wanted.

The satellite law the fix round left standing is the simple one, and it should survive this
redesign unchanged: **satellites are note-scoped, always, everywhere** — a satellite is its note's
held face, full stop, with the selection handle (a selected note's satellite is hit-tested as part
of that selection) as its one refinement.

## Markers are visible and deletable (user, 2026-08-31)

**Every authored marker draws a visual tell at its location; derived boundaries draw nothing.**
The principle is the snap-off strike rule generalized: authored state that changes behavior must
be legible on the surface. An invisible marker is a trap twice over — the charter cannot delete a
record they cannot see, and the derivation starts looking defective, because a span that splits at
an invisible marker reads as a law bug rather than an authored choice. A derived boundary needs no
mark for the same reason inverted: the notes around it explain it, and the only way to move it is
to move the notes.

Consequences to design in the dedicated session:

- **Editor 2D only.** The tell is a charting affordance, like LeftTap's light-T mark; the highway
  draws the derived result (brackets, boxes), never authoring metadata, so the surfaces do not
  diverge on musical content.
- **The selection face is already free**: deleting `NoteAttack::None` frees the "selecting this
  highlights the brackets" language, and the marker inherits it — click the tell, the furniture it
  defines or splits lights up, Delete removes the record, derivation reflows, undo restores it.
- **Absence is the derived tell**: mark present = authored and deletable; no mark = derived, and
  not directly deletable by construction.

## Tap harmonics — a named open area

Unruled, and named so the redesign does not quietly assume the ordinary tap's answer covers it.

A tap harmonic's HELD fret is **pitch-critical**: the stopped length is what the sound IS, not
merely where the other hand happens to be. So its presentation needs are stronger than an ordinary
tap's, and two questions are open:

- **Do tap harmonics always warrant brackets?** Under the FINAL satellite law (below), an
  AUTHORED pitch-critical stop already stands everywhere and a DERIVED one is revealed on the
  truth channel; the open question is whether a tap harmonic's stop should additionally warrant a
  mark whatever its position in the span.
- **The per-note pitch gap.** A DERIVED held stop does not currently retune a tapped harmonic's
  node, because per-note resolution cannot see the neighbour that states the fret. The resolution
  is a fact about a note's successor; the node is computed per note. Nothing bridges them today.

Related record: `docs/plans/todo/tap-harmonic-display.md`.

## The execution phases (user, 2026-08-31 — the plan takes shape)

**PHASE 1 — the marker lands, N retires** (its own seam, after the accumulation seam commits):

1. **Shift+S** places a span marker at the caret (revised from the first Alt+S proposal after a
   registry check: plain `s` is TAKEN by Toggle Slap, the map's signed Shift-plane law makes
   Shift+S "the S letter's second claimant" — exactly the T/Shift+T and L/Shift+L idiom — and
   the map has NO Alt+letter chord at all; Alt is the gestural plane (arrows, click, wheel), so
   Alt+S would open a new plane for one verb. Runner-up considered: Shift+Insert beside
   NeutralInsert — rejected for losing the mnemonic). RULED: Shift+S on a slot that already
   holds a marker SELECTS it — place, select, Delete is a keyboard-only round trip; no removal
   verb and no pointer needed. The build records the claim in keymap-matrix.md, the map of
   record.
2. Save-file record: grid position only, under the key **`"span"`** (user ruled, overriding the
   `span_markers` suggestion): the record is a manually SPECIFIED span — the charter's own, as
   opposed to the derived ones that are never stored — and the short name says exactly that.
3. A marker placed where a derived span already opens PINS that span (the forced-boundary
   semantic already covers it).
4. Deleting a marker reflows the derivation automatically (spans are read-time derived).
5. **`NoteAttack::None` RIPS OUT ENTIRELY IN PHASE 1** (user ruled — no half-state, no surviving
   reader machinery): the N verb, the attack value, the silent-hold claims arm, and the sweep's
   note half all leave together, ACCEPTING the named gap — a span with a bracket that never
   sounds is unauthorable until Phase 2's templates land. Build decision for Phase 1: what a
   loaded file's existing `None` records do — the population is authored-only (imports write
   none), so the curve_shape precedent applies: drop on load WITH A LOAD NOTICE naming the marker
   system as the replacement, never silently.

Also RIDING PHASE 1's seam (user, 2026-09-01):

- **Publish the OPENING SLOT on `ChartShape`** (Q-D): one derived `GridPosition`, written where
  `open_span_here` already knows it before computing the front. Makes the strike-less census row
  exact instead of a floor; the census row and its comment upgrade in the same change.
- **The clip-pass simplification** (Q-B): replace `clipLetRingExtensions`' `deriveChartShapes`
  call + `event_fronts` with a pure note-stream test — a contradicting fretting-hand onset over a
  still-ringing member founds an event span by construction, so the derived fronts should be
  provably redundant. PROOF OBLIGATION before the deletion ships: the full let-ring fixture
  suite green AND a corpus census diff of ZERO on every let-ring counter; any divergence means
  the derivation was load-bearing — analyze and report instead of deleting. (Briefly CLOSED
  2026-09-01 while the A2 gate stood — A2 read the founded span's own extent, which no
  note-stream-local test can see — and REOPENED the same day by A2's revert.)
- **The Q-A rider** (the simultaneous-start figure): **CLOSED FOR NOW 2026-09-01 — by the A2
  REVERT plus a watch item, not by a law.** A2 — a foreign statement contradicts only when the
  span FOUNDED at it STATES two or more distinct strings across its own extent — was signed,
  built, and REVERTED the same day as unvalidated: acquitted of the sighted spill it was
  suspected of (stored rings and spans in the sighted window are identical with it on or off;
  the spill was an intermediate display state), it protected a population — walking-melody
  drones under co-struck textures — never sighted as a defect on real material. The committed
  contradiction law stands; the discontinuity this rider names is recorded as a watch item
  (docs/tracking/watch-items.md: the clip cuts let-ring drones under co-struck walking
  melodies), whose trigger is a real sighting of a wrongly-clipped drone on corpus material and
  whose remedy menu is pre-measured in the chart ruleset's A2 entry. THE CANDIDATE RECORD BELOW
  STANDS — when the trigger fires, start from it, never from scratch.
  The "sounding-stop" narrowing
  (contradiction counts only where it re-frets a still-sounding grip stop) was UNSATISFIABLE BY
  CONSTRUCTION — the grip entry and the contradicting statement always share a string, and the
  same-string clamp has already ended every ring at exactly that re-strike, so the predicate
  clipped 0 of 2,321 rings and would have silently repealed the whole clip (~5,254 beats back to
  ringing, 90.1% landing on the clamp, not even the region end); do not re-derive it. Plain
  STATEMENT MULTIPLICITY — only a multi-string onset GROUP states a new grip — died on the
  arpeggiated new shape, which states one string at a time and would have stopped clipping;
  A2 is that candidate corrected to read the span's whole extent rather than its opening slot,
  which is what lets a broken chord still clip while a walking melody does not. Measured at:
  2,321 -> 1,521 clips, 800 walking-melody rings spared (~1,763 beats), 1,262 clips keeping their
  exact instant, 259 landing at a later qualifying front, and the motivating figure identical to
  the beat.

**PHASE 2 — templates** (after Phase 1):

1. Template save format (`"template"` proposed) + the template editor UI.
2. Spans reference templates via markers; chord and arpeggio spans share ONE template list.
3. Save WARNS on template-less spans (save allowed, EXPORT blocked until resolved), plus a
   standing unresolved-span count in the editor chrome so the debt is visible before save.
4. The save-walk resolver: left to right, stop at each unassigned span, offer grip-matching
   templates first (the authoring-time-suggestion ruling) or define a new one; the choice authors
   a marker referencing the template at the span's location.
5. A note edit that CHANGES THE GRIP removes the template reference (the marker survives; only
   the reference clears; composite undo restores it). NOTE: this supersedes the same-day
   invalidation law above (contradicted-tell) — the save-walk recovery net is what makes removal
   the cleaner rule — pending the user's final confirming word, flagged at the walk.
6. OPEN: purge-unused-templates-on-save collides with templates folding into the chord
   dictionary (a library's value includes unused entries). Candidates: purge project-local only;
   never auto-purge, report unused at export; purge at export. Decide in the session.
7. `NoteAttack::None` leaves the format; the remaining deletion inventory completes.

**THE SATELLITE REVEAL LAW, FINAL** (user, same day — supersedes the position-based law above,
and is MANDATORY in the accumulation seam, not deferred): a satellite is ALWAYS STANDING where
its held is AUTHORED (everywhere — no front/mid-span distinction) and where a tap fronts a
bracket; a DERIVED held's satellite is REVEALED on the note's truth channel — the same
selection/tail reveal that shows the note's actual ring — so revealing a note shows the whole
truth about it at once. Visibility keys on AUTHORSHIP plus one existing reveal channel.

## Sequencing and notes

- **The sweep's remaining scope.** After `sweepInertClaimedStops` goes, what remains is the stored
  `held` field on right-hand onsets. Re-examine it here: with claims no longer founding, a held
  stop that reaches nothing may simply be a stop the hand was on, which is not obviously an error
  to sweep at all.
- **The `None` population is authored-only.** The GP importer authors ZERO claims — it never emits
  `NoteAttack::None` and never writes a `held` field — so every record the deletion touches was
  typed by a charter in this editor. That bounds the migration question to the local corpus.
- **Format changes in place.** No migration path, no version bump — the format changes and packages
  are re-imported, as every format change here has worked.
- **Relation to W10's split-tail law.** A SPLIT marker and `Shift+L`'s disconnect are cousins: both
  say "the thing that was one is two from here". Whether they are one verb wearing two faces is
  worth asking when W10 is built (`docs/plans/in-progress/technique-review-walkthrough.md`, W10).
- **The halfway step already exists.** `docs/plans/todo/arpeggio-authoring.md`'s **storage F** —
  the fret-optional hold MARKER — was exactly this shape, shipped 2026-08-26 and replaced the next
  day by option X (the silent member inside the note stream). The survey that killed F and the
  sighting that replaced it are both in that record, and a session starting here should read them
  first: this plan is F's idea returning with a template and a founding principle behind it.

## The settlement rider — both correction polarities (user, 2026-09-01)

The provisional three-member accumulation minimum (chart-ruleset.md's dated entry; the F6
sighting rig) SETTLES at three only if this plan delivers BOTH correction polarities, so the
minimum becomes a default guess with an escape in each direction rather than a correctness rule:

1. **The explicit two-note span.** Free by construction: the minimum lives in the DERIVATION's
   opening law and governs what sound may found implicitly; a marker never passes through the
   opening law — it defines. A Shift+S span with two members is legal with no new machinery,
   floored at two (one stop is a note; two held at once is a grip; a one-note bracket states
   nothing).

2. **Explicit deletion of a derived span, stored as a SPAN-FREE ZONE.** Derived spans have no
   identity — they are recomputed from the notes on every read — so any durable deletion record
   must anchor to LOCATION, not to a derivation artifact: a slot-anchored suppression dangles the
   moment an edit moves the founding, and the span pops back. The zone replaces identity with
   geography, the language markers already speak. The GESTURE is "select the derived bracket,
   Delete"; the RECORD the editor authors is a free zone over exactly that span's musical extent
   (the close, not the margin-trimmed rails, so no sliver refounds). Zones are visible,
   selectable, and deletable like any marker; deleting the zone resumes derivation.

   Recalculation is not a question to answer but one that dissolves: the zone is ONE MORE CLOSE
   CAUSE — spans accumulate to its edge and close there, nothing founds inside it, everything
   outside recalculates by the ordinary laws. The zone also PINS the deleted extent, so
   neighbours cannot creep in and re-derive a shifted variant of the span the user rejected —
   the exact failure a bare suppression record invites.

   Pins: (a) zones block ACCUMULATION-founded openings only — Statement openings (strums, chord
   boxes, dyads) are immune, on the same founding discriminator the sighting rig uses; (b) a
   span marker inside a free zone is REFUSED at authoring (delete or trim the zone first) — two
   contradictory statements about one stretch stay unrepresentable; (c) zones touch span
   furniture only — stored rings, the cut law, and tail presentation are untouched, so a
   de-spanned wash draws its ordinary tails, matching the minimum's own two-note behaviour.

   Open for the build: one record kind with polarity vs. two kinds (naming question); the delete
   gesture's undo shape (one entry restoring the zone-less derivation).

This rider also gives the manual escape for segmentation figures the derivation guesses wrong
(the m11 sweep-boundary case in the ruleset's segmentation record): statement beats a cleverer
automatic walk.
