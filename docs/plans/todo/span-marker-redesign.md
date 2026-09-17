# Span Markers — Sound Founds, Claims Attach, Markers Define

Status: **DESIGN RECORD; its roadmap seat is `docs/plans/roadmap/60-hand-markers.md`** (the former
plan 61 seat merged into plan 60 on 2026-09-15 — Phases 3, 4 and 5 there execute this record).
Opened 2026-08-31 out of the review-blocker walk, for a
dedicated future session. Nothing here is built; nothing here is signed as law. What IS signed is
the founding principle below, as the redesign's PREMISE — the thing a future session starts from
rather than re-argues. The shipped model is unchanged and stays described by
`docs/developer/the-project-lifecycle.md` and `docs/plans/in-progress/chart-ruleset.md`.

## The founding principle

> **Sound founds. Claims attach. Markers define.**

Three clauses, and each one takes something away from the model as it stands:

- **Sound founds.** A statement comes into existence only by something SOUNDING. Today a pair of
  claimed stops opens a span by itself — two claims at a slot meet the member threshold —
  so a span can exist that nothing ever played.
- **Claims attach.** A claim can JOIN a standing statement and COUNT toward it, but it should never
  CONSTITUTE one. It is a fact about where a finger is, hung on a statement made by
  sound; it is not itself the statement.
- **Markers define.** Deliberate span authoring stops being a shape conjured out of claims
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

1. **`NoteAttack::None`** leaves the attack enum — **DONE 2026-09-17**, out of phase and ahead of
   the markers. A note that does not sound is not a note; the
   grip it was standing in for waits on a marker's template. With it went the
   attack-conditional `sustain` rule — `sustain` is now strictly positive on EVERY note — and the
   "is this note a sound?" predicate every consumer asked. `N` is unbound and free for
   reuse; no load notice was built, the user having ruled out legacy and back-compat code, so
   an old `"attack": "none"` fails to read like any unknown attack token.
2. **The silent-hold SHAPE of a claim, and LAW II's justification half with it.** What LEFT
   (2026-09-17): the second shape itself, and then — as dead code, once every claim rode a carrier
   that sounds its stop — the justification test and the dissolution it fed. Named exactly: the
   per-slot sounded-stop column and `answersClaim`, `OpenSpan::justified` / `justified_by`, the
   dissolve of an unjustified hand-alone span, the later-slot justification loop,
   `replaces_unjustified` and the same-slot justify block. What STAYED: the carry fold's
   "stated otherwise" skip, which is live — a carried ring ends where the same string states a
   different stop; the claim arm of the opening law, claim-founded spans (`silent_only`), the
   claim's own dating rule, the growth split, posture membership (`silent_member`) and the
   published reach (`ChartShapes::claim_shapes`) — `claimed_stops[]` still carries `held` under taps
   and scrapes, so a claim still founds, still dates and still grows a span. A claim-only span now
   publishes AT ONCE with zero sustain instead of waiting to be justified.
3. **`sweepInertClaimedStops`.** Its NOTE half is gone with `None` (DONE 2026-09-17), along with
   `ChartRepair::InertSilentHold`. The held-field half — `ChartRepair::InertHeldStop`, the function
   name and its one-pass justification — was kept UNCHANGED. Markers are never inert: a marker
   states a span because the
   charter drew it, so there is no such thing as one that reached nothing and has to be removed on
   load. Whether the sweep's REMAINING scope — the stored `held` fields on right-hand onsets —
   survives the marker redesign is **still OPEN**, taken up under "Sequencing" below.
4. **The E25 muted-tail residue.** A dead note stores a ring nobody hears and still classifies and
   carries as a member at a statement boundary (`chart_shapes.cpp`, the fold-in that reads the
   STORED stream). Under "sound founds" that participation is residue. Exactly what falls out —
   the stored ring itself, or only its membership in a founding — is the first thing a build here
   has to settle, and it is NOT settled by this record.
5. **The `N` verb's fake-note coaxing** — **DONE 2026-09-17** with the verb. Stating "a finger is on
   fret 5 of the A string" no longer means authoring a note there and converting it. Where a
   right-hand onset sounds the string, the charter clicks or arrow-steps onto that note's drawn
   satellite and types the fret; where NOTHING sounds it, the figure waits on a marker's template.
