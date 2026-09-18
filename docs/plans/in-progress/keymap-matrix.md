# RockHero editor keymap — per-keybind × surface reference (working draft for verification)

> **Status: SIGNED OFF 2026-07-20** (every confirm flag accepted; G53-FOLD-IN and G46-KEYMAP
> closed — this matrix is the authoritative default keymap, superseding plan 46's Appendix
> tier A). Every keybind is a row; every editing surface is a column, so you can read across one
> row and see exactly what it does — or doesn't do — on each surface, with no binding hidden by
> editorial judgement. The matrix now serves as plan 53's build-tracking artifact: rows flip to
> Live as phases land, and it dissolves into `editing-interaction-model.md` (with the parked
> rows as registry entries) when plan 53 completes.
>
> **Total rebindability (plan 53 Phase 1b, executed 2026-07-21):** every keybind in this
> matrix is a registered, individually rebindable command — the chords shown are the shipped
> *defaults*. The modifier rules below describe the default map's shape, no longer an enforced
> restriction; a user who rebinds away from the algebra owns the result, with per-command and
> reset-all defaults as the fallback.
>
> **Amended 2026-08-12 (user-signed): the technique letter map.** The typed technique family now
> follows one rule — the plain letter is the first letter of our own verb's name — replacing
> per-key Guitar Pro compatibility. Legato moved `H` → `L`, the left-hand tap `Ctrl+H` →
> `Shift+T`, and `H` was freed for the harmonics. Rationale and the full map in the technique verb
> section, which also states once what the `Shift` plane itself means (2026-08-25).
>
> **Amended 2026-08-23 (user-signed): the `Ctrl` PRECISION tier is retired.** Off-grid authoring
> is now a MODE, not a per-gesture modifier: `Ctrl+G` toggles a session grid-snap switch, and one
> placement quantum (the grid note value while snap is on, the 1/3840-whole-note tick while it is
> off) answers every position-quantizing verb on every surface. Every row below reading "fine",
> "1/960", or "off-grid under `Ctrl`" is retired with it — the six `Ctrl+Alt…` commands are gone
> and `Ctrl` composes nothing on a placement. The design is
> `docs/plans/in-progress/grid-snap.md`; this matrix's affected rows are stale until it is folded
> in.
>
> **Amended 2026-09-11 (user-signed): every note is TYPED, a click never creates, and `Alt` creates
> only the slide-out.** One grammar decides every entry row below, and it has one authoring key.
> A DIGIT at the armed caret is the whole of chart entry: on an EMPTY slot, and at a ring's EXACT
> END, it is a head — the next note; on a slot a ring COVERS it is a POINT on that note's path at
> the typed fret. `Alt`+digit differs in exactly ONE cell — at a ring's exact end where nothing
> stands it CREATES the slide-out, which is the only thing on this lane `Alt` creates. A CLICK,
> under any modifier, arms the caret and selects what is there; it never creates. The SPLIT is two
> keystrokes rather than a gesture of its own: a digit plants the point, `Shift+L` disconnects it,
> and `planToggleJunctions`' segment walk — now its only caller — makes the point the new head
> with everything after it riding the new note. (`Shift`+`Insert` held the SECTION insert for one
> day under this amendment; the marker grammar below returned it to `Ctrl+M`.)
>
> **Retired with it, and every row below for them goes:** `Insert` ("Insert Note") on the chart,
> `Alt`+`Insert` ("Insert Point"), `Shift`+`Insert` as "Insert Note, Repeating Fret", the
> `Alt`+click and `Alt`+double-click authoring gestures, `Shift+Alt`+click, the fret-in-force
> default, the fret-0 default, the insert ghost, and the strike/split an insert once carried. The
> `Alt` ring REVEAL stays, and so do the Windows Alt-code filter, the both-key-code `Alt`+digit
> chords, `Alt`+arrows and `Alt`+wheel. Nothing single-press truncates a ring or clips a keyframe,
> still: the ring clamp and the clearance repair remain the load, import and MOVE authorities, and
> the MOVE verb shortens a ring and rides its release back without ever deleting a statement — a
> landing that would clip any other keyframe off the tail is refused whole.
>
> *This rewrites the two 2026-09-11 forms of the amendment in place — the morning's truncating
> strike and the afternoon's lossless split under a bare gesture — rather than stacking a third
> note on them.*
>
> **Amended 2026-09-12 (user-signed): the marker grammar.** Three planes, one sentence: **letters
> touch the note, `Ctrl` touches the document, and `Alt`+letter opens a menu.** A MARKER is a
> stated fact about the document at a position — a section starts, the tempo pins, the meter
> changes, the tone changes, the hand does this here — and `Ctrl`+letter AUTHORS a marker of that
> kind at the CURSOR: it restates the one standing exactly there, else inserts one (re-ruled and
> BUILT 2026-09-14: the cursor is the armed caret, else the paused cursor's slot, snapped to the
> kind's own quantum, and the chord is inert while playing and with no song; the chord never reads
> the selection). That is the digit law's positional create-or-retype: an object under the cursor is
> the operand, an empty slot creates. `Enter` restates a selected marker, `Ctrl+R` renames it where
> its kind has a name, `Alt+←/→` moves it by its kind's step and `Delete` removes it; no kind gets a
> verb of its own beyond its chord. *(Retired by
> the re-ruling: the 2026-09-12 half where the chord restated a SELECTED marker wherever the cursor
> was, and the 2026-09-13 caret-only position.)* The
> five kinds and their chords are the *Markers* table below: `Ctrl+T` and `Ctrl+M` are live, the
> other three are RESERVED for the plans that build their objects (six until 2026-09-15, when the
> fret-hand position and the span became ONE hand marker on `Ctrl+H`). `Alt`+letter is the platform's
> menu-access plane and nothing else: `Alt+F` / `Alt+E` / `Alt+V` open the menus. `Alt` alone
> stays the ring reveal and `Alt`+digit stays the slide-out. Five rival families — `Shift`+letter,
> the `Ctrl` plane without menu access, a leader key, a ruler caret, and a structure mode — were
> each built at full strength and lost on a fact, not a taste; the `Alt`+letter authoring plane
> won that round and then lost to the platform's menu convention, which this app now honors.
>
> **Retired with it:** `Shift`+`Insert` as the section insert, `F2` ("Rename Section", `0x1403`,
> the section chord restates instead), the tabled `Shift+S` span-marker chord, and the tempo
> plan's `Alt`+click anchor insert.

## The rule this encodes

`Ctrl`'s meaning follows the **operation**, not the key:

- **Navigating** — moving the caret / extending a selection → `Ctrl` = **REACH** (coarser unit:
  measure, section, first/last row).
- **Placing / moving an object** — pointer placement, or `Alt`+arrows → `Ctrl` = **PRECISION**
  (off-grid / 1/960 fine).
- **Clicking an existing object** → `Ctrl` = **TOGGLE** selection membership.
- **`Ctrl`+letter** → the DOCUMENT: file, history, view, grid, and the marker family (a
  section, tone change, tempo anchor, meter or hand marker inserted at the cursor, or
  restated when one already stands there). Letters alone touch the note under the caret; `Ctrl`
  never does.

`Alt` = the authoring gate (input mutates). Holding it *alone* mutates nothing and instead
**reveals what it authors**: every visible note in the 2D chart lane shows its ACTUAL ring — the
stored duration `Alt`+wheel edits, which the presented tail may have trimmed or dropped — and
releasing snaps back (stage B of `docs/plans/in-progress/note-sustain-model.md`, shipped
2026-08-22). Global while held, never selection-scoped, and never hit-testable: clicks, marquees
and the `Alt` gestures keep resolving against the presented picture. The mark it makes is the
notation itself: the lane redraws in the chart's actual form (ruled 2026-08-23; the outline
candidate and its `F6` toggle are deleted).

**Windows composes `Alt`+numpad digits, and the key entry filters the product out.** While `Alt` is
held, Windows accumulates numpad digits into an "Alt code" and delivers the composed CHARACTER on
the `Alt` release, which JUCE reports as a bare key press with no modifiers — so `Alt`+7 then
`Alt`+6 would arrive as a plain `L` and fire the legato verb, and the codes 27 and 32 would fire
cancel and play/pause. Each top-level window filters at its own key entry (`MainWindow` and
`PreviewWindow` alike): a press carrying NO modifiers whose key is not physically down
(`juce::KeyPress::isKeyCurrentlyDown`) is swallowed before the mapping set sees it, so a composed
character never reaches the keymap. The same OS path is why the `Alt`+digit row below registers both
key codes — under `Alt` a numpad digit reaches JUCE with the TOP-ROW code (`doKeyDown`'s
`MapVirtualKey` path), so on Windows the top-row chord is the one that matches.

**While a text field in the main window is being edited, NO command in this matrix runs from the
keyboard** (D3, built 2026-09-14): `MainWindow::keyPressed` hands a press to the mapping set only
when the window's peer has no text input target, so the short inline edits that live there — the
grid value box and the output-gain text box — keep every key they decline instead of leaking it to
the chart behind them, and `Tab`/`Shift+Tab` fall to JUCE's own focus traversal, which commits the
typed value.

`Shift` = range / extend / axis-lock — with one named exception, `Shift+Tab`, which steps back to
the previous object as it does everywhere else keyboards use Tab, rather than extending anything
(2026-09-13). The **time
selection** is **always grid-locked** — keyboard *and* pointer, never finer than the display grid
(decision B, 2026-07-19; this **amends plan 47**, dropping its `Ctrl`-off-grid range endpoints).

## Two selection kinds, one at a time (decision A, 2026-07-19)

The single editor-wide selection is at any moment **one of two kinds** — making one clears the
other; verbs dispatch on whichever is active (no precedence):

- **Object selection** — a *set* of objects (notes / point / region). Built by click, `Ctrl`+click,
  marquee, double-click.
- **Time selection** — a grid-locked *span* (full height, can be empty). Built by `Shift`+click,
  `Shift`+arrows, ruler drag. Feeds the **loop region** (a separate persistent transport state).

## The surfaces

