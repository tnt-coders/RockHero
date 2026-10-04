/*!
\file meter_edits.h
\brief The pure re-addressing transform behind every time-signature edit.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief A time signature placed by where it starts in musical time rather than by measure number.

A meter edit keeps every meter's time (65-Q3), so an edit is stated on the whole-note axis, the one
axis a meter change does not bend; the measure numbers are derived from these places
(\ref readdressUnderMeters), never stated beside them.
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
\brief Reads a tempo map's time signatures at the places they start.
\param tempo_map Map to read.
\return One placed meter per time-signature change, in order.
*/
[[nodiscard]] std::vector<PlacedMeter> placedMetersOf(const common::core::TempoMap& tempo_map);

/*!
\brief Re-addresses one position from one meter list to another, keeping its time.

The one re-address every meter-edit slice and the cursor a meter verb carries go through: the
position's whole-note place under \p from, read back as a token under \p to. Exact rationals both
ways, so nothing rounds.

\param from Map the position is addressed under.
\param to Map to address it under; only its meters are read, never its anchors' times.
\param position Valid grid position under \p from.
\return The same whole-note place as a grid position under \p to.
*/
[[nodiscard]] common::core::GridPosition readdressedPosition(
    const common::core::TempoMap& from, const common::core::TempoMap& to,
    common::core::GridPosition position);

/*! \brief Stable reasons a meter edit refuses. */
enum class MeterEditErrorCode : std::uint8_t
{
    /*!
    \brief A meter's place is not a downbeat under the meters before it: the reign before it is not
    a whole number of measures (65-Q3).
    */
    MeterOffDownbeat,
};

/*! \brief Recoverable failure produced by a meter edit. */
struct [[nodiscard]] MeterEditError
{
    /*! \brief Stable error code used by callers for branching. */
    MeterEditErrorCode code{};

    /*! \brief Human-readable diagnostic for the refusal log. */
    std::string message;
};

/*!
\brief Re-addresses the whole song timeline under a new meter list, every object keeping its time.

The new list's measure numbers are derived by walking the places in order, the first standing at
measure 1; a place that is not a downbeat under the meters before it refuses the edit. Then every
slice keeps its whole-note position exactly:
- tempo anchors (their milliseconds untouched), note onsets, fret-hand positions, tone region
  starts and tone automation points are re-addressed: the same whole-note position, read as a token
  under the new meters;
- a note's ring and its keyframe offsets are BEAT counts, which a denominator change re-means, so
  they are re-expressed: the new beat distance from the re-addressed onset to the re-addressed end;
- a section left off a downbeat snaps back to its measure's downbeat, the one slice that moves in
  time (a section is navigation and moves no sound); two snapping onto one downbeat are left for
  the section rules to refuse, as they refuse any repeated position;
- a terminal left off a downbeat re-seats on the first one after it (\ref reseatTerminalAnchor).

Every arrangement is re-addressed, not only the loaded one. The result is not validated: the caller
judges it against the rules as it would any edit.

\param timeline Timeline to re-address; its tempo map holds the meters being replaced.
\param meters The new meter list, in order, the first at whole note 0.
\return The re-addressed timeline, or the refusal.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, MeterEditError> readdressUnderMeters(
    const common::core::SongTimeline& timeline, std::span<const PlacedMeter> meters);

} // namespace rock_hero::editor::core
