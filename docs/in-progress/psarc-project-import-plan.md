# PSARC Project Import Plan

## Goal

Add a project import path that converts Rocksmith PSARC content into Rock Hero's native `.rhp`
project format without making PSARC a first-class project type inside the editor session.

The target shape is:

- `Session` owns the currently editable `Song`.
- `Project` owns native project package/workspace behavior and exposes native `load` and `save`.
- `IProjectImporter` owns conversion from a foreign package into a project-neutral import result.
- `Project::import` uses an importer to commit the converted content into a native `.rhp`.

This keeps foreign package parsing at the boundary while leaving the editor and game to work with
the normal Rock Hero `Song` model.

## Current State

- Native `.rhp` packages store song-level data in `song.json`.
- `Project::load(path)` reads a native project package and returns a `Song`.
- `Project::save(song)` writes a native project package from the caller's current `Song`.
- The editor controller loads into a temporary project first and only replaces the current project
  after the song is accepted by the audio/editor state.
- `open-psarc` is already listed as the PSARC-format dependency in the design docs.

## Proposed Public API

Add an importer contract in `rock-hero-core`, with no PSARC-specific types in the public API:

```cpp
struct ProjectImport
{
    Song song;
    std::filesystem::path workspace_directory;
};

class IProjectImporter
{
public:
    virtual ~IProjectImporter() = default;

    [[nodiscard]] virtual bool canImport(const std::filesystem::path& path) const = 0;

    [[nodiscard]] virtual std::expected<ProjectImport, std::string> importProject(
        const std::filesystem::path& path) = 0;
};
```

`ProjectImport::workspace_directory` is a staging folder containing any converted assets referenced
by the returned `Song`. Paths inside the returned song should already be relative to that staging
workspace.

Extend `Project` with an import operation:

```cpp
[[nodiscard]] std::expected<Song, std::string> import(
    const std::filesystem::path& source_path,
    const std::filesystem::path& target_path,
    IProjectImporter& importer);
```

`Project::import` should:

- reject sources that the importer does not support;
- ask the importer for a converted `ProjectImport`;
- create a native project workspace for `target_path`;
- copy or move the staged assets into the native workspace;
- save the converted song as `song.json` in a native `.rhp`;
- update the `Project` instance to refer to `target_path` only after commit succeeds;
- return the converted `Song` for the caller to load into `Session`.

The concrete PSARC implementation can be:

```cpp
class PsarcProjectImporter final : public IProjectImporter
{
public:
    [[nodiscard]] bool canImport(const std::filesystem::path& path) const override;

    [[nodiscard]] std::expected<ProjectImport, std::string> importProject(
        const std::filesystem::path& path) override;
};
```

## Ownership And Failure Rules

- `IProjectImporter` owns foreign-format extraction and conversion.
- `Project` owns native `.rhp` workspace/package layout and commit behavior.
- `Session` never stores `Project`; it only stores the editable `Song`.
- The editor controller should continue using a temporary `Project` during import.
- On import failure, the currently loaded project and session song must remain unchanged.
- On audio/editor-load failure after import, the controller should not replace the current project.
- No PSARC paths should leak into the saved native project unless they intentionally point to
  imported asset copies inside the native workspace.

## Implementation Phases

### Phase 1: Importer Contract

- Add `ProjectImport` and `IProjectImporter` public headers under
  `libs/rock-hero-core/include/rock_hero/core/`.
- Add Doxygen for the new public types.
- Add focused tests using a fake importer to prove success, unsupported-file, and conversion-error
  behavior without involving PSARC archives.

### Phase 2: Native Import Commit

- Implement `Project::import` in `project.cpp`.
- Reuse the existing native save path so imported projects are persisted exactly like authored
  Rock Hero projects.
- Normalize and validate staged asset paths before copying them into the native workspace.
- Reject unsafe paths that escape the staging workspace.
- Ensure a failed import leaves the existing `Project` state untouched.

### Phase 3: PSARC Extraction Adapter

- Add `PsarcProjectImporter` as the first concrete `IProjectImporter`.
- Link `open-psarc` privately from `rock_hero_core`; do not expose it in public headers.
- Extract the archive into a temporary staging directory.
- Convert Rocksmith audio and arrangement data into files the existing Rock Hero loaders can read.
- Catch PSARC/conversion exceptions and map them to `std::expected` error strings.

### Phase 4: Minimal Song Conversion

- Read Rocksmith metadata into `Song::metadata`.
- Detect playable arrangements and map them to Rock Hero `Part` values.
- Require at least one arrangement with an audio asset.
- Set arrangement audio assets to staged relative paths.
- Preserve unavailable or not-yet-supported data by leaving current Rock Hero fields empty rather
  than inventing partial semantics.

### Phase 5: Arrangement Data Conversion

- Parse converted Rocksmith arrangement data into Rock Hero `NoteEvent` values.
- Start with single notes and basic timing/fret/string fields.
- Add chord and technique support only when the Rock Hero model has fields that can represent them
  cleanly.
- Keep unsupported Rocksmith detail out of the public importer contract.

### Phase 6: Editor Workflow

- Add an explicit import command beside the existing native open command.
- Let the user pick a source PSARC and a target `.rhp` path.
- Use a temporary `Project` and the production `PsarcProjectImporter`.
- Load the returned `Song` into the editor session only after the native import commit succeeds.
- Display "No project loaded." when no song is present in the session.

## Testing Plan

- Unit test `Project::import` with fake importers for success and failure paths.
- Verify imported asset copying with a temporary staged workspace fixture.
- Verify path traversal and absolute staged paths are rejected.
- Verify failed import does not alter an already loaded `Project`.
- Verify a successful import can be loaded again through `Project::load`.
- Add PSARC importer tests with a small fixture once a legally redistributable sample is available.
- Prefer an extracted-directory converter fixture first if a binary PSARC fixture is not practical.

## Open Questions

- Should the target `.rhp` path be chosen by the caller, or should `Project::import` derive it from
  the PSARC file name?
- Should imported audio be stored under `audio/`, `assets/audio/`, or another native workspace
  folder?
- Should PSARC XML parsing add a direct `pugixml` dependency to `rock_hero_core`, or should it go
  through conversion helpers exposed by `open-psarc`?
- What minimum Rocksmith data should be required before an import is considered valid?
- How should unsupported Rocksmith techniques be reported to the user without blocking basic
  import?

## Acceptance Criteria

- A native project can still be loaded and saved through `Project`.
- A PSARC source can be converted through `IProjectImporter` into a native `.rhp`.
- The returned `Song` can be loaded into `Session` without `Session` knowing about PSARC.
- Saved imported projects contain `song.json` and copied native asset files.
- Import failures return clear errors and preserve the previous loaded session/project state.