| Surface | What it holds | Keyboard model |
|---|---|---|
| **Chart** (the note highway) | notes on strings | caret (point) |
| **Automation lanes** | parameter points | caret (point) |
| **Tone strip** | tone regions | **selectable region-row** — keyboard-navigable (select the region at the cursor's time); span-selection, *not* point-placement |
| **Plugin chain** (bottom panel) | the current tone's plugins | slot-focus (`✚ proposed`) — signal-order, no time caret |

## Legends

**Surface cell:** *text* = the behavior on that surface · `✓` = bound & identical to the others ·
`✗` = **not bound** on that surface · `—` = that surface has no such object (n/a).

**Status:** *(none)* = Live today · `Δ` = shipped but this scheme changes it · `✚` = new this
session (unbuilt) · `◇` = open, needs your call · `▷N` = in plan N (47 loop · 52 range · 40 chart ·
46 registry).

---

## Navigation — caret

Point-caret lives on chart + lanes; the tone strip participates as a selectable **region-row** (no point-caret) — see *Tone-region row*.

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `←` / `→` | → next stop (grid **or** note) | → next stop (grid **or** point) | `✗` | Live |
| `Ctrl+←/→` | → **measure** jump | → **measure** jump | `✗` | Live |
| `↑` / `↓` | → adjacent string; `↑` from the top string SELECTS the time-signature chip holding the cursor, then the tempo chip, then the section chip (caret demoted in place, cursor unmoved); `↓` from string 1 selects the tone region holding the cursor | → adjacent lane; `↑` from the first lane selects the tone region, `↓` from the last lane selects the "+" row | from a selected region: `↑` arms string 1, `↓` arms the first lane (the "+" row when the tone has none) | Live (2026-09-13, the focus rows — `keyboard-focus-rows.md`; unsighted) |
| `Ctrl+↑/↓` | → nearest row of the adjacent **group** — section, tempo, time signature, the strings, the tone row, the lanes, the "+" row: `Ctrl+↑` from any string selects the time-signature chip, `Ctrl+↓` the tone region | `Ctrl+↑` from any lane selects the tone region; `Ctrl+↓` selects the "+" row | `Ctrl+↑` arms string 1; `Ctrl+↓` arms the first lane (or the "+" row) | Live (2026-09-13, `CaretJumpSurfaceAbove`/`Below`, `0x150B`/`0x150C`; unsighted) |
| `Tab` / `Shift+Tab` | → next / previous **object** on the caret's string, grid ignored: a note or a keyframe, always landing on a note's head (a held stop's satellite is not an object of its own) | → next / previous **point** | from a selected region: the next / previous region start strictly after / before the CURSOR, its start becoming the cursor — so from a cursor inside the region past its start, `Shift+Tab` lands on that region's own start (every marker row steps the same way, from the cursor); inert on the "+" row; from the passive marker the first press arms in place | Live (2026-09-13, `CaretStepNextObject`/`PreviousObject`, `0x150D`/`0x150E`; reads the cursor 2026-09-14; unsighted) |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | → next / previous **note**, over the string's keyframes | same as `Tab` (a lane has no keyframes) | same as `Tab` | Live (2026-09-13, `CaretStepNextNote`/`PreviousNote`, `0x150F`/`0x1510`; the PHYSICAL Ctrl key on every platform, since Cmd+Tab is the macOS app switcher; unsighted) |
| `PageUp` / `PageDn` | → prev / next **section stop**: the chart start, every section start, the chart end, as one set | same | `✗` | Live (ae0e7ad5; the chart bounds joined the stops 2026-09-14, so PageUp from the first section reaches the start and PageDn from the last reaches the end; Ctrl rides along as an alias — accepted 2026-07-20) |
| `Home` / `End` | → chart **start / end** | → chart **start / end** | `✗` | Live (ae0e7ad5) |
| `Ctrl+Home` / `Ctrl+End` | chart start / end (alias) | chart start / end (alias) | `✗` | Live (ae0e7ad5) |

## Time selection (one full-height span — crosses every surface, so not per-surface)

| Keybind | Behavior | Status |
|---|---|---|
| `Shift+←/→` | extend time-range by the **display grid**. A plain `←/→` afterwards leaves the span the way a caret leaves the slot it stands on: `←` arms one grid step past the span's START, `→` one past its END, and the span is released (2026-09-15) | Live (759b145f) |
| `Shift+Ctrl+←/→` | extend time-range by **measure** | Live (759b145f) |
| `Shift+PageUp/Dn` | extend time-range by **section stop** (the chart start and end count, as for `PageUp/Dn`) | Live (759b145f; bounds 2026-09-14) |
| `Shift+Home` / `Shift+End` | extend time-range to chart **start / end** | Live (759b145f) |
| `Shift+↑/↓` | *(nothing — the range is full-height; no vertical extension)* | `—` unbound (confirmed) |

*(759b145f ships conservative defaults on the unsigned sub-decisions — accepted 2026-07-20 as
placeholders until plan 52's range verbs land: typing with a range is inert (52-Q11), Delete over
a range is a no-op pending plan 52's content-delete, and the extend is paused-only.)*

## Authoring — move / resize / fret (acts on the object selection)

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `Alt+←/→` | move note(s) in time (grid) — and a selected KEYFRAME by its OFFSET along the ring it rides, the same placement-quantum step at that note's measure (W13 ruled, 2026-09-09). On the RELEASE (the keyframe at the ring's end, the falls-away chip) the step drags the ring's end with it — the slide-out lengthens or shortens — refused onto the last sounded fret (ruled 2026-09-10: the chip is the fall's handle, the tail verb never moves a point). One planner and one entry for a mixed selection; a selected note's own keyframes ride at unchanged offsets, since an offset is relative to its onset. Bounds are the rule authority's through the finalize gate, so a step onto or across a neighbour refuses rather than swapping — the offset IS the keyframe's identity, which is also why the step re-keys the selection. A note landing INSIDE an earlier note's tail on its string re-strikes it, and this is the one editing gesture that truncates: that ring SHORTENS to the landing and its release rides back to its clearance. It never DELETES a statement, though (ruled 2026-09-11) — a landing that would clip any OTHER keyframe off that tail is refused whole, since the statement belongs to a note the charter never touched; a keyframe standing exactly ON the landing is moved back by the clearance repair, not erased, so it is allowed. A held or repeated run is ONE GESTURE and one undo entry (see the gesture note below) | move point in time (grid) | move a selected region's START one placement-quantum line; the paused cursor follows (see *Tone-region row*) | Live · tone strip 2026-09-14 |
| `Ctrl+Alt+←/→` | move **1/960 fine** | move **1/960 fine** | `✗` | **Retired 2026-08-23** — snap off + `Alt+←/→` |
| `Alt+↑/↓` | move across **strings** — notes only: a keyframe has no string of its own and a selected head carries its path across by construction, so a keyframe-only selection is inert here (W13 ruled, 2026-09-09). Same gesture as the row above: a run of presses in either axis is one entry | move **value** — the lane's value readout shows the selected point whenever one is selected (and the dragged point while a drag stands), so a keyboard nudge reads its result without a pointer | `✗` | Live |
| **`Ctrl+Alt+↑/↓`** | **`✗` (strings are discrete — no fine)** | **move fine value** | `✗` | **Retired 2026-08-23** — the value tier went with the fine tier |
| `Shift+Alt+←/→` | resize **sustain** (grid) — from a selected head OR a selected KEYFRAME, which reaches the ring it rides: a keyframe sits on the tail and this is the verb that acts on the tail, so the end of a slide is a place to pull the tail out from. The HEAD verbs (mute, accent, the techniques) deliberately do not reach through a keyframe (user ruled, 2026-09-09). A step every bound absorbs is refused and never recorded, so a tail at its floor or ceiling simply stops and the next press the other way moves it (2026-09-09) | `—` (points have no extent) | `✗` (pointer edge-drag instead) | Live |
| `Ctrl+Shift+Alt+←/→` | resize sustain **fine** | `—` | `✗` | **Retired 2026-08-23** — snap off + `Shift+Alt+←/→` |
| `Shift+Alt+↑/↓` | **fret shift** ±1 — over heads and selected KEYFRAMES alike, off one anchor (the lowest stop the selection addresses), which is the delta form a chord slide needs (W13 ruled, 2026-09-09) | `—` (no frets) | `✗` | Live |

*(The `Ctrl+Alt+↑/↓` row was your example of a chord bound on one surface and unbound on another;
it is retired outright now, but the asymmetry it illustrated is still how the matrix reads.)*

*(GESTURE ROWS. THREE verbs now run this shape: the sustain row, the two `Alt`+arrow move rows, and
— since 2026-09-16 — the harmonic verb `H`. Each is ONE verb and one gesture: repeated presses
re-plan the whole selection from the values the run started at, and the run stays one undo entry.
All three run through one authority (`commitChartGestureStep`), so all three end at exactly the
same commit points — a selection change, a caret move, any other verb, undo/redo, a save, a
committing settle — and a run that returns to its start retires its entry rather than leaving a
Ctrl+Z that changes nothing. What differs is only what a STEP means. The sustain and move rows
record a step LIST and replay it: a sustain step moves the ring's END onto the adjacent line of the
placement quantum's lattice, so a run may cross a snap toggle; a move step carries the selection by
that quantum scaled by the meter where the run has REACHED, so a run crossing a signature change
steps by that meter's own amount from there on — which is why neither verb can sum its presses into
one delta. The harmonic verb keeps no list at all: its "gesture" is the LATEST CHOICE, each one
planned from the pre-run chart, so `ChartHarmonicGesture` is an empty alternative. Sustain ruled
2026-08-22, steps 2026-08-23; the move rows joined it as ruling 8's own extension; the harmonic
verb joined when it stopped being a toggle; `docs/plans/in-progress/note-sustain-model.md` ruling 8.
The `Alt`+wheel duration rows below are the sustain verb through the pointer.)*

## Payload entry

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `0`–`9` / numpad `0`–`9` | **THE WHOLE OF CHART ENTRY — every note is TYPED** (re-ruled 2026-09-11; this row's two earlier same-day forms, the truncating strike and the lossless split under a bare digit, are both superseded). On an EMPTY slot, a **head** at the typed fret. At the EXACT END of a ring, a **head — the next note**: the ring already stops there, so there is nothing to divide and nothing to shorten, which is what makes **sequential entry safe**, a digit at the previous note's end always being the note after it. Where a slide-out ALREADY ends on that slot, arming the caret selects its chip, so the digit RETYPES the fall by the ordinary selection rule rather than placing anything. STRICTLY INSIDE a ring, a **POINT on that note's path** at the typed fret — the same product `Alt`+digit states, since inside a ring the modifier has nothing left to say. There is no note gesture over a covered slot at all: to divide a ringing note, type the point and press `Shift+L` (the row below). **Sequential entry meets the covered case only past the grid**: the slot after a grid-step ring is that ring's END, so the digit is the next note, while a ring deliberately lengthened PAST its grid step makes the following slot a covered one, where the digit is a point instead — Guitar Pro users never meet this, its durations being per beat. A digit inside an OPEN STRING's tail shows the red box: a fret-stating point on an open string is refused by chart law (`OpenStringSlide`), nothing being pressed to glide. A point that merely restates the fret the path is already running on says nothing, so it is silent authoring state and dissolves with focus — typing the same fret on a tail leaves nothing behind. Multi-digit through the shared 750 ms window; an illegal fret paints red and discards. A digit over a non-empty SELECTION retypes it instead — bare or under `Alt`: a non-empty selection is the operand either way, and only a digit at a bare caret standing on a ring's exact end has anything to choose between. Which STOP the digits state is the caret's channel (2026-08-27, the held-fret increment): bare digits state the note's own sounding fret, and digits after clicking the satellite digit beside a bracket — or after stepping the caret onto that satellite — state the fretting hand's `held` stop under a right-hand onset — refused on a note carrying a NODE, whose stop is its own `fret` and whose satellite is therefore read-only (2026-09-17). One pending entry either way; the channel decides where it lands and where its red box draws. A selected KEYFRAME retypes the same way (W13 ruled, 2026-09-09): the flow, the multi-digit window and `planRetypeFrets` are the note flow's, the entry simply gained a keyframe operand, and the SELECTION KIND — not a third `ChartStopChannel` value — says which stop the digit reached. Its pending BOX has no target yet, since the view publishes retype targets as note indices; a refused keyframe digit is therefore silent until that display lands (`docs/tracking/backlog.md`). A digit CONTINUES a live pending entry rather than opening a second one, bare or under `Alt`, and THE FIRST DIGIT'S MODIFIER DECIDES what that entry creates — so at a ring's exact end, the one slot where the two chords part, `Alt`+1 then a bare 2 is a fret-12 slide-out and a bare 1 then `Alt`+2 is a fret-12 head | open **value editor** at armed caret | `✗` | Live |
| `Alt`+digit · `Alt`+numpad digit | **THE SLIDE-OUT, and nothing else of its own** — the ONE thing `Alt` creates on this lane (re-ruled 2026-09-11). At the EXACT END of a ring where nothing yet stands, the typed fret is the **release keyframe**: the one keystroke that authors a fall, and the one cell where this chord differs from the bare digit, which lands the next note there instead. Everywhere else it IS the bare digit: strictly INSIDE a ring the same point on the path, on an EMPTY slot the same head, over a slide-out already at the slot the same retype of the selected chip — so a mistimed `Alt` costs nothing anywhere. The multi-digit window, the red-box refusal, the open-string refusal and the retype-the-selection clause are all the digit row's above — one pending entry, whichever chord opened it. Registered on BOTH key codes because `Alt` changes which one arrives: under `Alt` a numpad digit reaches JUCE with the TOP-ROW code on Windows, so the top-row chord is the one that matches there (`TypePathDigit0`–`9`, "Type Path Digit N") | `✗` | `✗` | Live |
| `Ctrl`+digit | `✗` | `✗` | `✗` | Live (guarded) |

**PLANNED — a technique letter on a COVERED slot states its own channel at the caret.** The digit
states the position channel there; the same slot has other channels to state, and the letter that
owns one is the natural key for it: `V` on a covered slot would plant a **vibrato keyframe** and `B`
a **bend point**, each at the caret's instant on the ring that covers it, exactly as a digit plants a
position point. Recorded here as the shape the entry column is expected to take; `B` waits on the
bend plan, and neither is built. `✚`

## Editing verbs

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `Delete` / `Backspace` | delete note(s) | delete point | delete region (merges) | Live |
| `Insert` | ~~a NOTE with no digit to type~~ — **the chart half is RETIRED 2026-09-11**: every note is TYPED, so a key that supplies a fret nothing stated has nothing to author. The registry command (`NeutralInsert`) keeps its other surfaces and loses the name "Insert Note" with the verb | on-curve point at caret | `✗` (no keyboard) | Live on the lanes · **chart half retired 2026-09-11** |
| ~~`Shift`+`Insert`~~ | ~~the same note, at the fret in force~~ | ~~same as `Insert`~~ | ~~`✗`~~ | **Retired 2026-09-11** — the FRET IN FORCE default (`fretInForceOn`, `NeutralInsertRepeat`, "Insert Note, Repeating Fret") goes with the fret-less head it defaulted: there is no fretless head left to default. Repeating a whole note is copy and paste's job when that lands. The chord held the section insert for one day and is now unbound: the section insert is `Ctrl+M` (marker grammar, 2026-09-12) |
| ~~`Alt`+`Insert`~~ | ~~a silent point on the path, caret armed~~ | ~~`—`~~ | ~~`✗`~~ | **Retired 2026-09-11** (`InsertPoint`, "Insert Point") — a point with no fret to type restated the running fret, which says nothing, and the grammar now reaches that state through the digit itself: a typed fret the path already holds is the same silent authoring state, dissolving with focus and never written. **THE COMMIT LAW survives the key and is unchanged:** a point that says nothing — no bend, no shake, a fret the path passes through anyway — pushes NO undo entry, dissolves when its NOTE leaves focus, collapses before undo or redo replays, and is never written, so a charter plants the point first, walks the tail to where the slide lands, and gives the start its meaning second |
| ~~`Ctrl+D`~~ | — | — | **verb dropped** | **RESOLVED 2026-08-08 — see below** |

**RESOLVED 2026-08-08 — there is no duplicate verb in the chart scope.** The user: *"Wouldn't
copy/paste via Ctrl+C/Ctrl+V be the mechanism for duplicating objects? Ctrl+D seems unconventional and
most likely unneeded."* Guitar Pro settles it: it has **no duplicate command at all** — Copy is
`Ctrl+C`, Paste is `Ctrl+V`, and **`Ctrl+D` is bound to Brush Down**, a strum-direction technique. So
the chord is not merely unconventional here, it is conventionally spoken for by a technique mark. With
a caret defining the paste target, copy/paste covers duplication in two chords and needs no third.

**`Ctrl+D` is therefore reserved for Brush Down**, should strum direction ever be notated — it is not
in the model today (there is no brush attack), but it is real notation and this is where it would land.

The plugin-chain scope keeps its own `Ctrl+D` for now (below), since bindings are scope-local and a
plugin's state has no clipboard yet. Worth revisiting together: if a plugin clipboard lands, copy/paste
likely subsumes that one too by the same argument.

## Markers (the `Ctrl` plane)

A marker is a stated fact about the document at a position. Every kind has ONE chord, and that
chord is an AUTHORING verb that reads ONE precedence at the cursor. **This is the law every marker
kind follows, the RESERVED ones below included** — a new kind inherits it rather than inventing its
own (ruled and BUILT 2026-09-14; `marker-verb-grammar.md` is the design record):

1. a marker of that kind stands EXACTLY at the cursor, under the kind's quantum — the chord
   RESTATES it, reopening its payload;
2. else — the chord INSERTS one there.

The cursor is the armed caret, else the paused cursor's slot (where an arrow press would arm), and
the chord is inert while the transport plays and with no song — the playing half is one case of
the wider rule: **while the transport plays, marker selection and every marker edit are
unavailable** (ruled 2026-09-14), pointer gestures included, for every kind in this table and for
automation
points. The tone designer is outside it: the plugin chain, plugin parameters and the output gain
stay live mid-play, and renaming a tone document is not a marker edit. The selection is never read
and never written: a marker selected elsewhere does not redirect the chord, and the chord itself
never selects. The selection has its own verbs — `Enter` restates it and `Ctrl+R` renames it where
its kind has a name. The keyboard's route onto an existing marker is the focus-row walk, `Tab`,
`Enter`, a click, and the `Ctrl+Shift`+letter jumps — each kind's `Ctrl` pair is
author / select.

Selecting a marker DISARMS the armed caret, demoted in place so the cursor line stays put: an armed
caret is where the next keystroke would author, so one standing beside a selected marker would be a
second answer to the same question. `Esc` drops the selection. `Alt+←/→` MOVES the selection by the
kind's step. A verb that authors or restates a marker leaves it SELECTED, as a typed note is, so
the next verb acts on what was just made; the caret the chord was typed from demotes in place, and
the next `←/→` re-arms it exactly where it stood (confirmed 2026-09-13 with the focus rows).
Click selects a marker's chip; double-click is the pointer form of `Enter` where a chip has one. A
kind with no payload has nothing to restate, so a chord on one only selects it.

*Retired 2026-09-14:* the precedence built 2026-09-12/13, in which a SELECTED marker was restated
wherever the cursor stood, and a marker exactly at the caret was SELECTED — then the keyboard's only
path onto a marker.

The **Jump** column is each letter's second claimant, the select half of the pair: it lands focus on
that kind's row exactly as the vertical walk does, and is silent where the row holds nothing. The
five rows that exist went Live 2026-09-15 with step 4a; the one hand row arrives with its row
(plan 60 Phase 3).

| Chord | Jump | Marker | Scope | Quantum | Payload | Status |
|---|---|---|---|---|---|---|
| `Ctrl+T` | `Ctrl+Shift+T` (`0x1514`) | tone change (the region boundary; the cursor ON a boundary restates that change, repointing its region at another catalog tone or at a NEW one minted in its place, and anywhere inside a region splits it) | arrangement | grid slot | tone pick | Live (restate 2026-09-12; select + mint 2026-09-13; author at the cursor 2026-09-14) |
| `Ctrl+M` | `Ctrl+Shift+M` (`0x1511`) | section | song | measure downbeat | name | Live (`0x1402`, "Insert or Rename Section"; restored 2026-09-12; author at the cursor 2026-09-14) |
| `Ctrl+B` | `Ctrl+Shift+B` (`0x1512`) | tempo anchor (BPM; inserting pins the time the map already assigns to that beat, so it changes nothing audible until moved) | song | beat | none — `Alt+←/→` is its millisecond nudge | **RESERVED** for plan 41 |
| `Ctrl+/` | `Ctrl+Shift+/` (`0x1513`) | meter (the glyph in 4/4) | song | measure downbeat | numerator, denominator | **RESERVED** for plan 41 phase 6 |
| `Ctrl+H` | `Ctrl+Shift+H` (`0x1516`, plan 60 Phase 3) | hand marker — the fret-hand position AND the span are ONE object (plan 60, 60-H1, RULED 2026-09-15: a span is a hand holding a shape, so its position cannot change inside it) | arrangement, chart | grid slot | the template reference (plan 60 Phase 5) | **RESERVED** behind gate G60-RULINGS; the letter is RULED (plan 60's 60-H2, 2026-09-15: H, accepting the macOS `Cmd+H` = Hide cost; `Ctrl+P` / `Ctrl+Shift+P` returned to the pool, `0x1517` unassigned) |

`Ctrl+G` is grid snap and `Ctrl+S` is save, which is why span is not on G and section is not on
S. The quanta are the coarsest grid each kind can live on; "measure downbeat" means the measure
the cursor is IN, never the nearest one. Every chord is a default and rebinds like any other.

**The tick and the slot part in one case** (recorded with the build, 2026-09-14). `Tab` and the
vertical walk ask which marker HOLDS the cursor and read its exact TICK; a chord asks where
authoring would LAND and reads the placement-quantum slot an arrow press would arm on. The two
readings differ only while snap is ON and a click, drag or stop left the cursor off the placement
grid: with a quarter grid, a cursor at beat 2.9 and a tone region starting at beat 3, `Tab` selects
the earlier region while `Ctrl+T` retones the one at beat 3. With snap OFF the placement quantum IS
the tick, so the two coincide — as they do anywhere the editor itself parked the cursor.

## Song sections (the ruler's chip row)

Sections are song-level markers on the pinned ruler, and the chip is a fourth object kind the one
editor-wide selection can hold. One chord is the section's own; the rest are the selection verbs
already in the tables above, reaching a new alternative rather than gaining a chord. Every row below
is paused-only, the chip click included, under the marker-plane rule above (2026-09-14).

| Keybind / gesture | Behavior | Status |
|---|---|---|
| `Ctrl+M` | read the MEASURE the cursor is in: a free downbeat adds a section and prompts for its name; an occupied one reopens that section's rename prompt. A selected chip elsewhere changes nothing — `Enter` or a double-click renames the selection. The double press still works positionally: the first press inserts and selects, the second finds that section in the cursor's measure and renames it (`Ctrl+M` is the section's letter on the document plane, and `Ctrl+S` is save). The cursor is the armed caret, else the paused cursor, and the chord is inert while playing and with no song | Live (signed 2026-09-12; author at the cursor 2026-09-14, replacing the form where a SELECTED chip won, the armed caret alone decided, and an occupied downbeat merely SELECTED) |
| `Enter` | restate the selected marker, dispatching on its kind the way `Delete` does: a section's name through the prompt `Ctrl+M` reopens, a tone region's tone through the picker `Ctrl+T` reopens (existing tone or a new one). Its own command (`RestateSelection`, `0x1404`) rather than a second chord on either marker's, because `Enter` must never ADD one — with nothing selected the press is inert | Live |
| `Delete` | delete the selected section — the same `Delete` as everywhere, dispatching on the selection's kind | Live |
| `Alt+←/→` | move the selected section one **MEASURE**, not one grid step: a section starts on a downbeat and nowhere else, so a measure is its step. Refused, never clamped, onto a downbeat another section holds or outside the song. A landed move brings the paused cursor to the new downbeat, so the edit is in view (every selection move of a marker does; a pointer drag leaves the cursor, the edge being under the mouse already) | Live (cursor follows 2026-09-14) |
| `Alt+↑/↓` | *(nothing — a marker on one timeline row has no vertical axis)* | `—` unbound |
| **Click chip** | select it. Seeks nothing, which is what lets the selection survive the cursor-move rule that clears it (the tone region's lifecycle, shared) | Live |
| **Double-click chip** | rename prompt, the pointer form of `Enter` on a selected chip | Live |
| **Right-click ruler** | the section menu: add always, plus rename / move / delete over a chip, which the menu selects first. A chord alone is undiscoverable, which is why the menu exists. Every row is DISABLED while the transport plays (2026-09-14) — greyed, not hidden, so the menu still says what the row is | Live |

## Tempo and time-signature chips (the ruler's lower chip rows)

Selectable so the keyboard's vertical walk can stand on them (`keyboard-focus-rows.md`); they carry
no verbs until tempo-map authoring (plan 41) gives them some. Selecting one is paused-only, like
every marker kind (2026-09-14). A selected chip is always drawn: on a
dense map its neighbours give way instead, and as the row's pinned chip it never yields to the
chip scrolling in.

| Keybind / gesture | Behavior | Status |
|---|---|---|
| `↑` / `↓` | walk onto the chip holding the cursor, and off it again; the section row above the tempo row, the top string below the signature row | Live (2026-09-13; unsighted) |
| `Tab` / `Shift+Tab` | select the next / previous chip on the row and move the cursor to it, reading the CURSOR as `Tab` does on every row: `Tab` goes to the next start strictly after it, `Shift+Tab` to the previous start strictly before it — so from a cursor inside a marker past its start, `Shift+Tab` lands on that marker's own start first. One rule for all four marker rows (sections step the same way); a step past either end is inert; `Ctrl` changes nothing here | Live (2026-09-13; reads the cursor 2026-09-14, replacing the index step from the selected marker; unsighted) |
| **Click chip** | select it; seeks nothing, like a section chip | Live (2026-09-13; unsighted) |
| `Delete`, `Enter`, `Alt+←/→` | *(nothing yet — inert, silently)* | `—` until plan 41 |
| `Esc` | release the selection | Live |

## Pointer

| Gesture | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| **Click empty** | seek + arm caret at grid | seek + arm caret at grid | select region under cursor | Live |
| **`Ctrl`+click empty** | **nothing** — Ctrl is the membership modifier and an empty slot has no member to toggle, so the standing selection and marker survive a misclick (user ruled 2026-09-09; the off-grid arm it once meant was retired 2026-08-23) | arm caret **off-grid** | (own meaning) | Live · chart |
| **Click object** | select note + arm caret | select point + arm caret | select region | Live |
| **`Ctrl`+click object** | **toggle** membership | **toggle** membership (scheduled) `✚` | select (**no toggle**) | Live · `✚` lanes |
| **`Shift`+click** | time-range select (full-height span) | — same span — | — same span — | `▷52` |
| **Double-click object** | select **chord** | **property editor** | **rename / pick tone** | Live |
| **`Alt`+click** | **NOTHING BUT ARMING THE CARET AND SELECTING WHAT IS THERE** — the plain click's answer, under every modifier (re-ruled 2026-09-11: **a click never creates** on this lane, because every note is typed). The chart's authoring pointer gestures are retired with it; the `Alt` ring REVEAL, `Alt`+wheel and `Alt`+arrows are unaffected | insert on-curve point | **split** region | Live · **chart authoring retired 2026-09-11** |
| ~~**`Shift+Alt`+click**~~ | ~~the same point verb, its head at the fret in force~~ | ~~insert on-curve point~~ | ~~**split** region~~ | **Retired 2026-09-11** with the fret-in-force default and the pointer's authoring half |
| ~~**`Alt`+double-click**~~ | ~~a NOTE at the pointer~~ | ~~`✗`~~ | ~~`✗`~~ | **Retired 2026-09-11** — the press-count split it carried existed only because the pointer authored at all |
| **`Ctrl+Alt`+click** | insert **off-grid** | insert **off-grid** | split off-grid | **Retired 2026-08-23** — `Alt`+click inserts on the quantum, on the surfaces that still create at all (the chart's authoring half went 2026-09-11) |
| **Drag on object** | move note (scheduled) `✚` | move point | move boundary | Live (lanes/tone) · `✚` chart |
| **`Ctrl`+drag object** | move **off-grid** (scheduled) `✚` | move **off-grid** | move boundary off-grid | **Retired 2026-08-23** — a plain drag moves on the quantum |
| **Edge-drag extent** | `✗` — chart sustain is `Alt`+wheel | `—` (no extent) | resize region | Live (tone) · chart uses `Alt`+wheel |
| **Drag from empty (marquee)** | marquee select — plain **replaces** (the multi-object form of the plain click), `Shift` **extends**, `Ctrl` **toggles the boxed set as one unit** (the box form of `Ctrl`+click: all in → all out, else all in; user ruled 2026-09-09) | marquee (scheduled) `✚` | `✗` | Live chart · `✚` lanes |
| **`Alt`+drag from empty** | `✗` — the press-drag-release form of the retired `Alt`+click authoring, gone with it 2026-09-11 | insert + place point | split + drag boundary | Live · **chart retired 2026-09-11** |
| **`Alt` held (no gesture)** | reveal every visible note's **actual ring** — the lane redraws in the chart's actual form, so each ring is an ordinary tail | `✗` | `✗` | Live 2026-08-22 (chart only; mark signed 2026-08-23) |
| **`Alt`+wheel** | duration (sustain / span) | `✗` | `✗` | Live (chart only) |
| **`Ctrl+Alt`+wheel** | **fine** duration | `✗` | `✗` | **Retired 2026-08-23** — snap off + `Alt`+wheel |
| **`Shift+Alt`+wheel** | fret shift ±1 | `✗` | `✗` | Live (chart only) |
| **Right-click** | keybind-discovery menu (scheduled) `✚` | keybind-discovery menu | keybind-discovery menu | Live (lanes/tone) · `✚` chart |
| **Ruler drag** | create time selection → feeds loop region | — same span — | — same span — | `▷47` |

## Editor-wide (one behavior, surface-independent)

| Keybind | Behavior | Status |
|---|---|---|
| `Space` | play / pause from the marker | Live |
| `Ctrl+Z` / `Ctrl+Y` / `Ctrl+Shift+Z` | undo / redo (exact-modifier matched); `Ctrl+Shift+Z` = redo alias — **fully rebindable** with `Space` (fixed-trio decision reversed 2026-07-20; rebinds mirror into plugin windows via the generalized layout-neutral seam) | Live (registry + mirror sync 2026-07-20; manual plugin verification passed 2026-07-20) |
| `Ctrl+O` · `Ctrl+I` · `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+E` · `Ctrl+W` · `Ctrl+Q` | Open / Import Song / Save / Save As / Export Song / Close / Exit (the tier A file-menu chords; menu items show live shortcuts; `Ctrl+Q` added 2026-07-20) | Live (registry 2026-07-20; Publish renamed Export Song on `Ctrl+E` and Import moved to `Ctrl+I` 2026-09-15, `keyboard-focus-rows.md` step 4.0b — which freed `Ctrl+Shift+P`, at that point the fret-hand position row's planned jump; the one-hand-object ruling later the same day returned both `Ctrl+P` and `Ctrl+Shift+P` to the free pool) |
| `Ctrl+Shift+I` · `Ctrl+Shift+E` | Import Tone… / Export Tone… — the second claimants of `Ctrl+I`/`Ctrl+E`: the same verbs on the active tone, over plan 50's actions (import replaces the active tone's chain; export writes its rig to a file). No song Export As is planned; one would be menu-only | Live (step 4.0b, 2026-09-15; `ImportTone` 0x1008 / `ExportTone` 0x1009, category Tone, no menu items — the signal-chain header buttons are their surface) |
| `Ctrl+R` | rename the SELECTION where its kind has a name: a section (the same prompt as `Enter`), a tone region's TONE (the tone document's name, shared by every region that uses it); silently inert on everything else. A selection verb like `Enter`, not a marker chord — R is no marker's letter. Main window only, not in the 3D preview's whitelist | Live (`RenameSelection`, `0x1405`; ruled and built 2026-09-14 with the marker grammar, `keyboard-focus-rows.md` Phase 3; unsighted) |
| `Ctrl+Shift+M` · `Ctrl+Shift+B` · `Ctrl+Shift+/` · `Ctrl+Shift+H` · `Ctrl+Shift+T` · `Ctrl+Shift+A` | jump to the section / tempo / time-signature / hand (position + span, one row — plan 60) / tone / "+" row. Each marker kind's `Ctrl` pair is author / select: `Shift` is the letter's second claimant, as on every letter plane. A jump lands as the walk does (select the marker holding the cursor, caret demoted), does nothing with no holder, and the "+" jump only lands (`Enter` opens the picker). The time-signature pair keeps `/` (ruled 2026-09-14; per-language key bindings are the long-term fix): the `Ctrl+Shift+/` jump has no working default on macOS, and both `/` chords need rebinding where `/` needs Shift (`keyboard-focus-rows.md` Phase 4, D1) | Live for the five rows that exist (step 4a, 2026-09-15: `CaretJumpSectionRow` 0x1511, `CaretJumpTempoRow` 0x1512, `CaretJumpTimeSignatureRow` 0x1513, `CaretJumpToneRow` 0x1514, `CaretJumpAddLaneRow` 0x1515, category Navigation, main window only, unsighted). `Ctrl+Shift+H` (`0x1516`) waits for the ONE hand row — steps 4b and 4c were handed off to plan 60 Phase 3 on 2026-09-15, when the position and the span became one object, and `Ctrl+P` / `Ctrl+Shift+P` went back to the free pool with `0x1517` unassigned |
| `Ctrl+T` | the same marker grammar as `Ctrl+M`, on the tone's own grain, at the **cursor**: standing EXACTLY on a region's start, the picker reopens to repoint that region — at any other catalog tone (only its own is left out, since that would change nothing; a NEIGHBOUR's tone merges the two regions, because a boundary with no change across it is no boundary) or at a **new tone** minted on the spot, which is always offered so the restate never dies silently; anywhere inside a region, it splits it into a new one (choosing the next region's tone there pulls that tone back to the cursor). A selected region elsewhere changes nothing; `Enter` repoints the selection and `Ctrl+R` renames its tone | Live (guard against `Alt` 2026-07-20; marker-rule anchor + "at Cursor" name 2026-07-21; restate 2026-09-12; select-at-boundary 2026-09-13; merge instead of refuse 2026-09-13; author at the cursor 2026-09-14, replacing the form where a SELECTED region won, the caret alone decided, and a caret on a start merely SELECTED it) |
| `Ctrl+M` | add a **song section** at the MEASURE the cursor is in, snapped to that measure's downbeat, which is the only place a section can start; a prompt takes the name, carrying the downbeat captured at the press. Where a section already stands there, RESTATE it: the rename prompt. The selection is not read, and the cursor is the armed caret, else the paused cursor, read from the TICK so a cursor paused just before a barline still names the measure it is IN | Live (`0x1402`, "Insert or Rename Section"). **Signed 2026-09-12** under the marker grammar; held by `Shift`+`Insert` for one day before that; press-time capture 2026-09-13; author at the cursor 2026-09-14, replacing the form where a selected section won, the armed caret alone decided, and an occupied downbeat merely SELECTED |
| `Ctrl+B` · `Ctrl+/` · `Ctrl+H` | tempo anchor · meter · hand marker (position and span, one object since 2026-09-15), each inserted at the cursor, or restated where one already stands there — see *Markers* | **RESERVED** (plan 41; plan 41 phase 6; plan 60 gate G60-RULINGS) |
| `Alt+F` · `Alt+E` · `Alt+V` | open the File / Edit / View menu — the platform's own menu-access convention, implemented here because JUCE's menu bar has no mnemonic handling of its own. One command per menu-bar title, in the bar's order; registering them also stops the system beep an unhandled `Alt`+letter makes on Windows. `Alt` alone is still the ring reveal, so the reveal flashes for the chord's length, as it does under `Alt`+digit. Holding `Alt` underlines the access letter in each menu title, the platform's own hint, hidden until `Alt` is down | Live (`0x1B01`-`0x1B03`, Menu; 2026-09-12) |
| `Esc` | cancel gesture → disarm caret → clear selection | Live |
| `F3` / `F5` / `F8` | toggle 3D preview / waveform / undo-history inspector | Live (`F5` added 2026-07-21) |
| `?` (`Shift+/`) | open the Actions dialog (the binding editor; REAPER's actions-list key) | Live (renamed from "Keyboard Shortcuts" + default added 2026-07-20; display collapses shifted chords through the shared `keyChordText` formatter) |
| plain wheel | zoom, marker-centered | Live |
| `Ctrl`+wheel | zoom (browser reflex — same as plain wheel) | Live |
| `+` / `-` (main-row or numpad — numpad arrives as the same character key codes) · `=` / `_` convenience aliases | **grid** finer (`+`) / coarser (`-`) | Live (chord sets corrected 2026-07-21: `numberPad*` chords never matched on Windows and were removed; display-equal shapes group into one chip; `=`/`_` aliases kept until something better claims them) |
| `Ctrl` + the same `+`/`-` family (incl. the `Ctrl+_` alias) | **zoom** in / out, marker-centered | Live (44f24ab6; chord sets corrected 2026-07-21) |
| `Ctrl+G` | toggle **grid snap** — the session switch that picks the placement quantum (grid note value on, 1/3840-whole-note tick off). Session-only, never persisted, back ON at every project boundary. Indicated by the quieted grid dots and the struck-through grid readout value; forwarded to the 3D preview window, because with snap off it is what the caret's arrows travel by | Live (`0x1905`, Grid & Zoom; exact-modifier matched so plain `G` stays the ghost-note technique) |
| `[` / `]` | **free** — grid moved to `+/-` | `—` |
| `L` | claimed by **legato** since the 2026-08-12 technique-letter amendment — the link/slide reservation fulfilled (see the technique verb table; `Shift+L` there carries the same verb extended with travel, its keyframe-disconnect clause live since 2026-08-26) | Live |
| `B` | reserved for **bend** (plan 40 Phase 7), no longer for the pencil — unbound (user 2026-08-07: "B for bend makes more sense than B for pencil"; `Alt` already *is* the held pencil quasimode for the pointer, so the pencil needed no letter) | `—` |
| `W` | reserved for the **whammy bar** (future feature; user 2026-08-13) — unbound. **The `Shift+V` alternative is CLOSED 2026-08-28**: wide vibrato went live on that chord, so whammy keeps `W` outright and the official call the whammy work was waiting for is made by the chord no longer being free. The mnemonic the user liked is the one that survives | `—` |

## Technique verbs — the typed family (plan 40 Phase 5)

Technique toggles are **typed input**, so they take the typing family's gate rather than `Alt`: they
act on the selection (or the armed marker) and no-op when there is none, exactly as the digits that
retype a fret already do. That is the interaction model's law — *plain input never mutates; every
mutation passes a gate*, applied per input family — not a new rule. So these are **plain letters**.

**The letter map (amended 2026-08-12, user-signed).** One rule instead of per-key Guitar Pro
compatibility: **the plain letter is the first letter of our own verb's name** — `L` legato,
`T` tap (mirroring the plates' one-letter-two-polarities hand signature), `H` natural harmonic,
`M` palm mute, `V` vibrato — with those same letters' second slots taken by `Shift+L`
link-with-travel, `Shift+T` the left-hand tap, `Shift+H` the pinch harmonic and `Shift+V` the wide
vibrato. `Ctrl`+letter stays out of the typed family entirely: it is the app-command plane
(`Ctrl+S/O/W/Q/T`, the reserved `Ctrl+D`).
Guitar Pro familiarity is weighed, not binding: where GP's key matches our *semantics* it survives —
`L` is GP's "Tie note", the destination-stored backward link, exactly this model's claim shape, and
`V`/`B`/`A` line up too — and where it conflicts it is dropped: GP's `H` links the selected note
FORWARD to the next, so an H habit here authored a silent off-by-one link, the worst of both
worlds. The freed `H` now means the loudest first-letter mnemonic in the map instead (Harmonic).

**The `Shift` plane — stated once (signed 2026-08-25).** The LETTER is the index; `Shift` is that
letter's second slot. `Shift` is not a semantic operator in this map — it is a disambiguator: the
letter carries all the meaning, and `Shift` says only which claimant of that letter you mean, with
the plain key going to the meaning a charter reaches for first. That is why a sibling (`Shift+H`)
and a collision (`Shift+X`) share the plane without sharing a kind — and never needed to. (The
arpeggio hold briefly held `Shift+A` as a third kind of claimant on 2026-08-25 before moving to
plain `N` the same day; the rule needed no third example to absorb it, which is the rule working.) This **supersedes the separate sibling reading (2026-08-12) and
collision reading (2026-08-18) as their superset**; both stay true as instances, so the rows below
keep their own local reasoning (why this key for this verb) and no longer restate what the plane
means. Corpus check behind the plain-key half: the plain key holds the letter's more-reached-for
meaning in five of the map's six letter pairs, with `Shift+L` inverted — a ranking question carried
into W10's build, not a rebind today.

**Settled 2026-08-07, amended 2026-08-12:**

| Keybind | Verb | Status |
|---|---|---|
| `L` | **legato** — one verb for hammer-on and pull-off, because **no direction is stored**: the note claims a connection to its same-string predecessor and which way that runs is read back by `resolveLegato` (amended 2026-08-11, `legato-final-spec.md`). Three things gate the claim beyond the frets: the judged fret is the predecessor's **released** one (its last keyframe, or a scrape's slide-out end), the predecessor must still be **ringing** at this onset (strict adjacency against its stored ring, 2026-08-22), and a fret-hand-harmonic predecessor is disqualified outright. Then the released fret picks the motion — higher = pull, lower = hammer — and a claim nothing justifies is refused rather than guessed, plays as the pick it sounds like where it already stands, and is flattened at the next settle. When the hold is the only thing missing, the verb authors the connection itself (the D14 assist): the plan grows the predecessor's ring to the successor's onset in the same undo entry — which is exactly the furthest a manual drag could take it — and skips a gesture-carrying predecessor (a scrape or a slide-out) whose tail is authored geometry. While the selection and history still prove the previous press was this verb's own, a second press reverses it exactly — grown tails included — leaving no trace (ruling 4's true toggle), and when a save between the presses made that entry the file's clean state the reversal pushes the exact inverse as a new entry instead of erasing it, so the tail still returns while "return to clean" stays truthful. Otherwise the press means apply-or-clear — **applying wins whenever it changes anything**, and the clear flattens the stored claims only, so a left-hand tap riding the selection keeps its attack — and a press that only skipped reports the count and the dominant reason | **Live** (`planSetLegato` + `ChartLegatoToggle`, labelled "Toggle Legato", default chord plain `L` since 2026-08-12, shipped on `H` 2026-08-07 to 2026-08-12 — verified in `editor_command_registry.cpp`). **Correction 2026-08-12:** the old note here claimed GP binds `H` "the same way" — the chord matched but the direction did not. GP's `H` is origin-stored and links the selected note forward; this claim is destination-stored and reaches backward, which is the shape of GP's "Tie note" (`L`) — the reason for the move |
| `Shift+L` | **split or join at every selected junction** — one verb, two directions, because a junction has exactly two states and the press moves each selected one to the other. A selected KEYFRAME becomes a head (the split): the note's path ends there, a new head takes the remainder, and the origin's arrival retreats one margin before it. A selected HEAD becomes a point on its same-string predecessor's path (the join): the two rings lie end to end, the head's own keyframes ride along rebased onto the predecessor's onset, and everything a STRIKE states — attack, mutes, node, tremolo, emphasis, held stop — goes with the head, because the join is precisely the statement that no strike happens there. Both halves run in ONE press and one undo entry. The join is written as the split's exact INVERSE rather than as a second law, so split-then-join restores the chart byte for byte: the arrival the split retreated off the new head RETURNS to the junction, asked of the same clearance authority backward. **The tie falls out of the commit law rather than being built** — on an equal-fret junction the point says nothing the path does not already say, so the history entry and the document writer both shed it, and what is recorded is one longer ring with one note fewer; the tie never enters the format. Different frets leave an ordinary slide keyframe at the junction, which is the connecting slide W10 asked for. Refused whole and never clamped: a head with no predecessor on its string, a scrape or fret-hand-harmonic predecessor, a predecessor whose tail already falls away (a trail-off's tail is authored geometry, not slack to spend — the D14 assist's own rule), and a scraping or node-carrying head. The SELECTION carries the toggle, so no verb window is armed and none is needed: each press leaves exactly what it made selected — a split's new heads, a join's new point — so pressing again reverses it, with no window to expire. GP's own `Shift+L` ("tie the beat") is subsumed by the uniform-scope law, so the slot is vacated by our design, not stolen. W10 also inherits the map's one inverted pair: on `L` it is the `Shift` meaning a charter reaches for more often (the 5-of-6 corpus check above), so W10 rules whether these two slots stay this way round. Full design: `technique-review-walkthrough.md` W10. **This chord is also the SPLIT's one home** (2026-09-11) | **Live 2026-09-12** (`0x1713`, "Split or Join at Selection", Authoring): `planToggleJunctions` + `ChartJunctionToggle`. The KEYFRAME clause has been live since **2026-08-26** (W10's 2026-08-26 addendum, then spelled `planDisconnectKeyframes` / "Disconnect Keyframe"); the JOIN clause, the one-press both-halves verb and the rename landed 2026-09-12 — the command id kept its VALUE across it, because the value is the contract a saved keymap stores and the name is not. **THE LOSSLESS SPLIT, in two keystrokes** (2026-09-11): a digit plants the point where the charter wants the division, `Shift+L` splits it there — the point becomes the new head, the original note ends exactly on it, everything after it rides the new note, a slide-out included, and the first note's arrival retreats one margin before the new head. The segment walk is this verb's alone — its only caller, now that no entry gesture splits — so there is one rule and one place it lives. A point that merely restates the fret the path is already running on is silent authoring state and dissolves with focus, so typing the same fret on a tail and stopping there leaves nothing behind; the split is the two keystrokes together. The split product's unstruck-tie default is still a PROPOSAL, so the split head stores the plain legato claim W10 signed |
| `T` | **tap** — the right-hand tap attack; the dark-T plate's letter | **Live 2026-08-28** (`ChartTapToggle`, labelled "Toggle Right-Hand Tap", verb `planSetAttack` through `chartTechniqueLaw`), taking up the reservation held since 2026-08-12. One of four rows on the ATTACK field — with the scrape, the slap and the pop — so all four are one shared row shape handed their own value: each toggles its own attack against the plain pick, and a press over a scope carrying another attack REPLACES it in one entry, exactly as the `Shift+V` pair does on the width axis. Validity stays the one rule authority's: `planSetAttack`'s written-form gate skips the open string with no node (E4's boundary, the same skip `Shift+T` has always had), and drops a scrape's path and terminal when a note converts away from it. The label says "Right-Hand" because the menu and the undo history show it beside `Shift+T`'s "Left-Hand Tap"; the plates already name the pair by hand rather than by letter, so the words do too |
| `Shift+T` | **left-hand tap** — the **sole author of the left-hand tap**, the one statement no predecessor can justify or withdraw, valid across E4's domain (positive sounding position) and able to override a standing connection claim. The chord mirrors the notation's own family structure — the charting marks give both taps ONE letter with plate fill polarity as the hand signature, so the keymap does the same: plain `T` for the right hand, `Shift+T` for the left (moved off `Ctrl+H` 2026-08-12; `Ctrl` is the app-command plane). There is deliberately no second stating verb, a pull-off to a higher fret being physically impossible. Plain `L` can never produce this attack and its clear never destroys one — which is what makes the claim/statement split policeable rather than a convention. Under the derived-direction model the statement is its own stored value (`LeftTap`), so no neighbour edit and no settle sweep can withdraw it | **Live** (`ChartLeftTap`, labelled "Left-Hand Tap" since 2026-08-11 with its command id value retained, default chord `Shift+T` since 2026-08-12, `Ctrl+H` before — verified in `editor_command_registry.cpp`). The verb is `planSetAttack(LeftTap)`: its written-form validity check yields the ruled domain from the one rule authority — the no-node open string is the sole skip, an open-string pinch's bridge-side graze refuses to re-hand, and a tap harmonic's strike point carries into E13's form. Since 2026-08-12 the stored statement wears its own charting mark in the 2D lane: the tap letter on the LIGHT plate - fill polarity is the plate family's hand signature (55-Q1's corrected basis), so the right-hand tap's dark T and this light T share a letter without colliding. Editor-only by the charting-mark law; the 3D surfaces stay merged |
| `Shift+X` | **pick-slide toggle** — converts the selection to or from the scrape attack | **Live but UNBOUND** (`planSetAttack` + `ChartPickSlideToggle`), deliberately registered with **no default chord**: the verb shipped with plan 55 while the signed keymap never assigned it one, and inventing one in the registry would be an unsigned keymap decision. Reachable through the chart's context menu (the Actions dialog binds chords, it does not invoke — the walkthrough's W9-I states this correctly), and the user can bind it a chord there. **SIGNED `Shift+X` 2026-08-18 (W9-I closed)**, and bound the same day. Both natural
first letters were taken by plate letters that outrank a name letter — `P` is pop's plate, `S`
is slap's — so the scrape takes the `X` family instead: `X` is standard tab's dead-note glyph and
is becoming the full mute's own letter (pending #38's two-flag ruling), and a full mute and a
scrape are both UNPITCHED NOISE, which makes `Shift+X` a real kinship rather than a bare letter
collision. Either way the chord is X's second claimant (the plane statement above); `Shift+V`,
then contested between wide vibrato and the whammy bar, was the standing precedent — and it
resolved to the wide vibrato 2026-08-28, so the precedent it sets is now a live one |
| `V` | **vibrato** (the ordinary, narrow tier) | **Live 2026-08-19** (`ChartVibratoToggle`, verb `planSetVibrato`). Reserved for the wide tier since 2026-08-12 (superseding the earlier `Alt+V` float), and **that reservation went LIVE 2026-08-28** on the `Shift+V` row below, once the format carried the width the distinction needed ([D8], `chart-ruleset.md`) |
| `Shift+V` | **wide vibrato** — the deliberately exaggerated tier of the same width axis | **SIGNED and Live 2026-08-28** (`ChartWideVibratoToggle`, verb `planSetVibrato`), taking up the reservation `V` had held since 2026-08-12 and closing the `W` row's recorded whammy alternative in the same ruling. Each key toggles its OWN tier and REPLACES the other: `V` on a wide scope makes it narrow, `Shift+V` on a narrow scope makes it wide, one entry either way, and a second press of the key naming the scope's own tier clears it. No cycling verb — a cycle has no "already carries it" answer to give the uniform-scope law. **This settles the Shift-plane-vs-cycling question in the Shift plane's favour, and it is a precedent, not a rule**: the `Shift` plane is for a magnitude variant of a plain key's own technique where the variant is a distinct VALUE the format carries, exactly as `Shift+A`'s heavy-accent reservation reads. The `A` row's own cycling plan below is unaffected — the emphasis axis has two poles on two plain letters and no free second slot to claim — but it is now the exception the `Shift` plane is measured against rather than the default |
| `A` | **accent** | settled, conditional on `A` not being wanted elsewhere. Checked 2026-08-07: plain `A` and `;` are both unassigned everywhere in this matrix, the interaction model and the registry; "select all" would be `Ctrl+A`, which is a different chord. **Live 2026-08-18** (`ChartAccentToggle`, verb `planSetEmphasis`). CORRECTED 2026-08-25: the old note's claim that "the arpeggio reading is derived rather than authored so it needs no key" lost its premise — a silently-held shape member is underivable (identical notes carry both hand intents), so arpeggio membership IS authored and took `Shift+A` below. If a heavy accent ever lands, its plan of record is plain `A` cycling the emphasis axis (normal → accent → heavy) rather than a chord: a magnitude is a step on the axis, not a separate verb needing a chord, and `isAccented` already reads "every emphasis above normal". **Noted 2026-08-28**: the wide-vibrato signing put the opposite shape live one row up (`Shift+V` toggling its own tier rather than `V` cycling three states), so this plan is now the deliberate exception — it stands because the emphasis axis's two poles already own two plain letters, leaving no second slot for a third tier to claim, which is not true of vibrato |
| `G` | **ghost note** — the quiet end of the emphasis axis | **SIGNED and Live 2026-08-18** (user: *"Ghost should be G not Shift+A"*, `ChartGhostToggle`). Two PLAIN letters rather than a `Shift` pair, for a reason worth stating because it looks like a sibling and is not: the ghost and the accent are opposite POLES of one three-valued field, and each owns its own first letter, so neither has to claim the other's second slot. That is the same conclusion the two mutes reached on `M`/`X`, by a different route — they are independent flags, these are opposite poles, and in both cases two letters were free |
| `N` | **UNBOUND and free for reuse** (2026-09-17). The letter carried the arpeggio hold verb from 2026-08-25 to 2026-09-17, when that verb and the silent-hold record it wrote (`NoteAttack::None`) were removed from the model entirely. A claim now reads through ONE query (`claimedStop`) — the `held` stop under a plain tap or a pick slide, authored by clicking or arrow-stepping onto its drawn satellite and typing a fret, and cleared with Delete; a tapped harmonic's claim is the `fret` it presses, whose satellite is read-only (2026-09-17) — and stating a fretting-hand stop on a string where nothing sounds waits on plan 60's span templates. Historical design record: `docs/plans/todo/arpeggio-authoring.md` | RETIRED 2026-09-17. The chord is left empty: `N` is simply unbound and available for reuse |
| `Shift+A` | **heavy accent** — RESERVED again, unbuilt and possibly never built | reservation RESTORED 2026-08-25 when the arpeggio hold moved to `N` (its brief same-day tenancy here is recorded in the `N` row). Both futures stay recorded: the original chord reservation (user 2026-08-18: *"Shift+A may someday be HEAVY accent"*), and the alternative plan of record noted on the `A` row — plain `A` cycling the emphasis axis — with the choice deferred until a heavy accent is actually wanted; the format's fourth emphasis value is the real gate either way |
| ~~`Shift+A`~~ | ~~(historical row)~~ | the arpeggio hold's brief 2026-08-25 tenancy, ended the same day when the verb moved to plain `N` — the LIVE `Shift+A` row above restores the heavy-accent reservation. Original reservation reasoning kept here: reserved 2026-08-18 (user: *"Shift+A may someday be HEAVY accent"*). This is the `Shift` plane used exactly as intended: a magnitude variant of the plain key's own technique, the same shape as `Shift+V`'s wide vibrato, and it is why the ghost could not have this chord. Like wide vibrato it would need the format to carry the extra value — `NoteEmphasis` has three today, and `isAccented` is already written as "every emphasis above normal" so a fourth would light up every consumer without a hunt |
| `H` | **fret-hand harmonic — NOT a toggle** (re-ruled 2026-09-16; freed by legato's move, and still the strongest first-letter mnemonic in the map, GP's `Y` being legacy). A GP `H` habit authors a loud, visible, undoable wrong mark instead of the silent off-by-one link it authored before — an improvement even for the habit it breaks. **THE VERB OFFERS EVERY CHANGE THE SELECTION ALLOWS, AND ASKS ONLY WHERE THERE IS MORE THAN ONE.** **THE FRET YOU TYPE IS THE NODE**: the finger stands where it would otherwise have pressed, so the fret-stating flow already states it — type 12, press `H`; the decimals are physics, not notation a charter chooses. **WHICH ROWS CHANGE ANYTHING IS THE PLANNER'S ANSWER**, never a count kept beside it: the verb plans each node row (`planSetHarmonic`) and the clear (`planClearHarmonic`) over the live chart, and a `NoChange` plan is not a change. The node rows are those of the member whose label names the MOST nodes — a carrier's label being the fret its node lies at (`harmonicLabelFret`, `chart_edits.cpp`, which is also what the clear presses back down), so a note touching 4.98 is offered the 13th and 15th partials of a 5 — and EVERY one of them is shown, a ticked row that changes nothing included; the **"No harmonic"** row LEADS them, ruled off by a separator, only where the clear itself changes something. ONE change applies in the keystroke with no menu (a typed 12 writes its single node; a 12 already touching that node clears it); SEVERAL open the picker on the row below. A label naming nothing — an open string, a pinch — is inert. Restating a node is one press and one row | **Live** (`EditorCommandId::ChartHarmonic`, value `0x1718`, labelled "Harmonic..." — was `ChartHarmonicToggle` / "Toggle Harmonic" before the 2026-09-16 ruling, the key unchanged). `H` raises `EditorAction::ChooseChartHarmonic` → `IEditorController::onChartHarmonicRequested()` → `commitChartHarmonic`; verbs `planSetHarmonic` / `planClearHarmonic`. **THE TYPED FRET IS THE TOUCH ON THE OPEN STRING** (ruled 2026-09-17): the set writes `fret = 0` — clearing any planted `held` with it, since a note carrying a node has no planted finger — and measures the node from the stop the open string speaks from, the nut or the capo, so one formula covers every note: fret 5 gives node 4.98 and absolute fret 7 under a capo at 2 gives 6.98. On a `Tap` it therefore authors an OPEN-STRING tapped harmonic; a harmonic over a PRESSED stop, artificial or tapped, belongs to the separate unbuilt verb below. `normalizeChartNote` then strips what a touch cannot carry. A fret naming no node is SKIPPED, never repaired: an open string states no position at all, and under the whole bound (item 2 below) 1, 11 and 13 are no longer the dead keys the snapping cap made them. The clear is the FRET HAND's alone — `planClearHarmonic` writes only notes that `carriesNeckHarmonic` (a node whose attack keeps it on the neck, a pinch excluded), the picking thumb's node being `planClearPinchHarmonic`'s since 2026-09-16 — and it inverts the set exactly — the finger presses where it was touching — which reaches a tap harmonic and an imported artificial too, where it leaves the note plain at the stop it was already pressed at, the only way the editor can un-harmonic either. It is reached by the "No harmonic" row where several changes are on offer, and by the press itself where the clear is the ONLY change, so a bare second `H` still clears a one-node carrier. **ALL FOUR PROVISIONAL ITEMS RULED 2026-09-15:** (1) **the RANGE RULE is the label window**, not the ceil law — a typed fret names every node within `g_max_node_label_error` (0.5) of it, which is what makes 7 and 19 reachable, the commonest harmonics on the instrument; (2) **the candidate list is the WHOLE validation bound**, `common::core::g_max_harmonic_partial` (16, the partial form of `g_max_harmonic_node`), not import's snapping cap `g_max_snapped_partial` (8), which stays where it is because raising it would need the corpus re-measured — so most labels name several nodes: a typed 1 names FOUR (the 13th through 16th partials), and 11 and 13 name two each, which is what stops all three being the dead keys the snapping cap made them; (3) **the list is ordered by PARTIAL, lowest first, and that order is a CONTRACT** — the lowest partial is the harmonic a charter means by the label and the loudest the string gives, so it is the row the picker PRESELECTS — amended 2026-09-16: unless every selected note already carries a fret-hand harmonic, when the preselected row is "No harmonic" instead, and otherwise the lowest partial that CHANGES something — and import's `nearestHarmonicNode` is deliberately not used here (under this bound the node nearest a typed 3 is the 13th partial's 2.892, 0.108 away, while the harmonic a charter means by 3 is the 6th's 3.156, 0.156 away); (4) **the default chords themselves**, `H` and `Shift+H`. **SCOPE:** this verb and `Shift+H` author the natural and pinch harmonics only — the artificial and tap families were carved out the same day into `docs/plans/todo/artificial-harmonic-authoring.md`, because a node measured from a PRESSED stop needs a verb that does not rewrite the fret. |
| `H` (where the selection offers more than one change) | **the harmonic node picker** — the one press whose meaning the key alone cannot settle. Under the whole bound most labels name several nodes — a typed 5 names the 4th partial's 4.98, the 13th's 4.54 and the 15th's 5.37; a typed 3 names the 6th, 7th, 11th and 13th — and a selection where ANY member carries a FRET-HAND harmonic offers the **"No harmonic"** row on top of whatever else it names, so the press ASKS instead of guessing. What counts as a change is the PLANNER's answer throughout: each node row and the clear are planned over the live chart, and a `NoChange` plan is not one. A selection offering exactly ONE change never sees a menu and applies in the same keystroke: a label naming a single node (7, 12, 19, 24), or a note already touching the only node its label names, whose one remaining change is the clear | **Live 2026-09-15, re-ruled 2026-09-16**, as a `juce::PopupMenu` — the tone picker's own idiom — anchored at the tab lane head of the note the rows were READ from (`TabView::noteHeadBounds(picker.note)`), which need not be the earliest selected one. `H` reaches the controller as `ChooseChartHarmonic`, and the CONTROLLER asks for the rows through the view port (`IEditorView::showChartHarmonicNodePicker`, served by `EditorView::showChartHarmonicNodePicker`; payload `ChartHarmonicNodePicker{note, choices, preselected}`, `choices` a `std::vector<ChartHarmonicChoice>` — a variant of `ChartHarmonicNodeChoice{node, partial, current}` and `ChartHarmonicClearChoice`, the clear row FIRST when offered and the node rows ascending by partial after it — and `preselected` an index into that list, so a clear row that was never offered cannot be preselected or chosen) — below its settle prologue, and returning with nothing written. Opening it ends NOTHING another verb staged, because **a menu is a question, not an edit**; the CHOICE's write does, through `applyChartEditPlan`'s disarm. NOT on the pending-entry machinery it was first built over, and NOT on a fork in the view, which sat upstream of both. Rows read `<node> · <ordinal> partial` (`4.98 · 4th partial`): the value through the one label authority `harmonicNodeText`, so a row and the head it will produce print the same number, and the ORDINAL beside it because our frets are absolute where published tab is capo-relative. **RULED 2026-09-17: "No harmonic" comes FIRST wherever it is offered, on every selection alike** — it sat last, and where it opens preselected (a note already carrying a harmonic) `Up` from it landed on the highest partial, the least likely next choice; leading the list, the row never MOVES between states and `Down` from it walks the partials from the lowest. The node rows follow it, lowest partial first, and the VIEW rules the clear off from them on whichever side they lie, so the order stays the controller's. The rows are NUMBERED from 1 so a row can open **PRESELECTED** — JUCE matches `withInitiallySelectedItem` against item IDs, so an unnumbered row could never be. **The node the ANCHOR member — the note the rows were read from, whose head the menu sits on — is touching is TICKED, and the PRESELECTED row is what a toggle would have done**: "No harmonic" when every selected note carries a fret-hand harmonic (`carriesNeckHarmonic`), else the lowest partial that CHANGES something — so `H` `Return` still clears a harmonic and still sets the lowest partial on a plain note, keeping the old toggle's two-keystroke common case. `Esc` dismisses with the note untouched, because nothing was committed to reverse. A chosen row runs `onChartHarmonicNodeRequested(std::optional<int>)` → `SetChartHarmonicNode{partial}` → `planSetHarmonic` with that partial, or `planClearHarmonic` where the answer is ABSENT ("No harmonic"): one plan, ONE undo entry, folded into the run on the row below. One picker per press over a whole chord — the rows come from the member whose own label names the MOST nodes, a chosen partial binds every member whose label offers it, and the rest take their default. The right-click Note submenu's "Harmonic..." row runs the SAME verb the key does, so it reaches the picker down the same path and the menu lists no harmonic rows of its own; the lane's ~26x16 px labels remain a display, never a target |
| `H` (again) | **opens the picker again, or applies the one change — and the run FOLDS.** There is NO second-`H` reversal any more (retired 2026-09-16 with the toggle): the verb joined the GESTURE family, so consecutive choices on one selection REPLACE a single undo entry rather than stacking, and a choice back to the state the run started at RETIRES that entry — which is why `H` `Return` `H` `Return` leaves no trace of a carrier the VERB itself produced, by the fold's retire rule rather than by a reversal. An IMPORTED carrier whose payload (a bend, a shake) the set normalized away is a different case: the round trip really did strip it, so the entry describing that strip stays — a real edit, not a hole in the fold. The run ends at the family's own commit points: a selection change, a caret move, another verb's edit, undo/redo, a save, a committing settle. What the fold cannot give back is a node the LABEL CANNOT NAME — an imported artificial 17.0 on a fret 5 — and restoring that is `Ctrl+Z` only, which is the honest answer once "on" is multi-valued | **Live 2026-09-16** (`commitChartGestureStep`, the shared gesture authority; a new EMPTY `ChartHarmonicGesture` alternative in `ChartVerbWindowVerb`, now FOUR: `ChartTechniqueToggle`, `ChartHarmonicGesture`, `ChartSustainGesture`, `ChartMoveGesture`). Its "gesture" is the LATEST CHOICE rather than a step list, which is why the alternative carries no payload; three verbs run the fold shape now. Nothing PENDS: the picker commits on the row chosen rather than arming a provisional value, so the settle prologue's one exemption is a DIGIT continuing a live fret entry and nothing else, and the picker/toggle-window exclusion the pending build needed is gone with it. **Why the toggle went**: with a multi-valued "on", "restore what the last press removed" and "set" diverge, and an invisible window picking the restore made `H` after a clear behave differently from `H` on any other plain note |
| `Shift+H` | **pinch harmonic** — the fret-hand harmonic's sibling, the same technique reached by the OTHER hand, which is what the `Shift` plane is for here (as `Shift+T` is for the taps) rather than a magnitude variant | **Live** (`ChartPinchHarmonicToggle`, labelled "Toggle Pinch Harmonic"). The SET is `planSetAttack(Pinch)` unchanged — it already re-asks a node whose owning hand flips and authors the octave at the physical stop when none exists — and it asks for no value, because **neither surface draws a pinch's node VALUE** (`tab_paint_core.cpp`, roadmap 25-Q5) and the charter could not see one. Both surfaces do say it IS a harmonic: RULED 2026-09-17, 2D draws a pinch as the **diamond head with its bar in front, printing its fret**, and 3D wears the pinch-harmonic cell — the shape reads `common::core::isHarmonic` while the head text keeps reading `soundingStopAt`. The CLEAR is a harmonic clear, NOT `planSetAttack(Pick)`: the row's noun is a harmonic, so its clear must remove one, where the raw attack row would leave `Pick + fret 5 + node 17` — an artificial harmonic nobody authored. **Amended 2026-09-16**: with `H` out of `chartTechniqueLaw`, `Shift+H` is the toggle family's ONE harmonic row — the two are no longer one family's two rows — and the clear is no longer SHARED either: this row runs its own `planClearPinchHarmonic` (the thumb's node, back to a plain pick) while `planClearHarmonic` writes only notes that `carriesNeckHarmonic`, so neither verb can strip the other hand's harmonic and a pinch selected beside a fret-hand carrier is left untouched by `H`. The pinch is expected to carry a node/partial of its own eventually and to adopt `H`'s "offer every change" law when it does; recorded as direction, not built |
| `M` | **palm mute** — the picking hand damping at the bridge; pitched but damped. Moving the mutes to `M` is what keeps `P` free for pop | **Live 2026-08-18** (`ChartPalmMuteToggle`, verb `planSetMute`) |
| `X` | **dead note** — the fretting hand's full mute: unpitched, percussive. AMENDED 2026-08-18, off `Shift+M`: the two mutes stopped being siblings when the user ruled they may be set INDEPENDENTLY on one note (a dead string inside a palm-muted chord), and nothing forced them to share one letter's two slots. Two independent properties take two plain letters, and `X` is the strongest mnemonic available — standard tab writes a dead note as an X, which is also the glyph our own lane draws. The field is named `dead` in the format for the same reason | **Live 2026-08-18** (`ChartDeadNoteToggle`, verb `planSetMute`) |
| `S` | **slap** — the S plate's letter (GP's `S` is its legato slide; slides live on `Shift+L` here, so no collision) | **Live 2026-08-28** (`ChartSlapToggle`, labelled "Toggle Slap"), taking up the reservation held since 2026-08-12. An attack row like `T` above and the scrape, under the shared row shape and the same replace-in-one-entry law. Nothing here needs a strike to land on, unlike the tap: a slap strikes the string itself, so the open string it refuses for `T` is an ordinary slap |
| `P` | **pop** — the P plate's letter | **Live 2026-08-28** (`ChartPopToggle`, labelled "Toggle Pop"), taking up the reservation held since 2026-08-12, and the fourth row of the attack family. The two plate letters going live is what retroactively pays for `Shift+X`: the scrape took the X family in 2026-08-18 precisely because a plate letter outranks a name letter, and `S` and `P` now hold the letters that reasoning reserved |

