/*!
\file chart_document.h
\brief Reads and writes the arrangement chart sidecar document.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Parses a chart document from its JSON text.

Parsing is structural only; run validateChartRules against the song's tempo map afterwards for
the rules that need grid context.

\param text UTF-8 chart document text.
\return Parsed chart, or a typed failure naming the malformed element.
*/
[[nodiscard]] std::expected<Chart, ChartError> parseChartDocument(const std::string& text);

/*!
\brief Reads and parses a chart document file.
\param file Native path of the chart document.
\return Parsed chart, or a typed failure.
*/
[[nodiscard]] std::expected<Chart, ChartError> readChartDocument(const std::filesystem::path& file);

/*!
\brief Renders a chart document as JSON text in the canonical one-entry-per-line layout.

Writes the RESOLVED form of every note, which is what makes the file-level invariant
unconditional: a `Legato` claim the chart does not justify serializes as the plain pick it plays as
(\ref sweepUnjustifiedLegato), so no written document can carry an unjustifiable claim regardless of
which verb, importer, or save path produced the chart. That is why the tempo map is a parameter —
the claim is only decidable on the beat axis its hold test measures — and it is a parameter rather
than each caller's own sweep so no write path can forget.

Latent in-memory overrides are stripped by the same seam the display reads (\ref savedChartNote).

\param chart Chart to render.
\param tempo_map Song tempo map the chart's positions lie on.
\return UTF-8 chart document text.
*/
[[nodiscard]] std::string chartDocumentText(const Chart& chart, const TempoMap& tempo_map);

/*!
\brief Writes a chart document file, creating parent directories as needed.
\param file Native path of the chart document.
\param chart Chart to write.
\param tempo_map Song tempo map the chart's positions lie on; see \ref chartDocumentText.
\return Empty success, or a typed failure.
*/
[[nodiscard]] std::expected<void, ChartError> writeChartDocument(
    const std::filesystem::path& file, const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
