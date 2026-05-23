# Backing Audio Normalization Check Latency

Status: deferred. Mostly superseded by the LUFS-I gain normalization plan which dropped true
peak in favor of sample peak, eliminating the dominant analysis cost. Revisit only if the
open-time prompt still feels too slow on real songs.

## Why This Exists

When a project is opened (including auto-reopen of the last project), the editor schedules a
background loudness analysis for each unique backing audio asset whose persisted
`AudioLoudnessMetadata` is missing or out of date. The worker runs
`common::audio::measureAudioLoudness`, which:

1. Decodes the full audio file through JUCE's `AudioFormatReader`.
2. Streams every sample through libebur128 with `EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK`. The
   true-peak mode performs 4x oversampling internally.
3. Computes a SHA-256 fingerprint over the file contents in a separate pass.

For a typical 3-minute backing track this takes a few seconds of CPU on a background thread.
Nothing on the message thread blocks, but the user does not see the prompt until the worker
completes.

## Why It Probably Will Not Matter

This cost is paid at most once per backing audio file. Once `Project::import` runs (which writes
audio at the normalization target and persists `AudioLoudnessMetadata`) or the user accepts an
open-time prompt (which does the same), every subsequent open of that project takes the fast
path: the controller compares `metadata.target` and `metadata.analysis.measurement` against the
current target with no file IO. The slow path only fires for:

- Projects imported before the metadata schema landed.
- Files that were normalized externally and dropped in.
- Files whose persisted metadata target no longer matches the configured target.

Once those are migrated the slow path is silent.

## Measure Before Changing

Before optimizing, collect timing data so the right phase gets attention:

- Total wall time from open success to prompt appearance.
- Time spent in `runLoudnessAnalyzer` (file decode + ebur128 K-weighted filter + true peak
  oversampling).
- Time spent in `fingerprintAudioFile` (one pass for SHA-256).
- File size and duration of the asset.

If the analyzer dominates, look at the optimizations below in order. If the fingerprint dominates
(unlikely; SHA-256 is fast on typical hardware), the unified-read option below kills both at
once.

## Optimization Options, Cheapest First

### 1. Skip true-peak mode and use sample peak

`EBUR128_MODE_TRUE_PEAK` enables internal 4x oversampling so the analyzer reports inter-sample
peaks. Sample peak (no oversampling) is much cheaper and is usually accurate enough for the
"should we re-normalize?" decision. For the open-time check we only need to know whether the file
is in the right neighborhood; the actual normalization render can still use true-peak mode for
the gain calculation it commits to disk.

Tradeoff: tolerance comparison against `true_peak_ceiling_dbtp` would compare sample peak instead
of true peak. A signal whose inter-sample peak is above the ceiling but whose sample peak is
below could be marked "current" incorrectly. Acceptable for a prompt-or-not decision; not
acceptable for the render itself.

Effort: trivial — change the mode flags in `runLoudnessAnalyzer` (or split into two helpers, one
for fast checks, one for render-time analysis).

### 2. Unify the analyzer and fingerprint passes

`measureAudioLoudness` currently opens the file twice: once through `AudioFormatReader` for
loudness analysis, once through `FileInputStream` for SHA-256. Both touch the entire file. A
single sequential pass that splits bytes into both the SHA-256 sink and the audio decoder would
roughly halve the IO cost.

Caveat: this only helps when the file is not in the OS page cache. On Windows after a recent
open, the second pass usually reads from cache and is nearly free. Measure first.

Effort: medium — needs a custom `juce::InputStream` wrapper that forwards reads to a SHA-256
accumulator before yielding bytes to the format reader.

### 3. Cache prior analyses keyed on fingerprint

If a file's `AudioFileFingerprint` matches a previously cached entry (across editor launches),
skip re-analysis and reuse the stored measurement. The persisted `AudioLoudnessMetadata` already
covers the in-project case; this would extend the caching across projects that share the same
audio file.

Effort: medium — adds a small on-disk cache keyed on `(size_bytes, sha256)`, scoped to the editor
settings directory.

Probably not worth doing in isolation. The persisted-on-project metadata already handles the
common case.

### 4. Push the prompt before the full analysis completes

For files with no persisted metadata, libebur128 has the integrated loudness available after
roughly the first second of audio in `EBUR128_MODE_M` (momentary). If the momentary loudness is
already wildly outside the target, surface the prompt with a "preliminary measurement" caveat and
let the user decide while the full integrated loudness finishes. The full measurement still has
to complete before the actual normalization render is allowed.

Tradeoff: small UX cost. The user might be prompted on a sample whose final integrated loudness
turns out to be within tolerance — a false positive. The prompt copy would need to acknowledge
the preliminary nature of the measurement.

Effort: high — requires reworking `measureAudioLoudness` to emit progress, and the controller to
handle preliminary-then-final results.

Probably never worth it unless option 1 + option 2 are insufficient.

## Related: Waveform Redraw After Normalization

When the user accepts an open-time normalization prompt the controller swaps the canonical audio
file and reloads the session. `ArrangementViewState::audio_asset` then carries new
`loudness_metadata`, so the arrangement view's equality check against
`m_thumbnail_source_asset` detects a change and calls `IThumbnail::setSource(...)` on the new
file. JUCE's `AudioThumbnail` then rescans the file to build a new waveform summary; that scan
runs on a JUCE thumbnail thread and fills the visual in incrementally over a few seconds.

This is a separate code path from the analyzer above (JUCE thumbnail vs libebur128) but shares
the same one-time-per-file character and the same "probably fine" disposition. Same priority for
optimization: leave alone unless real songs show painful UX.

If it ever needs attention, the cheapest path is to reuse the decoded sample data the analyzer
already produced during the normalization render. That requires plumbing audio frames out of
`renderNormalizedAudioFile` and into the thumbnail boundary, which is much larger surgery than
anything above. Realistic only if both this and the analyzer above are confirmed problems.

## What to Verify Before Implementing Any of These

1. The current latency actually causes user complaints. A few-second delay on a one-time
   migration is acceptable.
2. Persisted metadata is not silently being thrown away on save somewhere (would explain why the
   slow path keeps firing). Check `rock_song_package.cpp` write path.
3. The slow path is not firing on every open because of a target mismatch. If
   `g_default_normalization_target` is changed in code, existing projects' persisted
   `metadata.target` no longer matches and every open triggers a fresh analysis. That is correct
   behavior but worth knowing.

## References

- `docs/in-progress/backing-audio-normalization-plan.md` — original design.
- `rock-hero-common/audio/src/audio_normalization.cpp` — analyzer + fingerprint implementation.
- `rock-hero-editor/core/src/editor_controller.cpp` —
  `scheduleBackingAudioNormalizationCheck` and the background measurement path.
