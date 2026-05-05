# Basic RHP Project File Plan

Status: in progress. This plan defines the first native Rock Hero project package format and the
minimal workflow needed to generate one from a user-supplied WAV file, then load it in the editor.

## Goal

Create a basic `.rhp` project package that can be generated from one WAV file and opened by the
editor. The opened project should display one arrangement with the backing waveform and be ready
for later chart and tone authoring work.

The first version should be intentionally small:

- one top-level project index file
- one bundled backing WAV file
- one Lead arrangement XML file
- enough metadata to display and reload the project
- no Rocksmith import, WEM conversion, plugin preset loading, or note authoring yet

## Format Decision

`.rhp` is a normal zip file with a Rock Hero-specific extension. The archive should contain a
simple directory layout:

```text
project.json
audio/
  backing.wav
arrangements/
  lead.xml
```

Use JSON for the root project index because it is a compact and direct match for project-level
metadata, asset lists, and arrangement references. Use XML for arrangement files because Rocksmith
arrangements are XML and Rock Hero will eventually need XML arrangement parsing for import work.

Do not name the root file `manifest.json`. Reserve "manifest" for Rocksmith import data and any
future package-only metadata. In Rock Hero, the root document is the editable project definition,
so `project.json` is clearer.

## Initial `project.json`

The first root document should describe project metadata, audio assets, and arrangement entries:

```json
{
  "formatVersion": 1,
  "metadata": {
    "title": "backing",
    "artist": "",
    "album": "",
    "year": 0
  },
  "audioAssets": [
    {
      "id": "backing",
      "path": "audio/backing.wav",
      "durationSeconds": 123.456
    }
  ],
  "arrangements": [
    {
      "id": "lead",
      "part": "Lead",
      "file": "arrangements/lead.xml",
      "audio": "backing"
    }
  ],
  "selectedArrangement": "lead"
}
```

The `audio` field is an asset id, not a file path. This keeps the model ready for shared backing
audio and later arrangement-specific overrides without duplicating paths in every arrangement XML.

## Initial Arrangement XML

The first arrangement XML should be a minimal placeholder with no editor-unused fields:

```xml
<?xml version="1.0" encoding="utf-8"?>
<Arrangement formatVersion="1" />
```

Audio assignment belongs in `project.json`, not in the arrangement XML, because backing audio is
usually global. Arrangement identity and part also belong in `project.json` for now because the
current editor only needs enough arrangement data to select a path and display its waveform. Add
notes, difficulty, tone timeline references, and other path-specific authoring data only when the
editor actually needs to read or write them.

## Dependency Direction

Add `nlohmann_json` for:

- Rock Hero `project.json`
- future Rocksmith `.hsan`
- future Rocksmith manifest `.json`

Add `pugixml` for:

- Rock Hero `arrangements/*.xml`
- future Rocksmith `songs/arr/*.xml`
- future Rocksmith `.xblock`

Keep both parser dependencies private to implementation files. Public Rock Hero headers should
continue to expose project-owned types such as `Song`, `Arrangement`, and `AudioAsset`, not parser
types.

## Archive Handling

Core should own project data parsing and writing, but it should not depend on JUCE. The archive
layer should therefore be separated from the document parser:

- core parses and writes an extracted project directory layout
- a thin archive adapter packs and extracts `.rhp` zip files
- editor/app workflow can use the archive adapter before calling core project loading

JUCE already provides `juce::ZipFile` and `juce::ZipFile::Builder`, so the first archive adapter can
use JUCE at the app/UI boundary if that is the fastest path. If `.rhp` packaging becomes broader
than the editor, replace that temporary boundary with a core-compatible zip dependency instead of
letting JUCE enter `rock-hero-core`.

## Audio Metadata Direction

Do not parse WAV headers in `rock-hero-core`. All audio inspection should go through the audio
layer using JUCE/Tracktion facilities.

Add or extend an audio-facing contract that can inspect a file before project generation:

```cpp
struct AudioFileInfo
{
    core::TimeDuration duration;
    int sample_rate{0};
    int channel_count{0};
};
```

The editor generation workflow should ask the audio engine for `AudioFileInfo`, then pass the
accepted duration into the project writer. On load, the editor should ask the audio engine to load
the selected arrangement's audio before committing the loaded project into `Session`.

## Generation Workflow

When the user provides a WAV file:

1. Probe the WAV through the audio engine.
2. Create a temporary staging directory.
3. Copy the WAV into `audio/`.
4. Generate `project.json` using the WAV filename as the default title.
5. Generate `arrangements/lead.xml`.
6. Pack the staging directory into an `.rhp` zip archive.
7. Optionally load the generated package immediately into the editor.

The generator should sanitize archive paths and preserve the original file extension. The first
version can normalize the copied file name to `backing.wav` for deterministic output.

## Load Workflow

When the editor opens an `.rhp` file:

1. Extract the zip to a temporary project cache directory.
2. Parse `project.json`.
3. Resolve project-relative paths.
4. Parse the selected arrangement XML.
5. Build a `core::Song` with one or more arrangements.
6. Ask the audio engine to load the selected arrangement's audio.
7. Commit the loaded song into `Session`.
8. Push derived editor state so `ArrangementView` displays the waveform.

If archive extraction, JSON parsing, XML parsing, or backend audio loading fails, preserve the
previous editor session and show a load error.

## Implementation Phases

### Phase 1: Format and Dependencies

- Add `nlohmann_json` to Conan and core CMake privately.
- Add `pugixml` to Conan and core CMake privately.
- Define core project load/create result structs with string error messages.
- Add project JSON parsing/writing for an extracted directory.
- Add arrangement XML parsing/writing for the minimal placeholder shell.

### Phase 2: Audio Probe Contract

- Add an audio-layer file-inspection contract or extend the existing edit-loading workflow.
- Implement WAV inspection with JUCE/Tracktion in `rock-hero-audio`.
- Keep `rock-hero-core` unaware of WAV internals.

### Phase 3: RHP Generation

- Add a generator workflow that accepts a WAV path and output `.rhp` path.
- Probe audio, stage files, write project documents, and pack the zip.
- Keep package creation deterministic enough for tests where possible.

### Phase 4: Editor Load and Display

- Add an "Open Project" controller intent for `.rhp`.
- Extract the package, load core project data, and backend-load the selected arrangement audio.
- Replace the session only after the core data and audio backend both succeed.
- Display the loaded arrangement waveform through the existing `ArrangementView`.

### Phase 5: Tests

- Core tests for `project.json` round-trip and path resolution.
- Core tests for minimal placeholder arrangement XML read/write.
- Workflow tests with fake audio probing and fake archive behavior.
- UI/controller tests proving failed project load preserves the old session.
- Focused archive tests around `.rhp` pack/extract if the adapter is small and deterministic.

## Non-Goals

- Rocksmith PSARC import.
- Rocksmith `.wem` or `.bnk` parsing.
- Rocksmith `.hsan`, manifest JSON, `.xblock`, or aggregate graph parsing.
- Multiple arrangements generated from one WAV in the first workflow.
- Plugin preset loading, tone automation editing, or VST chain serialization.
- Full note chart authoring.

## Open Questions

- Should the first archive adapter live in the editor app/UI boundary using `juce::ZipFile`, or
  should the project add a core-compatible zip dependency immediately?
- Should generated `.rhp` files preserve the original WAV basename or always use `audio/backing.wav`
  for deterministic packages?
- Should the generator ask for metadata up front, or should v1 derive title from the WAV filename
  and leave artist/album/year blank?
