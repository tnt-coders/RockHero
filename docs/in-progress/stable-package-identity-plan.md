# Stable Package Identity Plan

## Problem

Rock Hero package paths are starting to carry too much identity.

The current tone persistence work can derive tone documents and Tracktion state sidecars from
arrangement IDs or tone document names. That is fragile once arrangement names, tone names, or tone
blocks become editable. A user-facing name such as `Lead`, `Rhythm`, `Crunch`, or `Solo Practice`
is presentation data. It can change. If file paths are derived from those names, the project must
rename files, update references, and clean old paths every time the name changes.

The specific failure mode this plan is meant to prevent is path drift:

```text
song.json references: tones/lead.tone.json
tone JSON references: tones/state/lead/plugin-1.tracktion.xml
renamed tone name: Lead Alt
new capture writes: tones/state/lead-alt/plugin-1.tracktion.xml
old state remains: tones/state/lead/plugin-1.tracktion.xml
```

That leaves the package with stale or mismatched files even though the stable project object did
not actually change.

Rocksmith PSARC packages provide useful precedent: the extracted sample package uses readable
resource names such as `bigrminthefaceofthenameless_lead.xml`, but the manifests also carry stable
arrangement identity through `PersistentID` values such as
`20F97706BD07464EA7CF63AC86838117`. Rock Hero adopts the same identity *rule* — stable IDs are
authoritative, display names are metadata — but uses its own ID namespace rather than carrying
PSARC IDs into the Rock Hero package format.

## Recommendation

Use stable, opaque IDs as the durable identity for arrangements, tone documents, and tone state.
Do not use editable display names as the durable key.

Generate a fresh Rock Hero ID whenever an object is created, including during PSARC import. Use a
128-bit value rendered as 32 lowercase hex characters:

```text
9b26d8e83ec54f979a81d18ef6bce30d
```

The Rock Hero package format is its own format. PSARC-specific identifiers
(`PersistentID`, `MasterID`, `ManifestUrn`, `DLCKey`) are not stored in `song.json`, tone
documents, or any other persisted Rock Hero artifact. They may be used transiently during import
(for example, to disambiguate arrangements within a single PSARC) but do not survive into the
saved package. This keeps the format self-contained and free of coupling to Ubisoft's tooling or
identity scheme.

Note on arrangement XML payloads: Rock Hero currently embeds PSARC's arrangement XML format
unchanged as the contents of `arrangements/<id>.xml`. This is an interim choice. Because the file
is named by ID rather than by format, the surrounding package structure does not depend on the
payload format, and a future Rock Hero-native arrangement format can replace it without changing
identity, paths, or references.

Do not add an `arr_` prefix to arrangement IDs or filenames. The package directory already gives
the ID its domain:

```text
arrangements/20f97706bd07464ea7cf63ac86838117.xml
tones/9b26d8e83ec54f979a81d18ef6bce30d.tone.json
tones/state/9b26d8e83ec54f979a81d18ef6bce30d/plugin-1.tracktion.xml
```

Tone IDs should be separate from arrangement IDs unless the model deliberately says one
arrangement owns exactly one tone document forever. Keeping them separate leaves room for shared
tones, alternate tones, and tone blocks without redefining arrangement identity.

## Option A: ID Filenames

This is the recommended implementation.

Store package resources under stable IDs:

```text
song.json
arrangements/
  4f3a1c5e9d2b48a6b1f0c7e8d9a2b3c4.xml
tones/
  9b26d8e83ec54f979a81d18ef6bce30d.tone.json
  state/
    9b26d8e83ec54f979a81d18ef6bce30d/
      plugin-1.tracktion.xml
```

The display name remains metadata, not a path input:

```json
{
  "id": "4f3a1c5e9d2b48a6b1f0c7e8d9a2b3c4",
  "name": "Lead",
  "part": "Lead",
  "file": "arrangements/4f3a1c5e9d2b48a6b1f0c7e8d9a2b3c4.xml",
  "toneDocument": "tones/9b26d8e83ec54f979a81d18ef6bce30d.tone.json"
}
```

Advantages:

- Rename operations do not move files.
- Tone state sidecar paths cannot drift from editable names.
- Save/load validation stays simple.
- Package writes are less likely to leave orphaned files.
- Implementation is smaller because no rename cleanup policy is required for normal edits.

Tradeoffs:

- Browsing the package by hand is less descriptive.
- Debugging package contents requires looking at `song.json` or tone JSON to connect IDs to names.
- Existing fixtures and imported output that expect `arrangements/lead.xml` need to be updated.

## Option B: Readable Filenames With Stable IDs

This is a viable alternative if human-readable package contents become important.

Store identity in metadata, but keep readable resource paths:

```text
song.json
arrangements/
  lead.xml
tones/
  lead.tone.json
  state/
    lead/
      plugin-1.tracktion.xml
```

The stable ID remains authoritative:

```json
{
  "id": "4f3a1c5e9d2b48a6b1f0c7e8d9a2b3c4",
  "name": "Lead",
  "part": "Lead",
  "file": "arrangements/lead.xml",
  "toneDocument": "tones/lead.tone.json"
}
```

To keep this correct, rename must be a first-class persistence operation:

1. Compute canonical paths from the new display name.
2. Move the arrangement file, tone document, and tone state directory.
3. Update every reference in `song.json` and tone JSON.
4. Remove old unreferenced files after successful writes.
5. Reject or repair packages where references point at missing, unsafe, or stale paths.
6. Add regression tests for rename, collision handling, failed moves, and stale sidecar cleanup.

Advantages:

- Package contents are easier to inspect manually.
- This is closer to the PSARC resource-name style.

Tradeoffs:

- Rename operations become filesystem migrations.
- More code is needed to update references and remove stale paths.
- Partial failures, manual edits, and merge conflicts need more validation.
- `Arrangement` may need to retain or expose arrangement file references if arrangement filenames
  become editable package state rather than generated save output.

## Implementation Plan For Option A

1. Add a small package ID helper in the appropriate core module.
   - Generate fresh 128-bit IDs rendered as 32 lowercase hex characters.
   - Reject empty, unsafe, or duplicate IDs at package boundaries.

2. Update PSARC import identity.
   - Generate a fresh Rock Hero ID for each imported arrangement at import time.
   - Do not carry `PersistentID`, `MasterID`, `ManifestUrn`, or `DLCKey` into the saved Rock Hero
     package. These values may be used transiently during import but do not appear in `song.json`
     or any other persisted artifact.

3. Update arrangement file generation.
   - For imported and saved arrangements, prefer `arrangements/<arrangement-id>.xml`.
   - Preserve duplicate-ID rejection in native package IO.
   - Update tests that currently expect `arrangements/lead.xml`.

4. Update tone document identity.
   - Create or preserve a stable tone document ID.
   - Use `tones/<tone-id>.tone.json` for the document path.
   - Use `tones/state/<tone-id>/` for Tracktion state sidecars.
   - Stop deriving tone state directories from arrangement names or tone display names.

5. Update live rig capture and restore.
   - Capture writes state sidecars under the tone ID directory.
   - Existing tone documents keep their existing tone ID and sidecar root.
   - New tone documents get a generated tone ID before the first write.

6. Tighten validation.
   - `common/core` validates that arrangement and tone document refs are safe package-relative
     paths.
   - `common/audio` validates tone document sidecar refs and keeps them under the expected
     `tones/state/<tone-id>/` root.
   - Save fails on duplicate IDs or missing referenced files.

7. Add focused tests.
   - PSARC importer generates a fresh Rock Hero ID per arrangement and writes no PSARC
     identifiers into `song.json`.
   - Native package save/load round-trips ID-based arrangement files.
   - Tone capture creates `tones/<tone-id>.tone.json`.
   - Tone capture writes plugin state under `tones/state/<tone-id>/`.
   - Renaming a display name does not move or orphan package resources.

## Migration

No automated migration. The project is pre-1.0 and is being tested against a single working
package. Existing in-repo fixtures and the working test package are rewritten by hand alongside
the code change; old `tones/lead.tone.json` and `tones/state/lead/` layouts are not supported at
load time.

## Deferred Decisions

- Whether `Arrangement` should gain a display `name` separate from `part`.
- Whether tone documents need their own first-class metadata object in `common/core`, or can remain
  referenced only through `Arrangement::tone_document_ref` for now.
- Whether to embed a format/version marker in `song.json` (e.g. `"arrangementFormat":
  "psarc-xml-v1"`) ahead of a future Rock Hero-native arrangement format. Not needed until a
  second format is on the horizon.