6. **The POSTURE GAP** (2026-08-31, ruled frozen under the N-verb stop-loss): a claim reaches a span
   in the ledger while its fret never joins that span's posture, so no bracket digit prints for it.
   Its justification-driven form left with the justification half on 2026-09-17 — a claim now
   reaches only a span it is a member of. What remains is the narrower case of a claimed string the
   posture already fills with a DIFFERENT stop, where the claim's face and the bracket disagree
   (logged in `docs/tracking/backlog.md`); what removes it is markers founding instead.
7. **The residual Blocker-3 defect class.** Blocker 3 was a claim that justified a span while the
   ledger recorded it as reaching nothing; the fix round published the answered claim's reach, and
   the justification half it belonged to is itself gone as of 2026-09-17. The whole CLASS — a span
   existing by claims alone, and therefore a claim
   whose reach has to be tracked at all — is unrepresentable once no span exists by claims alone.

## The template

The marker DEFINES the span. The span's TEMPLATE states the grip — **including stops nothing
sounds.** That last clause is why the template exists at all rather than the marker being a bare
bracket: hold an A minor shape and play three of its strings, and two of the stops are real
statements about the hand that no sounding record carries. Since `NoteAttack::None` left
(2026-09-17) the template is the only place they can live, and a model without one is unable to
write that figure down.

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

THE NAMED SEQUENCING COST — DECIDED 2026-09-17: the figure LAPSES in the gap, and the user
ACCEPTED that. `NoteAttack::None` shipped out ahead of the template editor, so until that editor
lands there is no way to state a fretting-hand stop on a string nothing sounds, and a location-only
marker cannot state it either. Brackets are wholly derived in the meantime.

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

## The template's content, the picker, and the FHP coupling (2026-09-05, fingering design conversation)

The FHP-push endgame (see `docs/plans/in-progress/fhp-derivation-algorithm.md`) walked this
design again from the fingering side and landed on the standing model with these sharpenings:

- **Entry content**: `{name, fingering, stops}` with fingering keyed by string. **Stacked
  fingerings are first-class** — distinct fingers on one fret across strings are common (the
  standard power chord is 1-3-4 with ring and pinky stacked), so fingering is never derivable
  from the window alone; a barre is one finger at one fret across strings. Within a span each
  string carries exactly one stop (the grip law), so string-keyed fingering duplicates nothing
  the notes own; the entry's FRET statements are authoritative only for unsounded stops, and the
  invalidation law above already defines what a sounded contradiction does.
- **Reference model reaffirmed** against a stamp-copy alternative weighed in conversation: the
  stamp gave per-occurrence choice but forfeited shared identity (renaming back to a fifty-edit
  change). References give both — choice is WHICH entry the marker references (the same grip
  legitimately wears different names by harmonic context), identity is THE entry.
- **Scope (user, 2026-09-05)**: the dictionary stays song-scoped, keeping charts self-contained.
  A future cross-chart GLOBAL library is an authoring palette whose entries COPY INTO the song
  dictionary on first use (identical-definition collapse applies); renames ripple within a song,
  never across songs; nothing in the format ever references the global library.
- **The picker's ranking cascade (user, 2026-09-05)**: entries this chart already references for
  the same grip first, then (future) the user's global usage, then a shipped commonness rank —
  which can be MEASURED from ground-truth corpus hand-shape aggregates rather than hand-authored.
- **REVISION FLAGGED — needs the user's explicit re-confirmation, because it reverses the killed
  auto-match above**: the user proposed (2026-09-05) a DERIVED DEFAULT — an unreferenced span
  displays the cascade's top-ranked candidate in the editor's derived styling (the visual grammar
  of the derived tone-lane baseline anchor), never persisted, never claiming authorship;
  referencing or clearing remains the authored act. The killed auto-match's rationale ("a match
  at read is authoring a guess") is answered by the styling: the guess is visibly a guess, and
  the cascade's top layer makes the common case the charter's own in-song vocabulary rather than
  a guess at all. If re-confirmed, the resolution law's "otherwise NOTHING" becomes "otherwise
  the derived-styled suggestion, which states nothing."
