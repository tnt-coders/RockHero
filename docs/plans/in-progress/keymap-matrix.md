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

## The rule this encodes

`Ctrl`'s meaning follows the **operation**, not the key:

- **Navigating** — moving the caret / extending a selection → `Ctrl` = **REACH** (coarser unit:
  measure, section, first/last row).
- **Placing / moving an object** — pointer placement, or `Alt`+arrows → `Ctrl` = **PRECISION**
  (off-grid / 1/960 fine).
- **Clicking an existing object** → `Ctrl` = **TOGGLE** selection membership.

`Alt` = the authoring gate (input mutates). `Shift` = range / extend / axis-lock. The **time
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
| `Alt+←/→` | move note(s) in time (grid) | move point in time (grid) | `✗` (no keyboard) | Live |
| `Ctrl+Alt+←/→` | move **1/960 fine** | move **1/960 fine** | `✗` | Live |
| `Alt+↑/↓` | move across **strings** | move **value** | `✗` | Live |
| **`Ctrl+Alt+↑/↓`** | **`✗` (strings are discrete — no fine)** | **move fine value** | `✗` | Live |
| `Shift+Alt+←/→` | resize **sustain** (grid) | `—` (points have no extent) | `✗` (pointer edge-drag instead) | Live |
| `Ctrl+Shift+Alt+←/→` | resize sustain **fine** | `—` | `✗` | Live |
| `Shift+Alt+↑/↓` | **fret shift** ±1 | `—` (no frets) | `✗` | Live |

*(The `Ctrl+Alt+↑/↓` row is your example, now explicit: bound on lanes, unbound on the highway.)*

## Payload entry

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `0`–`9` / numpad `0`–`9` | type **fret** at armed caret | open **value editor** at armed caret | `✗` | Live |
| `Ctrl`+digit · `Alt`+digit | `✗` | `✗` | `✗` | Live (guarded) |

## Editing verbs

| Keybind | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| `Delete` / `Backspace` | delete note(s) | delete point | delete region (merges) | Live |
| `Insert` | fret-0 note at caret | on-curve point at caret | `✗` (no keyboard) | Live |
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

## Pointer

| Gesture | Chart (highway) | Automation lanes | Tone strip | Status |
|---|---|---|---|---|
| **Click empty** | seek + arm caret at grid | seek + arm caret at grid | select region under cursor | Live |
| **`Ctrl`+click empty** | arm caret **off-grid** | arm caret **off-grid** | (own meaning) | Live |
| **Click object** | select note + arm caret | select point + arm caret | select region | Live |
| **`Ctrl`+click object** | **toggle** membership | **toggle** membership (scheduled) `✚` | select (**no toggle**) | Live · `✚` lanes |
| **`Shift`+click** | time-range select (full-height span) | — same span — | — same span — | `▷52` |
| **Double-click object** | select **chord** | **property editor** | **rename / pick tone** | Live |
| **`Alt`+click** | insert fret-0 note | insert on-curve point | **split** region | Live |
| **`Ctrl+Alt`+click** | insert **off-grid** | insert **off-grid** | split off-grid | Live |
| **Drag on object** | move note (scheduled) `✚` | move point | move boundary | Live (lanes/tone) · `✚` chart |
| **`Ctrl`+drag object** | move **off-grid** (scheduled) `✚` | move **off-grid** | move boundary off-grid | Live (lanes/tone) · `✚` chart |
| **Edge-drag extent** | `✗` — chart sustain is `Alt`+wheel | `—` (no extent) | resize region | Live (tone) · chart uses `Alt`+wheel |
| **Drag from empty (marquee)** | marquee select | marquee (scheduled) `✚` | `✗` | Live chart · `✚` lanes |
| **`Alt`+drag from empty** | insert + place note | insert + place point | split + drag boundary | Live |
| **`Alt`+wheel** | duration (sustain / span) | `✗` | `✗` | Live (chart only) |
| **`Ctrl+Alt`+wheel** | **fine** duration | `✗` | `✗` | Live (chart only) |
| **`Shift+Alt`+wheel** | fret shift ±1 | `✗` | `✗` | Live (chart only) |
| **Right-click** | keybind-discovery menu (scheduled) `✚` | keybind-discovery menu | keybind-discovery menu | Live (lanes/tone) · `✚` chart |
| **Ruler drag** | create time selection → feeds loop region | — same span — | — same span — | `▷47` |

## Editor-wide (one behavior, surface-independent)

