/*!
\file song_timeline.h
\brief The song's grid and every position stored on it: what a meter edit can change.
*/

#pragma once

#include <expected>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One arrangement's share of the song timeline: the three documents holding its grid
positions, each whole.

Whole rather than sliced down to the positions: the chart travels with its tuning and every
non-positional field, so a restore puts back exactly the documents it captured.
*/
struct ArrangementTimeline
{
    /*! \brief The arrangement's id, which keys this share back onto its arrangement. */
    std::string id;

    /*! \brief The arrangement's chart, absent when it has none. */
    std::optional<Chart> chart;

    /*! \brief The arrangement's tone schedule. */
    ToneTrack tone_track;

    /*! \brief The arrangement's plugin-parameter automation. */
    std::vector<ToneParameterAutomation> tone_automation;

    /*!
    \brief Compares two shares by their stored fields.
    \param lhs Left-hand share.
    \param rhs Right-hand share.
    \return True when both store equal values.
    */
    friend bool operator==(const ArrangementTimeline& lhs, const ArrangementTimeline& rhs) =
        default;
};

/*!
\brief The song's grid and every slice that addresses it: the single definition of what a meter
edit can change.

A meter edit keeps every object's time and changes its address, so it touches the tempo map, the
section list and, in EVERY arrangement (not only the loaded one), the chart, the tone schedule and
the automation. It changes nothing else — metadata, the tone catalog and the audio are absent, so a
whole-song undo restoring this cannot clobber them.
*/
struct SongTimeline
{
    /*! \brief The song's tempo map. */
    TempoMap tempo_map;

    /*! \brief The song's section markers. */
    std::vector<SongSection> sections;

    /*! \brief Every arrangement's share, in song order. */
    std::vector<ArrangementTimeline> arrangements;

    /*!
    \brief Compares two timelines by their stored fields.
    \param lhs Left-hand timeline.
    \param rhs Right-hand timeline.
    \return True when both store equal values.
    */
    friend bool operator==(const SongTimeline& lhs, const SongTimeline& rhs) = default;
};

/*!
\brief Reads a song's timeline.
\param song Song to read.
\return Its tempo map, sections and every arrangement's share.
*/
[[nodiscard]] SongTimeline makeSongTimeline(const Song& song);

/*!
\brief Writes a timeline back over the song it was read from.

The inverse of \ref makeSongTimeline, and the write \ref Session::setSongTimeline applies. The
timeline's arrangements must be the song's, by id and in order; one read from another song is
refused whole rather than half-applied.

\param song Song to write into; everything outside the timeline stays unchanged.
\param timeline Timeline to write.
\return True when written; false, with the song untouched, when the arrangements are not the song's.
*/
[[nodiscard]] bool writeSongTimeline(Song& song, SongTimeline timeline);

/*!
\brief Validates every slice of a timeline against the rules the package reader enforces.

Asks each slice's own validator rather than restating a rule: the tempo-map rules, the section
rules, and for every arrangement its chart rules, its tone-track rules and the structural rules of
each tone automation entry.

\param timeline Timeline to validate.
\return Empty success, or the first violation's diagnostic, an arrangement's prefixed with its id.
*/
[[nodiscard]] std::expected<void, std::string> validateSongTimelineRules(
    const SongTimeline& timeline);

} // namespace rock_hero::common::core
