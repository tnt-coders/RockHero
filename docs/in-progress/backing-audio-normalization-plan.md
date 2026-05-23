# Backing Audio Normalization Plan

Status: planning. No implementation has landed for this plan yet.

## Goal

Normalize imported backing audio during project import so songs have a reasonably consistent
perceived playback level before authors design guitar tones against them.

The first target is:

- integrated loudness: `-16 LUFS`
- peak ceiling: `-2 dBTP`
- output format: canonical project-owned WAV
- source retention: do not keep the original unnormalized imported audio in the project package

The waveform should be generated from the normalized canonical audio file, so visual waveform size
tracks the audio that actually plays.

Persist loudness metadata with each backing audio asset so later project opens can cheaply tell
whether the asset was already normalized to the current target. Existing projects that lack this
metadata should still open immediately, then analyze backing audio in the background and prompt the
user if normalization is recommended.

## Non-Goals

Do not implement a backing-track mixer as part of this plan.

Do not add pan controls, per-track backing volume automation, master-bus UI, or mixer channel
view-state types. A later guitar-chain design can still add an input gain control before the signal
chain and an output gain control after the signal chain.

Do not normalize tone or live guitar volume. Guitar tone loudness must remain author-controlled
because it depends on live performance, plugin choices, automation, and song section.

Do not block project open on a full LUFS scan. LUFS analysis requires reading the whole audio file,
so stale or missing loudness metadata should trigger background analysis after the project is
usable.

Do not automatically rewrite existing project audio during load. If background analysis finds audio
outside the normalization target, the editor should ask before rendering and replacing the
canonical audio file.

## Framework and Dependency Findings

Local JUCE and Tracktion source checks did not reveal a built-in LUFS, EBU R128, BS.1770, true-peak
loudness normalizer, or ReplayGain-style analyzer suitable for this feature.

Relevant local findings:

- JUCE's `WavAudioFormat` exposes ASWG/iXML metadata names such as `aswgLoudness`,
  `aswgLoudnessRange`, and `aswgMaxPeak`, but those are metadata fields, not a loudness analyzer.
- Tracktion Engine has a `NormaliseEffect`, but the render job reads max levels and applies gain
  from peak level. That is useful context, but it is not LUFS normalization.
- Tracktion's transient detection path also uses peak normalization internally for analysis.
  That confirms Tracktion has utility peak normalization code, not the import loudness policy
  needed here.

The chosen external dependency is `libebur128`:

- upstream: <https://github.com/jiixyj/libebur128>
- license: MIT
- stated purpose: EBU R128 loudness normalization support
- useful capabilities: integrated loudness, loudness range, true peak scanning

Dependency route is decided. The project ships a Conan 2 recipe for `libebur128/1.2.6` under the
`conan-recipes/` submodule (`conan-recipes/recipes/libebur128/all/conanfile.py`). The
`project-config/cmake-conan` provider auto-registers that checkout as a `--recipes-only`
local-recipes-index remote during configure, so `conanfile.txt` only needs the bare
`libebur128/1.2.6` requirement. No ConanCenter fallback is needed.

The remaining verification work is to confirm the recipe builds clean on MSVC and that the
`test_package` step passes once locally before relying on it for normalization work.

Avoid FFmpeg for the first pass. It is heavier than this feature needs, complicates packaging, and
would likely push the implementation toward external-process behavior instead of a small
in-process audio adapter.

## Architecture

`rock-hero-common/audio` owns the normalization primitive because it is shared audio-adjacent
behavior and will be needed by both the editor and game import/configuration paths over time. The
implementation can use JUCE audio format readers and writers plus the chosen loudness analyzer.

`rock-hero-editor/core` owns the import workflow policy. It should decide when imported audio is
normalized, which project paths become canonical, and how import failures are translated to
`ProjectError`.

`rock-hero-common/core` should persist loudness metadata as plain data on `AudioAsset`. This is a
schema concern, not analyzer policy. Common core should not know how to measure LUFS or true peak;
it should only serialize and deserialize the metadata produced by the audio boundary.

This revises the earlier "no song-schema change" assumption. Open-time detection without rescanning
every project needs durable metadata, and the song package is the right place to store it because
both editor and game will consume the same backing audio assets.

`rock-hero-editor/core` should own the prompt policy for loaded projects. Opening a project should
return quickly. If the song's audio metadata is missing, stale, or outside tolerance, the controller
can schedule background analysis and then publish prompt state to the view.

