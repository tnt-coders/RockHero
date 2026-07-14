/*!
\file filesystem_directory_lister.h
\brief Real filesystem implementation of the library directory lister.
*/

#pragma once

#include <filesystem>
#include <rock_hero/game/core/library/i_library_directory_lister.h>
#include <rock_hero/game/core/library/library_scan_plan.h>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Lists `.rock` package files under a scan root from the real filesystem.

Recurses the root, records each `.rock` file's path, size, and modification time, and never opens
an archive (the describer does that). An unreadable or absent root yields an empty list rather than
throwing, honoring the port's one-bad-root-never-aborts contract.
*/
class FilesystemDirectoryLister final : public ILibraryDirectoryLister
{
public:
    /*!
    \brief Enumerates the `.rock` package files reachable under one scan root.
    \param scan_root Directory to enumerate package files under.
    \return File-identity facts for each `.rock` file found; empty when none or on an IO error.
    */
    [[nodiscard]] std::vector<PackageFileFacts> listPackages(
        const std::filesystem::path& scan_root) override;
};

} // namespace rock_hero::game::core
