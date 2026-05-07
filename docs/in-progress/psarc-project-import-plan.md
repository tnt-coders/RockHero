# PSARC Project Import Plan

## Goal

Add a project import path that converts Rocksmith PSARC content into Rock Hero's native `.rhp`
project format without making PSARC a first-class project type inside the editor session.

The target shape is:

- `Session` owns the currently editable `Song`.
- `Project` owns native project package/workspace behavior and exposes native `load` and `save`.
- `IProjectImporter` owns conversion from a foreign package into a caller-provided workspace.
- `Project::import` uses an importer to populate a new unsaved workspace and returns a `Song`.
- `Project::saveAs` writes an unsaved imported workspace to a native `.rhp`.

This keeps foreign package parsing at the boundary while leaving the editor and game to work with
the normal Rock Hero `Song` model.

## Current State

- Native `.rhp` packages store song-level data in `song.json`.
- `Project::load(path)` reads a native project package and returns a `Song`.
- `Project::save(song)` writes a native project package from the caller's current `Song`.
- `Project::saveAs(path, song)` writes the current workspace to a selected native package path.
- The editor controller loads into a temporary project first and only replaces the current project
  after the song is accepted by the audio/editor state.
- `open-psarc` is already listed as the PSARC-format dependency in the design docs.

## Proposed Public API

Add an importer contract in `rock-hero-core`, with no PSARC-specific types in the public API:

```cpp
class IProjectImporter
{
public:
    virtual ~IProjectImporter() = default;

    [[nodiscard]] virtual std::expected<Song, std::string> importProject(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) = 0;
};
```

`workspace_directory` is created and owned by `Project::import`. The importer must copy or convert
the minimum required files into that workspace and return a `Song` whose arrangement audio paths
resolve inside it.

Extend `Project` with an import operation:

```cpp
[[nodiscard]] std::expected<Song, std::string> import(
    const std::filesystem::path& source_path,
    IProjectImporter& importer);

[[nodiscard]] std::expected<void, std::string> saveAs(
    const std::filesystem::path& path,
    const Song& song);
```

`Project::import` should:

- create a new temporary native workspace;
- ask the importer to populate that workspace;
- validate that imported arrangements have audio inside the new workspace;
- update the `Project` instance to own the new workspace only after import succeeds;
- leave `Project::path()` empty because no native package has been saved yet;
- return the converted `Song` for the caller to load into `Session`.

`Project::saveAs` should write the current workspace and supplied `Song` to a selected `.rhp` path,
then set `Project::path()` after the package write succeeds.

The concrete PSARC implementation can be:

```cpp
class PsarcImporter final : public IProjectImporter
{
public:
    [[nodiscard]] std::expected<Song, std::string> importProject(
        const std::filesystem::path& source_path,
        const std::filesystem::path& workspace_directory) override;
};
```

## Ownership And Failure Rules

- `IProjectImporter` owns foreign-format extraction and conversion.
- `Project` owns native `.rhp` workspace/package layout and commit behavior.
- `Session` never stores `Project`; it only stores the editable `Song`.
- The editor controller should continue using a temporary `Project` during import.
- On import failure, the currently loaded project and session song must remain unchanged.
- A successfully imported project is unsaved until `Project::saveAs` chooses a package path.
- On audio/editor-load failure after import, the controller should not replace the current project.
- No PSARC paths should leak into the saved native project unless they intentionally point to
  imported asset copies inside the native workspace.

## Implementation Phases

### Phase 1: Importer Contract

- Add `IProjectImporter` public header under `libs/rock-hero-core/include/rock_hero/core/`.
- Add Doxygen for the new public interface.
- Add focused tests using a fake importer to prove success, unsupported-file, and conversion-error
  behavior without involving PSARC archives.

### Phase 2: Native Import Commit

- Implement `Project::import` in `project.cpp`.
- Implement `Project::saveAs` in `project.cpp`.
- Keep import and native package writing separate; import creates an unsaved workspace.
- Normalize and validate imported asset paths before exposing them through the returned `Song`.
- Reject unsafe paths that escape the imported workspace.
- Ensure a failed import leaves the existing `Project` state untouched.

### Phase 3: PSARC Extraction Adapter

- Add `PsarcImporter` as the first concrete `IProjectImporter`.
- Link `open-psarc` privately from `rock_hero_core`; do not expose it in public headers.
- Extract the archive into the `Project::import` workspace directory.
- Copy associated audio into the workspace.
- Copy arrangement XML files into the workspace as-is for the first importer version.
- Read only the song metadata, audio, and arrangement references needed to build the `Song`.
- Catch PSARC/conversion exceptions and map them to `std::expected` error strings.

### Phase 4: Minimal Song Conversion

- Read Rocksmith metadata into `Song::metadata`.
- Detect playable arrangements and map them to Rock Hero `Part` values.
- Require at least one arrangement with an audio asset.
- Set arrangement audio assets to workspace-local paths.
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
- Let the user pick a source PSARC.
- Use a temporary `Project` and the production `PsarcImporter`.
- Load the returned `Song` into the editor session only after the native import commit succeeds.
- Treat the imported project as unsaved until the user saves or chooses Save As.
- Display "No project loaded." when no song is present in the session.

## Testing Plan

- Unit test `Project::import` with fake importers for success and failure paths.
- Verify imported asset copying with a temporary staged workspace fixture.
- Verify path traversal and absolute staged paths are rejected.
- Verify failed import does not alter an already loaded `Project`.
- Verify a successful import has an empty package path until `saveAs` succeeds.
- Verify a successful `saveAs` can be loaded again through `Project::load`.
- Add PSARC importer tests with a small fixture once a legally redistributable sample is available.
- Prefer an extracted-directory converter fixture first if a binary PSARC fixture is not practical.

## Open Questions

- How should the editor suggest a default Save As path for an imported PSARC?
- Should imported audio be stored under `audio/`, `assets/audio/`, or another native workspace
  folder?
- Should PSARC XML parsing add a direct `pugixml` dependency to `rock_hero_core`, or should it go
  through conversion helpers exposed by `open-psarc`?
- What minimum Rocksmith data should be required before an import is considered valid?
- How should unsupported Rocksmith techniques be reported to the user without blocking basic
  import?

## Acceptance Criteria

- A native project can still be loaded and saved through `Project`.
- A PSARC source can be converted through `IProjectImporter` into an unsaved native workspace.
- The returned `Song` can be loaded into `Session` without `Session` knowing about PSARC.
- Saved imported projects contain `song.json`, copied native asset files, and arrangement XML.
- Import failures return clear errors and preserve the previous loaded session/project state.
