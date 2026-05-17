# Plugin Persistence Plan

## Goal

Make the editor's plugin signal chain save with the song and restore from both `.rhp` editor
projects and published `.rock` song packages.

The saved data must be usable by the game through the same `rock-hero-common/audio` runtime path
the editor uses. The format should also stay compatible with the direction in
`tone-rack-plan.md` and `tone-automation-track-plan.md`: a user-facing tone system made from
project-owned tone slots, tone clips, and automation, compiled by the audio backend into Tracktion
plugins, racks, branch gains, and automation.

## Current State

- `common/core::Arrangement` uses `tone_document_ref` because the referenced file will contain
  tone slots, plugin chain state, tone clips, and future automation rather than only a timeline.
- Native song packages write `song.json`, audio assets, and placeholder arrangement XML. They do
  not persist plugin or tone files yet.
- `.rhp` projects store native song content under `song/`; `.rock` packages store the same native
  song content at the archive root.
- The editor controller currently keeps the loaded plugin chain as runtime-only
  `PluginViewState` values in `m_plugins`.
- Adding or removing a plugin intentionally does not mark the project dirty because Save cannot
  restore those changes yet.
- `IPluginHost` can scan a VST3 file, add the selected candidate to the runtime chain, and remove a
  runtime plugin instance. It does not expose a save or restore operation.

## Design Direction

Persist tone data as native song data, not as editor-only `project.json` data.

Use `Arrangement::tone_document_ref` as the arrangement's pointer to a package-relative tone
document. The JSON field should be `toneDocument`. For example:

```json
{
  "id": "lead",
  "part": "Lead",
  "file": "arrangements/lead.xml",
  "audio": "backing",
  "toneDocument": "tones/lead.tone.json"
}
```

The file layout should be identical inside `.rhp` and `.rock` once the native song root is chosen:

```text
song.json
audio/backing.wav
arrangements/lead.xml
tones/lead.tone.json
tones/state/lead/plugin-1.bin
```

In an editor project package, those same entries live under `song/`:

```text
project.json
song/song.json
song/audio/backing.wav
song/arrangements/lead.xml
song/tones/lead.tone.json
song/tones/state/lead/plugin-1.bin
```

`rock-hero-common/core` should validate the `toneDocument` reference as a safe package-relative
path and preserve it, but it should not parse the tone document. `rock-hero-common/audio` owns the
tone document schema, plugin identity resolution, Tracktion plugin state, and compilation into the
runtime signal chain.

## V1 Tone Document Shape

The first tone document should represent the current linear chain as one default tone slot and one
default tone clip that covers the whole arrangement. That keeps the first implementation simple
while giving the tone automation track a natural place to attach future clips and parameter
automation.

Suggested v1 shape:

```json
{
  "formatVersion": 1,
  "toneSlots": [
    {
      "id": "tone-default",
      "name": "Default",
      "chain": [
        {
          "id": "plugin-1",
          "identity": {
            "format": "VST3",
            "name": "Amp",
            "descriptiveName": "Amp",
            "manufacturer": "Vendor",
            "version": "1.2.3",
            "uniqueId": "7f3a21c0",
            "deprecatedUid": "7f3a21c0",
            "isInstrument": false,
            "originalFileOrIdentifier": "C:/Program Files/Common Files/VST3/Amp.vst3",
            "juceIdentifierHint": "VST3-Amp-...",
            "tracktionIdentifierHint": "VST3-Amp-..."
          },
          "tracktionState": "tones/state/lead/plugin-1.tracktion-plugin"
        }
      ]
    }
  ],
  "toneClips": [
    {
      "id": "clip-default",
      "toneSlotId": "tone-default",
      "name": "Default",
      "startSeconds": 0.0,
      "endSeconds": null,
      "fadeInSeconds": 0.0,
      "fadeOutSeconds": 0.0
    }
  ],
  "automation": []
}
```

Rules for v1:

- Do not persist Tracktion plugin instance IDs. They are runtime IDs and must be regenerated when a
  project or song package is loaded.
- Do not treat the current `PluginCandidate::id` as durable package identity. It is a useful
  lookup hint, but it may be derived from path-sensitive JUCE or Tracktion identifier strings.
- Persist structured plugin identity: format, name, descriptive name, manufacturer, version,
  `uniqueId`, `deprecatedUid`, instrument flag, and original file or identifier. Store JUCE and
  Tracktion identifier strings only as non-authoritative hints.
