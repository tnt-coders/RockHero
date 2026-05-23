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

The best external candidate is `libebur128`:

- upstream: <https://github.com/jiixyj/libebur128>
- license: MIT
- stated purpose: EBU R128 loudness normalization support
- useful capabilities: integrated loudness, loudness range, true peak scanning

Conan status needs one last implementation-time verification. A web check of ConanCenter did not
surface a `libebur128` recipe page, and ConanCenter's recipe search page returned no visible recipe
results for that name. Before implementation, run the current build-environment equivalent of:

```powershell
conan list "libebur128/*" -r=conancenter
```

If ConanCenter has a usable recipe, prefer that. If not, the next best options are:

1. Add a project-owned Conan recipe for `libebur128`.
2. Vendor `libebur128` only if the Conan route is impractical.
3. Implement the BS.1770 / EBU R128 measurement locally only if dependency management becomes the
   larger maintenance risk.

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
  - `sha256`
- Prefer a content hash over modification time because `.rhp` and `.rock` extraction can rewrite
  filesystem timestamps. The hash can be produced while validating the rendered WAV; do not
  recompute it synchronously during project open.

`AudioLoudnessMeasurement`

- Value type containing integrated loudness and peak measurement fields.
- Expected fields:
  - `integrated_loudness_lufs`
  - `true_peak_dbtp`

If the chosen analyzer cannot provide true peak in the first pass, use sample-peak field names and
do not expose the value as `dBTP`.

`AudioLoudnessMetadata`

- Value type persisted on `AudioAsset`.
- Expected fields:
  - `target_integrated_loudness_lufs`
  - `target_true_peak_ceiling_dbtp`
  - `measurement`
  - `applied_gain_db`
  - `limited_by_peak_ceiling`
  - `analyzer_id`
  - `analyzer_version`
  - `fingerprint`
- This type is durable package data, so keep it framework-free and independent of `common/audio`.

Modify `AudioAsset` to carry:

```cpp
std::optional<AudioLoudnessMetadata> loudness_metadata;
```

Add these public types in
`rock-hero-common/audio/include/rock_hero/common/audio/audio_normalization.h`.

`AudioNormalizationTarget`

- Value type containing the requested integrated loudness and peak ceiling.
- Initial defaults: `integrated_loudness_lufs = -16.0`, `true_peak_ceiling_dbtp = -2.0`.

`AudioNormalizationRequest`

- Value type containing the input file path, output file path, and target.
- The output path should be a final `.wav` path; the implementation can use a temporary sibling
  path internally before replacing it.

`AudioLoudnessAnalysisRequest`

- Value type containing the input file path and requested analyzer options.
- Used for background checks on existing projects without rendering a new file.

`AudioLoudnessAnalysisResult`

- Value type containing the measured loudness, file fingerprint, analyzer identity, and analyzer
  version.
- This result should be enough for editor/core to decide whether to show a normalization prompt.

`AudioNormalizationResult`

- Value type reporting the input measurement, applied gain, rendered output measurement, and
  metadata to persist on the output `AudioAsset`.
- Expected fields:
  - `output_path`
  - `source_integrated_loudness_lufs`
  - `source_true_peak_dbtp`
  - `applied_gain_db`
  - `output_integrated_loudness_lufs`
  - `output_true_peak_dbtp`
  - `limited_by_peak_ceiling`
  - `output_loudness_metadata`

If the chosen analyzer cannot provide true peak in the first pass, rename the peak fields to
sample-peak names and do not call the `-2 dB` ceiling `dBTP` in code or UI.

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
[[nodiscard]] std::expected<AudioNormalizationResult, AudioNormalizationError> normalizeAudioFile(
    const AudioNormalizationRequest& request);
```

`analyzeAudioFile`

- Public free function:

```cpp
[[nodiscard]] std::expected<AudioLoudnessAnalysisResult, AudioNormalizationError> analyzeAudioFile(
    const AudioLoudnessAnalysisRequest& request);
