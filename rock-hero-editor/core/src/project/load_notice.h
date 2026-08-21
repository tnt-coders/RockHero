/*!
\file load_notice.h
\brief The one-shot text an open shows when the load had to normalize a chart.
*/

#pragma once

#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Builds the notice an open shows when the load normalized something.

A rule change repairs-and-reports instead of bricking a project, and this is the report: grouped
by arrangement (named by its part, and only when the song has more than one) and then by rule, each
rule followed by the positions it fired on. The list per rule is capped so a dense chart cannot
produce a dialog taller than the screen; the full list is always in the log, and the text says so
when it has cut anything. It closes by stating what the user most needs to know — the file is
unchanged until they save — because a trimmed tail that was meant as tremolo is recoverable only
while that is true.

Pure text, so a test can pin the wording without a view.

\param song The song as loaded; supplies the arrangement parts the notice names.
\param conversions Every repair the load applied, from \ref common::core::SongPackageRead.

\return The notice body; empty when there were no conversions.
*/
[[nodiscard]] std::string loadConversionNoticeText(
    const common::core::Song& song,
    const std::vector<common::core::SongPackageConversion>& conversions);

} // namespace rock_hero::editor::core
