/*!
\file rock_song_package_format.h
\brief Shared song-package format rules used by the reader and writer translation units.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string_view>

namespace rock_hero::common::core
{

/*! \brief Name of the required root song document inside a native song package. */
inline constexpr std::string_view g_song_document_name{"song.json"};

/*!
\brief Fixed decimal precision for persisted anchor seconds.

Anchor seconds are the only absolute time stored in a package, persisted at a fixed three-decimal
(millisecond) grid. This matches the Song Data Model note in docs/design/architecture.md.
*/
inline constexpr int g_timing_decimals = 3;

/*!
\brief Reports whether a package-relative reference stays inside its workspace.
\param path Package-relative path taken from a song document.
\return True when the path is relative and never escapes upward.
*/
[[nodiscard]] bool isSafeRelativePath(const std::filesystem::path& path);

/*!
\brief Validates the structural tempo-map rules shared by package read and write.
\param tempo_map Parsed or about-to-be-persisted tempo map.
\return Empty success, or the format violation to report.
*/
[[nodiscard]] std::expected<void, SongPackageError> validateTempoMap(const TempoMap& tempo_map);

} // namespace rock_hero::common::core
