# Editor Logging Implementation Plan

Status: in-progress planning note. Scoped down to one essential: install a durable, file-backed
logger for the editor app. The structured diagnostics surface, action/undo-event logging, and the
rollback-contract diagnostic are no longer part of this plan; they are built incrementally inside
`editor-undo-plan-v9.md`, each alongside the undo stage that uses it.

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

Make editor logging durable, and nothing more:

- normal editor logs are written to a predictable per-user file;
- existing `juce::Logger::writeToLog` calls become file-backed once the logger is installed.

The `IEditorDiagnostics` surface, action/undo-event logging, and the rollback-contract diagnostic
all live in `editor-undo-plan-v9.md` and are built stage by stage with the undo work. They are
intentionally not part of this plan; the durable file logger here is what makes all of that output
persistent.

## Non-Goals

- Do not add a third-party logging backend.
- Do not implement the broader `core-domain-logging-targets` split.
- Do not add real-time audio-thread logging.
- Do not thread logger dependencies through pure domain objects.
- Do not rewrite existing diagnostic calls; they become durable for free once the logger is installed.
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
- existing `juce::Logger::writeToLog` calls are file-backed after startup setup.

## Implementation Stages

1. Add editor log-path helpers and install `juce::FileLogger` from the editor app composition root,
   with the bounded size and creation-failure fallback above.
2. Verify existing editor and audio-engine `juce::Logger::writeToLog` calls become file-backed; leave
   those call sites in place.

That is the whole plan. The diagnostics surface, action/undo-event logging, and the rollback-contract
diagnostic are added in `editor-undo-plan-v9.md` as their stages arrive.

## Acceptance Criteria

- Starting the editor installs a file-backed logger before the audio engine and editor window are
  constructed.
- Existing JUCE logger calls write to the normal editor log once the logger is installed.
- No diagnostics surface, rollback-contract handling, target split, or third-party backend is
  introduced here.

## Open Questions

- Is JUCE's initial-size trim enough for the first pass, or does the normal log need rotation?
- Should the game app share the same file-logger setup now, or wait until it has comparable runtime
  diagnostics needs?