- **The FHP coupling**: a marked span's resolved template feeds FHP derivation — the fingering
  pins the anchor (`anchor = floor − (floor's finger − 1)`; a barre pins at the barre fret), and
  unsounded stated stops JOIN the run's coverage demand (the window covers the whole grip, not
  just the sounded members). A CONTRADICTED reference stops feeding, exactly as it stops
  drawing — FHP derivation falls back to notes-only. The arrow stays acyclic: markers and
  entries are authored at positions; FHP derivation reads them; span derivation reads FHPs.

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
- **The selection face is already free**: `NoteAttack::None` left on 2026-09-17, so the "selecting
  this highlights the brackets" language is unclaimed and the marker inherits it — click the tell,
  the furniture it defines or splits lights up, Delete removes the record, derivation reflows, undo restores it.
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

   **Superseded 2026-09-12, re-ruled 2026-09-14:** the marker grammar reserves `Ctrl+H` for the
   span marker (what the hand holds) and `Ctrl+P` for the position marker. Both read the cursor
   (the armed caret, else the paused cursor) and never the selection — a marker of that kind
   standing exactly there is restated, otherwise one is inserted there — and both are inert while
   playing and with no song; `Ctrl+Shift+H` and `Ctrl+Shift+P` select, and `Enter` and `Ctrl+R`
   are the selection verbs. `Shift+S` is withdrawn. The premise that the map has no `Alt`+letter
   chord no longer holds either: `Alt`+letter is the menu-access plane (`Alt+F/E/V`).
   **RULED 2026-09-15: span and position markers are ONE object, the hand marker**
   (`docs/plans/roadmap/60-hand-markers.md` §2, the roadmap plan this record now executes under;
   it absorbed plan 61's seat). One chord pair — `Ctrl+H` / `Ctrl+Shift+H`, ruled there as
   60-H2 — and `Ctrl+P` / `Ctrl+Shift+P` return to the pool. The record key `"span"` chosen in
   item 2 below was chosen for a span-only record and is re-asked as part of 60-H2.
2. Save-file record: grid position only, under the key **`"span"`** (user ruled, overriding the
   `span_markers` suggestion): the record is a manually SPECIFIED span — the charter's own, as
   opposed to the derived ones that are never stored — and the short name says exactly that.
3. A marker placed where a derived span already opens PINS that span (the forced-boundary
   semantic already covers it).
4. Deleting a marker reflows the derivation automatically (spans are read-time derived).
5. **`NoteAttack::None` RIPPED OUT ENTIRELY — DONE 2026-09-17**, out of phase and ahead of the
   markers, so this item is closed and Phase 1 inherits none of it. The `N` verb, the attack value,
   the silent-hold SHAPE of a claim and the sweep's note half left together, and LAW II's
   justification machinery went with them as dead code (inventory item 2); `N` is unbound and free
   for reuse. The claim arm of the opening law and claim-founded spans STAYED — a claim
   still founds, through `held` under a tap or a scrape, and a claim-only span publishes at its
   instant with zero sustain. Phase 3's templates inherit no justification rule: a template that
   states a stop NOTHING sounds must bring its own. The named gap is ACCEPTED: a span with a
   bracket nothing sounds is unauthorable until Phase 3's templates land. No load notice was built:
   the user ruled out legacy and back-compat code, so an old `"attack": "none"` fails to read like
   any unknown attack token, and the affected population was at most one local project.

Also RIDING PHASE 1's seam (user, 2026-09-01):

- **Publish the OPENING SLOT on `ChartShape`** (Q-D): one derived `GridPosition`, written where
  `open_span_here` already knows it before computing the front. Makes the strike-less census row
  exact instead of a floor; the census row and its comment upgrade in the same change.
- **The clip-pass simplification** (Q-B): **DONE 2026-09-01, off this seam's list** — the clean
  let-ring baseline (85d08e20) deleted `clipLetRingExtensions` and the import pass's span
  derivation entirely (the cut law reads the note stream's sounding grip directly), a stronger
  form than this rider asked for. Kept for the record: the original brief was to replace the
  `deriveChartShapes` call + `event_fronts` with a pure note-stream test — a contradicting fretting-hand onset over a
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
  **Context shift 2026-09-01 (read before using the record above)**: the let-ring laws under this
  rider changed the same day — the clean baseline (three-rule law, sound-scoped grip, 85d08e20)
  replaced the span-founded clip and the voice-scoped cut (98ccd215) followed; the watch item was
  rescoped to same-voice drones with "drones belong in their own voice" as the standing practice
  answer. The A2 remedy menu remains the pre-measured starting point if the trigger fires, but
  every population number above was measured against the retired law.

**PHASE 2 — span-free zones** (after Phase 1; REORDERED AHEAD OF TEMPLATES, user 2026-09-01:
the >=3 settlement is gated on BOTH correction polarities and zones are the second one, while
templates gate nothing — so the settlement lands at this phase's end instead of waiting behind
the template system):

1. The zone record (location-anchored range; one-kind-with-polarity vs. two kinds is the named
   naming question) and its derivation wall: no Accumulation-founded opening inside a zone,
   spans close at its edge (one more close cause), Statement openings immune.
2. The DELETE gesture on a derived bracket authors a zone over the span's musical extent (the
   close, not the trimmed rails); one undo entry restores the zone-less derivation.
3. Zone resize via the note-tail sustain keybind pair; tail keys on a chord/arpeggio span REFUSE
   loudly (the extent invariant above). **Resize is BLOCKED at an authored span marker** (user,
   2026-09-01): expansion clamps at the marker's edge and a further press refuses loudly — the
   user must delete their own marker to extend the zone through it. This is the authoring-time
   refusal's twin, so the no-overlap invariant holds through EVERY gesture, not just placement.
   The precedence law in full: derived furniture YIELDS to the zone (expanding into derived
   territory eats it — the point of the zone); authored statements BLOCK it; authored never
   silently destroys authored in either polarity, and only explicit deletion changes an authored
   statement. By construction the Delete gesture never births the conflict: a derived bracket's
   extent cannot contain an authored marker (mid-extent markers split, front markers pin), so a
   Delete-authored zone is always born marker-free and the clamp only ever engages at resize.
4. Zone ink, editor-only: the zone's own lane style plus its authorship start line (red leaning,
   white reserved, EditorTheme roles); a span marker inside a zone is refused at authoring.
