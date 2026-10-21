# Refusal flash (task #278)

Status: BUILT 2026-09-30, awaiting its sighting (F1, F2); rebuilt 2026-10-20 to one rule for every
refusal on every surface (see "One rule for every refusal"). Written 2026-09-21 from a recovered
discussion — see "Provenance" — because the ruling had survived in one conversation only, and task
#278 had gone on describing the design the ruling replaced.

When an edit key is refused, the editor says so **on the things it concerned**: the thing the key
tried to change and the thing that stood in its way glow red a couple of times and stay as they
were. Why it was refused goes to the log. There is no status line, no toast and no caret callout —
that was the proposal on the table, and the flash was chosen instead of it.

Every claim about current code below was verified on 2026-09-21, and re-verified against
`05edcc5f` on 2026-10-20 from "Shape, as built" down. Re-verify before acting on one.

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
   surface's frame attachment steps it, a refusal arriving mid-flash joins the running one, and a
   new projection drops it. The level is `cos²(π·(pulses − ½)·t/T)`: lit at the keystroke, dark
   between pulses and at the end. The 3D preview restates the same pulse's level to the renderer
   every frame (`HighwayRenderer::setHighlight`).
5. **The log.** The Quill-backed `RH_LOG_*` facade, at `%APPDATA%/Rock Hero/Rock Hero Editor.log`,
   every record written by `RefusalLog` (`shared/refusal_log.h`). The app has no way to open it
   (F5): the reason is developer-visible until an "Open Log" entry exists, which is small and the
   user's to call.

**Deliberately silent:** an honest `PlanNoChange` — the sustain clamp at the next head and every
visible bound that plans as one; and a legato press that CLEARED, whose notes' refusals to set
were never asked for. A held-key gesture's step reports every refusal it meets, like any other key
(F8, F9).

## Open — the user's to rule

| # | Question | Recommendation |
|---|---|---|
| F1 | Pulse count and timing | BUILT at two pulses over 0.6 s (`g_refusal_flash_train` in `shared/flash_pulse.h`); sight and tune |
| F2 | Drawn geometry: the selection ring turning red, or a glow around it | BUILT as the ring turning red, then made a GLOW (user, 2026-09-30: wider, closer to the accent's halo): reach 0.2 of the mark's size, the accent halo's, after 0.45 sighted too wide; four layers (`g_refusal_glow_*`, `shared/flash_pulse.cpp`), drawn over the caret square (user, 2026-09-30) |
| ~~F3~~ | ~~Key repeat~~ | BUILT as recommended: a refusal arriving mid-flash joins the running one on its clock, so a held key reads as one pulse train |
| F8 | The fret shift at the neck's edge (`ShiftChartFrets` below fret 0 or past the last) flashes the selection, and so does a move off the neck (`planMoveSelection`: "the move would leave the neck or the grid") | Sight it: the fret or string on screen already says why, so under the bound ruling both may belong with the silent bounds |
| F9 | The move gesture's `PlanRefused` carries both visible bounds (the neck's edge, an occupied slot) and a real refusal (a landing that would strand a point), and the gesture reports every refusal, so all of them flash | If a bound should stay silent, it should plan `PlanNoChange` as the sustain clamp does, leaving `PlanRefused` for refusals only |
| F10 | A keyframe a per-note rule refuses flashes its NOTE's head, since the rule authority names notes, not keyframes | Sight it; naming the keyframe needs the rule authority to name one |
| F11 | A legato press that CLEARS flashes nothing, even where some selected notes could not have been claimed | Built silent: the press cleared, which never asked those notes to be set; the ruling's "applies to seven, flashes the three" reads either way for a clear |
| ~~F4~~ | ~~2D only?~~ | ~~RULED 2026-09-21: yes, 2D only~~ — superseded 2026-10-20: the span bars are targets on the 3D preview too, so the preview glows ("One rule for every refusal") |
| ~~F5~~ | ~~Reason to the log ONLY~~ | RULED 2026-09-21: yes, "for now" — the log line is the thing to surface if charters cannot find the why |
| ~~F6~~ | ~~Scope of "every selection"~~ | RULED 2026-09-21: notes and keyframes with #278; a marker verb gains it when it first has a refusal to report. The first is the Tempo Lock's refused anchor move (plan 66 Phase 1, 2026-10-04): the tempo chip glows on the ruler, on the same pulse and glow, which moved to `ui/src/shared/flash_pulse.h` (named `refusal_flash.h` until plan 65 Phase 3 gave its pulse a second, non-refusal use) so both surfaces share them. Since 2026-10-20 every chip of every marker kind is a target, through the one `IEditorView::flashRefusal` |
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

## One rule for every refusal (ruled 2026-10-20)

Built in `bdc466c4`, `03a4ec54`, `38154b3e`, `fc2fd3b3` and `05edcc5f`; everything below was read
against `05edcc5f`.

### The ruling

> A refusal flashes the thing you tried to change and the thing that stood in its way, and the log
> carries the sentence.

- **Where they are the same thing, it flashes once.** A refusal naming the very note, chip or hold
  the key acted on glows that one mark, never twice and never in a second style.