## Types to Add

Add these public types in
`rock-hero-common/core/include/rock_hero/common/core/audio_loudness_metadata.h`.

`AudioFileFingerprint`

- Value type used to decide whether stored loudness metadata still describes the current file.
- Expected fields:
  - `size_bytes`
  - `last_write_time` (`std::filesystem::file_time_type`)
- The canonical normalized WAV lives inside a project-owned workspace and is only written by this
  feature, so size plus mtime is enough to detect "the file changed since we measured it." A
  content hash is intentionally not used because it adds cost without a real threat model for
  project-owned audio.

`AudioLoudnessMeasurement`

- Value type containing integrated loudness and peak measurement fields.
- Expected fields:
  - `integrated_loudness_lufs`
  - `true_peak_dbtp`

If the chosen analyzer cannot provide true peak in the first pass, use sample-peak field names and
do not expose the value as `dBTP`.

`AudioLoudnessAnalysis`

- Value type bundling everything a single read of the file produces.
- Expected fields:
  - `measurement` (`AudioLoudnessMeasurement`)
  - `fingerprint` (`AudioFileFingerprint`)
  - `analyzer_id`
  - `analyzer_version`
- Returned by `measureAudioLoudness`. Embedded inside `AudioLoudnessMetadata`. Having one shape
  for "what we just read off disk" avoids reshaping between the analyzer return value and the
  persisted record.

`AudioLoudnessMetadata`

- Value type persisted on `AudioAsset`.
- Expected fields:
  - `target` (`AudioNormalizationTarget`)
  - `analysis` (`AudioLoudnessAnalysis`)
- Persists *what the file is* plus *what target it was normalized against*. Render-operation
  trivia (`applied_gain_db`, `limited_by_peak_ceiling`) is intentionally not persisted: it is
  available in the normalization return value for logging and tests, but once the source audio is
  deleted those fields cannot be reconstructed or verified and so do not belong in durable schema.
- This type is durable package data, so keep it framework-free and independent of `common/audio`.

`AudioNormalizationTarget`

- Value type containing the requested integrated loudness and peak ceiling.
- Initial defaults: `integrated_loudness_lufs = -16.0`, `true_peak_ceiling_dbtp = -2.0`.
- Placed in `common/core` (not `common/audio`) because it is referenced by the durable
  `AudioLoudnessMetadata`.

Modify `AudioAsset` to carry:

```cpp
std::optional<AudioLoudnessMetadata> loudness_metadata;
```

Add these public types in
`rock-hero-common/audio/include/rock_hero/common/audio/audio_normalization.h`.

`AudioNormalizationOutcome`

- In-memory value returned by `normalizeAudioFile`, containing the durable metadata to persist
  plus render-operation context that is only useful to the immediate caller.
- Expected fields:
  - `metadata` (`AudioLoudnessMetadata`) — attached to the output `AudioAsset`.
  - `source_measurement` (`AudioLoudnessMeasurement`) — what the input read as before gain was
    applied.
  - `applied_gain_db`
  - `limited_by_peak_ceiling`
- Not persisted. If the analyzer cannot provide true peak in the first pass, rename the peak
  fields to sample-peak names and do not call the `-2 dB` ceiling `dBTP` in code or UI.

`AudioNormalizationErrorCode`

- Stable error enum for the public normalization boundary.
- Expected values:
  - `InputFileMissing`
  - `OutputPathRequired`
  - `CouldNotOpenInput`
  - `UnsupportedInputFormat`
  - `InvalidInputAudio`
  - `CouldNotCreateOutputDirectory`
  - `CouldNotCreateOutputFile`
  - `LoudnessMeasurementFailed`
  - `SilentInputCannotBeNormalized`
  - `OutputRenderFailed`
  - `OutputValidationFailed`
  - `TemporaryOutputCleanupFailed`

`AudioNormalizationError`

- `[[nodiscard]]` error value with `AudioNormalizationErrorCode code` and `std::string message`.
- Follow the existing project error style: constructor from code, constructor from code plus
  contextual message, and no equality operator unless structured comparisons become useful.

`normalizeAudioFile`

- Public free function:

```cpp
[[nodiscard]] std::expected<AudioNormalizationOutcome, AudioNormalizationError> normalizeAudioFile(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const common::core::AudioNormalizationTarget& target);
```

