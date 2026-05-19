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

## 2. `pluginChanged()` fires on every `moved()` / `resized()` callback — `[~]`

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

**Action:** implement the changes to `PluginWindow` above (local change). Project-level
persistence work is tracked in "Project-level placement notes" below.

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

## 3. `setResizeLimits(100, 50, 4000, 4000)` is effectively dead when the editor supplies a constrainer — `[ ]`

**Location:** `rock-hero-common/audio/src/engine.cpp:764, 850-853`

**Observation.** `setResizeLimits` configures the default constrainer, but `setEditor()`
immediately calls `setConstrainer(m_editor->getBoundsConstrainer())` whenever the editor
provides one, replacing the constrainer entirely. The limits only apply when the editor
returns no constrainer.

**Action:** confirm whether this is intentional (limits as a safety net for editors without a
constrainer) or whether the limits should be applied to whichever constrainer is active.

---

## 4. `getLocalBounds()` at the `setBoundsConstrained` call may be zero-sized — `[ ]`

**Location:** `rock-hero-common/audio/src/engine.cpp:766`

**Observation.** The expression `setBoundsConstrained(getLocalBounds() + choosePositionForPluginWindow())`
relies on `setContentNonOwned(..., true)` having already sized the document window from the
editor's bounds. Plugin editors that return a zero-sized component (some do, expecting the host
to size them) would yield a 0×0 window at the Tracktion-chosen position. The previous code
had an explicit `480×320` fallback; that fallback is gone.

**Open questions.**
- Does Tracktion's reference example exhibit the same gap? (It calls `setBoundsConstrained`
  with the same shape after `recreateEditor`, suggesting Tracktion accepts the gap or relies on
  the content's own `resizeToFitEditor()` call.)
- Have we observed any plugin in practice that returns an empty editor here?

**Action:** decide whether to restore the fallback size or trust the editor + content sizing path.

---

## 5. `openPluginWindow` checks `isWindowShowing()` synchronously after `showWindowExplicitly()` — `[ ]`

**Location:** `rock-hero-common/audio/src/engine.cpp` — `Engine::openPluginWindow`
(introduced in commit `d8f297a5`)

**Observation.** After calling `plugin->showWindowExplicitly()`, the function immediately
checks `!plugin->windowState->isWindowShowing()` and returns `PluginWindowUnavailable` if
false. Tracktion's `showWindowExplicitly()` is documented to trigger asynchronous window
creation; if the window comes up via the message loop on a subsequent tick, this check would
report a spurious failure even when the open succeeds.

**Observed behavior.** The user reports "it seems to work okay," which is consistent either
with synchronous window creation when invoked from the message thread, or with the error path
firing silently because the controller surfaces the message as a toast that hasn't been
noticed.

**Action:**
- Confirm whether `showWindowExplicitly()` creates the window synchronously when called from
  the message thread (read Tracktion's `PluginWindowState` implementation).
- If async, drop the post-call check or replace it with a non-blocking validation
  (e.g., `plugin->windowState != nullptr`).

---

## 6. Plugin row hover may flicker over the Remove button — `[ ]`

**Location:** `rock-hero-editor/ui/src/signal_chain_panel.cpp` — plugin row component
(commit `d8f297a5`)

**Observation.** `mouseEnter` / `mouseExit` are dispatched per-component. When the pointer
crosses from the row into the child `TextButton`, the row receives `mouseExit` and clears its
hover affordance, even though the cursor is still inside the row's bounds. The highlight likely
blinks off whenever the user reaches for "Remove."

**Action:**
- Verify visually whether the flicker is noticeable.
- If so, gate the hover state on the recursive `isMouseOver(true)` form, or repaint on
  enter/exit events bubbled from children via a `MouseListener`.

---

## 7. `recreateEditor()` double-clears — `[ ]`

**Location:** `rock-hero-common/audio/src/engine.cpp:779-783`

**Observation.** `recreateEditor()` calls `setEditor(nullptr)` and then
`setEditor(m_plugin.createEditor())`. Since `setEditor()` already nulls and resets the existing
editor at the start of its body, the first call is redundant.

**Note.** Tracktion's reference example has the same double-call. It is harmless.

**Action:** decide whether to leave it for parity with the reference example or drop the
redundant call for clarity.

---

## Working order

We are stepping through these one at a time in the order above. Finding 1 is resolved.
Finding 2 has a decision (project-level placement, Option B nested shape) but is not yet
implemented; implementation is gated on the project-dirty signaling design and the
`ProjectEditorState` / `project_io` extension. Findings 3–7 remain open.