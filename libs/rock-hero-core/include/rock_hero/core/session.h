/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <rock_hero/core/timeline.h>
#include <rock_hero/core/track.h>
#include <string>
#include <vector>

namespace rock_hero::core
{

/*!
\brief Editable in-memory session state.

Session owns the ordered collection of tracks used by the editor workflow. It is deliberately
framework-free so controllers and tests can exercise session behavior without JUCE or Tracktion.
Track reads are exposed as const views; all track mutations must go through explicit Session
methods so model changes stay centralized and testable.
*/
class Session
{
public:
    /*!
    \brief Returns the tracks in insertion order.
    \return Ordered tracks owned by the session.
    */
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept;

    /*!
    \brief Returns the project timeline range covered by the loaded track content.
    \return Current project timeline range.
    */
    [[nodiscard]] TimeRange timeline() const noexcept;

    /*!
    \brief Finds a track by id.
    \param id Track id to search for.
    \return Pointer to the matching track, or nullptr when no track has the id.
    */
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;

    /*!
    \brief Allocates the next stable track id for an edit transaction.

    Track ids are allocated before the audio backend is asked to create a track so the backend
    mapping and the framework-free Session state can agree on the same durable identity. Allocated
    ids are intentionally not reused if the backend rejects the edit, so gaps are expected.

    \return Newly allocated nonzero track id.
    */
    [[nodiscard]] TrackId allocateTrackId() noexcept;

    /*!
    \brief Adds track data with an already allocated id.

    This supports cross-boundary edit orchestration: callers allocate the id before a backend
    mutation, then commit the resulting framework-free track data using the same id. This method
    never allocates ids or advances the allocator.

    \param id Allocated track id to store.
    \param track_data Track data to attach to the id.
    \return True when the id is valid, unused, and the track was added.
    */
    bool addTrack(TrackId id, TrackData track_data);

    /*!
    \brief Renames an existing track.
    \param id Track id whose name should be updated.
    \param name New user-visible track name.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool renameTrack(TrackId id, std::string name);

    /*!
    \brief Allocates the next stable audio clip id for an edit transaction.

    Clip ids are allocated before the audio backend is asked to create a clip so backend state and
    the framework-free Session state can agree on the same durable identity. Allocated ids are
    intentionally not reused if the backend rejects the edit, so gaps are expected.

    \return Newly allocated nonzero audio clip id.
    */
    [[nodiscard]] AudioClipId allocateAudioClipId() noexcept;

    // TODO: Replace setAudioClip with addAudioClip and removeAudioClip once the project expands
    // to support more than one clip per track.
    /*!
    \brief Sets the current audio clip data for an existing track.

    This is the current single-clip track mutation. Editor orchestration should ask the playback
    backend to accept the candidate clip before storing it in Session, but Session deliberately
    stays framework-free and only attaches the already allocated id to framework-free clip data.

    \param id Track id whose clip should be set.
    \param audio_clip_id Allocated clip id to attach to the clip data.
    \param audio_clip_data Clip data to store on the track.
    \return True when the track existed and was updated; false when the track or clip id is invalid.
    */
    bool setAudioClip(TrackId id, AudioClipId audio_clip_id, AudioClipData audio_clip_data);

private:
    // Tracks stay in insertion order so UI projections can preserve row ordering.
    std::vector<Track> m_tracks;

    // Canonical timeline range for loaded project content.
    TimeRange m_timeline{};

    // Zero is reserved for invalid ids, so generated ids start at one.
    TrackId m_next_track_id{1};

    // Zero is reserved for invalid ids, so generated clip ids start at one.
    AudioClipId m_next_audio_clip_id{1};
};

} // namespace rock_hero::core
