/*!
\file timeline_readdress.h
\brief The whole-song re-addressing transform every time-holding grid edit runs.
*/

#pragma once

#include <cstdint>
#include <functional>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief A tempo anchor placed by where it stands in musical time rather than by grid token.

The anchor's address under the new meters is derived from this place (\ref readdressTimeline).
*/
struct PlacedAnchor
{
    /*! \brief Whole notes from the song's start to the anchor; rounded onto the tick lattice. */
    common::core::Fraction whole_note{};

    /*! \brief The anchor's time, in whole milliseconds. */
    std::int64_t milliseconds{0};

    /*!
    \brief Compares two placed anchors by their stored fields.
    \param lhs Left-hand anchor.
    \param rhs Right-hand anchor.
    \return True when both store equal values.
    */
    friend bool operator==(const PlacedAnchor& lhs, const PlacedAnchor& rhs) = default;
};

/*!
\brief Where the content of a whole-song edit goes: an old whole-note place to its new one.

The identity for a meter edit, a scaling for Scale Tempo. A result off the tick lattice is rounded
onto it (\ref common::core::nearestTickPlace).
*/
using ContentMap = std::function<common::core::Fraction(common::core::Fraction)>;

/*!
\brief The content map that keeps every place: a meter edit's, and a tempo edit's for the cursor.

Every stored place is already on the tick lattice, so nothing it sends rounds; a debug assert holds
it to that.

\param whole_note Old whole-note place.
\return The same place.
*/
[[nodiscard]] common::core::Fraction keptPlace(common::core::Fraction whole_note);

/*!
\brief Reads a tempo map's anchors at the places they stand.
\param tempo_map Map to read.
\return One placed anchor per anchor, in order, the terminal last.
*/
[[nodiscard]] std::vector<PlacedAnchor> placedAnchorsOf(const common::core::TempoMap& tempo_map);

/*!
\brief Re-addresses the whole song timeline under a new map stated on the whole-note axis, every
object going where the content map sends it and nothing moving in time.

THE one pass every whole-song edit runs: a meter edit, Scale Tempo and the Note Lock differ only
in the map they state and where they send the content. It refuses nothing: a reign that is not a
whole number of its measures ends in a derived short bar (\ref common::core::TempoMap::reigns),
and a terminal left mid-measure stands, the song ending on a short final bar.
- The new meters are the map's own placed changes.
- The new anchors' addresses are derived from their places, keeping their milliseconds.
- Sections, note onsets, fret-hand positions, tone region starts and tone automation points each
  go where the content map sends their place, rounded onto the tick lattice only where it lands
  off it, and read as a token under the new meters.
- A note's ring and its keyframe offsets are note values from its onset, so each becomes the
  distance between where its two instants are sent: a meter edit leaves them untouched, Scale Tempo
  scales them, and the Note Lock re-rounds them with the places they join.

Every arrangement is re-addressed, not only the loaded one. The result is not validated: the caller
judges it against the rules as it would any edit, and a reign holding no whole measure is the
tempo-map rules' refusal.

\param timeline Timeline to re-address.
\param meters The new meter list, in order, the first at whole note 0.
\param anchors The new anchor list, in order, the first at whole note 0 and the terminal last.
\param content Where each old whole-note place goes.
\return The re-addressed timeline.
*/
[[nodiscard]] common::core::SongTimeline readdressTimeline(
    const common::core::SongTimeline& timeline,
    std::span<const common::core::TimeSignatureChange> meters,
    std::span<const PlacedAnchor> anchors, const ContentMap& content);

} // namespace rock_hero::editor::core
