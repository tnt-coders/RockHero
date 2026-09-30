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

1. **Core: refusals carry reasons and identities.** `ChartPlanRefusal` is
   `variant<ChartPlanNoChange, ChartPlanInvalid>`, and only the refusal carries a reason. The
   per-note verbs return `ChartSelectionPlan`, whose `refused` names each note they could not
   write. In `planNoteWrite` a verb's write either refuses a note with a reason (the harmonic's
   "no node is reachable") or leaves the no-op test to find "already so", so a no-op is never a
   refusal; a note the per-note rules refuse is named with the rule's message.
2. **Controller: one report path.** `reportChartPlanRefusal` logs a whole-plan refusal and flashes
   the selection; `reportChartRefusedNotes` logs each refused note and flashes those; both reach the
   view through `flashChartElements`, which resolves keys through the two authorities the
   selection is published by. The funnel `applyChartEditPlan` reports, and its per-note overload
   reports the notes AFTER applying the rest, because the flash is addressed in the projection the
   apply published. The junction toggle, `Insert`'s refused cut (the ring's note, since nothing was
   typed to box) and the harmonic picker's "nothing on offer" report through the same two.
3. **Port: a one-shot effect**, `IEditorView::flashChartRefusal(ChartRefusalFlash)`, not view state.
   The test fake records each flash, which is what controller tests assert.
4. **View: the tab lane.** The selection ring, stroked again in `EditorTheme::invalid` at the
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
| F1 | Pulse count and timing | BUILT at two pulses over 0.6 s (`g_refusal_flash_pulses`, `g_refusal_flash_seconds` in `tab_view.cpp`); sight and tune |
| F2 | Drawn geometry: the selection ring turning red, or a glow around it | BUILT as the ring turning red, the smaller build; sight it |
| ~~F3~~ | ~~Key repeat~~ | BUILT as recommended: a refusal arriving mid-flash joins the running one on its clock, so a held key reads as one pulse train |
| F8 | The fret shift at the neck's edge (`ShiftChartFrets` below fret 0 or past the last) flashes the selection, while a move stopping at the edge is silent | Sight it: the fret on screen already says why, so under the bound ruling it may belong with the silent bounds |
| F9 | The move gesture's `Invalid` carries both visible bounds (neck edge, occupied slot) and a real refusal (a landing that would strand a point), so the gesture stays silent for both | If a real move refusal needs its flash, a bound should plan `NoChange` as the sustain clamp does, leaving `Invalid` for refusals only |
| F10 | A keyframe a per-note rule refuses flashes its NOTE's head, since the rule authority names notes, not keyframes | Sight it; naming the keyframe needs the rule authority to name one |
| F11 | A legato press that CLEARS flashes nothing, even where some selected notes could not have been claimed | Built silent: the press cleared, which never asked those notes to be set; the ruling's "applies to seven, flashes the three" reads either way for a clear |
| ~~F4~~ | ~~2D only?~~ | RULED 2026-09-21: yes, 2D only |
| ~~F5~~ | ~~Reason to the log ONLY~~ | RULED 2026-09-21: yes, "for now" — the log line is the thing to surface if charters cannot find the why |
| ~~F6~~ | ~~Scope of "every selection"~~ | RULED 2026-09-21: notes and keyframes with #278; a marker verb gains it when it first has a refusal to report |
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
