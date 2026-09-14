# Keyboard focus rows — the caret arms on point rows, markers are walked by selection

*Status: DESIGN AGREED 2026-09-13 for Phases 1a, 1b and 2, with the user's rulings marked inline
(RULED) and one open leaning (arrows on a marker row). **Phases 1a, 1b and 2 BUILT 2026-09-13**
(records under each), awaiting one sighting of all three. Phase 3, the marker grammar, is still
open; the user chose to sight Phases 1 and 2 before holding that discussion. Supersedes the
armed-caret row model of `d320e7ac` (kept on `master` for reference only).*

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
  select — the precedence template loses an arm.

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

**Horizontal keys** (Tab RULED 2026-09-13: next object on every row; arrows on a marker row:
user LEANING 2026-09-13 toward "leave the row", confirmed or reversed by the Phase 1 sighting):

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

A `Tab` marker step starts from the SELECTED marker (index ±1), so after a mouse selection or an
`Alt+←/→` move it steps from what is outlined, not from wherever the cursor rests. A step past
either end is inert. `Alt+←/→` still moves the selected marker.

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
   rule; after pausing, walk back onto the row. Revisit only if it bites in sighting.
8. **Chartless arrangements:** the walk rides `StepChartCaret` (gated `has_chart`), so it is chart-only
   for now; recorded as a limitation.
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
  It is the select half of `applySongSectionSelection` and `applyToneSelection`, which keep only
  their deselect halves, and of the tempo and signature chip clicks.

**Marker-row helpers** — `Impl` members beside the pair, since each reads the session:
- `enum class MarkerRow { Section, Tempo, TimeSignature, Tone };`
- `markerStarts(row) -> std::vector<GridPosition>`, ascending;
- `markerHolderIndex(starts, position)` — one `upper_bound` in musical space: the last start at or
  before the position, else the first marker, which owns the lead-in. The position is the paused
  cursor's (`pausedCursorPosition`: the trusted column, else the nearest tick), so a marker the
  cursor was moved onto still holds it after Tracktion's write-back. `toneRegionIdAt` keeps its
  seconds-space loop, because the drawn tone row and the automation window share that span rule.

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

**Build record (2026-09-13, uncommitted, awaiting the sighting).** Built as specified, with these
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

**Build record (2026-09-13).** Built as specified, with these calls made during the build:
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

**Build record (2026-09-13).** Built as specified, with these calls made during the build:
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
direction cleaner than the armed-caret rows it replaced. More sighting may follow before Phase 2b.

### Phase 2b — direct row jumps and the hand rows (planned, not built)
Two additions discussed 2026-09-13, after Phases 1–2 were seen. Rulings are marked RULED, the user's
tentative answers LEANING.

**Direct jumps — RULED: `Ctrl+Shift` + the marker kind's letter.** `Ctrl`+letter already names the
document's marker kinds; `Shift` gives that letter its second meaning, "go to that kind's row", so
each pair reads as one family. A jump lands exactly as the walk does: it selects the marker holding
the cursor and demotes the caret in place. The string and lane rows need no key, since `←/→` return
to the row the caret was last armed on. `Shift`+letter was rejected: `Shift+T` is the left-hand tap,
`Shift+/` is the Actions dialog, and on letters `Shift` is the letter's second claimant (`Shift+M`
would belong to palm mute, `Shift+B` to bend).

| Row | Insert chord | Jump chord |
|---|---|---|
| Section | `Ctrl+M` | `Ctrl+Shift+M` |
| Tempo | `Ctrl+B` (reserved) | `Ctrl+Shift+B` |
| Time signature | `Ctrl+/` (reserved) | `Ctrl+Shift+/` |
| Span | `Ctrl+H` (reserved) | `Ctrl+Shift+H` |
| Fret-hand position | `Ctrl+P` (reserved) | `Ctrl+Shift+P` |
| Tone | `Ctrl+T` | `Ctrl+Shift+T` |
| "+" row | — | `Ctrl+Shift+A` (LEANING) |