| Keybind | Behavior | Status |
|---|---|---|
| `Space` | play / pause from the marker | Live |
| `Ctrl+Z` / `Ctrl+Y` / `Ctrl+Shift+Z` | undo / redo (exact-modifier matched); `Ctrl+Shift+Z` = redo alias — **fully rebindable** with `Space` (fixed-trio decision reversed 2026-07-20; rebinds mirror into plugin windows via the generalized layout-neutral seam) | Live (registry + mirror sync 2026-07-20; manual plugin verification passed 2026-07-20) |
| `Ctrl+O` · `Ctrl+Shift+O` · `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+Shift+P` · `Ctrl+W` · `Ctrl+Q` | Open / Import / Save / Save As / Publish / Close / Exit (the tier A file-menu chords; menu items show live shortcuts; `Ctrl+Q` added 2026-07-20) | Live (registry 2026-07-20) |
| `Ctrl+T` | insert a tone-change marker at the **cursor** — the marker rule: armed caret if present, else the transport position (from any surface) | Live (guard against `Alt` 2026-07-20; marker-rule anchor + "at Cursor" name 2026-07-21) |
| `Esc` | cancel gesture → disarm caret → clear selection | Live |
| `F3` / `F5` / `F8` | toggle 3D preview / waveform / undo-history inspector | Live (`F5` added 2026-07-21) |
| `?` (`Shift+/`) | open the Actions dialog (the binding editor; REAPER's actions-list key) | Live (renamed from "Keyboard Shortcuts" + default added 2026-07-20; display collapses shifted chords through the shared `keyChordText` formatter) |
| plain wheel | zoom, marker-centered | Live |
| `Ctrl`+wheel | zoom (browser reflex — same as plain wheel) | Live |
| `+` / `-` (main-row or numpad — numpad arrives as the same character key codes) · `=` / `_` convenience aliases | **grid** finer (`+`) / coarser (`-`) | Live (chord sets corrected 2026-07-21: `numberPad*` chords never matched on Windows and were removed; display-equal shapes group into one chip; `=`/`_` aliases kept until something better claims them) |
| `Ctrl` + the same `+`/`-` family (incl. the `Ctrl+_` alias) | **zoom** in / out, marker-centered | Live (44f24ab6; chord sets corrected 2026-07-21) |
| `[` / `]` | **free** — grid moved to `+/-` | `—` |
| `L` | reserved (link/slide) — unbound | `—` |
| `B` | reserved for **bend** (plan 40 Phase 7), no longer for the pencil — unbound (user 2026-08-07: "B for bend makes more sense than B for pencil"; `Alt` already *is* the held pencil quasimode for the pointer, so the pencil needed no letter) | `—` |

## Technique verbs — the typed family (plan 40 Phase 5)

Technique toggles are **typed input**, so they take the typing family's gate rather than `Alt`: they
act on the selection (or the armed marker) and no-op when there is none, exactly as the digits that
retype a fret already do. That is the interaction model's law — *plain input never mutates; every
mutation passes a gate*, applied per input family — not a new rule. So these are **plain letters**.

**Settled 2026-08-07:**

| Keybind | Verb | Status |
|---|---|---|
| `H` | **legato** — one verb for hammer-on and pull-off, because **no direction is stored**: the note claims a connection to its same-string predecessor and which way that runs is read back by `resolveLegato` (amended 2026-08-11, `legato-final-spec.md`). Three things gate the claim beyond the frets: the judged fret is the predecessor's **released** one (its last waypoint, or a scrape's slide-out end), the predecessor must still be **holdable** at this onset (past the kept-sustain bound a disconnected tail is a proven release), and a fret-hand-harmonic predecessor is disqualified outright. Then the released fret picks the motion — higher = pull, lower = hammer — and a claim nothing justifies is refused rather than guessed, plays as the pick it sounds like where it already stands, and is flattened at the next settle. When the hold is the only thing missing, the verb authors the connection itself (the D14 assist): the plan grows the predecessor's tail to the margin point in the same undo entry, bounded by the duration verb's growth clamp, and skips a gesture-carrying predecessor (a scrape or a slide-out) whose tail is authored geometry. While the selection and history still prove the previous press was this verb's own, a second press reverses it exactly — grown tails included — leaving no trace (ruling 4's true toggle), and when a save between the presses made that entry the file's clean state the reversal pushes the exact inverse as a new entry instead of erasing it, so the tail still returns while "return to clean" stays truthful. Otherwise the press means apply-or-clear — **applying wins whenever it changes anything**, and the clear flattens the stored claims only, so a left-hand tap riding the selection keeps its attack — and a press that only skipped reports the count and the dominant reason | **Live** (`planSetLegato` + `ChartLegatoToggle`, labelled "Toggle Legato", default chord plain `H` — verified in `editor_command_registry.cpp`). Verified against the official GP8 manual's shortcut appendix: Guitar Pro binds `H` to "Hammer On / Pull Off" the same way. Its `Shift+H` "Legato" is a sheet-music slur that does not change the claim, so it has nothing to map onto here and stays unbound |
| `Ctrl+H` | **left-hand tap** — the **sole author of the left-hand tap**, the one statement no predecessor can justify or withdraw, valid across E4's domain (positive sounding position) and able to override a standing connection claim. `Ctrl` means *precision* — "I will state it exactly, do not infer" — and there is deliberately no second stating verb, a pull-off to a higher fret being physically impossible. Plain `H` can never produce this attack and its clear never destroys one — which is what makes the claim/statement split policeable rather than a convention. Under the derived-direction model the statement is its own stored value (`LeftTap`), so no neighbour edit and no settle sweep can withdraw it | **Live** (`ChartLeftTap`, labelled "Left-Hand Tap" since 2026-08-11 with its command id value retained, default chord `Ctrl+H` — verified in `editor_command_registry.cpp`). The verb is `planSetAttack(LeftTap)`: its written-form validity check yields the ruled domain from the one rule authority — the no-node open string is the sole skip, an open-string pinch's bridge-side graze refuses to re-hand, and a tap harmonic's strike point carries into E13's form. Since 2026-08-12 the stored statement wears its own charting mark in the 2D lane: the tap letter on the LIGHT plate - fill polarity is the plate family's hand signature (55-Q1's corrected basis), so the right-hand tap's dark T and this light T share a letter without colliding. Editor-only by the charting-mark law; the 3D surfaces stay merged |
| — | **pick-slide toggle** — converts the selection to or from the scrape attack | **Live but UNBOUND** (`planSetAttack` + `ChartPickSlideToggle`), deliberately registered with **no default chord**: the verb shipped with plan 55 while the signed keymap never assigned it one, and inventing one in the registry would be an unsigned keymap decision. Reachable through the chart's context menu (the Actions dialog binds chords, it does not invoke — the walkthrough's W9-I states this correctly), and the user can bind it a chord there. **Needs a chord picked here** |
| `V` | **vibrato** | settled. `Alt+V` for a *wide* vibrato is an open possibility, not a decision — the field is a bool today, so a width distinction would need the format to carry one |
| `A` | **accent** | settled, conditional on `A` not being wanted elsewhere. Checked 2026-08-07: plain `A` and `;` are both unassigned everywhere in this matrix, the interaction model and the registry; "select all" would be `Ctrl+A`, which is a different chord, and the arpeggio reading is derived rather than authored so it needs no key |

