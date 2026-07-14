/*!
\file library_entry_projection.h
\brief Pure projection from a scanned package's facts and description into a library entry.
*/

#pragma once

#include <expected>
#include <rock_hero/common/core/package/package_description.h>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/library/library_scan_plan.h>
#include <string>

namespace rock_hero::game::core
{

/*!
\brief Projects one scanned package into its library entry.

Pure: the scan engine gathers the file facts, the describer's typed result, and any album-art image
name, then this function assembles the cached entry with no IO — which is what keeps the
entry-shaping rules unit-testable in isolation. A describer failure becomes a warning entry that
still carries its identity facts (so change detection keeps working), never a dropped package.

\param facts File-identity facts for the package.
\param description The describer's result: the peeked description, or the typed read failure.
\param image_file_name Cached album-art image file name, empty when the package has no art.
\return The library entry, warned when the package could not be read.
*/
[[nodiscard]] LibraryEntry makeLibraryEntry(
    const PackageFileFacts& facts,
    const std::expected<common::core::PackageDescription, common::core::SongPackageError>&
        description,
    const std::string& image_file_name);

} // namespace rock_hero::game::core
