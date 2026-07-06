/*!
\file audio_transcode.h
\brief Transcodes decodable audio into RockHero's canonical FLAC package format.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace rock_hero::common::audio
{

/*! \brief Stable reasons an audio transcode can fail. */
enum class AudioTranscodeErrorCode : std::uint8_t
{
    /*! \brief The source could not be opened or decoded by any known audio format. */
    SourceUndecodable,

    /*! \brief The destination FLAC file could not be created or written. */
    DestinationUnwritable,
};

/*! \brief Typed audio transcode failure with a stable code and displayable detail. */
struct [[nodiscard]] AudioTranscodeError
{
    /*! \brief Stable error code for caller branching. */
    AudioTranscodeErrorCode code{};

    /*! \brief Human-readable diagnostic for UI display or logs. */
    std::string message;
};

/*!
\brief Decodes an audio file and re-encodes it losslessly as 24-bit FLAC.

FLAC is RockHero's canonical package audio format: it is lossless, roughly half the size of WAV,
and — unlike lossy sources (MP3, AAC) — decodes to identical samples for both playback and the
waveform thumbnail, so the two never drift apart. Any format the platform can read (WAV, FLAC,
MP3, AAC/m4a, Ogg Vorbis) is accepted; the output preserves the source's sample rate and channel
count. FLAC input needs no round-trip, so callers should copy an already-FLAC source instead of
calling this.

\param source Existing audio file to read.
\param destination FLAC file to write; any existing file at the path is replaced.
\return Nothing on success, or a typed transcode failure.
*/
[[nodiscard]] std::expected<void, AudioTranscodeError> transcodeToFlac(
    const std::filesystem::path& source, const std::filesystem::path& destination);

} // namespace rock_hero::common::audio