The implementation may use a temporary sibling path internally before replacing the final output
path.

`measureAudioLoudness`

- Public free function:

```cpp
[[nodiscard]] std::expected<common::core::AudioLoudnessAnalysis, AudioNormalizationError>
measureAudioLoudness(const std::filesystem::path& input);
```

Used for background checks on existing projects without rendering a new file. Returns the same
shape that is embedded inside the durable metadata, so editor/core can compare a fresh analysis
against `AudioLoudnessMetadata::analysis` field-for-field.

Use the same error domain as normalization unless analysis grows enough distinct failure modes to
justify a separate public error type.

No new concrete service class is planned. Do not add `IAudioNormalizer`,
`AudioNormalizationService`, or a broad wrapper around JUCE/Tracktion just for this feature.

Add this alias in `rock-hero-editor/core/include/rock_hero/editor/core/project.h`:

```cpp
using AudioNormalizeFunction = std::function<
    std::expected<common::audio::AudioNormalizationOutcome, common::audio::AudioNormalizationError>(
        const std::filesystem::path& input,
        const std::filesystem::path& output,
        const common::core::AudioNormalizationTarget& target)>;
```

Then extend `Project::import` with an optional target and normalization function defaulting to
`common::audio::normalizeAudioFile`. Passing the target as a separate parameter keeps test fakes
target-agnostic — they can ignore the target value entirely instead of having to round-trip it
through the bound function:

```cpp
[[nodiscard]] std::expected<common::core::Song, ProjectError> import(
    const std::filesystem::path& source_path,
    ISongImporter& importer,
    const common::core::AudioNormalizationTarget& target = {},
    const AudioNormalizeFunction& normalize_audio = common::audio::normalizeAudioFile);
```

Add this alias in the editor/core boundary that schedules background project checks:

```cpp
using AudioAnalyzeFunction = std::function<
    std::expected<common::core::AudioLoudnessAnalysis,
                  common::audio::AudioNormalizationError>(
        const std::filesystem::path& input)>;
```

Expose this through `EditorController::Services`, along with a normalization function for accepted
open-time prompts, so controller tests can cover the workflow through public controller APIs. The
controller layer owns the active `AudioNormalizationTarget` value; the analyze and normalize
functions injected into `Services` do not need to know it.

Add these view-state types in `rock-hero-editor/core/include/rock_hero/editor/core/editor_view_state.h`
if the prompt is implemented as durable controller state:

`BackingAudioNormalizationPrompt`

- Contains the asset path or display name, measured loudness, target loudness, peak information,
  and whether multiple shared arrangements use the asset.
- The view renders this as a popup or equivalent prompt.

`BackingAudioNormalizationDecision`

- Scoped enum for the user's response.
- Expected values: `Normalize`, `Dismiss`.

Add `std::optional<BackingAudioNormalizationPrompt>` to `EditorViewState`.

## Files to Add

`rock-hero-common/core/include/rock_hero/common/core/audio_loudness_metadata.h`

- Public framework-free loudness metadata and fingerprint values persisted by song packages.

`rock-hero-common/audio/include/rock_hero/common/audio/audio_normalization.h`

- Public analysis and normalization request, result, error, target, and free-function API.
- This is the only new public common/audio header planned.

`rock-hero-common/audio/src/audio_normalization.cpp`

- JUCE/libebur128-backed implementation.
- File-local helpers should stay here unless a helper becomes a real public concept.

`rock-hero-common/audio/tests/test_audio_normalization.cpp`

- Tests through the public `measureAudioLoudness` and `normalizeAudioFile` APIs.
- Generate small WAV fixtures inside the test temp directory instead of committing large audio
  binaries unless fixture stability becomes more important than size.

## Files to Modify

`conanfile.txt`

- Add `libebur128` only if an acceptable Conan recipe is available or added.

`cmake/RockHeroExternalModules.cmake`

- Add or adjust dependency wiring only if the chosen dependency requires a project-owned wrapper
  target. If Conan provides a normal CMake package target, keep this change minimal.

`rock-hero-common/audio/CMakeLists.txt`

- Add `audio_normalization.cpp`.
- Link `rock_hero::juce_audio_formats` privately for readers and WAV writing.
- Link the chosen loudness dependency privately.

`rock-hero-common/audio/tests/CMakeLists.txt`

- Add `test_audio_normalization.cpp`.