```

Use the same error domain as normalization unless analysis grows enough distinct failure modes to
justify a separate public error type.

No new concrete service class is planned. Do not add `IAudioNormalizer`,
`AudioNormalizationService`, or a broad wrapper around JUCE/Tracktion just for this feature.

Add this alias in `rock-hero-editor/core/include/rock_hero/editor/core/project.h`:

```cpp
using AudioNormalizeFunction = std::function<
    std::expected<common::audio::AudioNormalizationResult, common::audio::AudioNormalizationError>(
        const common::audio::AudioNormalizationRequest&)>;
```

Then extend `Project::import` with an optional function parameter defaulting to
`common::audio::normalizeAudioFile`. This keeps tests public-API oriented without adding another
interface:

```cpp
[[nodiscard]] std::expected<common::core::Song, ProjectError> import(
    const std::filesystem::path& source_path,
    ISongImporter& importer,
    const AudioNormalizeFunction& normalize_audio = common::audio::normalizeAudioFile);
```

Add this alias in the editor/core boundary that schedules background project checks:

```cpp
using AudioAnalyzeFunction = std::function<
    std::expected<common::audio::AudioLoudnessAnalysisResult,
                  common::audio::AudioNormalizationError>(
        const common::audio::AudioLoudnessAnalysisRequest&)>;
```

Expose this through `EditorController::Services`, along with a normalization function for accepted
open-time prompts, so controller tests can cover the workflow through public controller APIs.

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

- Tests through the public `analyzeAudioFile` and `normalizeAudioFile` APIs.
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
- Attach `AudioLoudnessMetadata` from `AudioNormalizationResult` to the normalized `AudioAsset`.
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
4. For each unique audio path, create a unique sibling canonical WAV path under `song/audio/`.
5. Call `normalize_audio` for each unique source path.
6. Replace every arrangement reference to the raw source path with the normalized WAV asset,
   including the returned loudness metadata.
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
4. If metadata is missing or stale, schedule `analyzeAudioFile` on editor background work.
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

`measureAudioLoudness`

- Streams decoded audio blocks into the chosen loudness analyzer.
- Returns integrated loudness and true peak.

`fingerprintAudioFile`

- Produces `AudioFileFingerprint` while streaming file data.
- Prefer computing the hash during analysis or output validation rather than as a separate
  project-open step.

`calculateNormalizationGainDb`

- Pure helper for the gain formula.
- This can be directly covered by public API tests using generated input files; do not expose it
  unless implementation complexity proves it needs isolated tests.

`renderNormalizedAudioFile`

- Streams decoded input to the WAV writer while applying the calculated gain.
- Must avoid loading full songs into memory.

`validateRenderedAudioFile`

- Reopens and measures the rendered WAV so the result reports the actual output.
- Produces the `AudioLoudnessMetadata` value that should be attached to the output `AudioAsset`.

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

- Analysis returns measured loudness and fingerprint for a generated WAV.
- Missing input returns `InputFileMissing`.
- Empty output path returns `OutputPathRequired`.
- Unsupported input returns `UnsupportedInputFormat` or `CouldNotOpenInput`, depending on the
  failure point.
- A generated sine/noise WAV louder than `-16 LUFS` is rendered quieter.
- A generated sine/noise WAV quieter than `-16 LUFS` is rendered louder.
- Peak-ceiling behavior caps the gain and marks `limited_by_peak_ceiling`.
- Silent input returns `SilentInputCannotBeNormalized`.
- Failure cleanup removes temporary output files.
- Normalization result contains metadata that matches the rendered output file.

Use tolerance-based assertions for LUFS and peak values. Exact values will vary slightly by analyzer
and generated fixture.

`rock-hero-common/core/tests`

- Song packages without audio loudness metadata still load.
- Song packages with valid audio loudness metadata round-trip the metadata.
- Malformed optional loudness metadata fails with `InvalidAudioAsset`.

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

1. Confirm the dependency route for `libebur128`.
2. Confirm whether the first WAV writer should be 24-bit PCM or 32-bit float.
3. Confirm the initial prompt tolerance for "needs normalization".
4. Confirm the fingerprint format, most likely SHA-256 plus file size.
5. Re-check whether any existing tests assert raw imported audio filenames.

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
