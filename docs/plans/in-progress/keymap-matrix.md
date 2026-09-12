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
> and `planDisconnectKeyframes`' segment walk — now its only caller — makes the point the new head
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
> changes, the tone changes, the hand does this here — and `Ctrl`+letter inserts a marker of that
> kind at the CURSOR (the marker rule: the armed caret when one exists, else the transport
> position, snapped to the kind's own quantum). The SAME chord with a marker of that kind
> SELECTED restates it — the digit law's create-or-retype, applied to markers — and selection
> wins over the cursor exactly as it does for a typed digit. `Alt+←/→` moves a selected marker by
> its kind's step and `Delete` removes it; no kind gets a verb of its own beyond its chord. The
> six kinds and their chords are the *Markers* table below: `Ctrl+T` and `Ctrl+M` are live, the
> other four are RESERVED for the plans that build their objects. `Alt`+letter is the platform's
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
  section, tone change, tempo anchor, meter, position or span marker inserted at the cursor, or
  restated when one is selected). Letters alone touch the note under the caret; `Ctrl` never does.

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

`Shift` = range / extend / axis-lock. The **time
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
| `↑` / `↓` | → adjacent string (crosses into lanes at the edge) | → adjacent lane (crosses into strings at the edge) | `✗` | Live |
| `Ctrl+↑/↓` | → adjacent **surface** (chart ↔ tone-region ↔ lanes) | → adjacent **surface** | → region-row | `Δ` (replaces the dead first/last-row no-op) |
| `PageUp` / `PageDn` | → prev / next **section** | → prev / next **section** | `✗` | Live (ae0e7ad5; Ctrl rides along as an alias — accepted 2026-07-20) |
| `Home` / `End` | → chart **start / end** | → chart **start / end** | `✗` | Live (ae0e7ad5) |
| `Ctrl+Home` / `Ctrl+End` | chart start / end (alias) | chart start / end (alias) | `✗` | Live (ae0e7ad5) |

## Time selection (one full-height span — crosses every surface, so not per-surface)

| Keybind | Behavior | Status |
|---|---|---|
| `Shift+←/→` | extend time-range by the **display grid** | Live (759b145f) |
| `Shift+Ctrl+←/→` | extend time-range by **measure** | Live (759b145f) |
| `Shift+PageUp/Dn` | extend time-range by **section** | Live (759b145f) |
| `Shift+Home` / `Shift+End` | extend time-range to chart **start / end** | Live (759b145f) |
| `Shift+↑/↓` | *(nothing — the range is full-height; no vertical extension)* | `—` unbound (confirmed) |

