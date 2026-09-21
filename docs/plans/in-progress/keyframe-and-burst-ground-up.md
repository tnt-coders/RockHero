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
2. **The release is a keyframe by POSITION, so a point changes kind when the ring's end moves.**
   Root of five of the six. The ruleset below closes it as rules; whether the MODEL should also
   change to a stated release turns on one fact about bends, to verify before Phase 3. A model
   change is the user's to rule — it reverses a signed 2026-09-09 decision.
3. **Silent points living in the chart document is NOT a root.** The obvious simplification —
   hold a just-typed point in the controller, like the pending fret entry — does not work, and
   the reason is worth recording so it is not tried again.

## The ruleset

The user asked, mid-review, whether a SIMPLE ruleset exists to build from. It does; it was never
written in one place, which is part of why each defect was met with a local rule. Seven rules, and
the first three carry the rest. Rule 3's wall was RULED by the user on 2026-09-21 after weighing
three alternatives (recorded below the rules); everything else restates rulings already made.

A note RINGS from its onset to its END. STATEMENTS are made at moments along the ring — a fret, a
bend, a shake, any combination — one per moment, in order.

1. **Shape.** A statement sits strictly inside the ring, or exactly AT its end. The statement at
   the end is the RELEASE: an unpitched fall, which can only be a bare fret that TRAVELS — no
   bend, no shake, and not the fret the string already holds. Nothing else may stand at the end.
2. **The tail verb moves the END, never a statement.** So statements are its walls. It stops at
   the next note's head and at the ring's last statement — landing ON that statement only if it
   qualifies as a release (rule 1), which is how a glide becomes a slide-out. For the same reason
   a released ring cannot shrink, and growing one leaves the release where it stood, now an
   ordinary pitched statement inside a longer ring.
3. **The move verb moves what is SELECTED, never anything else.** So the end is its wall: a moved
   statement stops one step short of it, as it stops short of the onset and of its neighbours.
   The one selected thing that IS the end is the release, and moving it moves the end. Selecting
   several statements moves them as one figure.
4. **No verb deletes a statement it was not aimed at.** A verb may stop, or refuse; it may not
   leave a statement cut off or saying less. When another note's arrival cuts a ring short, a
   statement in the way refuses the arrival; the release alone rides back to the new end, because
   it is the end's own statement and cutting it off would delete it.
5. **A statement that says nothing new is authoring state.** Visible and fully editable while its
   note is in focus; never saved, never in undo, gone when focus leaves. A release cannot be one
   (rule 1), so a release that an edit leaves saying nothing is removed by that same edit.
6. **A held run is a pure function of where it started and its net steps.** Every run inverts
   inside itself, step for step; nothing depends on the path taken to a position.
7. **A wall the charter can see is silent.** The refusal flash is for a key that did nothing for a
   reason the screen does not show.

Who owns what, in one line: **the end is the tail verb's and the release's; statements are the
move verb's; deleting is Delete's.** The asymmetry that prompted the question — bringing the tail
IN converts a slide to a slide-out, moving the landing OUT does not — is rule 2 and rule 3 each
doing their one job: becoming the release is something that happens to the END.

One seam is accepted rather than hidden: the release follows the end when the move verb or a
truncation moves it, and stays behind when the tail verb grows past it. The first is rule 4 (it
would otherwise be deleted), the second is rule 2 (the tail verb moves no statement), ruled
2026-09-10 after being re-opened as a toss-up.

**Set aside, with the reasons, so they are not re-proposed:**

| Shape | A statement moved onto the end… | Why not |
|---|---|---|
| Symmetric conversion | becomes the release, then drags the end | coherent under rule 6 — the ring ends at `max(start end, p)` — but the move verb then changes something it was not handed, and still needs the wall for statements that cannot convert |
| Push | pushes the end one step ahead of itself | moving a slide's landing changes the note's DURATION, which the legato hold test and the drawn tail read; with snap off the pushed tail is a one-tick sliver. The small upgrade if the wall proves obstructive in the hand |
| Ripple | carries every later statement and the end | a head move moves ONE object whose statements ride with it; statements are siblings, each pinned to its own musical moment. The useful half already exists: select the figure and it moves whole |

What is open against these rules today: rule 4's truncation half — a statement with a shake or a
bend standing exactly ON the landing is kept by the inclusive clip and then bared, with no refusal
and no test — and inserting a note, which truncates with no guard at all where the move verb has
`moveErasesStatement`.

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

### One fact to establish before ruling

Phase 3 builds bend authoring on this substrate. A bend that ramps to the END of a ring needs a
statement at the end. Today one offset holds one keyframe, and a fret at the end is the release and
sheds its bend — so a note that bends to its end AND falls away appears to be unwritable. Under a
stated release the two are separate data and both fit. If that reading is right it is an
expressiveness argument, not only a tidiness one, and it moves the decision ahead of Phase 3. It has
not been verified against the bend model and must be before this section is signed.

### Recommendation

**Narrower since the wall was ruled.** The ruleset above is most simply SAID positionally — "the
statement at the end is the release" — and under it rule 2's two conversions cost the positional
model nothing. With every route to the end guarded by a rule (the landing test, the wall, the
truncation refusal, the same-edit removal), the strip family stops being reachable from the editor
at all. What a stated release still buys is the deleted detach / re-attach dance and
skip-the-release exclusions, rule 4's truncation hole closed by construction rather than by a
guard — and the bend question above, which now decides it: rule 1 says nothing but a release may
stand at the end, and if a bend must, rule 1 cannot hold in a model with one statement per moment.

So: verify the bend fact first. If a bend needs the end, take the stated release, decided before
Phase 3 begins and built after the burst redesign. If it does not, the positional model stands, the
rules above are its guards, and the remaining work is rule 4's two open halves.

The stated release is the larger change and the only one here that touches the format, which is
why it is the user's call and not folded into a fix. Either way rule 4's open halves get built:
under the positional model, a truncation landing refuses through `releaseWouldStripChannels` and
the insert verb asks `moveErasesStatement`.

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

0. **Sign the ruleset.** Rule 3's wall is ruled; the seven as a set are not yet. Everything below
   is built to them, and they move into `chart-ruleset.md` once signed so there is one place they
   are stated.
1. **Now:** the stale-record fix and its regression tests (in flight). The tests outlive the fix.
2. **Next, no ruling needed beyond this document:** the burst redesign. It replaces the fix it
   follows and deletes more than it adds.
3. **Verify** the bend-at-the-end fact, then **rule** the release model.
4. If ruled in: the stated release, as one change set with its format, importer, test and doc
   halves, before Phase 3. If ruled out: the truncation-landing refusal alone.
