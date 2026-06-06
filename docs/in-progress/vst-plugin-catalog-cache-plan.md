# VST Plugin Catalog Cache Plan

Status: in-progress planning note. This captures the active direction for preserving fast VST3
catalog scans while filling missing metadata and avoiding repeated work across app launches.

## Scope

This plan covers the plugin browser catalog produced by `common::audio::IPluginHost` and the
Tracktion/JUCE-backed `Engine` implementation.

The goal is to:

- keep the current fast folder walk for `.vst3` paths;
- fill manufacturer metadata without loading plugin binaries during catalog refresh;
- persist app-local catalog metadata so the browser can show useful results immediately after
  launch;
- keep plugin validation lazy and authoritative when the user inserts a selected plugin.

## Current State

`Engine::scanPluginCatalog()` currently calls `discoverPluginLocationCandidates()` over the
default VST3 roots. Discovery finds `.vst3` paths, normalizes bundle-style Windows module paths
with `normalizedVst3ScanPath()`, and emits optimistic `PluginCandidate` values.

That keeps scanning fast because it does not call JUCE's full plugin scan for every candidate.
The tradeoff is that `makeVst3FileCandidate()` has no scanner metadata, so `manufacturer` is empty
for filesystem-only candidates.

`Engine::knownPluginCatalog()` combines two sources:

- Tracktion's in-memory `KnownPluginList`, which contains real JUCE `PluginDescription` values
  from prior validation or live-rig restore work; and
- `m_discovered_plugin_catalog`, which contains the fast filesystem candidates from the current
  process only.

Tracktion already persists its `KnownPluginList` through `PropertyStorage`, but that list only
helps plugins that have already gone through JUCE validation. It does not persist Rock Hero's
folder-only catalog candidates.

## Decision

Add a Rock Hero plugin catalog cache in app data. Treat it as a display and performance cache, not
as authority to load plugins.

The cache should live behind the `common/audio` plugin-host adapter, not in
`rock-hero-editor/core::EditorSettings`. Plugin catalog data describes the per-machine plugin host
environment and should remain outside editor workflow settings and outside song packages.

The preferred file is a separate JSON cache, for example `PluginCatalogCache.json`, under the
app-data/cache folder owned by the Tracktion-backed engine. With the current default Tracktion
`PropertyStorage`, that means the same common audio app-data area used for engine settings rather
than the editor-only `Rock Hero Editor.settings` file.

Keep the whole cache in `rock-hero-common/audio`, including the pure record shape and JSON
round-trip helpers, rather than splitting the pure parts into `rock-hero-common/core`. The cache
vocabulary is plugin-host-specific (normalized VST3 scan path, moduleinfo metadata source, format
name) and the path normalization, file-stamping, and moduleinfo extraction it depends on are audio
adapter concerns that cannot move to core. `common/core` is permitted `juce::JSON`/`juce::File` for
headless serialization, so a split is possible, but it would scatter one cohesive,
plugin-host-only concept across two libraries for no reuse benefit. The record and JSON helpers
stay pure and headless-testable within `common/audio/tests`. Revisit the split only if a non-audio
consumer ever needs these records.

## Cache Contract

`knownPluginCatalog()` must remain lightweight. It may read in-memory state that was loaded earlier,
but it must not read the filesystem, traverse plugin folders, launch plugin scanner processes, or
execute plugin code.

