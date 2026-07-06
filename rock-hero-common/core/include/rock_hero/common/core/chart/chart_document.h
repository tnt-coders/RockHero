/*!
\file chart_document.h
\brief Reads and writes the arrangement chart sidecar document.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
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
\param chart Chart to render.
\return UTF-8 chart document text.
*/
[[nodiscard]] std::string chartDocumentText(const Chart& chart);

/*!
\brief Writes a chart document file, creating parent directories as needed.
\param file Native path of the chart document.
\param chart Chart to write.
\return Empty success, or a typed failure.
*/
[[nodiscard]] std::expected<void, ChartError> writeChartDocument(
    const std::filesystem::path& file, const Chart& chart);

} // namespace rock_hero::common::core
