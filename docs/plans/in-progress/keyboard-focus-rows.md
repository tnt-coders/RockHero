# Keyboard focus rows — the caret arms on point rows, markers are walked by selection

*Status: **Phases 1a, 1b and 2 BUILT 2026-09-13** (commits `7bfb2d33`, `c46f0874`, `0bb42bfc`;
records under each) and SIGHTED. The one open leaning, arrows on a marker row, is RULED 2026-09-14:
they leave the row. Phase 3, the marker grammar: its chord precedence RULED 2026-09-14 (author at
the cursor, never the selection) with `Ctrl+R` as the rename, not built — except the selection-verb
move (`Alt+←/→` on a selected tone region, the paused cursor following to the new start), built
2026-09-14 in `5d33a1fc` and recorded in `marker-verb-grammar.md`. **Phase 4 — the `Ctrl+Shift`
selection chords and the hand rows — PLANNED 2026-09-14** (decisions listed under it before
building). Supersedes the armed-caret row model of `d320e7ac` (kept on `master` for reference
only).*

## Context

`d320e7ac` + `1573acf8` (still on `master`, reverted off `work-in-progress` at `804879d6`) gave the
ARMED caret a section row and a tone row (`CaretRow` sum type, `armToneRowCaret`,
`armSectionRowCaret`, `rearmCaretAt`, caret squares on the tone strip and ruler; +812/−155). A caret
on those rows both stayed armed AND selected the marker under it, which contradicts the grammar's own
rule that selecting a marker disarms the caret (`marker-verb-grammar.md:22`), and it drew a typing
caret on rows where nothing can be typed.

The user's proposal: the caret is armed only where the next keystroke authors a point object — the
strings and the automation lanes. Marker rows (section, tempo, meter above the strings; tone below)
are reached by SELECTION, with the caret disarmed and the cursor left where it is.

## Verdict: coherent, and a genuine upgrade

Three independent critiques (interaction, architecture, edge cases) all concluded the same.

- **It keeps a true invariant instead of breaking it.** At HEAD an armed caret and a selected
  marker already cannot coexist (arming replaces the selection, `chart_handlers.cpp:528-577`;
  selecting dissolves the caret, `section_handlers.cpp:76-94`, `tone_handlers.cpp:252-273`).
  `d320e7ac` broke that; this proposal does not.
- **Nothing is stored to say which row you are on.** On a point row the caret names it; on a marker
  row the selection's kind names it. No `CaretRow`, no marker squares.
- **The on-screen order matches the key order.** The ruler draws section (y2), tempo (y16), meter
  (y30) above the strings (`timeline_ruler.cpp:30-39`); the tone strip sits between strings and
  lanes (`track_viewport.cpp:643-650`), which today's walk skips.
- **It retires grammar rule 2** (a chord SELECTS a marker exactly at the cursor). The walk becomes
  the keyboard's route onto a marker, so Phase 3's chords can author or overwrite and never merely
  select — the precedence template loses an arm. (RULED 2026-09-14 as part of Phase 3 item 2.)

It is not ready to phase exactly as written, for the reasons folded into the model below:
horizontal keys, the walk's landings, two latent defects the walk makes routine, and grammar rule 4.

## The model — one law

**Keyboard focus has three states:** passive (no caret, no row selection), an armed caret (point
rows: strings, lanes), or a selected row (marker rows, and the "+" row). Vertical keys walk the
row stack and keep the column. On a point row the arrows move along the row; on a marker row `Tab`
moves along the row and the arrows return to charting.

**The row stack, top to bottom, in seven groups:** section · tempo · meter · strings (top =
`stringCount()` … 1) · tone · lanes (visible, of the active tone) · the **"+" row** (the trailing
empty lane whose pinned chip opens the parameter picker, `tone_automation_lanes_view.cpp:1152-1171`,
`showParameterPicker` `:1605`). Every group is one row except strings and lanes.

