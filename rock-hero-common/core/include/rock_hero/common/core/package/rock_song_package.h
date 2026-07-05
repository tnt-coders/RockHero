/*!
\file rock_song_package.h
\brief Native Rock Hero song package persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Reads native song data from an extracted Rock Hero song package directory.
\param directory Directory containing song.json and the files referenced by it.
\return Parsed song data, or a typed package failure.
*/
[[nodiscard]] std::expected<Song, SongPackageError> readRockSongPackageDirectory(
    const std::filesystem::path& directory);

/*!
\brief Extracts and reads a native Rock Hero song package into an existing workspace.
\param package_path Native song package to extract.
\param workspace_directory Existing directory that receives extracted native song package entries.
\return Parsed song data, or a typed package failure.
*/
[[nodiscard]] std::expected<Song, SongPackageError> readRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory);

/*!
\brief Writes native song files into a Rock Hero song package content directory.
\param song_directory Directory that receives song.json and referenced native song content.
\param song Song data to persist.
\return Arrangement IDs written into song.json, in document order, or a typed package failure.
*/
[[nodiscard]] std::expected<std::vector<std::string>, SongPackageError>
writeRockSongPackageDirectory(const std::filesystem::path& song_directory, const Song& song);

/*!
\brief Writes a native Rock Hero song package from a package content directory.
\param package_path Destination native song package path.
\param song_directory Directory that receives song.json and referenced native song content.
\param song Song data to persist.
\return Empty success, or a typed package failure.
*/
[[nodiscard]] std::expected<void, SongPackageError> writeRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& song_directory,
    const Song& song);

} // namespace rock_hero::common::core