**Open — do not bind before discussing:**

- **The mutes take two plain letters, not a sibling pair.** AMENDED 2026-08-18: `M` palm, `X` dead
  (was `M` / `Shift+M`). The user's ruling that a note may carry both mutes at once makes them
  independent properties rather than two values of one, and each has its own free letter — neither
  had to sit in the other's second slot. **Both verbs shipped 2026-08-18 on those chords**, through
  one `planSetMute` taking a field selector; the emphasis pair (`A` / `G`) landed the same day and
  reached the same two-plain-letters answer by a different route — two different shapes,
  independent flags and opposite poles of one axis, both answered by two plain letters.
  `Shift+X` (the pick slide, below) is unaffected — it is X's second claimant rather than the dead
  note's sibling, which the plane statement above covers.

- ~~**A chord for the pick-slide toggle.**~~ **CLOSED 2026-08-18 at `Shift+X`** (the row above
  carries the reasoning). Nobody should read `Shift+X` as a claim that a scrape is a kind of full
  mute — the 2026-08-25 plane statement above says why in general: `Shift` names a claimant of the
  letter, not a kind.
- **`;` as an alias for accent.** The only argument for it is familiarity to Guitar Pro users, and
  that premise is unverified: the search that suggested it also claimed `[` was Guitar Pro's palm
  mute, which is wrong (`[` starts a repeat section there). Verify Guitar Pro's real accent key from
  a reliable source before adding the alias, since without the familiarity argument the alias has no
  purpose. Also weigh that a punctuation key chosen for muscle memory only transfers on layouts that
  place it identically. (2026-08-12: a shortcut cheat sheet corroborates `;` = "Accented note", but
  the same page repeats the wrong `[` claim, so it may describe an older GP — the official GP8
  appendix remains the bar.)