**Plain `↑/↓` step one row; `Ctrl+↑/↓` jump to the adjacent non-empty GROUP** (user, 2026-09-13 —
`Ctrl` = reach, the matrix's planned "adjacent surface" row realised). Both land on the destination
group's NEAREST edge, so within a one-row group they coincide:

| From | `↑` | `↓` | `Ctrl+↑` | `Ctrl+↓` |
|---|---|---|---|---|
| section chip | inert | tempo holder | inert | tempo holder |
| tempo chip | section holder (default 1) | meter holder | section holder | meter holder |
| meter chip | tempo holder | arm the TOP string | tempo holder | arm the TOP string |
| top string | meter holder (disarm) | string below | meter holder | tone holder |
| any middle string | string above | string below | meter holder | tone holder |
| string 1 | string above | tone holder (disarm) | meter holder | tone holder |
| tone region | arm string 1 | arm the first lane; the "+" row with no lanes | arm string 1 | arm the first lane; "+" with no lanes |
| first lane | tone holder (disarm) | next lane; "+" if it is the only lane | tone holder | "+" row |
| any lane | lane above | lane below | tone holder | "+" row |
| last lane | lane above | "+" row | tone holder | "+" row |
| "+" row | arm the last lane; tone holder with no lanes | inert | arm the last lane; tone holder with no lanes | inert |
| passive / chart multi-select / time selection / point with caret dissolved | arm in place on the remembered row (today's passive law) | same | same | same |

**The "+" row** is reached by selection like a marker row (the caret disarms), but it names no
document object: it is "add a lane to the active tone". On it:
- `Enter` opens the parameter picker, anchored to the chip rather than the mouse position, and
  choosing a parameter opens that lane and ARMS the caret on it at the cursor, so the keyboard
  continues on what was just made. (`Insert` is not paired yet. The picker's top level is one
  submenu per plugin, which carries no item id to pre-select, so the first arrow press highlights.)
- With nothing to offer (empty tone, tone not loaded) the picker states why, as the pointer form does.
- `←/→` leave the row (the passive law); `Tab` is inert (the row holds no objects); `Delete` and
  `Alt+arrows` are inert.
- The chip wears the selection ring while focused. Keeping it drawn while it is scrolled past the
  tone's end, and revealing it, are not built (see the Phase 1a build record).

A "holder" is the marker of that row whose span holds the column. Tempo, meter and tone always have
one (first anchor at 1:1, first meter at measure 1, tone tiles the song; the lead-in before 1:1
belongs to the first of each). Sections may have none.

**The column.** Moving vertically FROM a marker row uses the cursor if the selected marker's span
holds it, otherwise the marker's start (seeking there first). This keeps mouse selection unchanged
(a click still selects without seeking) while guaranteeing the keyboard never lands in a region that
does not own the lanes it just computed — the edge case where a mouse-selected tone region plus Down
armed an invisible lane caret.

**Re-arming from a marker row keeps the slot.** The passive cursor remembers the exact position the
editor last put it at (a dissolving caret's slot, or the start the column rule moved it to), and
re-arming takes that position back while the transport still stands within a millisecond of it;
anywhere else the nearest placement-quantum slot. Today's direct strings→lanes step keeps the exact
position, so without this the new tone row would regress a walk over an off-grid slot. (Built so
after the review: the first form, "an object standing on the cursor's tick", lost the slot whenever
the landing row had no object there. An exact seconds match would not do either, because Tracktion
rewrites a paused position from its sample-based playhead 200 ms after a seek.)

**Horizontal keys** (Tab RULED 2026-09-13: next object on every row; arrows on a marker row RULED
2026-09-14 after the Phase 1 sighting: leave the row):

| Key | String row | Lane row | Marker row |
|---|---|---|---|
| `←/→` | next stop: grid ∪ notes/keyframes (unchanged) | next stop: grid ∪ points (unchanged) | **leave the row**: arm at the cursor on the remembered point row, no step |
| `Ctrl+←/→`, `Home`/`End`, `PageUp`/`PageDown` | unchanged | unchanged | leave the row and jump (today's passive behaviour) |
| `Tab`/`Shift+Tab` (Phase 2) | next/previous note, grid ignored | next/previous point | next/previous marker: select it, seek the cursor to its start |

Why this is the simpler model, not a compromise:
- **It is a law that already exists.** A marker row is a passive-caret state, and every arrow press
  from passive already arms in place without stepping (`chart_handlers.cpp:1227-1236`). Marker rows
  need NO horizontal branch in `StepChartCaret` / `JumpChartCaret`.
- **No redundancy.** `Tab` owns marker stepping; the arrows gain a distinct use on marker rows —
  resume charting where you were — instead of repeating `Tab`.
- **Cost, named:** an instinctive `→` on a chip leaves the row. `Tab` is the key to learn, and the
  keymap matrix states it.

A `Tab` marker step reads the CURSOR, as `Tab` does on every row (re-ruled 2026-09-14, Phase 3 item
5): after the column rule brings the cursor into the selected marker, `Tab` goes to the next marker
start strictly after the cursor and `Shift+Tab` to the previous one strictly before it. So after a
mouse selection or an `Alt+←/→` move it still steps from what is outlined, and from a cursor inside
a marker past its start, `Shift+Tab` lands on that marker's own start first. A step past either end
is inert. `Alt+←/→` still moves the selected marker.

**The remembered row** (recommended refinement): `ChartCursor` remembers the point row the next arming
lands on — a string OR a lane, mirroring `ChartCaret` — instead of only the string, so leaving a tone
region reached from a lane returns to that lane. A lane that has left view falls back to its string
(the existing stale-lane fallback). Side effect to accept: `Esc` on a lane caret followed by an arrow
returns to the lane (today it lands on a string).

## Recommended defaults (overridable)

1. **Cursor before the first section:** Up from tempo selects the first section after the cursor
   without moving it (no dead key). The first marker of every row owns whatever precedes it, so
   the column rule leaves the cursor in the lead-in on the next vertical move too. With no sections
   at all, inert.
2. **Delete on a marker row** keeps the signed rule: nothing stays selected, focus drops to passive.
3. **Esc** on a marker row releases the selection (existing ladder).
4. **Tempo walks anchors as drawn** — every non-terminal `BeatAnchor`, not deduplicated BPM values;
   compare stops through `secondsAtGridPosition`, never `BeatAnchor::seconds`.
5. **Tempo/meter selections are inert to Delete, Enter and `Alt+←/→`** until plan 41 gives them
   verbs; they are left out of `selection_present` with a comment, so Delete neither beeps nor lies.
6. **Mouse:** tempo and meter chips become click-selectable exactly like section chips (select, no
   seek). Clicks keep seeking nothing.
7. **Playback ends marker focus** — RULED 2026-09-13: keep the signed "play clears the selection"
   rule; after pausing, walk back onto the row. Revisit only if it bites in sighting. **RULED
   2026-09-14: while the transport plays, marker selection and every marker edit are
   unavailable** — one rule for every marker kind (sections, tempo anchors, time signatures, tone
   regions, the "+" row) and for automation points, enforced by `isActionAvailable` in
   `editor_action_availability.cpp`, and published to the views as one flag,
   `EditorViewState::marker_edits_enabled`. The tone designer is excluded: the plugin chain, plugin
   parameters and the output gain stay live mid-play (that is the point of the live rig). The
   tone rename is IN: it names a catalog document rather than a marker, but the tone row is its
   only surface, so it closes with the row rather than standing as the one live item on it.
   Weighed and set aside the same day: letting a tone region be selected during playback with
   its tone LOCKED as the audible one until deselected, so the tone under edit does not change
   out from under the charter. The lock is a real workflow, but it gives one marker kind playback
   semantics the others lack. It returns later as an explicit **tone audition** state — not a
   selection — that overrides the derived audible tone while set and that the playback follow
   ignores; the frame handler already derives the audible tone from its inputs, so an audition
   is one more input, not a second rule. Unplanned; record it as a plan when it is wanted.
8. **Chartless arrangements:** the walk rides `StepChartCaret` (gated `has_chart`), so it is
   chart-only for now; recorded as a limitation.
9. **The 3D preview** keeps its whitelist (plain and `Ctrl` Up/Down stay main-window); Phase 2 adds
   Tab/Shift+Tab.
10. **Returning to a group lands on its nearest edge**, exactly as the plain arrows do (`Ctrl+↓` from
    the meter row → top string, `Ctrl+↑` from the "+" row → last lane). "Resume where I was" is the
    arrows' job (`←/→` on a selected row), so reach does not also consult the remembered row.

## Implementation shape (editor core unless noted)

**Selection storage — add kinds, map once** (built in 1b). Keep `ToneRegionSelection{region_id}`
(position identity breaks under boundary moves and undo of a first-region delete). Add
`TempoAnchorSelection{GridPosition}` and `TimeSignatureSelection{int measure}` beside
`SongSectionSelection` in `core/src/controller/editor_selection.h`. One mapping pair, in
`core/src/timeline/marker_row_handlers.cpp`:
- `selectedMarker() -> optional<{MarkerRow row; optional<size_t> index}>`, and its inverse
  `markerSelectionAt(row, index)`, which builds the selection naming a row's marker;
- `selectMarker(MarkerSelection)` — dissolves the caret, sets the selection and re-syncs the rig.
  Every path that SELECTS a marker calls it directly, the chip clicks included; every path that
  DESELECTS one calls the generic `releaseSelectionIfHeld<Kind>()`, which frees the selection only
  while it still holds that kind. (`applySongSectionSelection` and `applyToneSelection`, which first
  carried both halves behind an empty-sentinel argument, were deleted 2026-09-14.)

**Marker-row helpers** — `Impl` members beside the pair, since each reads the session:
- `enum class MarkerRow { Section, Tempo, TimeSignature, Tone };`
- `markerStarts(row) -> std::vector<GridPosition>`, ascending;
- `markerHolderIndex(starts, position)` — one `upper_bound` in musical space: the last start at or
  before the position, else the first marker, which owns the lead-in. The position is the paused
  cursor's (`pausedCursorPosition(g_tick_quantum_note_value)`: the trusted column, else the nearest
  line of the quantum asked for), so a marker the cursor was moved onto still holds it after
  Tracktion's write-back. `toneRegionIdAt` keeps its seconds-space loop, because the drawn tone row
  and the automation window share that span rule.

**The "+" row selection** (built in 1a). One more alternative, `AddAutomationLaneRowSelection{}`,
beside the marker kinds: mutually exclusive with every other selection by construction, disarms the
caret when selected, inert to Delete/Alt-move, cursor-coupled (a seek releases it), published as
`ToneAutomationViewState::add_lane_row_selected`. `Enter`'s view dispatch routes it to the lanes
view's `openParameterPicker`, the chip-anchored keyboard entry; opening a lane is synchronous, so
`onToneAutomationLaneAddRequested` arms the lane caret on the new lane itself while the "+" row is
selected.

**One row walk.** Replace `stepCaretRow` (`chart_handlers.cpp:1164-1209`) with
`stepFocusRow(bool up, bool reach)`: the current row from the armed caret or the selection, ONE
ordered stack whose rows carry their group, and one landing (arm / select holder / select "+" /
inert). Plain steps one row; `reach` steps to the nearest row of the adjacent non-empty group — a
parameter on the same walk, never a second ladder. `StepChartCaret` already carries a reach flag
spelled `measure` (horizontal reach IS a measure); rename it `reach` so it says what it means on both
axes. Keep the stale-lane fallback. The column rule runs before the stack is listed, so the lanes
it lists are the ones the selected region owns.
`StepChartCaret`'s passive branch (`:1227-1236`) consults `selectedMarker()` for VERTICAL presses
only; horizontal presses from a marker row keep the passive law untouched (they arm on the
remembered row). `ChartCursor` (`editor_controller_impl.h:1180-1186`) widens from `string` to the
remembered point row.

**Two latent defects the walk makes routine — fixed in Phase 1:**
- *Dissolve seeks without re-syncing the rig* (fixed in 1a). The lanes already follow the cursor,
  so the fix is one helper rather than a split: `moveCursorTo(position)` seeks and calls
  `syncAudibleTone`, and `dissolveChartCaretInPlace` and the column rule move the cursor through it.
  `activateToneAtCursor` (clear plus sync) is unchanged for the transport moves. This retires
  dissolve's "display handoff, not a listening move" rationale.
- *Undo never releases a stale section selection* (fixed in 1b). `releaseToneSelectionNamingNothing`
  became `releaseMarkerSelectionNamingNothing` over `selectedMarker()`, at both call sites (every
  tone commit and every undo transition).

**View state** (`editor_view_state.h`): publish the selected tempo anchor and time-signature measure
(the ruler reads the raw `TempoMap` and has no per-anchor identity), and `selected_row_cursor`, the
paused cursor with its measure span while focus stands on a row reached by selection.

**Ruler UI** (`ui/src/timeline/timeline_ruler.{h,cpp}`, built in 1b): the three chip rows share one
`RulerChip` (placed label, source index, selected flag) and one placement template, `placeChipRow`,
which replaced three hand-written row builders; hit-testing is one `chipAt` per row. The placement
claims the selected chip's room before the greedy pass (fixing the same latent drop for sections),
and a selected pinned chip never yields. Chips share one frame painter with the existing 1px accent
outline. **Reveal:** the view glides the cursor's measure into view when `selected_row_cursor` moves
under a standing focus — never when the focus merely appears, so a click never scrolls away from
what was clicked. Vertical ensure-visible is not built.

## Phases

Phase 1 splits in two so each half is sighted before the next is stacked on it.

### Phase 1a — below the strings, and the reach keys
The walk function over the rows below the strings (a step past the top string is inert until 1b):
string 1 ↔ tone ↔ lanes ↔ "+" row, plain and `Ctrl+↑/↓` (new always-active registry rows
`CaretJumpSurfaceAbove`/`CaretJumpSurfaceBelow`, ids `0x150B`/`0x150C`, default
`Ctrl+Up`/`Ctrl+Down`, reaching the tone row and "+" from any string or lane). The "+" row selection and its keyboard
picker. The column rule, slot-keeping re-arm, the remembered row, and the dissolve re-sync fix.
Rule 4 stands as the grammar first stated it — an insert leaves its product SELECTED (re-ruled back
2026-09-13, see Phase 3 item 1) — and rule 2's deliberate select-at-cursor stays until Phase 3
removes it. Horizontal keys on the tone and "+" rows need no code: they are the passive law.
Sighting brief: walk strings → tone → lanes → "+" and back, plain and `Ctrl`, on a chart with an
off-grid note, a tone with no lanes, a tone with several, and a mouse-selected tone region far from
the cursor; open a lane from "+" by keyboard; press `←/→` on the tone and "+" rows (reached from a
string AND from a lane) to judge the "leave the row" leaning.

**Build record (2026-09-13, committed `7bfb2d33`).** Built as specified, with these
calls made during the build:
- **The stack is the rows that exist.** `stepFocusRow` lists only the 1a rows, so the top string is
  the stack's top and a step past it is inert (it used to re-arm in place). Phase 1b prepends the
  ruler rows; no placeholder rows were written ahead of it.
- **The commands are named for what they reach:** `CaretJumpSurfaceAbove` / `CaretJumpSurfaceBelow`
  ("Jump to Surface Above/Below"), ids `0x150B`/`0x150C`, riding `StepChartCaret` with its bool
  renamed `measure` → `reach`. Phase 2's `Tab` pair takes `0x150D`/`0x150E`.
- **Every insert selects its product, from any input** (the section insert, the tone split onto an
  existing tone — which used to select nothing — and the split that mints a tone). The verb selects
  after the commit, not the rig-activation step: the select id `activateEmptyToneBranch` and
  `reloadLiveRigForToneSet` carried is deleted, and both now end with `syncAudibleTone`, the other
  job that id was quietly doing — which also re-syncs a retone that mints a tone, which never
  re-synced before. (For one build the rule ran the other way, an insert selecting nothing; the
  user re-ruled it back before the sighting.)
- **A dissolving caret takes the rig with it.** `dissolveChartCaretInPlace` moves the cursor through
  `moveCursorTo` (seek plus `syncAudibleTone`), so Esc, Ctrl+click, a marquee or a step onto a marker
  row no longer leave the audible tone and signal-chain panel behind the lanes when the caret had
  walked into another tone's region.
- **`clearCursorCoupledSelection` names the survivors** (nothing, a chart selection, the time span),
  so the "+" row selection and every later kind follow the cursor by default.
- **The kind-dispatch ladders were not converted to `std::visit`.** The one new kind, the "+" row,
  is correctly inert at every ladder's fall-through (Delete, Alt+arrows, Esc's last rung). Phase 1b
  adds two kinds with real per-kind answers, which is where the conversion pays; decide it there.
- **`Enter` only.** `Insert` is not paired with the "+" row; the user asked for `Enter`.
- **The adversarial review round** (22 of 27 verified findings confirmed; the rest refuted or left
  to sighting) changed four things beyond comments and tests. A passive landing that keeps the row
  (an arrow or a jump from a selected tone or "+" row) first releases the cursor-coupled selection
  through `activateToneAtCursor`, so a remembered lane is judged against the tone under the cursor
  rather than a region selected elsewhere with the pointer — which had armed an invisible lane
  caret — and the rig follows the cursor's tone (`prepareLandingRow`, the one row rule every
  keep-the-row landing shares, horizontal steps included, so a caret stepping on a lane that is no
  longer visible now falls back onto its string exactly as a jump always did). The slot memory
  above replaced the tick rule. A marker-row landing reuses `dissolveChartCaretInPlace` rather than
  restating it. The walk's `do`/`while` became a `for` (CI's `cppcoreguidelines-avoid-do-while`).
  Behaviour change to sight: `Esc` then an arrow now re-arms at the caret's exact slot, where it
  used to snap to the nearest grid line.
