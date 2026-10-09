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
the rules that need grid context. The tempo map is a parameter because the document stores every
position as a whole-note place, which only the song's meters can address (\ref parsePlaceToken).

\param text UTF-8 chart document text.
\param tempo_map Song tempo map the chart's places are addressed on; only its meters are read.
\return Parsed chart, or a typed failure naming the malformed element.
*/
[[nodiscard]] std::expected<Chart, ChartError> parseChartDocument(
    const std::string& text, const TempoMap& tempo_map);

/*!
\brief Parses a chart document's tuning alone.

For a reader that wants the tuning and nothing the grid addresses — the package peek — so it need
not hold the song's tempo map. The version gate and the tuning read are the ones
\ref parseChartDocument runs.

\param text UTF-8 chart document text.
\return The tuning, or a typed failure naming the malformed element.
*/
[[nodiscard]] std::expected<ChartTuning, ChartError> parseChartDocumentTuning(
    const std::string& text);

/*!
\brief Reads and parses a chart document file.
\param file Native path of the chart document.
\param tempo_map Song tempo map the chart's places are addressed on; see \ref parseChartDocument.
\return Parsed chart, or a typed failure.
*/
[[nodiscard]] std::expected<Chart, ChartError> readChartDocument(
    const std::filesystem::path& file, const TempoMap& tempo_map);

/*!
\brief The chart as its document holds it: every note in saved form, every claim settled.

The ONE seam between memory and file. Memory is richer than the file on purpose — a scrape keeps
its latent overrides so the attack toggle can restore them, and a `Legato` claim the chart does not
justify survives mid-burst so a neighbour edit can re-justify it — and this is where both are
resolved: latents are stripped (\ref savedChartNote) and unjustifiable claims leave as the plain
picks they play as (\ref sweepUnjustifiedLegato). Stated once so the renderer, the writer's
refusal gate, and any reader of "what will the file say" cannot disagree. That is why the tempo
map is a parameter — the claim is only decidable on the whole-note axis its hold test measures.

\param chart Chart in memory.
\param tempo_map Song tempo map the chart's positions lie on.
\return The chart exactly as a document would hold it.
*/
[[nodiscard]] Chart documentChart(const Chart& chart, const TempoMap& tempo_map);

/*!
\brief Renders a chart document as JSON text in the canonical one-entry-per-line layout.

Renders \ref documentChart, so no written document can carry an unjustifiable claim or a latent
override regardless of which verb, importer, or save path produced the chart.

\param chart Chart to render.
\param tempo_map Song tempo map the chart's positions lie on.
\return UTF-8 chart document text.
*/
[[nodiscard]] std::string chartDocumentText(const Chart& chart, const TempoMap& tempo_map);

/*!
\brief Writes a chart document file, creating parent directories as needed.

Refuses to write a document the reader would refuse: the document form is validated first
(\ref validateChartRules), and a failure is returned as the typed rule error rather than written.
Memory is valid by construction — load normalizes and the edit verbs refuse — so this gate never
fires in a correct build; when it does it has caught a verb defect, and the message says so.

\param file Native path of the chart document.
\param chart Chart to write.
\param tempo_map Song tempo map the chart's positions lie on; see \ref documentChart.
\return Empty success, or a typed failure.
*/
[[nodiscard]] std::expected<void, ChartError> writeChartDocument(
    const std::filesystem::path& file, const Chart& chart, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
