# The Marker Verb Grammar

*Status: LIVE for sections and tone changes as of 2026-09-13, reviewed and simplified the same day
(see [The review pass](#the-review-pass-of-2026-09-13)). Binding on every marker kind added from
here, including the four still RESERVED. Decisions that were judgment calls rather than forced
moves are collected under [Open questions for review](#open-questions-for-review) at the end; they
are the ones to push on.*

## The goal, stated first

**Every marker kind must answer its chord the same way.** A charter who learns one marker verb has
learned all of them, and a marker kind added later inherits the grammar instead of inventing one.

Concretely, `Ctrl`+letter reads ONE precedence, whatever the marker is:

1. a marker of that kind is **SELECTED** — the chord **RESTATES** it, reopening its payload;
2. else a marker of that kind stands **EXACTLY at the cursor** — the chord **SELECTS** it;
3. else — the chord **INSERTS** one there.

And four rules ride along with it:

- **Selecting a marker disarms the armed caret**, demoted in place so the cursor line stays put.
- **`Enter` restates the selection; `Delete` removes it; `Alt`+`←`/`→` moves it.** These are the
  shared selection verbs, reaching a new alternative rather than gaining a chord of their own.
- **A verb that authors or replaces a marker leaves it SELECTED**, so the next verb acts on what
  was just made — as a typed note is left selected. Confirmed 2026-09-13 with the focus rows
  (`keyboard-focus-rows.md`): the caret the chord was typed from demotes in place, and the next
  `←/→` re-arms it exactly where it stood.
- **`Esc` drops the selection**, and `Delete` leaves nothing selected behind it.

Rule 2 is the load-bearing one. It is what makes a marker reachable from the keyboard at all: with
no way to select an existing marker, `Enter` and `Delete` had nothing to act on, so renaming or
removing one always needed the mouse.

## What was wrong

Three separate defects, all the same shape — a rule stated twice, in two places that disagreed.

- **The chord refused where a marker already stood.** Pressing it over an existing marker was
  declined, so the keyboard could never reach one. For the tone chord this was worse than a
  refusal: the splitter looks for the region a position falls *strictly inside*, a boundary falls
  inside none, so the press silently did nothing at all.
- **The marker POSITION rule existed twice and the copies disagreed.** The core read the armed
  caret's exact grid position. The view rebuilt an answer from published *seconds*, through a
  lane-caret branch, a chart-caret branch, and a requantising round trip its own comment described
  as lossy.
- **MINT-AND-REPOINT existed twice.** `resetSoleToneRegion` minted a fresh tone and repointed the
  last region at it; the restate could not mint at all, so authoring a tone in place was impossible
  and the picker often opened with nothing in it.

## What was committed

| Commit | What it did |
|---|---|
| `e7c449d4` | A SELECTED section chip outranks the cursor, so the chord restates it wherever the cursor is. Establishes rule 1. |
| `449bc4a1` | The tone chord takes the section's grammar. Adds rule 2 for both kinds, generalises `Enter` to `RestateSelection` (`0x1404`), and makes selecting a tone region disarm the caret. Publishes ONE marker position from the core and deletes the view's reconstruction. |
| `cb33ca39` | The retone MINTS, so one verb covers every way a region's tone changes. Deletes the sole-region reset path, prunes the catalog on retone, leaves nothing selected after Delete, and fixes the picker's focus and its one-row case. |

Earlier in the same sequence, `dd81a395` made the section chord positional and `ff6b3981` gave
`Enter` its own command.

## The design

### One marker position, published once

`EditorController::Impl::markerGridPosition()` is the sole authority: the armed caret, and nothing
else (ruled 2026-09-13; until then the transport position, quantised to the placement grid, stood
in when no caret was armed). A caret riding an automation lane needs no branch of its own, because
it IS an armed caret that names a lane — which the core knew all along and the view had been
reconstructing.

It is published as `EditorViewState::marker_grid_position`, an optional: no caret, no marker, and
the chords' positional halves are inert while a selected marker still restates.
`section_marker_downbeat` is the same answer under the section's own snap, computed in the core
beside it. One rule, two projections; each verb applies its own quantum and nothing re-derives the
rule.

Caret-only is what settles the playback question below without a gate: arming requires a paused
transport, so no marker verb is reachable while playing, and a marker lands exactly where the
charter placed the caret rather than a beat late off a moving cursor. The cost is that a press with
the transport parked but no caret armed (a ruler click seeks without arming) does nothing; if that
bites in sighting, the remedy is to make the parking gesture arm the caret, never to restore the
fallback. The ruler's own menu inserts at the CLICK's measure ("Insert Section Here"), the pointer
form, so it needs no caret.

### One precedence, shared by both chords

`performMarkerChord` (a file-local template in `editor_view.cpp`) takes the selected marker, the
marker at the cursor, and three callables for restate / select / insert. Both chords call it. The
precedence IS the rule, and stating it twice would guarantee eventual disagreement.

Each kind supplies its own "exactly at the cursor" predicate:

- **Section** — a section whose position equals the marker's measure downbeat.
- **Tone change** — a region whose `grid_start` equals the marker position, because a region's
  start *is* the tone change that opens it. Anywhere else inside a region still splits.

### Selection is the caller's business, never the action's

Selecting a marker demotes the armed caret, in the core's shared selection paths
(`applySongSectionSelection`, `applyToneSelection`) so the mouse gets it too. An armed caret is
where the next keystroke would author, so one standing beside a selected marker is a second answer
to the same question.

The rig-activation step never selects; the verb does, after its commit. A split selects the region
it made, onto an existing tone or a minted one alike; a restate keeps the region already pointed at;
a delete clears. The select id the activation step once carried is gone (2026-09-13): both
activation paths now end by pointing the rig at the active tone, which was the other thing that id
was doing, and folding a selection choice into the step raced the asynchronous rig reload besides.

### One retone, whose target may not exist yet

`EditorAction::SetToneRegionTone` carries a `RetoneTarget`, a sum type of `ExistingTone{ref}` or
`NewTone{name}`. The two arms differ *solely* in whether the tone exists yet, so minting belongs to
the retone rather than to an action of its own: it is one change to one region, and therefore one
undo entry.

This is why no new action id was added, and why none of the nine exhaustive switches over
`EditorActionId` moved.

It also made `resetSoleToneRegion` redundant. Delete on the LAST region cannot merge, because the
track must cover the whole song, so it resets instead — which is exactly a retone onto a new tone
named "Default". The sole-region branch now says that, and `resetSoleToneRegion` and `ToneResetEdit`
are both deleted.

### The catalog holds exactly the tones regions reference

A tone that loses its last reference leaves the catalog, on every tone verb alike, because every
tone verb commits through one helper (`commitToneModel`) that prunes before it validates. The
justification is that an unreferenced entry is *already* unreachable: the picker is built from the
tones REGIONS reference, so a phantom entry keeps owning its name while nothing can offer it.

Pruning before validating is also what makes the duplicate-name rule one rule: resetting a lone
region to "Default" replaces the very entry it is pruning, and with the prune already done the
name check needs no carve-out for the outgoing tone.

### The picker

`showTonePicker` opens with its first row selected (`withInitiallySelectedItem(1)`; reuse rows are
numbered from 1, and with none of them the "New tone" id falls to 1 too). JUCE otherwise opens with
nothing selected, forcing an arrow press before the keyboard reaches anything.

A picker whose only row would be "New tone" is skipped entirely — the restate asks for the name
directly, the shortcut the insert path already took.

Each verb's picker leaves out exactly ONE tone: the one that would make the verb a no-op (the
region being restated already sounds it; the region being split already sounds it). A neighbour's
tone is offered, and choosing it merges — see the review pass below.

## The review pass of 2026-09-13

A review of the grammar as shipped found the two remaining bugs were one missing law, and that the
law made most of the tone-edit machinery redundant.

### A region boundary IS a tone change

The reported bugs: restating a region did not offer the tone of the region after it, and deleting
the middle of TONE1, TONE2, TONE1 left two adjacent TONE1 regions. Both are the same absence: the
model could hold a boundary that changed nothing. The picker hid the neighbours' tones, and the
retone refused them, precisely because the model could not merge — a rule stated three times
(picker, retone, nowhere for delete) to cover for a law stated nowhere.

The law now lives in one function, `coalesceToneRegions` in common core: a region whose tone
equals its predecessor's is removed, the earlier region keeps its id and start. Every edit
primitive ends there, and the package reader runs it once on load. So:

- **Retone onto a neighbour's tone merges.** Onto the previous tone, the region vanishes into its
  predecessor; onto the next, the next vanishes into it. The selection follows the region holding
  the retoned start, so `Enter` and `Delete` still act on what the charter made.
- **Delete merges the neighbours it brings together.**
- **Insert with the next region's tone pulls that tone back to the marker**; insert with the
  containing region's tone changes nothing and records nothing.

### A region stores only its start

The format already persisted only starts; the in-memory `ToneRegion` carried an `end` derived at
load and then maintained by hand in four edits and two undo paths, while the schedule ignored it
and read the next start anyway — one datum, two derivations. `end` is gone. A region ends where
the next begins (`toneRegionEnd` in the projection derives it for the view). Gaps, overlaps and
empty regions are now unrepresentable; the rules validate strictly ascending starts and that the
first region starts at the song's first downbeat, which every consumer already assumed.

The four edits are now four common-core primitives — `createToneRegion`, `deleteToneRegion`,
`retoneToneRegion`, `moveToneBoundary` — so the whole edit vocabulary lives beside the type.

### One memento, one commit

Six tone edit classes with hand-written inverses became one `ToneModelEdit` carrying the tone
model (catalog plus track) whole before and after, the shape `SongSectionsEdit` already had.
Whole-model because a merge can take any number of regions with it, and an inverse command would
have to know each one; a handful of small structs costs nothing to copy, and the round trip is
exact by assignment.

Every verb commits through `commitToneModel`: prune unreferenced tones, validate (track rules,
every reference in the catalog, unique names), restore the before-state whole on refusal, record
nothing when nothing changed, release a selection naming a region that is gone, resync the
audible tone, publish. The `NewTone` arm mints its document before the commit, so a name
collision leaves an orphan file — kept and collected at publish, as every removed tone's is.

### The section chord

`Enter` and `Ctrl+M` restate a section through one member (`restateSongSection`), so the two
cannot drift. The section insert now captures its downbeat AT THE PRESS and carries it through the
prompt (`InsertSongSection` gained a position), the shape the tone insert already had; the
recorded drift is fixed and its backlog entry removed.

## What a NEW marker kind must do

The four reserved kinds — tempo anchor (`Ctrl+B`), meter (`Ctrl+/`), position marker (`Ctrl+P`),
span marker (`Ctrl+H`) — inherit all of the above. Building one means:

1. Call `performMarkerChord` from the chord's case. Do not write the precedence again.
2. Supply a "marker exactly at `marker_grid_position`" predicate, under the kind's own quantum.
3. Read the published marker position. Never re-derive caret-or-transport on a surface.
4. Make the kind's selection path demote the caret, in the core, so the mouse shares it.
5. Add the kind to `RestateSelection`'s dispatch and to `Delete`'s, both of which switch on the
   selection's kind.
6. Leave the marker selected after authoring or replacing it.
7. If the kind has no payload, rule 1 simply does nothing for it.

The grammar is stated for readers in `docs/plans/in-progress/keymap-matrix.md`, in the Markers
section intro, which was updated in `cb33ca39` from the old two-move form.

## Not done, deliberately

- **The sole-region delete's undo label** changed from "Reset Tone" to the retone wording, a
  consequence of deleting the reset memento.
- **Undo still does not resync the audible tone.** Every verb's forward path now does, through the
  commit helper; the undo path is blocked on the live-rig test fake, as the backlog entry records.

## The playback question

Raised while reviewing the drift above: if authoring during playback is what exposes it, why not
simply disable editing while the transport rolls?

**Most of that lockout already exists, structurally.** Arming a caret REQUIRES a paused transport,
so while playing there is no armed caret at all and every caret-based verb is already unreachable.
The only actions carrying an explicit playing check are `StepChartCaret`, `JumpChartCaret` and
`ExtendTimeSelection`, and the comment beside them gives exactly that reason.

**What stays reachable is the workflow, not an oversight.** Only verbs that read the TRANSPORT as
their marker survive playback, which is the two marker chords. While rolling, the transport is the
only thing "the cursor" can mean, so those chords ARE the listen-and-drop-a-marker workflow: play
the song, hear the chorus arrive, press the key, and the downbeat snap forgives a late reaction
inside the measure. Disabling editing during playback deletes that, and it is the main reason the
marker rule consults the transport at all.

**The drift is not really about playback.** It is one verb re-reading its position after a modal
prompt closes. The tone insert does the same job correctly, capturing at the press and carrying the
position through its picker callback. The exposure is a single site, not a class of workflow.

**The rule proposed instead:** a verb captures its position AT THE PRESS and never re-reads it.
That keeps the workflow, fixes the section insert, and immunises any future verb that puts a prompt
between the key and the effect. It is the shape the rename verbs already have.

**A middle option, named honestly:** pausing the transport when a modal opens would also fix it.
Not chosen here, because it is more surprising to the charter and does nothing for a verb that
defers for some other reason.

This is a recommendation, not a ruling.

## Open questions for review

Each of these was a judgment call. The forced moves are not listed; these are.

1. **Editing during playback — RULED 2026-09-13: no.** The marker is the armed caret and nothing
   else, so the chords are unreachable while playing by the existing armed-implies-paused
   invariant. The opposing case, that a moving cursor makes "at the cursor" ambiguous by nature,
   won: editing needs precision, and a late press off a rolling transport lands a tone change a
   beat off. The capture-at-press discipline stays as the prompt's shape but is no longer
   load-bearing.
2. **The catalog prunes a tone that loses its last reference, on every tone verb.** An
   unreferenced tone is already unofferable because the picker is built from tones regions
   reference. The cost: retoning away from a tone destroys its chain, recoverable only by undo
   (which restores the whole model, catalog included). Delete had the same property, but a delete
   is a more deliberate act than a repoint.
3. **A marker exactly at the cursor SELECTS rather than being refused.** This is what gives the
   keyboard its only route onto an existing marker. The first tone region's start is included
   deliberately, though it is a song boundary rather than an authored change, because restating it
   repoints the opening tone and an insert there would be a zero-width split the core refuses.
4. **Minting lives inside the retone rather than in an action of its own.** That kept one undo
   entry and avoided a new action id along with its exhaustive switches. The counter-case: one
   action now has two shapes, and a sum-typed payload is a branch by another name.
5. **Delete leaves nothing selected, but a merging retone selects the survivor.** Delete's target
   is gone and nothing inherits it. A retone that merges the selected region into its predecessor
   selects that predecessor, on the grounds that it now holds what the charter just made. The
   opposing reading: the marker the charter pointed at was dissolved, so nothing should stay
   selected there either, exactly as after Delete.
6. **The sole-region delete's undo label** became the retone wording when `ToneResetEdit` was
   deleted. A dedicated label could be restored, at the cost of a field existing only to carry a
   string.
7. **The reader coalesces rather than refuses.** A package holding two consecutive changes on
   one tone (which the delete bug used to write) loads as one region instead of failing. A
   redundant change carries no information, so nothing is lost; the alternative was a load error
   naming a defect the reader can state away in one call.
8. **The first region must start at `1:1`, validated on load.** Every consumer already treated
   the first region as owning the lead-in from the origin, so the stored start was dead data
   unless it said so. A package with a late first change now fails to load; before, it loaded and
   was drawn as if it started at the origin anyway.

Answered by the review pass: the old question 7 (the section insert's late position) is fixed,
and the picker no longer hides neighbours' tones.

## Verification

All five suites build; the two touched suites are green at each commit. At `cb33ca39` the editor
core suite runs 872 cases and the editor UI suite 218, including new coverage for the tone chord
selecting at a boundary, the caret demoting on tone selection, the retone minting and pruning with
a full undo/redo round trip, and Delete leaving nothing selected.

Still to SIGHT (task #298): the whole grammar end to end, on both marker kinds.