- ~~**tremolo picking.**~~ **CLOSED 2026-08-19 at `R`** (user), and Live the same day
  (`ChartTremoloToggle`). It was the one technique verb the 2026-08-12 letter map left without a
  letter, because `T` is the tap's. `R` is for REPEAT rather than for tRemolo's second letter, and
  that distinction is the reason to prefer it: both surfaces already describe the teeth as
  "repeated attacks", so the mnemonic states the rule the notation is drawn from instead of
  borrowing whichever letter happened to be free.

---

## Tone-region row (keyboard) — Live 2026-09-13, unsighted

The tone strip is a **single selectable region-row** in the vertical stack, between the chart
strings and the automation lanes, and the keyboard reaches it by SELECTION, never with an armed
caret: nothing is typed on a span surface, and the caret arms only where a keystroke authors a point
(re-ruled 2026-09-13, `keyboard-focus-rows.md`). `↓` from string 1 selects **the region holding the
cursor**, the caret demoted in place. The **signal chain is not in the arrow flow**. Every verb here
is paused-only, and so are the strip's pointer gestures — the boundary drag and the `Alt` insert
do not even start while the transport plays (2026-09-14); the region's own SIGNAL CHAIN stays live
mid-play, which is the point of the live rig.

With a tone region selected:
- `↑` arms string 1 at the cursor, `↓` arms the first lane (or selects the "+" row when the tone has
  none); `Ctrl+↑/↓` reach the adjacent group the same way.
