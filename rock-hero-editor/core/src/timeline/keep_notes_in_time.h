/*!
\file keep_notes_in_time.h
\brief Keeping notes in time: a new map laid under a song, every time held.

The Note Lock is what drives it: while the lock is on, every tempo edit lays its map under the
content the lock holds through here.
*/

#pragma once

#include "timeline/timeline_readdress.h"

#include <expected>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::editor::core
{

/*!
\brief The Note Lock's content map: each place sent to where the new map puts the time the old
map gave it.

The time is read back as the nearest tick under the new map, so each place rounds once, onto the
tick lattice, and lands within half a tick of its old time.

\param old_map Map the places are addressed under.
\param new_map Map the places are re-addressed under.
\return The content map.
*/
[[nodiscard]] ContentMap timeKeptPlaces(
    common::core::TempoMap old_map, common::core::TempoMap new_map);

/*!
\brief Lays a new map under a whole song while every time holds.

The Note Lock's driver of \ref readdressTimeline (plan 66 decision 5): the meters and the anchors
are the new map's, and the content map is \ref timeKeptPlaces, so the song's map matters only for
the time it gives each place. The new map's meters already stand on its downbeats and its terminal
on one, so no meter refuses and the terminal does not re-seat.

\param timeline Timeline to re-address; its tempo map is the map its places are addressed under.
\param tempo_map The map to lay under it.
\return The re-addressed timeline, not yet validated, or the refusal.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, ReaddressError> timeKeptTimeline(
    const common::core::SongTimeline& timeline, const common::core::TempoMap& tempo_map);

} // namespace rock_hero::editor::core
