/*!
\file keep_notes_in_time.h
\brief Keep Notes in Time: a tempo edit's new map laid under the song, every time held.
*/

#pragma once

#include "timeline/timeline_readdress.h"

#include <expected>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::editor::core
{

/*!
\brief Keep Notes in Time's content map: each place sent to where the new map puts the time the old
map gave it.

The time is read back as the nearest tick under the new map, so each place rounds once, onto the
tick lattice, and lands within half a tick of its old time.

\param old_map Map the places are addressed under.
\param new_map Map the places are re-addressed under; it holds the old map's meters.
\return The content map.
*/
[[nodiscard]] ContentMap timeKeptPlaces(
    common::core::TempoMap old_map, common::core::TempoMap new_map);

/*!
\brief Lays a tempo edit's new map under the whole song while every time holds.

Keep Notes in Time's driver of \ref readdressTimeline (plan 66 decision 5): the meters stand where
they stood, the anchors are the new map's, and the content map is \ref timeKeptPlaces. A tempo edit
changes no meter, so no meter refuses and the terminal, already on a downbeat, does not re-seat.

\param timeline Timeline to re-address; its tempo map is the map the edit started from.
\param tempo_map The tempo edit's new map.
\return The re-addressed timeline, not yet validated, or the refusal.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, ReaddressError> timeKeptTimeline(
    const common::core::SongTimeline& timeline, const common::core::TempoMap& tempo_map);

} // namespace rock_hero::editor::core
