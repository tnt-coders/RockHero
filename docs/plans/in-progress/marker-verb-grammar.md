# The Marker Verb Grammar

*Status: LIVE for sections and tone changes as of 2026-09-13. Binding on every marker kind added
from here, including the four still RESERVED.*

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
  was just made.
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

`EditorController::Impl::markerGridPosition()` is the sole authority: the armed caret when one
exists, else the transport position quantised to the placement grid. A caret riding an automation
lane needs no branch of its own, because it IS an armed caret that names a lane — which the core
knew all along and the view had been reconstructing.

It is published as `EditorViewState::marker_grid_position`. `section_marker_downbeat` is the same
answer under the section's own snap, computed in the core beside it. One rule, two projections;
each verb applies its own quantum and nothing re-derives the rule.

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

The rig-activation step takes an OPTIONAL select id, where absent means *leave the selection
alone*. A split selects the region it just made; a restate keeps the region already pointed at; a
delete clears. Folding that choice into the activation step also raced the asynchronous rig reload,
which selects long after the verb returned.

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

One subtlety that change required: the new-tone arm rejects a duplicate NAME, and the outgoing tone
is pruned by this very repoint, so it must not count against the name. Otherwise resetting a lone
region to "Default" would collide with the entry it is replacing.

### The catalog prunes on every verb that can orphan a tone

A tone that loses its last reference leaves the catalog. The delete verb already did this; the
retone did not, so the two disagreed. Both now prune through `pruneUnreferencedTone`.

The justification is that an unreferenced entry is *already* unreachable: the picker is built from
the tones REGIONS reference, so a phantom entry keeps owning its name while nothing can offer it.
The retone memento carries both the entry it added and the entry it removed, so undo restores each.

### The picker

`showTonePicker` opens with its first row selected (`withInitiallySelectedItem(1)`; reuse rows are
numbered from 1, and with none of them the "New tone" id falls to 1 too). JUCE otherwise opens with
nothing selected, forcing an arrow press before the keyboard reaches anything.

A picker whose only row would be "New tone" is skipped entirely — the restate asks for the name
directly, the shortcut the insert path already took.

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

- **The section insert resolves its position when the prompt is ACCEPTED, not when the key is
  pressed.** With the transport rolling, a section lands where the playhead drifted to while the
  charter typed a name. The tone marker does not have this bug, because it captures the position at
  press time and its picker callback carries it. Recorded in `docs/tracking/backlog.md` with the
  fix spelled out; left out because it concerns *when* a position is read, not which verb a press
  means.
- **The sole-region delete's undo label** changed from "Reset Tone" to the retone wording, a
  consequence of deleting the reset memento.

## Verification

All five suites build; the two touched suites are green at each commit. At `cb33ca39` the editor
core suite runs 872 cases and the editor UI suite 218, including new coverage for the tone chord
selecting at a boundary, the caret demoting on tone selection, the retone minting and pruning with
a full undo/redo round trip, and Delete leaving nothing selected.

Still to SIGHT (task #298): the whole grammar end to end, on both marker kinds.
