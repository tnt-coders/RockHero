# Refusal flash (task #278)

Status: BUILT 2026-09-30, awaiting its sighting (F1, F2). Written 2026-09-21 from a recovered
discussion — see "Provenance" — because the ruling had survived in one conversation only, and task
#278 had gone on describing the design the ruling replaced.

When an edit key is refused, the editor says so **on the thing that refused it**: the selected
elements the verb turned down glow red a couple of times and stay as they were. Why it was refused
goes to the log. There is no status line, no toast and no caret callout — that was the proposal on
the table, and the flash was chosen instead of it.

Every claim about current code below was verified on 2026-09-21 and the Shape section on 2026-09-30. Re-verify before acting on one.

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
- **The reason goes to the log**, not the screen.
- **One mechanism for every verb.** No per-verb feedback design.

**A refusal lights what it names; the reason goes to the log** (user, 2026-10-20). What a refusal
lights is decided where it refuses, never defaulted by the mechanism: every `ChartPlanInvalid`
carries its light (`ChartPlanInvalid::lit`, a `ChartRefusalLight` of note keys, keyframe keys and
a fret-hand position's chip, `rock-hero-editor/core/src/chart/chart_edits.h`) beside its reason,
and the controller's one flash path (`flashRefusalLight`) only resolves it. Where another authored
thing stands in the way, that culprit glows instead of the selection: the authored fret-hand
position that would not cover a stop, beside every note gripping a stop it misses — all the things
"hand position P does not cover frets 5 and 7 gripped from Q" names, since a refusal naming one
missed stop of several would send the charter to fix one and meet the next (the note gate's and the
hold gate's one coverage refusal), the note a move would land on, the ring whose authored statement an edit would cut off (beside the
head that cuts it, since one of the two is the edit's own product with no mark yet), the
predecessor a head cannot join, the neighbour hold a push would empty, and the stretch between a
hold's stated end and where its shape closes. A hold edge key refused at every boundary its way
surfaces the NEAREST boundary's own refusal, reason and light, never a message of its own about
the direction (`planSteppedShapeHoldEdge`; only with no boundary that way at all does it say so,
lighting the moved hold). Where nothing does, the refusing site names the refused elements
themselves; a site with nothing on screen to name (an empty slot, no chart) says
so with an empty light. A hold is lit as the notes sounding inside it, struck in it or ringing into
it. The marker funnel's refusal (`MarkerModelInvalid`) carries no light, because the funnel judges
a model and never knows which chip the verb acted on: the verb that produced the model lights its
chip beside the funnel's record.

Accepted in the same exchange without objection, but the assistant's words and not the user's: the
flash fires on a real refusal only, never on an honest no-op (`ChartPlanInvalid`, never
`NoChange`); a fading glow modulates luminance, so the signal does not rest on hue alone; and if
sighting ever shows charters not finding the why, the log line is the thing to surface — not a
redesign.

**A visible bound is not a refusal** (user, 2026-09-21). Resizing happens constantly, and a ring
that runs into the next note's head, or shrinks to its last keyframe, and simply stops there is
not an error: the charter can see what bounds it. No flash, no mark, no message. This settles
roadmap 40-Q5 — the slide tail lock is a silent floor in the resize clamp, and was already built
(`planAdjustSustain`, `83f2afcd`) — and it draws the
flash's line for every verb: the flash is for a key that did nothing for a reason the screen does
not already show. Every keyframe floor is visible: a keyframe that states no fret states a bend or
a vibrato, which the tail draws, and one that states nothing dissolves. (W13's open display
question is a different one — such a keyframe has no HEAD for a pointer to reach.)

**Never said by anyone**, and not to be reconstructed as if it had been: pulse timing, the choice
between two pulses and three, the drawn geometry (the "selection ring" was a *perhaps*), a color
token, behaviour under key repeat, and any 3D treatment.

## What it replaces, and what dissolves

Task #278 read "the one non-modal notice channel (status bar / toast)" and the release plan called
four payloads "built and waiting" for it. Neither survives:

- **The payloads are one type now.** Every per-note verb — legato and every technique write —
  returns `ChartSelectionPlan{plan, refused}` (`rock-hero-editor/core/src/chart/chart_edits.h`):
  the whole-plan answer beside each note it could not write, with its reason. It replaced
  `ChartLegatoPlan` and its skip enum. A whole-plan refusal carries its reason too
  (`ChartPlanInvalid::reason`, the rule's own message where the gate refused), so the slide-tail
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

## Shape, as built (2026-09-30)

1. **Core: refusals carry reasons, identities and their light.** `ChartPlanRefusal` is
   `variant<ChartPlanNoChange, ChartPlanInvalid>`, and only the refusal carries a reason and what
   it lights (`ChartPlanInvalid::lit`), both stated where it refuses — the note planners, the note
   gate `finalizePlan` and the hold gate `finalizeShapeHoldPlan`. The per-note verbs return
   `ChartSelectionPlan`, whose `refused` names each note they could not write. In `planNoteWrite`
   a verb's write either refuses a note with a reason (the harmonic's "no node is reachable") or
   leaves the no-op test to find "already so", so a no-op is never a refusal; a note the per-note
   rules refuse is named with the rule's message.
2. **Controller: one report path.** `reportChartPlanRefusal` logs a whole-plan refusal against its
   subject and flashes what the refusal lights; `reportChartRefusedNotes` logs each refused note
   and flashes those; a hold verb's `refuseShapeHoldVerb` logs under the verb's label and flashes
   the same way; all reach the view through `flashRefusalLight`, which resolves keys through the
   two authorities the selection is published by and a hand chip through its row's marker index.
   The funnel `applyChartEditPlan` reports, and its per-note overload
   reports the notes AFTER applying the rest, because the flash is addressed in the projection the
   apply published. The junction toggle, `Insert`'s refused cut (the ring's note, since nothing was
   typed to box) and the harmonic picker's "nothing on offer" report through the same two.
3. **Port: a one-shot effect**, `IEditorView::flashChartRefusal(ChartRefusalFlash)`, not view state.
   The test fake records each flash, which is what controller tests assert.
4. **View: the tab lane.** A glow in `EditorTheme::invalid` spreading outward from each refused
   mark's outline (the outline the selection ring strokes), clipped outside the mark, at the
   flash's level; the lane's one frame attachment steps it beside the presence ease, and a new
   projection drops it. The level is `cos²(π·(pulses − ½)·t/T)`: lit at the keystroke, dark between
   pulses and at the end.
5. **The log.** The Quill-backed `RH_LOG_*` facade, at `%APPDATA%/Rock Hero/Rock Hero Editor.log`.
   The app still has no way to open it (F5): the reason is developer-visible until an "Open Log"
   entry exists, which is small and the user's to call.

**Deliberately silent:** the held-key gestures (move, resize) — their `Invalid` is mostly a visible
bound, and the gesture step never hands a refused plan to the funnel; an honest `NoChange`; and a
legato press that CLEARED, whose notes' refusals to set were never asked for.

## Open — the user's to rule

| # | Question | Recommendation |
|---|---|---|
| F1 | Pulse count and timing | BUILT at two pulses over 0.6 s (`g_refusal_flash_train` in `shared/flash_pulse.h`); sight and tune |
| F2 | Drawn geometry: the selection ring turning red, or a glow around it | BUILT as the ring turning red, then made a GLOW (user, 2026-09-30: wider, closer to the accent's halo): reach 0.2 of the mark's size, the accent halo's, after 0.45 sighted too wide; four layers (`g_refusal_glow_*`, `shared/flash_pulse.cpp`), drawn over the caret square (user, 2026-09-30) |
| ~~F3~~ | ~~Key repeat~~ | BUILT as recommended: a refusal arriving mid-flash joins the running one on its clock, so a held key reads as one pulse train |
| F8 | The fret shift at the neck's edge (`ShiftChartFrets` below fret 0 or past the last) flashes the selection, while a move stopping at the edge is silent | Sight it: the fret on screen already says why, so under the bound ruling it may belong with the silent bounds |
| F9 | The move gesture's `Invalid` carries both visible bounds (neck edge, occupied slot) and a real refusal (a landing that would strand a point), so the gesture stays silent for both | If a real move refusal needs its flash, a bound should plan `NoChange` as the sustain clamp does, leaving `Invalid` for refusals only |
| F10 | A keyframe a per-note rule refuses flashes its NOTE's head, since the rule authority names notes, not keyframes | Sight it; naming the keyframe needs the rule authority to name one |
| F11 | A legato press that CLEARS flashes nothing, even where some selected notes could not have been claimed | Built silent: the press cleared, which never asked those notes to be set; the ruling's "applies to seven, flashes the three" reads either way for a clear |
| ~~F4~~ | ~~2D only?~~ | RULED 2026-09-21: yes, 2D only |
| ~~F5~~ | ~~Reason to the log ONLY~~ | RULED 2026-09-21: yes, "for now" — the log line is the thing to surface if charters cannot find the why |
| ~~F6~~ | ~~Scope of "every selection"~~ | RULED 2026-09-21: notes and keyframes with #278; a marker verb gains it when it first has a refusal to report. The first is the Tempo Lock's refused anchor move (plan 66 Phase 1, 2026-10-04): the tempo chip glows on the ruler through `IEditorView::flashTempoAnchorRefusal`, on the same pulse and glow, which moved to `ui/src/shared/flash_pulse.h` (named `refusal_flash.h` until plan 65 Phase 3 gave its pulse a second, non-refusal use) so both surfaces share them |
| ~~F7~~ | ~~40-Q5~~ | RULED 2026-09-21: a silent floor, no flash — see "A visible bound is not a refusal" above. The lock itself was ruled 2026-08-09 |

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

## Design session 2026-10-20: one rule for every refusal

For the user to rule from. Read against the working tree of 2026-10-20 (the uncommitted rewrite of
the refusal light in `rock-hero-editor/core`); re-verify before acting on a row.

### The mechanism today

A refused chart plan is `ChartPlanInvalid{reason, lit}`, where `lit` is a `ChartRefusalLight{notes,
keyframes, fret_hand_position}` — chart keys plus at most one hand position's place
(`rock-hero-editor/core/src/chart/chart_edits.h`) — stated at the refusing site, never defaulted.
Three controller paths feed the one flash path `flashRefusalLight`
(`editor_controller_impl.h`, defined in `chart_handlers.cpp`): `reportChartPlanRefusal` (logs a
whole-plan refusal against the verb's SUBJECT, flashes `lit`), `reportChartRefusedNotes` (logs and
flashes each per-note refusal of a `ChartSelectionPlan`) and `refuseShapeHoldVerb` (logs under the
hold verb's label, flashes `lit`); `insertAtChartCaret` and `flashFretHandChip` call it directly.
It resolves the keys to projection indices and the position to its chip's marker index, and hands
`IEditorView::flashChartRefusal(ChartRefusalFlash{notes, keyframes, fret_hand_position})` to the tab
lane, which glows exactly those: notes, keyframes, one hand-position chip. The marker funnel's
refusal is reason-only (`MarkerModelInvalid{reason}`, `marker_model_commit.h`); the verb that
produced the model lights its own chip, if it chooses to. Ruler chips are a SEPARATE port,
`IEditorView::flashRulerChipRefusal(RulerChipKey{row, start})` for the Section, Tempo and
TimeSignature rows, reached by `flashSongSectionChip` (section verbs), `refuseTimeSignatureVerb` and
the meter move's commit refusal, and `refuseLockedTempoAnchorMove` (Tempo Lock only);
`ChartRefusalLight` cannot name a ruler chip. Nothing can light a hold's own furniture (its bracket
or rails): a hold is lit as the notes sounding in it. Nothing lights a tone-region chip, and nothing
flashes on the 3D highway (F4).

### Census

S = the refused selection or subject (what the verb acted on); C = a culprit the reason names
besides S. "as notes" = a hold lit through the notes sounding in it. All files under
`rock-hero-editor/core/src/`.

| # | Site | Message | Lights today | S lit | C lit |
|---|---|---|---|---|---|
| 1 | `chart/chart_edits.cpp:finalizePlan` (truncation) | the edit would cut an authored statement off a ring it shortens | the ring + the head bounding it | when S is one of the two | yes |
| 2 | `chart_edits.cpp:validateAffected` (alone) | the rule's message | the first affected note the rule refuses | one note of S | — |
| 3 | `validateAffected` (pair) | the rule's message | the pair | part of S | the neighbour, yes |
| 4 | `fretHandCoverageRefusal` (both gates) | hand position P does not cover frets … gripped from Q | first uncovering position's chip + every note gripping a missed stop | no, unless S grips a missed stop | yes (a stop taken from a slide arrival: chip only) |
| 5 | `finalizeShapeHoldPlan` (reader rule) | the rule's message | every changed hold, as notes | as notes | as notes |
| 6 | `finalizeShapeHoldPlan` (normalizer drop) | H would hold nothing | H, as notes | as notes | — |
| 7 | `finalizeShapeHoldPlan` (no fit) | H does not fit its notes: its shape closes at X | notes sounding between H's stated end and X | no | yes |
| 8 | `planSteppedShapeHoldEdge` (nearest boundary) | that boundary's own refusal (rows 4–7, 10–11) | that row's light | per row | per row |
| 9 | `planSteppedShapeHoldEdge` (no boundary) | no note onset/boundary later/earlier | the moved hold, as notes | as notes | — |
| 10 | `movedShapeHolds` (moved emptied) | H would hold nothing | the moved hold, as notes | as notes | — |
| 11 | `movedShapeHolds` (neighbour emptied) | the edge would push H′ to nothing | H′, as notes | no | as notes |
| 12 | split walk (junction split, `planCutRing`) | a scrape has no junction to split | the note + the junction keyframes asked (a cut's instant resolves to none) | yes | — |
| 13 | split walk | the split instant is not inside the ring | same as 12 | yes | — |
| 14 | `joinHeadIntoPath` | the ring before this head has no stop to hand to a point | the predecessor | **no** | yes |
| 15 | `joinHeadIntoPath` | a scrape or harmonic head cannot become a point on a ring | the head | yes | — |
| 16 | `planInsertKeyframe` | no note stands at that slot | nothing | — | — |
| 17 | `planCutRing` | no note stands at that slot | nothing | — | — |
| 18 | `planMoveSelection` | the move would stand a point on or past its ring's end | that keyframe | one element of S | — |
| 19 | `planMoveSelection` | the move would leave the neck or the grid | the moved notes | notes; selected keyframes dark | — |
| 20 | `planMoveSelection` | a moved note would land on a note that stays | the unmoved notes in the way | **no** | yes |
| 21 | `planRetypeFrets` | a fret-hand harmonic has no stop to retype | that note | one element of S | — |
| 22 | `planToggleJunctions` (cut) | a head must sit on a stated fret | that keyframe | one element of S | — |
| 23 | `planToggleJunctions` (join) | nothing earlier on the string holds a path to join | that head | one element of S | — |
| 24 | `planNoteWrite` (verb's write) | the verb's reason (harmonic: no node is reachable, …) | each refused note | yes | — |
| 25 | `planNoteWrite` (rule) | the rule's message | each refused note | yes | — |
| 26 | `planSetLegato` | the picking hand strikes it, so no connection describes it | each refused note | yes | — |
| 27 | `planSetLegato` | nothing earlier … to connect to / the ring before it stops short and cannot be grown to reach it / no connection between the two stops | each refused note | yes | the short ring: **no** |
| 28 | `chart/chart_handlers.cpp` harmonic picker, nothing on offer | each note's reason | each note | yes | — |
| 29 | `chart_handlers.cpp:applyChartEditPlan` (Note Lock) | Note Lock is on: … | the notes the plan rewrote | notes; selected keyframes dark | — |
| 30 | `planChartFretEntry` (Note Lock) | Note Lock is on: … | per target: the ring, the retyped notes + keyframes, the hand chip; nothing for an insert at an empty slot | red box while typed; flashed only through `Insert` | — |
| 31 | `planChartFretEntry` | no chart is loaded | nothing | — | — |
| 32 | `armOrSettleChartFretEntry` (any refused digit entry) | the plan's reason | the pending red box only; `lit` is not flashed | the box | **no** (e.g. row 4's chip) |
| 33 | `chart/fret_hand_handlers.cpp` hand-position Add at cursor (funnel) | the funnel's reason (coverage, Note Lock) | a derived chip at that place if one stands, else nothing | — | **no** |
| 34 | `refuseDerivedFretHandPosition` (Move, Delete) | P is derived, so nothing is stored there | the chip | yes | — |
| 35 | `moveSelectedFretHandPosition` | fret-hand position would leave the chart | the chip | yes | — |
| 36 | `moveSelectedFretHandPosition` (funnel) | occupied start, past end, coverage, Note Lock | the chip at its old place | yes | **no** |
| 37 | `planFretHandFret` | the funnel's reason | the red box (`lit` names the chip, unflashed) | the box | no |
| 38 | `ClearFretHandEnd` (funnel) | the funnel's reason | nothing | **no** | **no** |
| 39 | `deleteSelectedFretHandPosition` (funnel) | Note Lock | nothing | **no** | — |
| 40 | `PruneFretHandPositions` | no stored hand position is redundant / Note Lock | nothing | — | — |
| 41 | `chart/shape_hold_handlers.cpp:ChooseHold` | nothing is selected to hold | nothing | — | — |
| 42 | `timeline/section_handlers.cpp` Insert (funnel) | empty name, off grid, at terminal, place held, Note Lock | nothing (prompt) | — | **no** (the holding section) |
| 43 | Rename (funnel) | blank name, Note Lock | the section chip | yes | — |
| 44 | `moveSelectedSongSection` | section would leave the song | the chip | yes | — |
| 45 | `moveSelectedSongSection` (funnel) | terminal, place held, Note Lock | the chip at its old place | yes | **no** |
| 46 | `deleteSelectedSongSection` (funnel) | Note Lock | the chip | yes | — |
| 47 | `timeline/meter_handlers.cpp:onTimeSignatureSetRequested` | not a time signature: text | nothing (prompt) | — | — |
| 48 | `SetTimeSignature` | cannot start at or past the end of the song | nothing | — | — |
| 49 | `SetTimeSignature` (`commitGridModel`) | tempo-map rule (reign too short) | nothing | **no** | **no** |
| 50 | `deleteSelectedTimeSignature` | measure 1 cannot be deleted / derived | the chip | yes | — |
| 51 | `deleteSelectedTimeSignature` (`commitGridModel`) | tempo-map rule | nothing | **no** | **no** |
| 52 | `moveSelectedTimeSignature` | measure 1 / derived / to or past the end | the chip | yes | — |
| 53 | `moveSelectedTimeSignature` | cannot step onto or past its neighbour | the moved chip | yes | **no** |
| 54 | `moveSelectedTimeSignature` (`commitMarkerMove`) | ReignTooShort, … | the chip | yes | **no** |
| 55 | `timeline/tempo_handlers.cpp:refuseLockedTempoAnchorMove` (nudge, drag, release) | Tempo Lock is on: … | the tempo chip | yes | — |
| 56 | `commitTempoEdit` (Add, Set BPM, Move, Delete, Scale) | the edit's message (occupied, terminal, neighbour, first anchor, tempo) | nothing | **no** | **no** |
| 57 | `commitTempoEdit` (funnel, Note Lock branch) | the funnel's reason | nothing | **no** | **no** |
| 58 | `MoveTempoAnchor` drag event | the edit's message | nothing (last legal time held) | no | no |
| 59 | `onTempoSpanBpmRequested`, `onScaleTempoRequested` | not a number: text / the parse's message | nothing (prompt) | — | — |
| 60 | `ScaleTempo` relabel (Note Lock) | `scaledTimeline`'s message / funnel | nothing | — | — |
| 61 | `tone/tone_handlers.cpp` create/delete/reset/retone core errors | `RH_LOG_WARNING`, not the refusal log | nothing | no | no |
| 62 | `reportedDuplicateToneName` | an error dialog (`reportError`) | nothing | — | — |
| 63 | every tone funnel refusal (`commitMarkerModel`, `commitMarkerMove`) | the funnel's reason | nothing (no tone chip target) | **no** | **no** |
| 64 | `moveSelectedToneRegionStart` | tone change would leave the song | nothing | **no** | — |
| 65 | `SetToneAutomationPoints` | Note Lock is on: … | nothing | — | — |

### Findings from the sighting

- **(a) The subject goes dark.** Lengthening a note's tail through an authored position's reign into
  a chord refuses with row 4, which lights the position's chip and the chord's uncovered notes but
  NOT the note whose tail was lengthened. Rows 14, 20 and 11 share the shape: what the key acted on
  is dark, only what stood in its way glows.
- **(b) A hold has no target.** Every refusal naming a hold (5–11) lights the notes sounding in it,
  never its bracket or rails: neither the tab lane nor the highway can glow a hold.
- **(c) A composed message hid the culprit.** The hold-edge stepper's own "no boundary … where the
  hold fits" named only the direction and lit only the moved hold; it now surfaces the nearest
  boundary's own refusal (row 8).
- **(d) A partial culprit misleads.** The coverage refusal first named one missed stop of two; the
  charter fixed one and met the next. It now names and lights them all (row 4).
- **(e) Doc drift.** The held-key move and sustain steps report their refusals today
  (`chart_handlers.cpp`, `reportChartPlanRefusal` after `commitChartGestureStep`), so rows 18–20
  and the sustain path flash, which "Deliberately silent" and F9 above say they do not; and the
  junction toggle's comment ("flashes the selection it refused") predates rows 12–15/22–23.

### Candidate rules, applied

- **R1** — lights only what the refusal names (today).
- **R2** — the refused selection always lights, and the named culprits light beside it.
- **R3** — lights the culprit when one is named, else the selection (the 2026-08-28 optional form).

| Rows | R1 (today) | R2 | R3 |
|---|---|---|---|
| 9, 10, 24–26, 27 (first and third reasons), 28, 34, 35, 43, 44, 46, 50, 52, 55 | S exactly | same | same |
| 2, 3, 12, 13, 15, 18, 21–23 (one element of a wider S) | the named element(s) | all of S, the named element(s) within it | same as today |
| 19, 29 (S's keyframes dark) | the notes | all of S | all of S (no culprit) |
| 1 | ring + bounding head | + rest of S | the ring only: the edit's own product goes dark |
| 4, 7, 11, 14, 20 (S dark) | C only | S + C | same as today |
| 27 (short-ring reason) | the note | + the predecessor ring | the predecessor ring only: the note goes dark |
| 36, 45, 53, 54 (C dark) | S's chip | S's chip + C (holding placement/section, neighbour meter, short reign) | C only |
| 33, 42 (no S on screen, C dark) | nothing or a derived chip | C (+ S where a chip stands) | C |
| 5, 6, 8 (holds) | as notes | the hold itself + C | C, else the hold — every rule needs a hold target |
| 32, 37, 30 typed (red box) | the box | box + C flashed | open: C flashed instead of, or beside, the box |
| 38, 39, 49, 51, 56, 57, 58, 63, 64 (S on screen, nothing lit) | nothing | S (+ C) | C, else S |
| 16, 17, 31, 40, 41, 47, 48, 59–62, 65 (nothing on screen) | nothing | nothing | nothing |

Open per rule:

- **R1**: what does a flash with S dark communicate (finding a)? The charter sees an unrelated glow
  and must infer what they pressed was refused; rows 38–64 lighting nothing contradicts "the flash
  is for a key that did nothing".
- **R2**: does S double-light when S IS the named thing (rows 2, 12, 15, 34…) — one glow, or a second
  glow style to tell "refused" from "in the way"? With one style, S and C read as the same kind of
  mark.
- **R3**: row 1 and row 27 lose the subject; in rows 36/45/53/54 the moved chip, the thing the key
  addressed, goes dark — the same complaint as finding (a), relocated.
- **All**: rows 5–11 cannot be honoured without a hold target (see the requirement below).

### Stated requirement: every authored thing needs a glow target (user, 2026-10-20)

"holds as well as other chips like BPM/time sig/section will need to be able to flash too on
refusal." Not a question: every authored thing on the timeline needs a refusal-glow target.

| Target | Exists today | Reached by |
|---|---|---|
| Note, keyframe (tab lane) | yes | `flashChartRefusal` |
| Fret-hand position chip (tab lane) | yes | `ChartRefusalFlash::fret_hand_position` |
| Section chip (ruler) | yes | `flashSongSectionChip` → `flashRulerChipRefusal` |
| Time-signature chip (ruler) | yes | `refuseTimeSignatureVerb`, the meter move's commit refusal |
| Tempo (BPM) anchor chip (ruler) | yes, but used by the Tempo Lock only | `refuseLockedTempoAnchorMove`; rows 56–58 never reach it |
| Hold bracket / rails, tab lane | **no** | — |
| Hold furniture, 3D highway | **no** (no refusal flash exists in 3D; F4 ruled 2D only, which this requirement reopens for holds) | — |
| Tone-region chip | **no** (not in the user's list; recorded as fact) | — |

A refusal's light today cannot carry a ruler chip (`ChartRefusalLight` has no such field), so a
chart refusal whose culprit is a meter or tempo chip has no way to name it.

### Open questions for the ruling

1. **The hold target.** What glows for a hold — bracket, rails, both — on the tab lane and the
   highway, and does the hold light as itself instead of, or beside, its notes?
2. **The marker-lane gaps.** (a) A refused Add of a hand position (row 33) lights a derived chip
   standing there or nothing — light the culprit, the would-be chip, or nothing? (b) A refused time
   signature (rows 48, 49, 51) lights nothing. (c) A refused tempo edit (rows 56–58) lights
   nothing, though the tempo chip target exists. (d) Prompt-text refusals (rows 42, 47, 59) have
   nothing on screen: is the log the whole answer there?
3. **The truncation's two marks (row 1).** Keep both the ring and the head that cuts it, given one
   of the two is the edit's own product with no mark yet?
4. **The Note Lock's lights (rows 29, 30, 39, 46, 57, 63, 65).** The lock is the culprit and is not
   on the lane: light S, the lock's toolbar control, or both?
5. **The log line.** Does the log stay the only carrier of the full sentence (F5), now that a glow
   may stand for several different things at once (S, C, or both)?
6. **One port or two.** Should a refusal's light be able to name a ruler chip, so a chart refusal
   can light a tempo or meter culprit and the marker funnel's refusal can carry a light at all?