- **Not built, still owed:** the vertical reveal (a walk onto a lane or the "+" row below the fold
  does not scroll it into view) and keeping the "+" chip drawn while it is focused but scrolled out
  of its column. Both are pre-existing gaps for lane carets; the sighting decides whether they block.

### Phase 1b — above the strings
Section, tempo and meter rows join the stack (plain and `Ctrl`): tempo/meter selection (keyboard and
click), the undo stale-selection fix, ruler reservation/pinning, the stronger selected-chip style,
and reveal. `Ctrl+↑` from any string now reaches the meter row.
Sighting brief: a dense GP tempo map (identical chips), a song whose first section starts late, a
song with no sections, a mouse-selected chip far from the cursor, `←/→` from each ruler row.

**Build record (2026-09-13, committed `c46f0874`).** Built as specified, with these calls made
during the build:
- **One select for every marker.** `selectMarker` takes the selection kind and does the three
  things every marker select owes — demote the caret in place, set the selection, re-sync the rig.
  The rig sync is new for a section select: a section chip clicked while a region selected away
  from the cursor was the active tone left the rig on that region's tone while the lanes followed
  the cursor. The keyboard landing builds the kind from the holder's index (`markerSelectionAt`);
  a chip click names its marker by start (`selectMarkerStartingAt`, which selects nothing for a
  start no marker has).
- **The column rule is one call for every marker row.** `moveCursorIntoSelectedMarker` replaced the
  tone-only version and runs before every vertical step; it is a no-op without a marker selection.
- **The kind-dispatch ladders stayed `if` chains.** The two new kinds have no Delete, move or
  restate, so every ladder's fall-through is already their correct answer, and Esc's last rung is
  kind-agnostic. The conversion to `std::visit` belongs with plan 41, when they gain verbs.
- **`selection_present` leaves the tempo and signature chips out**, so Delete is consumed silently
  on them; Enter's restate falls through to inert the same way.
- **The ruler's selected style is unchanged** (the section chip's 1px accent outline, now on all
  three rows). A stronger style is the sighting's call.
- **The reveal covers the "+" row too.** `selected_row_cursor` is published for every row reached by
  selection, so stepping from a region selected far from the cursor onto its "+" row also glides.

### Phase 2 — Tab / Shift+Tab, next object on every row
New always-active command pair (next free ids after Phase 1a's, `0x150D`/`0x150E`, Navigation)
bound to Tab/Shift+Tab, added to the 3D preview whitelist. RULED scope: next/previous OBJECT on the focused row — marker
starts on marker rows, notes on the caret's string, points on a lane — built on the object half of
`nextRowObjectStop` (`chart_handlers.cpp:641-688`) with the grid ignored; from passive it arms like
the arrows. Sub-questions for the Phase 2 brief: whether a string's objects include keyframes (the
union stop set counts them; "between notes" suggests heads only) and the held-stop satellite (skip,
recommended). Always-active is required: a disabled
matching command beeps and JUCE then moves focus off `EditorView` (`juce_ComponentPeer.cpp:197-233`),
which is also today's latent hazard (Tab moves focus to a ComboBox, then arrows drive it). Verify
against JUCE that Tab typed in the grid value `TextEditor` is not stolen by the command; gate if so.
Record Shift+Tab as a named exception to "Shift = extend" in the matrix.

**Rulings for the build (2026-09-13).** A string's objects are its notes AND their keyframes, and
`Ctrl+Tab`/`Ctrl+Shift+Tab` step the notes alone, jumping over keyframes; on a lane or a marker row
the Ctrl pair steps exactly as Tab does. The notes-only pair binds the PHYSICAL Ctrl key, because
Cmd+Tab is the macOS application switcher. The held-stop satellite is skipped.