- **The file chords move to free `Ctrl+Shift+P` — RULED direction.** Publish is renamed Export on
  `Ctrl+E`, and Import moves from `Ctrl+Shift+O` to `Ctrl+I`, overruling plan 46's avoidance of `Ctrl+I`
  (italics muscle memory). The menu labels and whether internal `Publish*` identifiers follow the
  rename are open.
- **`Ctrl+Shift+A` — LEANING,** with the user's concern that it sits beside the conventional
  select-all `Ctrl+A`. It lands on the "+" row and opens the parameter picker at once. Either slip is
  harmless: the picker mutates nothing until a parameter is chosen, and select-all mutates nothing.
  `Ctrl+Shift+L` (lane) is the alternative letter.
- **`/` is layout-fragile** (the key only matches where `/` is unshifted, as for `Ctrl+/` and
  `Shift+/`), and on macOS `Cmd+Shift+/` is the system Help search. Recorded, not solved.

**The hand rows.** The fret-hand position (FHP) row and the span row join the stack between the
time signature and the top string: time signature · span · FHP · strings. Both are select-only for
now, like the tempo and time-signature rows. Each is its own Ctrl-reach group, so each is a new
`FocusRow` alternative.
- **Selected style in the current band — RULED.** The FHP chips and span rails stay where they are
  drawn today, inside the top string's lane band; a selected FHP chip gets the selection outline, and a
  selected span highlights its rails. A selected span also reveals its full musical extent, since the
  rails otherwise stop at the trimmed drawn end and a cursor near the close would select a span that
  looks already over.
- **FHP identity and holder.** FHPs are stored per arrangement (`Chart::fret_hand_positions`), each
  holding until the next, so the tempo row's model fits: identity by position, and the first FHP
  owns the lead-in (matching the 3D highway, though the 2D lane pins no chip there). The validator
  accepts two FHPs at one position; tighten it to strictly ascending before keying selections by
  start.
- **Span identity and holder.** Spans are derived and store nothing, never overlap, and routinely
  leave gaps. A span is named by its front position; the selection is released after any chart edit or
  arrangement switch, since a re-derived front may name nothing. The holder is the span with
  front ≤ cursor < musical close — never the previous span — and the walk lists the span row only
  while a span holds the cursor, so the stack becomes cursor-dependent. The three places that assume a
  marker row always has a holder (`landOnRow`, `focusRowStack`, `moveCursorIntoSelectedMarker`) must
  learn the gap. The row's positions come from span fronts, not bracket positions, which box spans
  lack and landing successors defer.
- **A jump with nothing holding the cursor — LEANING: do nothing.** Only the span row has gaps, so this
  is the one case: `Ctrl+Shift+H` in a gap is silent.
- **Spans will carry a payload: the TEMPLATE.** Selecting a span is the way to specify its template
  (docs/plans/todo/span-marker-redesign.md), so `Enter` on a selected span becomes its restate, a
  template picker. Applying a template authors a span marker at the span's front, which pins that
  front and is what gives the span a durable identity (plan 61).
- **Dependencies.** Plan 60 may make FHPs derived rather than stored, and its first open ruling is
  whether the `Ctrl+P` and `Ctrl+H` markers are one object or two; one object would merge the two hand
  rows into one. The select-only rows are buildable before either ruling, and would be reshaped by it.

### Phase 3 — grammar (separate discussion before building)
Questions to settle, with current leanings:
1. **Rule 4 and the armed caret — RULED 2026-09-13, then RE-RULED the same day:** an insert
   leaves its product SELECTED, as a typed note does, and restating a selected marker keeps it
   selected; the caret the chord was typed from demotes in place. The first ruling (keep the caret
   armed, select nothing) guarded against `Right` stepping markers after an insert; once the
   arrows on a marker row were settled as "leave the row", that danger was gone, and consistency
   with the note won: the next `←/→` re-arms the caret exactly where it stood. The costs, accepted:
   a digit typed straight after a marker insert does nothing until an arrow re-arms, and `↑/↓` from
   the marker's row walk spatially rather than back to the string the chord was typed on. Phase 3's
   "overwrite at the caret" follows the same rule.