5. The background sighting triplet (none / all spans / zones only) is judged in this phase.
SETTLEMENT EDGE (user-sighted 2026-09-02, on generated material): the signed minimum binds
   ACCUMULATION openings, but a CLAIM plus a lone strike at one slot is
   STATEMENT-founded (the dyad carve-out) while PRESENTING as a bracket — only one member ever
   sounds, so nothing arrives together — yielding a 2-note arpeggio-looking span the ruling's
   intent ("an arpeggio span only exists when 3+ notes are in the grip") plainly meant to forbid.
   Founding class and arrival class are separate derivations, and the minimum is scoped by the
   former while the intent is scoped by what the bracket claims. Imports author zero
   claims (census: imported claims 0), so the shape stays hand-authored-only. DECIDE AT THE
   SETTLEMENT: bind the minimum by bracket-class presentation, or stop counting a claim toward
   the 2-member Statement protection when only one member sounds — one rule, not both. THE
   SIGHTING FIXTURE NEEDS REBUILDING (2026-09-17): the generated sighting reel
   (C:/__MAIN__/Coding/__scratch__/rockhero-sighting-reel/sighting-reel.rock) states its
   measure-3 figure with an `"attack": "none"` record, which no longer reads, so the package must
   be regenerated with the claim under a tap before it can be opened again. Keep the figure ON
   PURPOSE — open the rebuilt reel before and after the closure lands; the 2-note bracket must be
   there today and gone (or re-justified) after.