**Build record (2026-09-13, committed `0bb42bfc`).** Built as specified, with these calls made
during the build:
- **One action, `StepToRowObject{later, notes_only}`**, rather than a third flag on
  `StepChartCaret`: the arrows and Tab share no horizontal branch (the arrows' grid, satellite and
  measure jump are all theirs; Tab's marker-row step is its own), so a shared action would have
  been two handlers behind one switch. They share the landing (`landOnRow`, `prepareLandingRow`)
  and the stop search (`nextRowObjectStop`, which gained `notes_only`).
- **Commands:** `CaretStepNextObject`/`CaretStepPreviousObject` (`0x150D`/`0x150E`,
  `Tab`/`Shift+Tab`) and `CaretStepNextNote`/`CaretStepPreviousNote` (`0x150F`/`0x1510`,
  `Ctrl+Tab`/`Ctrl+Shift+Tab`), always active, in the 3D preview's whitelist, and listed in the
  Navigate menu — which also gained the two `Ctrl+↑/↓` reach commands Phase 1a had left out.
- **A marker step moves the cursor, then selects.** The cursor moves to the neighbour's start and
  the neighbour is selected there, so the rig ends on the tone the new selection makes active; the
  view's `selected_row_cursor` glide brings the cursor into sight.
- **Tab in a text field was stolen, and is now gated.** JUCE's `TextEditor::keyPressed` declines a
  Tab it does not type, the key bubbles to the window's mapping set, and the command would have
  stepped the chart behind the grid value. The view instead performs JUCE's own unclaimed-Tab
  traversal whenever a text editor holds focus. The prompts and the automation value field live in
  their own desktop windows, so the grid value is the one field this reaches today.

**Sighting (2026-09-13, partial).** Phases 1 and 2 look right, and the user judges the selection-row
direction cleaner than the armed-caret rows it replaced. More sighting may follow before Phase 4.
The `Alt+←/→` tone-region move (`5d33a1fc`) was sighted 2026-09-14 and looks right: the refusals
read as refusals, and the paused cursor arriving at the new start keeps the edit in view.

### Phase 3 — grammar (separate discussion before building)
Questions to settle, with current leanings:
1. **Rule 4 and the armed caret — RULED 2026-09-13, then RE-RULED the same day:** an insert
   leaves its product SELECTED, as a typed note does, and restating a selected marker keeps it
   selected; the caret the chord was typed from demotes in place. The first ruling (keep the caret
   armed, select nothing) guarded against `Right` stepping markers after an insert; once the
   arrows on a marker row were settled as "leave the row", that danger was gone, and consistency
   with the note won: the next `←/→` re-arms the caret exactly where it stood. The costs, accepted:
   a digit typed straight after a marker insert does nothing until an arrow re-arms, and `↑/↓` from
   the marker's row walk spatially rather than back to the string the chord was typed on. The
   restate at the cursor (item 2) follows the same rule, which now needs the core's restate verbs to
   select their target.
2. **Chord precedence — RULED 2026-09-14: author at the cursor.** Every `Ctrl`+letter marker chord
   (`Ctrl+M`, `Ctrl+T`, and the reserved `Ctrl+B`, `Ctrl+/`, `Ctrl+P`, `Ctrl+H`) uses the cursor even
   when no caret is armed and even when a marker of that kind is selected: a marker of that kind
   standing exactly there is restated, otherwise one is inserted there. `Enter` is the only key that
   restates the selection. The user's reason: the chord is an authoring verb, not a "replace the
   selected object" verb. The record is `marker-verb-grammar.md`; the build shape researched with the
   ruling:
   - **The position.** `markerGridPosition()` returns the armed caret, else
     `pausedCursorPosition(placementQuantum())`, and nothing while playing or with no song — two
     gates stated once in the authority, never in availability, which the pointer inserts share.
     Sections and meters snap to the downbeat of the measure the cursor is IN, from the tick
     (`pausedCursorPosition(g_tick_quantum_note_value)`), not from the slot; the tempo anchor floors
     to the beat it is in. The recommended fold of the two paused-cursor functions into one
     quantum-taking helper LANDED 2026-09-14 (`trustedCursorColumn`, `pausedCursorSlot` and
     `pausedCursorPosition` became one `pausedCursorPosition(quantum)`); what remains for Phase 3 is
     only making it ARMED-AWARE, which is what Phase 4c's `focusColumn()` then is.
   - **The template — superseded 2026-09-14 by the verb projection.** The design review found the
     chord precedence living in the UI over published lists, restating the core's "a marker stands
     exactly here" predicate in five places. The build shape is instead: the core publishes, per
     kind, the VERB the chord would perform at the cursor (a section chord target of rename-this /
     insert-here / nothing, and the tone twin of retone-this / split-here / nothing), so the UI
     only opens the prompt the verb names. `performMarkerChord`, `sectionAtMarker`,
     `toneRegionStartingAtMarker`, the containment scan in `createToneMarkerAt`, the UI's
     `project_loaded` guards and the two published positions are deleted; "nothing" is the
     playback and no-song gate, stated once in the projection. `Enter`'s three-arm ladder over
     published selection flags takes the same shape (a restate target), so `Ctrl+R` adds a verb
     without adding a UI ladder arm.
   - **Rule 4 moves into the core.** Before, a restate only ever reached a selected marker, so it held
     for free. `RenameSongSection` selects its section after the commit; `SetToneRegionTone` deletes
     `was_selected` and always selects the surviving region; `DeleteToneRegion`'s sole-region reset
     deselects AFTER its commit rather than before (check the ordering against the minted-tone reload).
   - **What the charter sees change.** A chip clicked far from the cursor is no longer what `Ctrl+M`
     renames — `Enter` or a double-click is. `Ctrl+T` on the tone row, with a region selected and the
     cursor inside it, splits at the cursor. The double press survives positionally: the first
     `Ctrl+M` inserts and selects, the second finds that section in the cursor's measure and renames
     it. A second chord of any kind works at once, with no re-arm. The chords also start working on
     chartless arrangements.
   - **Sequencing.** Land this before or with Phase 4a, so rule 2's prose is rewritten once.
   - **Owed before building:**
     - **Playback — RULED 2026-09-14: inert while playing.** It keeps the 2026-09-13 "editing
       during playback: no", and play already clears the selection. The same day's wider ruling
       makes the whole marker plane paused-only (recommended default 7), so the chord's own
       projection gate is a second line of defence rather than the only one: the core refuses every
       marker select and every marker edit while the transport plays.
     - **The playback gate is the projection, not a UI check.** The chord commands are
       always-active in `EditorView` on purpose (a disabled matching chord beeps), and
       `EditorViewState` publishes no playing flag, so "inert while playing" means the marker
       position publishes empty while the transport plays and the chord's perform finds nothing
       to act on.
     - **A rename to the same name, which records nothing, still selects — RULED 2026-09-14: yes.**
     - **A kind's projection publishes nothing where that kind cannot stand** (a section on the
       terminal downbeat), so the chord does nothing instead of prompting for an insert the core
       refuses — **RULED 2026-09-14: yes.**
     - **Tick and slot — RULED 2026-09-14: keep the split.** The jump's holder reads the tick (the
       exact cursor), the chord reads the slot (the placement-quantum line an arrow press would arm
       on); they answer "which marker holds the cursor" and "where would authoring land". They
       part only while snap is ON and a click, drag or stop left the cursor off the placement
       grid: with a quarter grid, a cursor at beat 2.9 and a region starting at beat 3, the jump
       selects the earlier region while `Ctrl+T` retones the one at beat 3. With snap OFF the
       placement quantum is the tick, so the two readings coincide, as they do wherever the
       editor itself parked the cursor. Record the edge in `keymap-matrix.md` with the build.
   - **Tests.** The two UI tests pinning rule 2 (`test_editor_view_state.cpp`, the section and tone
     chords selecting at the marker) are replaced: the chord raises no select intent even with another
     marker selected, and is fully inert with no published position while a marker is selected. In the
     core, `test_editor_controller_sections.cpp`'s two "no marker without a caret" checks flip, and new
     cases pin the paused slot (snap on and off), the measure-IN edge, empty while playing and with no
     song, the rename and retone selecting, and the sole reset leaving nothing selected.
   - **Docs swept with the build** (each still states a selected-marker restate or caret-only):
     `editing-interaction-model.md`, `docs/developer/keyboard-input.md`,
     `docs/developer/the-editor-2d-views.md`, the command-id and registry Doxygen, and the roadmap
     notes in `00-roadmap.md`, plans 40, 53 and 60, and BOTH span-marker documents —
     `docs/plans/roadmap/61-span-marker-redesign.md:16-18` and
     `docs/plans/todo/span-marker-redesign.md:259-261`, which each state the reserved `Ctrl+H` and
     `Ctrl+P` chords as inserting at the cursor and restating on selection.
3. **Tone has two payload verbs** — retone (repoint the region; today's `Enter`/`Ctrl+T` restate)
   and rename (the tone document's name, shared by every region using it; today's double-click).
   - **`Ctrl+R` = rename — RULED 2026-09-14:** rename the SELECTED object wherever a rename makes
     sense for its kind — a section's name (the same prompt as `Enter` and `Ctrl+M`), a tone
     region's TONE — and inert on kinds with no name (tempo, time signature, FHP, span, the "+" row),
     silently, as `Delete` is on the tempo chips. It is a selection verb like `Enter`, reading the
     selection and never the cursor, and not a marker chord: R is no marker's letter (it sits with
     `Ctrl+S` and `Ctrl+G` on the document plane). `Ctrl+R` is free today (plain R is tremolo). Do
     not reuse F2's retired id `0x1403`: saved keymaps key off the numeric id.
   - **`Enter` on a selected tone region drills into its signal chain — PROPOSED 2026-09-14, not
     ruled** (the user: "potentially"). With the rename on `Ctrl+R`, `Enter` can take the tone's
     primary "open", the Explorer shape (Enter opens, a separate key renames); on a section, whose
     only content is its name, opening is renaming. It restores the plugin-chain drill
     `keymap-matrix.md` proposed and the 2026-09-13 re-ruling set aside. Named costs: the region's
     retone moves wholly to `Ctrl+T` at its start, which after a `Ctrl+Shift+T` jump that leaves the
     cursor mid-region is one `Shift+Tab` away (item 5); and the tone strip's double-click (rename)
     then matches `Ctrl+R` rather than `Enter`. It needs the chain's keyboard model (slot focus, `Esc`
     back to the region), so until that is built `Enter` keeps retoning.
4. **Plan 41's wording.** `Ctrl+B` still reads "the armed caret when one exists, else the transport
   position" (retired by `804879d6`) and "with an anchor already selected is REFUSED"; `Ctrl+/` reads
   "with a meter selected RESTATES it". Both become the cursor precedence: an anchor on the cursor's
   beat is selected (a restate with no payload), and the meter on the cursor's downbeat is restated.
   Its re-addressing list must add the editor selections and `ChartCursor::column`, which is now a
   marker-verb input.
5. **Marker-row `Tab` reads the cursor — RULED 2026-09-14.** Phase 2 built the marker step from the
   selection's index (±1), which never puts the cursor on the selected marker's own start. Instead
   it takes the point rows' own rule (`nextRowObjectStop`: the next object strictly beyond the caret
   in the step direction), applied to the row's marker starts from the cursor:
   - `Tab` goes to the next marker start strictly after the cursor, so stepping on stays one press;
   - `Shift+Tab` goes to the previous start strictly before it — from a cursor inside a marker past
     its start, that is the marker's OWN start, and a second press reaches the one before. It is the
     media player's "previous" and `Ctrl+←` at a word's middle, and `Tab` never moves backward.

   Rejected 2026-09-14: the snap on `Tab` (the first press lands on the selected marker, the next
   steps on), which would move the cursor backward on the forward key and cost two presses for the
   common "next marker"; and a jump that moves the cursor to the marker's start, which would send the
   cursor to measure 1 on any song with one time signature or tempo, lose the place `←/→` return to,
   and make the jump land differently from the walk.

   The build: the marker branch of `StepToRowObject` runs the column rule
   (`moveCursorIntoSelectedMarker`), so a marker the pointer selected away from the cursor still
   steps from the selection, then finds the neighbouring start from the cursor's position rather than
   from the index, and selects the marker starting there. It rewrites that branch rather than adding
   one. Workflows it serves: `Ctrl+Shift+T`, `Shift+Tab`, `Ctrl+T` retones the region holding the
   cursor; `Ctrl+Shift+M`, `Tab` reaches the next section. Tests: from a mid-marker cursor `Shift+Tab`
   selects the same marker and puts the cursor on its start, then steps to the previous; `Tab` from the
   same place reaches the next marker; a pointer-selected marker away from the cursor steps from the
   selection, both ways.

### Phase 4 — the `Ctrl+Shift` selection chords and the hand rows (PLANNED 2026-09-14, not built)

Phase 4 gives every marker kind a direct keyboard route onto its row, and adds two select-only rows
above the strings for the fret-hand position (FHP) and the span. It is simpler than Phase 3 in one
important way: **it adds no grammar**. Every jump reuses the walk's existing landing (`landOnRow`),
and the hand rows reuse the marker-row model built in Phase 1b. The work is split into a handful of
prerequisites and three sub-phases, each built, committed and sighted on its own.

Discussed 2026-09-13 and 2026-09-14 (previously recorded as "Phase 2b"). Marks: RULED (the user
decided), RECOMMENDED (a decision still owed, with the recommended answer), OPEN (no recommendation
yet).

#### The law — RULED
On every letter plane, `Shift` picks the letter's second claimant and has no meaning of its own. Each
marker kind's two `Ctrl` slots are therefore one pair: `Ctrl`+letter AUTHORS that kind, and
`Ctrl+Shift`+letter SELECTS it, the same way `Ctrl+S`/`Ctrl+Shift+S` and `Ctrl+Z`/`Ctrl+Shift+Z` pair a
command with its variant. A jump lands exactly as the walk does: it selects the marker holding the
cursor and demotes the caret in place. With nothing holding the cursor (a song with no sections, a
span gap, a chart with no FHPs) it does nothing, silently. The string and lane rows need no key,
because `←/→` return to the row the caret was last armed on. Each kind's letter is declared once in the
registry, and both default chords are composed from it, so the pair cannot drift.