2. **Chord precedence collapses to:** marker of that kind selected → restate it; else caret armed →
   restate the marker exactly at the caret, or insert; else nothing. `performMarkerChord` loses its
   select arm and both select lambdas; `sectionAtMarker` / `toneRegionStartingAtMarker` stay (overwrite
   needs them).
3. **Tone has two payload verbs.** Retone (repoint the region; today's `Enter`/`Ctrl+T` restate) and
   rename (the tone document's name, shared by every region using it; today's double-click). Pointer
   and key already disagree. Options: `Enter` = the kind's primary "open" (tone → drill into the
   signal chain, section → rename) with `Ctrl+R` = rename any named marker (free today); or `Enter` =
   rename everywhere with `Ctrl+T` alone retoning. Section rename is both its restate and its rename,
   so `Ctrl+M` and `Ctrl+R` coincide there.
4. Plan 41 still says `Ctrl+B` uses "armed caret, else transport" (retired by `804879d6`), and its
   meter re-addressing list must add editor selections.

## Docs to rewrite in the same change as Phase 1 (replace, don't stack amendments)
- `docs/plans/in-progress/editing-interaction-model.md:80-98` (row axis) and `:348-367` (tone-region
  row with an armed caret, grid-stepping Left/Right, Insert split, Enter drill).
- `docs/plans/in-progress/chart-span-and-selection-model.md:403-409` (rows, not strings).
- `docs/plans/in-progress/keymap-matrix.md`: Navigation table (the `Ctrl+↑/↓` "adjacent surface" `Δ`
  row becomes the live group reach), *Tone-region row* section, *Automation lanes — creating a lane*
  section (its proposed focusable "+ add automation" row becomes live), *Song sections* click row,
  `:251` stale "armed caret else transport".
- `docs/plans/in-progress/marker-verb-grammar.md`: rule 2's role and the checklist for new kinds.
- `docs/developer/keyboard-input.md`: ChartMarker description, preview whitelist count (13, doc says
  12), and the new-keybind recipe if steps change.

## Backlog candidates (pre-existing, outside these phases)
- A lane caret's keyboard steps can leave the active region's window, so Up re-selects a different
  region; clamp or document.
- A lane caret is saved as transport time under a comment claiming "lane arming seeks"; keyboard
  arming does not.
- Structural suppression of focus traversal out of `EditorView`, so unbinding Tab cannot bring the
  focus-escape hazard back.
- Padding string lanes (minimum display count) are skipped by the walk; document.
- Consecutive identical meters are not coalesced (the tone law's analogue); consider under plan 41.

## Verification (when built)
- Core: `marker_navigation` unit tests (lead-in, no sections, cursor exactly on a start, first
  section late); `test_chart_caret.cpp` walk tests for every cell of the vertical table above (plain
  and `Ctrl`, with zero, one and several lanes), the "+" row's picker arming the new lane, the
  off-grid round trip, the mouse-selected-region → lanes case, arrows from a marker row re-arming on
  the remembered row (string and lane origins, stale-lane fallback); Phase 2 Tab steps from the
  selection; undo releasing a stale section; the `selectedMarker`/`selectMarker` round trip;
  dissolve re-syncing the audible tone.
- UI: ruler hit-test and reserved-placement tests for all three chip rows; key routing for
  Up/Down/Left/Right on marker rows through `pressCommandKey`; Phase 2 adds the locked registry
  table rows (`test_editor_view_state.cpp:316`, position-sensitive) and Tab routing.
- Build through `.agents/rockhero-build.ps1`; delete the relevant `*_tests.exe` before trusting a run;
  reading pass for the CI blind-spot table (new variant alternatives → designated initializers,
  `-Wswitch-enum` on `MarkerRow`, optional access in tests).
- Sighting per phase before the next is built.
