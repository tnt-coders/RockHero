# Keyframes, the release and the undo burst — a ground-up review

Status: PROPOSAL, 2026-09-21. Nothing here is signed. It was asked for after a day in which six
defects in one corner of the chart editor were fixed one at a time (`c9e72dbf`, `928b25da`,
`9312840c`, `8318f371`, and the stale-burst-record fix), each by adding a rule. The question put:
if this corner were built from scratch knowing every rule since ruled, what would it look like, and
is a refactor warranted?

Three read-only studies of the code at `8318f371` feed it. Every code claim below comes from them
and carries that date; re-verify before acting on one.

## The finding

The six defects have two roots, not six, and a third thing that looked like a root is not one.

1. **The undo burst proves ownership with a number that is not an identity.** Root of the
   data-loss bug. Redesign recommended; no format change; do it first.
2. **The release is a keyframe by POSITION, so a statement changes meaning when the ring's end
   reaches or leaves it.** Root of five of the six. Two complete rulesets are offered below: Set A
   keeps that and makes every verb ask about it; Set B gives the end its own statement so nothing
   converts. B is recommended and is the user's to rule — it changes a behaviour the user likes
   and reverses the signed 2026-09-09 model decision.
3. **Silent points living in the chart document is NOT a root.** The obvious simplification —
   hold a just-typed point in the controller, like the pending fret entry — does not work, and
   the reason is worth recording so it is not tried again.

## The ruleset

The user asked for the CLEAN ruleset to redesign from, said a bend must probably be placeable
exactly at a ring's end (Guitar Pro is believed to allow it; being measured), and opened the door
to changing behaviour where that buys a simpler coherent set. So this section offers two complete
rulesets. Both satisfy everything ruled so far except where marked; they differ in ONE decision,
and that decision also picks the stored model. The first draft of this section had a single
seven-rule set whose rule 1 said "nothing but a bare fret may stand at the end"; the bend
requirement breaks that rule, and looking at why produced both sets below.

Shared vocabulary. A note RINGS from its onset to its END. STATEMENTS are made at moments along
the ring, one per moment, in order, each speaking on one or more CHANNELS: the FRET path (glides
between stated stops), the BEND curve, the SHAKE (a state that holds until the next statement).

What a statement MEANS at the very end is a fact about the channels, not a rule about releases —
the earlier "a release is bare" rule was this, over-stated:

| Channel | Inside the ring | Exactly at the end |
|---|---|---|
| fret | a pitched stop the path arrives at | the FALL: an unpitched slide-out toward it, if it travels; nothing if it repeats the fret in force |
| bend | a value the curve passes through | the curve's LAST value — meaningful, it sets the final slope |
| shake | the state from here on | nothing: there is no ring left to shake |

### Set A — position gives meaning (today's behaviour, generalised)

1. **Shape.** Statements stand at moments in (onset, end], in order. The statement AT the end means
   what the table says.
2. **The tail verb moves the END, never a statement.** Statements are its walls: it stops at the
   next head and at the ring's last statement. It may land ON that statement only if every channel
   the statement speaks on still speaks at the end — so never onto a shake, never onto a fret that
   would stop travelling. Landing on a travelling fret is how a glide becomes a slide-out; a ring
   whose end holds a statement cannot shrink; growing one leaves the statement behind, now inside.
3. **The move verb moves what is SELECTED, never anything else.** The end is its wall, like the
   onset and the neighbours. The statement at the end IS the end, so moving it moves the end.
4. **No verb makes a statement say less, or deletes one it was not aimed at.** A note arriving on a
   ring is refused by any statement it would cut or silence; the end's own statement rides back to
   the new end.
5. **A statement that says nothing new is authoring state** while its note is in focus — and only
   where it can be SEEN. One nothing draws is removed by the edit that made it.
6. **A held run is a pure function of its start and its net steps.**
7. **A wall the charter can see is silent.** The flash is for an unexplained nothing.

Its accepted seam: the end's statement follows the end under the move verb and a truncation, and
stays behind under the tail verb's grow (ruled 2026-09-10 as a toss-up). Its standing hazard: a
statement CHANGES MEANING when the end reaches or leaves it, so every verb that moves an end or a
statement has to ask rule 2's question. Most of this week's defects were a verb that did not.

