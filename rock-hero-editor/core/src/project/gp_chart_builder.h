/*!
\file gp_chart_builder.h
\brief Builds RockHero song data from a parsed Guitar Pro score.
*/

#pragma once

#include "project/gp_score.h"

#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One built arrangement: the part classification and its chart. */
struct GpBuiltArrangement
{
    /*! \brief Part the track maps to (four strings or a bass name mean bass). */
    common::core::Part part{common::core::Part::Lead};

    /*! \brief Chart built from the track, validated against the built tempo map. */
    common::core::Chart chart;
};

/*! \brief Everything the importer needs from one score, plus conversion notes. */
struct GpBuiltSong
{
    /*! \brief Song metadata from the score header. */
    common::core::SongMetadata metadata;

    /*! \brief Tempo map built from the score's audio sync points. */
    common::core::TempoMap tempo_map;

    /*! \brief One built arrangement per score track, in track order. */
    std::vector<GpBuiltArrangement> arrangements;

    /*! \brief Human-readable notes about content the chart format does not carry. */
    std::vector<std::string> notes;
};

/*!
\brief Builds song metadata, the tempo map, and per-track charts from a parsed score.

The tempo map comes from the score's audio sync points (bar positions pinned to audio seconds),
so imported notes line up with the backing audio exactly as they did in Guitar Pro; the base
tempo is only used for scores without sync points. Tie chains merge into single notes with
combined sustains, matching the format's one-onset-one-note model, and techniques map onto the
chart's fields with unrepresentable ornaments recorded in the notes list.

\param score Parsed score to convert.
\return Built song data, or a human-readable failure message.
*/
[[nodiscard]] std::expected<GpBuiltSong, std::string> buildGpSong(const GpScore& score);

} // namespace rock_hero::editor::core
