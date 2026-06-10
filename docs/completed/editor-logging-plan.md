# Editor Logging Implementation Plan

Status: completed (superseded approach), retired from `docs/in-progress/` in Phase 0 of
`editor-engine-undo-master-plan-v2.md`. The durable, file-backed editor logging this plan called for
is implemented, but by a different mechanism than the plan specified. Retained as a historical
record; see Implementation Result below for what actually shipped.

## Implementation Result

The plan's intent — durable, file-backed editor logging installed before the audio engine and
window, with a shared timestamp/severity/category log-line shape usable by both editor and game, and
existing `juce::Logger::writeToLog` calls becoming file-backed — is satisfied by the Quill-backed
`Logger` facade in `rock-hero-common/core`, not by the `juce::FileLogger` mechanism this plan
described.

What shipped:

- `rock_hero::common::core::Logger`, a Quill-backed facade with a durable `RotatingFileSink`,
  category loggers, and `RH_LOG_*` macros, installed via `Logger::init(...)` from the editor app
  composition root (`rock-hero-editor/app/main.cpp`) before the audio engine and editor window.
- `JuceQuillBridge` (a `juce::Logger`) routes framework `juce::Logger::writeToLog` output into the
  same Quill pipeline, so existing project-owned and JUCE/Tracktion diagnostics land in the durable
  log under a shared `time [level] category: message` pattern.
- The facade lives in `common/core`, so the editor and game share one formatter and log vocabulary.

Deliberate departures from this plan:

- **Third-party backend adopted.** This plan's "Do not add a third-party logging backend" non-goal
  was superseded; Quill is now the project logging backend, chosen for structured, category-based,
  realtime-aware logging after this plan was written.
- **Mechanism changed** from `juce::FileLogger` plus a hand-written common-core formatter to Quill's
  `RotatingFileSink` and `PatternFormatterOptions`.

Still deferred (tracked elsewhere, not regressions of this plan):

- Game-app logging install parity (see the deferred list in `editor-engine-undo-master-plan-v2.md`).
- The `docs/todo/core-domain-logging-targets-plan.md` target split remains out of scope.
- Undo-specific action/event logging is built with the undo stages in `editor-undo-plan.md` via
  `RH_LOG_*` directly; no separate diagnostics interface is introduced.

---

The original plan is retained verbatim below as a historical record.

Original scope: install a durable, file-backed logger for the editor app and establish the shared
timestamp/severity log-line formatter that both the editor and game can use. The structured
diagnostics surface, action/undo-event logging, and the rollback-contract diagnostic are no longer
part of this plan; they are built incrementally inside `editor-undo-plan.md`, each alongside the undo
stage that uses it.

## Current State

Rock Hero already has a few ad hoc `juce::Logger::writeToLog` calls:

- editor best-effort cleanup and persistence failures in
  `rock-hero-editor/core/src/editor_controller.cpp`;
- plugin catalog scan, plugin validation, and instrument-monitoring diagnostics in
  `rock-hero-common/audio/src/engine.cpp`.

No project-owned code installs a `juce::FileLogger` via `juce::Logger::setCurrentLogger`, so today
those calls are not a durable file log.

The broader `docs/todo/core-domain-logging-targets-plan.md` is out of scope and appears stale
relative to the current `rock-hero-common` / `rock-hero-editor` / `rock-hero-game` layout. Do not
implement it here.

## Goal

Make editor logging durable and establish the shared log-line shape:

- normal editor logs are written to a predictable per-user file;
- existing project-owned `juce::Logger::writeToLog` calls move through a shared timestamp/severity
  helper and become file-backed once the logger is installed.

The `IEditorDiagnostics` surface, action/undo-event logging, and the rollback-contract diagnostic
all live in `editor-undo-plan.md` and are built stage by stage with the undo work. They are
intentionally not part of this plan; the durable file logger here is what makes all of that output
persistent.

## Non-Goals

