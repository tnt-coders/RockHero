/*!
\file i_editor_controller.h
\brief Framework-free editor controller contract.
*/

#pragma once

#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/track.h>

namespace rock_hero::ui
{

/*!
\brief Project-owned boundary for editor user intents.

Concrete implementations translate these plain-English intents into transport, edit, session, and
view updates without exposing JUCE callback types to tests or to non-UI code.
*/
class IEditorController
{
public:
    /*! \brief Destroys the editor-controller interface. */
    virtual ~IEditorController() = default;

    /*!
    \brief Handles a request to assign an audio asset to one track.
    \param track_id Track whose audio clip should change.
    \param audio_asset Framework-free audio asset selected by the user.
    */
    virtual void onLoadAudioAssetRequested(
        core::TrackId track_id, core::AudioAsset audio_asset) = 0;

    /*! \brief Handles a play/pause button press from the editor UI. */
    virtual void onPlayPausePressed() = 0;

    /*! \brief Handles a stop button press from the editor UI. */
    virtual void onStopPressed() = 0;

    /*!
    \brief Handles a click on a waveform at a normalized horizontal position.
    \param normalized_x Click position normalized to the interval [0, 1].
    */
    virtual void onWaveformClicked(double normalized_x) = 0;

protected:
    /*! \brief Creates the editor-controller interface. */
    IEditorController() = default;

    /*! \brief Copies the editor-controller interface. */
    IEditorController(const IEditorController&) = default;

    /*! \brief Moves the editor-controller interface. */
    IEditorController(IEditorController&&) = default;

    /*!
    \brief Assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(const IEditorController&) = default;

    /*!
    \brief Move-assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(IEditorController&&) = default;
};

} // namespace rock_hero::ui