6. **THE >=3 MINIMUM IS ALREADY SIGNED — what lands here is the CENSUS RE-SIGN.** The user
   sighted and SIGNED the three-member minimum on 2026-09-04, ahead of this phase rather than at
   it, and the sighting rig went with the signature: the `F6` key and its command id (0x1A01,
   retired), the mutable minimum with its setter/getter, and the RAII test guard are all deleted,
   the derived minimum is a plain named constant 3 beside the Statement threshold of 2
   (`chart_shapes.cpp`), and the law-mechanics tests were REWORKED onto fixed-minimum-3 figures
   instead of being pinned to an explicit value. What still rides here is the census: **the
   accumulation checklist's section G** (user, 2026-09-02) — the four red rows from the
   clean-baseline seam (arpeggio spans / successor spans / death-opened successors /
   trigger-4-only flips) re-sign ONCE, at this settlement, on the world both polarities leave.

**PHASE 3 — templates** (after Phase 2):

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
7. `NoteAttack::None` left the format on 2026-09-17, ahead of this phase; the remaining deletion
   inventory completes here.

**THE SATELLITE REVEAL LAW, FINAL** (user, same day — supersedes the position-based law above,
and is MANDATORY in the accumulation seam, not deferred): a satellite is ALWAYS STANDING where
its held is AUTHORED (everywhere — no front/mid-span distinction) and where a tap fronts a
bracket; a DERIVED held's satellite is REVEALED on the note's truth channel — the same
selection/tail reveal that shows the note's actual ring — so revealing a note shows the whole
truth about it at once. Visibility keys on AUTHORSHIP plus one existing reveal channel.

## Sequencing and notes

- **The sweep's remaining scope — STILL OPEN.** The 2026-09-17 removal took only the sweep's NOTE
  half; `sweepInertClaimedStops` keeps its name and its held-field half unchanged. Re-examine that
  half here: with markers founding instead of claims, a held
  stop that reaches nothing may simply be a stop the hand was on, which is not obviously an error
  to sweep at all.
- **The `None` population was authored-only.** The GP importer authors ZERO claims — it never
  emitted `NoteAttack::None` and never writes a `held` field — so every record the 2026-09-17
  deletion touched was typed by a charter in this editor, at most one local project. That is why no
  load notice was built.
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

**SIGNED 2026-09-04, and the rider is now a DEBT rather than a condition.** The user sighted the
three-member accumulation minimum and signed it as the permanent rule; the sighting rig that let
the two pictures be flipped between (`F6`, the mutable minimum, the test guard) is deleted, and
the minimum is a plain constant in the derivation. What the signature does NOT do is discharge
this rider: the minimum is still a DEFAULT GUESS about what sound may found implicitly, and both
correction polarities below remain owed by this plan. Until polarity 1 ships there is no way to
state a two-note grip that sound did not found whole — an AUTHORED two-note span is the escape,
and it is this plan's deliverable.

The two polarities, so the minimum reads as a default with an escape in each direction rather than
as a correctness rule:

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
   boxes, dyads) are immune, on the same founding discriminator the minimum reads (`SpanFounding`);
   (b) a
   span marker inside a free zone is REFUSED at authoring (delete or trim the zone first) — two
   contradictory statements about one stretch stay unrepresentable; (c) zones touch span
   furniture only — stored rings, the cut law, and tail presentation are untouched, so a
   de-spanned wash draws its ordinary tails, matching the minimum's own two-note behaviour.

   Open for the build: one record kind with polarity vs. two kinds (naming question); the delete
   gesture's undo shape (one entry restoring the zone-less derivation).

This rider also gives the manual escape for segmentation figures the derivation guesses wrong
(the m11 sweep-boundary case in the ruleset's segmentation record): statement beats a cleverer
automatic walk.

### Rider additions (user, 2026-09-01, same conversation)

- **Zone resize.** A selected span-free zone extends and retracts with the SAME keybind pair the
  note-tail sustain gesture uses — one resize verb across the editor, no zone-specific keys.
- **Zone ink is EDITOR-ONLY.** The zone's derivation effect ships (the game sees the spans the
  zone shaped), but its ink is authoring furniture and never draws on the game highway — the same
  editor-only rule as the light-T charting mark. It draws in the lane with its own span style
  beside the existing chord (blue) and arpeggio (purple) line styles. Color leaning: RED — denial
  reads as denial, and it keeps WHITE in reserve for a possible future let-ring span. All three
  through EditorTheme roles, never baked.