- Treat absolute plugin file paths as machine-local restore hints. Do not validate them as package
  paths, and do not copy plugin binaries into `.rhp` or `.rock` packages by default.
- Persist Tracktion-aware plugin state separately from the JSON document so binary or framework
  state does not bloat `song.json` or the tone manifest. A v1 implementation should either store a
  Tracktion-private serialized plugin `ValueTree` sidecar owned by `common/audio`, or store a
  project-owned state object that includes raw processor bytes plus required host fields such as
  enabled/bypass, wet/dry, program number, bus layout, and custom host properties.
- Treat plugin state as the static base state for the slot. Future automation curves override
  parameters over time rather than replacing the base state.
- Validate every tone-document sidecar path inside `rock-hero-common/audio`: safe relative path,
  no root, no colon, no `.` or `..`, and preferably constrained under
  `tones/state/<arrangement-id>/`.
- Leave `automation` empty in the first pass, but keep the field so the document shape already
  matches the automation-track direction.
- `endSeconds: null` means the default tone clip extends through the active arrangement duration.
  A later implementation may materialize explicit clip end times once arrangement duration is
  stable at authoring time.

## Core Package Work

1. Rename `Arrangement::tone_timeline_ref` to `tone_document_ref`.
2. Extend native song package IO to read and write optional arrangement `toneDocument` fields.
3. Validate the field with the same safe-relative-path rules used for audio and arrangement files.
4. On load, reject an unsafe tone reference. For a missing tone file, prefer a typed package error
   for published `.rock` packages rather than silently loading a different signal chain.
5. On save, include `toneDocument` only when the arrangement has a non-empty `tone_document_ref`.
6. Preserve existing behavior for packages with no tone data so old fixtures and imported songs
   still load.
7. Add package tests that verify both directory and archive round trips keep the tone reference and
   include the tone files in the resulting package.

This work belongs in `rock-hero-common/core` because it is package structure, path safety, and
song-file persistence. It must not parse `lead.tone.json` beyond validating that the referenced
file exists inside the package.

## Audio Runtime Work

Add a project-owned tone persistence/runtime boundary in `rock-hero-common/audio`. This can start as
a small interface beside `IPluginHost`, rather than growing `IPluginHost` into a broad tone system:

```text
IToneRuntime
  captureActiveToneDocument(request) -> ToneDocumentSnapshot
  loadToneDocument(request) -> ToneLoadResult
  clearToneDocument()
```

The concrete Tracktion-backed implementation should:

1. Stop transport and release the playback context before capture or restore.
2. Capture the current runtime plugin chain into a v1 tone document.
3. Flush each external plugin through Tracktion's state path before reading plugin state. Do not
   call raw plugin state APIs in a way that bypasses Tracktion's host-level fields.
4. Write Tracktion-aware state sidecars under `tones/state/<arrangement-id>/`.
5. Restore a tone document by resolving each plugin reference through structured identity first,
   original file path second, and candidate or framework identifier hints last.
6. If the stored path must be scanned, do that before playback, on the message thread.
7. Recreate plugins in document order, restore their state sidecars, and return fresh runtime
   handles for editor view state.
8. Fail with typed audio errors when a required plugin, state file, or unambiguous plugin match
   cannot be loaded.
9. Rebuild monitoring/playback context after capture or restore.
10. Keep all Tracktion `Plugin`, `ExternalPlugin`, `Edit`, `RackType`, and state details
   private to the adapter.

No plugin scanning, state capture, state restore, rack creation, or plugin-list mutation may happen
on the audio callback.

For the first pass, `loadToneDocument()` can compile the default tone slot into the current linear
instrument chain. Later, the same loaded document can compile into the RackType-backed tone slots
described in `tone-rack-plan.md`.

## Editor Workflow Work

1. When a plugin is added successfully, update the editor's runtime chain and mark the project
   dirty.
2. When a plugin is removed successfully, update the runtime chain, reindex the display state, and
   mark the project dirty.
3. Before Save, Save As, or Publish moves project IO to the worker task, capture the active
   arrangement's tone document on the message thread while transport is stopped and playback
   context has been released.
4. Write or stage the returned tone document and plugin state files under the native song workspace.
5. Save a `Song` snapshot whose active arrangement has `tone_document_ref` set to the generated
   `tones/<arrangement-id>.tone.json` path.
6. Preserve existing tone document refs and sidecars for inactive arrangements until arrangement
   switching and per-arrangement tone editing exist.
7. Keep the project dirty if tone capture fails and report the typed failure before starting the
   package write.
