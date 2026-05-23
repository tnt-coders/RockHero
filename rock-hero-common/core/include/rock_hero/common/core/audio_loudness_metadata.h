/*!
\file audio_loudness_metadata.h
\brief Framework-free loudness metadata persisted on backing audio assets.
*/

#pragma once

#include <cstdint>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Identifies a specific revision of an audio file.

Persisted with loudness metadata so the editor can detect whether a stored measurement still
describes the current file when a project is reopened. Size plus SHA-256 is robust to
cross-machine project moves, archive round-trips that reset modification times, FAT/exFAT mtime
resolution loss, and rare manual edits. The hash is intended to be computed only on background
threads alongside loudness analysis.
*/
struct AudioFileFingerprint
{
    /*! \brief File size in bytes. */
    std::uint64_t size_bytes{0};

    /*! \brief SHA-256 of the file contents as a 64-character lowercase hexadecimal string. */
    std::string sha256;

    /*!
    \brief Compares two fingerprints by their stored fields.
    \param lhs Left-hand fingerprint.
    \param rhs Right-hand fingerprint.
    \return True when both fingerprints store equal fields.
    */
    friend bool operator==(const AudioFileFingerprint& lhs, const AudioFileFingerprint& rhs) =
        default;
};

/*!
\brief Integrated loudness and sample peak measurement for one audio file.

Produced by the common/audio loudness analyzer and embedded inside AudioLoudnessAnalysis.
Integrated loudness is reported in LUFS; sample peak is reported in dBFS (the maximum
absolute sample value across all channels, with no oversampling).
*/
struct AudioLoudnessMeasurement
{
    /*! \brief Integrated loudness in LUFS over the full file duration. */
    double integrated_loudness_lufs{0.0};

    /*! \brief Maximum absolute sample value in dBFS across all channels. */
    double sample_peak_dbfs{0.0};

    /*!
    \brief Compares two measurements by their stored fields.
    \param lhs Left-hand measurement.
    \param rhs Right-hand measurement.
    \return True when both measurements store equal fields.
    */
    friend bool operator==(
        const AudioLoudnessMeasurement& lhs, const AudioLoudnessMeasurement& rhs) = default;
};

/*!
\brief Everything a single read of an audio file produces.

Bundles measurement, fingerprint, and analyzer identity so the editor can decide whether a stored
record still applies to the current file using only field-level comparison. Used in two places:
returned by the common/audio measureAudioLoudness function, and embedded inside
AudioLoudnessMetadata as the persisted "what the file is" record.
*/
struct AudioLoudnessAnalysis
{
    /*! \brief Loudness measurement produced by the analyzer. */
    AudioLoudnessMeasurement measurement;

    /*! \brief Fingerprint identifying the exact file revision that produced the measurement. */
    AudioFileFingerprint fingerprint;

    /*!
    \brief Stable identifier for the analyzer used to produce the measurement.

    Changing analyzer implementations or libraries should change this identifier so editor-side
    staleness checks treat measurements from a different analyzer as not comparable.
    */
    std::string analyzer_id;

    /*! \brief Analyzer version string paired with analyzer_id for finer-grained staleness checks. */
    std::string analyzer_version;

    /*!
    \brief Compares two analyses by their stored fields.
    \param lhs Left-hand analysis.
    \param rhs Right-hand analysis.
    \return True when both analyses store equal fields.
    */
    friend bool operator==(const AudioLoudnessAnalysis& lhs, const AudioLoudnessAnalysis& rhs) =
        default;
};

/*!
\brief Target loudness used when normalizing a backing audio file.

Persisted on AudioLoudnessMetadata so a reopened project can tell whether existing audio was
normalized to the currently-configured target. The peak clamp is implicit: applied gain is
clamped so the loudest sample does not exceed 0 dBFS.
*/
struct AudioNormalizationTarget
{
    /*! \brief Requested integrated loudness in LUFS. */
    double integrated_loudness_lufs{-16.0};

    /*!
    \brief Compares two targets by their stored fields.
    \param lhs Left-hand target.
    \param rhs Right-hand target.
    \return True when both targets store equal fields.
    */
    friend bool operator==(
        const AudioNormalizationTarget& lhs, const AudioNormalizationTarget& rhs) = default;
};

/*!
\brief Durable loudness record persisted on an AudioAsset.

Stores what the file is (analysis), what target it was normalized against (target), and the
gain that should be applied during playback and waveform drawing (applied_gain_db). The gain
is the single source of truth for playback volume scaling and waveform vertical zoom.
Persisted by the song package; consumed by editor open-time policy and the audio backend.
*/
struct AudioLoudnessMetadata
{
    /*! \brief Normalization target the file was analyzed against. */
    AudioNormalizationTarget target;

    /*! \brief Analysis of the source file at the time normalization was computed. */
    AudioLoudnessAnalysis analysis;

    /*!
    \brief Gain in decibels to apply during playback and waveform drawing.

    Computed as the minimum of the LUFS-I delta (target minus measured) and the sample-peak
    headroom (0 dBFS minus measured peak), so the loudest sample after gain never exceeds
    0 dBFS.
    */
    double applied_gain_db{0.0};

    /*!
    \brief Compares two metadata records by their stored fields.
    \param lhs Left-hand metadata record.
    \param rhs Right-hand metadata record.
    \return True when both records store equal fields.
    */
    friend bool operator==(const AudioLoudnessMetadata& lhs, const AudioLoudnessMetadata& rhs) =
        default;
};

} // namespace rock_hero::common::core
