# Plugin Window Review — Findings

Tracks the review of commit `d8f297a5` ("Partial implementation of plugin opening from click")
and the follow-up staged refactor of `PluginEditorWindow` → `PluginWindow` in
`rock-hero-common/audio/src/engine.cpp`. Each finding is worked through one at a time; this
document is the running checklist.

Status legend: `[ ]` open, `[~]` analysis done / awaiting action, `[x]` resolved.

---

## 1. `recreateEditorAsync` comment contradicts the code — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp:786-799`

**Observation.** The comment says the function "Defers editor recreation so plugin callbacks can
finish before the old component is removed," but the code synchronously destroys the old editor
via `setEditor(nullptr)` and only defers the *creation* of the replacement.

**Analysis.** The implementation is a direct port of Tracktion's reference example at
`external/tracktion_engine/examples/common/PluginWindow.h:200-209` and is correct. The
asynchrony is not about call-stack re-entrancy or paint timing — it is a sequencing
requirement against `ExternalPlugin::forceFullReinitialise()`
(`external/tracktion_engine/modules/tracktion_engine/plugins/external/tracktion_ExternalPlugin.cpp:707-714`):

```cpp
void ExternalPlugin::forceFullReinitialise()
{
    TransportControl::ScopedPlaybackRestarter restarter (edit.getTransport());
    engine.getUIBehaviour().recreatePluginWindowContentAsync (*this);  // our hook
    edit.getTransport().stop (false, true);
    fullyInitialised = false;
    initialiseFully();   // creates a NEW AudioPluginInstance
}
```

- The current `AudioPluginInstance` is about to be torn down, so the editor bound to it must be
  dropped synchronously now — that is what the immediate `setEditor(nullptr)` accomplishes.
- The replacement editor must be created *after* `initialiseFully()` has installed the new
  `AudioPluginInstance`. The timer delay (50 ms) lets `forceFullReinitialise` return and finish
  swapping the instance before `plugin.createEditor()` runs.

If the call were fully synchronous, `plugin.createEditor()` would run before the new instance
existed, binding the new editor to a dying or absent backend.

**Fix.** Replace the comment to describe the real intent:

```cpp
// Drops the current editor immediately because its underlying AudioPluginInstance is about to
// be destroyed (Tracktion calls this from forceFullReinitialise() right before swapping the
// instance). Creating the replacement editor is deferred so it binds to the new instance that
// forceFullReinitialise installs after this returns.
void recreateEditorAsync()
```

No code change required.

---

## 2. `pluginChanged()` fires on every `moved()` / `resized()` callback — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp:857-864`

**Observation.** `storeWindowBounds()` is called from both `moved()` and `resized()`, and each
call invokes `m_plugin.edit.pluginChanged(m_plugin)`. During a window drag this dirties the
edit and posts a change notification per mouse-move tick. `closeButtonPressed()` writes
`lastWindowBounds` directly and skips `pluginChanged()`, so the close path is inconsistent
with the drag path.

**Decision.** Store plugin window geometry at the project level (in `project.json` editor
state), not in Tracktion's `Edit`. This keeps the data out of `.rock` publishes by
construction. See "Project-level placement notes" below for the file shape and plumbing.

Concrete effect on `PluginWindow`:

- `moved()` / `resized()`: update `m_window_state.lastWindowBounds` only — used as
  in-session memory so reopening a closed plugin window restores its last position. No
  `pluginChanged()` call.
- `closeButtonPressed()`: route through `m_window_state.closeWindowExplicitly()` as before;
  do not call `pluginChanged()`.
- Persistence is handled by the editor controller snapshotting `lastWindowBounds` into
  `ProjectEditorState` at save time, and priming `lastWindowBounds` from
  `ProjectEditorState` at load time before any plugin window opens. Tracktion's
  `choosePositionForPluginWindow()` then naturally returns the stored position on first
  open.
