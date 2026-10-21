# Plan 66 — Tempo Repair Tools

**Status:** DONE 2026-10-09, sighted by the user (the Note Lock, Scale Tempo in both forms, and
Scale Tempo's undo leaving the window and the cursor in place, `1beb3e5f`). Phase 4 (Scale Tempo
obeys the Note Lock): the unlocked stretch rides the typed tempo's commit path (`scaleTempoMap`
in `tempo_map_edits`), and the locked relabel is unchanged. Phase 1 (`73ffab27`, the Tempo
Lock), Phase 2 (`799d2a12`, Scale Tempo, on the generalized `readdressTimeline`), and Phase 3 (the
Note Lock, the commit after `4f1cb4c9`; renamed from "Keep Notes in Time" on 2026-10-04). Open
for the user:
- the sighting of all three tools, including the chip drag's preview-then-land gesture in both
  lock states (decision 7, resolved overnight on the recommendation and widened the same day);
- the strings: "Scale Tempo...", "Scale factor (e.g. 2, 1/2, 3/2)", "Scale", the factor bound
  of 16;
- the whole-song verbs' cost at 50k notes (the Note Lock ≈ 300 ms against plan 64's
  proposed 250 ms; see plan 64 §6).

Ruled by the user on 2026-10-03 as follow-ups to plan 41 (its "Ruled
follow-ups" section, moved here the same day; git history keeps that text), placed after the
projection-cost fix. Builds on `docs/plans/roadmap/65-time-signature-editing.md`'s whole-song
re-addressing transform and snapshot undo, so it is **built after plan 65 or with it**, and so
also after plan 60. Re-verify against the code before building.

## Goal

Fix a song whose grid is wrong underneath correctly timed notes — a tempo tabbed at half speed, a
wrong grid under a performance played without a click — which is common, and which "the grid is
truth" (plan 41 decision 4) cannot do alone: every tempo edit there moves content with its beat.
Three tools give the charter the other direction, keeping TIME and re-addressing the grid.

## Non-goals

- No range-scoped Scale Tempo (the user's ruling defers it: "a range-scoped form is later").
- No onset detection or automatic beat tracking (plan 41's deferred Phase 5).
- No persisted toggle state: neither toggle is saved, in the song or in settings.
- No new song.json key.

## Constraints

- Plan 65's constraints (a), (b), (f) and (h) apply unchanged.
- **Whole-song verbs commit once.** Scale Tempo and every tempo edit made with the Note Lock
  on re-address the whole song: the album-scale analysis
  (`docs/plans/roadmap/64-album-scale-performance.md`) measured ~150–250 ms per whole-song
  re-address at 50k notes, and proposes a ≤ 250 ms bound per whole-song verb (64-Q4). So each
  commits ONCE per command, or on drag RELEASE, never per pointer event and never per held key
  repeat.

## The rulings (user, 2026-10-03)

- **Scale Tempo** (Edit menu "Scale Tempo...", and the tempo row's right-click menu): every grid
  address in the song — anchors, notes, rings, keyframes, hand markers, sections, tone changes,
  automation points — is scaled by a factor on the whole-note axis while every TIME stays put. x2
  doubles every span's tempo and the measure count (a meter change at old bar m lands on new bar
  2m-1); /2 and free factors (3/2) round to the tick lattice. A range-scoped form is later.
  **AMENDED 2026-10-09 (user): Scale Tempo obeys the Note Lock like every other tempo verb.**
  The paragraph above is the verb with the Note Lock ON: a relabel, nothing moves in time. With
  the Note Lock OFF it STRETCHES: every span's tempo × f, every anchor keeping its grid address
  and its milliseconds rescaled about the first anchor (`t0 + (t − t0) / f`, whole milliseconds),
  every object keeping its grid address and so moving in time with the beat; the terminal grows
  to cover the audio exactly as a typed tempo's does (`terminalCovering`). The use is a chart that
  drifts uniformly against its recording (a GP tempo of 120 under a 125 BPM take). Fixing a
  half-time chart now asks for the lock first; unlocked x2 desyncs the chart visibly and is one
  undo away. Phase 4 builds it.
- **Note Lock** (toolbar toggle, OFF by default, session state; named "Keep Notes in Time" until
  the user renamed it on 2026-10-04 to read with Tempo Lock): while on, a tempo edit keeps every
  object's time and re-addresses its grid position, instead of moving it with the beat. It applies to tempo edits only (a meter edit already keeps time under 65-Q1 = A). The
  workflow for a non-click song: turn it on, sync anchors to the audio, turn it off.
- **Tempo lock** (toolbar toggle, ON by default for every song each session, never saved): while
  locked, the tempo map and the time signatures are frozen: every tempo and meter verb is
  refused — an anchor or meter move (chip drag, its release, `Alt+←/→`), adding, deleting or
  retyping an anchor or a meter, the tempo and meter chords, and Scale Tempo in either form. A
  refused verb flashes the selection and the lock's own toolbar button, and logs one line.
  **AMENDED 2026-10-20 (user), twice:** the lock had blocked only the moves; it now freezes the
  whole map and every meter, and the lock's button glows as the thing that stood in the way.
  **The lock answers first** (ruled the same day): while it is on, its refusal is a verb's first
  answer, before any planning, bound or prompt, and no prompt opens. Which lock governs which
  action is stated once, in the action table (`governingLock`,
  `controller/editor_action_availability.h`), and read once, at dispatch (`refuseLockedAction`);
  the chords, Enter and Ctrl+R ask the same table through the action their answer runs. Undo and
  redo bypass both locks on purpose: a lock guards new intentions, and undo only reproduces
  history already consented to; the toggles themselves are not undoable. The deleted 2026-07-09
  grid lock blocked every tempo edit too; what sets this one apart is the visible, one-click
  toggle beside the grid control.

## Decisions

1. **One transform, two drivers.** Scale Tempo and the Note Lock both run plan 65's
   whole-note re-addressing transform — Scale Tempo with a factor, the Note Lock with the old
   and new maps — over the same slice list, and both commit through plan 65's whole-song snapshot,
   one undo entry per command. Neither restates the slice list.
2. **A Note Lock drag commits on release.** The chip and grid follow the pointer and the
   song is re-addressed ONCE at release, from the drag's starting map, as one entry — which also
   rounds every position to the tick lattice once instead of compounding the rounding per event
   (plan 64's 64-Q5, recommendation B). Decision 7 (2026-10-04) made the toggle-off drag the same
   gesture: it too commits once, on release, where plan 41 committed it per event.
3. **The lock freezes the tempo map and the time signatures** (amended 2026-10-20; it had blocked
   only the chip drag and `Alt+←/→`) and is ON by default at every song open, every session, never
   saved. Its refusal is a verb's first answer, before any bound or prompt. A refused verb gets the
   refusal flash (red glow on the selection and on the lock's toolbar button, plus the log line,
   `docs/plans/in-progress/refusal-flash.md`), never a status line or toast.

4. **The two toggles' icons and tooltips (user, 2026-10-04).** Material Symbols Outlined style,
   weight 400, in `rock-hero-editor/ui/resources/` beside the transport icons: Tempo Lock is
   Material's `lock_clock` (on) / a custom `lock_clock_open` (off); Note Lock is a custom
   `lock_note` (on) / `lock_note_open` (off) — filled glyph when on, outlined with the open
   shackle when off. The on state is a 1 px `accent` ring with 3 px corners and no fill, the
   editor's selection mark; disabled is the whole button at 0.4 alpha; a small `DrawableButton`
   subclass paints it. The four final SVGs are in `docs/plans/roadmap/66-assets/`, already in the
   shipped icons' form; Phase 1 moves the `lock_clock` pair into `resources/` and its
   binary-data list, Phase 3 the `lock_note` pair, and the folder is deleted with the second move.
   **Placement (UI review, 2026-10-04):** right of the Grid selector as one pair, Tempo Lock
   first: `[Arrangement ▾]·8·[Grid ▾]·8·[TL]·4·[NL] … [▶ ■ readout] … [meter]`. The pair is a
   grid/tempo editing mode beside the grid control it shapes, apart from the playback group; the
   toggles get a "Grid & Zoom" menu row each beside Grid Snap, not instead of the button (a
   default-ON latched mode needs an on-screen indicator). The pair adds 76 px; at the 1280 px
   minimum the transport stays centred. Sighting falsifiers: at 1280 and 1200 px the pair groups
   with Grid, not with play, nothing overlaps, and the ON ring does not read as a box like the Grid
   combo's same-geometry outline.
   Tooltips, verbatim:
   - "Tempo Lock: while on, the tempo map and time signatures are frozen: no anchor or time
     signature can be added, moved, retyped, scaled or deleted." (amended 2026-10-20 with the
     lock's scope)
   - "Note Lock: while on, tempo edits leave notes where they sound in the audio and move the grid
     under them." (renamed from "Keep Notes in Time" on 2026-10-04)

5. **One transform, generalized (2026-10-04, against plan 65 as built).** Plan 65's
   `readdressUnderMeters` maps every slice by identity on the whole-note axis under a new placed
   meter list. Both tools here are the same pass with a different map, so the one function is
   generalized rather than joined by a second:
   - It takes the new map stated on the whole-note axis: the placed meters (`PlacedMeter`) and the
     placed anchors (a whole-note position plus milliseconds).
   - It also takes a content map φ from old whole-note position to new whole-note position.
   - It derives measure numbers for both meters and anchors, maps every content slice through φ
     (rounding to the tick lattice through `nearestTick` only where φ is not exact), sets each ring
     and keyframe offset to the whole-note distance between its mapped onset and mapped end (so
     identity leaves it untouched, since `987b62cf` made them note values), snaps sections, and
     re-seats the terminal.
   - Its three drivers:
     - *meter edit*: φ = identity, with the anchors placed where they stood;
     - *Scale Tempo* by f: φ(w) = f·w, with meters and anchors placed at f·w and milliseconds
       held;
     - *the Note Lock*: anchors placed by the tempo verb's result, meters where they stood
       in measure terms, and φ(w) = the new map's whole-note position at the old map's time of w.
   - The cursor is carried as one more slice, through the transform's own mapping (φ, the
     lattice, the address under the new map). That is the path content takes, so the cursor stays
     on the note it sat on. Carrying it by seconds was built and reverted (Scale Tempo review,
     2026-10-04): the terminal re-seat re-rounds the last span's milliseconds, so a seconds-held
     cursor near the end could land a tick off its note.
6. **Q2 resolved by plan 65's ruling** (65-Q3: meters keep their time and an indivisible reign
   refuses). Scale Tempo places every meter at f·w, so a reign of k measures becomes f·k measures.
   When that is not whole before a later meter, the factor is refused, naming the meter. A
   single-meter song is never refused; its end re-seats. The terminal re-seats through plan 65's
   `reseatTerminalAnchor`.
   **SUPERSEDED by plan 67 (§6 items 3–4, built 2026-10-09):** a reign of f·k measures that is
   not whole ends in a derived short bar and refuses nothing; the only refusal left is a factor
   whose reduced denominator is not a power of two carrying a meter off the meter lattice
   (1/256), reported naming the meter. The terminal is never re-seated; `reseatTerminalAnchor`
   is deleted and the song may end on a short final measure.

7. **Q1 resolved (2026-10-04, overnight, on the recommendation), then widened the same day to
   every chip drag:** a chip drag is ONE gesture — hold, preview, land once — with the Note Lock
   on or off. The widening followed the measured drag lag on a 766-note song: a commit per pointer
   event (about 5–6 ms) kept the message queue fed, so the vblank paint never ran until the
   pointer paused.
   - **During the drag:** the session, the history and the engine are not written; the core
     holds the last legal time. The view state publishes two maps beside the session's: the
     **grid map** (the map the release would land), which the grid, the ruler and its chips draw
     from, and the **content map** — the grid map with the toggle off, so content
     moves with the beat, and the session's map with it on, so content holds its time. Every
     content surface re-times its published seconds to the content map at its own
     seconds-to-pixel seam (`TempoMap::secondsRetimedFrom`); the view has no lock branch and the
     waveform never re-times. Onsets and ends are exact per event; notation a law states in
     seconds (a tail's crop, the kept-sustain floor, a head margin) is off by the drag's tempo
     ratio until the release. The cursor rides the chip (user, 2026-10-04): it is drawn on the
     held anchor's time, where the release seeks it, with no seek or scroll per event. No
     whole-song work runs per pointer event (plan 64 decision 2), and nothing on screen shows
     anything moving that release will not move.
   - **At release:** the one commit an `Alt+←/→` press makes, from the drag's starting map: the
     map alone with the toggle off, one re-address through decision 5's transform with it on.
     ONE undo entry, one engine resync, one re-projection; a drag released where it began
     records nothing.
   - **Key runs are unchanged:** `Alt+←/→` lands per press (a key run has no release), so its
     run coalescing stays.
   - **Rejected:** re-addressing per event from the drag's origin. It is correct, but it is
     whole-song work per pointer event (about 100 ms at 50k notes).

## Open questions (build-session)

- **Q1 — what a Note Lock drag shows before release.** Content must not appear to move
  with the beat mid-drag when it will not after release. Settle at build time against the cost
  measured then.
- **Q2 — meters and the terminal under a non-integer factor.** /2 can land a meter change between
  downbeats and the terminal off one; the ruling says only "round to the tick lattice". Decide
  whether such a meter moves to the nearest downbeat or the factor is refused, and re-seat the
  terminal with plan 65's `reseatTerminalAnchor`.
  **ANSWERED by plan 67 (67-Q6, built 2026-10-09):** a meter landing between downbeats cuts a
  derived short bar and is fine; a meter carried off the meter lattice refuses, naming it; the
  terminal needs no rule and is never re-seated.

## Dependencies

- **Plan 65**: the transform (Phase 1) and the whole-song snapshot (Phase 2). Build after it or
  with it.
- **Plan 41** (built): the anchor verbs these tools drive, the chip drag, the tempo row's
  right-click menu.
- **Plan 64**: the measured whole-song cost that fixes the commit-once rule.
- **The refusal flash** (built 2026-09-30): the lock's refusal.

## Phased implementation

Each phase is a coherent commit (or small series), gets a simplicity review before its commit,
and verifies with a build and the touched tests.

### Phase 1 — Tempo lock

- A session-state toggle in the core, ON at every song open and never saved; the toolbar button
  and its command. Every tempo and meter verb is refused while it is on, first, through the
  refusal flash, which glows the selection and the lock's button (amended 2026-10-20: it had
  refused only `Alt+←/→` and the chip drag, passing insert, delete, typed BPM and Scale Tempo, and
  every meter verb).
- **Tests:** the lock is on after open; it refuses the nudge, every drag event, a drag's release,
  insert, delete, typed BPM and Scale Tempo in both forms, and the meter set, insert, move, delete
  and chord, each with a flash naming the lock and no undo entry, while each verb lands with the
  lock off; a locked chord opens no prompt; it is absent from the saved package and the settings
  file.
- Independent of plan 65; it may land first.

### Phase 2 — Scale Tempo

- A pure function over the song through plan 65's transform: every grid address scaled by the
  factor on the whole-note axis, every time held, positions rounded to the tick lattice. The Edit
  menu item and the tempo row's right-click row open a factor prompt (x2, /2, a free fraction).
- **Tests:** x2 then /2 round-trips a lattice-aligned song exactly; times identical within half a
  tick; a meter at bar m lands at 2m-1 under x2; every result passes the rules; one undo entry.

### Phase 3 — Note Lock

- The toolbar toggle (OFF by default, session state). With it on, the move, typed BPM and delete
  verbs re-address content against the new map so its times hold; a drag commits on release.
- **Tests:** each tempo verb with the toggle on leaves every resolved time unchanged within half a
  tick and is one undo entry; with it off, plan 41's behaviour is bit-identical.
- **Sighting:** a non-click song: turn the toggle on, sync anchors to the audio by drag and nudge,
  turn it off; the notes never move in time and the grid lands under them. Scale a half-speed tab
  by x2 and confirm playback is unchanged. Try to move an anchor with the lock on and see it
  refused.

### Phase 4 — Scale Tempo obeys the Note Lock (amended ruling, 2026-10-09)

- Lock ON: today's verb, unchanged (the relabel; it re-engages the lock on what it wrote). Lock
  OFF: the stretch. Anchors keep their addresses with milliseconds `t0 + (t − t0) / f` rounded to
  whole milliseconds, content keeps its addresses (`keptPlace`), meters keep their places, and
  the terminal grows to cover the audio as a typed tempo's does. No lattice refusal applies
  unlocked: nothing is re-placed. A factor that would collapse two anchors onto one millisecond
  refuses through the tempo-map rules at the commit.
- One verb, one prompt, one menu row; the lock is the one switch. The prompt and the undo label
  say which form ran, so the charter is never surprised by a desync.
- **Failing tests first:** unlocked x2 halves every anchor's distance from the first in
  milliseconds, doubles every span's BPM text, and keeps every note, section, meter and anchor
  address; every object's time moves exactly as its beat does; x2 then /2 restores every
  original anchor's milliseconds within the whole-millisecond rounding; the terminal covers the audio after /2.
  Locked behaviour is bit-identical to today (its suite green). One undo entry each.

## Final acceptance

As separate invocations: a build, the touched tests, and `pre-commit run --all-files`; clang-tidy
when the user asks. Measure Scale Tempo and a Note Lock drag release on the largest local
import on relwithdebinfo, and report the numbers.

## Rollback/abort notes

- Format-neutral; the toggles are never saved, so reverting removes them without trace.
- Every whole-song edit is one snapshot entry; `Ctrl+Z` takes it back whole.