Model: the keyframe vector as it is, with `stripReleaseChannels` and the `ReleasePayload` repair
DELETED (a bend at the end is kept; a shake there is ordinary silence), the landing test
generalised to the channel table, and truncation reduced to "refuse, or carry the end's statement".
No format change beyond permitting a bend on the end keyframe.

### Set B — kind never changes

The one decision: **the end has its own statement, and it belongs to the end.** It is not a
keyframe that happens to stand there.

1. **Shape.** POINTS stand strictly inside the ring, in order. The END may carry an END STATEMENT:
   a fall toward a fret that travels, a final bend value, or both.
2. **The tail verb moves the END, and the end statement goes with it.** Points are its walls: it
   stops one step short of the last point, and at the next head.
3. **The move verb moves the selected POINTS, never anything else.** Its walls are the onset, the
   neighbours and the end. (Selecting the fall's chip and stepping it IS the tail verb: the chip is
   the end's handle.)
4. **No verb makes a statement say less, or deletes one it was not aimed at.** A note arriving on a
   ring is refused by any point it would cut; the end statement rides back with the end.
5. **A point that says nothing new is authoring state** while its note is in focus. An end
   statement that says nothing is removed by the edit that made it.
6. **A held run is a pure function of its start and its net steps.**
7. **A wall the charter can see is silent.**

Nothing converts, so there is no landing test, no seam, no "released ring cannot shrink" (it can:
the fall gets shorter, which is what a charter would expect), no grow-leaves-it-behind branch and
no grown-release-steps-back branch. A scrape's terminal already rides its end in both directions
as a special case; under Set B that is simply the rule.

What it costs in behaviour: **shrinking a glide onto its landing no longer turns it into a
slide-out, and growing past a slide-out no longer turns it back.** The end statement is authored
with `Alt`+digit at the ring's end (already built) and removed with Delete; turning an existing
glide into a slide-out is Delete the landing, then `Alt`+digit. The user called the shrink
conversion out approvingly on 2026-09-21, so this is a real loss to weigh, not a free deletion.

Model, IN MEMORY: `keyframes` strictly inside `(0, sustain)`, plus an end statement on the note
holding an optional fall fret and an optional final bend — with no offset, because its moment is
the end by definition. Held as a keyframe whose offset must always equal the sustain, every verb
that writes a sustain would have to move it by hand, and forgetting to is exactly the accidental
kind-change this set exists to remove.

Model, ON DISK: **unchanged** (the user's question, 2026-09-21: why could the current format not
hold a keyframe at the end stating both a bend and a fret? It can). The file already writes the
statement at the end as the last keyframe row, and nothing in the JSON forbids that row a bend;
only the `ReleasePayload` load repair and `stripReleaseChannels` destroy it, and both go under
either set. So the in-memory model and the file are separate decisions: the writer emits the end
statement as a row at `offset == sustain`, the reader takes the row at `offset == sustain` AS the
end statement, and a shake stated there is shed as the silence it is. In a file the equality is a
definition, not two facts to keep in step — a row is the end statement exactly when its offset is
the sustain — so nothing can desync, and no package is re-imported and no converter is touched.
The format spec changes by one sentence: the row at the ring's end may state a fret (the fall) and
a bend (the curve's last value).

About twenty-five tests pin a conversion and are retired or rewritten.

**The end statement and the next head — one clearance rule** (user, 2026-09-21: a bend landing on
the next note "would need to squish backward the same way a slide out currently does. Following
the SAME rules"). The chart law already forbids a statement standing ON the next head on its
string: `normalizeKeyframeClearances` pulls it back to the clearance, on load and in the plan
gate. Today that repair has two branches — a release SHORTENS the ring to the clearance, while any
other keyframe at the end is moved back ALONE and the ring keeps its length — so a fall and a bend
at the same end obey different rules. Under Set B there is one: the end statement is the end, so
the ring ends at the clearance, whatever the end states. It also means no end statement ever
shares a slot with a head, which is the gap the user proposed as a rule; it is the existing law,
generalised. Material imported from Guitar Pro, where notes abut by construction, reaches it
through that repair.

Measured the same day: the WRITTEN material abuts more often than not — 174 of the 288 bends still
moving at their note's end (60%) and 463 of the corpus's 977 slide-outs (47%) end exactly on the
next head of their own string — and the imported chart holds none of them there; every one is
pulled back. The clearance is one minimum sustain distance (0.1 s, through the tempo map) before
the head, or halfway along the last leg where that would crowd it. The two branches matter in
practice: a slide-out keeps its meaning, but a bend's moving terminal is moved back alone, so it
becomes an interior point and the curve then holds flat to a ring end it no longer reaches — a
bend that "completes as the note ends" is silently imported as one that completes slightly early
and holds. 174 notes in the corpus take that today. Under Set B's single rule they do not — and
Set A can reach the same result: its rule 4 already says the end's own statement rides back with
a truncated end, so the repair's release branch generalises to "whatever stands at the end". The
defect argues for one rule, not for one set.

**The user leans to Set A, 2026-09-21**, on the one behaviour that separates the sets: lengthening
a note that ends in a bend release, "I'd expect the bend point to stay put I think." That is Set
A's rule 2 — the tail verb moves no statement. Not yet signed as a set.

**Authoring at the end WITHOUT `Alt`** (the user, same day: needing `Alt` there "is not
intuitive"). Under Set A the modifier can go entirely, because the only thing it disambiguates is
a DIGIT at a ring's exact end — next note, or fall — and Set A already has an unmodified way to
make a fall:

- **A slide-out is made by the conversion the user already likes**: type the landing fret on the
  tail, then bring the tail in onto it (rule 2's landing). No chord, and it reads as what it is —
  a slide that ends the note. A bare digit at a ring's exact end stays what sequential entry needs
  it to be: the next note.
- **A bend at the end needs no modifier either**: `B` on an empty slot means nothing else, so with
  the caret on a ring's exact end where no head stands, bare `B` can only be the ending ring's
  final bend value. Where a head does stand on that slot, `B` is the head's (its pre-bend), and the
  clearance law says no end statement may sit there anyway — the end bend is authored a step
  inside and the tail brought in onto it, the same conversion.
- What this DELETES: `Alt`+digit's one creating cell (`at_ring_end && path` in
  `chartCaretDigitTarget`) and, since the chord is the bare digit everywhere else, plausibly the
  ten `TypePathDigit` commands and their both-key-code registration with it. `Alt` alone stays the
  ring reveal. It also retires the defect found the same day — `Alt`+digit cannot reach a ring
  whose end abuts a selected head — by removing the chord rather than repairing it.
- It reverses the 2026-09-11 ruling that made `Alt`+digit "the ONE thing `Alt` creates on this
  lane", so it is the user's to sign. It is only available under Set A: Set B has no conversion.

**The DRAWN tail beside a head on another string** (raised by the user, 2026-09-21). The stored
ring is the actual ring, and a head on ANOTHER string does not stop it, so nothing above applies.
What applies is presentation: `trimToMargin` pulls a drawn tail back one minimum sustain distance
before the binding onset on any string, so the columns keep their spacing — but its rule 2 floors
the trim at the note's last keyframe (`lastStatementEnd`), and an end statement IS the last
keyframe. So a bare tail tucks in before a head on the next string while the same tail ending in a
bend, or a slide-out, draws right up to that head's column. The slide-out half exists today and is
written down as accepted ("a head on another string may sit inside it"); end bends make it common.

The user's expectation — it should compress to the normal distance — is the same one rule again:
**whatever moves the end, other than the tail verb, carries the end's own statement with it.**
Truncation and the clearance apply it to the stored ring; the presentation trim applies it to the
presented copy. Rule 2 floors the trim at the last INTERIOR statement instead, and the clip the
trim already calls carries the end statement to the presented end, where a same-string neighbour
and an other-string neighbour then look alike: the statement completes as the drawn tail ends, one
margin before the head. The stored ring is untouched, as every presentation rule leaves it. Both
surfaces and scoring read the presented note, so the highway agrees by construction.

**Storing the truth: deleting the stored clearance law** (the user, 2026-09-21; under study). If
the presented trim carries the end's statement, the stored rule "no keyframe sits on a head of its
own string" duplicates presentation, and the file can hold what the hands do — a slide-out or a
bend running right to the next head — while display and scoring alone apply the spacing. The user
wants it seriously considered even if the study finds obstacles: obstacles are then things to
design around, not a veto. Candidates to go with it: `normalizeKeyframeClearances` and its helpers,
its call and ordering constraints in the plan gate, the scrape wall special case, and the import
squish — so the 174 abutting bends import exactly as written. To be established: why the law was
introduced; how a drawn chip maps back to its stored keyframe once the end chip draws a margin
early; the `Shift+L` join of two abutting notes when the first states something at the junction
(two statements at one offset); and any derivation that reads a release's offset against the next
onset.

*Study result, same day.* The law's origin is four commits in one session (`de91e3d8`,
`c01698fc`, `93f108f4`, `4414579e`) and its reason is REACHABILITY, not sound: a release on the
next head "could be neither seen nor reached". `93f108f4` moved the clearance INTO the stored chart
precisely so presentation could stop compressing and "a release always draws where it is stored".
So this is a reversal of that reversal — justified by what has changed since: the end may now
carry a bend, the user wants every tail spaced alike, and the file should say what the hands do.

One real obstacle, and it is not specific to deleting the law. `KeyframeViewState::offset` is
documented as the keyframe's stable identity and is filled from the PRESENTED keyframe; click,
caret and the accent ring all map through it. The moment presentation moves the end's statement,
the drawn chip and the stored keyframe have different offsets and the three mappings disagree. But
the other-string spacing the user asked for moves the presented end statement too, so this cost is
owed EITHER way. The fix is small and has precedent: `offset` stays the STORED identity, `seconds`
is the presented instant (both surfaces already paint from `seconds` alone), and the projection
reads the stored note for it as it already does for the release flag. With that paid, deleting the
stored law is pure deletion.

What else the study settled:

- **Not blockers.** The `Shift+L` join refuses a slide-out predecessor and MERGES a bend at the
  junction rather than doubling the offset. The validator never enforced the law ("normalized,
  never refused"), so every saved package stays valid. Nothing game-side reads stored keyframes.
  No derivation reads a release's offset against the next onset.
- **Deleted** (~90 lines of production code, ~270 of tests): `keyframeClearanceOf`,
  `normalizeKeyframeClearances`, `ChartRepair::CrowdedKeyframe` (sweep every switch over the
  enum), the gate call and the between-the-two-repairs ordering argument, the scrape "strike is a
  WALL" clause, and the import squish — 637 abutting end statements import as written.
- **Must stay.** `latestStatementBeforeStrike` for the importer's synthesized shift arrivals (span
  rule 6 is calibrated to it) and for the `Shift+L` split's retreat, which becomes load-bearing for
  a new reason: left on the head, the arrival would now be the product's release by position.
- **Presentation's change, exactly.** The trim needs "last INTERIOR statement" while the tail law's
  rest landmark still needs "last statement, end included", so `lastStatementEnd` becomes two
  questions. `clipPayloadsToSustain` must CARRY whatever stands at the end to the new end — today it
  carries only a fret-stating end and ERASES a bend-only one — merging where a statement already
  stands there, with a guard for the zero-length drop (`dropPresentedTail`), and
  `stripReleaseChannels` goes with it.
- **Accepted consequences for the user to confirm.** The reveal (`Alt`, a selected note, the caret
  peek) shows the STORED form, so an end chip shifts by the margin when its note is selected —
  which is the behaviour the user described. Old packages keep their squished ends until
  re-imported: two spellings of one sound, both valid. And where a head shares the slot the caret
  selects the HEAD (`chartObjectAt` searches notes first), which settles "a bare digit there is
  always the next note" with no new clause and makes the end chip click-only — the gap the
  through-the-note proposal below closes.

**Reaching a ring's end without a modifier — THROUGH THE NOTE, not through the slot** (proposal,
2026-09-21, answering the user's "either ALT, or maybe SHIFT, or another method"). The ambiguity
at a ring's end is an ambiguity of OPERAND: the ring that ends here, or the note that starts here.
A modifier answers it per keystroke. The editor's own grammar answers it better: *a non-empty
selection is the operand*. So make the ring's END a selectable point of its note — the key
`(note, offset == sustain)`, whether or not anything is stated there yet; the selection model
already lets a keyframe key name a point that does not exist — and the slot stays what it always
was, the place the NEXT note starts:

- **Reach it** by clicking the tail's end, or from the keyboard with `End` while on a note
  (`Home` returns to its head) — the text-editor convention, line versus document, which the keymap
  is already half-way to: `Ctrl+Home` / `Ctrl+End` are the ruled aliases for the chart's bounds, so
  the bare keys can take the narrower scope. `Tab`, which already walks the objects on a string
  with the grid ignored, stops on an end only when it states something.
- **With the end selected, nothing new is needed**: a digit states the fall and `B` the final bend
  by the selected-point rule that already retypes keyframes; Delete clears it; `Alt`+arrows drag it
  and the ring's end with it, which is what the move verb already does to a release; `V` is refused
  there, a shake at the end saying nothing.
- **What it removes**: `Alt`+digit's creating cell, any `Alt+B`, the rule that arming a slot
  selects a slide-out's chip, and the shared-slot question entirely — a slot never has to choose
  between a head and the end beside it, which matters once the stored end may sit exactly on the
  next head. Sequential entry is untouched: a bare digit on a slot is always a note.
- `Shift` is not a candidate: it is the extend modifier across the whole interaction model.
- Unverified: how "armed means the selection is what sits under the caret" reads when a head and
  the previous ring's end share a time. The clearance study reports on that.

**A head typed onto a ring's end that holds a statement** (the user's question, 2026-09-21). The
answer the rules give: the head wins and the end squishes back. The insert is legal — a digit at a
ring's exact end is the next note — and the plan gate's clearance repair then finds a statement on
a head of its own string and, under the one clearance rule, shortens the ring to the clearance
with its end statement still AT the end. The bend still completes as the ring ends, a hair before
the new head; one undo entry holds both. It is exactly what import does to the 174 abutting bends,
which is the point of "the SAME rules". A head on another string changes nothing.

Weighed the same day and not recommended: REFUSING the head instead. It is shorter to say — no
statement ever moves except under the move verb — but it is not simpler to build or to use. Import
cannot refuse 637 abutting notes, so the squish stays in the load path and the editor would gain a
second, different answer to the same situation, against "the SAME rules". And the charter loses
the only easy way to write what the material mostly is: 60% of end bends abut the next note, a
ring whose end holds a statement cannot be shortened by the tail verb, so the workaround is to
drag the end statement back by hand — a whole grid step where the repair leaves 0.1 s, or a run of
tick steps with snap off. The squish is also not a breach of rule 4: that rule is about LOSS, and
a squished statement says everything it said.

One thing stands in the way and needs a ruling: today ARMING the caret on a slot where a slide-out
ends SELECTS its chip, so a digit there retypes the fall instead of placing a head (ruled
2026-09-11, when `Alt`+digit made falls and a fall at the slot was the rarer case). Carried over to
end bends, a charter who ends a note with a bend release and steps right to type the next note
would instead give the bend point a fret — a fall nobody asked for. Proposed: a bare digit at a
ring's end slot is ALWAYS the next note, no exception for what the ring's end states; the end
statement's chip is selected by clicking it, and the keys address it once it is. Sequential entry
then has no trap in it, and the rule has one clause instead of two.

**Authoring the end's bend, for Phase 3** (superseded by the entry above if that is signed) (raised by the user 2026-09-21; a proposal, to be signed
with the `B` verb). The slot where a ring ends is often the slot where the next note's head
stands, and the keymap already has the grammar for that one ambiguous cell: a bare digit there is
the NEXT NOTE, `Alt`+digit is the ENDING RING's fall — "the ONE thing `Alt` creates on this
lane". The bend follows it unchanged: bare `B` on that slot addresses the head standing there (its
onset bend, which is what a pre-bend is), and `Alt+B` states the ending ring's final bend value.
`Alt` means "the ring that ends here" in both chords, and strictly inside a ring `Alt+B` is just
`B`, so a mistimed `Alt` costs nothing — the property the digit row was ruled to have. `Alt+B` is
unbound today (`Alt+F` / `Alt+E` / `Alt+V` are the menu mnemonics). Open with it: what arming the
caret selects when a head AND the previous ring's end statement share the slot — today arming
selects a slide-out's chip where one ends there, which was ruled before a head could be assumed
beside it.

