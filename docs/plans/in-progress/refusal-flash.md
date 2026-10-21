# Refusal flash (task #278)

Status: BUILT 2026-09-30, awaiting its sighting (F1, F2); rebuilt 2026-10-20 to one rule for every
refusal on every surface (see "One rule for every refusal"). Written 2026-09-21 from a recovered
discussion — see "Provenance" — because the ruling had survived in one conversation only, and task
#278 had gone on describing the design the ruling replaced.

When an edit key is refused, the editor says so **on the things it concerned**: the thing the key
tried to change and the thing that stood in its way glow red a couple of times and stay as they
were. Why it was refused goes to the log, and while the flash runs the tab lane's status line says
it too (ruled 2026-10-20, "The ruling" below). There is no toast and no caret callout — that was
the proposal on the table, and the flash was chosen instead of it.

Every claim about current code below was verified on 2026-09-21, and re-verified against the code
on 2026-10-21 from "Shape, as built" down. Re-verify before acting on one.

## What was ruled

The user, 2026-08-28, verbatim:

> For general refusal of an action like this I would think just making every selection where the
> action was refused flash red a couple times and then remain unchanged would probably be a good
> level of feedback to the end user to show that the action was rejected. The reason for the
> rejection could probably be written to the log. [...] Perhaps it could make the selection ring
> flash red? Maybe flash twice or 3 times? Maybe the flash should be more like a glow that fades in
> and out so it looks kind of like a red light turning on and off? This is definitely a bit more
> complex though but I think that may be the CORRECT direction to generalize this and give the user
> the proper feedback needed.

So, ruled:

- **The surface is the refused selection itself**, per element. `L` over ten notes that applies to
  seven flashes the three it skipped — the flash says WHICH, at the gaze, which no count in a
  corner can.
- **Red, pulsed a couple of times, then unchanged.** The preferred form is a glow that fades in and
  out, a red light turning on and off, over a hard blink.
- **The reason goes to the log**, not the screen. (Since 2026-10-20 the tab lane's status line
  shows it too, "The ruling" below.)
- **One mechanism for every verb.** No per-verb feedback design.

**A refusal lights what it names; the reason goes to the log** (user, 2026-10-20). What a refusal
lights is decided where it refuses, never defaulted by the mechanism, and a refusal naming one
missed stop of several would send the charter to fix one and meet the next, so a coverage refusal
names them all. Its first build lit a culprit INSTEAD of the selection, a hold as the notes
sounding in it, and nothing for a marker funnel's refusal; the design session the same day
replaced that with "One rule for every refusal" (below), which keeps the decide-where-it-refuses
half and lights the subject beside the culprit.

Accepted in the same exchange without objection, but the assistant's words and not the user's: the
flash fires on a real refusal only, never on an honest no-op (`PlanRefused`, never
`PlanNoChange`); a fading glow modulates luminance, so the signal does not rest on hue alone; and if
sighting ever shows charters not finding the why, the log line is the thing to surface — not a
redesign.

