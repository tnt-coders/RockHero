/*!
\file edit_coordinator.h
\brief Headless editor edit coordinator for backend-accepted session mutations.
*/

#pragma once

#include <functional>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/track.h>
#include <string>

namespace rock_hero::ui
{

/*!
\brief Coordinates editor edit transactions across Session and the audio backend.

EditCoordinator is the narrow workflow layer for edits that must touch both the framework-free
core::Session and the audio::IEdit backend. It owns the editor session, allocates durable
session identities first, asks the backend to provision identity-free state, and commits the
accepted state only after the backend succeeds.

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
    \brief Creates an editor track in the backend and editable session.

    The coordinator allocates the track id, asks the backend to provision a matching track mapping,
    and then stores the accepted track spec in Session. If the backend rejects the track provision,
    Session remains unchanged and the returned id is invalid.

    \param name User-visible track name.
    \return Stable id allocated for the newly added track, or an invalid id when creation fails.
    */
    core::TrackId createTrack(const std::string& name = {});

    /*!
    \brief Sets track audio through the backend and stores the accepted value in Session.

    The target track must already exist in Session. The coordinator asks the backend to provision
    the audio first; if the backend rejects the request, Session remains unchanged.

    \param track_id Track that should receive the audio.
    \param audio_asset Framework-free asset selected by the user.
    \return True when the backend and Session both accept the edit.
    */
    [[nodiscard]] bool setTrackAudio(core::TrackId track_id, const core::AudioAsset& audio_asset);

private:
    // Session stores accepted framework-free values after backend edits succeed.
    core::Session m_session;

    // Audio edit port performs the framework-backed mutation before Session records it.
    std::reference_wrapper<audio::IEdit> m_edit;
};

} // namespace rock_hero::ui