`rock-hero-editor/core/include/rock_hero/editor/core/project.h`

- Include the normalization header and `<functional>`.
- Add `AudioNormalizeFunction`.
- Extend `Project::import` with the optional normalization function parameter.

`rock-hero-editor/core/src/project.cpp`

- Rename the current private `normalizeImportedSong` helper because it does not normalize audio.
  Suggested name: `resolveImportedSongAudioPaths`.
- Add import-time normalization and canonical asset replacement.
- Attach `AudioNormalizationOutcome::metadata` to the normalized `AudioAsset`.
- Translate `AudioNormalizationError` to `ProjectError`.

`rock-hero-common/core/include/rock_hero/common/core/audio_asset.h`

- Include optional loudness metadata on `AudioAsset`.

`rock-hero-common/core/src/rock_song_package.cpp`

- Persist optional loudness metadata under each `audioAssets` entry in `song.json`.
- Read packages without loudness metadata as valid older packages.
- Reject malformed loudness metadata only when the optional metadata object is present.

`rock-hero-common/core/tests`

- Add or update package serialization tests for audio assets with and without loudness metadata.

`rock-hero-editor/core/include/rock_hero/editor/core/project_error.h`

- Add `AudioNormalizationFailed`.

`rock-hero-editor/core/src/project_error.cpp`

- Add the default message for `AudioNormalizationFailed`.

`rock-hero-editor/core/tests/test_project.cpp`

- Test `Project::import` through fake importers and fake `AudioNormalizeFunction` values.
- Verify normalized imports persist loudness metadata on the resulting `AudioAsset`.

`rock-hero-editor/core/tests/test_editor_controller.cpp`

- Update import-flow tests if expectations need to account for `.wav` canonical audio paths.
- Add project-open tests that missing or stale loudness metadata schedules background analysis
  without blocking the open.
- Add prompt-decision tests for accepting or dismissing recommended normalization.

`rock-hero-editor/core/include/rock_hero/editor/core/editor_view_state.h`

- Add prompt state only if the editor owns the popup through normal view-state flow.

`rock-hero-editor/core/include/rock_hero/editor/core/i_editor_controller.h`

- Add a user-intent method for the backing-audio normalization prompt response if prompt state is
  routed through `EditorViewState`.

`rock-hero-editor/core/include/rock_hero/editor/core/editor_controller.h`

- Add analysis and normalization functions to `EditorController::Services` for open-time
  background checks and accepted prompt handling.

`rock-hero-editor/core/include/rock_hero/editor/core/busy_view_state.h`

- Add `BusyOperation::NormalizingBackingAudio` for the accepted rewrite operation.

## Implementation Flow

1. The importer copies or converts the source package into the temporary song workspace exactly as
   it does today.
2. `Project::import` validates that imported arrangements exist and reference audio inside the
   song workspace.
3. `Project::import` gathers unique imported `AudioAsset` paths.
4. For each unique audio path, create a unique sibling canonical WAV path under `song/audio/`. The
   normalized path must itself be inside the song workspace — the existing
   `relativeWorkspacePath` containment check is run again after substitution, so a normalized
   path that escapes the workspace fails with `InvalidImportedSong` just like a malformed source
   path would.
5. Call `normalize_audio` for each unique source path.
6. Replace every arrangement reference to the raw source path with the normalized WAV asset,
   including the returned loudness metadata (from `AudioNormalizationOutcome::metadata`).
7. Delete raw unnormalized audio files that are no longer referenced.
8. Resolve final arrangement audio paths against the project workspace.
9. Commit the imported project workspace to the `Project` instance.

If any normalization fails, return `ProjectErrorCode::AudioNormalizationFailed` and let the
temporary import workspace be removed by normal project cleanup. Do not partially commit a project
with a mix of raw and normalized audio.

## Open-Time Detection Flow

Opening a project should not synchronously analyze LUFS. Use this flow instead:

1. Load the project and prepare the arrangement exactly as today.
2. For each unique backing `AudioAsset`, inspect `loudness_metadata`.
3. If metadata exists, matches the current target and analyzer identity, and its fingerprint is
   trusted, do nothing.
4. If metadata is missing or stale, schedule `measureAudioLoudness` on editor background work.
5. When analysis completes, compare measured loudness and peak to the current target.
6. If normalization is recommended, publish `BackingAudioNormalizationPrompt` in
   `EditorViewState`.
