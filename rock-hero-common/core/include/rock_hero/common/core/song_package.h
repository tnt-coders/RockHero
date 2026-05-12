/*!
\file song_package.h
\brief Runtime Rock Hero song package persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/song.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Result of writing runtime song package files into a directory. */
struct SongPackageWriteResult
{
    /*! \brief Arrangement IDs written into the song document, in document order. */
    std::vector<std::string> arrangement_ids;
};

/*!
\brief Reads runtime song data from an extracted song package directory.
\param directory Directory containing song.json and the files referenced by it.
\return Parsed song data, or a failure message.
*/
[[nodiscard]] std::expected<Song, std::string> readSongPackageDirectory(
    const std::filesystem::path& directory);

/*!
\brief Extracts and reads a runtime Rock Hero song package into an existing workspace.
\param package_path Runtime package archive to extract.
\param workspace_directory Existing directory that receives extracted package entries.
\return Parsed song data, or a failure message.
*/
[[nodiscard]] std::expected<Song, std::string> readSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory);

/*!
\brief Writes runtime song files into a package-content directory.
\param song_directory Directory that receives song.json and referenced runtime content.
\param song Song data to persist.
\return Written package metadata, or a failure message.
*/
[[nodiscard]] std::expected<SongPackageWriteResult, std::string> writeSongPackageDirectory(
    const std::filesystem::path& song_directory, const Song& song);

/*!
\brief Writes a runtime Rock Hero song package from a package-content directory.
\param package_path Destination runtime package path.
\param song_directory Directory that receives song.json and referenced runtime content.
\param song Song data to persist.
\return Empty success, or a failure message.
*/
[[nodiscard]] std::expected<void, std::string> writeSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& song_directory,
    const Song& song);

} // namespace rock_hero::common::core
