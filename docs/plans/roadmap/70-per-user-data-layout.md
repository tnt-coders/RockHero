# Plan 70 — Per-User Data Layout

**Status:** Drafted 2026-10-11 against `master @ 9aec2de0`. **Phases 1 and 2 landed 2026-10-11;**
Phase 3 (the user's by-hand moves and runtime check) is pending. The structure was decided by
the user on 2026-10-11 after two simplicity reviews, the second of which removed a proposed new
library; the user approved the resulting design-rule change the same day. One name waits on plan
68's family-name decision (70-Q1). It does not depend on the product rename and is kept separate
from it.

## Goal

Both apps' per-user data becomes one tidy, predictable tree with every location named in one
place, and the settings both apps share live in one file that both apps lock while writing.

## The sharing test

A setting is **shared** when it describes the player or the machine, so the two apps must never
disagree about it; it is **per app** when it describes one app's workspace. A shared datum is a
`common` type resolved the same way by both apps; that is necessary but not sufficient
(`minimum_string_count` is a common field but describes the editor's tab lane, so it stays per app).
The file boundary makes the rule structural: an app cannot read a per-app file it never opens.

| Setting | Home |
|---|---|
| Audio device route, input calibration | shared file, `AudioConfigStore`'s keys |
| Highway theme id; the palette, head-shape and inlay pins ("Theme's choice" when absent); left-handed mirroring; string order; plan 69's ring, rotation and bracket toggles | shared file, `highway.*` keys |
| Tab minimum strings, waveform visibility, keymap, last and interrupted project, tone-file folder, per-project markers, zoom, grid and arrangement, the editor's chrome theme | editor (`editor.settings`) |
| Profile, first run, custom scan roots, input-slot routes | game (`game.settings`) |

## The tree

```
<family folder>/                      %APPDATA% on Windows, Application Support on macOS
  settings/  shared.settings  game.settings  editor.settings
  logs/      game.log  editor.log  (+ rolling .1 to .5)
  themes/<id>/theme.json …            user themes (plan 54)
  songs/                              user content
  tracktion/                          Tracktion's own Settings.xml and plugin-scan file
<temp>/<family folder>/  game-sessions/  project-workspaces/   scratch, not Roaming
```

- **Names:** the folder carries the family name; every file and subfolder is one lowercase word
  for what it holds. Each app has ONE product token in `application_identity.h` (`"game"`,
  `"editor"`; the user's 2026-10-11 choice of today's names while plan 68's rename is parked)
  naming both its settings file and its log, so the pair cannot drift and no app display name
  appears in a saved path. A later rename changes the two tokens, one value each, and the user
  renames two files once, free before the first release. `.settings` stays the suffix (JUCE writes XML; that is its detail).
- **One authority for every location:** `common/core/shared/user_data_folders.h` (`juce_core`
  only) returns the user-data, settings, logs, themes and scratch folders. Today the path is
  spelled at six sites (both apps' `main.cpp`, the theme registry, `settings_file_options.h`, and
  the editor's project temp root, which even spells it differently). `settingsFileOptions()` loses
  its parameter and names no file: options are the format policy, folders are the location.
  Scratch moves under the temp directory, which also clears the game's orphaned
  `game-sessions/` out of Roaming; the editor's `project.cpp` temp root and the game's session
  root both become `scratchFolder()`.

## The shared file

- **The mechanism, stated once and knowing no area:** `common/core/shared/shared_settings_file.h`
  (+ `.cpp`) holds the path, one `juce::InterProcessLock` and the options, with two operations:
  `open()` returns a fresh `PropertiesFile`, and `whileLocked(fn)` runs `fn` on a fresh file under
  the held lock, returning nothing when the lock cannot be taken (each store maps that to its own
  "could not save" error); JUCE's lock and file can be neither copied nor moved, so a callback is
  the honest shape. The lock's name is a fixed prefix plus a hash of the file's full path, since
  JUCE turns the name into a lock file on macOS and Linux. It is the lock-then-open pattern the audio store carries today
  (`audio_config_store.h/.cpp`) extracted, so a second store does not restate it. Both apps run
  at once, so every shared operation opens fresh under the lock.
- **Composition:** each `main` builds ONE `SharedSettingsFile` from the folder authority and hands
  it by reference to both stores; tests build one at a temporary path. One lock object per process.
- **Each area lives with its own types:**
  - `AudioConfigStore` stays in `common/audio` with its port unchanged; its constructor takes the
    shared file and its keys gain the `audio.` prefix. Its own lock, options and
    file members, deleted special members and extra constructors go.
  - The highway area is new and lives in `common/core/highway/` beside `HighwayDisplayOptions`:
    the value type `HighwayPlayerSettings` and the concrete `HighwaySettingsStore` (no port yet:
    nothing takes one, so it is extracted with its first consumer, plan 54 Phase 2b or plan 69
    Phase 2). Not `common/ui` (UI owns no persistence), not
    `common/audio` (the wrong home).
- **Keys:** the highway store writes flat prefixed keys through `PropertiesFile`'s typed accessors
  (`highway.theme`, `highway.mirrored`, …): no XML record, no version attribute. A corrupt or
  absent key resets only itself, the tolerance both per-app stores already use; an absent pin is
  "Theme's choice" by `std::optional`. Each store keeps its keys as one constexpr set under its
  prefix, which is the whole collision defense.
- **The highway store:** `read() const` returns `HighwayPlayerSettings`; `update(fn)` reads, applies
  `fn`, and writes under the lock, returning the new record or an error. A whole-record write from
  a cached copy would lose the other running app's change (the game sets the theme, the editor
  sets mirroring from a stale record, and the theme is lost); per-field setters would restate the
  same read-modify-write for every field. Callbacks stay trivial because the lock is held across
  them.
- **One field list for the view preferences:** `HighwayViewPreferences` holds the player's shared
  reading choices (mirrored, string order, plan 69's three toggles). It is nested once in the
  renderer's `HighwayDisplayOptions { view, minimum_string_count }` and once in
  `HighwayPlayerSettings { theme, view }`; the three optional pins join it with plan 54 Phase
  2b, their first reader. The renderer never sees a theme id
  or pins it does not read, and the editor-only lane minimum never enters the shared file. The
  game's hard-coded string order and the editor preview's become reads of the record, and the
  game's `--lefty` developer flag is deleted: a per-run override would be a second source for
  mirroring, so until plan 69's menu writes it, a left-handed tester sets `highway.mirrored` in the
  shared file. `HighwayViewPreferences{}` itself defaults to the lowest string on top, stated once.
- **Per-app files** keep their held `PropertiesFile` and no lock; only their names and folder
  change.
- **Design-rule change, approved by the user 2026-10-11:** `common/core` links the
  `juce_data_structures` properties types PRIVATE as well as INTERFACE, so its own sources may
  open a properties file. The grant's stated test, headless testability, is unchanged (the audio
  store's open-fresh tests already run headless); only the interface-only guard goes.
  `docs/design/architectural-principles.md` and CLAUDE.md's dependency rules are updated in
  Phase 1.

## Non-goals

- **The product rename** (plan 68), the tree and namespace scheme, and any repository split.
- **Migration code.** Pre-release, nothing reads the old files (the no-legacy rule). The user moves
  the audio calibration and the editor's keymap by hand once (Phase 3).
- **Live sync** between two running apps; each reads the shared file at launch and on its own
  operations.

## Open questions

- **70-Q1 — the family folder's name.** Today `Rock Hero/`, which will name only the game. Waits on
  plan 68's family-name decision; one value in the identity header either way, and free to change
  before the first release.
- **70-Q2 — Tracktion's folder and temp files.** Reading Tracktion's source confirms its
  application name only picks its preferences folder (and one dead ReWire string), so passing
  `<family>/tracktion` is safe. *As built:* a `PropertyStorage` subclass overrides its settings
  and cache folders (`setTempDirectory` would first create a Roaming temp folder and store its
  path in Settings.xml), and the engine takes its folders as an argument so tests run Tracktion on
  temporary folders. Runtime check: both apps and a plugin rescan, in Phase 3.
- **macOS location (found 2026-10-11).** On macOS JUCE's `userApplicationDataDirectory` is
  `~/Library`, while `PropertiesFile` writes under `~/Library/Application Support`, so today's
  logs and plan 54's user themes folder land beside, not inside, the settings' folder there.
  `user_data_folders.h` resolves the family folder once, so every location agrees on every
  platform; verified on macOS when it is first packaged.
- **70-Q3 — logs in Roaming.** Rolling logs (up to about 40 MB) stay under Roaming, because the only
  per-machine alternative is a Windows-only location and the platform rule outranks it. Watch item
  "Logs in Roaming".

## Phases

1. **The folder authority and the shared mechanism.** `user_data_folders.h`, the two product
   tokens, `SharedSettingsFile`, `settingsFileOptions()` without its parameter, every site routed
   through the folder authority, the audio store on the shared file, the design-doc rule update.
   One test-support copy of the scoped settings file (today written twice, in the audio and editor
   test trees). Tests: the folder functions; the shared file's lock and fresh open; the audio
   store's existing suite on the shared file. **Verification:** configure, build, every suite.
2. **The highway area.** `HighwayViewPreferences`, `HighwayPlayerSettings` and its store, with
   round-trip, corrupt-key and concurrent-update tests; both apps read
   the theme id, mirroring and string order from it. Plan 54 Phase 2b's pins and plan 69 Phase 2's
   toggles build on it directly. **Verification:** build, touched tests.
3. **On the user's machine (no commit):** the moves listed in the start-here sighting queue (the
   old audio file becomes `settings/shared.settings` with its two keys renamed to `audio.*`, the
   editor's file becomes `settings/editor.settings`, Tracktion's two files move into `tracktion/`,
   `Songs/` becomes `songs/`), delete the rest, run both apps and a plugin rescan (70-Q2).
   **STOP: both apps launch, keep calibration and keymap, and write only the new tree.**

## Plan text this changes

- **Plan 54:** "The player (settings, per product)" and constraint (a) become "persistence is the
  shared highway keys; the picker is per product"; 54-Q18's "one per product"; Phase 2a ships no
  theme-id setting (the default theme is used until this plan's Phase 2); Phase 3's private-theme
  A/B follows this plan's Phase 2.
- **Plan 45:** decision 7 ("the ids match, the stores do not") is reversed for the highway's player
  settings.
- **Plan 69:** 69-Q1 is answered (the shared `highway.*` keys); its toggles are fields of
  `HighwayViewPreferences`.
- **Plan 68:** its §3.5 and Phase 2 log and settings names follow this plan; 68-Q3 is 70-Q1.
