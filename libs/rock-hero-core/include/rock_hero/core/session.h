/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <optional>
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
    \brief Finds a mutable track by id.
    \param id Track id to search for.
    \return Pointer to the matching track, or nullptr when no track has the id.
    */
    [[nodiscard]] Track* findTrack(TrackId id) noexcept;

    /*!
    \brief Finds a read-only track by id.
    \param id Track id to search for.
    \return Pointer to the matching track, or nullptr when no track has the id.
    */
    [[nodiscard]] const Track* findTrack(TrackId id) const noexcept;

    /*!
    \brief Adds a role-free track to the end of the session.
    \param name User-visible track name.
    \param audio_asset Optional audio asset assigned to the track.
    \return Stable id assigned to the newly added track.
    */
    TrackId addTrack(std::string name = {}, std::optional<AudioAsset> audio_asset = std::nullopt);

    /*!
    \brief Commits an audio asset already accepted by the playback backend.

    This is intentionally named as a commit operation because it must not be used to ask whether
    an arbitrary file can be loaded. The audio backend owns that decision and returns the timeline
    range that should be committed with the asset.

    \param id Track id whose asset should be committed.
    \param audio_asset Accepted asset to store on the track.
    \param timeline_range Project timeline range reported by the playback backend.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool commitTrackAudioAsset(TrackId id, AudioAsset audio_asset, TimeRange timeline_range);

private:
    // Tracks stay in insertion order so UI projections can preserve row ordering.
    std::vector<Track> m_tracks;

    // Canonical timeline range for loaded project content.
    TimeRange m_timeline{};

    // Zero is reserved for invalid ids, so generated ids start at one.
    TrackId m_next_track_id{1};
};

} // namespace rock_hero::core