- `←/→` and the jump keys leave the row, arming in place on the remembered row (the passive
  marker's law, the lane included). Stepping between regions is `Tab`'s job (Live 2026-09-13);
  `Shift+Tab` from inside a region past its start lands on that region's start first (Live
  2026-09-14, when the step began reading the cursor).
- `Alt+←/→` moves the region's START — the tone change it opens — one placement-quantum line,
  refused (never clamped) for the first region, whose start is the song's, and onto a neighbour's
  start or the song's end. A landed move brings the paused cursor to the new start, so the edit is
  in view (Live 2026-09-14).
- `Enter` restates the region (the marker grammar); `Delete` deletes the change (merge into the
  previous region). `Ctrl+R` renames the region's tone (Live 2026-09-14).
  Proposed the same day, not ruled: once the chain has a keyboard model, `Enter` drills into the
  tone's signal chain instead (*Plugin chain* below), leaving the retone to `Ctrl+T` at the region's
  start.
- A split location is the CURSOR — the armed caret on a string or a lane, else the paused cursor.
  Under the author-at-cursor grammar (Live 2026-09-14) `Ctrl+T` therefore splits from this row too,
  with the region selected and the cursor inside it.

Retired with the re-ruling: the region row's own caret with grid-stepping `←/→`, the `Insert` split,
the keyboard `Shift+Alt` resize, and `Enter` as the signal-chain drill (re-proposed 2026-09-14 above,
now that `Ctrl+R` carries the tone's rename).

---

## Automation lanes — creating a lane, and the empty case (the "+" row Live 2026-09-13, unsighted)

An automation lane's identity is a **plugin parameter**, not a grid position — so there is no empty
"lane slot" to `Insert` into the way a note has an empty grid slot; creating a lane means *picking a
parameter to automate*. To keep that keyboard-reachable and avoid a jarring skip when a tone has no
lanes yet, the automation surface always carries a focusable **"+" row** (present whether the tone
has zero lanes or ten), reached by selection like the tone row:

- Descending the stack lands on the first lane, **or on the "+" row when there are none**; `↓` past
  the last lane selects the "+" row, and `Ctrl+↓` from any lane reaches it.
- On the "+" row, `Enter` opens the **plugin → parameter picker** the "+" chip opens, anchored to the
  chip; choosing one opens the lane and arms the caret on it. `Insert` is not paired with the row
  yet.
- Edge: if the tone has **no plugins**, there is nothing to automate — the picker says so.
- The "+" row is reached only by the walk, which is paused-only, and a lane's POINTS are markers
  too: selecting, creating, moving and deleting one is refused while the transport plays
  (2026-09-14).

The plugin-centric path — `Ctrl+↑` from a selected plugin reveals (or offers to create) *that
plugin's* lanes — is filed under the deferred **targeted drill**; both can coexist later.

---

## Plugin chain (keyboard) — `✚ proposed`

The signal-chain panel (`SignalChainView`, bottom of the editor) already exists but is pointer-only;
this adds a keyboard model to it. It is a **slot** axis (signal order), not a time axis, so it has
**no time caret** — time stays owned by the timeline caret, and *which* tone's chain you edit follows
the playhead. Keyboard tone/plugin editing lives **here**, which is why the tone strip stays
pointer-only.

**Entering / leaving:** `Enter` on a selected **tone region** drills into that tone's chain (first
plugin); **`Esc` returns** to the tone region. The chain is *not* in the arrow flow — `Ctrl+↓` from
the lanes does not reach it, and `↑/↓` are inert inside it (no vertical axis). Entering **parks the
shared time caret (passive)** and hands the arrows to a **slot-focus**. The selected plugin slot is a
**mutually-exclusive variant of the one editor-wide selection** — selecting a plugin clears any
note/point/region selection, and vice-versa.