7. If the user accepts, render a normalized WAV to a temporary path, replace the canonical audio
   file only after success, update `AudioAsset::loudness_metadata`, mark the project dirty, and
   refresh playback/waveform state.
8. If the user dismisses, clear the prompt and leave the project unchanged.

The first prompt threshold should be tolerant rather than exact. A reasonable starting point is to
prompt when integrated loudness differs from target by more than `1.0 LU`, or when true peak is
above the configured ceiling. Tune this after hearing real imported songs.

## Normalization Algorithm

The first pass should be a gain-only render:

```text
desired_gain_db = target_lufs - source_integrated_loudness_lufs
peak_limited_gain_db = target_true_peak_ceiling_dbtp - source_true_peak_dbtp
applied_gain_db = min(desired_gain_db, peak_limited_gain_db)
```

If `applied_gain_db` is lower than `desired_gain_db`, set `limited_by_peak_ceiling = true`. In that
case the output is intentionally quieter than the LUFS target because avoiding clipping is more
important than forcing every track to exactly `-16 LUFS`.

Do not add a limiter in the first pass. Limiting would change the backing track sound and requires
more subjective validation than gain normalization.

Render as WAV while preserving source sample rate and channel count. Use 24-bit PCM WAV for the
initial implementation unless JUCE writer limitations make 32-bit float WAV materially simpler.
Do not resample during normalization.

Write to a temporary output file first, validate it can be opened and measured, then atomically
replace or rename to the final output path when possible.

## Function-Level Plan

In `audio_normalization.cpp`, keep these helpers file-local unless tests or other modules need a
real public boundary:

`createAudioFormatManager`

- Registers the JUCE formats needed to read current imports and write WAV.

`openAudioReader`

- Opens the input file and translates reader failures to `AudioNormalizationError`.

`runLoudnessAnalyzer`

- Streams decoded audio blocks into the chosen loudness analyzer.
- Returns `AudioLoudnessMeasurement` (integrated loudness and true peak).
- Distinct from the public `measureAudioLoudness` free function, which composes this helper with
  `fingerprintAudioFile` and analyzer-identity strings to produce a full `AudioLoudnessAnalysis`.

`fingerprintAudioFile`

- Produces `AudioFileFingerprint` (size + last-write-time) from `std::filesystem` queries.
- No file streaming required; runs in constant time regardless of audio length.

`calculateNormalizationGainDb`

- Pure helper for the gain formula.
- This can be directly covered by public API tests using generated input files; do not expose it
  unless implementation complexity proves it needs isolated tests.

`renderNormalizedAudioFile`

- Streams decoded input to the WAV writer while applying the calculated gain.
- Must avoid loading full songs into memory.

`validateRenderedAudioFile`

- Reopens and measures the rendered WAV so the result reports the actual output.
- Produces the `AudioLoudnessAnalysis` value that becomes
  `AudioNormalizationOutcome::metadata.analysis`, which is attached to the output `AudioAsset`.

`removePartialOutputOnFailure`

- Deletes temporary output files on failure and reports cleanup failure when it matters.

In `project.cpp`, keep these helpers file-local:

`resolveImportedSongAudioPaths`

- Replacement name for the current `normalizeImportedSong`.
- Validates arrangement audio references and resolves paths inside the workspace.

`collectUniqueAudioAssets`

- Produces one source audio path per unique imported asset.

`uniqueNormalizedAudioPath`

- Chooses a collision-free `.wav` path under `song/audio/`.

`normalizeImportedAudioAssets`

- Coordinates calls to `AudioNormalizeFunction` and builds a source-to-output path map.

`replaceArrangementAudioAssets`

- Rewrites arrangements to the canonical normalized WAV paths.

`removeRawImportedAudioAssets`

- Deletes unnormalized source audio files after every arrangement has been retargeted.

`projectErrorFromNormalizationError`

- Converts `common::audio::AudioNormalizationError` to `ProjectErrorCode::AudioNormalizationFailed`
  while preserving the normalization error message.

In `editor_controller.cpp`, keep open-time detection behind controller workflow:

`scheduleBackingAudioNormalizationCheck`

- Runs after project load succeeds and state has been pushed to the view.
- Filters unique backing audio assets that need metadata validation or background analysis.

`isBackingAudioNormalizationCurrent`

- Pure helper that compares stored metadata to the current target, analyzer identity, and
  fingerprint policy.

