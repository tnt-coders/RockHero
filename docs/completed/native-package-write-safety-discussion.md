# Native Package Write Safety Discussion

Status: completed for the narrowed slice.

## Result

The original concrete bug was that package save could write generated arrangement data before fully
validating the same arrangement's tone-document reference. That issue is resolved in the current
format:

- arrangement note/tuning document files are no longer part of the package;
- save-time tone-document validation happens before importing/copying audio for that arrangement;
- a missing or non-canonical `tone_document_ref` fails with the existing `SongPackageError` shape;
- the save path does not add chart/note validation infrastructure for data the product cannot
  author yet.

This matches the current tone-system scope: validate the tone document reference that exists today,
without adding speculative structured validation for future chart editing.

## Remaining Future Work

Broader transactional save behavior is not part of this completed slice. Multi-file save/publish
hardening, validate-all-then-write planning, structured validation diagnostics, and temporary-file or
staging commits are tracked separately in `docs/todo/native-package-write-safety-followups.md`.