**Open — do not bind before discussing:**

- **A chord for the pick-slide toggle.** The verb ships registered and enabled with no default,
  so it is the one live technique verb no key reaches. Checked 2026-08-10: `Ctrl+H` is claimed by
  the left-hand tap verb (registered that day), so the scrape needs its own chord rather than a
  variant of `H`.
- **`;` as an alias for accent.** The only argument for it is familiarity to Guitar Pro users, and
  that premise is unverified: the search that suggested it also claimed `[` was Guitar Pro's palm
  mute, which is wrong (`[` starts a repeat section there). Verify Guitar Pro's real accent key from
  a reliable source before adding the alias, since without the familiarity argument the alias has no
  purpose. Also weigh that a punctuation key chosen for muscle memory only transfers on layouts that
  place it identically.
- **palm mute, full mute, tap, slap, pop, tremolo picking, natural harmonic, pinch harmonic.** Eight
  verbs, no chords settled. Our own 2D marks supply obvious letters for three of them (the lettered
  plates read `T`, `S`, `P`) and the on-head X for full mute, but Guitar Pro's real map needs
  verifying first — see the reliability note above. Guitar Pro's technique shortcuts are also heavily
  **digit**-based, and plain digits are committed to fret entry here while `Shift`+anything means
  extend, so some divergence is forced rather than chosen.

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
occupied has no *useful* meaning (you change a note by retyping its fret, so `Insert` there would
just redundantly zero it; an automation point has nothing to overwrite). A plugin is the one object
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
`0`–`9`, `Insert`, `Alt+↑/↓`, `Delete`, `Alt`+click, click/double-click — fret vs value vs point vs
region are just what each surface's objects *are*.

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
  not `Alt`), NOT by removal. *(Reverses the earlier "Ctrl+T retired" note.)*
- **F — DECIDED: one named exception + create framing.** "Insert never mutates an existing object"
  gains exactly ONE named exception — a *filled plugin slot* (replace-with-confirm). The tone-row
  `Insert`-split is framed as a **create** (a new tone change at an empty region-interior; the
  objects are the boundaries, interiors are the empty gaps between them; `Insert` on an existing
  boundary no-ops) — so it stays *inside* the rule, no exception. Update the verb table + §9b together.
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
