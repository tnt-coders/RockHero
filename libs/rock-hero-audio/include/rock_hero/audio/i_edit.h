/*!
\file i_edit.h
\brief Tracktion-free audio edit port.
*/

#pragma once

#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/timeline.h>

namespace rock_hero::audio
{

/*!
\brief Project-owned facade over the concrete Tracktion edit model.

This interface represents audio edit-model mutation, not transport control. Implementations may
update their paired transport state while applying an edit, but callers must use ITransport for
playback, position, and listener behavior.

The current Tracktion-backed implementation is intentionally minimal and updates one backend audio
track that represents the currently displayed arrangement.
*/
class IEdit
{
public:
    /*! \brief Destroys the audio-edit interface. */
    virtual ~IEdit() = default;

    /*!
    \brief Loads full-source audio for the currently displayed arrangement.

    Implementations inspect the asset, apply it to the playback backend as the arrangement's
    complete audio source, and return the accepted asset duration. Loading may mutate backend
    adapter state.

    \param audio_asset Framework-free audio asset used as the arrangement audio source.
    \return Accepted full-source duration when the backend loaded it; std::nullopt otherwise.
    */
    [[nodiscard]] virtual std::optional<core::TimeDuration> loadAudio(
        const core::AudioAsset& audio_asset) = 0;

    /*! \brief Clears the currently loaded arrangement audio from the backend edit. */
    virtual void clearAudio() = 0;

    // TODO: Expand this surface with project-owned arrangement edit commands when the editor gains
    // real tone, automation, and chart authoring behavior.

    // TODO: Add a separate project-owned undo/history boundary when editor requirements justify
    // it. Do not expose juce::UndoManager or Tracktion types through this public contract.

protected:
    /*! \brief Creates the audio-edit interface. */
    IEdit() = default;

    /*! \brief Copies the audio-edit interface. */
    IEdit(const IEdit&) = default;

    /*! \brief Moves the audio-edit interface. */
    IEdit(IEdit&&) = default;

    /*!
    \brief Assigns the audio-edit interface from another interface.
    \return Reference to this audio-edit interface.
    */
    IEdit& operator=(const IEdit&) = default;

    /*!
    \brief Move-assigns the audio-edit interface from another interface.
    \return Reference to this audio-edit interface.
    */
    IEdit& operator=(IEdit&&) = default;
};

} // namespace rock_hero::audio
