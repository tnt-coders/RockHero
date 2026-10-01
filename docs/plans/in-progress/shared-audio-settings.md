# Shared Audio Settings

Status: in progress (user, 2026-10-01). Reverses plan 13's per-app audio stores and the related
roadmap decision (RM-4's storage half and 13-Q3): the editor and the game share ONE audio-settings
file. RM-4's runtime half stands unchanged: one active ASIO client at a time, and the second app
runs the silent device.

## Why

The file stores two authored facts: the device route (JUCE's restore blob) and, per physical input
route, one measured calibration gain (later also latency offsets). Neither is per-product. The
calibration target is a fixed level, so for the same guitar, interface and channel the correct
gain is the same number in both apps: two copies can only agree or be wrong, and a chart authored
at one gain and played at another is exactly the inconsistency calibration exists to prevent.

The split's recorded reasons do not hold:

- **"One writer per file, so no InterProcessLock."** Both apps already share Tracktion's own
  `Rock Hero/Settings.xml`, which records the device choice on every change with no lock, and opens
  the other app's last device at startup. JUCE's `PropertiesFile::Options::processLock` covers two
  writers in a few lines.
- **Per-product device settings "by directive".** No product reason was recorded; the editor also
  monitors the guitar live and wants the same low latency, and latency offsets depend on the
  buffer size, so splitting the route would split that calibration too.
- **Authoring against the player's setup.** One store gives it automatically, both ways, with no
  toggle.

The analysis behind this plan (Fable, 2026-10-01) found no strong reason to keep them separate.

## Design

- **One file:** `Rock Hero/Rock Hero Audio.settings`, through `settingsFileOptions`. Contents: the
  device route blob, and the route-keyed calibration list (`KeyedRecordStore<InputCalibrationCodec>`,
  unchanged).
- **One store:** `AudioConfigStore`, constructed identically by both composition roots and injected
  as the plain `IAudioConfigStore&` the monitor and controllers already take. No read-only mode,
  no `fileFor`, no per-app name.
- **Concurrency:** every operation opens the file fresh under a JUCE `InterProcessLock`; a write
  holds the lock across its read-modify-write, so neither app clobbers the other's key or works
  from a stale copy.
- **The route is the blob alone.** The input identity stored beside it had readers only in the
  mirror; the live identity always comes from the hardware port.
- **Deleted:** the editor's delegating store and its read-only game view, the "use game audio
  settings" toggle and its view and settings state, the startup recommendation dialog and the
  game-unavailable notice, the read-only modes of the device and calibration windows, and the
  game's `--import-editor-audio` path.
- **No migration** (pre-release): the two old per-app audio files are no longer read, and are
  deleted from the developer machine once this lands.

## Phases

1. One shared store, and the mirror machinery deleted (they must land together).
2. The route becomes the blob alone.
3. Documentation: plans 13, 14, 31, 32 and 48, the roadmap's RM-4 and 13-Q3, the developer guide,
   the settings-file policy comments.