- **Where nothing stood in the way, only the subject flashes.**
- **Where there is no subject on screen** — a prompt's text, no chart loaded — **only the log
  speaks.**
- **Every authored thing on the timeline is a target:** notes and keyframes; every chip — the
  fret-hand position, the tempo anchor, the time signature, the section, the tone region; and the
  span and none-hold bars, on the 2D lane and on the 3D preview.

### The mechanism, as shipped

A refusal's light is one type, `RefusalLight` (`controller/refusal_light.h` under
`rock-hero-editor/core/src/`): `RefusalTarget`s of any kinds, each named exactly as the selection
names that kind (`ChartNoteKey`, `ChartKeyframeKey`, `SpanSelection`, `NoneHoldSelection`,
`FretHandPositionSelection`, `SongSectionSelection`, `TempoAnchorSelection`,
`TimeSignatureSelection`, `ToneRegionSelection`), so a refusal and a selection name a thing the one
way. Each refusing site adds what it knows (`PlanRefused::lit`, `ChartRefusedNote::lit`,
`TempoMapEditError::lit`), and the controller adds the subject it holds — the selection or operand
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
`EditorView::flashRefusal` hands each surface its share: the tab lane (`TabView::flashRefusal`) its
notes, keyframes, fret-hand position chips and span and none-hold rails; the ruler
(`TimelineRuler::flashRefusal`) its section, tempo and time-signature chips by `RulerChipKey`; the
tone row (`ToneTrackView::flashRefusal`) its regions by id; and the 3D preview
(`PreviewSurface::flashRefusal`) a `HighwayHighlight` of its spans' rails and its positions' floor
numbers. Every painter glows the set it is given and never knows subject from culprit. A typed tone
name that duplicates another is input the charter retypes, not a broken model: it is logged under
the verb's label and lights every region bearing the name (`reportedDuplicateToneName`), the prompt
having no subject on screen.

### Census

