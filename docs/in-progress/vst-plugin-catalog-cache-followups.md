# VST Plugin Catalog Cache Follow-ups

Status: superseded by the JUCE/Tracktion scan simplification.

The concerns recorded here were valid for the custom Rock Hero catalog cache design, especially
around in-process VST3 factory probing and cache invalidation complexity. That design has been
removed. Catalog refresh now uses JUCE/Tracktion scanning and Tracktion's persisted
`knownPluginList64` data instead of `PluginCatalogCache.json`, so the earlier follow-up items no
longer apply as active work.
