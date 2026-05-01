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
methods so callers cannot bypass backend-accepted commit paths.
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

    /*!
    \brief Commits an audio clip already accepted by the playback backend.

    This is intentionally named as a commit operation because it must not be used to ask whether
    an arbitrary file can be loaded. The audio backend owns that decision and accepts or rejects
    the requested clip before it is committed to session state.

    \param id Track id whose clip should be committed.
    \param audio_clip Accepted clip to store on the track.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool commitTrackAudioClip(TrackId id, AudioClip audio_clip);

private:
    // TODO: Replace single-clip replacement with add/remove/move clip commands when tracks can
    // contain multiple clips and clips need stable command-level editing.

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