`Shift`+letter ("`Shift` = selection") was weighed at full strength and rejected 2026-09-13:
- In this map `Shift` means EXTEND from an anchor (`Shift`+arrows, `Shift+PageUp/Down`, the marquee,
  the planned `Shift`+click), and most selections carry no `Shift` at all. A jump REPLACES the
  selection with one marker and has no anchor, which is what plain `↑/↓` and `Ctrl+↑/↓` already do.
  Read as extend, `Shift+M` would promise "grow the range to the section", which `Shift+PageUp/Down`
  already is.
- It would move five live technique chords (`Shift+T` left-hand tap, `Shift+H` pinch harmonic,
  `Shift+L` split/join, `Shift+V` wide vibrato, `Shift+X` pick slide), withdraw the `Shift+A` heavy
  accent, evict Actions from `?`, and repeal the signed `Shift` plane. A habitual technique press would
  then silently replace a hand-built selection, which undo cannot restore.
- Guitar Pro, this audience's reference, uses `Shift`+letter as a technique's second slot.
- The sound half of the intuition — a time span is a selection — has its own later home: turning a
  selected marker into its time span (`Shift+Enter` is the candidate), for plans 47 and 52.

#### The chords
| Row | Author chord | Select (jump) chord | Command id | Built in |
|---|---|---|---|---|
| Section | `Ctrl+M` (live) | `Ctrl+Shift+M` | `0x1511` | 4a |
| Tempo | `Ctrl+B` (reserved, plan 41) | `Ctrl+Shift+B` | `0x1512` | 4a |
| Time signature | `Ctrl+/` (reserved, plan 41) | `Ctrl+Shift+/` | `0x1513` | 4a — see D1 for its macOS and layout limits |
| Tone | `Ctrl+T` (live) | `Ctrl+Shift+T` | `0x1514` | 4a |
| "+" row | — | `Ctrl+Shift+A` | `0x1515` | 4a |
| Fret-hand position | `Ctrl+P` (reserved, plan 60) | `Ctrl+Shift+P` | `0x1516` | 4b |
| Span | `Ctrl+H` (reserved, plan 61) | `Ctrl+Shift+H` | `0x1517` | 4c |

Rulings already made on this table:
- **"+" row — RULED 2026-09-14: `Ctrl+Shift+A`** (A for automation). It only LANDS on the "+" row, like
  every other jump; `Enter` then opens the parameter picker. Other tools use `Ctrl+Shift+A` for
  select-none, but nothing in this app does.
- **File chords — RULED 2026-09-14.** Publish is renamed Export on `Ctrl+E`, and Import moves from
  `Ctrl+Shift+O` to `Ctrl+I`, overruling plan 46's avoidance of `Ctrl+I` (italics muscle memory, which a
  charting editor has no use for). This frees `Ctrl+Shift+P` for the FHP jump.
- **Tone import and export — RULED 2026-09-14: `Ctrl+Shift+I` = Import Tone…, `Ctrl+Shift+E` =
  Export Tone…**, the letters' second claimants under the law above: the same verbs on the active
  tone. Both actions are live today as plan 50's signal-chain header buttons (`ImportToneFile`,
  `ExportToneFile`) with no command or chord, so this gives them keyboard reach and a row in the
  Actions dialog. It takes the slot once held for a song Export As, which is not planned; if one is
  ever needed it is menu-only.
- **Jumps stay in the main window.** Like the vertical walk, they are not in the 3D preview's command
  whitelist: they select rows the highway does not draw.

#### Decisions
D1–D4 were ruled 2026-09-14. D5–D13 are deliberately left to be revisited when the step that needs
each one is built; the recommendation recorded with each is a starting point from the 2026-09-14
research, not a ruling.

- **D1 — the time-signature key — RULED 2026-09-14: keep `/`** (`Ctrl+/` authors, `Ctrl+Shift+/`
  selects). The long-term correct answer is key bindings specific to each keyboard language, which is
  out of scope now. What that leaves broken, recorded so it is a known limit rather than a surprise:
  - JUCE matches a chord by exact modifiers and exact key code, folding case for letters only
    (`juce_KeyPress.cpp:52-63`).
  - macOS key codes keep Shift (`juce_NSViewComponentPeer_mac.mm:1366-1385`). On a US Mac `Cmd+/`
    works, but `Cmd+Shift+/` arrives as `?`, so the jump has no working default there.
  - Windows key codes are the key's unshifted character (`juce_Windowing_windows.cpp:3150, 3199-3205`).
    On German, French, Nordic and Swiss layouts no key produces `/`, so both chords need rebinding.

  How other apps solve it, for the day it matters: match the typed character while ignoring the Shift
  it needed (Qt, Eclipse, AppKit menus), match the physical key (VS Code, Sublime), ship a hand-made
  keymap per language (Dorico, Sibelius — the direction the user named), bind a numpad `/` twin
  (JetBrains), or avoid punctuation (Apple HIG, Eclipse). The researched letter alternative, `K`, is
  recorded in case the per-language keymaps never arrive.

  **macOS defaults — the user "sort of cares".** A per-platform default layer could give the macOS
  `/` chords working defaults: the Mac twin of `Ctrl+Shift+/` is a `?` key code with `Cmd+Shift`, and of
  `Shift+/` a `?` with `Shift` (D2). One caution to verify on a real Mac before shipping the first: macOS
  uses `Cmd+?` for Help-menu search, and JUCE offers key equivalents to the focused component first. See
  step 4.0d.
- **D2 — the Actions dialog's default — RULED 2026-09-14: keep `Shift+/`** (`?`, REAPER's key). As with
  D1, per-language key bindings are the long-term answer. Today its default is dead on macOS (it arrives
  as `?`) and on German Windows (it arrives as `7`); the macOS half is the easy case for a per-platform
  default (`?` with `Shift` carries no `Cmd`, so it cannot collide with Help search). The researched
  alternative, `F1`, is recorded for the day the Actions dialog needs a default that works everywhere.
- **D3 — which commands run while typing in a text field — RULED 2026-09-14: none.** While a text
  field in the main window is being edited, no command runs from the keyboard: the field keeps what it
  types, and Tab moves focus. The only fields that reach the main window are short inline edits (the
  grid value and output-gain boxes) that close on Enter or a click away, so almost nothing is lost —
  the menus still work by mouse, and commit the typed value first. The recommendation it replaced, a
  `fires_while_typing` flag letting 14 window-level commands run, needed a registry column, a scan of
  every command bound to the key and four borderline rulings, and it kept two defects that holding
  everything back removes (step 4.0a). Revisit when the main window gains its first text field that
  stays open, where running Save from the field is the platform convention.
- **D4 — the Export rename — RULED 2026-09-14: rename everywhere, identifiers included**, so one
  operation has one name. Today it carries four spellings (`PublishSong`, `PublishProject`,
  `PublishingProject`, `CouldNotPublishSong`), and `PublishProject` is also misleading: it writes the
  `.rock` SONG package, not the project. Still to settle when 4.0b is built:
  - the label — "Export Song..." (recommended, now that "Export Tone..." sits on `Ctrl+Shift+E` beside
    it in the Actions dialog; "Import..." becomes "Import Song..." for the same reason) or "Export...";
  - the method name — `Project::exportSong`, since `export` is a C++ keyword;
  - what command `0x1005` means — Export (recommended), which may later grow GIMP-style re-export; a
    song Export As, if ever needed, is menu-only, since `Ctrl+Shift+E` now exports the tone.
- **D5 — restoring a saved keymap must keep one owner per chord. RECOMMENDED: fix before any new
  default ships.** The keymap editor enforces one owner per chord (strip, then add), but restoring a
  saved keymap does not: `addKeyPress` removes no conflicts. Any user override on a chord that a NEW
  default now claims (`Ctrl+E`, `Ctrl+I`, the jump chords) would leave two owners, with dispatch picking
  one by mapping order. The fix is one keybinds helper used by assign, reset and restore; the user's
  override wins. It is general correctness, not a migration.
- **D6 — FHPs at duplicate positions. RECOMMENDED: run corpus-smoke first.** The validator accepts two
  FHPs at one position (`chart_rules.cpp:142`). A selection keyed by position then disagrees with
  itself: the lookup finds the first duplicate and the holder rule the last, so Tab gets stuck.
  - With zero duplicates in the corpus, tighten the validator to strictly ascending (the importer
    already guarantees it).
  - Otherwise also add a normalizer repair that keeps the LAST placement at each position, which is
    what every current reader already uses.
