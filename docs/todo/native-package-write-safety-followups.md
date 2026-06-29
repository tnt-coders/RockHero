# Native Package Write Safety Followups

Status: future work.

The narrowed tone-reference write-safety issue is complete; see
`docs/completed/native-package-write-safety-discussion.md`. This file tracks broader package-save
hardening that should wait until the editor has real save-repair UI needs or package saves grow more
generated outputs.

## Future Direction

1. Split native package save into a pure planning phase and a commit phase.
2. Validate all deterministic song/package issues before writing package files.
3. Consider structured validation diagnostics only when UI can use field-level repair information.
4. Treat audio import/copy operations as planned side effects instead of doing them while building
   JSON.
5. Use temporary sibling files for generated JSON outputs if corruption risk becomes meaningful.
6. Consider whole-directory staging for `.rock` publish/archive operations once publish semantics
   are clearer.

## Candidate Future Tests

- Multiple independent validation issues can be reported together when structured diagnostics exist.
- A validation failure before commit leaves the destination package untouched.
- A missing external audio asset does not create new audio asset entries.
- Commit-time filesystem failures report a clear partial-write risk.
- A successful save still writes the expected `song.json`, audio assets, tone documents, and archive
  entries.
