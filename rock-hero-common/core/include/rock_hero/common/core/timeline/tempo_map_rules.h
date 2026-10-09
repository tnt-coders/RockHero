/*!
\file tempo_map_rules.h
\brief Structural validation rules for tempo maps, shared by editing and persistence.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable failure reasons reported by tempo-map structural validation. */
enum class TempoMapErrorCode : std::uint8_t
{
    /*! \brief The map carries no time signature. */
    MissingTimeSignature,

    /*! \brief The first time signature does not start at the song's start. */
    FirstTimeSignatureLate,

    /*!
    \brief A time signature is out of order, has a non-positive numerator or a denominator that is
    not a power of two of at most 256, or stands off the meter lattice (its place is not a whole
    number of the finest beat a meter may state).
    */
    InvalidTimeSignature,

    /*!
    \brief A time signature's reign, up to the next, is shorter than one of its own measures, so it
    governs no whole measure.
    */
    ReignTooShort,

    /*! \brief The map has fewer than the start and terminal anchors. */
    MissingAnchors,

    /*! \brief The first anchor is not at measure 1 beat 1. */
    FirstAnchorLate,

    /*! \brief An anchor stands off a valid grid position or at a negative time. */
    InvalidAnchor,

    /*! \brief Anchors are not strictly ascending in both position and time. */
    UnorderedAnchors,
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
\brief Reports whether a place on the whole-note axis lies on the meter lattice: where a time
signature may start.

THE meter lattice rule: a place a whole number of the finest beat a meter may state (a power of two
the tick lattice holds, 1/256 note) from the song's start, so the measure a change cuts short is
itself a meter. Asked by the tempo-map rules of every stored change and by Scale Tempo of every
change a factor would place.

\param place Whole notes from the song's start.
\return True when a time signature may start at the place.
*/
[[nodiscard]] bool isOnMeterLattice(Fraction place) noexcept;

/*!
\brief Validates the structural tempo-map rules shared by editing and persistence.

Checks the time signatures (at least one, the first at the song's start, places strictly
ascending and on the meter lattice (\ref isOnMeterLattice), positive numerators, power-of-two
denominators the tick lattice can hold, each reign holding at least one whole measure of its meter
before the next change) and the anchors (a start anchor at `1:1`, every anchor on a valid grid
position (\ref isValidGridPosition) at a non-negative time, strictly ascending in position and
time). The terminal is an anchor like any other: a song may end mid-measure, on a short final bar.

\param tempo_map Tempo map to validate.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, TempoMapError> validateTempoMapRules(const TempoMap& tempo_map);

} // namespace rock_hero::common::core
