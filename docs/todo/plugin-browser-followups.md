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
