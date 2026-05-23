/*!
\file audio_normalization.h
\brief Public loudness analysis and normalization boundary for backing audio files.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <rock_hero/common/core/audio_loudness_metadata.h>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons returned by the loudness analysis and normalization boundary. */
enum class AudioNormalizationErrorCode : std::uint8_t
{
    /*! \brief The supplied input file path is empty or does not exist. */
    InputFileMissing,

    /*! \brief The supplied output file path is empty or otherwise unusable. */
    OutputPathRequired,

    /*! \brief No JUCE audio format reader recognized the input file. */
    CouldNotOpenInput,

    /*! \brief The input file format is recognized but unsupported for normalization. */
    UnsupportedInputFormat,

    /*! \brief The input file is malformed or its declared metadata is invalid. */
    InvalidInputAudio,

    /*! \brief The parent directory for the output file could not be created. */
    CouldNotCreateOutputDirectory,

    /*! \brief A temporary or final output file could not be opened for writing. */
    CouldNotCreateOutputFile,

    /*! \brief The loudness analyzer reported an internal failure. */
    LoudnessMeasurementFailed,

    /*! \brief The input is effectively silent and cannot be normalized. */
    SilentInputCannotBeNormalized,

    /*! \brief Streaming the input through the writer failed. */
    OutputRenderFailed,

    /*! \brief The rendered output file could not be reopened or measured. */
    OutputValidationFailed,

    /*! \brief A temporary output file could not be removed after a failure or on success. */
    TemporaryOutputCleanupFailed,
};

/*!
\brief Recoverable failure produced by the loudness analysis and normalization boundary.

Public to the editor workflow so callers can branch on the stable code and surface the
displayable message without parsing free-form strings.
*/
struct [[nodiscard]] AudioNormalizationError
{
    /*! \brief Stable error code used by callers for branching. */
    AudioNormalizationErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for its code.
    \param error_code Stable error code used by callers for branching.
    */
    explicit AudioNormalizationError(AudioNormalizationErrorCode error_code);

    /*!
    \brief Creates an error with contextual diagnostic text.
    \param error_code Stable error code used by callers for branching.
    \param message_text Human-readable diagnostic suitable for UI display or logs.
    */
    AudioNormalizationError(AudioNormalizationErrorCode error_code, std::string message_text);
};

/*!
\brief In-memory result of a successful normalization render.

Contains the durable metadata the caller should attach to the output AudioAsset and the source
measurement before gain was applied for logging and tests.
*/
struct AudioNormalizationOutcome
{
    /*! \brief Persistable record describing the rendered output file. */
    common::core::AudioLoudnessMetadata metadata;

    /*! \brief Loudness measurement of the input file before gain was applied. */
    common::core::AudioLoudnessMeasurement source_measurement;

    /*! \brief Gain in decibels actually applied during the render. */
    double applied_gain_db{0.0};
};

/*!
\brief Measures integrated loudness and true peak of an existing audio file.

Used by the editor on a background thread to check whether a project's backing audio asset still
matches the current normalization target. Returns the same analysis shape that is embedded inside
AudioLoudnessMetadata so the caller can compare field-for-field against any previously persisted
record. The analyzer reads the file twice: once for the loudness scan and once for the SHA-256
fingerprint.

\param input Absolute path to a backing audio file.
\return Analysis of the file, or a recoverable failure.
*/
[[nodiscard]] std::expected<common::core::AudioLoudnessAnalysis, AudioNormalizationError>
measureAudioLoudness(const std::filesystem::path& input);

/*!
\brief Analyzes a source audio file and computes the gain needed to hit a loudness target.

Measures integrated loudness and sample peak, fingerprints the source file, and computes the
applied gain. The gain is clamped so the loudest sample after gain does not exceed 0 dBFS. No
new audio file is rendered; the returned metadata is intended to be stored on the AudioAsset
and applied during playback and waveform drawing.

\param input Absolute path to the source audio file.
\param target Loudness target the gain should be computed against.
\return Loudness metadata including the computed gain, or a recoverable failure.
*/
[[nodiscard]] std::expected<common::core::AudioLoudnessMetadata, AudioNormalizationError>
analyzeAudioForGainNormalization(
    const std::filesystem::path& input, const common::core::AudioNormalizationTarget& target);

/*!
\brief Renders a loudness-normalized copy of an input audio file.

Implements gain-only normalization: the analyzer measures the input, the helper picks the gain
that hits the target LUFS without exceeding 0 dBFS sample peak, and the renderer streams the
input through a WAV writer applying that gain. The renderer preserves the input's sample rate
and channel count and does not resample.

The output is written to a temporary sibling path first and only moved to the final location
after the rendered file has been reopened, measured, and fingerprinted. Failures clean up the
temporary file on a best-effort basis.

\param input Absolute path to the source audio file.
\param output Absolute path where the normalized WAV should be written. Parent directories are
created when missing.
\param target Loudness target the output should be rendered against.
\return Normalization outcome, or a recoverable failure.
*/
[[nodiscard]] std::expected<AudioNormalizationOutcome, AudioNormalizationError> normalizeAudioFile(
    const std::filesystem::path& input, const std::filesystem::path& output,
    const common::core::AudioNormalizationTarget& target);

} // namespace rock_hero::common::audio
