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

    /*! \brief A region start does not address a valid beat on the tempo map. */
    InvalidEndpoint,

    /*! \brief The first region does not start at the song's first downbeat. */
    SongStartUncovered,

    /*! \brief A region starts at or past the tempo map's terminal anchor, so it would be empty. */
    RegionPastTerminalAnchor,

    /*!
    \brief Region starts are not strictly ascending, so a region is empty or reversed against its
    neighbor.
    */
    UnsortedOrOverlappingRegions,

    /*! \brief A region's tone document reference is not a canonical package path. */
    InvalidToneDocumentRef,

    /*! \brief An edit referenced a region id that is not present on the track. */
    RegionNotFound,

    /*! \brief A create position does not fall strictly inside any region on the track. */
    PositionOutsideAnyRegion,

    /*! \brief A delete would remove the only region; the song must always stay covered. */
    CannotRemoveOnlyRegion,

    /*! \brief A boundary move named the first region, whose start is the song's own. */
    CannotMoveSongStart,
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

Checks region IDs (canonical, unique), start validity against the tempo map's grid, coverage
(the first region starts at the song's first downbeat, every start lies before the terminal
anchor), strictly ascending starts, and canonical tone document references. Whether consecutive
regions share a tone is not a validation question: \ref coalesceToneRegions removes such a
boundary wherever a track is built or edited. Referenced document existence is a persistence
concern checked by package code, not here.

\param tone_track Tone track to validate.
\param tempo_map Tempo map the region endpoints must address.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, ToneTrackError> validateToneTrackRules(
    const ToneTrack& tone_track, const TempoMap& tempo_map);

/*!
\brief Whether a tone region may start at a position: on the tempo map's grid and strictly before
the terminal anchor.

The one place rule, shared by \ref validateToneTrackRules and by the editor's projection of where a
tone change would land, so a chord never offers a split the commit then refuses. A region starting
on the closing barline would sound for no time at all.

\param start Candidate region start.
\param tempo_map Tempo map the position must address.
\return True when a region may start there.
*/
[[nodiscard]] bool toneRegionCanStartAt(const GridPosition& start, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