*(759b145f ships conservative defaults on the unsigned sub-decisions — accepted 2026-07-20 as
placeholders until plan 52's range verbs land: typing with a range is inert (52-Q11), Delete over
a range is a no-op pending plan 52's content-delete, and the extend is paused-only.)*

## Authoring — move / resize / fret (acts on the object selection)

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `Alt+←/→` | move note(s) in time (grid) — and a selected KEYFRAME by its OFFSET along the ring it rides, the same placement-quantum step at that note's measure (W13 ruled, 2026-09-09). On the RELEASE (the keyframe at the ring's end, the falls-away chip) the step drags the ring's end with it — the slide-out lengthens or shortens — refused onto the last sounded fret (ruled 2026-09-10: the chip is the fall's handle, the tail verb never moves a point). One planner and one entry for a mixed selection; a selected note's own keyframes ride at unchanged offsets, since an offset is relative to its onset. Bounds are the rule authority's through the finalize gate, so a step onto or across a neighbour refuses rather than swapping — the offset IS the keyframe's identity, which is also why the step re-keys the selection. A note landing INSIDE an earlier note's tail on its string re-strikes it, and this is the one editing gesture that truncates: that ring SHORTENS to the landing and its release rides back to its clearance. It never DELETES a statement, though (ruled 2026-09-11) — a landing that would clip any OTHER keyframe off that tail is refused whole, since the statement belongs to a note the charter never touched; a keyframe standing exactly ON the landing is moved back by the clearance repair, not erased, so it is allowed. A held or repeated run is ONE GESTURE and one undo entry (see the gesture note below) | move point in time (grid) | `✗` (no keyboard) | Live |
| `Ctrl+Alt+←/→` | move **1/960 fine** | move **1/960 fine** | `✗` | **Retired 2026-08-23** — snap off + `Alt+←/→` |
| `Alt+↑/↓` | move across **strings** — notes only: a keyframe has no string of its own and a selected head carries its path across by construction, so a keyframe-only selection is inert here (W13 ruled, 2026-09-09). Same gesture as the row above: a run of presses in either axis is one entry | move **value** | `✗` | Live |
| **`Ctrl+Alt+↑/↓`** | **`✗` (strings are discrete — no fine)** | **move fine value** | `✗` | **Retired 2026-08-23** — the value tier went with the fine tier |
| `Shift+Alt+←/→` | resize **sustain** (grid) — from a selected head OR a selected KEYFRAME, which reaches the ring it rides: a keyframe sits on the tail and this is the verb that acts on the tail, so the end of a slide is a place to pull the tail out from. The HEAD verbs (mute, accent, the techniques) deliberately do not reach through a keyframe (user ruled, 2026-09-09). A step every bound absorbs is refused and never recorded, so a tail at its floor or ceiling simply stops and the next press the other way moves it (2026-09-09) | `—` (points have no extent) | `✗` (pointer edge-drag instead) | Live |
| `Ctrl+Shift+Alt+←/→` | resize sustain **fine** | `—` | `✗` | **Retired 2026-08-23** — snap off + `Shift+Alt+←/→` |
| `Shift+Alt+↑/↓` | **fret shift** ±1 — over heads and selected KEYFRAMES alike, off one anchor (the lowest stop the selection addresses), which is the delta form a chord slide needs (W13 ruled, 2026-09-09) | `—` (no frets) | `✗` | Live |

*(The `Ctrl+Alt+↑/↓` row was your example of a chord bound on one surface and unbound on another;
it is retired outright now, but the asymmetry it illustrated is still how the matrix reads.)*

*(GESTURE ROWS. The sustain row and the two `Alt`+arrow move rows are each ONE verb and one gesture:
held or repeated, the presses record a step LIST, the whole selection re-plans by replaying that
list over the values the run started at, and the run stays one undo entry. Both run through one
authority (`commitChartGestureStep`), so both end at exactly the same commit points — a selection
change, a caret move, any other verb, undo/redo, a save, a committing settle — and a run that
replays back to its start retires its entry rather than leaving a Ctrl+Z that changes nothing. What
differs is only what a step MEANS: a sustain step moves the ring's END onto the adjacent line of the
placement quantum's lattice, so a run may cross a snap toggle; a move step carries the selection by
that quantum scaled by the meter where the run has REACHED, so a run crossing a signature change
steps by that meter's own amount from there on — which is why neither verb can sum its presses into
one delta. Sustain ruled 2026-08-22, steps 2026-08-23; the move rows joined it as ruling 8's own
extension; `docs/plans/in-progress/note-sustain-model.md` ruling 8. The `Alt`+wheel duration rows
below are the sustain verb through the pointer.)*

