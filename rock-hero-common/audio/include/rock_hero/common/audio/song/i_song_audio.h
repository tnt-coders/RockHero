/*!
\file i_song_audio.h
\brief Tracktion-free song audio port.
*/

#pragma once

#include <expected>
#include <rock_hero/common/audio/song/song_audio_error.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned port for song audio preparation and active arrangement playback.

This interface owns message-thread audio operations needed to prepare loaded project songs and make
one arrangement active for playback. It is not an undoable edit-command surface; future
model-editing commands should get a dedicated audio edit-command port when that need becomes real.
*/
class ISongAudio
{
public:
    /*! \brief Destroys the song audio port. */
    virtual ~ISongAudio() = default;

    /*!
    \brief Validates arrangement audio and fills each arrangement's natural audio duration.

    The caller owns the candidate song and must discard it when preparation fails because
    implementations may partially update durations before discovering an invalid arrangement.

    \param song Candidate song to prepare for session loading.
    \return Empty success when every arrangement has usable positive-duration audio, or failure.
    */
    [[nodiscard]] virtual std::expected<void, SongAudioError> prepareSong(
        common::core::Song& song) = 0;

    /*!
    \brief Makes an already-prepared arrangement active for playback.

    Implementations may load, switch, or replace backend audio resources. The arrangement must
    already have a usable audio asset and positive duration from prepareSong().

    \param arrangement Prepared arrangement to make active.
    \return Empty success when the backend made the arrangement playable, or failure.
    */
    [[nodiscard]] virtual std::expected<void, SongAudioError> setActiveArrangement(
        const common::core::Arrangement& arrangement) = 0;

    /*!
    \brief Clears the active arrangement from the playback backend.
    \return Empty success, or a typed failure.
    */
    [[nodiscard]] virtual std::expected<void, SongAudioError> clearActiveArrangement() = 0;

    /*!
    \brief Mirrors the song tempo map into the playback backend, one way.

    The backend's tempo state is derived output only — nothing ever reads it back into project
    state — so hosted plugins receive the song's real tempo and time signature instead of the
    backend default. Callers re-mirror after load and after any tempo-map change; failures are
    absorbed as best-effort, matching the other derived caches.

    \param tempo_map Song tempo map to mirror.
    */
    virtual void mirrorTempoMap(const common::core::TempoMap& tempo_map) = 0;

protected:
    /*! \brief Creates the song audio port. */
    ISongAudio() = default;

    /*! \brief Copies the song audio port. */
    ISongAudio(const ISongAudio&) = default;

    /*! \brief Moves the song audio port. */
    ISongAudio(ISongAudio&&) = default;

    /*!
    \brief Assigns the song audio port from another port.
    \return Reference to this song audio port.
    */
    ISongAudio& operator=(const ISongAudio&) = default;

    /*!
    \brief Move-assigns the song audio port from another port.
    \return Reference to this song audio port.
    */
    ISongAudio& operator=(ISongAudio&&) = default;
};

} // namespace rock_hero::common::audio
