# Plan 66 — Tempo Repair Tools

**Status:** Not started. Ruled by the user on 2026-10-03 as follow-ups to plan 41 (its "Ruled
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
- **Whole-song verbs commit once.** Scale Tempo and every tempo edit made with Keep Notes in Time
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
- **Keep Notes in Time** (toolbar toggle, OFF by default, session state): while on, a tempo edit
  keeps every object's time and re-addresses its grid position, instead of moving it with the
  beat. It applies to tempo edits only (a meter edit already keeps time under 65-Q1 = A). The
  workflow for a non-click song: turn it on, sync anchors to the audio, turn it off.
- **Tempo lock** (toolbar toggle, ON by default for every song each session, never saved): while
  locked, anchors cannot be MOVED in time (chip drag, `Alt+←/→`); adding one, deleting one, typing
  a tempo and Scale Tempo stay available. A blocked move is refused visibly. This replaces the
  deleted 2026-07-09 grid lock, whose fault was blocking every tempo edit by default.

## Decisions

1. **One transform, two drivers.** Scale Tempo and Keep Notes in Time both run plan 65's
   whole-note re-addressing transform — Scale Tempo with a factor, Keep Notes in Time with the old
   and new maps — over the same slice list, and both commit through plan 65's whole-song snapshot,
   one undo entry per command. Neither restates the slice list.
2. **A Keep-Notes-in-Time drag commits on release.** With the toggle off, plan 41's chip drag
   stays as built (the anchor moves live, one entry). With it on, the chip and grid follow the
   pointer and the song is re-addressed ONCE at release, from the drag's starting map, as one
   entry — which also rounds every position to the tick lattice once instead of compounding the
   rounding per event (plan 64's 64-Q5, recommendation B).
3. **The lock blocks only moves** — the chip drag and `Alt+←/→` — and is ON by default at every
   song open, every session, never saved. A refused move gets the refusal flash (red glow on the
   refused selection plus the log line, `docs/plans/in-progress/refusal-flash.md`), never a status
   line or toast.

4. **The two toggles' icons and tooltips (user, 2026-10-04).** Material Symbols Outlined style,
   weight 400, in `rock-hero-editor/ui/resources/` beside the transport icons: Tempo Lock is
   Material's `lock_clock` (on) / a custom `lock_clock_open` (off); Keep Notes in Time is a custom
   `lock_note` (on) / `lock_note_open` (off) — filled glyph when on, outlined with the open
   shackle when off. The on state is a 1 px `accent` ring with 3 px corners and no fill, the
   editor's selection mark; disabled is the whole button at 0.4 alpha; a small `DrawableButton`
   subclass paints it. The four final SVGs are in `docs/plans/roadmap/66-assets/`, already in the
   shipped icons' form; Phase 1 moves the `lock_clock` pair into `resources/` and its
   binary-data list, Phase 3 the `lock_note` pair, and the folder is deleted with the second move.
   **Placement (UI review, 2026-10-04):** right of the Grid selector as one pair, Tempo Lock
   first: `[Arrangement ▾]·8·[Grid ▾]·8·[TL]·4·[KN] … [▶ ■ readout] … [meter]`. The pair is a
   grid/tempo editing mode beside the grid control it shapes, apart from the playback group; the
   toggles get a "Grid & Zoom" menu row each beside Grid Snap, not instead of the button (a
   default-ON latched mode needs an on-screen indicator). The pair adds 76 px; at the 1280 px
   minimum the transport stays centred. Sighting falsifiers: at 1280 and 1200 px the pair groups
   with Grid, not with play, nothing overlaps, and the ON ring does not read as a box like the Grid
   combo's same-geometry outline.
   Tooltips, verbatim:
   - "Tempo Lock: while on, tempo anchors can't be dragged or nudged. Adding, deleting and typing
     tempos still work."
   - "Keep Notes in Time: while on, tempo edits leave notes where they sound in the audio and move
     the grid under them."

5. **One transform, generalized (2026-10-04, against plan 65 as built).** Plan 65's
   `readdressUnderMeters` maps every slice by identity on the whole-note axis under a new placed
   meter list. Both tools here are the same pass with a different map, so the one function is
   generalized rather than joined by a second:
   - It takes the new map stated on the whole-note axis: the placed meters (`PlacedMeter`) and the
     placed anchors (a whole-note position plus milliseconds).
   - It also takes a content map φ from old whole-note position to new whole-note position.
   - It derives measure numbers for both meters and anchors, maps every content slice through φ
     (rounding to the tick lattice through `nearestTick` only where φ is not exact), re-expresses
     rings and keyframe offsets from their mapped ends, snaps sections, and re-seats the terminal.
   - Its three drivers:
     - *meter edit*: φ = identity, with the anchors placed where they stood;
     - *Scale Tempo* by f: φ(w) = f·w, with meters and anchors placed at f·w and milliseconds
       held;
     - *Keep Notes in Time*: anchors placed by the tempo verb's result, meters where they stood
       in measure terms, and φ(w) = the new map's whole-note position at the old map's time of w.
   - Every driver holds TIME, so the cursor is carried by its seconds. `SongTimelineSnapshot`'s
     `carryCursor` becomes "the same seconds under the new map", which drops the meter-specific
     re-address.
6. **Q2 resolved by plan 65's ruling** (65-Q3: meters keep their time and an indivisible reign
   refuses). Scale Tempo places every meter at f·w, so a reign of k measures becomes f·k measures.
   When that is not whole before a later meter, the factor is refused, naming the meter. A
   single-meter song is never refused; its end re-seats. The terminal re-seats through plan 65's
   `reseatTerminalAnchor`.

## Open questions (build-session)

- **Q1 — what a Keep-Notes-in-Time drag shows before release.** Content must not appear to move
  with the beat mid-drag when it will not after release. Settle at build time against the cost
  measured then.
- **Q2 — meters and the terminal under a non-integer factor.** /2 can land a meter change between
  downbeats and the terminal off one; the ruling says only "round to the tick lattice". Decide
  whether such a meter moves to the nearest downbeat or the factor is refused, and re-seat the
  terminal with plan 65's `reseatTerminalAnchor`.

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
  and its command. `Alt+←/→` on a tempo chip and the chip drag are refused while it is on, through
  the refusal flash; insert, delete, typed BPM and Scale Tempo are not.
- **Tests:** the lock is on after open, refuses both moves with a flash and no undo entry, and
  passes the other four verbs; it is absent from the saved package and the settings file.
- Independent of plan 65; it may land first.

### Phase 2 — Scale Tempo

- A pure function over the song through plan 65's transform: every grid address scaled by the
  factor on the whole-note axis, every time held, positions rounded to the tick lattice. The Edit
  menu item and the tempo row's right-click row open a factor prompt (x2, /2, a free fraction).
- **Tests:** x2 then /2 round-trips a lattice-aligned song exactly; times identical within half a
  tick; a meter at bar m lands at 2m-1 under x2; every result passes the rules; one undo entry.

### Phase 3 — Keep Notes in Time

- The toolbar toggle (OFF by default, session state). With it on, the move, typed BPM and delete
  verbs re-address content against the new map so its times hold; a drag commits on release.
- **Tests:** each tempo verb with the toggle on leaves every resolved time unchanged within half a
  tick and is one undo entry; with it off, plan 41's behaviour is bit-identical.
- **Sighting:** a non-click song: turn the toggle on, sync anchors to the audio by drag and nudge,
  turn it off; the notes never move in time and the grid lands under them. Scale a half-speed tab
  by x2 and confirm playback is unchanged. Try to move an anchor with the lock on and see it
  refused.

## Final acceptance

As separate invocations: a build, the touched tests, and `pre-commit run --all-files`; clang-tidy
when the user asks. Measure Scale Tempo and a Keep-Notes-in-Time drag release on the largest local
import on relwithdebinfo, and report the numbers.

## Rollback/abort notes

- Format-neutral; the toggles are never saved, so reverting removes them without trace.
- Every whole-song edit is one snapshot entry; `Ctrl+Z` takes it back whole.
