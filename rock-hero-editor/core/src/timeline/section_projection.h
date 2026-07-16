/*!
\file section_projection.h
\brief Pure projection from song-level section markers to seconds-resolved view entries.
*/

#pragma once

#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/section_view_state.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Projects the song's section markers into seconds-resolved section views.

Every musical position resolves through the tempo map exactly once here, mirroring the tab
projection discipline, so the ruler's section lane never queries musical positions per paint.

\param sections Song-structure section markers in ascending position order.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\return Seconds-resolved section views in the same order.
*/
[[nodiscard]] std::vector<SongSectionView> makeSongSectionViews(
    const std::vector<common::core::SongSection>& sections,
    const common::core::TempoMap& tempo_map);

} // namespace rock_hero::editor::core