Cache file IO should happen at three points only: the startup load, an explicit catalog refresh
save, and a shutdown save. It must not happen in `knownPluginCatalog()`. In particular, the
message-thread insert path must not write the cache file inline (see
[Insert And Validation Flow](#insert-and-validation-flow)); it may update the in-memory catalog
record, which is persisted by the next refresh or shutdown save.

Cache failures should be recoverable:

- malformed cache files should be ignored and replaced on the next successful scan;
- cache absence should behave like an empty catalog;
- save failure is best effort: log it and continue. A display/performance cache has no
  user-visible state to corrupt when a save fails, so this is the no-caller-visible-channel
  exception in `architectural-principles.md`. Do not surface save failure as a typed
  `PluginHostError` through the plugin-host port.

## Cached Record Shape

Store enough data to decide whether a cached candidate is still usable without scanning the plugin
binary:

- cache schema version;
- normalized scan path key;
- display path;
- format name, currently `VST3`;
- candidate ID used by Rock Hero for filesystem candidates (optional; see below);
- display name;
- manufacturer;
- metadata source, one of `moduleinfo`, `version-resource`, `plugin-factory`, `path`,
  `validated`, or `filename`;
- file size and last-write time for invalidation;
- moduleinfo file size and last-write time when `moduleinfo.json` exists.

Do not add JUCE or Tracktion identifier hints to the cached record in this first pass. Authoritative
identifiers from a validation pass already live in Tracktion's persisted `KnownPluginList`, which
`knownPluginCatalog()` merges ahead of filesystem candidates, so duplicating them here would add a
second persistence path for no display benefit (see
[Insert And Validation Flow](#insert-and-validation-flow)). Add such hints later only if a concrete
need appears that `KnownPluginList` does not already cover.

Use the normalized scan path as the primary key. On Windows this should be the architecture-specific
module path produced by `normalizedVst3ScanPath()`, because JUCE and Tracktion describe VST3 bundle
plugins by that path.

The Rock Hero candidate ID is derived deterministically from the normalized scan path by
`makeVst3FileCandidateId()`, so it is recomputable from the path key on load. Storing it in the
record is therefore optional and informational, not a second independent key. If it is stored, the
loader should treat the normalized scan path as authoritative and may recompute the ID rather than
trusting the persisted value.

File size and last-write time invalidate against both the normalized module file and
`moduleinfo.json` when moduleinfo exists. This keeps display metadata fresh when a vendor updates
only moduleinfo while still avoiding plugin binary loading. Do not treat the file stamp as
authoritative for anything beyond catalog display.

The cache should be schema-versioned. If the schema version is unsupported, ignore the file instead
of trying to migrate it during this first implementation.

## Startup Flow

On `Engine` construction, after Tracktion has initialized its `KnownPluginList`, load the Rock Hero
catalog cache into `m_discovered_plugin_catalog`.

This gives the plugin browser immediate candidates from the previous successful scan. The UI should
still expose Rescan, because startup cache loading cannot discover newly installed or removed
plugins by itself.

Do not run the directory walk automatically on every launch unless a later UX decision explicitly
asks for background refresh. If background refresh is added later, it should use the same fast scan
and static metadata path described below.

## Rescan Flow

Keep the current explicit Rescan behavior and fast directory traversal.

The Rescan busy state should start indeterminate while the filesystem roots are being walked,
because the final deduped candidate count is not known yet. After discovery, process the collected
candidate list in a second phase and report count-based progress (`completed / total`) through the
plugin-host boundary so the editor can show determinate progress without doing a second directory
walk. When the plugin browser is open, render the same busy state over the browser content so the
scan progress appears where the user initiated the refresh.

For each discovered `.vst3` path:

1. Normalize the scan path.
2. Read file size and last-write time.
3. If the cache contains a matching record with unchanged module and moduleinfo metadata, reuse its
   name and manufacturer.
4. If the path is new or changed, run static metadata enrichment only.
5. Add or update the in-memory catalog.
6. Save a complete replacement cache after a successful refresh.

Remove stale cache entries that were not rediscovered during the refresh. If a cached plugin file
has disappeared, it should stop appearing after Rescan.

An unchanged cached record with an empty manufacturer and a `filename` or `moduleinfo` metadata
source should also be re-enriched on Rescan. That lets older caches pick up newer cheap metadata
fallbacks, such as Windows version-resource `CompanyName`, without requiring a full JUCE scan or a
manual cache delete.

If moduleinfo and version resources still do not provide manufacturer metadata, read VST3 factory
metadata for unresolved modules by loading the module, calling `GetPluginFactory`, reading
`PFactoryInfo.vendor`, releasing the factory, and unloading the module. This must not create plugin
component instances or call the full JUCE scanner. Cache successful factory-derived metadata as
`plugin-factory` so the cost is paid only on refresh when the module has changed or the existing
record needs metadata repair.

If factory metadata is unavailable, use the first folder under the scan root as a last-resort
display hint when the plugin lives in a vendor-style subfolder. Mark these records with metadata
source `path`, and keep re-enriching them on later rescans so stronger metadata can replace the
path-derived value if it becomes available.

## Static Metadata Enrichment

Prefer VST3 `moduleinfo.json` when present. JUCE's VST3 implementation already has a fast
moduleinfo path, but its public scan entry point may fall back to opening the plugin binary when
moduleinfo is missing. Rock Hero should parse moduleinfo directly or through a helper that cannot
fall back to loading plugin code.

For VST3 moduleinfo:

- manufacturer should come from factory/vendor metadata when available;
- factory/vendor metadata is still useful even when the class list cannot produce a browser row
  name;
- name may come from the class name only when the bundle exposes exactly one audio effect class;
- if the bundle exposes multiple plugin classes, keep the filename-derived display name until the
  browser and insert flow support selecting a specific class.

On Windows, use version-resource `CompanyName` as a cheap manufacturer fallback when moduleinfo is
missing, malformed, non-audio, or missing vendor data. This must not instantiate or scan the plugin
with JUCE.

For unresolved VST3s, use factory metadata as the next fallback. This loads the plugin module and
executes module entry/exit hooks, but it must not instantiate plugin classes, inspect parameters,
or add the plugin to Tracktion's `KnownPluginList`.

If no static metadata is available, keep the current filename fallback and leave manufacturer
empty.

## Insert And Validation Flow

Plugin insertion remains the authority boundary. A cached candidate must not skip JUCE validation.

If `findKnownPlugin(plugin_candidate.id)` misses, `insertPluginCandidateToTrack()` should keep
calling `scanPluginFileForCandidates(plugin_candidate.file_path)`. That lazy scan may load or
validate the selected plugin, but only for the one plugin the user chose.

After a successful lazy scan, the resulting `PluginDescription` is added to Tracktion's
`KnownPluginList`, which Tracktion already persists. Because `knownPluginCatalog()` merges
`KnownPluginList` ahead of the filesystem candidates, that validated plugin will already surface
authoritative metadata on the next launch without any Rock Hero cache writeback. The Rock Hero
cache should therefore not duplicate that persistence: do not write the cache file inline on the
message-thread insert path.

Limit the insert-time interaction with the Rock Hero cache to refreshing the matching in-memory
record's display fields (name and manufacturer) from the resulting `PluginDescription`, so the
browser stays consistent for the rest of the session. That in-memory update is persisted only by
the next refresh or shutdown save, per the [Cache Contract](#cache-contract). If even the
in-memory update proves unnecessary in practice — because the `KnownPluginList` merge already
covers the validated plugin — it can be dropped; treat it as the minimal change that keeps
within-session display consistent, not as a second persistence path.

Do not emit one browser row per VST3 moduleinfo class in this first pass. The current insertion
fallback resolves filesystem candidates with `scan_result->front()`, which can choose the wrong
class for multi-plugin bundles. Class-level candidates should be a separate change that stores the
class ID in the candidate and resolves the exact matching JUCE description during insert.

## Tracktion KnownPluginList Interaction

Continue letting Tracktion persist its `KnownPluginList`. That list is still useful for previously
validated plugins and live-rig restoration.

The Rock Hero cache should complement it:

- Tracktion `KnownPluginList` stores authoritative JUCE descriptions after validation.
- Rock Hero `PluginCatalogCache.json` stores fast catalog display records for filesystem candidates.
- `knownPluginCatalog()` continues merging Tracktion known descriptions before filesystem
  candidates so authoritative validated metadata wins.

Do not try to seed Tracktion's `KnownPluginList` from static metadata unless the seeded
`PluginDescription` is proven sufficient for `createNewPlugin()`. The safer first version is to
keep static metadata in Rock Hero's catalog cache and validate selected plugins through JUCE.

## Implementation Steps

1. Add small cache record and cache document helpers in `rock-hero-common/audio`.
2. Add JSON load/save helpers with schema-version checks and malformed-cache handling.
3. Add static VST3 metadata extraction for `moduleinfo.json`.
4. Add Windows version-resource `CompanyName` fallback for moduleinfo misses.
5. Add VST3 factory-info fallback for modules still missing manufacturer metadata.
6. Add scan-root folder fallback for vendor-style VST3 subfolders.
7. Load the cache during `Engine` startup and seed `m_discovered_plugin_catalog`.
8. Teach `discoverPluginLocationCandidates()` to reuse unchanged cached metadata.
9. Split catalog refresh into discovery and metadata phases so the metadata phase can report
   determinate progress without a second filesystem walk.
10. Save the replacement cache after a successful `scanPluginCatalog()` refresh, and also save on
   engine shutdown so in-memory updates made since the last refresh are not lost.
11. After a successful lazy insert scan, refresh only the matching in-memory catalog record's
   display fields from the resulting `PluginDescription`. Do not write the cache file inline on the
   message-thread insert path; rely on the refresh/shutdown save and on Tracktion's persisted
   `KnownPluginList` for the validated plugin's authoritative metadata.
12. Keep `scanPluginLocations()` transient in this first pass: return enriched candidates using the
   same cache/static metadata logic, but do not update or save the default cache. Once custom scan
   locations are persisted as user-approved roots, that saved-root workflow can opt those paths
   into the durable cache.

## Testing

Prefer tests around pure/cache helpers rather than end-to-end plugin scanning.

Cover:

- empty cache loads as an empty catalog;
- malformed JSON is ignored without crashing;
- unknown schema versions are ignored;
- valid cache records round-trip with path, name, manufacturer, and file stamps intact;
- unchanged discovered paths reuse cached manufacturer metadata;
- changed size or last-write time causes static metadata to be reread;
- missing rediscovered paths are removed from the saved replacement cache;
- VST3 moduleinfo with one class fills name and manufacturer;
- VST3 moduleinfo with multiple classes fills manufacturer but keeps a filename-derived name;
- VST3 factory metadata fills manufacturer when sidecar/static metadata is missing;
- cache loading does not call JUCE plugin scanning APIs.

Use synthetic temporary directories and small JSON fixtures. Do not require installed real VST3
plugins for automated tests.

## Non-Goals

- Do not run a full JUCE scan on startup.
- Do not run a full JUCE scan or instantiate plugin classes during catalog refresh.
- Do not store plugin catalog data in `.rhp` project packages or `.rock` song packages.
- Do not put this cache in editor-only `EditorSettings`.
- Do not make cached metadata authoritative for plugin creation.
- Do not implement class-level VST3 browser rows until insertion can resolve the selected class
  exactly.
- Do not update durable `docs/design/` documents from this plan.
