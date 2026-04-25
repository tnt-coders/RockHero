/*!
\file i_edit.h
\brief Tracktion-free audio edit port.
*/

#pragma once

#include <rock_hero/audio/transport_state.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/track.h>

namespace rock_hero::audio
{

/*!
\brief Result of applying one audio edit command.

Edit outcomes are reported through this return value rather than through transport-listener
callbacks. The returned transport snapshot always reflects the backend state immediately after the
edit attempt finishes, whether the edit succeeded or failed.
*/
struct EditResult
{
    /*! \brief True when the backend accepted and applied the requested edit. */
    bool applied{false};

    /*! \brief Transport snapshot immediately after the edit attempt completes. */
    TransportState transport_state{};

    /*!
    \brief Compares two edit results by their stored fields.
    \param lhs Left-hand edit result.
    \param rhs Right-hand edit result.
    \return True when both edit results store equal values.
    */
    friend bool operator==(const EditResult& lhs, const EditResult& rhs) = default;
};

/*!
\brief Project-owned facade over the concrete Tracktion edit model.

This interface represents undoable-style audio mutation, not transport control. Implementations may
update their paired transport state while applying an edit, but callers must use ITransport for
playback, position, duration, and listener behavior.

The first concrete implementation is intentionally minimal and still single-track-applied. Only the
most recently set track source is required to behave correctly. Setting audio on a second track id
is unspecified until stem playback semantics are implemented.
*/
class IEdit
{
public:
    /*! \brief Destroys the audio-edit interface. */
    virtual ~IEdit() = default;

    /*!
    \brief Sets the audio source used for one session track in the playback backend.

    Successful mutation must update the paired transport state synchronously, including any
    duration change, and return the resulting transport snapshot directly through EditResult.
    Transport listeners remain reserved for transport-driven state changes such as play, pause,
    stop, and explicit seek commands.

    \param track_id Track whose audio source should be updated.
    \param audio_asset Framework-free audio asset selected for the track.
    \return Result of the edit attempt, including whether it applied and the resulting transport
    snapshot.
    \note Current duration semantics describe the most recently set track source only.
    */
    virtual EditResult setTrackAudioSource(
        core::TrackId track_id, const core::AudioAsset& audio_asset) = 0;

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