## Payload entry

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `0`–`9` / numpad `0`–`9` | **THE WHOLE OF CHART ENTRY — every note is TYPED** (re-ruled 2026-09-11; this row's two earlier same-day forms, the truncating strike and the lossless split under a bare digit, are both superseded). On an EMPTY slot, a **head** at the typed fret. At the EXACT END of a ring, a **head — the next note**: the ring already stops there, so there is nothing to divide and nothing to shorten, which is what makes **sequential entry safe**, a digit at the previous note's end always being the note after it. Where a slide-out ALREADY ends on that slot, arming the caret selects its chip, so the digit RETYPES the fall by the ordinary selection rule rather than placing anything. STRICTLY INSIDE a ring, a **POINT on that note's path** at the typed fret — the same product `Alt`+digit states, since inside a ring the modifier has nothing left to say. There is no note gesture over a covered slot at all: to divide a ringing note, type the point and press `Shift+L` (the row below). **Sequential entry meets the covered case only past the grid**: the slot after a grid-step ring is that ring's END, so the digit is the next note, while a ring deliberately lengthened PAST its grid step makes the following slot a covered one, where the digit is a point instead — Guitar Pro users never meet this, its durations being per beat. A digit inside an OPEN STRING's tail shows the red box: a fret-stating point on an open string is refused by chart law (`OpenStringSlide`), nothing being pressed to glide. A point that merely restates the fret the path is already running on says nothing, so it is silent authoring state and dissolves with focus — typing the same fret on a tail leaves nothing behind. Multi-digit through the shared 750 ms window; an illegal fret paints red and discards. A digit over a non-empty SELECTION retypes it instead — heads and the arpeggio bracket of a selected silently-held stop alike (2026-08-27) — bare or under `Alt`: a non-empty selection is the operand either way, and only a digit at a bare caret standing on a ring's exact end has anything to choose between. Which STOP the digits state is the caret's channel (2026-08-27, the held-fret increment): bare digits state the note's own sounding fret, and digits after `N` — or after clicking the satellite digit beside a bracket, or after stepping the caret onto that satellite — state the fretting hand's `held` stop under a right-hand onset. One pending entry either way; the channel decides where it lands and where its red box draws. A selected KEYFRAME retypes the same way (W13 ruled, 2026-09-09): the flow, the multi-digit window and `planRetypeFrets` are the note flow's, the entry simply gained a keyframe operand, and the SELECTION KIND — not a third `ChartStopChannel` value — says which stop the digit reached. Its pending BOX has no target yet, since the view publishes retype targets as note indices; a refused keyframe digit is therefore silent until that display lands (`docs/tracking/backlog.md`). A digit CONTINUES a live pending entry rather than opening a second one, bare or under `Alt`, and THE FIRST DIGIT'S MODIFIER DECIDES what that entry creates — so at a ring's exact end, the one slot where the two chords part, `Alt`+1 then a bare 2 is a fret-12 slide-out and a bare 1 then `Alt`+2 is a fret-12 head | open **value editor** at armed caret | `✗` | Live |
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

A marker is a stated fact about the document at a position. Every kind has ONE chord and the same
four verbs: the chord INSERTS at the cursor (armed caret else transport, snapped to the kind's
quantum; refused where one of that kind already stands), the same chord with one SELECTED
RESTATES it (reopens its payload), `Alt+←/→` MOVES it by the kind's step, `Delete` removes it.
Click selects a marker's chip; double-click is the pointer form of restate where a chip has one.
A kind with no payload has nothing to restate, and its chord with one selected is refused.

| Chord | Marker | Scope | Quantum | Payload | Status |
|---|---|---|---|---|---|
| `Ctrl+T` | tone change (the region boundary; restating repoints the selected region at another catalog tone) | arrangement | grid slot | tone pick | Live (restate 2026-09-12) |
| `Ctrl+M` | section | song | measure downbeat | name | Live (`0x1402`, "Insert or Rename Section"; restored 2026-09-12) |
| `Ctrl+B` | tempo anchor (BPM; inserting pins the time the map already assigns to that beat, so it changes nothing audible until moved) | song | beat | none — `Alt+←/→` is its millisecond nudge | **RESERVED** for plan 41 |
| `Ctrl+/` | meter (the glyph in 4/4) | song | measure downbeat | numerator, denominator | **RESERVED** for plan 41 phase 6 |
| `Ctrl+P` | position marker (fret-hand position) | arrangement, chart | grid slot | this gate's ruling | **RESERVED** behind the FHP gate |
| `Ctrl+H` | span marker (what the hand does) | arrangement, chart | grid slot | this gate's ruling | **RESERVED** behind the FHP gate (whether span and position are one object or two is its first ruling; one object means one chord) |

`Ctrl+G` is grid snap and `Ctrl+S` is save, which is why span is not on G and section is not on
S. The quanta are the coarsest grid each kind can live on; "measure downbeat" means the measure
the cursor is IN, never the nearest one. Every chord is a default and rebinds like any other.

## Song sections (the ruler's chip row)

Sections are song-level markers on the pinned ruler, and the chip is a fourth object kind the one
editor-wide selection can hold. One chord is the section's own; the rest are the selection verbs
already in the tables above, reaching a new alternative rather than gaining a chord.

| Keybind / gesture | Behavior | Status |
|---|---|---|
| `Ctrl+M` | with no section selected, add one at the cursor's measure downbeat, name from a prompt; with a section selected, reopen that prompt to rename it (the marker grammar's restate; `Ctrl+M` is the section's letter on the document plane, and `Ctrl+S` is save) | Live (signed 2026-09-12) |
| `Delete` | delete the selected section — the same `Delete` as everywhere, dispatching on the selection's kind | Live |
| `Alt+←/→` | move the selected section one **MEASURE**, not one grid step: a section starts on a downbeat and nowhere else, so a measure is its step. Refused, never clamped, onto a downbeat another section holds or outside the song | Live |
| `Alt+↑/↓` | *(nothing — a marker on one timeline row has no vertical axis)* | `—` unbound |
| **Click chip** | select it. Seeks nothing, which is what lets the selection survive the cursor-move rule that clears it (the tone region's lifecycle, shared) | Live |
| **Double-click chip** | rename prompt, the pointer form of `Ctrl+M` on a selected chip | Live |
| **Right-click ruler** | the section menu: add always, plus rename / move / delete over a chip, which the menu selects first. A chord alone is undiscoverable, which is why the menu exists | Live |

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
| `Ctrl+O` · `Ctrl+Shift+O` · `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+Shift+P` · `Ctrl+W` · `Ctrl+Q` | Open / Import / Save / Save As / Publish / Close / Exit (the tier A file-menu chords; menu items show live shortcuts; `Ctrl+Q` added 2026-07-20) | Live (registry 2026-07-20) |
| `Ctrl+T` | with no tone region selected, insert a tone-change marker at the **cursor** — the marker rule: armed caret if present, else the transport position (from any surface); with a region selected, RESTATE it: the picker reopens to repoint that region at another catalog tone (its own and both neighbours' excluded, since either would leave a boundary with no change across it) | Live (guard against `Alt` 2026-07-20; marker-rule anchor + "at Cursor" name 2026-07-21; restate 2026-09-12) |
| `Ctrl+M` | with no section selected, add a **song section** at the cursor's MEASURE — the same marker rule as `Ctrl+T`, then snapped to that measure's downbeat, which is the only place a section can start; a prompt takes the name. Refused where a section already stands. With a section selected, RESTATE it: the rename prompt | Live (`0x1402`, "Insert or Rename Section"). **Signed 2026-09-12** under the marker grammar; held by `Shift`+`Insert` for one day before that |
| `Ctrl+B` · `Ctrl+/` · `Ctrl+P` · `Ctrl+H` | tempo anchor · meter · position marker · span marker, each inserted at the cursor and restated when selected — see *Markers* | **RESERVED** (plan 41; plan 41 phase 6; the FHP gate) |
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
| `Shift+L` | **tie / slide-link** — the `L` verb extended with travel: on an equal-fret junction with no technique change, a tie whose settled truth is one longer sustain (the tie never enters the format); on different frets it authors the connecting slide on the predecessor's tail. Apply-or-clear toggle at parity with `L` — clearing an existing link IS the slide break, dissolving W6's separate break verb. GP's own `Shift+L` ("tie the beat") is subsumed by the uniform-scope law, so the slot is vacated by our design, not stolen. W10 also inherits the map's one inverted pair: on `L` it is the `Shift` meaning a charter reaches for more often (the 5-of-6 corpus check above), so W10 rules whether these two slots stay this way round. Full design + open rulings: `technique-review-walkthrough.md` W10. **This chord is also the SPLIT's one home** (2026-09-11) | **Partly live 2026-08-26** (`0x1713`, "Disconnect Keyframe", Authoring): the chord is bound and its KEYFRAME clause is built — with a keyframe selected the press severs the gesture there, `planDisconnectKeyframes` (W10's 2026-08-26 addendum; the split product's unstruck-tie default is still a PROPOSAL, so the split head stores the plain legato claim W10 signed). **THE LOSSLESS SPLIT, in two keystrokes** (2026-09-11): a digit plants the point where the charter wants the division, `Shift+L` disconnects it there — the point becomes the new head, the original note ends exactly on it, everything after it rides the new note, a slide-out included, and the first note's arrival retreats one margin before the new head. `planDisconnectKeyframes`' segment walk is this verb's alone again — its only caller, now that no entry gesture splits — so there is one rule and one place it lives. A point that merely restates the fret the path is already running on is silent authoring state and dissolves with focus, so typing the same fret on a tail and stopping there leaves nothing behind; the split is the two keystrokes together. The tie/slide-link half and the apply-or-clear toggle on a LINK are unbuilt, so no verb window is armed |
| `T` | **tap** — the right-hand tap attack; the dark-T plate's letter | **Live 2026-08-28** (`ChartTapToggle`, labelled "Toggle Right-Hand Tap", verb `planSetAttack` through `chartTechniqueLaw`), taking up the reservation held since 2026-08-12. One of four rows on the ATTACK field — with the scrape, the slap and the pop — so all four are one shared row shape handed their own value: each toggles its own attack against the plain pick, and a press over a scope carrying another attack REPLACES it in one entry, exactly as the `Shift+V` pair does on the width axis. Validity stays the one rule authority's: `planSetAttack`'s written-form gate skips the open string with no node (E4's boundary, the same skip `Shift+T` has always had), drops a scrape's path and terminal when a note converts away from it, and refuses an attack on a silent hold, whose zero ring no struck note may keep. The label says "Right-Hand" because the menu and the undo history show it beside `Shift+T`'s "Left-Hand Tap"; the plates already name the pair by hand rather than by letter, so the words do too |
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
| `N` | **arpeggio hold (note-type conversion)** — promote a note to a silently-held member of the earlier shape ("my finger was already there"); the underivable fact the bracket needs to state a shape from its initial onset when a member is not struck until later. Design + storage: `docs/plans/todo/arpeggio-authoring.md` | **SIGNED 2026-08-25, Live 2026-08-26** (`ChartSilentHoldToggle`, labelled "Arpeggio Hold"; verb `planToggleSilentHold`; user: *"what if … we used 'N' … I guess N could stand for something like 'Note Type'? … This is a common enough thing that I like having a bare key for it, not Shift modified"*). `N` verified unclaimed in this matrix and the registry, no reservation. Supersedes the same-day `Shift+A` signing after the user's frequency judgment — a bare key for a common charting verb outranks the corpus proxy, which measures imports rather than authoring. The registry's `Shift+A` technique-block comment took its matching one-line correction in the same change. The verb is selection-scoped like every other row, with the typing family's caret fallback behind it (**re-ruled 2026-08-27**, retiring the brief caret-anchored exception): it acts on the selected notes, or on the armed caret's own slot when nothing is selected — the empty slot being the one operand a selection cannot name. Per slot: empty → a hold at the open string the charter then types a stop onto, a sounding note → converted in place, keeping its slot and fret, losing its ring and techniques, a hold → sounded again as a plain pick at the grid step; the direction is the scope's as a whole, exactly as a technique toggle's is, so a whole chord converts in one press and one entry. A press whose product would state nothing (a lone member, or a shape nothing justifies) refuses whole rather than deleting the note it was asked to hold. It shares the technique verbs' toggle window, so a second press reverses the first entry exactly — the only thing that can restore what a conversion stripped. **Sighted 2026-08-27, chord unchanged**: the verb draws no mark of its own — the arpeggio bracket at the span start IS what a hold looks like and selects as — and a selected bracket takes a typed fret through the ordinary digit row below. **Substrate swapped 2026-08-27**: the record is a note with `attack: none`, not a second array, so every chart verb reaches it with no case of its own. **FOURTH CASE 2026-08-27** (user, the held-fret increment): a slot holding a RIGHT-HAND onset — a tap or a scrape — gains a `held` stop at the open string instead of being converted, because that onset belongs to the picking hand and converting it would delete a sound the charter wrote; the caret moves onto that stop, so the digits that follow state it, and a second press clears it. Four cases, one meaning: state — or stop stating — the fretting hand's stop at this slot |
| `Shift+A` | **heavy accent** — RESERVED again, unbuilt and possibly never built | reservation RESTORED 2026-08-25 when the arpeggio hold moved to `N` (its brief same-day tenancy here is recorded in the `N` row). Both futures stay recorded: the original chord reservation (user 2026-08-18: *"Shift+A may someday be HEAVY accent"*), and the alternative plan of record noted on the `A` row — plain `A` cycling the emphasis axis — with the choice deferred until a heavy accent is actually wanted; the format's fourth emphasis value is the real gate either way |
| ~~`Shift+A`~~ | ~~(historical row)~~ | the arpeggio hold's brief 2026-08-25 tenancy, ended the same day when the verb moved to plain `N` — the LIVE `Shift+A` row above restores the heavy-accent reservation. Original reservation reasoning kept here: reserved 2026-08-18 (user: *"Shift+A may someday be HEAVY accent"*). This is the `Shift` plane used exactly as intended: a magnitude variant of the plain key's own technique, the same shape as `Shift+V`'s wide vibrato, and it is why the ghost could not have this chord. Like wide vibrato it would need the format to carry the extra value — `NoteEmphasis` has three today, and `isAccented` is already written as "every emphasis above normal" so a fourth would light up every consumer without a hunt |
| `H` | **fret-hand harmonic** — freed by legato's move; the strongest first-letter mnemonic in the map (GP's `Y` is legacy). A GP `H` habit now authors a loud, visible, undoable wrong mark instead of the silent off-by-one link it authored before — an improvement even for the habit it breaks. **THE FRET YOU TYPE IS THE NODE**: the finger stands where it would otherwise have pressed, so the fret-stating flow already states it — type 12, press `H`; the decimals are physics, not notation a charter chooses. Restating a node is press `H`, type, press `H` | **Live** (`ChartHarmonicToggle`, labelled "Toggle Harmonic"; verbs `planSetHarmonic` / `planClearHarmonic`). The set resolves each note's own fret against the stop its string SPEAKS from (the held stop under a right-hand onset, the capo otherwise), so one formula covers every hand — fret 5 open gives node 4.98, absolute fret 7 under a capo at 2 gives 6.98, a tap holding 5 and landing on 17 gives 17 — and `normalizeChartNote` then strips what a touch cannot carry. A fret naming no node is SKIPPED, never repaired: 1, 11 and 13 name nothing, and an open string states no position at all. The clear is shared with `Shift+H` and inverts the set exactly — the finger presses where it was touching — which reaches a tap harmonic and an imported artificial too, the only way the editor can un-harmonic either. **FOUR PROVISIONAL ITEMS AWAIT SIGNING, all recorded here rather than assumed:** (1) **the RANGE RULE is the label window**, not the ceil law — a typed fret names every node within `g_max_node_label_error` (0.5) of it, which is what makes 7 and 19 reachable and 1, 11, 13 dead; (2) **the nearest candidate is armed first** where a fret names two; (3) **every other action, the 750 ms window and `Esc` settle and COMMIT** the shown candidate, uniform with the fret entry, the discard rung being invalid-only; (4) **the default chords themselves**, `H` and `Shift+H`. |
| `H` (again, inside the window) | **the harmonic node picker** — the one press whose meaning the key alone cannot settle. Offset 3 above the stop names TWO nodes, the 7th partial's 2.7 and the 6th's 3.2, and no other offset in the whole ladder names more than one; there the press arms a provisional value instead of committing, a second `H` cycles, and any other action commits. Every other label settles in the same keystroke | **Live**, on the shipped `ChartFretEntry` machinery rather than a popup — one provisional-value idiom, so the window, the settle prologue and the single undo entry come for free, and the picker and toggle windows are exclusive by construction (the picker commits nothing, so there is nothing yet to reverse). The armed node draws on the head in the form it will COMMIT — diamond silhouette, node text, inside the accent-bordered pending box — with the unchosen one outboard on the same baseline in `EditorTheme::muted_text`, never dimmed: an unchosen row is fully choosable. One picker per press over a whole chord, because ambiguity happens at one offset, so every ambiguous member shares the choice and every unambiguous one resolves in the same plan. The MOUSE form is rows in the chart's right-click menu labelled value plus partial ordinal (`SetChartHarmonicNode`), applying directly — a menu choice is already deliberate, and the lane's ~26x16 px labels are a display, never a target |
| `Shift+H` | **pinch harmonic** — the fret-hand harmonic's sibling, the same technique reached by the OTHER hand, which is what the `Shift` plane is for here (as `Shift+T` is for the taps) rather than a magnitude variant | **Live** (`ChartPinchHarmonicToggle`, labelled "Toggle Pinch Harmonic"). The SET is `planSetAttack(Pinch)` unchanged — it already re-asks a node whose owning hand flips and authors the octave at the physical stop when none exists — and it asks for no value, because **neither surface draws a pinch's node** (`tab_paint_core.cpp`, roadmap 25-Q5) and the charter could not see one. The CLEAR is `planClearHarmonic`, NOT `planSetAttack(Pick)`: the row's noun is a harmonic, so its clear must remove one, where the raw attack row would leave `Pick + fret 5 + node 17` — an artificial harmonic nobody authored |
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

## Tone-region row (keyboard) — `✚ proposed`

The tone strip joins the vertical stack as a **single selectable region-row** (between the chart
strings and the automation lanes): dropping onto it **selects the tone region at the cursor's
time** — a span-selection, not a point-caret, so it respects the strip's span nature (you never
caret-place a tone point). The arrow stack is **chart strings ↔ tone-region ↔ automation lanes** —
plain `↑/↓` walk every row, `Ctrl+↑/↓` jump surface-to-surface (they converge on the single tone
row, a harmless seam effect, like plain `←`/`Ctrl+←` at a measure start). The **signal chain is not
in the arrow flow** — it's reached only by `Enter` (below), so `↓`/`Ctrl+↓` stop at the last lane.

