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

    /*! \brief An anchor stands off a valid grid position or at a negative time. */
    InvalidAnchor,

    /*! \brief Anchors are not strictly ascending in both position and time. */
    UnorderedAnchors,

    /*! \brief The terminal anchor is not on a downbeat with no sub-beat offset. */
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
\brief Validates the structural tempo-map rules shared by editing and persistence.

Checks the time signatures (at least one, the first at measure 1, strictly ordered, positive
numerators, power-of-two denominators the tick lattice can hold) and the anchors (a start anchor at
`1:1`, a terminal anchor on a downbeat with no sub-beat offset, every anchor on a valid grid
position (\ref isValidGridPosition) at a non-negative time, strictly ascending in position and
time).

\param tempo_map Tempo map to validate.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, TempoMapError> validateTempoMapRules(const TempoMap& tempo_map);

} // namespace rock_hero::common::core
