# Backing Audio LUFS-I Gain Normalization Plan

## Status

Complete. This superseded the rendered-WAV normalization direction from
`docs/completed/backing-audio-normalization-plan.md` and records the implemented LUFS-I gain
normalization direction.

## Why This Exists

The current loudness path is correct enough to work, but it is too slow for large-scale use. Even
"check only" analysis currently pays costs that are not needed for Rock Hero's backing-track goal:

- libebur128 currently does more peak work than the backing-track goal requires.
- `measureAudioLoudness` performs a full decoded audio pass, then `fingerprintAudioFile` performs a
  second file pass.
- Normalization renders a new WAV, reopens the rendered output, remeasures it, fingerprints it, and
  only then swaps it into place.
- Project-open metadata checks do not currently use the stored fingerprint to prove that a backing
  audio file is already the file that was analyzed.

REAPER's fast path for this class of operation appears to be much simpler: SWS integrated-only
loudness analysis uses libebur128 in integrated mode only, and REAPER item normalization applies a
gain change to the item/take rather than rendering a new file. Rock Hero can use the same basic
model while keeping Tracktion and JUCE isolated inside `rock-hero-common/audio`.

## Goal

Normalize backing audio by measuring LUFS-I once, persisting the source-file fingerprint and applied
gain, and applying that gain during playback and waveform drawing.

The target remains perceived integrated loudness, currently `-16 LUFS-I`. A backing file whose
measured integrated loudness is `-20 LUFS-I` should persist an applied gain of `+4 dB`; a file at
`-12 LUFS-I` should persist `-4 dB`.

## Non-Goals

- Do not render a new normalized WAV as the normal workflow.
- Do not add a separate limiting pass for backing audio.
- Do not add a mixer, per-song backing gain UI, or automation as part of this work.
- Do not normalize guitar tone, live input, or plugin output.
- Do not block project open on a full LUFS-I analysis when metadata can be trusted.

## Decisions

- Use LUFS-I, meaning integrated loudness over the whole program according to EBU R128 / BS.1770.
- Configure libebur128 with `EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK` for backing-track
  normalization. Sample peak tracks the maximum absolute sample value during the analysis pass with
  no oversampling overhead, so it is essentially free alongside the integrated loudness scan.
- Use sample peak to clamp the applied gain so normalized playback never exceeds 0 dBFS. The
  guarantee is simple and fast: the loudest sample in the source file, after gain, will not clip.
  Inter-sample peaks are not tracked; sample-peak accuracy is sufficient for a backing-track gain
  decision.
- Use sample peak only as a 0 dBFS gain clamp.
- Treat normalization as a gain decision, not an audio-file rewrite.
- Persist the gain decision in project metadata.
- Draw the waveform with the same gain scale used for playback so the visible waveform matches what
  the user hears.
- Use the stored file fingerprint as the reopen-time proof that persisted loudness metadata still
  describes the current backing audio bytes.

## Data Model

Update the project-owned loudness metadata so the durable state describes the source file and the
gain Rock Hero applies to it:

- `AudioLoudnessMeasurement`
  - Keep `integrated_loudness_lufs`.
  - Keep `sample_peak_dbfs` (maximum absolute sample value in dBFS).
- `AudioNormalizationTarget`
  - Keep `integrated_loudness_lufs`.
  - The peak clamp is implicitly 0 dBFS (do not clip).
- `AudioLoudnessAnalysis`
  - Keep `fingerprint` as the fingerprint of `AudioAsset::path`.
  - Keep `measurement`.
  - Keep an analyzer/version identifier, for example `libebur128-lufs-i`.
- `AudioLoudnessMetadata`
  - Keep `analysis`.
  - Keep `target`.
  - Add `applied_gain_db`.

`applied_gain_db` should be computed as:

```text
desired_gain = target.integrated_loudness_lufs - analysis.measurement.integrated_loudness_lufs
peak_headroom = 0.0 - analysis.measurement.sample_peak_dbfs
applied_gain_db = min(desired_gain, peak_headroom)
```

The `min` ensures the loudest sample in the source, after gain, will not exceed 0 dBFS.
If peak headroom limits the gain, the output will be quieter than the target LUFS-I rather
than clipping.

This value is the single source of truth for playback and waveform scaling.

## Serialization And Migration

Package loading should remain backward-compatible:

- Older packages without loudness metadata still load.
- New saves should write the LUFS-I fields and `appliedGainDb`.
- Projects that already contain older loudness metadata must be migrated manually if needed.

If the analyzer identifier, target LUFS-I, source file size, or source file hash does not match,
the metadata is stale and should be refreshed in the background.

## Analysis API Direction

Split the current all-in-one workflow into explicit operations:

- `measureIntegratedLoudness(file)`
  - Decodes the file and feeds samples to libebur128 with
    `EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK`.
  - Returns `AudioLoudnessMeasurement` (integrated loudness and sample peak).
- `fingerprintAudioFile(file)`
  - Computes the persisted size and SHA-256 for the source file.
  - Can remain a separate pass initially. Combining it with decode can be a later optimization if
    profiling shows file IO is still meaningful.