- The existing construction-time guard (`m_update_stored_bounds`) is kept for the same
  reason — to avoid clobbering the initial position chosen by Tracktion during ctor.
- `closeButtonPressed()` and the destructor no longer need to write `lastWindowBounds`;
  `moved()` / `resized()` already keep it current.

Per-tick dirtying is eliminated because `pluginChanged()` is no longer called from any
`PluginWindow` path.

The trade-off: the editor needs a separate "project dirty" signal for window-geometry
changes, since Tracktion's edit-dirty channel no longer carries this. That signal will need
to bridge from `PluginWindow` (or its `lastWindowBounds` consumer in the controller) up to
whatever owns the project's unsaved-changes state.

**Alternative considered and rejected:** keep the data in Tracktion's `Edit` and decouple
`lastWindowBounds` updates from `pluginChanged()` firing (writing the bounds on every
`moved`/`resized`, but firing `pluginChanged()` only on close). Simpler in plumbing but
keeps editor-only window geometry inside song data, which would also flow through native
song packages unless explicitly stripped during publish.

**Resolution:**

1. `PluginWindow` local changes: dropped `pluginChanged()` from `storeWindowBounds()`;
   dropped redundant `lastWindowBounds = getBounds()` from `closeButtonPressed()`. The
   per-tick edit-dirty storm is eliminated. (engine.cpp)
2. Within-session restore is now position **and** size: the ctor restores
   `*m_window_state.lastWindowBounds` directly when the editor supports resizing, instead
   of consuming only `choosePositionForPluginWindow()`'s `Point`. (engine.cpp)
3. Tracktion's built-in `windowX` / `windowY` ValueTree persistence is severed as a
   side-effect of removing `pluginChanged()`, which means plugin window geometry no longer
   leaks into the song package or `.rock` — this is the desired behavior given the
   editor-only nature of this state.

Cross-session restore (persist position/size/open-state to `project.json`) is deferred and
tracked separately in `docs/todo/plugin-window-persistence.md`. The "Project-level
placement notes" section below remains as design context for that future work.

### Project-level placement notes

The `.rhp` package extracts to a workspace with `song/` (gameplay data; mirrors `.rock`
contents) and `project.json` at the root (editor-only state, already excluded from publish).
Currently `project.json` holds `cursorPosition` and `selectedArrangement` via
`project_io::readProjectDocument` / `writeProjectDocument`. Plugin window geometry will live
in a new section of this same file.

**Chosen shape — Option B (nested per arrangement):**

```json
{
  "cursorPosition": 12.34,
  "selectedArrangement": "arr-abc",
  "arrangementEditorState": {
    "arr-abc": {
      "pluginWindows": {
        "<instance_id>": { "x": 120, "y": 80, "w": 480, "h": 320 }
      }
    }
  }
}
```

Rationale for nesting (vs. flat-by-instance-id):

- Plugin instances are arrangement-scoped in the domain model. The signal chain lives in
  `Arrangement`. The file should reflect that ownership.
- The "consistency with existing flat shape" argument is weak: today's fields
  (`cursorPosition`, `selectedArrangement`) are single values, not multi-keyed maps. There
  is no precedent for multi-keyed project state in `project.json` yet — the precedent we
  set with this field will define the shape for future per-arrangement editor state.
- When an arrangement is deleted, removing one nested entry drops all of its plugin
  geometry naturally. A flat map would orphan entries that have to be swept by
  cross-referencing the song.
- The wrapper key `arrangementEditorState` is deliberately generic so future
  per-arrangement fields (zoom, scroll position, expanded panels, etc.) can join without
  re-architecting. This is shape modeling, not speculative pre-building.
- Instance IDs come from `tracktion::Plugin::itemID.toString()` (engine.cpp:381,
  engine.cpp:1814) and are project-unique UUIDs, so flat *would* work — but uniqueness
  doesn't justify discarding the natural arrangement scoping.

