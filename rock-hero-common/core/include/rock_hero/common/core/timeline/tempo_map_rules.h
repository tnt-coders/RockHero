/*!
\file tempo_map_rules.h
\brief Structural validation rules for tempo maps, shared by editing and persistence.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Fixed decimal precision of anchor seconds.

Anchor seconds are the only absolute time a song stores, kept on a fixed three-decimal
(millisecond) grid. This matches the Song Data Model note in docs/design/architecture.md.
*/
inline constexpr int g_anchor_seconds_decimals = 3;

/*! \brief Steps per second of the anchor-seconds grid: 10 to the \ref g_anchor_seconds_decimals. */
inline constexpr int g_anchor_seconds_grid_units = [] {
    int units = 1;
    for (int decimal = 0; decimal < g_anchor_seconds_decimals; ++decimal)
    {
        units *= 10;
    }
    return units;
}();

/*! \brief The smallest step an anchor's seconds can take: one grid unit (1 ms). */
inline constexpr double g_anchor_seconds_step = 1.0 / g_anchor_seconds_grid_units;

/*! \brief Stable failure reasons reported by tempo-map structural validation. */
enum class TempoMapErrorCode : std::uint8_t
{
    /*! \brief The map carries no time signature. */
    MissingTimeSignature,

    /*! \brief The first time signature does not start at measure 1. */
    FirstTimeSignatureLate,

    /*!
    \brief A time signature is out of order, has a non-positive numerator, or a denominator that is
    not a power of two of at most 256.
    */
    InvalidTimeSignature,

    /*! \brief The map has fewer than the start and terminal anchors. */
    MissingAnchors,

    /*! \brief The first anchor is not at measure 1 beat 1. */
    FirstAnchorLate,

    /*! \brief An anchor has a non-positive address or a negative or non-finite time. */
    InvalidAnchor,

    /*! \brief An anchor's seconds are off the millisecond grid. */
    AnchorOffSecondsGrid,

    /*! \brief An anchor's beat exceeds the length of its measure. */
    AnchorBeatOutsideMeasure,

    /*! \brief Anchors are not strictly ascending in both beat and seconds. */
    UnorderedAnchors,

    /*! \brief The terminal anchor is not on a downbeat. */
    TerminalOffDownbeat,
};

/*! \brief Recoverable failure produced by tempo-map structural validation. */
struct [[nodiscard]] TempoMapError
{
    /*! \brief Stable error code used by callers for branching. */
    TempoMapErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;
};

/*!
\brief Rounds a time to the nearest millisecond-grid value an anchor can store.

Halves round away from zero. A non-finite time stays non-finite, so the rules refuse it as an
invalid anchor rather than a platform's integer conversion deciding what it becomes.

\param seconds Time in seconds.
\return The nearest value on the \ref g_anchor_seconds_decimals grid.
*/
[[nodiscard]] double nearestAnchorSeconds(double seconds) noexcept;

/*!
\brief Validates the structural tempo-map rules shared by editing and persistence.

Checks the time signatures (at least one, the first at measure 1, strictly ordered, positive
numerators, power-of-two denominators the tick lattice can hold) and the anchors (a start anchor at
`1:1`, a terminal anchor on a downbeat, positive addresses inside their measures, finite
non-negative seconds on the millisecond grid, strictly ascending in beat and seconds).

\param tempo_map Tempo map to validate.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, TempoMapError> validateTempoMapRules(const TempoMap& tempo_map);

} // namespace rock_hero::common::core
