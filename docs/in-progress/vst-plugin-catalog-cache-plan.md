# VST Plugin Catalog Scan Plan

Status: current implementation direction. This supersedes the earlier Rock Hero
`PluginCatalogCache.json` plan.

## Decision

Use JUCE and Tracktion for VST3 catalog scanning instead of maintaining a separate Rock Hero
filesystem metadata cache.

`Engine::scanPluginCatalog()` scans JUCE's default VST3 locations through Tracktion's
`KnownPluginList`. `Engine::scanPluginLocations()` scans caller-supplied roots through the same
path and returns the known candidates produced for those roots. Tracktion persists the resulting
JUCE `PluginDescription` values in its existing `knownPluginList64` property, so Rock Hero does
not own a second plugin catalog cache.

## Constraints

- Keep the editor loading bar by precomputing JUCE's VST3 scan path list and reporting count-based
  progress while `PluginDirectoryScanner` scans each path.
- Keep Tracktion separate-process scanning enabled so plugin validation remains isolated when the
  scanner helper process is available.
- Keep `knownPluginCatalog()` lightweight: it reads Tracktion's in-memory known-plugin list and
  does not traverse plugin folders or launch scanners.
- Keep lazy single-file scanning for plugin insert and live-rig restore fallback.
- Do not write or read `PluginCatalogCache.json`; Tracktion's known-plugin persistence is the
  catalog cache.

## Consequence

A cold scan is allowed to be slow, similar to other hosts. Warm launches and unchanged rescans rely
on Tracktion/JUCE known-plugin persistence instead of Rock Hero-specific metadata heuristics.