| Keybind | Behavior | Status |
|---|---|---|
| `←` / `→` | navigate plugin **slots** | `✚` |
| `Alt+←/→` | **reorder** — move the plugin slot-to-slot (reuses the pointer `MovePlugin` edit) | `✚` |
| `Enter` | **open** the plugin window; on an empty slot, falls back to the picker | `✚` |
| `Insert` | open the plugin **picker** — empty slot → **create**; filled slot → **replace** (confirmation prompt before overwriting) | `✚` |
| `Delete` / `Backspace` | **remove** the plugin (reuses the pointer `RemovePlugin` edit) — **with a confirmation prompt** | `✚` |

Reorder and delete route through the **existing pointer-path edits**, so the cascade (a plugin owns
its automation lanes) and single-entry undo are inherited unchanged — the keyboard is only a new
front-end, undo behavior is unaffected. The delete confirmation is justified not by data loss (undo
restores it) but because **undo reloads the plugin, which is slow**.

**`Insert` on a filled slot = replace — a deliberate, scoped exception** to the editor-wide "`Insert`
never mutates an existing object." The exception is principled: on the other surfaces `Insert`-on-
occupied has no *useful* meaning (an automation point has nothing to overwrite). The chart is no
longer part of this rule at all — `Insert` authors nothing there since 2026-09-11, every note being
typed — so the rule now governs the lanes, the tone row and the chain, and its one exception is the
only place it could ever have been tested. A plugin is the one object
where the occupied action *is* useful and common — swapping one pedal for another shouldn't require
a two-step delete-then-add — so on a filled slot `Insert` opens the picker and, once you choose the
replacement, prompts for confirmation before overwriting. `Enter` stays *open the window*, never
replace. *(When this folds into `editing-interaction-model.md`, the settled Insert rule gets this
chain-scoped exception noted alongside it.)*