### Which

Set B is the one that would be built from scratch: the same seven rules with three of them
shorter, no seam, and the property six rulings in eleven days converged on — kind never changes by
position — held by construction instead of by every verb remembering to ask. With the file format
unchanged its whole price is behavioural: the shrink conversion. Set A keeps that gesture, and its
hazard does not go away: the next verb that
moves an end (paste, transpose-with-duration, a tempo-map retime, bend authoring itself) must
remember rule 2's question or reopen this week.

Recommendation: **Set B**, on the strength of the long-term bar the user set for this review — with
the shrink conversion's loss stated plainly as the price, and the user's to refuse. If the
conversion is worth more than the simplification, Set A is coherent and cheaper today.

Either set settles who owns what in one line: **the end is the tail verb's; points are the move
verb's; deleting is Delete's.**

**The move verb at the end — set aside, with reasons, so they are not re-proposed** (the wall was
ruled by the user on 2026-09-21 and both sets keep it):

| Shape | A statement moved onto the end… | Why not |
|---|---|---|
| Symmetric conversion | becomes the release, then drags the end | coherent under rule 6 — the ring ends at `max(start end, p)` — but the move verb then changes something it was not handed, and still needs the wall for statements that cannot convert |
| Push | pushes the end one step ahead of itself | moving a slide's landing changes the note's DURATION, which the legato hold test and the drawn tail read; with snap off the pushed tail is a one-tick sliver. The small upgrade if the wall proves obstructive in the hand |
| Ripple | carries every later statement and the end | a head move moves ONE object whose statements ride with it; statements are siblings, each pinned to its own musical moment. The useful half already exists: select the figure and it moves whole |