8. On project open or `.rock` import, after audio preparation and active arrangement selection,
   load the active arrangement's tone document through the audio runtime before deriving the signal
   chain panel state.
9. Clear runtime plugin/tone state on project close and arrangement replacement.

The save path should capture plugin state on the stopped message thread because the current
plugin-host operations are message-thread operations and Tracktion plugin-state flushing is not an
audio-thread operation. Package writing can still remain on the worker once the tone files and
`Song` snapshot are ready.

## Game Workflow Work

The game should not have a separate signal-chain format.

When the game loads a `.rock` package, it should use the same native song package reader and pass
the selected arrangement to `rock-hero-common/audio`. The audio runtime should load
`tone_document_ref` and compile the chain before gameplay playback starts. Missing plugins should
be reported as a load failure or a deliberate safe-mode fallback, not silently ignored.

The first game integration can be minimal:

- load the default tone slot for the selected arrangement;
- reject or report missing plugin/state files;
- do not expose editor-only signal-chain UI;
- keep future safe-mode fallback policy separate from the persistence format.

## Compatibility With Tone Automation

The v1 tone document should separate reusable sound definitions from timeline usage:

- `toneSlots` own plugin chains and static plugin state.
- `toneClips` say when a tone slot is active.
- `automation` is reserved for future clip-level and parameter-level curves.

That means the current "one linear plugin chain" is just:

- one tone slot named `Default`;
- one tone clip spanning the arrangement;
- no automation curves.

The tone automation track can later render `toneClips` without migrating basic plugin persistence.
The RackType backend can later compile `toneSlots` into parallel branches without changing the
native song package contract.

## Testing Strategy

Add tests in layers:

- `common/core` package tests for reading, writing, publishing, and importing `toneDocument` refs.
- `common/core` safety tests for unsafe tone paths and missing referenced tone files.
- `common/audio` unit tests for v1 tone document parse/serialize using fake plugin descriptors and
  fake state sidecars.
- `common/audio` safety tests for unsafe tone-document sidecar paths.
- `common/audio` adapter tests for restore failure paths without requiring a real third-party
  plugin.
- `editor/core` controller tests with a fake tone runtime that verify add/remove marks dirty, save
  captures tone state, failed capture blocks save, and open restores the displayed chain.
- A focused publish test proving `.rock` contains the same tone document and state files as the
  project song workspace.

Real VST3 restore tests should stay optional integration tests because they depend on machine-local
plugin installation.

## Suggested Implementation Slices

### Slice 1: Persist The Reference

- Rename `Arrangement::tone_timeline_ref` to `tone_document_ref`.
- Add optional `toneDocument` read/write support to `rock_song_package.cpp`.
- Preserve `Arrangement::tone_document_ref` through package directory and archive round trips.
- Add tests for `.rhp` save and `.rock` publish layout.

This slice can land without restoring plugins yet. It establishes the package contract.

### Slice 2: Add The Tone Document Model

- Add v1 tone document values and JSON parse/serialize helpers in `rock-hero-common/audio`.
- Keep the model project-owned and free of Tracktion types.
- Add tests for linear-chain round trips, structured plugin identity, state file references,
  unknown fields, and bad schemas.

### Slice 3: Capture Runtime Plugins On Save

- Add the audio runtime capture API.
- Capture structured plugin identity and Tracktion-aware state sidecars from the current chain.
- Generate `tones/<arrangement-id>.tone.json` and state files in the song workspace.
- Mark plugin add/remove as dirty once Save can restore them.

### Slice 4: Restore Runtime Plugins On Load

- Add the audio runtime load API.
- During project open/import and game package load, restore the arrangement tone before playback is
  considered ready.
- Populate editor signal-chain view state from the restored runtime handles.

### Slice 5: Prepare For Tone Slots

- Replace the hard-coded single default slot with a small tone-slot collection in the audio model.
- Keep the editor panel focused on the selected slot.
- Leave timeline clip editing to the tone automation track implementation.

## Open Questions

- Should the first release fail loading when a plugin is missing, or allow a clearly reported
  silent/safe fallback for gameplay?
- Should plugin file paths be stored as absolute machine-local paths only, or should we also store
  a project-local copied plugin reference for user-supplied plugins where licensing permits it?
- Should `PluginCandidate` expose the full structured identity immediately, or should
  `IToneRuntime` own the richer identity until plugin search UX needs it?
- Should save capture tone state for every arrangement immediately, or only the currently selected
  arrangement until arrangement switching exists in the editor?
