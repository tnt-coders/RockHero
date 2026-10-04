/*!
\file meter_edits.h
\brief The time-signature edits' driver of the whole-song re-address, and their renumber query.
*/

#pragma once

#include "timeline/timeline_readdress.h"

#include <expected>
#include <optional>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>

namespace rock_hero::editor::core
{

/*!
\brief The first measure a meter edit renumbered: the first whose downbeat stands at another
whole-note place after the edit than before it.

A meter edit keeps every object on the whole-note axis, so a measure whose downbeat moved there now
numbers other music, and so does every measure after it. A restate that keeps every measure's
length (4/4 to 8/8) moves no downbeat and renumbers nothing. Measures are compared up to the later
of the two terminals.

\param before Map before the edit.
\param after Map after it.
\return The first renumbered measure, or empty when no downbeat moved.
*/
[[nodiscard]] std::optional<int> firstRenumberedMeasure(
    const common::core::TempoMap& before, const common::core::TempoMap& after);

/*!
\brief Re-addresses the whole song under a new meter list, every object keeping its time.

The meter edits' driver of \ref readdressTimeline: the anchors stay where they stand and the
content map is \ref keptPlace, so every place is kept exactly and nothing rounds.

\param timeline Timeline to re-address; its tempo map holds the meters being replaced.
\param meters The new meter list, in order, the first at whole note 0.
\return The re-addressed timeline, or the refusal.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, ReaddressError> readdressUnderMeters(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters);

} // namespace rock_hero::editor::core