`showBackingAudioNormalizationPrompt`

- Publishes prompt view state after background analysis says normalization is recommended.

`applyBackingAudioNormalizationPrompt`

- Handles the user's accepted prompt by rendering the normalized WAV, updating the song model,
  marking the project dirty, and refreshing waveform/playback state.

`dismissBackingAudioNormalizationPrompt`

- Clears prompt view state without changing project audio.

## Testing Plan

`rock-hero-common/audio/tests/test_audio_normalization.cpp`

- `measureAudioLoudness` returns measurement and fingerprint for a generated WAV.
- Missing input returns `InputFileMissing` from both `measureAudioLoudness` and
  `normalizeAudioFile`.
- Empty output path returns `OutputPathRequired`.
- Unsupported input returns `UnsupportedInputFormat` or `CouldNotOpenInput`, depending on the
  failure point.
- A generated sine/noise WAV louder than `-16 LUFS` is rendered quieter.
- A generated sine/noise WAV quieter than `-16 LUFS` is rendered louder.
- Peak-ceiling behavior caps the gain and sets `AudioNormalizationOutcome::limited_by_peak_ceiling`
  to true.
- Silent input returns `SilentInputCannotBeNormalized`.
- Failure cleanup removes temporary output files.
- `AudioNormalizationOutcome::metadata.analysis` matches a fresh `measureAudioLoudness` reading of
  the rendered output file (within LUFS/peak tolerance).

Use tolerance-based assertions for LUFS and peak values. Exact values will vary slightly by analyzer
and generated fixture.

`rock-hero-common/core/tests`

- Song packages without an audio loudness metadata field still load (older packages remain valid).
- Song packages with an explicit `null` loudness metadata field load identically to packages with
  the field omitted entirely.
- Song packages with valid audio loudness metadata round-trip every field: target, analysis
  measurement, fingerprint, and analyzer identity.
- Malformed optional loudness metadata fails with `InvalidAudioAsset` only when the metadata
  object is present.

`rock-hero-editor/core/tests/test_project.cpp`

- `Project::import` calls the provided fake normalizer through the public import API.
- Arrangements that shared one raw asset all point at one normalized WAV after import.
- Arrangements that shared one raw asset all receive the same persisted loudness metadata.
- The raw imported audio file is removed after successful normalization.
- Normalization failure returns `AudioNormalizationFailed` and does not commit the project
  workspace.
- Existing invalid-import tests still fail before normalization when arrangements are missing or
  audio paths are outside the workspace.

`rock-hero-editor/core/tests/test_editor_controller.cpp`

- Keep tests focused on controller-observable behavior.
- Opening a project with current metadata should not schedule analysis.
- Opening a project with missing metadata should schedule background analysis after the project is
  visible.
- Background analysis outside tolerance should set `BackingAudioNormalizationPrompt`.
- Dismissing the prompt should not mutate the project.
- Accepting the prompt should normalize, update metadata, refresh audio/waveform state, and mark
  the project dirty.

## Review Checkpoints Before Coding

Before implementation starts:

1. Verify the project-owned `libebur128/1.2.6` Conan recipe builds clean on MSVC and that its
   `test_package` step passes.
2. Confirm whether the first WAV writer should be 24-bit PCM or 32-bit float.
3. Confirm the initial prompt tolerance for "needs normalization".
4. Re-check whether any existing tests assert raw imported audio filenames.

Before implementation is considered complete:

1. Verify the common/audio public API is the only normalization API tests need.
2. Verify `Project::import` remains the public editor/core test surface.
3. Verify no service/interface/controller class was added just to satisfy tests.
4. Verify raw imported audio is not retained in `.rhp` or `.rock` packages after normalization.
5. Verify waveform and playback both use the normalized canonical audio path.
6. Verify project open does not synchronously scan full audio files.
7. Verify old projects without loudness metadata still load and can prompt after background
   analysis.

## References Checked

- `external/tracktion_engine/modules/juce/modules/juce_audio_formats/codecs/juce_WavAudioFormat.h`
- `external/tracktion_engine/modules/tracktion_engine/model/clips/tracktion_ClipEffects.cpp`
- `external/tracktion_engine/modules/tracktion_engine/model/clips/tracktion_WarpTimeManager.cpp`
- <https://github.com/jiixyj/libebur128>
- <https://conan.io/center/recipes>
- <https://github.com/conan-io/conan-center-index>