- **Authored-span start indicators, and their meaning is AUTHORSHIP.** A span that exists in the
  save file gets a selectable vertical line behind the note heads at its start — blue for chord
  spans, purple for arpeggio spans, the settled zone color for span-free zones. Derived spans get
  NO start line: the line answers "what did I author?" at a glance, which is the fact no other
  ink states.
- **The let-ring span stays a named MAYBE.** Arpeggio spans may cover the need; if a let-ring
  notation is ever added it must solve the precision problem first (the mark is inherently
  imprecise and the user wants precision — see the let-ring texture analysis item in
  docs/tracking; the WHITE style is reserved for it either way).
- **Background sighting triplet.** Build the span backgrounds sightable in three candidate
  styles and judge in the editor: (1) no background over the 2D lane (the current look);
  (2) every span gets a subtle colored background; (3) ONLY span-free zones get one.

### The extent invariant (user-confirmed 2026-09-01): spans are NEVER resizable

A chord or arpeggio span's extent is STRICTLY derived from its content, always — a direct resize
would be a second authority for extent, free to disagree with the sound and make the bracket lie.
Every want routes through an existing verb: longer sound = the notes' tails (sustain keys);
shape held in silence = authored claims (a slot of held fingers already adds to a shape);
a different boundary = place or move a marker (a position, not a size); ended early = a zone from
that instant (the zone is a close cause, so it doubles as the end-a-span-early verb); gone =
Delete (the zone); below the minimum = Shift+S. The ZONE is the one resizable record precisely
because it has no content to derive from — its geometry is its whole statement.

UI pin — SUPERSEDED 2026-09-05 by the end gesture in the front-move section below: the tail keys
on a selected span now MOVE ITS END through the content (the co-terminating tails), which is not
the direct resize this pin refused — the pin's rationale (no second extent authority) stands;
its prescription updated once the content-edit mechanism existed to route the keys through. Falsification trigger: an extent a charter cannot express through
content + markers + zones would reopen this; nothing is pre-built for it.

## The span boundary gestures (SIGNED 2026-09-05 — front, end, Delete; build scheduled soon)

Motivation: the let-ring import walk (2026-09-04) proved some seams have NO obvious universal
answer — the watch registry's phrase-tier entries (the B figure foremost) are corrections only
the author can make, and today the correction is a hand-built batch of tail trims and extends.
The author is the tier above physics, grammar, and hand; this gesture is that tier's verb.

THE GESTURE: with a span selected, **Alt+Left / Alt+Right moves the span's START** to the
adjacent member onset (never a grid step — fronts date from onsets, so between-onset positions
are meaningless). The move stops where it would (a) cross a CONTRADICTION of the span's grip, or
(b) drop either affected span below the founding minimum and dissolve it.

