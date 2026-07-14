/*!
\file library_index_store.h
\brief Versioned JSON persistence for the cached song-library index.

The index is a discardable cache, so loading is deliberately unforgiving: a missing file, parse
failure, version mismatch, or structural damage never surfaces as an error — it schedules a
rebuild by rescanning the packages. A corrupt cache must never block startup. Saving is atomic
(staged to a temporary file, then swapped) so an interrupted write leaves the previous index
intact.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/library/library_index_store_error.h>
#include <string>

namespace rock_hero::game::core
{

/*!
\brief Index document version this store writes and accepts.

Any other persisted version schedules a rebuild; no migration ladder exists because the index is
always recomputable from the packages themselves.
*/
inline constexpr int g_library_index_format_version = 1;

/*! \brief Outcome of loading the persisted index. */
struct LibraryIndexLoadResult
{
    /*! \brief Loaded index; empty whenever a rebuild is required. */
    LibraryIndex index;

    /*! \brief True when no usable index exists and a full rescan must rebuild it. */
    bool rebuild_required{false};

    /*! \brief Why the rebuild is required; empty when a usable index loaded. */
    std::string rebuild_reason;
};

/*!
\brief Loads the persisted library index, scheduling a rebuild on any doubt.
\param index_path Index document path.
\return The loaded index, or a rebuild-required outcome; never a hard failure.
*/
[[nodiscard]] LibraryIndexLoadResult loadLibraryIndex(const std::filesystem::path& index_path);

/*!
\brief Atomically writes the library index document, creating parent directories as needed.
\param index Index to persist.
\param index_path Index document path.
\return Empty success, or the typed persistence failure.
*/
[[nodiscard]] std::expected<void, LibraryIndexStoreError> saveLibraryIndex(
    const LibraryIndex& index, const std::filesystem::path& index_path);

} // namespace rock_hero::game::core