What is open against rule 4 under EITHER set, and gets built whichever is chosen: a note INSERT
truncates with no guard at all where the move verb has `moveErasesStatement`; and a statement
standing exactly ON a truncation landing is kept by the inclusive clip and then bared, with no
refusal and no test (Set B closes this one by construction — no point can stand at the end).

## 1. The undo burst

### What is there

A held-key run (move, sustain, harmonic), a technique-toggle window and the legato settle all fold
their work into ONE undo entry by splicing the top of the stack (`replaceTop`, `dropTop`). Two
optionals carry the claim: `m_chart_notes_top` — the in-memory plan of "the chart-notes entry this
burst pushed", plus the history POSITION it was pushed at — and `m_chart_verb_window` — the keys
and verb of the run. The whole ownership proof is `position == recorded position`.

A position is a slot, and a slot is not an entry:

- an edit whose WRITTEN diff is empty (it only plants or moves a silent point) pushes nothing, so
  the position does not move and the previous edit's record still passes — the bug that brought a
  deleted note back and dropped its undo entry;
- undo followed by a push lands a different entry in the same slot — patched by hand, with a
  comment saying so, in the undo and redo handlers;
- at the 100-entry cap the history erases its front and decrements the position
  (`enforceMaxEntries`, verified 2026-09-21), so every push from then on leaves the number at 100.
  A section or tone edit pushed after a chart edit therefore does NOT end the chart burst by the
  proof: the record still passes while the top entry is the stranger's. The stale-record fix
  (`0bbac153`) does not cover this — its reachability argument assumed a push always raises the
  position. Which reader acts on it (the settle fold's `replaceTop` is the likeliest) is to be
  pinned by a failing test as the redesign's first step; a long session reaches the cap, so this is
  the reason the redesign is next and not someday.

Around that proof sit thirteen hand-kept clear sites for the record, a window that can be armed
with no record behind it, and a record whose single writer only wrote on one of its two paths.
`EditorUndoHistory` has no test for `dropTop` at all.

### What it should be

**One fact instead of two, and an identity instead of a slot.**

- `EditorUndoHistory` gains a monotonic **revision**, bumped by every mutation of the timeline —
  push, replace, drop, undo, redo, the cap's trim, reset, and marking clean. It is a plain counter
  on a class that stays a pure, unit-testable timeline.
- The controller holds one `std::optional<ChartBurst>`:
  the in-memory plan (start of the burst to now), whether the top entry is the burst's own,
  the revision it last left the history at, and — optionally, inside it — the run (keys and verb).
  A window cannot be armed without a burst because it is a member of one; clearing the burst is
  the only clear there is.
- Every chart-notes apply REPLACES the burst. There is no path that leaves an older one standing,
  so "the writer is total" stops being a rule to remember and becomes the shape of the assignment.
- The proof is `history.revision() == burst.revision`. Undo, redo, an interleaved section or tone
  edit, the cap and a save all end the burst by themselves.
- **An entry-less burst is an ordinary state**, not an accident: a run that has only moved a silent
  point holds its plan and owns no entry. Its next step still reconstructs the run's true start
  from that plan, and the first step that writes something pushes one entry describing the whole
  run. That is strictly better than the stopgap fix, which has to break the run at that boundary.

What stays, on purpose: reconstructing the pre-gesture chart by reversing the burst's plan. The
code's own argument holds — the plan already IS the start state, and a second held copy could only
disagree with it. The alternative costed, an open transaction on the history holding a base chart,
deletes that reversal but gives the pure timeline an open-entry state that `canUndo`, `snapshot`,
the clean marker and a mid-stack cursor would all need an answer for. Not worth it.

**To verify while building, not assumed:** save ending the burst through the revision should make
the three verb-specific clean-state fallbacks (the gesture opening a fresh run, the settle pushing
instead of folding, the toggle reversal pushing its inverse) fall out of "there is no burst" rather
than each asking `isAtCleanState()` first. If so, code is deleted; if one of them needs the
question for another reason, it keeps it.

The regression net already exists and is listed in the study: the gesture, toggle, legato, harmonic
and keyframe suites pin one-entry-per-run, net-zero-leaves-no-entry, save-mid-run, undo-mid-run and
the mid-stack deferral. Add the missing `dropTop` tests and the cap case.

## 2. The release

### What is there

A slide-out is `keyframes.back()` when its offset equals the sustain and it states a fret. It was a
field, `ChartNote::slide_out`, until `88b21344` (user-signed 2026-09-09, "whatever is definitively
better"; re-ruled 2026-09-10). The record attributes **no defect** to the field. Its three stated
wins: one payload kind; no second way to write a fret at the ring's end; and the falls-away chip
becoming a selection citizen — click, retype, Delete, drag — with no new machinery.

What position-as-kind has cost since, all of it code whose whole subject is a point changing kind
because the ring's end moved:

| Rule added | Why it exists |
|---|---|
| `stripReleaseChannels`, `releaseWouldStripChannels`, the `ReleasePayload` load repair | a point that becomes the release may be carrying a shake or a bend it cannot keep |
| the detach / clip / re-attach dance in `clipPayloadsToSustain` | the release must be lifted off, the ring cut, and the release put back at the new end |
| `ringEndMayLandOnLastKeyframe` | a shrink may only land where becoming the release costs and hides nothing |
| the move verb's end bound (`9312840c`) | a stepped point must not become the release mid-gesture |
| `dissolveSilentRelease` and its copy-pop-ask helper | a release is judged by the keyframe commit law, which is defined on the note WITHOUT the point |
| `&keyframe != release` exclusions in shapes, the importer (three sites), `releasedFret`, `moveErasesStatement` | every walk of "the statements a hand makes" must remember to skip the last one |
| **still open** | a keyframe with a shake or bend exactly ON a truncation landing survives the inclusive clip and is then bared: a statement deleted with no refusal, and no test |

Six user rulings in eleven days converge on one sentence — *kind never changes by position* — which
is the property a stated release has by construction.

### The three models

**Positional (today).** Two conversions are free: shrinking a glide onto its last point makes a
slide-out, and growing a released ring leaves the release behind as a pitched stop. Everything in
the table above is the price.

**A kind tag on the keyframe.** Keeps every editor surface untouched and deletes the strip family,
but the release then stores its offset AND must equal the sustain — the desyncable second
coordinate the 2026-09-09 record rejected in so many words. Not recommended.

**A stated release: `std::optional<int>` on the note, the ring's end its position by definition.**

- Unrepresentable, so deleted: a release carrying a bend or shake (the strip family and the load
  repair), the detach / re-attach dance, every skip-the-release exclusion, and the open hole above
  — a landing either clips a point, which the move verb's existing guard refuses, or does not
  reach it.
- Structural instead of enforced: nothing becomes the release by accident of position. Rule 2's
  conversion is ONE function and its inverse inside the tail verb, in place of a property every
  other verb must remember to guard against.
- Simpler, not gone: the silent-release check becomes one comparison of the field against the fret
  in force at the end. It stays because silence is relational — typing a 6 before a release to 6
  silences it under any model.
- Now explicit, three lines each, where position was free: converting the last point to the
  release, and the release back to a point at the old end (rule 2). Both remain the user's ruled
  behaviour; only their spelling changes. The predicate stays, minus its strip clause.
- The chip stays a selection citizen without a third selection kind: with keyframes no longer
  allowed a fret at the ring's end, the key `(note, offset == sustain)` names the release
  unambiguously, and `KeyframeViewState::release` is already a derived view flag both surfaces
  read. The cost is a branch where a verb resolves a key to what it edits (retype, Delete, the
  vibrato verb's refusal, move) — several of which branch on release-ness today anyway.
- The scrape's terminal is unaffected either way: it already differs from an ordinary release in
  five `isScrape` clauses (required, rides both directions, re-aims, never silent, its own floor).
  "One payload kind" bought less there than the record implies.

**Costs to weigh.** A format change — a release key returns to the note and the keyframe rows lose
the at-the-end fret — which by the no-migration rule means re-importing the corpus and teaching the
external converter, not writing a reader for the old form. About twenty-five tests pin a
kind-change and are rewritten rather than re-spelled. `file-formats.md`, the 2D-views guide and
the walkthrough's W10 / W13 entries are restated.

### Where this section now stands

Written before the user's statement that a bend must probably stand at a ring's end. That need is
what breaks "a release is bare", and the ruleset section above works through the consequence: the
choice is no longer "positional versus a stated release" in the abstract but Set A versus Set B,
and the stored model follows from the set. The inventory above still applies — it is the list of
what Set B deletes and what Set A generalises.

**Measured 2026-09-21 against the local Guitar Pro corpus (115 files, all read; 1960 bent notes).**
The user's belief holds, and the need is common:

| | bent notes | share |
|---|---|---|
| final value reached early, then held | 1129 | 57.6% |
| a point at the very end that only restates the plateau (silent, correctly stripped) | 528 | 26.9% |
| **a bend still MOVING at the very end** — 181 releases, 122 rises, across 26 files | **303** | **15.5%** |
| bent notes that also slide out | 9 | 0.5% |
| both — a moving end bend AND a slide-out | 0 | 0% |

The curve holds flat past its last statement, so a bend that completes exactly as the ring ends
can only be written as a statement AT the end; the bend study says as much ("a zero statement
creates the release ramp"). Today a bend-ONLY keyframe at the end is already legal, round-trips,
is imported from Guitar Pro without clamping, and both surfaces draw it. What today's model cannot
hold is a fret AND a bend at the end: the bend is destroyed at load (`ReleasePayload`), by
`setSlideOut` and after every clip, and the importer's slide-out path takes that loss on purpose.
No note in the corpus needs both — so that collision is a hole in the model, not yet in the data.

One side effect to carry into either set: the presentation trim never trims past a note's last
keyframe, so an end-of-ring bend pins the DRAWN tail to the full stored ring.

## 3. Why silent points stay in the document

A point that says nothing new — the note's own fret typed onto its tail, about to be given a shake —
is legal in the chart in memory, invisible to undo, stripped on save and load, and dissolved when
focus leaves its note. It fed several of the defects, and the project already has the opposite
pattern: the pending fret entry touches nothing until it settles. Moving the silent point out of the
document the same way looks like the clean answer. It is not:

- **Silence is relational.** A committed, undo-recorded point becomes silent with no act on it when
  the note's own fret is retyped, when an earlier keyframe is inserted, removed or retyped, when
  another point moves, or when its shake is cleared and the fret it also states is one the path
  already passes through. `Shift+L` joining equal frets creates a silent point ON PURPOSE — that is
  the tie. A creation-time pending point covers none of these, so the document-side dissolve would
  survive and a second provisional mechanism would stand beside it.
- It would restore the caret and arrow branches for a slotless point that W13 deliberately deleted
  on 2026-09-09 after a sighting found all three.
- **Recording silent points in undo was built and reverted on 2026-09-10**: a recorded dissolve
  cannot run while a later entry sits above it, so the point outlived its focus.

So the model stands. What made it dangerous was the burst's proof, and section 1 removes that: an
edit with an empty written diff becomes a representable state instead of a hole. Two loose ends
belong to this model whatever else is decided: `chart_presentation` states as an invariant that a
stored note's last keyframe always says something, which is false in memory; and the backlog's
accepted hazard — a gesture retiring a real entry on an empty written plan — should be re-verified
against the new burst and closed or restated.

## Order of work

0. **Choose and sign a ruleset** (Set A or Set B above). Rule 3's wall is ruled in both. The
   signed set moves into `chart-ruleset.md` so there is one place it is stated.
1. **Now:** the stale-record fix and its regression tests (in flight). The tests outlive the fix.
2. **Next, no ruling needed beyond this document:** the burst redesign. It replaces the fix it
   follows and deletes more than it adds.
3. **Rule Set A or Set B** once the corpus measurement of end-of-ring bends is in.
4. Build the chosen set as one change set — under B the in-memory model, the reader and writer's
   mapping of the end row, the tests and the docs — before Phase 3, which authors bends on this substrate. Rule 4's open halves are built
   under either.
