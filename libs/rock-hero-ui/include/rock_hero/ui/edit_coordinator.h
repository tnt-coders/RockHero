/*!
\file edit_coordinator.h
\brief Headless editor edit coordinator for backend-accepted session mutations.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/song.h>

namespace rock_hero::ui
{

/*!
\brief Coordinates editor edit transactions across Session and the audio backend.

EditCoordinator is the narrow workflow layer for edits that must touch both the framework-free
core::Session and the audio::IEdit backend. It owns the editor session, asks the backend to accept
the selected arrangement audio first, and commits the accepted framework-free song state only after
the backend succeeds.

Production editor code receives only read-only session access through this coordinator. That keeps
controller and app code from manually performing half of a cross-boundary transaction while still
leaving core::Session independent of Tracktion, JUCE, and audio-layer interfaces.
*/
class EditCoordinator final
{
public:
    /*!
    \brief Creates a coordinator over a new editor session and audio edit port.
    \param edit Audio edit port that accepts or rejects backend mutations.
    \note The referenced edit port must outlive this coordinator.
    */
    explicit EditCoordinator(audio::IEdit& edit) noexcept;

    /*! \brief Releases the owned editor session. */
    ~EditCoordinator() = default;

    /*! \brief Copying is disabled because the coordinator owns editor session state. */
    EditCoordinator(const EditCoordinator&) = delete;

    /*! \brief Copy assignment is disabled because the coordinator owns editor session state. */
    EditCoordinator& operator=(const EditCoordinator&) = delete;

    /*! \brief Moving is disabled so controller references remain stable. */
    EditCoordinator(EditCoordinator&&) = delete;

    /*! \brief Move assignment is disabled so controller references remain stable. */
    EditCoordinator& operator=(EditCoordinator&&) = delete;

    /*!
    \brief Returns read-only access to the editor session.
    \return Session state owned by this coordinator.
    */
    [[nodiscard]] const core::Session& session() const noexcept;

    /*!
    \brief Loads a prepared song through the backend and stores accepted values in Session.

    The selected arrangement's audio is loaded into the backend first. Only after the backend
    accepts the audio with a positive duration does the coordinator commit the song into Session.

    \param song Song parsed from a project package.
    \param selected_arrangement_index Arrangement index displayed by the editor.
    \return True when the backend and Session both accept the edit.
    */
    [[nodiscard]] bool loadSong(core::Song song, std::size_t selected_arrangement_index);

private:
    // Session stores accepted framework-free values after backend edits succeed.
    core::Session m_session;

    // Audio edit port performs the framework-backed mutation before Session records it.
    std::reference_wrapper<audio::IEdit> m_edit;
};

} // namespace rock_hero::ui
