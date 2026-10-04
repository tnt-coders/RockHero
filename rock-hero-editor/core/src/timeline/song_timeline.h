/*!
\file song_timeline.h
\brief The song's grid and every position stored on it: what a meter edit can change.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
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
    std::optional<common::core::Chart> chart;

    /*! \brief The arrangement's tone schedule. */
    common::core::ToneTrack tone_track;

    /*! \brief The arrangement's plugin-parameter automation. */
    std::vector<common::core::ToneParameterAutomation> tone_automation;

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
    common::core::TempoMap tempo_map;

    /*! \brief The song's section markers. */
    std::vector<common::core::SongSection> sections;

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
[[nodiscard]] SongTimeline makeSongTimeline(const common::core::Song& song);

/*!
\brief Writes a timeline back over the song it was read from.

The inverse of \ref makeSongTimeline, and the write a whole-song meter commit applies.

\param song Song to write into; holds the timeline's arrangements, in its order.
\param timeline Timeline to write.
\return The song carrying the timeline, everything else unchanged.
*/
[[nodiscard]] common::core::Song songWithTimeline(common::core::Song song, SongTimeline timeline);

} // namespace rock_hero::editor::core