Note: `selectedArrangement` already stores the stable `Arrangement::id`
(`rock-hero-common/core/include/rock_hero/common/core/arrangement.h:64`, doc'd as
"Stable arrangement identifier used by project editor state"). The same identifier keys
`arrangementEditorState`.

**Plumbing:**

1. `ProjectEditorState` (`rock-hero-editor/core/include/rock_hero/editor/core/project.h`)
   gains a nested map:
   `std::unordered_map<std::string /*arrangement_id*/, ArrangementEditorState>`, where
   `ArrangementEditorState` holds
   `std::unordered_map<std::string /*instance_id*/, /*rect*/>`. The rect is a small
   headless type — no JUCE in `rock-hero-editor/core`, since this should stay testable
   without GUI.
2. `project_io::readProjectDocument` / `writeProjectDocument` round-trip the new section
   alongside the existing fields, using the same defensive parsing pattern.
3. Editor controller snapshots `PluginWindowState::lastWindowBounds` per plugin into
   `ProjectEditorState` at save time, keyed by the arrangement that owns each plugin.
4. At load time, the editor controller primes each `PluginWindowState::lastWindowBounds`
   from the deserialized map **before** any plugin window opens. Tracktion's
   `choosePositionForPluginWindow()` then naturally returns the stored position on first
   open.
5. `PluginWindow` itself remains as described in Finding 2 — writes `lastWindowBounds` on
   `moved`/`resized`, never calls `pluginChanged()`.
