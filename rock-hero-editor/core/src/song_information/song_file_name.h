/*!
\file song_file_name.h
\brief The project file name the editor suggests for a song, from the song's information.
*/

#pragma once

#include <rock_hero/common/core/song/song.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Suggests `<Artist> - <Song>.rhp` for a song's project, the name Save As and a first Save
open on.

Artist first, the way a song is spoken of and a folder of songs sorts. Each half is the stored
field trimmed, so stray spaces typed around it never reach the name, and a blank half reads
"Unknown Artist" or "Unknown Song", so the name always states which half is missing instead of
collapsing to a bare " - ". The composed stem is made legal on every OS by
common::core::legalFileName().

\param metadata The song's information.
\return A file name with no directory, ending in `.rhp`.
*/
[[nodiscard]] std::string suggestedProjectFileName(const common::core::SongMetadata& metadata);

} // namespace rock_hero::editor::core
