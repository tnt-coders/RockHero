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
    \brief Adds a role-free track to the end of the session.
    \param name User-visible track name.
    \return Stable id assigned to the newly added track.
    */
    TrackId addTrack(std::string name = {});

    /*!
    \brief Renames an existing track.
    \param id Track id whose name should be updated.
    \param name New user-visible track name.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool renameTrack(TrackId id, std::string name);

    // TODO: Replace setAudioClip with addAudioClip and removeAudioClip once the project expands
    // to support more than one clip per track.
    /*!
    \brief Sets the current audio clip for an existing track.

    This is the current single-clip track mutation. Editor orchestration should ask the playback
    backend to accept the candidate clip before storing it in Session, but Session deliberately
    stays framework-free and only records the accepted value.

    \param id Track id whose clip should be set.
    \param audio_clip Clip to store on the track.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool setAudioClip(TrackId id, AudioClip audio_clip);

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
