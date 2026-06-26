/*!
\file rock_song_package.h
\brief Native Rock Hero song package persistence helpers.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/song.h>
#include <rock_hero/common/core/song_package_error.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Product-level validation limits used while reading or writing native song packages. */
struct SongPackageValidationConfig
{
    /*!
    \brief Maximum one-based playable string number accepted in arrangement note documents.

    The default supports common extended-range guitars while bounding malformed package values.
    Product configuration may raise or lower this value before calling package APIs.
    */
    int max_playable_string_count{10};
};

/*!
\brief Reads native song data from an extracted Rock Hero song package directory.
\param directory Directory containing song.json and the files referenced by it.
\param validation_config Product-level validation limits applied to package data.
\return Parsed song data, or a typed package failure.
*/
[[nodiscard]] std::expected<Song, SongPackageError> readRockSongPackageDirectory(
    const std::filesystem::path& directory, SongPackageValidationConfig validation_config = {});

/*!
\brief Extracts and reads a native Rock Hero song package into an existing workspace.
\param package_path Native song package to extract.
\param workspace_directory Existing directory that receives extracted native song package entries.
\param validation_config Product-level validation limits applied to package data.
\return Parsed song data, or a typed package failure.
*/
[[nodiscard]] std::expected<Song, SongPackageError> readRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& workspace_directory,
    SongPackageValidationConfig validation_config = {});

/*!
\brief Writes native song files into a Rock Hero song package content directory.
\param song_directory Directory that receives song.json and referenced native song content.
\param song Song data to persist.
\param validation_config Product-level validation limits applied before writing package data.
\return Arrangement IDs written into song.json, in document order, or a typed package failure.
*/
[[nodiscard]] std::expected<std::vector<std::string>, SongPackageError>
writeRockSongPackageDirectory(
    const std::filesystem::path& song_directory, const Song& song,
    SongPackageValidationConfig validation_config = {});

/*!
\brief Writes a native Rock Hero song package from a package content directory.
\param package_path Destination native song package path.
\param song_directory Directory that receives song.json and referenced native song content.
\param song Song data to persist.
\param validation_config Product-level validation limits applied before writing package data.
\return Empty success, or a typed package failure.
*/
[[nodiscard]] std::expected<void, SongPackageError> writeRockSongPackage(
    const std::filesystem::path& package_path, const std::filesystem::path& song_directory,
    const Song& song, SongPackageValidationConfig validation_config = {});

} // namespace rock_hero::common::core
