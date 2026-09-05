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
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/project/song_import_error.h>
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

/*!
\brief What the let-ring figure law did to the marked rings, for the census as numbers.

Published as NUMBERS rather than only as the prose conversion notice beside it, because the census
measures this population and a sentence is not a measurement. The two fields answer the two
questions a reader of an imported chart has about the law — how often a mark lengthened its ring,
and how often a mark changed nothing at all. The second is the mis-seam detector: with the law the
only thing that ever lengthens a marked ring, a mark left at exactly its written duration is
either a texture the physics already bounds or a figure seam landing too early, and the census
telling those apart is what gates the law's corpus behavior.
*/
struct GpLetRingReport
{
    /*! \brief Marked rings the figure end lengthened past their written duration. */
    int extended{0};

    /*! \brief Marked rings left at exactly their written duration after every bound had its say. */
    int at_written{0};
};

/*! \brief Everything the importer needs from one score, plus conversion notes. */
struct GpBuiltSong
{
    /*! \brief Song metadata from the score header. */
    common::core::SongMetadata metadata;

    /*! \brief Tempo map built from the score's audio sync points. */
    common::core::TempoMap tempo_map;

    /*! \brief Song-structure sections from the score's master-bar markers, in measure order. */
    std::vector<common::core::SongSection> sections;

    /*! \brief One built arrangement per score track, in track order. */
    std::vector<GpBuiltArrangement> arrangements;

    /*! \brief What the let-ring figure law did to the marked rings, for the census as numbers. */
    GpLetRingReport let_ring;

    /*! \brief Human-readable notes about content the chart format does not carry. */
    std::vector<std::string> notes;
};

/*!
\brief Builds song metadata, the tempo map, sections, and per-track charts from a parsed score.

The tempo map comes from the score's audio sync points (bar positions pinned to audio seconds),
so imported notes line up with the backing audio exactly as they did in Guitar Pro; the base
tempo is only used for scores without sync points. Tie chains merge into single notes with
combined sustains, matching the format's one-onset-one-note model, and techniques map onto the
chart's fields with unrepresentable ornaments recorded in the notes list.

\param score Parsed score to convert.
\return Built song data, or a typed import failure describing the unconvertible score.
*/
[[nodiscard]] std::expected<GpBuiltSong, SongImportError> buildGpSong(const GpScore& score);

} // namespace rock_hero::editor::core