- **D7 — what releases a span selection. RECOMMENDED: "the front names nothing", not "any chart
  edit".** The chart revision bumps on every mutable access, including the settle inside the select
  itself, so a revision stamp would release a selection the moment it is made. Later verbs (the
  template apply, plan 61's front move) also need the selection to survive or follow an edit.
  "Names nothing" is the existing `releaseMarkerSelectionNamingNothing`, already asked after every undo.
- **D8 — span naming and zero-length spans. RECOMMENDED: call the kind `HandSpan`, and leave
  zero-length spans out of the row.** "Span" already means the time selection's span. A zero-length
  span (all members silently held) can never hold a cursor and can share its front with a successor,
  which would break identity by front.
- **D9 — where the core reads span fronts. RECOMMENDED: a lazy, self-refreshing `ChartResolutions`
  cache** keyed on the arrangement and chart revision, consulted only by span-row questions. It must
  check freshness itself: releases run between a revision bump and the next view push, so fronts read
  back out of the published view state would be stale. Optional follow-on: feed that one cache to both
  lane projections and the highway, which removes two whole-song derivation passes per revision (half
  the remedy of the watch item on repeated passes).
- **D10 — `Ctrl+H` is `Cmd+H` on macOS, which is Hide in the app menu JUCE installs. OPEN, but it
  only binds when plan 61 ships the span AUTHOR chord.** Phase 4 ships only `Ctrl+Shift+H`, which has no
  macOS conflict. Under the pair law, changing the span's letter later moves both chords.
- **D11 — pointer selection of FHP chips and spans. RECOMMENDED: keyboard only in Phase 4.** The pinned
  FHP chip is inert chrome by ruling, a press on a scrolling chip or a rail is a press on the top string,
  and neither mark is in the core's hit model. Plan 61's selectable authored-span start lines are the
  natural pointer face later.
- **D12 — does a selected pinned FHP chip yield to the chip scrolling in? RECOMMENDED: never, as on the
  ruler.** Put that exemption in one named helper beside `pinYieldsToIncomingLabel` in `sticky_label.h`,
  used by both the ruler and the tab lane, so it is not stated twice by hand.
- **D13 — adjacent defects the research found. RECOMMENDED: record them in `docs/tracking/backlog.md`,
  including the capture-dialog bug unless 4.0d is built.**
  - The `Shift` grid and zoom aliases (`Shift+=`, `Shift+-`, `Ctrl+Shift+=`, `Ctrl+Shift+-`) and the
    numpad `+`/`-` never match on macOS. The unshifted aliases still work.
  - Rebinding a command to a press that TYPES `/` (German `Shift+7`) works until restart, then restores
    as a bare `/`. The keymap capture dialog stores the press's text character, and JUCE's saved
    description collapses to that character. The fix is to zero the text character at capture.

#### 4.0 — Prerequisites (small, independent commits)
**4.0a — Keys typed into a text field stay in the field (the typing gate).** Deferred by the user to a
Fable session; it must land before any jump ships. Phase 3 item 2 makes it more urgent: with no caret
required, `Ctrl+M` and `Ctrl+T` typed in a field would author at the cursor in many more states.

*The bug:*
- A focused `juce::TextEditor` declines the keys it does not type, so the press bubbles to the window's
  mapping set (`juce_ComponentPeer.cpp:189-230`). That is wider than `Ctrl` chords: Tab and its
  modifier variants, bare Insert, `Alt+↑/↓`, `Alt+Shift+↑/↓`, `Ctrl+PageUp/PageDown`, the F-keys, and
  `Ctrl+Z`/`Ctrl+Y` once the field's own undo history is empty (`juce_TextEditor.cpp:265-283`) all
  reach it. The field keeps `Ctrl+A/C/V/X`, `Ctrl`+arrows, Home/End, Backspace/Delete, Return and Esc.
- `Ctrl+M` and `Ctrl+T` (live today) and every Phase 4 chord therefore fire from inside the grid value
  box and act on the chart behind it, and a repeated `Ctrl+Z` runs past the typing into chart history.
- Two text fields reach the window: the grid value box, and the output-gain slider's editable text box
  (`signal_chain_view.cpp:209-210`), which the Phase 2 build record missed. The prompts, the automation
  value editor, the plugin browser and the Actions window live in their own desktop windows and are
  unaffected, in both directions.

*The design (D3, ruled 2026-09-14; the JUCE behaviour verified against the source the same day):*
- **The rule.** While the main window's peer has a text input target
  (`ComponentPeer::findCurrentTextInputTarget`, which skips read-only and disabled editors), no key
  press reaches the command mapping set. There is no per-command datum and no registry change.
- **The mechanism: an override of `MainWindow::keyPressed`, not a listener.** It hands the press to
  the mapping set only when no field is being edited, and otherwise falls to
  `DocumentWindow::keyPressed`. `PreviewWindow::keyPressed` already has this shape. The mapping set's
  `addKeyListener`/`removeKeyListener` pair is deleted. Key listeners run before a component's own
  `keyPressed` (`juce_ComponentPeer.cpp:200-217`), so `ComposedCharacterFilter` still runs first, and
  "registered after so it runs before" stops being an ordering rule to maintain.
- **Declining does not hand the key back to the field**, which has already refused it. It reaches only
  JUCE's unused-Tab fallback, `moveKeyboardFocusToSibling(!shift)` (`:223-230`), so Tab and `Shift+Tab`
  move focus and commit the value.
- **What it fixes besides the leak.** Today window commands run while the Label edit is still modal and
  uncommitted: `Ctrl+S` saves without the typed value (File > Save by mouse commits it first, because
  the click counts as input while modal), and F3 may pull the main window back over the preview
  (`bringModalComponentsToFront`). Neither is reachable by key once nothing runs.
- **What it costs.** Open, Save, Close, Exit and the F-key panels do nothing from the keyboard for the
  few seconds a field is open; the menus still work by mouse. `?` and `Alt`+letter were typed into the
  field anyway (`juce_TextEditor.cpp:1921-1924`). Undo keeps its split: the field keeps `Ctrl+Z` while it
  has its own history, and once that is spent the press does nothing rather than undoing hidden chart
  edits. The plugin window's "Undo/Redo never yield" rule (`plugin_window.cpp:421-430`) is unaffected:
  its hook sees `Ctrl+Z` before the plugin's field does, so it can never run past typing.
- **Two constraints for the override's comment.** Declining must have no side effects: on macOS JUCE
  asks twice for a refused key while a text target exists
  (`juce_NSViewComponentPeer_mac.mm:1655-1668, 2436-2460`). And `keyStateChanged` no longer reaches the
  mapping set; nothing is lost today, since no command wants key up/down, but the first hold-style
  command needs a `MainWindow::keyStateChanged` forward under the same condition.
- **Revisit when the main window gains a text field that stays open** (plan 43's song-info fields, if
  they land there). Running Save from such a field is the platform convention (Win32 accelerators,
  Cocoa key equivalents, Qt, VS Code), and the additive answer is the flag researched 2026-09-14: a
  `fires_while_typing` bool on `EditorCommandSpec`, true for the File commands, Actions, the F-key panels
  and the menu openers, checked against EVERY command bound to the key, because `findCommandForKeyPress`
  returns the first owner while `keyPressed` invokes the first enabled one
  (`juce_KeyPressMappingSet.cpp:186-193, 322-357`). Record the trigger in `docs/tracking/watch-items.md`
  when 4.0a is built.
- **Rejected:** a check inside `perform` (the mapping set has already reported the key used, so JUCE's
  Tab fallback never runs); disabling commands while typing (beeps, and greys the menus the mouse still
  needs).

*What it deletes:* the Tab pair's text-field branch in `EditorView::stepToRowObject`
(`editor_view.cpp:1180-1191`), whose traversal JUCE's fallback performs, for a rebound Tab command too;
the mapping-set listener and its two ordering comments in `main_window.cpp`; and the matching ordering
sentence in `composed_character_filter.h`. The preview window keeps its own
`ComposedCharacterFilter` and ordering comment (`preview_window.cpp:47-55`), both of which stay: the
preview window still has no mapping-set listener to order against, and that comment already speaks
only of a listener "a later change attaches here", never of the main window's, so it needs no
rewording.

*Verification:* the condition needs a live peer with real focus, so it cannot run headless, and what is
left to decide is one condition. The routing tests call the mapping set directly and stay as they are.
- By hand on Windows, in both fields: Tab and `Shift+Tab` move focus and commit the value; `Ctrl+T`,
  `Ctrl+M`, Insert, `Alt+↑`, `Ctrl+G`, `Ctrl+S`, F8 and `Ctrl+Z` (once the field's own undo is spent) do
  nothing and make no sound; after the field closes, every key works again.
- On a real Mac: `Cmd+C/V/X/A` still edit the field and Tab traversal works. A declined `Cmd` key falls
  to JUCE's default app menu and then an AppKit beep, where Windows stays silent — confirm the beep is
  acceptable.

*Docs:* `keyboard-input.md` (the MainWindow bullet, which says the forwarder was removed, and the
Tab-in-a-text-field bullet become the typing rule; and `keyboard-input.md:174-180`, which states the
"registered after the mapping set, so it runs first" mechanic as a rule for BOTH windows and must
become a preview-window-only note) and `keymap-matrix.md`.

*Pre-existing, for `docs/tracking/backlog.md`:* the 3D preview's forwarder picks its command by first
owner (`editor_view.cpp:1138-1152`) while JUCE invokes the first enabled owner; 4.0c's one-owner restore
makes the two agree.

**Build record (2026-09-14).**
- Built as specified: the `MainWindow::keyPressed` override gated on
  `ComponentPeer::findCurrentTextInputTarget`, the mapping-set `addKeyListener`/`removeKeyListener`
  pair deleted, and the Tab pair's text-field branch deleted from `EditorView::stepToRowObject`.
  Every JUCE fact above was re-verified against the vendored source the same day: the
  listeners-before-`keyPressed` order and the unused-Tab fallback
  (`juce_ComponentPeer.cpp:200-221`, `:223-230`), `findCurrentTextInputTarget`'s
  `isTextInputActive` skip of read-only and disabled editors (`:291-301`,
  `juce_TextEditor.cpp:344-346`), `DocumentWindow`'s inherited no-op `keyPressed`
  (`juce_Component.cpp:3159`), and the macOS double ask for a refused key
  (`juce_NSViewComponentPeer_mac.mm:1655-1668`, `:2437-2460`).
- Verification is by hand, exactly as the list above is written: the condition needs a live peer
  with real focus, so no headless test can reach it. The routing tests call the mapping set
  directly and were left as they are; none pinned the deleted Tab branch, since a headless run has
  no focused text editor.
- The `fires_while_typing` revisit is recorded as a watch item in `docs/tracking/watch-items.md`,
  with the trigger this step names (the main window's first text field that stays open).
- One correction to the *Docs* line above: `keyboard-input.md:174-180` did NOT become a
  preview-window-only note. Both windows now dispatch commands from their own `keyPressed`, so the
  listeners-run-first fact is true for both — what it stopped being is an ORDERING RULE. It is
  written as that: listeners are offered a press before the component's own `keyPressed`, so the
  filter precedes command dispatch with nothing to maintain. Scoping it to the preview window would
  have restated a rule that no longer exists for either.

**4.0b — Export and Import.** The chord move (`Ctrl+E`, `Ctrl+I`) plus the rename scoped by D4. Keep
id `0x1005`: saved keymaps key off the numeric id. In the same registry pass, two new commands over
plan 50's existing actions: Import Tone… on `Ctrl+Shift+I` and Export Tone… on `Ctrl+Shift+E`, each
opening the chooser its signal-chain header button opens and enabled exactly when that button is.
Their category and menu placement are settled at the build.
- Code: about 23 production files and 11 test files for the full rename. 13 doc files carry the
  song-export meaning, of which the three under `docs/plans/completed/` are historical records and
  stay as written, so 10 are rewritten. Every missed identifier is a compile error; strings,
  comments and docs are silent, so finish with a case-insensitive grep for "publish" (the unrelated
  "publishes" vocabulary stays).
- Tests with pinned strings: the File menu text, "Exporting song...", "Could not export: ...".
- Delete the registry's plan-46 italics comment, which sits on `ImportSong`
  (`editor_command_registry.cpp:41`).
- Must land before 4b.

**4.0c — One owner per chord on keymap restore (D5).** Extract the strip-then-add rule into one
keybinds helper, used by the keymap editor's assign and reset and by `EditorKeymapPersistence`'s restore
(which replaces JUCE's conflict-blind `restoreFromXml` loop). Tests in
`test_editor_keymap_persistence.cpp`: a restored override keeps a chord a default now claims; a stale
removal entry restores cleanly. Must land before 4a.

**4.0d — macOS defaults for the `/` chords (optional; D1, D2).** Only if the user wants the Mac
working before per-language keymaps exist. The registry would gain one seam for platform-specific
default chords: a Mac twin of `Shift+/` (Actions) and of `Ctrl+Shift+/` (the time-signature jump), each
a `?` key code. Keep the seam in the registry, not scattered `#if` blocks (the project's rule on
platform-specific code). Verify the `Cmd+Shift+?` Help-menu interaction on a real Mac before shipping
that twin. Rebinding stays the answer on non-US layouts until per-language keymaps arrive.

#### 4a — Jumps for the rows that exist (section, tempo, time signature, tone, "+")
**Core:**
1. **The target enum.** `enum class FocusRowJump : std::uint8_t { Section, Tempo, TimeSignature, Tone,
   AddAutomationLane }` beside `ChartCaretJump` (`chart_pointer.h`). The hand rows add `FretHandPosition`
   and `HandSpan` later.
2. **The intent.** `IEditorController::onFocusRowJumpRequested(FocusRowJump)`, plus the `EditorController`
   forwarder and the recording double.
3. **The action.** `EditorAction::JumpToFocusRow { FocusRowJump row; }` and its id. Every switch over the
   id takes the new case: the id mapping, four availability switches (gated exactly like the walk:
   chart loaded and transport paused), three controller switches, and the two unsaved-changes prompt
   switches in the view. Only the id mapping fails locally; the rest are `-Wswitch-enum`, which MSVC
   does not report, so sweep for every `StepToRowObject` case label and add the new case beside each.
4. **The handler.** It sits beside `StepToRowObject` and adds no second ladder:
   - apply the column rule;
   - list the stack;
   - if the target row is listed, `landOnRow(target, std::nullopt)`; otherwise do nothing, leaving any
     armed caret untouched.

   A private `focusRowFor(FocusRowJump)` maps each target to its `FocusRow`. Two things are
   load-bearing:
   - **Stack membership is the silent rule.** Without it, `Ctrl+Shift+M` on a song with no sections
     reaches `markerStarts(row).at(0)` and throws. Using membership keeps the walk's listing and the
     jump's silence one predicate.
   - **The column rule must run first.** Otherwise a jump from a region selected far from the cursor
     lands on the row found at the cursor, and the "+" landing shows another tone's lanes without
     re-syncing the rig.

   Fold "column rule, then list the stack" into one non-const call (a name like `rowsFromFocus`) that
   both `stepFocusRow` and the jump use, rather than two call sites that must remember the order.
   Do not call `prepareLandingRow`: that rule is for landings that keep the row.

**UI:**
5. **Command ids.** `CaretJumpSectionRow` `0x1511`, `CaretJumpTempoRow` `0x1512`,
   `CaretJumpTimeSignatureRow` `0x1513`, `CaretJumpToneRow` `0x1514`, `CaretJumpAddLaneRow` `0x1515`,
   each with Doxygen naming its chord.
6. **Registry.** File-local key constants (`g_section_key = 'm'`, `g_time_signature_key = '/'`, and so on) and two helpers,
   `markerAuthorChord(letter)` and `markerJumpChord(letter)`, all inside the anonymous namespace (a
   file-scope helper outside it fails macOS CI's `-Wmissing-prototypes`). `InsertSongSection` and
   `InsertToneChange` switch to the author helper, and the five jump rows go at the end of the Navigation
   block. A table-driven loop was rejected: it would reorder the registry and need optional author ids.
7. **EditorView.** The five ids join the always-active group (a silent jump must never beep), with one
   perform case each. The Navigate discovery menu gains a jump group.

**Grammar rule 2 leaves with Phase 3, which lands before or with 4a.** Today `Ctrl+M`/`Ctrl+T` still
select a marker exactly at the caret. Every cell where rule 2 selects, the jump selects the same
marker, so as a keyboard ROUTE it becomes redundant here. Deleting it alone would reopen two defects —
`Ctrl+T` on a region boundary returning silently, and `Ctrl+M` on an occupied downbeat opening an Add
Section prompt the core refuses — and Phase 3 item 2's restate at the cursor is what covers both.
Landing the two together rewrites rule 2's prose once (`editor_command_id.h:89-114`,
`keymap-matrix.md`, `marker-verb-grammar.md`, and the two UI tests that pin it).

**Tests:**
- `test_editor_controller_marker_rows.cpp`, with a `jump()` fixture helper:
  - each ruler jump from an armed string caret selects the holder, dissolves the caret and keeps the
    cursor, and a following `←/→` re-arms the remembered string;
  - the lead-in;
  - a song with no sections is silent and the caret stays armed;
  - the column rule from a pointer-selected section;
  - a repeated jump is idempotent;
  - a jump is refused while playing.
- `test_editor_controller_tone_automation.cpp`: the tone and "+" jumps from string and lane carets; a "+"
  jump from a far pointer-selected region shows that region's lanes; a "+" jump with no active tone is
  silent.
- `test_editor_action_availability.cpp`: the chart, no-chart and playing rows.
- `test_editor_view_state.cpp`: five rows in the position-sensitive locked registry table. The
  default-chord resolution test then fails on any collision, including `Ctrl+Shift+P` against Publish
  if 4b ran before 4.0b.
- `test_editor_view_timeline.cpp`: `Ctrl+Shift+M/B/T/A` and `Ctrl+Shift+/` route to the intent; no chart is consumed
  silently.

**Size:** about 16 code/test files, about 220 production and 200 test lines. Behavioural risk low;
the CI risk is the unreported switch sweep.

**Sighting brief:**
- Jump to each row from a string caret, a lane caret, and a pointer-selected marker far from the cursor.
- A song with no sections.
- A chart with no tone regions or no active tone ("+" jump).
- A jump, then `←/→`.
- `Ctrl+Shift+A`, then `Enter`.

#### 4b — The fret-hand position row
FHPs are stored per arrangement (`Chart::fret_hand_positions`), and each holds until the next, so the
tempo row's model fits unchanged: identity by position, and the first FHP owns the lead-in. Nothing
in-session edits FHPs today, so no new release hook is needed.
1. **The validator (D6).** Ascending order is enforced at `chart_rules.cpp:142`. Corpus-smoke decides
   between refusal and a keep-last repair. Update the "sorted" wording in `chart.h` and `chart_rules.h`,
   and `file-formats.md` (`fhps[]` strictly ascending).
2. **The selection kind.** `FretHandPositionSelection { GridPosition position; }` beside the tempo and
   time-signature kinds. It is cursor-coupled for free, and every dispatch ladder already falls through
   correctly (Delete, Alt-move, Enter inert; Esc releases; left out of `selection_present`). Add no
   explicit no-op arms.
3. **The model.** `MarkerRow::FretHandPosition`, handled in both switches over `MarkerRow`
   (`markerStarts` reads the current chart, bound once and guarded for the CI optional-access check;
   `markerSelectionAt`), plus a `selectedMarker` arm. No new `FocusRow` alternative: every marker
   row walks as `MarkerFocusRow{row}`, and `focusRowStack` lists the new row between the time
   signature and the strings (`sameReachGroup` already gives each marker row its own reach group).
4. **Behaviour on charts with FHPs.** `Ctrl+↑` from a string now reaches the FHP row, and `Ctrl+↓` from
   the time signature lands on it. Update the `CaretJumpSurfaceAbove/Below` Doxygen. Charts without FHPs
   (every existing test fixture) are unchanged.
5. **View state.** `ChartEditViewState::selected_fret_hand_position`, an index into
   `tab->fret_hand_positions` under the same contract as `selected_notes`. The projection is 1:1 with the
   chart, and the field has a default initializer.
6. **TabView.**
   - The host draws the accent outline from the existing `tabFhpChipBounds`, on the selected scrolling
     chip and on the pinned chip when it is the selected one, matching the ruler's chip frame. The
     game-shared paint core stays selection-free.
   - The pin stores an index instead of a copied `FhpViewState`. It re-derives when the selection
     changes, and a selected pin never yields (D12).
   - The pinned chip stays inert to the pointer (D11).
7. **The jump.** `CaretJumpFretHandPositionRow` `0x1516` on `Ctrl+Shift+P`, after 4.0b.

**Accepted trade:** landing from the lead-in selects FHP 0, whose only outlined mark may be its own
scrolling chip off-screen to the right, because the 2D pin shows nothing before the first placement
(the same trade as a late first section).

**Tests:**
- Marker-row walks, reach, Tab, releases and the lead-in on a fixture chart with FHPs.
- A duplicate-position case in `test_chart.cpp`.
- `test_tab_view.cpp`: outline pixels on the scrolling and pinned chips, the non-yielding selected pin,
  a selection-only refresh, and the pinned chip still inert.

**Size:** about 9 production files, about 150–220 production and 200–250 test lines. Risk low, except
that the validator is medium (gated by corpus-smoke).

**Sighting brief:**
- A GP import with dense FHPs.
- The pinned chip at a scroll edge.
- The lead-in.
- Tab across placements.

#### 4c — The span row (its own sub-phase; the hardest part)
Spans are derived and store nothing, never overlap, and routinely leave gaps. What is new is only
what gaps and derived data force:
1. **Fronts and closes (D9).** A lazy `ChartResolutions` cache behind a const accessor, self-refreshing
   on arrangement and chart revision.
2. **The kind (D8).** `HandSpanSelection { GridPosition front; }`, `MarkerRow::HandSpan` (no new
   `FocusRow` alternative — every marker row walks as `MarkerFocusRow{row}`), and the stack order
   time signature · span · FHP · strings.
3. **One holder function.** `markerHolderAt(MarkerRow, GridPosition) -> std::optional<std::size_t>`
   replaces `markerHolderIndex`:
   - tiling rows keep today's rule, and return nothing only when empty;
   - the span row takes the last front at or before the position, and keeps it only while
     position < musical close — half-open, so an abutting successor owns its seam.

   This deletes the "starts must not be empty" precondition and `landOnRow`'s "a holder always exists"
   assumption.
4. **One focus column.** `focusColumn()` returns the armed caret's position, else the paused cursor's.
   `focusRowStack` lists a marker row only while it has a holder at that column, and `landOnRow` computes
   the holder at that column BEFORE demoting the caret.

   This is load-bearing: an armed caret does not move the transport. Without it, Up from a caret inside
   a span, with the transport elsewhere, would skip the span row. The jump's silence in a gap then
   follows from stack membership for free.
5. **Tab** over fronts skips gaps unchanged, through `StepToRowObject`'s generic marker branch.
6. **Release (D7).** The generic `releaseMarkerSelectionNamingNothing` after undo, arrangement switch
   and seek. Pin with a test that the settle inside the select cannot move the front it just named; add
   a release inside `selectMarker` only if that test shows it can.
7. **View state and reveal.**
   - `ChartEditViewState::selected_hand_span`, an index into `tab->shapes`, left out of
     `selection_present`.
   - `chartSpanRevealed` gains a `selected` ground, the same shape as `chartNoteRevealed`, so a selected
     span draws out to its musical close.
8. **Selected style.** Export the rail rectangles from the paint core (a `tabShapeRailBounds` beside
   `tabFhpChipBounds`), and draw an accent highlight in a TabView overlay. The strength of the highlight
   is the sighting's call.
9. **The jump.** `CaretJumpHandSpanRow` `0x1517` on `Ctrl+Shift+H` (D10 does not block it).

**Probe first:** whether the shared test chart's measure 2 beat 1 dyad derives a span. If it does,
existing walk tests that arm there (for example "reaches between the ruler rows and the strings") must
move into a gap.

**Tests:**
- Up inside a span and in a gap.
- The focus-column case (caret inside a span, transport outside).
- The half-open close.
- Reach in and past the row.
- Tab across gaps.
- Inert verbs and releases.
- Undo that removes or keeps the front.
- Settle invariance.
- `Ctrl+Shift+H` silent in a gap.
- A strictly-ascending-fronts invariant in `test_chart_shapes.cpp` and a local census row.
- `test_tab_view.cpp`: rails reach the close when selected, and the highlight pixels.
- `test_tab_paint_core.cpp`: the exported rail bounds match the drawn rails.

**Size:** about 11 production files, about 250–350 production and 300–450 test lines. Medium risk: the
focus-column trap, the settle, and front uniqueness.

**Sighting brief:**
- Dense chord charts with short gaps.
- A span ending near the cursor.
- A landing successor that abuts its predecessor.
- The rail highlight's strength.

**Later, not Phase 4:** `Enter` on a selected span opens the TEMPLATE picker, whose apply authors a span
marker at the front (plan 61), which is also what gives a span a durable identity.

#### Order, commits and sightings
0. **The baseline refactor — DONE 2026-09-14, awaiting its sighting** (`fd895fcf`, `cdbc17c1`,
   `3cf7b1b1`, `80fe0c47`). Before 4.0a and Phase 3 build on it, the design review of the same day
   removed the rules the built code stated twice: every marker verb commits through one
   `commitMarkerModel<Snapshot>` funnel with the section rules in common core; the audible tone is
   re-derived, idempotently, only where its inputs change; the focus-row walk spells each rule
   once; and the playback tone-follow is the core's decision with the tone strip keeping only the
   frame tick. Deferred on purpose: the chord verb projection (Phase 3's build shape, below) and a
   per-kind marker policy table (4c's moment, when the holder rule is rewritten).
1. **4.0a** (the typing gate) lands BEFORE **Phase 3**, not merely before 4a: Phase 3 removes the
   caret requirement, so until the gate is in place a marker chord typed into a text field would
   author at the cursor in many more states than it can today. **4.0c** (keymap restore) and
   **4.0b** (Export/Import) are independent commits, in any order, any time before 4a ships.
2. **Phase 3** (the author-at-cursor grammar) before or with **4a** — one registry pass: the letter
   constants and the five jumps; **4.0d** alongside if the user wants the macOS defaults. **Sighting.**
3. **4b** — the FHP row and its jump. **Sighting.**
4. **4c** — the span row and its jump. **Sighting.**

If plan 60 rules that the FHP and span markers are one object, 4b and 4c merge into one hand row
before 4c is built. The select-only FHP row stays valid as the model for it.

#### Out of scope
- **Phase 3's build** — the author-at-cursor precedence (ruled; sequenced before or with 4a above).
- **`Shift+Enter`** — turning a selected marker into its time span (plans 47/52).
- **The span template picker and plan 61's span verbs.**
- **Plan 60's derived FHPs**, which would reuse 4c's cache.
- **Mouse selection** of FHP chips and spans.
- **Opening the walk and jumps to chartless arrangements.**
- **The macOS `Shift`-punctuation aliases** (backlog, D13).

#### Docs to change with Phase 4
- **This record:** a build record per sub-phase, the row stack and vertical table gaining the hand rows,
  and the holder paragraph (spans have gaps).
- **`keymap-matrix.md`:** the jump row goes Live; the Markers table gains a jump column; the File row;
  the time-signature and Actions chords; the stale rule-2 prose.
- **`marker-verb-grammar.md`:** the new-kind checklist gains: declare the letter once, register the
  jump beside the author chord, add the kind to `FocusRowJump`, `MarkerRow`, `markerHolderAt` and
  the stack. (Rule 2's retirement is already recorded there, with Phase 3.)
- **`docs/developer/keyboard-input.md`:** where key events enter (the typing gate), the path (b) action
  list, the focus-row paragraph, the preview whitelist paragraph, and the keybind recipe.
- **`docs/developer/the-editor-2d-views.md`:** the FHP pin and inert chrome, the span reveal grounds,
  and the `EditorSelection` list.
- **Export vocabulary:** `docs/developer/the-project-lifecycle.md`, `file-formats.md`,
  `changing-the-package-format.md` ("save is export"), and the example identifier in
  `docs/design/coding-conventions.md` (an example, not a rule change).
- **`docs/tracking/backlog.md`:** the D13 macOS aliases, and the stale "ruler shape-label band" comments
  in `tab_paint_core.cpp`.

## Docs swept with Phase 1 (done 2026-09-13)
`docs/plans/in-progress/editing-interaction-model.md`,
`docs/plans/in-progress/chart-span-and-selection-model.md`,
`docs/plans/in-progress/keymap-matrix.md`, `docs/plans/in-progress/marker-verb-grammar.md` and
`docs/developer/keyboard-input.md`. The 3D preview whitelist is seventeen commands, stated in the
guide and matching `g_preview_commands`.

## Backlog
The five pre-existing gaps found while planning were filed in `docs/tracking/backlog.md` on
2026-09-14.

## Verification
- Core: `test_editor_controller_marker_rows.cpp` (the walk, reach, the lead-in, the chip step, inert
  tempo and signature selections, undo releasing a stale selection, the marker `Tab` step);
  `test_editor_controller_tone_automation.cpp` (the rows below the strings, lane reach, the "+" row,
  an insert selecting its product); `test_chart_row_object_step.cpp` (`Tab` on the strings);
  `test_chart_caret.cpp` (demote on select, the `Esc` ladder, the exact-slot re-arm);
  `test_editor_controller_tone_regions.cpp` and `test_editor_controller_sections.cpp` (the
  `Alt+←/→` move).
- UI: `test_editor_view_state.cpp` (the position-sensitive locked registry table, default-chord
  resolution), `test_editor_view_timeline.cpp` (key routing), `test_tab_view.cpp`.
- Build through `.agents/rockhero-build.ps1`; delete the relevant `*_tests.exe` before trusting a run;
  reading pass for the CI blind-spot table (new variant alternatives → designated initializers,
  `-Wswitch-enum` on `MarkerRow`, optional access in tests).
- Sighting per phase before the next is built. That bullet and this one are the standing rule for
  Phases 3 and 4.