NOT A SECOND AUTHORITY — the settlement rider stands: the span's extent remains strictly derived
from content. The gesture EDITS THE CONTENT and lets the derivation answer: moving the front
earlier extends the crossed notes' tails to this span's end and clips the predecessor's tails at
the new front; moving it later does the reverse (user-confirmed 2026-09-05: moving the marker
adjusts the contained and new members' lengths accordingly). Pinned precisely: only the BOUNDARY
populations are rewritten — notes entering the span extend to its end and join the
co-termination, notes leaving it clip at the new front — while members already inside are
untouched, because the front move never touches the span's END. Where the boundary is
marker-defined (this
plan's own verb), the gesture moves THE MARKER instead — one gesture, whichever record owns the
boundary. This is the keyboard form of the rider's "a different boundary" want, not a new want.
It is distinct from the REFUSED tail keys on a span (the rider's UI pin): those would resize the
extent directly; this retargets a boundary through the content.

Gesture mechanics (the settled patterns apply): one undo entry per gesture, settled at
completion (the sustain-gesture model; the same law #116 demanded for Alt+arrow note moves, which
now SHIPS — both verbs run through the one gesture authority `commitChartGestureStep`, so a span
gesture written to that contract writes only what a step MEANS and inherits the entry bookkeeping,
the commit points and the retire-on-no-change);
Alt+arrow is already the authoring-move family, and the overload is scope-clean — a span
selected moves the span front, notes selected move notes (the uniform-scope law).

DELETE ON SPANS (user, 2026-09-05): Delete deletes a selected span marker, and deletes a
selected DERIVED span. The two resolve differently and neither can reach an invalid state.
An authored marker is a stored record: Delete removes it and the boundary FALLS BACK to pure
derivation — the worst outcome is surprising, never invalid. A derived span has no record to
remove and its tails must not be guessed at, so Delete AUTHORS A ZONE over it — the rider's own
"gone = Delete (the zone)" made concrete: the span stops deriving, the notes and their tails are
untouched, and Delete on the zone un-suppresses. THE FAMILY'S LAW, which is what answers the
invalid-state worry structurally: every verb here edits INPUTS to the derivation — tails, holds,
markers, zones — and never writes a derived output; the derivation is total, so every reachable
state derives something legal. Invalidity is unrepresentable, not merely checked for. One
surprise to teach in the UI: deleting a derived span removes the STATEMENT, not the SOUND — the
members keep ringing their full tails bare (the B figure's own trailing picture); silencing the
ring is the tails' own verb.

THE END GESTURE (user, 2026-09-05, closing the end-move question): the SAME keys that extend and
shrink a note's tail move a selected span's END — the next span boundary — through the content,
one grid quantum per step (fronts step by member onsets because fronts are onset-dated; ends
step by grid quanta because closes are tail-valued and legally sit anywhere — each boundary
steps in its own truth). GROWING extends the co-terminating members' tails to the new end;
compatible onsets crossed join as members, and the first CONTRADICTING onset is the wall — the
mirror of the front-move's bound; per-member same-string clamps still bind individually, physics
never overridden. SHRINKING clips the co-termination earlier, walled at the founding minimum
(the same dissolve question and recommendation as the front). Where spans tile, one boundary has
two addresses — this span's end IS the next span's front — and both gestures route to the same
boundary-move mechanism: two ergonomic entries, one edit, never a second producer. The ZONE
survives as a genuinely different want: end keys make the SOUND stop sooner; a zone makes the
BRACKET stop sooner while the sound rings on. This supersedes the marker section's old
refuse-loudly UI pin, whose rationale (no second extent authority) stands — the keys now route
through content, which is not the direct resize the pin refused.

Rulings closed at signing:
1. The dissolve bound — RULED 2026-09-05: HARD STOP, uniformly, both gestures' shrinking
   directions, with the loud refusal style at the wall. The analysis that carried it: (a) Dissolution already has its verbs — the rider routes "below
   the minimum" to Shift+S and "gone" to Delete — and a second producer of one outcome is the
   project's named recurring-defect pattern. (b) Moving LEFT shrinks the PREDECESSOR, a span the
   author never selected: allowing dissolve there is collateral destruction, and an asymmetric
   rule (dissolve the selected, protect the neighbor) is two rules where the stop is one.
   (c) The refusal teaches the model — the rider's own UI-pin pedagogy; the wall makes the
   founding minimum tangible, and the refusal can hint Shift+S. (d) A repeated-tap gesture that
   vanishes its subject one tap past the wall is a destructive surprise mid-gesture. The
   doctrine-purity counterargument (content edits may legally derive no span) is real but
   misplaced: verbs are scoped — the same discipline that makes tail keys refuse on spans scopes
   the front-mover to front-moving. If ruled the other way, dissolution's product is at least a
   familiar legal picture (bare co-terminating tails, the B figure's own trailing pair). Note:
   every stop the gesture offers is a derivable state by construction (onset-stepped, bounded),
   and the B correction itself never touches this bound - its predecessor holds exactly three
   members at the desired seam.
2. RULED 2026-09-05 — see THE END GESTURE above: the note-tail keys move the span's end.
3. Whether a front moved onto a note whose tail then re-derives differently on re-import should
   leave any record — likely NO record by doctrine (the notes ARE the record; re-import
   overwrites authored corrections like any import does), but say it aloud at signing.
