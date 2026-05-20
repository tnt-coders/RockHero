# Plugin Browser Code Review

Status: in-progress.

## Purpose

Review the current plugin browser change set before treating it as settled. The feature is now
usable enough for manual testing, but the implementation touches the audio boundary, editor core
workflow, JUCE presentation, busy-state behavior, and startup restore timing. The review should
look for design issues, not just local correctness.

## Scope

- `common/audio`: `IPluginHost`, `Engine` plugin scanning, known-plugin catalog reads, plugin
  insertion, and plugin-window behavior.
- `editor/core`: plugin-browser view state, controller actions, busy-state transitions, catalog
  scan scheduling, candidate add behavior, and startup-restore interaction.
- `editor/ui`: `PluginBrowserWindow`, `EditorView` window ownership, listener forwarding, and
  busy-overlay paint fencing.
- Tests around common audio plugin-host behavior, editor controller workflow, and editor UI
  wiring.
- Follow-up documentation in `docs/todo/plugin-browser-followups.md`.

## Review Checklist

- Confirm the layering remains clean: Tracktion/JUCE plugin details stay behind
  `common::audio::IPluginHost`, editor workflow stays in `editor/core`, and UI components only
  render state and emit intents.
- Verify opening the plugin browser is cheap and cannot trigger a broad plugin scan; Rescan should
  be the only path that walks default plugin folders.
- Check the lifecycle of plugin-browser window state when projects load, close, fail to load, add a
  plugin successfully, or fail to add a plugin.
- Re-check busy-token handling for plugin catalog scans and plugin insertion, especially stale
  completions after Close or another project-load action.
- Re-check the startup restore path with tone documents: the busy-overlay paint fence should not
  block forever when the editor is not yet paintable.
- Inspect plugin scan failure behavior: one bad plugin should not hide otherwise usable candidates,
  and true scan failures should surface with useful typed errors.
- Confirm tests cover the intended user-facing behavior without over-coupling to implementation
  details.

## Known Follow-Ups

- Persisting or caching scanned plugin metadata is still deferred.
- User-configurable plugin search paths are still deferred.
- Progress, cancellation, grouping, favorites, and durable browser preference identity are still
  deferred.
