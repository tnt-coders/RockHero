# Plugin Persistence Review Followups

Issues found during code review of the committed plugin/tone persistence work on the
`work-in-progress` branch. Recorded here so they can be addressed after the busy-overlay progress
rewrite lands. None of these block the rewrite; address them in a separate pass.

## 1. `Impl::scanPluginFileForCandidates` lost its message-thread guard

The public `Engine::scanPluginFile` still checks `MessageManager::isThisTheMessageThread()`, but
the work was moved into `Impl::scanPluginFileForCandidates` so it could be reused from
`ensureKnownPluginForIdentity` (called inside `loadRig`). Today both entry points already check
the thread before calling, but the helper itself no longer self-guards, and a future caller could
miss the requirement.

**Fix:** Add a one-line comment on `Impl::scanPluginFileForCandidates` stating that callers must
already be on the message thread, or restore an internal assertion.

## 2. Trivial JSON wrappers in `engine.cpp`

`appendJsonArray(array, value)` is a one-line `array.append(value)` indirection.
`jsonProperty(object, name)` is `object[juce::Identifier{name}]`. Neither carries its weight.

**Fix:** Inline them at call sites.

## 3. `readOptionalBool` is inconsistent with `readOptionalInt`

`readOptionalInt` takes a `fallback` parameter; `readOptionalBool` does not — missing-field and
explicit-false are indistinguishable. Today the only caller is `isInstrument` defaulting to false,
so it works, but the asymmetry is surprising.

**Fix:** Either add a `fallback` parameter for symmetry, or accept the asymmetry and document why.

## 4. Sidecar paths may drift from the existing tone document subdirectory

When `Engine::captureActiveRig` is called with an `existing_tone_document_ref`, it reuses the
existing document path string but generates new sidecar paths via
`generatedPluginStatePath(arrangement_id, ...)`, which runs the id through `safePathStem`. If a
project's existing document lives at `tones/lead.tone.json` but the arrangement id later becomes
e.g. `Lead Guitar`, sidecars get written to `tones/state/Lead_Guitar/` while the original document
referenced `tones/state/lead/`. The old sidecars become orphans and the document subdirectory
silently drifts.

**Fix:** Either derive the state subdirectory from the existing tone document path, or
canonicalize arrangement ids once at project creation so the safePathStem result is stable.

## 5. `m_has_unsaved_changes` flip depends on injected dependency

In `EditorController::Impl::performActionImpl` for both `AddPlugin` and `RemovePlugin`, the dirty
flag flips only when `m_live_rig != nullptr`. This is defensible (don't dirty a project that has
no way to persist the change) but it is surprising contract behavior driven by an injected port.

**Fix:** Add a short comment on each flip explaining the rule, or surface "live rig persistence is
available" as a derived predicate with a single named accessor.

## 6. Duplicated JSON helpers across modules

`engine.cpp` and `rock_song_package.cpp` both define `appendJsonArray`, `makeJsonObject`,
`makeJsonString`, `jsonProperty`, `readRequiredString`, `readOptionalString` in anonymous
namespaces. It works because they're file-local, but the duplication is growing.

**Fix:** Extract a shared `common/core` JSON utility header before a third caller appears. Mind
the layering rule: the helpers cannot pull `juce_core` into headers consumed by non-juce-linking
targets.

## 7. Plan doc bucket mismatch with CLAUDE.md

`docs/in-progress/plugin-persistence-plan.md` does not match the `docs/todo/` convention
referenced in `CLAUDE.md` under "Documentation Maintenance Rules". This followups doc is in the
same bucket — both should match whichever convention the project picks.

**Fix:** Either rename the in-progress docs under `docs/todo/`, or note `in-progress/` as a
deliberate new bucket in `CLAUDE.md`.