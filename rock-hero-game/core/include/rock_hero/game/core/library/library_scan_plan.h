/*!
\file library_scan_plan.h
\brief Pure planner that diffs the cached library index against current package file facts.

The planner performs no IO: callers snapshot file facts (paths, sizes, modification times) with
whatever lister they own and receive a deterministic action list. A moved package plans as
Remove + Add today; hash-based move detection activates once
docs/plans/roadmap/10-format-versioning-and-chart-identity.md supplies package identity hashes.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <rock_hero/game/core/library/library_index.h>
#include <span>
#include <vector>

namespace rock_hero::game::core
{

/*! \brief One package file's identity facts as observed on disk. */
struct PackageFileFacts
{
    /*! \brief Absolute package path. */
    std::filesystem::path path;

    /*! \brief File size in bytes. */
    std::int64_t file_size_bytes{0};

    /*! \brief Modification time in milliseconds since the epoch. */
    std::int64_t modification_time_milliseconds{0};
};

/*! \brief What the scan must do with one package path. */
enum class LibraryScanActionKind : std::uint8_t
{
    /*! \brief New package with no index entry; describe it and add an entry. */
    Add,

    /*! \brief Known package whose size or modification time changed; re-describe it. */
    Rescan,

    /*! \brief Known package that is unchanged; keep the cached entry as-is. */
    Reuse,

    /*! \brief Indexed package that no longer exists on disk; drop its entry. */
    Remove,
};

/*! \brief One planned scan action. */
struct LibraryScanAction
{
    /*! \brief What the scan must do with the package. */
    LibraryScanActionKind kind{LibraryScanActionKind::Add};

    /*! \brief Absolute package path the action applies to. */
    std::filesystem::path package_path;
};

/*!
\brief Diffs the previous index against current file facts into a deterministic scan plan.

Deterministic regardless of input order: the returned actions are sorted by package path, with
duplicate fact paths collapsing to the last occurrence.

\param previous_index Index loaded from the last completed scan (empty for a fresh rebuild).
\param current_files File facts for every package currently present under the scan roots.
\return Scan actions sorted by package path.
*/
[[nodiscard]] std::vector<LibraryScanAction> planLibraryScan(
    const LibraryIndex& previous_index, std::span<const PackageFileFacts> current_files);

} // namespace rock_hero::game::core
