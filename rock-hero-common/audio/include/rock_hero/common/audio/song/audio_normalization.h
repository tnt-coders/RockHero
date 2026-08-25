/*!
\file audio_normalization.h
\brief Public loudness analysis and normalization validation boundary for backing audio files.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/core/song/audio_normalization.h>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable failure reasons returned by the loudness analysis boundary. */
enum class AudioNormalizationErrorCode : std::uint8_t
{
    /*! \brief The supplied input file path is empty or does not exist. */
    InputFileMissing,

    /*! \brief No JUCE audio format reader recognized the input file. */
    CouldNotOpenInput,

    /*! \brief The input file format is recognized but unsupported for normalization. */
    UnsupportedInputFormat,

    /*! \brief The input file is malformed or its declared metadata is invalid. */
    InvalidInputAudio,

    /*! \brief The loudness analyzer reported an internal failure. */
    LoudnessMeasurementFailed,

    /*! \brief The validation hash could not be computed for the input file. */
    ValidationHashFailed,
};

/*!
\brief Recoverable failure produced by the loudness analysis boundary.

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
\brief Analyzes a source audio file and computes the gain needed to hit a loudness target.

Measures integrated loudness (LUFS-I) and sample peak, computes the gain clamped so the loudest
sample does not exceed 0 dBFS, rounds the gain to one decimal place, and produces a validation
hash covering both the gain and the audio file content.

Audio with no measurable integrated loudness is not a failure. A gain is only ever the distance
from a reading to the target, so audio that produces no reading — digital silence, or a level so
low that libebur128's absolute gate discards every block — has no gain to compute, and the honest
answer is no normalization at all: the asset keeps its raw level. The result type is exactly
\ref common::core::AudioAsset::normalization, so callers assign it straight through instead of
restating a silence rule of their own.

\param input Absolute path to the source audio file.
\param target Loudness target the gain should be computed against.
\return Normalization metadata including computed gain and validation hash, an empty optional when
        the audio has no measurable loudness, or a failure.
*/
[[nodiscard]] std::expected<
    std::optional<common::core::AudioNormalization>, AudioNormalizationError>
analyzeAudioForGainNormalization(
    const std::filesystem::path& input, const common::core::AudioNormalizationTarget& target);

/*!
\brief Validates that stored normalization data still matches an audio file.

Recomputes the validation hash from the stored gain and the current audio file content. Returns
true when the hash matches, meaning the stored gain can be trusted without re-analysis.

\param input Absolute path to the audio file to validate against.
\param normalization Stored normalization record to check.
\return True when the validation hash matches and the stored gain can be trusted.
*/
[[nodiscard]] bool validateAudioNormalization(
    const std::filesystem::path& input, const common::core::AudioNormalization& normalization);

} // namespace rock_hero::common::audio
