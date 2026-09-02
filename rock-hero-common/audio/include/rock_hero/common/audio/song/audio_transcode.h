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
\brief Answers whether this build can decode audio files with the given extension.

Asked of the same JUCE format manager \ref transcodeToFlac decodes through, so the answer and the
decode can never disagree. The registered set is narrower than "anything the platform plays":
WAV, AIFF, FLAC, Ogg Vorbis and MP3 everywhere (the in-tree software MP3 decoder, patents
expired 2017); AAC/.m4a only on Apple platforms, because JUCE ships no AAC reader for Windows or
Linux. Callers refuse an undecodable source LOUDLY before staging it (never a silent no-import);
the plan to decode AAC everywhere is docs/plans/todo/m4a-audio-decode.md.

\param extension File extension to ask about, with or without the leading dot.
\return True when a registered format claims the extension.
*/
[[nodiscard]] bool canDecodeAudioExtension(const std::string& extension);

/*!
\brief Decodes an audio file and re-encodes it losslessly as 24-bit FLAC.

FLAC is RockHero's canonical package audio format: it is lossless, roughly half the size of WAV,
and — unlike lossy sources — decodes to identical samples for both playback and the waveform
thumbnail, so the two never drift apart. Any format \ref canDecodeAudioExtension answers true for
is accepted; the output preserves the source's sample rate and channel count. FLAC input needs no
round-trip, so callers should copy an already-FLAC source instead of calling this.

\param source Existing audio file to read.
\param destination FLAC file to write; any existing file at the path is replaced.
\return Nothing on success, or a typed transcode failure.
*/
[[nodiscard]] std::expected<void, AudioTranscodeError> transcodeToFlac(
    const std::filesystem::path& source, const std::filesystem::path& destination);

} // namespace rock_hero::common::audio