**Future enhancements (revisit after the current keybinds settle):**
- **Bypass / enable toggle** — A/B a plugin without deleting it (agreed essential; a new state-toggle verb-class, needs a `PluginBypassEdit`).
- `Ctrl+D` **duplicate** the selected plugin (state included). Scope-local, so it does not
  collide with the chart scope reserving `Ctrl+D` for Brush Down; revisit if a plugin clipboard makes
  copy/paste sufficient here too.
- `Ctrl+C` / `Ctrl+V` **copy/paste** a plugin — or a whole chain — across tones (needs an editor clipboard).
- `Ctrl+↑` from a selected plugin surfaces **that plugin's** automation lanes (targeted drill, closing the chain↔lanes loop).
- `Ctrl+Alt+←/→` = **move-to-end** (or leave `✗`, uniform with the discrete strings row).
- Chain-level **A/B snapshot** compare.

---

## What the surface columns expose

Reading down the columns, the divergences separate into two kinds:

**Consistent specialization — same verb, the row's native data (leave as-is):**
`0`–`9`, `Alt+↑/↓`, `Delete`, click/double-click — fret vs value vs point vs
region are just what each surface's objects *are*. The chart's entry column is the one that
DIVERGED, and since 2026-09-11 it diverges by having fewer keys rather than more: every note there
is TYPED, so one digit row answers the whole surface — a head on an empty slot and at a ring's exact
end, a point on the path where a ring covers the slot — and `Alt`+digit differs in the single cell
that authors the slide-out. The keys the other surfaces still use to create — `Insert` and
`Alt`+click — create nothing on the chart, because a chart object always carries a fret and there
is no neutral value to plant without one.

