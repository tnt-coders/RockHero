/*!
\file i_song_audio.h
\brief Tracktion-free song audio port.
*/

#pragma once

#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/song.h>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned port for song audio preparation and active arrangement playback.

This interface owns message-thread audio operations needed to prepare loaded project songs and make
one arrangement active for playback. It is not an undoable edit-command surface; future
model-editing commands belong on IEdit.
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
    \return True when every arrangement has usable positive-duration audio.
    */
    [[nodiscard]] virtual bool prepareSong(common::core::Song& song) = 0;

    /*!
    \brief Makes an already-prepared arrangement active for playback.

    Implementations may load, switch, or replace backend audio resources. The arrangement must
    already have a usable audio asset and positive duration from prepareSong().

    \param arrangement Prepared arrangement to make active.
    \return True when the backend made the arrangement playable.
    */
    [[nodiscard]] virtual bool setActiveArrangement(
        const common::core::Arrangement& arrangement) = 0;

    /*! \brief Clears the active arrangement from the playback backend. */
    virtual void clearActiveArrangement() = 0;

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
