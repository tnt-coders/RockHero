# VST Plugin Catalog Scan Decision

Status: completed. This consolidates the former in-progress VST plugin catalog scan plan and
cache follow-up note.

## Decision

Use JUCE and Tracktion for VST3 catalog scanning instead of maintaining a separate Rock Hero
filesystem metadata cache.

`Engine::scanPluginCatalog()` scans JUCE's default VST3 locations through Tracktion's
`KnownPluginList`. `Engine::scanPluginLocations()` scans caller-supplied roots through the same
path and returns the known candidates produced for those roots. Tracktion persists the resulting
JUCE `PluginDescription` values in its existing `knownPluginList64` property, so Rock Hero does
not own a second plugin catalog cache.

## Final Model

- The editor keeps the loading bar by precomputing JUCE's VST3 scan path list and reporting
  count-based progress while `PluginDirectoryScanner` scans each path.
- Tracktion separate-process scanning remains enabled so plugin validation stays isolated when the
  scanner helper process is available.
- `knownPluginCatalog()` remains lightweight: it reads Tracktion's in-memory known-plugin list and
  does not traverse plugin folders or launch scanners.
- Lazy single-file scanning remains available for plugin insert and live-rig restore fallback.
- Catalog scans are cooperatively cancellable at candidate boundaries. Candidates already
  validated by Tracktion stay in the known-plugin list, so cancelling keeps partial progress.
- Rock Hero does not write or read `PluginCatalogCache.json`; Tracktion's known-plugin persistence
  is the catalog cache.

## Closed Follow-Ups

The earlier custom Rock Hero catalog-cache design raised valid concerns around in-process VST3
factory probing, cache invalidation, and duplicate catalog ownership. That design has been removed.
Catalog refresh now uses JUCE/Tracktion scanning and Tracktion's persisted `knownPluginList64`
data instead of `PluginCatalogCache.json`, so those cache-specific follow-up items are closed.

## Consequence

A cold scan is allowed to be slow, similar to other hosts. Warm launches and unchanged rescans rely
on Tracktion/JUCE known-plugin persistence instead of Rock Hero-specific metadata heuristics.