**A press that moves nothing is a refusal** (user, 2026-10-20, reversing "a visible bound is not
a refusal", 2026-09-21). A key press that moves nothing because a bound held it IS a refusal: it
flashes the thing the key tried to change and the thing that stood in its way, and the log carries
the sentence. The hold-edge keys already behaved so (`planSteppedShapeHoldEdge` refuses "no note
boundary or grid line later" and lights the hold), and the verbs that step a ring's end must agree
with them: a ring grown against the next onset on its string lights that onset, one whose shrink
its last keyframe stops lights that keyframe, one its onset or a scrape's shortest window holds
lights the note alone (`planAdjustSustain`); a slide-out the move verb parked on the next head
lights that head, and a move earlier from the song's start or later from its end lights the
selection alone, the edge being nothing on screen (`planMoveGesture`). A chord member pinned beside a moving one is no refusal — the press
moved something, and the step is recorded. A refused step is never recorded, so a refused press
banks no overshoot to pay back; a press a clamp absorbed only part of still does (backlog). The earlier ruling's line — the flash is for a key that did nothing for a
reason the screen does not already show — goes with it: a bound the charter can see flashes too.
This re-settles roadmap 40-Q5: the slide tail lock is a lit floor, not a silent one.

**Never said by anyone**, and not to be reconstructed as if it had been: pulse timing, the choice
between two pulses and three, the drawn geometry (the "selection ring" was a *perhaps*), a color
token, and behaviour under key repeat. (A 3D treatment was ruled 2026-10-20: none — see F4.)

## What it replaces, and what dissolves

Task #278 read "the one non-modal notice channel (status bar / toast)" and the release plan called
four payloads "built and waiting" for it. Neither survives:

- **The payloads are one type now.** Every per-note verb — legato and every technique write —
  returns `ChartSelectionPlan{plan, refused}` (`rock-hero-editor/core/src/chart/chart_edits.h`):
  the whole-plan answer beside each note it could not write, with its reason. It replaced
  `ChartLegatoPlan` and its skip enum. A whole-plan refusal carries its reason too
  (`PlanRefused::reason`, the rule's own message where the gate refused), so the slide-tail
  clip's refusal and every other gate refusal reach the log with their words.
- **The count-plus-dominant-reason shape was the wrong shape for a flash.** A flash needs the
  refused elements' identities, so the planner returns them and the count is the list's size. One
  datum, stored once.
- **§9a's "applied to 7 of 8" sentence has no screen to live on** and needs none: the seven changed
  and the one glowed. "Never silent partial application" is met by the flash. The sentence becomes
  the log line.
- **A whole-plan refusal has no per-note attribution** (`validateChartNotes` names a rule, not a
  note; relational refusals belong to a pair). The ruling already covers it: "every selection where
  the action was refused" — when the whole plan is refused, the whole selection flashes.

## Shape, as built

Built 2026-09-30 for the tab lane and generalized 2026-10-20 to every surface; the light itself —
what each refusal lights and how it reaches the screen — is under "One rule for every refusal".

1. **Core: refusals carry reasons and identities.** `PlanRefusal` is
   `variant<PlanNoChange, PlanRefused>` (`rock-hero-editor/core/src/controller/refusal_light.h`),
   and only the refusal carries a reason and a light, both stated where it refuses. The per-note
   verbs return `ChartSelectionPlan`, whose `refused` names each note they could not write
   (`ChartRefusedNote`). In `planNoteWrite` a verb's write either refuses a note with a reason (the
   harmonic's "no node is reachable") or leaves the no-op test to find "already so", so a no-op is
   never a refusal; a note the per-note rules refuse is named with the rule's message.
2. **Controller: the report follows the apply.** `applyChartEditPlan` reports a refused plan, and
   its per-note overload reports the refused notes AFTER applying the rest, because the flash is
   addressed in the projection the apply published.
3. **Port: a one-shot effect**, `IEditorView::flashRefusal(RefusalFlash)`, not view state. The test
   fake records each flash, which is what controller tests assert.
4. **View: one glow and one pulse on every 2D surface.** A glow in `EditorTheme::invalid` spreading
   outward from each refused mark's outline (the outline the selection ring strokes), clipped
   outside the mark, at the flash's level (`paintRefusalGlow`, `ui/src/shared/flash_pulse.h`); each
   surface's frame attachment steps it, the newest refusal replaces the running flash and restarts
   its clock (`startRefusalFlash`), and a new projection drops it. The level is
   `cos²(π·(pulses − ½)·t/T)`: lit at the keystroke, dark between pulses and at the end. The 3D
   preview glows nothing (F4).
5. **The log, and the lane line.** The Quill-backed `RH_LOG_*` facade, at
   `%APPDATA%/Rock Hero/Rock Hero Editor.log`, every record written by `RefusalLog`
   (`shared/refusal_log.h`). The record's sentence also shows on the tab lane's status line while
   the flash runs ("The ruling" below). The app still has no way to open the log itself (F5): an
   "Open Log" entry is small and the user's to call.

**Deliberately silent:** an honest `PlanNoChange` — a first press that changes nothing, a gesture
stepped back to its start; a press inside a gesture that its planner cannot tell from the run's own
plan, which the gesture authority drops unrecorded (`commitChartGestureStep`: the harmonic picker's
row already in force chosen again, a string step over keyframes alone, a step on a finer lattice
that only walks back over what an earlier press's clamp absorbed); and a legato press that
CLEARED, whose notes' refusals to set were never asked for. A held-key gesture's step
reports every refusal it meets, like any other key.

## Open — the user's to rule

| # | Question | Recommendation |
|---|---|---|
| F1 | Pulse count and timing | BUILT at two pulses over 0.6 s (`g_refusal_flash_train` in `shared/flash_pulse.h`); sight and tune |
| F2 | Drawn geometry: the selection ring turning red, or a glow around it | BUILT as the ring turning red, then made a GLOW (user, 2026-09-30: wider, closer to the accent's halo): reach 0.2 of the mark's size, the accent halo's, after 0.45 sighted too wide; four layers (`g_refusal_glow_*`, `shared/flash_pulse.cpp`), drawn over the caret square (user, 2026-09-30) |
| ~~F3~~ | ~~Key repeat~~ | BUILT, and made one rule for every surface 2026-10-20: the newest refusal replaces the running flash — what it lights, what it says, and its clock (`startRefusalFlash`) — so a held key that keeps being refused reads as a steady glow that pulses out once the refusals stop |
| ~~F8~~ | ~~The fret shift at the neck's edge and a move off the neck flash~~ | Settled by "A press that moves nothing is a refusal" (2026-10-20): a press a bound held flashes, so both stay as built |
| ~~F9~~ | ~~The move gesture's visible bounds flash beside its real refusals~~ | Settled the same way: a visible bound is a refusal, so every one of them flashes as built |
| F10 | A keyframe a per-note rule refuses flashes its NOTE's head, since the rule authority names notes, not keyframes | Sight it; naming the keyframe needs the rule authority to name one |
| F11 | A legato press that CLEARS flashes nothing, even where some selected notes could not have been claimed | Built silent: the press cleared, which never asked those notes to be set; the ruling's "applies to seven, flashes the three" reads either way for a clear |
| ~~F4~~ | ~~2D only?~~ | RULED 2026-09-21: yes, 2D only — and re-ruled so on 2026-10-20, after a preview glow of spans' rails and positions' floor numbers was built and removed: the flash is a 2D editing-surface affordance, and unless the preview can light every target it lights none; it cannot light note heads or none holds |
| ~~F5~~ | ~~Reason to the log ONLY~~ | RULED 2026-09-21: yes, "for now" — the log line is the thing to surface if charters cannot find the why. Surfaced 2026-10-20: the sentence shows on the tab lane's status line ("The ruling"); an "Open Log" entry stays small and the user's to call |
| ~~F6~~ | ~~Scope of "every selection"~~ | RULED 2026-09-21: notes and keyframes with #278; a marker verb gains it when it first has a refusal to report. The first is the Tempo Lock's refused anchor move (plan 66 Phase 1, 2026-10-04): the tempo chip glows on the ruler, on the same pulse and glow, which moved to `ui/src/shared/flash_pulse.h` (named `refusal_flash.h` until plan 65 Phase 3 gave its pulse a second, non-refusal use) so both surfaces share them. Since 2026-10-20 every chip of every marker kind is a target, through the one `IEditorView::flashRefusal` |
| ~~F7~~ | ~~40-Q5~~ | RULED 2026-09-21 a silent floor; RE-RULED 2026-10-20: a lit floor — the shrink a last keyframe holds flashes the note and that keyframe (see "A press that moves nothing is a refusal" above). The lock itself was ruled 2026-08-09 |

**A second consumer, RULED 2026-09-21 (user), STRUCK 2026-09-30 (user).** An `Alt` digit or
`Alt+Insert` at a ring's exact end naming the fret already in force was to flash, because it
planted a statement that said nothing. Since `09d34cf6` (2026-09-22) that point draws its chip, so
it can be seen, selected, retyped and deleted, and `planInsertKeyframe` applies it: the screen now
shows what the key did, which is the flash's own line for silence. A third consumer since
2026-09-23, built: `Insert` cutting a scrape, which refuses with no box of its own.

**Legato's "nothing earlier to connect to" flashes (user, 2026-09-30).** It is the commonest
refused press, but a key that does nothing visible is the case the flash exists for.

One unreconciled line from 2026-09-05 suggested the flash could "hint `Shift+S` as the verb they
actually want" — on-screen text, which contradicts F5. It was never put to the user and is not part
of the design.

## Provenance

The discussion is one exchange, 2026-08-28, in session `ee6f4455-2b03-47c3-95ff-52031ad22341`. It
was deferred by the user until the derivation package landed, the assistant said it was recorded,
and it was not: the task store kept the pre-ruling wording. Recovered from the transcript on
2026-09-21. A parallel design review run the same day, before the recovery, recommended a fixed
text line under the transport; it is superseded by the ruling and kept out of this plan.

## One rule for every refusal (ruled 2026-10-20)

Built in `bdc466c4`, `03a4ec54`, `38154b3e`, `fc2fd3b3` and `05edcc5f`, and since; everything below
was re-read against the code on 2026-10-21.

### The ruling

> A refusal flashes the thing you tried to change and the thing that stood in its way, and the log
> carries the sentence.

- **Where they are the same thing, it flashes once.** A refusal naming the very note, chip or hold
  the key acted on glows that one mark, never twice and never in a second style.
- **Where nothing stood in the way, only the subject flashes.**
- **Where there is no subject on screen** — a prompt's text, no chart loaded — **only the
  sentence speaks**, in the log and on the lane line.
- **Every authored thing on the timeline is a target:** notes and keyframes; every chip — the
  fret-hand position, the tempo anchor, the time signature, the section, the tone region; and the
  span and none-hold bars (in 2D, a span's chip and rails as one shape, drawn by the track
  viewport). Each glows on the 2D surface that draws it; the 3D preview glows nothing (F4).
- **A lock is a culprit** (ruled 2026-10-20, reversing the earlier ruling): when the Note Lock or
  the Tempo Lock is the reason, its toolbar button glows beside the subject.
- **The lock answers first, stated once at dispatch.** The lock that governs an action refuses it
  before planning, bounds or prompts get a say; the Tempo Lock freezes the tempo map and the time
  signatures, the Note Lock the song's content (sections included); undo and redo reproduce history
  already consented to and answer to neither. Remove Plugin no longer greys under the Note Lock: it
  refuses with the lock's button lit, as every governed action does.
- **The sentence is on screen (2026-10-20).** The refusal's sentence — the reason the log records,
  never the subject, since the glow is the subject — shows in the tab lane's status line, red, at
  the lane's top-left just under the fret-hand chip row, in its own 14 px regular face
  (`tabStatusLineFont`), held at full ink for 2.5 s and then faded over 0.4 s on the flash's own
  clock (`g_refusal_line_hold`), never on the glow's pulses. Between refusals the line reads
  "N notes selected" while two or more notes are selected; the transport strip's count chip is
  retired.
- **The newest refusal replaces the running flash, on every surface:** what it lights, what it
  says and its clock (`startRefusalFlash`). A refusal naming nothing on a surface clears that
  surface's flash; the sentence's hold rides only a record that carries a sentence, and a flash is
  finished when both its pulse train and its hold are done.

### The mechanism, as shipped

A refusal's light is one type, `RefusalLight` (`controller/refusal_light.h` under
`rock-hero-editor/core/src/`): `RefusalTarget`s of any kinds, each named exactly as the selection
names that kind (`ChartNoteKey`, `ChartKeyframeKey`, `SpanSelection`, `NoneHoldSelection`,
`FretHandPositionSelection`, `SongSectionSelection`, `TempoAnchorSelection`,
`TimeSignatureSelection`, `ToneRegionSelection`) plus the toolbar's `LockControl`, so a refusal and
a selection name a thing the one way. Each refusing site adds what it knows (`PlanRefused::lit`,
`ChartRefusedNote::lit`, `TempoMapEditError::lit`), and the controller adds the subject it holds — the selection or operand
a chart verb planned over (`reportChartPlanRefusal`, `reportChartRefusedNotes`,
`reportChartFretEntryRefusal`), whatever was selected when a hold verb ran — the chart selection's
notes and keyframes or the selected marker, a time span lighting nothing (`refuseShapeHoldVerb`,
through `selectionLight`) — the chip a marker verb
acted on (`refuseMarkerVerb`, which every marker verb and the marker funnel refuse through) — so
neither has to know what the other added, and the flash glows the union as one set. A rule's
error carries its records, so no verb restates a rule to find what stood in its way: a tempo-map
error names its anchors and meter places (`tempoMapCulprits`), a tone-track error its regions
(`toneTrackCulprits`), the coverage rule the uncovering position and the stops it misses
(`FretHandPositionsSnapshot::validate`). The marker funnel returns the same refusal type the chart
planners do (`judgeMarkerModel`: `std::expected<…, PlanRefusal>`, each snapshot's `validate`
returning `std::optional<PlanRefused>`), and a move adds the marker standing where the moved one
would land (`commitMarkerMove`). Every flash takes one path, `flashRefusalLight`
(`chart/chart_handlers.cpp`), which resolves each target through the authority its surface draws it
by and hands the view one `RefusalFlash` through one port, `IEditorView::flashRefusal`.
`RefusalFlash::sentence` carries the logged reason: `flashRefusalLight(lit, sentence)` sets it,
every reporter passing the string it logs (a per-note refusal its first note's), and a refusal whose
light resolves nothing still shows its sentence.
`EditorView::flashRefusal` hands each surface its share: the tab lane (`TabView::flashRefusal`) its
notes, keyframes and fret-hand position chips, and the sentence; the track viewport
(`TrackViewport::flashRefusal`) its spans and none holds, a span's chip and rails glowing as one
shape on the outline its selection ring strokes — a refused span's chip lights — and it forwards to
the ruler (`TimelineRuler::flashRefusal`) its section, tempo and time-signature chips by
`RulerChipKey` and to the tone row (`ToneTrackView::flashRefusal`) its regions by id; and the
toolbar (`EditorView` itself) its lock buttons by `LockControl`. The 3D preview is handed nothing
(F4). Every painter glows the set it is given and never knows subject from culprit. `TabView` owns
**the lane line** (`paintStatusLine`, under `common::ui::tabFhpChipRow`, past the legend): the
sentence rides the lane's one refusal record, and the selected-note count returns when the record
retires or a new projection drops it. A typed tone name that duplicates another is input the
charter retypes, not a broken model: it is logged under the verb's label and lights every region
bearing the name (`reportedDuplicateToneName`), the prompt having no subject on screen.

**The lock is stated once, at dispatch.** `governingLock(EditorAction::Id)`
(`controller/editor_action_availability.cpp`) names each action's lock — `None`, `Note`, `Tempo`,
or `Operand` for the editor-wide move and delete, `Enter` and `Ctrl+R`, which answer to the lock
of the selection's kind (a tempo anchor or time signature to the Tempo Lock, every other kind to
the Note Lock; nothing selected, a time span or the automation "+" row to none, so such a press
stays silent). `runAction` asks `refuseLockedAction` before every action, after availability and
before anything plans, bounds or prompts: while the governing lock is on it logs
`Refused locked action action=<id>` with the lock's sentence (`lockRefusal`) and lights the
selection as it stands beside the lock's button. Every marker intent is an action — the chords,
the At-verbs, `ToneRenamePrompt`, `ScaleTempoPrompt`, `RestateSelection`, `RenameSelection` — so
no verb, planner or funnel spells a lock of its own.

### Census

Every refusal as built on 2026-10-21; rows 63–68 came with "A press that moves nothing is a
refusal", and the last row is every lock refusal, stated once at dispatch.
**Subject** is what the controller adds — "sel" is the selection or
operand a chart verb planned over (a gesture's live selection), "hold" whatever was selected when
the hold verb ran — its notes and keyframes, or the span or none hold (a time selection adds
none), "chip" the marker the verb acted on;
**Culprit** is what the refusing site adds beyond it. A target the projection cannot resolve (a
note or chip the refused edit would have made) lights nothing, and "—" lights nothing. Every row
logs its reason and shows it on the tab lane's status line, a row that lights nothing included.
Paths are under `rock-hero-editor/core/src/`.

| # | Site | Reason (logged) | Subject | Culprit |
|---|---|---|---|---|
| 1 | `chart/chart_edits.cpp:finalizePlan` (truncation) | the edit would cut an authored statement off a ring it shortens | sel | the ring and the head bounding it |
| 2 | `validateAffected` (alone) | the rule's message | sel | the note the rule judged |
| 3 | `validateAffected` (pair) | the rule's message | sel | both notes of the pair |
| 4 | `finalizeShapeHoldPlan` (reader rule) | the rule's message | hold | every hold the candidate changed |
| 5 | `finalizeShapeHoldPlan` (normalizer drop) | H would hold nothing | hold | H |
| 6 | `finalizeShapeHoldPlan` (no fit) | H does not fit its notes: its shape closes at X | hold | the notes sounding between H's stated end and X |
| 7 | `planSteppedShapeHoldEdge` (nearest candidate) | that candidate's own refusal (rows 4–6, 9, 10) | hold | that row's culprit |
| 8 | `planSteppedShapeHoldEdge` (no candidate that way) | no note boundary or grid line later / earlier | hold | the moved hold: the subject, once |
| 9 | `movedShapeHolds` (moved hold emptied) | H would hold nothing | hold | the moved hold: the subject, once |
| 10 | `movedShapeHolds` (neighbour emptied) | the edge would push H′ to nothing | hold | H′ |
| 11 | `splitNoteIntoProducts` (junction toggle, `planCutRing`) | a scrape has no junction to split | sel | the note and the junctions asked of it |
| 12 | `splitNoteIntoProducts` | the split instant is not inside the ring | sel | the note and the junctions asked of it |
| 13 | `joinHeadIntoPath` | the ring before this head has no stop to hand to a point | sel | the predecessor |
| 14 | `joinHeadIntoPath` | a scrape or harmonic head cannot become a point on a ring | sel | the head, within the subject |
| 15 | `planInsertKeyframe`, `planCutRing` | no note stands at that slot | — | — |
| 16 | `planMoveSelection` | the move would stand a point on or past its ring's end | sel | that keyframe, within the subject |
| 17 | `planMoveSelection` | the move would leave the neck or the grid | sel | the moved notes, within the subject |
| 18 | `planMoveSelection` | a moved note would land on a note that stays | sel | the unmoved notes in the way |
| 19 | `planRetypeFrets` (`ShiftChartFrets`, a retyping digit) | a fret-hand harmonic has no stop to retype | sel, or the entry's target (row 26) | that note, within the subject |
| 20 | `planToggleJunctions` (cut) | a head must sit on a stated fret | sel | that keyframe, within the subject |
| 21 | `planToggleJunctions` (join) | nothing earlier on the string holds a path to join | sel | that head, within the subject |
| 22 | `planNoteWrite` (the verb's write; the per-note rules) | the verb's reason (harmonic: no node is reachable, …) or the rule's message | each refused note | — |
| 23 | `planSetLegato` | the picking hand strikes it, so no connection describes it / nothing earlier on the string to connect to | each refused note | — |
| 24 | `planSetLegato` | the ring before it stops short and cannot be grown to reach it / no connection between the two stops | each refused note | the predecessor ring |
| 25 | `chart/chart_handlers.cpp` harmonic picker, nothing on offer | each note's reason | each note | — |
| 26 | `reportChartFretEntryRefusal` (a refused bare or `Alt` digit, a refused `Insert`) | the plan's reason | the entry's target: the ring a cut or point divides, the objects a retype addresses, the hand chip; nothing for an insert at an empty slot | whatever the plan's refusal lit (rows 1–3, 11, 12, 15, 19, 30, 31) |
| 27 | `replanChartFretEntry` | no chart is loaded | — | — |
| 28 | `chart/shape_hold_handlers.cpp` `ChooseHold` | nothing is selected to hold | — | — |
| 29 | `chart/fret_hand_handlers.cpp` Move, Delete, Alt+Delete (`ClearFretHandEnd`) (derived) | P is derived, so nothing is stored there | chip | — |
| 30 | `FretHandPositionsSnapshot::validate` (coverage), via Add at cursor, Move, either fret, Clear End | P does not cover … | chip; for the Add, the place, which lights a derived chip standing there | P's chip and the notes gripping every stop it misses |
| 31 | `FretHandPositionsSnapshot::validate` (stream rules), via the same verbs and Delete | the rule's message | chip, as row 30 | — |
| 32 | Move (`moveSelectedFretHandPosition`) | fret-hand position would leave the chart | chip | — |
| 33 | Move, the funnel's refusal | occupied start, past the end, as rows 30–31 | chip | the chip at the landing, where one stands, and row 30's culprits |
| 34 | `PruneFretHandPositions` | no stored hand position is redundant | — | — |
| 35 | `PruneFretHandPositions`, the funnel's refusal | the rule's message | — (every stored position) | row 30's culprits |
| 36 | `timeline/section_handlers.cpp` Insert, the funnel's refusal | empty name, off the grid, at or past the terminal, place held | the place, which lights the section holding it: subject and culprit at once | — |
| 37 | Rename, Delete, the funnel's refusal | blank name | chip | — |
| 38 | `moveSelectedSongSection` | section would leave the song | chip | — |
| 39 | `moveSelectedSongSection`, the funnel's refusal | onto the terminal, place held | chip | the section holding the landing place |
| 40 | `timeline/meter_handlers.cpp:onTimeSignatureSetRequested` | not a time signature: text | the chip of the measure it was typed on | — |
| 41 | `refuseAtSongEnd` (`timeline/marker_row_handlers.cpp`): `SetTimeSignature`, and every marker chord or At-verb addressed on the closing barline — `TempoChord`, `SongSectionAt`, `TimeSignatureAt`, `ToneChangeAt`, `AuthorFretHandPositionAtCursor` | nothing can start at or past the end of the song | — (the closing barline draws no chip) | — |
| 42 | `SetTimeSignature`, the grid funnel (`SongTimelineSnapshot::validate`) | the timeline rule's message (a reign shorter than one of its measures, …) | the chip of the reign a Set restates; an Add's beat holds none | the anchors and meters a tempo-map rule names, the regions a tone rule names; a section or chart rule names none |
| 43 | `deleteSelectedTimeSignature` | the time signature at measure 1 cannot be deleted / the measure N meter is derived | chip | — |
| 44 | `deleteSelectedTimeSignature`, the grid funnel | as row 42 | chip | as row 42 |
| 45 | `moveSelectedTimeSignature` | measure 1 cannot move / derived / to or past the end of the song | chip | — |
| 46 | `moveSelectedTimeSignature` | a time signature cannot step onto or past its neighbour | chip | the neighbour's chip |
| 47 | `moveSelectedTimeSignature`, the funnel (`commitMarkerMove`) | as row 42 | chip | the chip at the landing measure, where one stands, and row 42's culprits |
| 48 | `timeline/tempo_handlers.cpp:onTempoSpanBpmRequested`, `onScaleTempoRequested` | not a number: text / the parse's message | the anchor the tempo was typed on / — | — |
| 49 | `MoveTempoAnchor` drag event | the edit's message | chip | the anchors and meters a broken rule names |
| 50 | `commitTempoEdit`, the edit's refusal (Add, Set Tempo, Move, Delete, Scale with the Note Lock off) | occupied, at or past the terminal, onto a neighbour, the first anchor, the tempo, a broken rule | the anchor addressed — Add's place lights an anchor standing there; Scale's subject is the whole map, lit as every anchor's chip | the anchors and meters a broken rule names (`TempoMapEditError::lit`) |
| 51 | `commitTempoEdit`, the funnel's refusal (`TempoMapSnapshot`, or `SongTimelineSnapshot` with the Note Lock on) | the rule's message | as row 50 | as row 42 |
| 52 | Scale Tempo with the Note Lock on (`scaledTimeline`) | the relabel's message | every anchor's chip | — |
| 53 | Scale Tempo with the Note Lock on, the grid funnel | as row 42 | every anchor's chip | as row 42 |
| 54 | `tone/tone_handlers.cpp` Create Tone Region, Add Tone: the core edit or the funnel | the core's message / the rule's | the region holding the place, which the insert splits | the regions the error names |
| 55 | Delete Tone Region, its sole-region reset, Change Tone of Region: the core edit or the funnel | as row 54 | the region | the regions the error names |
| 56 | Move Tone Boundary (drag): the core edit or the funnel | as row 54 | the region the boundary opens | the regions the error names |
| 57 | `moveSelectedToneRegionStart` | tone change would leave the song | the region | — |
| 58 | `moveSelectedToneRegionStart`: the core edit or the funnel (`commitMarkerMove`) | as row 54 | the region | the regions the error names |
| 59 | `ToneModelSnapshot::validate` (catalog), via the tone verbs | tone is not in the arrangement catalog | as the verb's | the region pointing outside the catalog |
| 60 | Rename Tone, the funnel's refusal | the rule's message | — (a catalog tone, which no chip draws) | the regions the error names |
| 61 | `reportedDuplicateToneName` (Rename Tone, Add Tone, a retone to a new tone) | A tone named "…" already exists in this arrangement. | — | every region bearing the name |
| 62 | `controller/marker_model_commit.h` (`landMarkerModel`, `commitMarkerMove`) | no model to apply onto | as the verb's | — |
| 63 | `chart/chart_edits.cpp:planAdjustSustain`, every ring held at its growth bound | ring already reaches the next note on its string | sel | the next note on each held ring's string |
| 64 | `planAdjustSustain`, every ring held by its last keyframe | ring cannot shrink past its last keyframe | sel | each held ring's last keyframe |
| 65 | `planAdjustSustain`, every ring held by its onset or a scrape's window | ring cannot shrink to nothing / scrape already at its shortest | sel | — |
| 66 | `planAdjustSustain`, every ring held, by bounds of different kinds | every ring already at its bound / every ring already at its floor (by the press's direction) | sel | rows 63–64's culprits |
| 67 | `planMoveGesture`, a time press over slide-outs alone, every one parked on the next head | slide-out already reaches the next note on its string | sel | the head each is parked on |
| 68 | `planMoveGesture`, a time press the grid's edge absorbs (the step collapses onto the origin or the terminal) | already at the start of the song / already at the end of the song | sel | — (a LATER selected note the step carries onto or past the terminal is row 2's: "a note cannot start at or past the end of the song", from `objectCanStartAt`, the place rule notes and markers share) |
| 69 | `tone/tone_handlers.cpp:moveSelectedAutomationPoint`, a value step at the range's end | automation point is already at the top / bottom of its range | — (a point is no target) | — |
| 70 | `moveSelectedAutomationPoint`, a time step off the song | automation point would leave the song | — | — |
| 71 | `moveSelectedAutomationPoint`, a time step onto a neighbour | automation point cannot step onto or past its neighbour | — | — |
| 72 | `moveSelectedAutomationPoint`, a time step out of the active region's window | automation point would leave its tone region | — | the active tone region, whose edge held it |
| 73 | `controller/editor_controller.cpp:refuseLockedAction`, asked by `runAction` before every action the governing lock refuses (`governingLock`: every content, grid and marker-intent action; the editor-wide move and delete, `Enter` and `Ctrl+R` by the selection's kind), logged `Refused locked action action=<id>` | Note Lock is on: the song's content is frozen while it holds the notes in time / Tempo Lock is on: the tempo map and time signatures are frozen | the selection as it stands (`selectionLight`); nothing where none is selected | the lock's toolbar button |

Not refusals, and outside the table: a mint failure is an error dialog (`reportError`); an action
naming a tone region, plugin instance or automation write the model no longer holds is ignored with
an `RH_LOG_WARNING`; an honest `PlanNoChange` reports nothing. The grid funnel's coverage refusal
(rows 42, 44, 47, 51, 53), which lit no culprit, is retired: positions yield to the grid — a tempo
or meter edit drops a position it leaves uncovering and logs it (`SongTimelineSnapshot::normalize`).

### What is not a target

- **Automation points and lanes.** A refused automation write or nudge logs and lights nothing of
  its own; a nudge held by the region's window lights that region.
- **A prompt's text, and anything with no mark on screen:** a typed meter, tempo, factor or tone
  name; a catalog tone; the stored positions as a whole (Prune); an edit with no chart loaded.
- **What the refused edit would have made.** It has no mark yet, so its target resolves to nothing;
  the authored thing it ran into is what glows.
- **Anything on the 3D preview.** The preview glows nothing: unless it can light every target it
  lights none, and it cannot light a note head or a none hold (F4). Every target glows on the 2D
  surface that draws it.
