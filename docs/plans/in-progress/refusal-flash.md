# Refusal flash (task #278)

Status: DIRECTION RULED, realization open. Written 2026-09-21 from a recovered discussion — see
"Provenance" — because the ruling had survived in one conversation only, and task #278 had gone on
describing the design the ruling replaced.

When an edit key is refused, the editor says so **on the thing that refused it**: the selected
elements the verb turned down glow red a couple of times and stay as they were. Why it was refused
goes to the log. There is no status line, no toast and no caret callout — that was the proposal on
the table, and the flash was chosen instead of it.

Every claim about current code below was verified on 2026-09-21. Re-verify before acting on one.

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
flash fires on a real refusal only, never on an honest no-op (`ChartPlanRefusal::Invalid`, never
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

- **Only one of the four payloads exists.** `ChartLegatoPlan{plan, skipped, reason}`
  (`rock-hero-editor/core/src/chart/chart_edits.h`) is produced and read nowhere outside tests. The
  harmonic picker's skip reason is three bare `return`s in `chart_handlers.cpp`; the slide-tail
  clip is `clipPayloadsToSustain` returning `void`; the mixed-validity count is doc-only.
- **The count-plus-dominant-reason shape is the wrong shape for a flash.** A flash needs the
  refused elements' identities. `planSetLegato` already walks each note and knows each one's
  reason; it folds that into a tally and then a single dominant reason, discarding what the flash
  and the log both need. Returning the refused notes with their reasons REMOVES the tally array and
  the dominant-reason fold, and the count is the list's size. One datum, stored once.
- **§9a's "applied to 7 of 8" sentence has no screen to live on** and needs none: the seven changed
  and the one glowed. "Never silent partial application" is met by the flash. The sentence becomes
  the log line.
- **A whole-plan refusal has no per-note attribution** (`validateChartNotes` names a rule, not a
  note; relational refusals belong to a pair). The ruling already covers it: "every selection where
  the action was refused" — when the whole plan is refused, the whole selection flashes.

## Shape

Settled by the ruling and the code as it stands; small enough to build in one pass.

1. **Core: refusals carry identities.** A verb's outcome names the selected elements it turned
   down, each with its reason. `ChartLegatoPlan` changes to that shape first; the other per-note
   verbs (`planSetAttack`, `planSetHarmonic`, `planSetNoteFlag`, the keyframe verbs) follow the
   same return as they are wired. A whole-plan `Invalid` refuses every selected element.
2. **Controller: one report path.** One function takes a refusal — elements and reasons — writes
   the log lines and asks the view to flash. Every verb head calls it; none formats its own
   message.
3. **Port: a one-shot effect**, beside `showError` and the node picker on `IEditorView`, not view
   state. The pulse is presentation with a lifetime of its own; putting it in `EditorViewState`
   would make every re-derivation ask whether it is the same flash. The test fake records the
   flashed elements, which is what controller tests assert.
4. **View: one pulse driver in the tab lane** that draws the glow over the named elements and
   retires itself. Red is `EditorTheme`'s existing `invalid` role — the role W3's pending entry
   already uses for "this will not apply". The driver must respect the VBlank-runs-before-paint
   ordering that has frozen memoised values before.
5. **The log.** Verified 2026-09-21: the editor has a durable one — the Quill-backed
   `RH_LOG_*` facade (`rock_hero/common/core/shared/logger.h`), rotating at
   `%APPDATA%/Rock Hero/Rock Hero Editor.log` — and NO way to open it from the app: the menu bar
   is File / Edit / View, and the path reaches a charter only inside the load-repair notice. So
   under F5 the reason is developer-visible until an "Open Log" entry exists. That entry is small
   and is the user's to call.

Follow `docs/developer/adding-an-editor-ui-view.md` Part B for the silent steps.

## Open — the user's to rule

| # | Question | Recommendation |
|---|---|---|
| F1 | Pulse count and timing | Sight two and three pulses at a few periods in the built editor; no number is worth guessing |
| F2 | Drawn geometry: the selection ring turning red, or a glow around it | Sight both; the ring is the smaller build |
| F3 | Key repeat — a refusal arriving while a flash is still running | Let the running flash finish; a held key then reads as one steady pulse train instead of a strobe. Much smaller since the bound ruling: the held-key gestures (resize, move) stop silently, so what repeats is a toggle verb |
| ~~F4~~ | ~~2D only?~~ | RULED 2026-09-21: yes, 2D only |
| ~~F5~~ | ~~Reason to the log ONLY~~ | RULED 2026-09-21: yes, "for now" — the log line is the thing to surface if charters cannot find the why |
| ~~F6~~ | ~~Scope of "every selection"~~ | RULED 2026-09-21: notes and keyframes with #278; a marker verb gains it when it first has a refusal to report |
| ~~F7~~ | ~~40-Q5~~ | RULED 2026-09-21: a silent floor, no flash — see "A visible bound is not a refusal" above. The lock itself was ruled 2026-08-09 |

**A second consumer, RULED 2026-09-21 (user): it flashes.** An `Alt` digit typed at a ring's exact
end naming the fret already in force is a `NoChange` no-op: the release it would state says
nothing, and the plan gate dissolves such a release in the same edit (`dissolveSilentRelease`). The
key does nothing for a reason the screen does not show, which is the flash's own line, so the note
flashes. It also corrects the rider above: the trigger is not `Invalid` versus `NoChange` but
whether the screen already explains the nothing — a toggle that finds its claim already set is an
honest no-op, this press is a refusal that happens to plan as one.

One unreconciled line from 2026-09-05 suggested the flash could "hint `Shift+S` as the verb they
actually want" — on-screen text, which contradicts F5. It was never put to the user and is not part
of the design.

## Provenance

The discussion is one exchange, 2026-08-28, in session `ee6f4455-2b03-47c3-95ff-52031ad22341`. It
was deferred by the user until the derivation package landed, the assistant said it was recorded,
and it was not: the task store kept the pre-ruling wording. Recovered from the transcript on
2026-09-21. A parallel design review run the same day, before the recovery, recommended a fixed
text line under the transport; it is superseded by the ruling and kept out of this plan.