With a tone region selected:
- `←/→` = move the time caret; crossing a boundary re-selects the region you're over — this is how you keyboard-pick a split location.
- `Insert` = split at the caret + open the tone picker (no neutral tone). Coexists with `Ctrl+T`, which inserts at the *playhead* from any surface (E2) — the two target different positions.
- `Shift+Alt+←/→` = **resize** the region (`Ctrl+Shift+Alt` = fine) — Gap 5.
- `Delete` = delete the selected change (merge into the previous region) — the unified selection-dispatched Delete.
- `Enter` = drill into the **signal chain** to edit that tone; **`Esc` returns** to the region (re-selected). Inside the chain `↑/↓` are inert — `Esc` is the way out.

This narrows the earlier "tone strip is keyboard-dead / pointer-only" to **"no point-placement
caret"** — region *selection* and its verbs are keyboard-reachable; point authoring still isn't.

---

## Automation lanes — creating a lane, and the empty case (`✚ proposed`)

An automation lane's identity is a **plugin parameter**, not a grid position — so there is no empty
"lane slot" to `Insert` into the way a note has an empty grid slot; creating a lane means *picking a
parameter to automate*. To keep that keyboard-reachable and avoid a jarring skip when a tone has no
lanes yet, the automation surface always carries a focusable **"+ add automation" row** (present
whether the tone has zero lanes or ten):

