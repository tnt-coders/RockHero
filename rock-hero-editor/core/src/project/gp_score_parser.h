/*!
\file gp_score_parser.h
\brief Parser for the Guitar Pro 7/8 gpif score document.
*/

#pragma once

#include "project/gp_score.h"

#include <expected>
#include <rock_hero/editor/core/project/song_import_error.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Parses a gpif XML document into the importer's score model.

Accepts linear GP7/GP8 scores. Scores using repeats, alternate endings, or jump directions are
rejected with a clear message because the chart format stores linear time only.

\param gpif_xml Full text of Content/score.gpif from a .gp archive.
\return Parsed score, or a typed import failure describing the unusable score.
*/
[[nodiscard]] std::expected<GpScore, SongImportError> parseGpScore(const std::string& gpif_xml);

} // namespace rock_hero::editor::core
