/*!
\file rock_song_package_describer.h
\brief Real package describer backed by common/core's extraction-free peek reader.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/package_description.h>
#include <rock_hero/common/core/package/song_package_error.h>
#include <rock_hero/game/core/library/i_library_package_describer.h>

namespace rock_hero::game::core
{

/*!
\brief Describes a package by streaming it through readRockSongPackageDescription.

A thin adapter: it forwards to the common/core peek reader, preserving the typed SongPackageError,
so the scan engine turns a hard read failure into a warning entry with the real diagnostic.
*/
class RockSongPackageDescriber final : public ILibraryPackageDescriber
{
public:
    /*!
    \brief Peeks one package's description straight from its archive.
    \param package_path Native `.rock` package to describe.
    \return The package description, or the typed reason it could not be read.
    */
    [[nodiscard]] std::expected<common::core::PackageDescription, common::core::SongPackageError>
    describe(const std::filesystem::path& package_path) override;
};

} // namespace rock_hero::game::core