Every refusal at `05edcc5f`. **Subject** is what the controller adds — "sel" is the selection or
operand a chart verb planned over (a gesture's live selection), "hold" whatever was selected when
the hold verb ran — its notes and keyframes, or the span or none hold (a time selection adds
none), "chip" the marker the verb acted on;
**Culprit** is what the refusing site adds beyond it. A target the projection cannot resolve (a
note or chip the refused edit would have made) lights nothing, and "—" lights nothing. Every row
logs its reason. Paths are under `rock-hero-editor/core/src/`.

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
| 26 | `reportChartFretEntryRefusal` (a refused bare or `Alt` digit, a refused `Insert`) | the plan's reason | the entry's target: the ring a cut or point divides, the objects a retype addresses, the hand chip; nothing for an insert at an empty slot | whatever the plan's refusal lit (rows 1–3, 11, 12, 15, 19, 32, 33) |
| 27 | `replanChartFretEntry` (Note Lock) | Note Lock is on: … | the entry's target, as row 26 | — |
| 28 | `replanChartFretEntry` | no chart is loaded | — | — |
| 29 | `applyChartEditPlan` (Note Lock, every chart verb) | Note Lock is on: … | the notes the plan rewrites or removes, and the holds and positions it drops or changes; an insert at an empty slot logs alone | — |
| 30 | `chart/shape_hold_handlers.cpp` `ChooseHold` | nothing is selected to hold | — | — |
| 31 | `chart/fret_hand_handlers.cpp` Move, Delete (derived) | P is derived, so nothing is stored there | chip | — |
| 32 | `FretHandPositionsSnapshot::validate` (coverage), via Add at cursor, Move, either fret, Clear End | P does not cover … | chip; for the Add, the place, which lights a derived chip standing there | P's chip and the notes gripping every stop it misses |
| 33 | `FretHandPositionsSnapshot::validate` (stream rules) and the Note Lock, via the same verbs and Delete | the rule's message / Note Lock is on: … | chip, as row 32 | — |
| 34 | Move (`moveSelectedFretHandPosition`) | fret-hand position would leave the chart | chip | — |
| 35 | Move, the funnel's refusal | occupied start, past the end, as rows 32–33 | chip | the chip at the landing, where one stands, and row 32's culprits |
| 36 | `PruneFretHandPositions` | no stored hand position is redundant | — | — |
| 37 | `PruneFretHandPositions`, the funnel's refusal | the rule's message / Note Lock is on: … | — (every stored position) | row 32's culprits |
| 38 | `timeline/section_handlers.cpp` Insert, the funnel's refusal | empty name, off the grid, at or past the terminal, place held, Note Lock | the place, which lights the section holding it: subject and culprit at once | — |
| 39 | Rename, Delete, the funnel's refusal | blank name / Note Lock is on: … | chip | — |
| 40 | `moveSelectedSongSection` | section would leave the song | chip | — |
| 41 | `moveSelectedSongSection`, the funnel's refusal | onto the terminal, place held, Note Lock | chip | the section holding the landing place |
| 42 | `timeline/meter_handlers.cpp:onTimeSignatureSetRequested` | not a time signature: text | the chip of the measure it was typed on | — |
| 43 | `SetTimeSignature` | a time signature cannot start at or past the end of the song | — | — |
| 44 | `SetTimeSignature`, the grid funnel (`SongTimelineSnapshot::validate`) | the timeline rule's message (a reign shorter than one of its measures, …) | the chip of the reign a Set restates; an Add's beat holds none | the anchors and meters a tempo-map rule names, the regions a tone rule names; a section or chart rule names none |
| 45 | `deleteSelectedTimeSignature` | the time signature at measure 1 cannot be deleted / the measure N meter is derived | chip | — |
| 46 | `deleteSelectedTimeSignature`, the grid funnel | as row 44 | chip | as row 44 |
| 47 | `moveSelectedTimeSignature` | measure 1 cannot move / derived / to or past the end of the song | chip | — |
| 48 | `moveSelectedTimeSignature` | a time signature cannot step onto or past its neighbour | chip | the neighbour's chip |
| 49 | `moveSelectedTimeSignature`, the funnel (`commitMarkerMove`) | as row 44 | chip | the chip at the landing measure, where one stands, and row 44's culprits |
| 50 | `timeline/tempo_handlers.cpp:onTempoSpanBpmRequested`, `onScaleTempoRequested` | not a number: text / the parse's message | the anchor the tempo was typed on / — | — |
| 51 | `refuseLockedTempoAnchorMove` (nudge, each drag event, release) | Tempo Lock is on: … the tempo anchor at … is blocked | chip | — |
| 52 | `MoveTempoAnchor` drag event | the edit's message | chip | the anchors and meters a broken rule names |
| 53 | `commitTempoEdit`, the edit's refusal (Add, Set Tempo, Move, Delete, Scale with the lock off) | occupied, at or past the terminal, onto a neighbour, the first anchor, the tempo, a broken rule | the anchor addressed — Add's place lights an anchor standing there; Scale's subject is the whole map, which no chip draws | the anchors and meters a broken rule names (`TempoMapEditError::lit`) |
| 54 | `commitTempoEdit`, the funnel's refusal (`TempoMapSnapshot`, or `SongTimelineSnapshot` with the Note Lock on) | the rule's message | as row 53 | as row 44 |
| 55 | Scale Tempo with the lock on (`scaledTimeline`) | the relabel's message | — | — |
| 56 | Scale Tempo with the lock on, the grid funnel | as row 44 | — (the whole song) | as row 44 |
| 57 | `tone/tone_handlers.cpp` Create Tone Region, Add Tone: the core edit or the funnel | the core's message / the rule's / Note Lock is on: … | the region holding the place, which the insert splits | the regions the error names |
| 58 | Delete Tone Region, its sole-region reset, Change Tone of Region: the core edit or the funnel | as row 57 | the region | the regions the error names |
| 59 | Move Tone Boundary (drag): the core edit or the funnel | as row 57 | the region the boundary opens | the regions the error names |
| 60 | `moveSelectedToneRegionStart` | tone change would leave the song | the region | — |
| 61 | `moveSelectedToneRegionStart`: the core edit or the funnel (`commitMarkerMove`) | as row 57 | the region | the regions the error names |
| 62 | `ToneModelSnapshot::validate` (catalog), via the tone verbs | tone is not in the arrangement catalog | as the verb's | the region pointing outside the catalog |
| 63 | Rename Tone, the funnel's refusal | the rule's message / Note Lock is on: … | — (a catalog tone, which no chip draws) | the regions the error names |
| 64 | `reportedDuplicateToneName` (Rename Tone, Add Tone, a retone to a new tone) | A tone named "…" already exists in this arrangement. | — | every region bearing the name |
| 65 | `SetToneAutomationPoints` (Note Lock) | Note Lock is on: … | — | — |
| 66 | `controller/marker_model_commit.h` (`landMarkerModel`, `commitMarkerMove`) | no model to apply onto | as the verb's | — |

Not refusals, and outside the table: a mint failure is an error dialog (`reportError`); an action
naming a tone region, plugin instance or automation write the model no longer holds is ignored with
an `RH_LOG_WARNING`; an honest `PlanNoChange` reports nothing. The grid funnel's coverage refusal
(rows 44, 46, 49, 54, 56), which lit no culprit, is retired: positions yield to the grid — a tempo
or meter edit drops a position it leaves uncovering and logs it (`SongTimelineSnapshot::normalize`).

### What is not a target

- **The lock toolbar controls.** The Note Lock and the Tempo Lock refuse as culprits that are
  nothing on the timeline; the log names the lock, and the flash lights what the lock froze.
- **Automation points and lanes.** A refused automation write logs, and lights nothing.
- **A prompt's text, and anything with no mark on screen:** a typed meter, tempo, factor or tone
  name; a catalog tone; the whole tempo map or song (Scale Tempo); the stored positions as a whole
  (Prune); an edit with no chart loaded.
- **What the refused edit would have made.** It has no mark yet, so its target resolves to nothing;
  the authored thing it ran into is what glows.
- **On the 3D preview: notes, keyframes, the ruler's and tone row's chips, and none holds.** The
  preview glows the spans' rails and the hand positions' floor numbers; a none hold draws nothing
  on the highway, and the rest glow on the 2D surfaces.
