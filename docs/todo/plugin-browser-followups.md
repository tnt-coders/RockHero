# Plugin Browser Follow-Ups

Status: todo. The first plugin browser implementation opens a VST3 catalog window from the host's
already-known plugins and lets the user explicitly rescan conventional VST3 folders. These items
should be revisited before treating plugin management as complete.

## Revisit

- Add user-configurable plugin search paths and persist them in editor settings. The current scan
  roots are the conventional system VST3 directories only.
- Persist the scanned plugin catalog or cache enough metadata so first browser open can show a
  useful list without a slow scan. The cache needs invalidation when search paths change or plugin
  files disappear.
- Move broad plugin catalog scans to a safer scanner process if in-process scans prove unstable.
  Individual third-party plugins can hang or crash during discovery.
- Add progress and cancellation for catalog scans. The current busy overlay is indeterminate and
  cannot show which plugin or directory is being scanned.
- Decide how plugin categories, favorites, manufacturer grouping, and text search should evolve
  once the catalog grows beyond a small VST3 list.
- Revisit candidate identity persistence before saving browser preferences. Candidate IDs are
  opaque and valid for the plugin host, but favorites and recent-plugin lists need a durable key.
- Prune stale `KnownPluginList` entries during catalog refresh. After the
  discover-then-validate-lazily split, Tracktion's `KnownPluginList` keeps entries for plugins
  whose `.vst3` paths no longer exist (e.g., the user moved or uninstalled the plugin between
  sessions). The browser merge in `Engine::Impl::knownPluginCatalog` then shows two rows for the
  same plugin: a stale Tracktion entry at the old path (Add would fail with "Plugin file does not
  exist") and a fresh optimistic discovery entry at the new path. Resolution: on Rescan, walk
  `KnownPluginList::getTypes()` and remove entries whose `fileOrIdentifier` no longer points at an
  existing file. Needs care because some hosts persist non-filesystem identifiers there; gate the
  pruning to `VST3` format entries with absolute paths.
- Move per-plugin validation off the message thread. `Engine::Impl::addKnownPluginToTrack` now
  runs `scanPluginFileForCandidates` synchronously on the message thread when adding a plugin
  Tracktion has never seen, which can block UI paint for up to `g_plugin_scan_timeout` (30 s)
  while a third-party scanner subprocess runs. The previous code had the same freeze window, but
  it was paid during Rescan — the discover-then-validate-lazily split moved it to first Add. The
  busy overlay still paints (`LoadingPlugin` busy state wraps the add through
  `runAfterBusyOverlayPaintedOrNow`), so the UI signals work, but the window is unresponsive
  during the scan. Resolution: dispatch validation to the editor task runner with its own busy
  phase, then complete the add on the message thread once a validated description is in hand.
  Threads the existing `ProjectLoadLiveRigStage`-style "scan then commit" pattern through the add
  path.
