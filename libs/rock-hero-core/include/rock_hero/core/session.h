/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <optional>
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
    \brief Replaces the audio asset assigned to an existing track.
    \param id Track id whose asset should be replaced.
    \param audio_asset New asset to store on the track.
    \return True when the track existed and was updated; false when no track matched the id.
    */
    bool replaceTrackAsset(TrackId id, AudioAsset audio_asset);

private:
    // Tracks stay in insertion order so UI projections can preserve row ordering.
    std::vector<Track> m_tracks;

    // Zero is reserved for invalid ids, so generated ids start at one.
    TrackId m_next_track_id{1};
};

} // namespace rock_hero::core
