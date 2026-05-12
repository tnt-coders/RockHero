/*!
\file i_audio.h
\brief Tracktion-free arrangement audio port.
*/

#pragma once

#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/song.h>

namespace rock_hero::audio
{

/*!
\brief Project-owned facade for song audio preparation and active arrangement playback.

This interface owns message-thread audio operations needed to prepare loaded project songs and make
one arrangement active for playback. It is not an undoable edit-command surface; future
model-editing commands belong on IEdit.
*/
class IAudio
{
public:
    /*! \brief Destroys the audio workflow interface. */
    virtual ~IAudio() = default;

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
    /*! \brief Creates the audio workflow interface. */
    IAudio() = default;

    /*! \brief Copies the audio workflow interface. */
    IAudio(const IAudio&) = default;

    /*! \brief Moves the audio workflow interface. */
    IAudio(IAudio&&) = default;

    /*!
    \brief Assigns the audio workflow interface from another interface.
    \return Reference to this audio workflow interface.
    */
    IAudio& operator=(const IAudio&) = default;

    /*!
    \brief Move-assigns the audio workflow interface from another interface.
    \return Reference to this audio workflow interface.
    */
    IAudio& operator=(IAudio&&) = default;
};

} // namespace rock_hero::audio
