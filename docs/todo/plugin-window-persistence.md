# Plugin Window State Persistence

Persist plugin window state across editor sessions so charters return to the layout they
left behind when reopening a project. Editor-only concern; must never leak into native song
packages or `.rock`.

## Background

In-session restore is already in place. `PluginWindow` (`rock-hero-common/audio/src/engine.cpp`)
writes `tracktion::PluginWindowState::lastWindowBounds` on `moved()` / `resized()`, and the
ctor's `chooseInitialBounds()` helper consumes both the position and the size on reopen when
the editor supports resizing. Within one editor run, close → reopen restores both position
and size.

What's missing is *cross-session* restore. After project close + reopen, `lastWindowBounds`
is empty again because nothing serializes it. Tracktion's built-in `windowX` / `windowY`
ValueTree persistence is no longer reached (we deliberately severed the `pluginChanged()`
call to keep editor-only data out of the song package — see Finding 2 in
`docs/completed/plugin-window-review.md`), and `windowW` / `windowH` aren't part of
Tracktion's built-in persistence at all.

## What to persist

For each plugin instance:

1. **Position** — `x`, `y`. Currently survives in-session via `lastWindowBounds`.
2. **Size** — `w`, `h`. Currently survives in-session via the `chooseInitialBounds()`
   helper but only when the editor allows resizing.
3. **Open/closed state** — was the window open when the project was last saved? On
   project load, reopen any window that was open at save time.

The third is the most opinionated. Some DAWs always reopen previously-open plugin windows
(REAPER, Cubase); others always start with all windows closed (older Logic). Reopening is
the more useful default for a charter coming back to in-progress work.

## File shape

`project.json` editor state, Option B (nested per arrangement). The shape was chosen in
`docs/completed/plugin-window-review.md`:

```json
{
  "cursorPosition": 12.34,
  "selectedArrangement": "arr-abc",
  "arrangementEditorState": {
    "arr-abc": {
      "pluginWindows": {
        "<instance_id>": {
          "x": 120, "y": 80, "w": 480, "h": 320,
          "open": true
        }
      }
    }
  }
}
```

The wrapper key `arrangementEditorState` is deliberately generic so future per-arrangement
fields (zoom, scroll position, expanded panels) can join without re-architecting.

## Plumbing checklist

- [ ] Extend `ProjectEditorState`
  (`rock-hero-editor/core/include/rock_hero/editor/core/project.h`) with a nested
  `std::unordered_map<std::string /*arrangement_id*/, ArrangementEditorState>`, where
  `ArrangementEditorState` holds
  `std::unordered_map<std::string /*instance_id*/, PluginWindowState>` with
  `{ int x, y, w, h; bool open; }` fields. Use a headless rect type — no JUCE in
  `rock-hero-editor/core`.
- [ ] Extend `project_io::readProjectDocument` / `writeProjectDocument`
  (`rock-hero-editor/core/src/project_io.cpp`) to round-trip the new section using the
  defensive parsing pattern already established for `cursorPosition` and
  `selectedArrangement`.
- [ ] Engine API: expose plugin window state for snapshot/restore through `IPluginHost` (or
  a small adjacent port) so the editor controller can read/write `lastWindowBounds` and
  `isWindowShowing()` per plugin without taking a Tracktion dependency. Likely:
  - `getPluginWindowState(instance_id) -> { rect, open }`
  - `setPluginWindowState(instance_id, { rect, open })`
- [ ] Editor controller: at save time, walk each arrangement's plugins and snapshot into
  `ProjectEditorState`. At load time, prime each plugin's window state **before** opening
  any windows, then reopen those marked `open`.
- [ ] Project-dirty signaling: window-geometry/open-state changes need to mark the
  project unsaved (separate channel from Tracktion's edit-dirty). Mechanism TBD — likely a
  controller-side observer or an explicit "project state changed" intent.
- [ ] Tests:
  - `project.json` round-trip for the new field (`project_io` tests).
  - `ProjectEditorState` equality / serialization tests for the nested map.
  - Controller-level snapshot/restore behavior tests with a fake `IPluginHost`.

## Design questions to decide when picking this up

- **Open/closed on first import.** When importing a song into a new project, what's the
  default `open` state? Probably "all closed" — the charter hasn't expressed a preference
  yet.
- **Multi-monitor safety.** If the saved rect lies on a monitor that no longer exists,
  clamp to the primary display or accept the off-screen position and trust the user to
  drag it back? Existing JUCE constrainers can handle this with `setMinimumOnscreenAmounts`.
- **Stale instance IDs.** If a plugin was removed between save and load (e.g., manual file
  edit), drop the orphaned `pluginWindows` entry silently at load time.
- **Project-dirty cadence.** Per-tick on `moved()`/`resized()` produces a notification
  storm; debounce or fire-on-close. Same trade-off as the original Finding 2 discussion,
  now relocated to the project layer.
- **Rack windows / plugin sub-windows.** `RockHeroUIBehaviour::createPluginWindow` currently
  only handles `Plugin::WindowState`, not `RackType::WindowState`. If rack windows ever
  get UI, decide whether their geometry persists through the same mechanism or a separate
  one.

## Out of scope

- `.rock` and native song packages must never carry any of this state. The current
  architecture already enforces this — editor-only state lives in `project.json` at the
  workspace root, not under `song/` which is what `publish()` consumes.
- Tracktion's own ValueTree `windowX` / `windowY` properties on plugins. We deliberately
  stopped writing those (by removing the `pluginChanged()` call in `storeWindowBounds`).
  Do not re-enable that channel; it would leak window data into song packages.
