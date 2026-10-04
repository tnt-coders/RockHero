/*!
\file timeline_readdress.h
\brief The whole-song re-addressing transform every time-holding grid edit runs.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief A time signature placed by where it starts in musical time rather than by measure number.

Every whole-song edit holds time and states its new map on the whole-note axis, the one axis a meter
change does not bend; the measure numbers are derived from these places (\ref readdressTimeline),
never stated beside them.
*/
struct PlacedMeter
{
    /*! \brief Whole notes from the song's start to the meter's first downbeat. */
    common::core::Fraction whole_note{};

    /*! \brief The meter that governs from that place on. */
    common::core::TimeSignature meter{};

    /*!
    \brief Compares two placed meters by their stored fields.
    \param lhs Left-hand meter.
    \param rhs Right-hand meter.
    \return True when both store equal values.
    */
    friend bool operator==(const PlacedMeter& lhs, const PlacedMeter& rhs) = default;
};

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
\brief Reads a tempo map's time signatures at the places they start.
\param tempo_map Map to read.
\return One placed meter per time-signature change, in order.
*/
[[nodiscard]] std::vector<PlacedMeter> placedMetersOf(const common::core::TempoMap& tempo_map);

/*!
\brief Reads a tempo map's anchors at the places they stand.
\param tempo_map Map to read.
\return One placed anchor per anchor, in order, the terminal last.
*/
[[nodiscard]] std::vector<PlacedAnchor> placedAnchorsOf(const common::core::TempoMap& tempo_map);

/*! \brief Stable reasons a whole-song re-address refuses. */
enum class ReaddressErrorCode : std::uint8_t
{
    /*!
    \brief A meter's place is not a downbeat under the meters before it: the reign before it is not
    a whole number of measures (65-Q3).
    */
    MeterOffDownbeat,
};

/*! \brief Recoverable failure produced by a whole-song re-address. */
struct [[nodiscard]] ReaddressError
{
    /*! \brief Stable error code used by callers for branching. */
    ReaddressErrorCode code{};

    /*! \brief Human-readable diagnostic for the refusal log. */
    std::string message;
};

/*!
\brief Maps one position from the old map to the new: THE path every slice takes.

The position's whole-note place under \p from, sent by the content map, rounded onto the tick
lattice only where it lies off it, and read as a token under \p to. Only \p to's meters are read,
so a map whose anchors are still the old ones addresses alike. The commit carries the cursor down
the same path, so it lands on what it stood on.

\param from Map the position is addressed under.
\param to Map holding the new meters.
\param content Where each old whole-note place goes.
\param position Valid grid position under \p from.
\return The mapped position under \p to.
*/
[[nodiscard]] common::core::GridPosition readdressedPosition(
    const common::core::TempoMap& from, const common::core::TempoMap& to, const ContentMap& content,
    common::core::GridPosition position);

/*!
\brief Re-addresses the whole song timeline under a new map stated on the whole-note axis, every
object going where the content map sends it.

THE one pass every whole-song edit runs: a meter edit, Scale Tempo and the Note Lock differ
only in the map they state and where they send the content (plan 66 decision 5).
- The new meters' measure numbers are derived by walking their places in order, the first standing
  at measure 1; a place that is not a downbeat under the meters before it refuses the edit, naming
  the meter where it stands now.
- The new anchors' addresses are derived from their places, keeping their milliseconds.
- Note onsets, fret-hand positions, tone region starts and tone automation points are mapped
  (\ref readdressedPosition).
- A note's ring and its keyframe offsets are note values from its onset, so each becomes the
  distance between where its two instants are sent: a meter edit leaves them untouched, Scale Tempo
  scales them, and the Note Lock re-rounds them with the places they join.
- A section left off a downbeat snaps back to its measure's downbeat (a section is navigation and
  moves no sound); two snapping onto one downbeat are left for the section rules to refuse, as they
  refuse any repeated position.
- A terminal left off a downbeat re-seats on the first one after it (\ref reseatTerminalAnchor).

Every arrangement is re-addressed, not only the loaded one. The result is not validated: the caller
judges it against the rules as it would any edit.

\param timeline Timeline to re-address.
\param meters The new meter list, in order, the first at whole note 0.
\param anchors The new anchor list, in order, the first at whole note 0 and the terminal last.
\param content Where each old whole-note place goes.
\return The re-addressed timeline, or the refusal.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, ReaddressError> readdressTimeline(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters,
    std::span<const PlacedAnchor> anchors, const ContentMap& content);

} // namespace rock_hero::editor::core
