/*!
\file i_library_directory_lister.h
\brief Port that enumerates package files under a scan root as planner-input facts.
*/

#pragma once

#include <filesystem>
#include <rock_hero/game/core/library/library_scan_plan.h>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Enumerates the package files present under a configured scan root.

The scan engine calls this to gather the current on-disk facts the pure planner diffs against the
cached index. Implementations stat file identity only (path, size, modification time) and never
open an archive, so listing stays cheap; the describer performs the per-package read.
*/
class ILibraryDirectoryLister
{
public:
    /*! \brief Destroys the directory-lister interface. */
    virtual ~ILibraryDirectoryLister() = default;

    /*!
    \brief Lists the package files directly reachable under one scan root.

    An unreadable or absent root yields an empty list rather than a failure, so one bad root never
    aborts a scan that spans several roots.

    \param scan_root Directory to enumerate package files under.
    \return File-identity facts for each package found; empty when the root has none or is unreadable.
    */
    [[nodiscard]] virtual std::vector<PackageFileFacts> listPackages(
        const std::filesystem::path& scan_root) = 0;

protected:
    /*! \brief Creates the directory-lister interface. */
    ILibraryDirectoryLister() = default;

    /*! \brief Copies the directory-lister interface. */
    ILibraryDirectoryLister(const ILibraryDirectoryLister&) = default;

    /*! \brief Moves the directory-lister interface. */
    ILibraryDirectoryLister(ILibraryDirectoryLister&&) = default;

    /*!
    \brief Assigns the directory-lister interface from another instance.
    \return Reference to this directory-lister interface.
    */
    ILibraryDirectoryLister& operator=(const ILibraryDirectoryLister&) = default;

    /*!
    \brief Move-assigns the directory-lister interface from another instance.
    \return Reference to this directory-lister interface.
    */
    ILibraryDirectoryLister& operator=(ILibraryDirectoryLister&&) = default;
};

} // namespace rock_hero::game::core
