# The Marker Verb Grammar

*Status: LIVE for sections and tone changes as of 2026-09-13, reviewed and simplified the same day
(see [The review pass](#the-review-pass-of-2026-09-13)). **The precedence was RE-RULED and BUILT
2026-09-14: every chord authors at the cursor and never reads the selection, and the core publishes
the VERB each chord would perform** — Phase 3 of `keyboard-focus-rows.md`, whose build record
names the fields. Binding on every marker kind added from here, including the four still
RESERVED. Decisions that were judgment calls rather than forced moves are collected under
[Open questions for review](#open-questions-for-review) at the end; they are the ones to push on.*

## The goal, stated first

**Every marker kind must answer its chord the same way.** A charter who learns one marker verb has
learned all of them, and a marker kind added later inherits the grammar instead of inventing one.

Concretely, `Ctrl`+letter is an AUTHORING verb, and it reads ONE precedence at the cursor, whatever
the marker is (ruled 2026-09-14):

1. a marker of that kind stands **EXACTLY at the cursor**, under the kind's quantum — the chord
   **RESTATES** it, reopening its payload;
2. else — the chord **INSERTS** one there.

**Built 2026-09-14.** The precedence is not restated on any surface: the CORE publishes, per kind,
the VERB the chord would perform at the cursor, and the UI only opens the prompt or picker that verb
names. A section chord target is rename-this / insert-here / nothing; the tone twin is retone-this /
split-here / nothing; `Enter`'s and `Ctrl+R`'s verbs are published the same way. "Nothing" is where
the playback and no-song gates live, stated once in the projection rather than in availability or in
five UI guards. Rule 4 (below) moved into the core with it: a restate can now reach a marker nobody
selected, so each restate verb selects its target after the commit.

The selection is never consulted. A marker selected elsewhere does not redirect the chord, and a
selected marker at the cursor is restated because it stands at the cursor, not because it is
selected. The selection has its own verbs, below.

"The cursor" is the armed caret, else the paused cursor: its trusted column, else the nearest
placement-grid slot, which is exactly where an arrow press would arm. While the transport plays the
chord is inert (ruled 2026-09-14): "the cursor" never means the rolling playhead.

**The whole marker plane is paused-only, not only the chords** (ruled 2026-09-14). While the
transport plays, marker SELECTION and every marker EDIT are unavailable — for every kind here, for
automation points, and through the pointer as much as the keyboard: the availability table refuses
each verb (`editor_action_availability.cpp`) and the tone strip will not even start a boundary drag
or an `Alt` insert. The tone designer is deliberately outside the rule: the plugin chain, plugin
parameters and the output gain stay live mid-play, which is the point of the live rig, and renaming
a tone DOCUMENT is not a marker edit.

And four rules ride along with it:

- **Selecting a marker disarms the armed caret**, demoted in place so the cursor line stays put.
- **`Enter` restates the selection; `Ctrl+R` renames it; `Delete` removes it; `Alt`+`←`/`→` moves
  it.** These are the shared selection verbs, reaching a new alternative rather than gaining a chord
  of their own. `Ctrl+R` (ruled 2026-09-14) acts only where the kind has a name — a section's own
  name, a tone region's TONE — and is inert elsewhere; it is a selection verb, not a marker chord, as
  R is no marker's letter. `Alt+←/→` moves a marker's START by the kind's step (a section a measure,
  a tone region's start a placement-quantum line), and a landed move brings the paused cursor to
  the new start so the edit is in view (built 2026-09-14). Proposed with it, not ruled: `Enter` on
  a selected tone region drills into that tone's signal chain once the chain has a keyboard model,
  leaving the region's retone to `Ctrl+T` at its start.
- **A verb that authors or restates a marker leaves it SELECTED**, so the next verb acts on what
  was just made — as a typed note is left selected. Confirmed 2026-09-13 with the focus rows
  (`keyboard-focus-rows.md`): the caret the chord was typed from demotes in place, and the next
  `←/→` re-arms it exactly where it stood. Since 2026-09-14 a restate can reach a marker that
  was NOT selected, so the core's restate verbs select their target after the commit, as the inserts
  already do — rule 4, now stated in the core rather than holding for free.
- **`Esc` drops the selection**, and `Delete` leaves nothing selected behind it.

**Superseded 2026-09-14.** The form built 2026-09-13 read three rules: a SELECTED marker is restated
wherever the cursor is; else a marker exactly at the cursor is SELECTED; else one is inserted — and
the cursor was the armed caret only. The select rule was then the keyboard's only route onto an
existing marker. The focus-row walk, `Tab` and the planned `Ctrl+Shift`+letter jumps
(`keyboard-focus-rows.md`) are now that route, and restating at the cursor is what keeps a chord
over an existing marker from dying silently or opening a prompt for an insert the core refuses. The
user's reason: `Ctrl`+letter authors, so a chord that ignored the cursor in favour of the selection
felt wrong. The pair law then holds literally — `Ctrl`+letter AUTHORS a kind, `Ctrl+Shift`+letter
SELECTS it.

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

### One cursor position, one authority

`EditorController::Impl::cursorPosition(quantum)` is the sole position authority: the armed caret's
position, else the paused cursor at the quantum asked for (its trusted column, else the nearest line
of that quantum), and nothing at all while the transport plays or with no song loaded. A caret
riding an automation lane needs no branch of its own, because it IS an armed caret that names a
lane — which the core knew all along and the view had been reconstructing.

- **Built 2026-09-13:** the armed caret, and nothing else (`markerGridPosition`). Until then the
  transport position, quantised to the placement grid, stood in when no caret was armed.
- **Built 2026-09-14:** the armed-aware helper above. `trustedCursorColumn`, `pausedCursorSlot` and
  `pausedCursorPosition` were one rule differing only in its quantum and became one
  `pausedCursorPosition(quantum)` (which also deleted `JumpChartCaret`'s inline restatement of it);
  making that ARMED-AWARE finished the fold, and it is what gives Phase 4c's planned `focusColumn()`
  one authority instead of a second.

Each verb applies its OWN quantum to it and nothing re-derives the rule: a chord reads the placement
quantum — the slot an arrow press would arm on — while a section and a meter read the TICK
(`g_tick_quantum_note_value`) and snap to the downbeat of the measure the cursor is IN, so a cursor
paused just before a barline does not round into the next measure. `EditorViewState`'s
`marker_grid_position` and `section_marker_downbeat` are deleted with the projection below: the view
needs no position at all, because it is handed a verb.

**Why the paused cursor is not the retired transport fallback.** The 2026-09-13 retirement answered
a marker landing a beat late off a ROLLING transport. This cursor is paused-only, and it is the
exact slot an arrow press would arm on, so a chord behaves as "arm, then press" with no rule of its
own; a caret demoted in place writes its exact slot into the column, so a second chord lands on the
first one's marker. Two gates are needed for correctness, stated once in the authority rather than
in availability, because the pointer inserts share the actions:

- **nothing while playing** — the view state is not pushed continuously, so a position sampled when
  play started would be stale for the whole playback;
- **nothing with no song** — the walker has not been checked against an empty tempo map.

Before the ruling, caret-only settled the playback question without a gate, because arming requires
a paused transport; the ruling moves that answer into the authority. The ruler's own menu inserts at
the CLICK's measure ("Insert Section Here"), the pointer form, so it needs neither gate.

### One precedence, published as a verb

The precedence lives in the CORE, once, and reaches each surface as the VERB that chord would
perform. `EditorViewState::section_chord_target` is nothing /
`RenameSectionTarget{position, name}` / `InsertSectionTarget{downbeat}`;
`EditorViewState::tone_chord_target` is nothing /
`RetoneRegionTarget{region_id, tone_document_ref}` /
`SplitToneRegionTarget{position, containing_tone_document_ref}`. The UI opens the prompt or picker
the variant names and decides nothing. `Enter`'s verb is published the same way as `restate_target`
(nothing / `RenameSectionTarget` / `RetoneRegionTarget` / `OpenAutomationPickerTarget`) and
`Ctrl+R`'s as `rename_target` (nothing / `RenameSectionTarget` /
`RenameToneTarget{tone_document_ref, name}`), so a new selection verb adds a projection rather than
a UI ladder arm.

- **Built 2026-09-13:** `performMarkerChord`, a file-local template in `editor_view.cpp`, taking the
  selected marker, the marker at the cursor, and three callables for restate / select / insert.
- **Built 2026-09-14:** that template is DELETED, together with `sectionAtMarker`,
  `toneRegionStartingAtMarker`, the containment scan in `createToneMarkerAt` and the UI's
  `project_loaded` guards. The design review found the precedence living in the UI over published
  lists, restating the core's "a marker stands exactly here" predicate in five places. "Nothing" is
  the playback and no-song gate, stated once in the projection. A kind with no payload still
  publishes a restate target: the no-op restate is what stops a duplicate insert on an occupied
  start.

Each kind's "exactly at the cursor" predicate is the core's, stated once where the verb is chosen:

- **Section** — a section whose position equals the downbeat of the measure the cursor is in.
- **Tone change** — a region whose `grid_start` equals the cursor's slot, because a region's
  start *is* the tone change that opens it. Anywhere else inside a region still splits.

### Selection is the caller's business, never the action's

Selecting a marker demotes the armed caret, in the core's one shared select (`selectMarker`, which
every section and tone-region select now calls directly) so the mouse gets it too. An armed caret is
where the next keystroke would author, so one standing beside a selected marker is a second answer
to the same question.

The rig-activation step never selects; the verb does, after its commit. A split selects the region
it made, onto an existing tone or a minted one alike; a restate keeps the region already pointed at;
a delete clears. Since 2026-09-14 a restate selects its target too, because the chord can restate a
marker nobody selected: `RenameSongSection` selects its section, `SetToneRegionTone`
always selects the surviving region (its `was_selected` guard is deleted). The sole-region reset
sits outside rule 4: it retones through the common-core primitive rather than through
`SetToneRegionTone`, and releases the selection first, so `Delete` still leaves nothing selected
without any ordering to keep. Selecting in the
view before the prompt would be wrong: a cancelled prompt would still demote the caret. The select id the activation step once carried is gone (2026-09-13): both
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

*Superseded: `resetSoleToneRegion` came back with the marker-commit funnel. Stating the reset as a
nested action made it depend on a deselect running before that action, and made a delete wear a
retone's undo label. It is a private body again — `ToneResetEdit` stays deleted, because it commits
through the one funnel like every other verb.*

### The catalog holds exactly the tones regions reference

A tone that loses its last reference leaves the catalog, on every tone verb alike, because every
tone verb commits through one funnel (`commitMarkerModel`) that normalizes before it validates, and
`ToneModelSnapshot::normalize` is that prune. The justification is that an unreferenced entry is
*already* unreachable: the picker is built from the tones REGIONS reference, so a phantom entry
keeps owning its name while nothing can offer it.

Pruning before checking is also what makes the duplicate-name rule one rule: a retone that mints a
replacement for the tone it drops replaces the very entry it is pruning, and with the prune already
done the name check needs no carve-out for the outgoing tone. The check itself is NOT one of the
funnel's rules — it is input validation on a TYPED name, so the three verbs that take one run it
(`reportedDuplicateToneName`, which normalizes first for exactly that reason) and report it to the
charter, who can retype.

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

Six tone edit classes with hand-written inverses became one whole-model memento carrying the tone
model (catalog plus track) before and after, the shape the section list already had. Whole-model
because a merge can take any number of regions with it, and an inverse command would have to know
each one; a handful of small structs costs nothing to copy, and the round trip is exact by
assignment.

*Generalized since: the two whole-model mementos became ONE `MarkerModelEdit<Snapshot>` over
`ToneModelSnapshot` and `SongSectionsSnapshot`, and the two commit helpers became one funnel,
`commitMarkerModel`. It normalizes the produced model, validates it, refuses it whole, records
nothing when nothing changed, releases a selection naming a marker that is gone, resyncs the
audible tone and publishes once. There is no restore-on-refusal any more: a verb builds its result
on a COPY of the model it captured and never touches the live one, so a refusal costs nothing by
construction. Unique tone names left the funnel with it — see the catalog section above.*

The `NewTone` arm mints its document before the commit, so a name collision leaves an orphan
file — kept and collected at the song export, as every removed tone's is.

### The section chord

`Enter`, `Ctrl+M` and, since 2026-09-14, `Ctrl+R` restate a section through one member (then
`restateSongSection`, now `EditorView::renameSection` over the published `RenameSectionTarget`), so
the three cannot drift. The section insert now captures its downbeat AT THE PRESS and carries it through the
prompt (`InsertSongSection` gained a position), the shape the tone insert already had; the
recorded drift is fixed and its backlog entry removed.

## What a NEW marker kind must do

The four reserved kinds — tempo anchor (`Ctrl+B`), meter (`Ctrl+/`), position marker (`Ctrl+P`),
span marker (`Ctrl+H`) — inherit all of the above. Building one means:

1. **Publish the kind's CHORD TARGET beside the section and tone targets** — a variant of
   nothing / restate-this / insert-here, chosen in the core under the kind's own quantum — and its
   restate and rename targets too if it has a name. Do not write the precedence again anywhere
   else, and never re-derive the cursor on a surface: read `cursorPosition(quantum)` in the core
   and hand the UI a verb.
2. In the UI, open exactly what the published verb names. The surface decides nothing, and holds no
   position, predicate or gate of its own.
3. Select through `selectMarker`, in the core, so the mouse and the keyboard's focus rows share
   one select that demotes the caret and re-syncs the rig. A kind with its own ruler or track row
   joins `MarkerRow` (`markerStarts`, `selectedMarker`, `markerSelectionAt`) and the focus-row
   stack (`docs/plans/in-progress/keyboard-focus-rows.md`); the tempo anchor and the meter already
   have selectable chips there, awaiting their verbs.
4. Add the kind to `RestateSelection`'s dispatch, to `RenameSelection`'s where it has a name, to
   `Delete`'s and to `MoveSelection`'s, all of which switch on the selection's kind; a landed move
   ends with `followMovedMarker(start)`.
5. Select the marker in the core after authoring or restating it, including a restate of a marker
   that was not selected.
6. If the kind has no payload, its restate is a no-op that only selects — still needed, because it
   is what stops a duplicate insert on an occupied start.
7. Commit through `commitMarkerModel` (`marker_model_commit.h`) and nothing else, by supplying a
   snapshot type with `capture` / `applyTo` / `normalize` / `validate` / equality. The verb builds
   its result on a COPY of the captured model and never touches the live one, so a refusal costs
   nothing; the funnel owns the undo entry, the refusal log, the release of a selection naming a
   marker that is gone, and the publish. A verb must not call `updateView()` itself except to
   publish a selection it makes AFTER a landed commit.

The grammar is stated for readers in `docs/plans/in-progress/keymap-matrix.md`, in the Markers
section intro, which was updated in `cb33ca39` from the old two-move form.

## Not done, deliberately

- **The sole-region delete's undo label.** It read "Reset Tone", then briefly took the retone's
  wording while the reset was stated as a nested retone; since the marker-commit funnel landed it
  is its own verb (`resetSoleToneRegion`) again and says "Delete Tone Region", which is the verb
  the charter pressed.
- **Undo still does not resync the audible tone.** Every verb's forward path now does, through the
  commit helper; the undo path is blocked on the live-rig test fake, as the backlog entry records.

## The playback question

*Historical: the recommendation below was overruled 2026-09-13 (open question 1), and the
2026-09-14 author-at-cursor ruling keeps that answer (ruled the same day) through an explicit gate
in the chord's verb projection instead of the armed-implies-paused invariant. The later ruling
went further than either: the whole marker plane is paused-only, selection included, so the "what
stays reachable is the workflow" reading below describes the shipped editor only up to that date.*

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
   load-bearing. **2026-09-14:** the author-at-cursor ruling no longer needs a caret, and the
   answer stays "no" (ruled the same day), enforced by a playing gate in `cursorPosition(quantum)`,
   which publishes the chord's verb as "nothing" while the transport rolls.
   **Widened the same day:** the answer is now the whole marker plane's, not the chords' — marker
   selection and every marker edit are unavailable while playing, enforced in the availability
   table, with the tone designer excluded.
2. **The catalog prunes a tone that loses its last reference, on every tone verb.** An
   unreferenced tone is already unofferable because the picker is built from tones regions
   reference. The cost: retoning away from a tone destroys its chain, recoverable only by undo
   (which restores the whole model, catalog included). Delete had the same property, but a delete
   is a more deliberate act than a repoint.
3. **A marker exactly at the cursor is RESTATED rather than refused** (re-ruled 2026-09-14; from
   2026-09-13 it was SELECTED, then the keyboard's only route onto an existing marker). The first
   tone region's start is included deliberately, though it is a song boundary rather than an
   authored change, because restating it repoints the opening tone and an insert there would be a
   zero-width split the core refuses.
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