6. Project-dirty signaling: window-geometry changes need to mark the *project* dirty
   (separate from Tracktion's edit-dirty channel). Mechanism TBD — likely a
   controller-side observer that watches `lastWindowBounds` changes and bumps a
   project-dirty flag.

**Open question:** should the project-dirty signal fire per-tick on `moved()`/`resized()`,
or be coalesced (e.g., snapshot-on-close, debounced timer)? Same question Finding 2 raised
about `pluginChanged()`, now relocated to the project layer. The same debounce-or-on-close
answer likely applies — to be decided when the project-dirty mechanism is designed.

---

## 3. `setResizeLimits(100, 50, 4000, 4000)` is effectively dead when the editor supplies a constrainer — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp:764, 850-853`

**Original observation.** `setResizeLimits` configures the default constrainer, but
`setEditor()` immediately calls `setConstrainer(m_editor->getBoundsConstrainer())` whenever
the editor provides one, replacing the constrainer entirely. The limits only apply when the
editor returns no constrainer.

**Trace.** In `setEditor`: `setConstrainer(nullptr)` resets to the default constrainer (which
`setResizeLimits` configures); a non-null editor constrainer replaces it; a null editor
constrainer keeps the default. So:

| Editor allows resizing | Editor provides constrainer | Effective limits |
|---|---|---|
| Yes | Yes | Editor's own (our limits ignored) |
| Yes | No | Our `setResizeLimits` |
| No | — | Window not resizable; limits irrelevant |

The call is therefore a defensive backstop for editors that allow resizing but don't
supply a constrainer — matching the pattern in Tracktion's reference example
(`external/tracktion_engine/examples/common/PluginWindow.h:111`). Not dead code; the
original finding was a misread.

**Resolution.**

1. Added a clarifying comment in the ctor explaining `setResizeLimits` is a backstop for
   editors without their own constrainer, so future readers don't make the same misread.
2. Added `getConstrainer()->setMinimumOnscreenAmounts(0x10000, 50, 30, 50)` to match
   Tracktion's reference. The `0x10000` for the title bar is effectively infinity, forcing
   the full title bar to stay onscreen so the user can always drag a window back when
   restored bounds land on a missing monitor. JUCE's default onscreen-amount minimums are
   permissive enough to allow most of the title bar offscreen.

---

## 4. `getLocalBounds()` at the `setBoundsConstrained` call may be zero-sized — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp` — ctor `else` branch

**Original observation.** The expression `setBoundsConstrained(getLocalBounds() +
choosePositionForPluginWindow())` relies on `setContentNonOwned(..., true)` having already
sized the document window from the editor's bounds. Plugin editors that return a zero-sized
component would yield a 0×0 window. The previous code had a `480×320` fallback; that
fallback is gone.

**Trace.** `ResizableWindow::setBoundsConstrained`
(`juce_gui_basics/windows/juce_ResizableWindow.cpp:338-344`) delegates to the constrainer's
`setBoundsForComponent`, which enforces size limits. With Finding 3's
`setResizeLimits(100, 50, 4000, 4000)` configured on the default constrainer, a 0×0 rect
passed through `setBoundsConstrained` clamps to a 100×50 minimum, **not 0×0**.

So the original concern was overstated. The worst case for an editor that returns 0×0 and
supplies no constrainer of its own is a 100×50 window — small but functional. The case
where this would fail (editor with custom constrainer whose minimum is below 100×50 *and*
zero-sized bounds) is implausible: editors that supply constrainers do so to enforce
sensible minimums, not zeros. Tracktion's reference example doesn't add a fallback either
(it accepts whatever the editor returns, with `jmax(8, ...)` as a tiny floor).

**Resolution.** No code change. A clarifying comment was added in the ctor's `else` branch
explaining that an editor returning empty bounds is signaling "host, pick a size" and that
the `setResizeLimits` floor takes over. A 480×320 fallback was briefly added and then
reverted — it would have imposed an arbitrary minimum on real plugin editors that
legitimately want to be small (no principled basis for 480×320 vs. any other value), and
the constrainer floor already prevents truly degenerate windows.

If users report that 100×50 is too small for specific plugins in the wild, the principled
fix is to widen `setResizeLimits` rather than special-case the empty-bounds path.

---

## 5. `openPluginWindow` checks `isWindowShowing()` synchronously after `showWindowExplicitly()` — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp` — `Engine::openPluginWindow`
(introduced in commit `d8f297a5`)

**Original observation.** After calling `plugin->showWindowExplicitly()`, the function
immediately checks `!plugin->windowState->isWindowShowing()`. The concern was that this
check could be racy if Tracktion creates the window asynchronously.

**Trace.** `PluginWindowState::showWindowExplicitly()`
(`tracktion_PluginWindowState.cpp:54-60`) asserts message-thread, stops its timer, and calls
`showWindow()` synchronously. `showWindow()`
(`tracktion_PluginWindowState.cpp:110-150`) synchronously calls
`engine.getUIBehaviour().createPluginWindow(*this)` (which lands in our
`RockHeroUIBehaviour::createPluginWindow` → `PluginWindow::create`), then
`setVisible(true)` and `toFront(false)` — all on the same thread, no deferred work.

`Engine::openPluginWindow` already gates on `MessageManager::isThisTheMessageThread()` and
returns `MessageThreadRequired` if false, so by the time `showWindowExplicitly` runs we are
guaranteed on the message thread and the whole chain executes synchronously.
`isWindowShowing()` is therefore authoritative immediately after the call returns.

**Real failures the check catches:**
- A modal dialog is up — `showWindow()` returns early via `isDialogOpen()` without
  creating the window.
- `createPluginWindow` returned `nullptr`, which our `RockHeroUIBehaviour` does when
  `plugin.createEditor()` returns null (plugins without an editor, e.g. some utility
  plugins).

**Resolution.** No code change for the logic — the check is correct as written. A
clarifying comment was added above the check so future readers don't make the same misread
about async-ness. Original Finding 5 was a misread; the post-call check is reliable
because we already gated on the message thread.

---

## 6. Plugin row hover may flicker over the Remove button — `[x]`

**Location:** `rock-hero-editor/ui/src/signal_chain_panel.cpp` — plugin row component
(commit `d8f297a5`)

**Original observation.** `mouseEnter` / `mouseExit` are dispatched per-component. When the
pointer crosses from the row into the child `TextButton`, the row receives `mouseExit` and
clears its hover affordance, even though the cursor is still inside the row's bounds. The
highlight likely blinks off whenever the user reaches for "Remove."

**Trace.** JUCE's `MouseInputSourceImpl::setComponentUnderMouse`
(`juce_gui_basics/detail/juce_MouseInputSourceImpl.h:247`) does fire `mouseExit` on the
parent when the cursor moves into a child component. So the theoretical concern is real:
`m_is_hovered` goes false when the cursor enters the Remove button. **But** user testing
showed no perceptible flicker — the row transitions from highlighted to non-highlighted in
the steady state when hovering the button, which reads as a normal "highlight follows
direct cursor target" interaction rather than a flicker.

**Resolution — design change instead of mouse-event juggling.** Rather than fighting JUCE's
event semantics, shrink the highlight area so it only covers the clickable label region.
The Remove button is visually beside the highlight, not inside it. The two interactive
zones never overlap visually, so any hover state change at their boundary reads as
"different control, different state" rather than "same control flickering."

Code change (`signal_chain_panel.cpp`):

- `paint()` now computes `highlight_area = getLocalBounds() - (button width + gap)` and
  paints the background, accent strip, border, and label only within that area. The Remove
  button continues to position itself at the right edge in `resized()`, sitting visually
  outside the highlight.
- No change to `mouseUp` — the entire row is still the click target for "open plugin,"
  which keeps the existing UI test valid (it clicks at row-local (4, 4), well within the
  shrunken highlight). The small dead zone between highlight and button (~6 px) is
  visually breathing room; clicks there still register as "open plugin," which is the
  expected default for the row.
- `addMouseListener(this, true)` was briefly added under the mistaken assumption that the
  original flicker concern was real, then reverted.

The original Finding 6 concern was a misread; the design change addresses the *actual*
visual ambiguity (two overlapping interactive zones with shared hover state) cleanly.

---

## 7. `recreateEditor()` double-clears — `[x]`

**Location:** `rock-hero-common/audio/src/engine.cpp` — `PluginWindow::recreateEditor`

**Original observation.** `recreateEditor()` calls `setEditor(nullptr)` and then
`setEditor(m_plugin.createEditor())`. Since `setEditor()` already nulls and resets the
existing editor at the start of its body, the first call is redundant.

**Trace.**
- `recreateEditor()` is only called from the lambda inside `recreateEditorAsync()`, which
  already does a synchronous `setEditor(nullptr)` before scheduling the timer. By the time
  the timer fires and `recreateEditor()` runs, `m_editor` is already null.
- `setEditor(newEditor)` itself begins with `setConstrainer(nullptr);
  clearContentComponent(); m_editor.reset();` — it always clears existing state before
  installing a new editor.

So the first `setEditor(nullptr)` in `recreateEditor()` is provably dead in every call
path.

**Resolution.** Dropped the redundant call. `recreateEditor()` now reduces to one line plus
a clarifying comment explaining that `setEditor()` handles the clear internally. Tracktion's
reference example has the same double-call, but it's just minor imprecision there — no
behavior to preserve, and our `PluginWindow` already diverges from the reference enough
that one less line of incidental similarity is not material.

---

## Working order

All seven findings have been worked through. Findings 1, 3, 4, 5, 6, 7 are fully resolved.
Finding 2's `PluginWindow` local change (the per-tick `pluginChanged()` storm) is resolved,
and within-session position **and** size restoration is implemented; cross-session
persistence is deferred and tracked in `docs/todo/plugin-window-persistence.md`.

Findings 3, 4, 5 turned out to be misreads of the code — the original concerns didn't
manifest, and the trace through Tracktion/JUCE source clarified why. Each closure includes
a comment in the code so the same misread doesn't recur. Finding 6 was also a misread on
the flicker side, but produced a real design improvement (highlight area shrunk so the
clickable label region and Remove button are visually distinct, with a 12 px gap).