# Backing Audio Minimal Normalization Metadata Plan

## Status

Planning. This narrows the LUFS-I gain-normalization metadata to the smallest useful persisted
shape.

## Goal

Persist only the backing-audio gain needed for normalized playback and a validation hash that proves
the gain still belongs to the current audio file.

## Persisted Shape

Store this on each backing `AudioAsset`:

```json
"normalization": {
  "gainDb": -4.0,
  "validationSha256": "..."
}
```

`validationSha256` is not a raw audio-file hash. It validates the pair of:

- Current audio file bytes.
- Stored `gainDb`.

If either the audio file changes or `gainDb` changes without recomputing the hash, validation
fails and LUFS-I analysis runs again before the project finishes opening or importing.

## Gain Precision

Round `gainDb` to one decimal place (0.1 dB) before storing and hashing. Sub-0.1 dB differences are
below the audible threshold for backing-track gain.

## Hash Input

Compute the validation hash from a stable byte sequence:

```text
RockHeroNormalizationV1\n
gainDb=-4.0\n
audioBytes\n
<raw audio file bytes>
```

Format the gain text with `snprintf(buf, size, "%.1f", gain_db)` or equivalent fixed one-decimal
formatting. Store `gainDb` as a normal JSON number. The one-decimal text is used only inside the hash
computation and is deterministic across all compilers.

## Load And Import Rule

Before a project open/load/import completes:

1. For each unique backing audio asset, inspect `normalization`.
2. If `gainDb` or `validationSha256` is missing, run LUFS-I analysis.
3. If both exist, recompute the validation hash from the current file bytes and stored `gainDb`.
4. If the hash matches, trust `gainDb`.
5. If the hash differs, run LUFS-I analysis.
6. Apply the resulting gain to playback and waveform state before the project becomes usable.

## Analysis Rule

When LUFS-I analysis is required:

```text
desired_gain_db = target_lufs - measured_integrated_loudness_lufs
peak_headroom_db = 0.0 - measured_sample_peak_dbfs
gain_db = min(desired_gain_db, peak_headroom_db)
```

Then compute and persist the new validation hash using that `gain_db`.

## Cleanup

Remove persisted:

- LUFS measurement.
- Sample peak measurement.
- Analyzer id.
- Analyzer version.
- Separate audio fingerprint size.
- Separate audio fingerprint SHA-256.

Keep measurement values as runtime-only outputs of the analysis function.

## Tests

- Package round-trip writes only `gainDb` and `validationSha256`.
- Missing normalization metadata triggers LUFS-I analysis before load/import completes.
- Missing `gainDb` triggers analysis.
- Missing `validationSha256` triggers analysis.
- Changed audio bytes trigger analysis.
- Changed `gainDb` with old hash triggers analysis.
- Matching hash skips LUFS-I analysis and applies stored gain.