**By design (not a gap):** the **tone strip has no point-placement caret** — it's a span surface, so
keyboard access is *region-selection*, not point-authoring: it's a selectable **region-row** in the
vertical stack (select/resize/`Enter`-to-chain/delete the region at the cursor), and point-level tone
editing lives on the **plugin chain**. See *Tone-region row*.

**Real gaps / inconsistencies (parity work for the plan, not keymap decisions):**
- **`Ctrl+Alt+↑/↓`** — fine value on lanes, `✗` on the highway (strings are discrete).
- **Multi-select is chart-only** — `Ctrl`+click toggle and marquee are `✗` on lanes and tone.
- **Drag-move / edge-resize** — live on lanes/tone, **parked** on the chart.
- **`Alt`+wheel duration** and its fine/fret variants — chart-only.
- **Right-click menu** — missing on the chart.
- **Extent-resize is split three ways** — chart keyboard (`Shift+Alt`), tone pointer edge-drag, lanes none.

## Surface parity triage (decision 5-B — deciding each gap individually)

Each coverage gap gets decided explicitly: **close** (schedule the parity work) or **intentional**
(document the per-surface difference as deliberate). Working through them one at a time:

1. Chart pointer drag-editing — **CLOSE: drag-move only** (reposition a note by mouse; plain = grid, `Ctrl` = off-grid). **Sustain edge-drag dropped** — `Alt`+wheel is already the mouse sustain command, so an edge-drag would be redundant. · **scheduled**
2. Right-click on the chart — **CLOSE: build it as a keybind-discovery menu** listing every applicable action + its **live keybind** (context-sensitive). Reframes the menu from "redundant action path" to "teach the shortcuts." Applies to **all surfaces'** menus for consistency; best built on plan 46's command registry (JUCE surfaces the current shortcut per item automatically). Supersedes the "deferred until techniques" note. · **scheduled**
3. Multi-select on automation lanes — **CLOSE: `Ctrl`+click toggle + marquee** for points (join the object-selection like notes). Use case: select a run of points and bump the whole shape across a grid line together. · **scheduled**
4. Multi-select on the tone strip — **INTENTIONAL: leave single-select.** Tone changes are sparse and rarely bulk-edited; single-select matches the strip's already-distinct sparse/structural, pointer-only nature. Documented as deliberate, not a gap. · **intentional**
5. Extent-resize on tone regions — **CLOSE: keyboard `Shift+Alt+←/→` resize + `Ctrl+Shift+Alt` fine**, via a new selectable **tone-region row** in the vertical stack (select the region at the cursor's time). Lanes stay `—` (no extent). This also brings the tone strip into keyboard nav — see *Tone-region row* below. · **scheduled**

## Fold-in issue resolutions (settled while folding rules into the design docs, 2026-07-20)

The rule fold-in surfaced conflicts needing a call. Resolutions as they settle:

- **A — DECIDED (A2): the plugin chain is a separate modal focus scope, NOT part of the one
  editor-wide selection.** The timeline keeps its flat one-selection; the chain is a modal
  sub-editor (`Enter`-in / `Esc`-out) with its own focus and verb set (`Delete` = remove plugin,
  `←/→` = slots, `Enter` = open, `Insert` = picker). "One selection editor-wide" is reworded to
  "one *timeline* selection + a separate chain focus" — the no-ambiguity benefit is preserved (the
  active scope determines what `Delete` hits). Drilling in **parks** the timeline selection (`Esc`
  restores it); clicking a plugin does not clear the timeline selection; reveal-on-undo may pull
  focus into the chain. Do **not** add a `PluginSlotSelection` to `EditorSelection`. **Links to G —
  the loud active-scope indicator is now a hard requirement.**
- **B — POSTPONED to last.** The tone-region row's marker semantics (armed caret riding a *span*,
  the clear-rule split, the string↔tone↔lane seams) need every keybind on that surface considered
  together — revisit after the rest are settled.
- **C — DECIDED: strict grid-lock + operation-`Ctrl` carried through.** The ruler drag is a
  *selection*, not a placement, so it uses grid-locked selection semantics rather than
  `placementModeFor` (amends plan 47 decision 5 — no "second mapping" issue, since placement and
  selection are different operations under the partition). **C-i (a):** `Ctrl+ruler-drag` =
  measure-snap (reach; pointer twin of `Shift+Ctrl+arrow`). **C-ii:** a time-selection started from
  an off-grid caret snaps its anchor to the grid (range starts at the caret's grid cell; the
  off-grid note stays *inside* the range) — accepted discontinuity. Execution: amend plan 47 (drop
  `Ctrl`-off-grid endpoints), rewrite "`Ctrl` = precision everywhere" → operation partition,
  re-check plan 46's fixed-vocabulary note.
- **D — DECIDED: mutual exclusivity.** Object-selection and time-selection are two
  mutually-exclusive kinds of the one selection — never both live; selecting one clears the other;
  no precedence ladder. Resolves plan 52's Q6 (moot), Q10 (a time range dissolves the object
  selection/caret), Q12 (moot); overrides plan 52's "complements, not competitors" language (update
  plan 52). The loop region stays a separate persistent transport state (unaffected). Structural
  landing: add a `TimeSelection` variant to `EditorSelection`, reconciled with plan 47's
  `LoopSelectionViewState`.
- **E — DECIDED (E2): keep `Ctrl+T`.** `Ctrl+T` = insert a tone change at the **playhead** from any
  surface (a from-anywhere accelerator, and the first of the insert-at-playhead family for future
  anchors/notes); it coexists with the tone-row `Insert` at the **caret** — different target
  positions. Fix the `Ctrl+Alt+T` bug by **guarding `Ctrl+T` against `Alt`** (require `Ctrl` and
  not `Alt`), NOT by removal. *(Reverses the earlier "Ctrl+T retired" note.)* **Realised
  2026-09-12:** the insert-at-cursor family is the `Ctrl` marker family (*Markers*
  above), and every member gains the RESTATE half. **Re-ruled 2026-09-14:** that half reads the
  cursor, not the selection — the chord reopens the payload of the marker of its kind standing
  exactly at the cursor instead of inserting, and `Enter` restates a selected one.
- **F — DECIDED: one named exception + create framing.** "Insert never mutates an existing object"
  gains exactly ONE named exception — a *filled plugin slot* (replace-with-confirm). The tone-row
  `Insert`-split is framed as a **create** (a new tone change at an empty region-interior; the
  objects are the boundaries, interiors are the empty gaps between them; `Insert` on an existing
  boundary no-ops) — so it stays *inside* the rule, no exception. Update the verb table + §9b together.
  **Amended 2026-09-11:** the chart drops out of this rule entirely — `Insert` authors nothing on
  the chart now that every note is typed — so the rule is the lanes', the tone row's and the chain's,
  and the filled plugin slot remains its one named exception. **2026-09-12:** the marker chords'
  RESTATE half sits outside this rule, not as an exception to it — `Ctrl+T` is not `Insert`; it is
  the digit law's positional create-or-retype (re-ruled 2026-09-14 to the marker AT the cursor, where
  it first read the selection).
- **G — DECIDED: loud active-scope indicator (required by A2) + `Enter` escalation.** While the chain
  holds focus: a loud focus ring on the selected slot, the chain panel reads "active"
  (highlighted header/border), the timeline visibly de-emphasized. `←/→` is documented as
  scope-dependent (chart/lanes/tone row = time caret; chain = slots). `Enter` escalates by drilling
  (tone region → chain → plugin window); `Esc` unwinds one level.
- **H — folded in as cleanup (no decision):** re-home/drop the deferred "GP-style `+/-` sustain-entry"
  note (`+/-` is now grid), and reconcile the `docs/tracking/watch-items.md` "sustain tail-drag"
  entry (the edge-drag is dropped; `Alt`+wheel covers it).

## Verdict record — all decided (sign-off completed 2026-07-20)

1. **DECIDED (A) — bind `Ctrl+Home`/`Ctrl+End` as chart-bound aliases.** A held-`Ctrl` navigation that does nothing reads as a broken editor — more confusing than a harmless duplicate; remap if a genuine use appears later.
2. **DECIDED (C) — GP-style zoom/grid.** `+/-` (main `=/-`, `Shift+=`, numpad) = **grid**; `Ctrl`+`+/-` = **zoom**; the wheel stays zoom (plain + `Ctrl`+wheel). `[ ]` freed. Matches GP + browser and dissolves the fine-zoom/browser collision (`Ctrl+=` = normal zoom, not fine). *(Grid direction confirmed 2026-07-20: `+` = finer.)*
3. **DECIDED (A) — `Shift+↑/↓` unbound.** The time selection is full-height, so there's no vertical axis to extend along; overloading it for object-multi-select would fracture the object-vs-time split. Stays free.
4. **DECIDED (B) — strict grid-lock.** *All* time-selection is grid-locked (keyboard **and** pointer); a range boundary can never be off-grid. **Amends plan 47** (drop its `Ctrl`-off-grid range endpoints). A copied range still captures off-grid *content* inside it — only the boundaries snap — so copy/paste always lands clean.
5. **DECIDED (B) — triage each gap individually** (close vs document-as-intentional, deciding each case explicitly). Per-gap outcomes tracked in *Surface parity triage* below.
6. **DECIDED 2026-07-20 — Undo/Redo/Play-Pause are non-rebindable core commands** (dissolving 46-Q3). `Ctrl+Shift+Z` joins as a first-class redo alias (DAW muscle memory). **REVERSED the same day** after the mirror-constraint correction (the user's REAPER counterexample): the trio is fully rebindable, and rebinds mirror into plugin windows through the generalized layout-neutral injection seam (plan 46 Phase 4 execution record).
7. **DECIDED 2026-07-20 — sign-off flags all accepted**: the `Ctrl+PageUp/Dn` ride-along alias, the `Shift+-` grid match, `+` = finer, and 759b145f's conservative time-selection defaults (placeholders pending plan 52).
