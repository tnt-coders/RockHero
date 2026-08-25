/*!
\file load_notice.h
\brief The one-shot texts an open or import shows about what the load had to settle.
*/

#pragma once

#include <filesystem>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Builds the notice an open shows when the load normalized something.

A rule change repairs-and-reports instead of bricking a project, and this is the report — kept
to a glance on purpose (user ruling 2026-08-21: the per-position listing was far too noisy for a
dialog). It says how many notes were updated, names each rule with its count (per arrangement,
by part, only when the song has more than one), states what the user most needs to know — the
file is unchanged until they save, because a trimmed tail that was meant as tremolo is recoverable
only while that is true — and points at the log, where every position was already written in
full when the package loaded.

Pure text, so a test can pin the wording without a view.

\param song The song as loaded; supplies the arrangement parts the notice names.
\param conversions Every repair the load applied, from \ref common::core::SongPackageRead.
\param log_file Where the per-position details are; an empty path phrases the pointer without
       one (a test, or a backend started without a file sink).

\return The notice body; empty when there were no conversions.
*/
[[nodiscard]] std::string loadConversionNoticeText(
    const common::core::Song& song,
    const std::vector<common::core::SongPackageConversion>& conversions,
    const std::filesystem::path& log_file);

/*!
\brief Builds the notice an open or import shows when backing audio could not be normalized.

A gain is only ever the distance from a loudness reading to a target, so audio that produces no
reading has no gain — it plays at the level it was recorded at. That is a report, not a refusal
(the same repairs-and-reports rule the chart notice above serves), and this is the report.

Derived from the committed song rather than from a remembered event: by the time an open or import
reaches its notice every backing asset has been through the analyzer, so an asset still carrying no
normalization record is exactly one the analyzer could not measure. Reading the state cannot fall
out of step with the event that produced it.

Pure text, so a test can pin the wording without a view.

\param song The song as opened or imported.
\return The notice body; empty when every backing asset carries a normalization record.
*/
[[nodiscard]] std::string unnormalizedAudioNoticeText(const common::core::Song& song);

} // namespace rock_hero::editor::core