- Do not add a third-party logging backend.
- Do not implement the broader `core-domain-logging-targets` split.
- Do not add real-time audio-thread logging.
- Do not thread logger dependencies through pure domain objects.
- Do not build the diagnostics surface or rollback-contract handling here.

## Log Location

Use JUCE's platform log/app-data conventions:

- normal log: `juce::FileLogger::getSystemLogFileFolder() / "Rock Hero" / "Rock Hero Editor.log"`.

On Windows, JUCE resolves the system log folder under `juce::File::userApplicationDataDirectory`,
which matches the `"Rock Hero"` folder that already stores editor settings.

Use one rolling normal log file; it is easier for users and developers to find than a pile of session
logs.

## Normal File Logger

Install a `juce::FileLogger` in the editor app composition root (`rock-hero-editor/app`) before
constructing the audio engine or editor window.

App ownership shape:

- add `std::unique_ptr<juce::FileLogger> m_file_logger` to `RockHeroEditorApplication`;
- create it during `initialise` before other app services;
- call `juce::Logger::setCurrentLogger(m_file_logger.get())` after creation succeeds;
- call `juce::Logger::setCurrentLogger(nullptr)` before destroying it in `shutdown`;
- log app startup and shutdown messages.

Use a bounded initial file size (for example 1-2 MiB) so a stale log cannot grow forever before the
next startup trims it. If logger creation fails, continue running on JUCE's default debug output; do
not fail app startup because normal logging could not be opened.

## Shared Log Format

Keep `juce::FileLogger` as the process file sink, but route project-owned log records through
`rock-hero-common/core` so the editor and game share formatting:

- severity enum: `Debug`, `Info`, `Warning`, `Error`, and `Fatal`;
- category string, such as `editor.app`, `editor.controller`, or `audio.plugin_validation`;
- human-readable message;
- optional structured fields rendered as escaped key-value pairs.

Each record is one physical line:

```text
2026-06-08T15:24:10.123-04:00 [INFO] editor.app: Rock Hero Editor started log_file="..."
```

`Fatal` is only a severity label. The caller owns any faulting, shutdown, or abort policy.

## Privacy And Size Rules

Logs are local developer/user diagnostics. Logging project and plugin paths is acceptable; those are
already needed to debug plugin/project failures. Do not log raw audio, opaque plugin-state bytes,
full project package contents, unbounded catalog dumps, or high-frequency meter/transport samples.
For large state, log ids, counts, hashes, and compact snapshots. These rules apply to all later
logging built in the undo plan, not just this file logger.

## Tests

Prefer focused helper tests over launching the full JUCE application:

- file-logger setup chooses the expected `"Rock Hero"` subdirectory and log filename;
- shutdown clears `juce::Logger::getCurrentLogger()` before destroying the logger;
- shared log formatting includes timestamp, severity, category, message, and escaped fields;
- existing project-owned diagnostic calls are file-backed after startup setup.

## Implementation Stages

1. Add editor log-path helpers and install `juce::FileLogger` from the editor app composition root,
   with the bounded size and creation-failure fallback above.
2. Add shared common-core log formatting and dispatch helpers.
3. Convert existing project-owned raw JUCE logger calls to the shared helper.

That is the whole plan. The diagnostics surface, action/undo-event logging, and the rollback-contract
diagnostic are added in `editor-undo-plan.md` as their stages arrive.

## Acceptance Criteria

- Starting the editor installs a file-backed logger before the audio engine and editor window are
  constructed.
- Existing project-owned logger calls write timestamped, severity-tagged records to the normal
  editor log once the logger is installed.
- No diagnostics surface, rollback-contract handling, target split, or third-party backend is
  introduced here.

## Open Questions

- Is JUCE's initial-size trim enough for the first pass, or does the normal log need rotation?
- Should the game app share the same file-logger setup now, or wait until it has comparable runtime
  diagnostics needs?