- Descending the stack (`↑/↓` or `Ctrl+↑/↓`) lands on the automation surface's first lane, **or on
  the "+ add" row when there are none** — it never silently skips past an empty automation surface.
- Plain `↑/↓` walk the lanes *and* the "+ add" row.
- On the "+ add" row, `Enter`/`Insert` opens a **plugin → parameter picker**; choosing one opens the
  lane and lands you on it. (Inside a lane, `Insert` keeps its normal meaning — create a point.)
- Edge: if the tone has **no plugins**, there is nothing to automate — the "+ add" row says so and
  points to the chain.

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
  2026-09-12:** the insert-at-cursor family is the six-chord `Ctrl` marker family (*Markers*
  above), and every member gains the RESTATE half: the chord with a marker of its kind selected
  reopens that marker's payload instead of inserting.
- **F — DECIDED: one named exception + create framing.** "Insert never mutates an existing object"
  gains exactly ONE named exception — a *filled plugin slot* (replace-with-confirm). The tone-row
  `Insert`-split is framed as a **create** (a new tone change at an empty region-interior; the
  objects are the boundaries, interiors are the empty gaps between them; `Insert` on an existing
  boundary no-ops) — so it stays *inside* the rule, no exception. Update the verb table + §9b together.
  **Amended 2026-09-11:** the chart drops out of this rule entirely — `Insert` authors nothing on
  the chart now that every note is typed — so the rule is the lanes', the tone row's and the chain's,
  and the filled plugin slot remains its one named exception. **2026-09-12:** the marker chords'
  RESTATE half sits outside this rule, not as an exception to it — `Ctrl+T` is not `Insert`, and a
  selected region is not the boundary a marker lands on; it is the digit law's create-or-retype,
  where selection has always meant "act on this, not the cursor".
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