- `analyzeAudioForGainNormalization(file, target)`
  - Measures LUFS-I.
  - Fingerprints the source file.
  - Computes `applied_gain_db`.
  - Returns `AudioLoudnessMetadata`.

Retire `normalizeAudioFile` from the normal backing-track workflow. It may be deleted or retained
only for tests/tools if there is still a concrete use case.

## Import And Open Flow

Project import should copy backing audio into the canonical project location without loudness
rewriting. After copy, analyze the canonical source file, compute `applied_gain_db`, and persist
metadata on the backing `AudioAsset`.

Project open should be responsive:

1. Load the project immediately.
2. For each unique backing `AudioAsset`, inspect `loudness_metadata`.
3. If metadata is missing, has the wrong analyzer, has the wrong target, or has a size mismatch,
   schedule background LUFS-I analysis.
4. If size matches, compute the SHA-256 fingerprint in the background.
5. If the hash matches, trust the stored LUFS-I measurement and `applied_gain_db`; do not perform a
   LUFS-I scan.
6. If the hash differs, run LUFS-I analysis and update metadata.

The editor may use existing metadata optimistically while the background fingerprint check runs.
If the check proves the metadata stale, update playback and waveform state after the refreshed
analysis completes.

## Prompt Behavior

Open-time prompts should compare LUFS-I only:

- If metadata is trusted and target/current LUFS-I differ by no more than the existing tolerance,
  do not prompt.
- If metadata is missing or stale, analyze in the background and prompt only after LUFS-I is known.
- Accepting normalization should update metadata and `applied_gain_db`, refresh playback state,
  repaint waveforms, and mark the project dirty.
- Dismissing the prompt should leave the file and metadata unchanged unless the UI explicitly
  records a "do not ask again" preference later.

## Playback

When `Engine::setActiveArrangement` inserts a Tracktion wave clip for backing audio, it should apply
the stored gain to the clip:

```text
wave_clip->setGainDB(static_cast<float>(audio_asset.loudness_metadata->applied_gain_db))
```

If there is no current metadata, use `0 dB` until analysis or user action supplies a gain value.
Tracktion details should remain contained in `rock-hero-common/audio`; the editor and common core
should only see project-owned metadata values.

## Waveform

The arrangement waveform should use the same gain value as playback. `TracktionThumbnail` already
accepts a `vertical_zoom` parameter, and `ArrangementView` currently passes `1.0f`.

Compute:

```text
vertical_zoom = pow(10.0, applied_gain_db / 20.0)
```

Then pass that value to `drawChannels`. Metadata-only gain changes should repaint the waveform
without rebuilding the thumbnail source when the audio path is unchanged. This keeps the displayed
waveform aligned with normalized playback while avoiding unnecessary thumbnail work.

## Files Likely To Change

- `rock-hero-common/core/include/rock_hero/common/core/audio_loudness_metadata.h`
- `rock-hero-common/core/src/rock_song_package.cpp`
- `rock-hero-common/audio/src/audio_normalization.cpp`
- `rock-hero-common/audio/include/rock_hero/common/audio/audio_normalization.h`
- `rock-hero-common/audio/src/engine.cpp`
- `rock-hero-common/audio/src/tracktion_thumbnail.cpp`
- `rock-hero-common/audio/include/rock_hero/common/audio/tracktion_thumbnail.h`
- `rock-hero-editor/core/src/editor_controller.cpp`
- `rock-hero-editor/ui/src/arrangement_view.cpp`

## Testing Strategy

- Add common-core serialization tests for new metadata, missing metadata, and malformed optional
  metadata.
- Add audio tests proving LUFS-I analysis uses integrated mode only and computes the expected
  `applied_gain_db` for generated fixtures.
- Add controller tests for trusted hash metadata, stale target metadata, size mismatch, and hash
  mismatch.
- Add playback adapter coverage that backing wave clips receive the persisted gain.
- Add UI or component-level coverage that waveform drawing receives the gain-derived
  `vertical_zoom`.
- Add performance instrumentation before and after the change so the cost of LUFS-I analysis,
  fingerprinting, and project-open checks is visible.

## Implementation Order

1. Add `applied_gain_db` to metadata and migrate serialization.
2. Change analysis to integrated-only LUFS-I for backing normalization.
3. Add an analysis helper that returns source fingerprint plus applied gain.
4. Update import and prompt acceptance to store gain metadata without rendering a new WAV.
5. Apply metadata gain to Tracktion backing clips during playback setup.
6. Scale arrangement waveform drawing by the same gain value.
7. Make reopen checks use size and SHA-256 fingerprint before scheduling LUFS-I analysis.
8. Remove or quarantine the rendered-WAV normalization path after replacement behavior is covered.

## Open Questions

- Should the analyzer identifier be a string in persisted JSON, or a small schema/integer version
  attached to loudness metadata?
- Should the editor show "analyzing backing audio" state while a fingerprint or LUFS-I refresh is
  running, or keep that work silent unless a prompt becomes necessary?
