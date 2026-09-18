# Tone: active tone vs. selected region

Status: **proposed, awaiting sign-off. Not implemented.** Supersedes the "auto-select on load"
behavior committed for task #16 (`fb587bd1`) by refining "select" into "activate", so that commit is
a stepping stone, not a revert.

## Motivation

`m_selected_tone_region_id` currently conflates two concepts: the tone the rig plays / the editing
context, *and* a formal user selection. Now that the cursor auto-picks a tone on load and seek
(task #16), a tone becomes "selected" merely because the cursor is over it — so the Delete keybind
(which deletes the selected region) deletes tones accidentally. Splitting the two concepts fixes this
at the root and keeps the intuitive Delete key.

## Model

- **Active tone** — the tone the rig plays and the default editing context (signal-chain panel and
  tone-automation lanes bind to it). `active = the selected region if one is selected, else the tone
  where the keyboard stands (toneRegionIdAt(keyboardTimePosition()))` — the armed caret's instant
  while a caret is armed, else the cursor's. There is **always** an active tone: tone regions are
  gapless by design and the schedule spans the whole chart — the first region owns `[0, …)` (the
  pre-measure-1 lead-in) and the last region owns `[…, end of chart)` — so no position is ever
  tone-less.

  RULED 2026-09-18: while PAUSED the audible tone follows the caret as well as the cursor. The
  caret step never followed before, because the derivation read the raw transport clock and arming
  never moves the playhead, so stepping into another region left the rig behind.

  SETTLED 2026-09-18, same day: while PLAYING the audible tone no longer follows the keyboard at
  all — the Play handler bakes the tone track into branch-gain automation and the audio thread
  switches block-accurately against the transport, so the precision item that was open here is
  closed by construction. The two states are one law: a schedule exists exactly while the transport
  plays, so the direct write described above owns the rig exactly while it does not.
- **Selected region** — a deliberate, formal selection, set *only* by clicking a region. Cleared
  whenever the transport position changes (seek, playback advance, stop-to-start). Drives the
  white-outline highlight and the Delete target. While a region is selected it is also the active
  tone (a preview / audition).

Editing + audible target = **selected ?? cursor-tone**. Delete target = **selected only** (no-op when
nothing is selected, so it can never fire from a cursor-driven "selection").

## Interactions

- **Load / import / seek / stop / playback follow** → set the active tone from the cursor and
  **clear** the formal selection.
- **Click a region (paused)** → **select** it (formal) and make it the active tone (preview). Does
  **not** move the playhead — otherwise it would immediately clear its own selection.
- **Delete key** → delete the selected region if one exists; otherwise do nothing.

## Visuals

- **Active** region: the current highlight fill (unchanged).
- **Selected** region: the current fill **plus** a ~2px inset white outline. A selected region is
  also active, so it shows both. At most one region is active and at most one is selected, so there
  is never ambiguity.

## Open questions for sign-off

1. **Editing/audible target = "selected ?? cursor-tone"** — confirm this is the rule (the signal-chain
   panel and automation lanes follow the cursor tone when nothing is selected, and stick to the
   selected tone once you click one).
2. **When does the selection clear?** Proposed: on *any* transport position change (seek/playback),
   not only when the active tone actually changes. Simple and predictable. OK?
3. **During playback**, the active tone (and the panel binding) follows the playhead across region
   boundaries — panel/automation retarget as tones change. Editing during playback is unusual, so
   this is acceptable; confirm.
4. **Clicking a region does not seek the playhead** (it selects + previews in place). Confirm.

All confirmed 2026-07-08 (1–4 as proposed). There is no "no active tone" case: the tone schedule is
gapless and spans the whole chart, so `toneRegionIdAt` must resolve the first region for any position
before its grid start (already done) **and** the last region for any position at or after its grid
end (symmetric addition), guaranteeing every cursor position maps to exactly one region.

## The click-vs-follow intent split (key mechanism)

Today both a click and the render-cadence playback follow (`ToneTrackView::advanceActiveRegion`,
*renamed 2026-09-14 to `reportPlaybackFrame` when the crossing decision moved into the controller*)
emit the single `onToneRegionSelected` intent, so playback formally *selects* tones. The split needs
two intents:

- `onToneRegionSelected(id)` — deliberate **click** → formal selection (white outline, Delete target).
- `onToneRegionActivated(id)` — the view's **playback/cursor follow** (and the controller's seek/load
  handlers) → set the active tone and **clear** the formal selection. *Shipped payload-less, and
  since 2026-09-14 narrowed further to `onPlaybackFrameAdvanced()`: the row reports only the render
  cadence and the controller owns the crossing decision. Since 2026-09-18 that frame is DISPLAY
  ONLY — the baked schedule moves the audio, so the frame moves only the drawn active flag.*

Selection therefore exists only between a click and the next transport move: play-start, seek, and
boundary crossings all route through the "activate" path, which clears it. That is exactly "selected
while paused" and keeps Delete safe.

## Implementation touch-points (once signed off)

- **Controller:** keep `m_selected_tone_region_id` as the *formal* selection (set only from the tone
  row's click). Introduce an "active tone" resolution (`selected ?? toneRegionIdAt(cursor)`) and drive
  `syncAudibleTone` and the signal-chain / automation scoping from it (rename
  `selectedToneDocumentRef`/`selectedToneName` to `activeTone*` to match). Cursor-change handlers
  (`SeekTimeline`, open/import completions, `Stop`, `PlayPause`, the playback follow) set active +
  clear selection instead of selecting the region at the cursor (then `applyToneSelection`, since
  2026-09-14 `selectMarker`). `onToneRegionSelected`
  (click) sets the formal selection + active.
- **View state:** `ToneRegionViewState` gains an `active` flag (current color) distinct from
  `selected` (white outline).
- **Tone track view:** paint active fill, then the selected outline.
- **Delete:** target unchanged (`m_selected_tone_region_id`), now safe; the Delete-key handler stays.
- **Tests:** load auto-select tests assert active (not selected); plugin-label tests are unaffected
  because the active tone on load is still "Default" (cursor over the baseline), so labels keep their
  "on Default" suffix. Add coverage for: cursor-move clears selection; click selects; Delete no-ops
  with no selection and deletes with one.

## Note

The active tone driving plugin-edit labels means those labels already read correctly ("... on
Default") after this change — no label churn beyond task #16's.
