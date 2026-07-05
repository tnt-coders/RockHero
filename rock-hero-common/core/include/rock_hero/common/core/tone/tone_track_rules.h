/*!
\file tone_track_rules.h
\brief Structural validation rules for tone tracks, shared by editing and persistence.
*/

#pragma once

#include <expected>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>

namespace rock_hero::common::core
{

/*! \brief Stable failure reasons reported by tone-track structural validation. */
enum class ToneTrackErrorCode : std::uint8_t
{
    /*! \brief A region id is not a canonical UUID or repeats an earlier region's id. */
    InvalidRegionId,

    /*! \brief A region endpoint does not address a valid beat on the tempo map. */
    InvalidEndpoint,

    /*! \brief A region's start is not strictly before its end. */
    EmptyOrReversedRegion,

    /*! \brief A region ends past the tempo map's terminal anchor. */
    RegionPastTerminalAnchor,

    /*! \brief Regions are not in ascending start order, or neighbors overlap. */
    UnsortedOrOverlappingRegions,

    /*! \brief A region's tone document reference is not a canonical package path. */
    InvalidToneDocumentRef,
};

/*! \brief Recoverable failure produced by tone-track structural validation. */
struct [[nodiscard]] ToneTrackError
{
    /*! \brief Stable error code used by callers for branching. */
    ToneTrackErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;
};

/*!
\brief Validates the structural tone-track rules shared by editing and persistence.

Checks region IDs (canonical, unique), endpoint validity against the tempo map's grid and
terminal anchor, strict start-before-end ordering, ascending non-overlapping regions, and
canonical tone document references. Referenced document existence is a persistence concern
checked by package code, not here.

\param tone_track Tone track to validate.
\param tempo_map Tempo map the region endpoints must address.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, ToneTrackError> validateToneTrackRules(
    const ToneTrack& tone_track, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
