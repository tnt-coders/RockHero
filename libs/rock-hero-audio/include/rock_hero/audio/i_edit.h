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

This interface represents undoable-style audio mutation, not transport control. Implementations may
update their paired transport state while applying an edit, but callers must use ITransport for
playback, position, and listener behavior.

The first concrete implementation is intentionally minimal and still single-track-applied. Only the
most recently set track clip is required to behave correctly. Setting audio on a second track id is
unspecified until stem playback semantics are implemented.
*/
class IEdit
{
public:
    /*! \brief Destroys the audio-edit interface. */
    virtual ~IEdit() = default;

    /*!
    \brief Reads the natural duration of an audio asset.

    This probe lets core/editor code create the requested clip before asking the
    playback backend to apply it.

    \param audio_asset Framework-free audio asset to inspect.
    \return Natural duration of the asset when it can be read; std::nullopt otherwise.
    */
    [[nodiscard]] virtual std::optional<core::TimeDuration> readAudioAssetDuration(
        const core::AudioAsset& audio_asset) const = 0;

    /*!
    \brief Sets one session track to the requested audio clip in the playback backend.

    The requested clip already contains the source range and timeline placement owned by core
    session/editor state. Implementations should either apply that clip or reject it without
    mutating the durable Session model.

    \param track_id Track whose audio clip should be updated.
    \param audio_clip Framework-free clip with requested source range and session placement.
    \return True when the backend accepted the requested clip; false otherwise.
    \note Current playback semantics still apply the most recently set track clip only.
    */
    [[nodiscard]] virtual bool setTrackAudioClip(
        core::TrackId track_id, const core::AudioClip& audio_clip) = 0;

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
