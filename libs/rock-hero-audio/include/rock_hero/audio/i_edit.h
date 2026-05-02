/*!
\file i_edit.h
\brief Tracktion-free audio edit port.
*/

#pragma once

#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/timeline.h>
#include <rock_hero/core/track.h>

namespace rock_hero::audio
{

/*!
\brief Project-owned facade over the concrete Tracktion edit model.

This interface represents audio edit-model mutation, not transport control. Implementations may
update their paired transport state while applying an edit, but callers must use ITransport for
playback, position, and listener behavior.

The first concrete implementation is intentionally minimal and still single-track-applied. Only the
most recently loaded track clip is required to behave correctly. Loading audio on a second track id
may be rejected until stem playback semantics are implemented.
*/
class IEdit
{
public:
    /*! \brief Destroys the audio-edit interface. */
    virtual ~IEdit() = default;

    /*!
    \brief Loads an audio asset and returns the backend-accepted clip.

    Implementations inspect the asset, build the default full-source clip at the requested timeline
    position, apply it to the playback backend, and return the accepted framework-free clip value.
    The returned clip keeps an invalid AudioClipId because Session assigns durable clip identity
    when it stores the accepted clip.

    \param track_id Track whose audio clip should be updated.
    \param audio_asset Framework-free audio asset to load.
    \param position Requested start position on the session timeline.
    \return Accepted clip when the backend loaded the asset; std::nullopt otherwise.
    \note Current playback semantics still apply the most recently loaded track clip only.
    */
    [[nodiscard]] virtual std::optional<core::AudioClip> loadAudioAsset(
        core::TrackId track_id, const core::AudioAsset& audio_asset,
        core::TimePosition position) = 0;

    // TODO: Expand this surface with project-owned clip and track edit commands when the editor
    // gains real timeline authoring behavior instead of the current single-file workflow.

    // TODO: Add a separate project-owned undo/history boundary when editor requirements justify it.
    // Do not expose juce::UndoManager or Tracktion types through this public contract.

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
