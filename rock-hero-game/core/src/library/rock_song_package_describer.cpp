#include "library/rock_song_package_describer.h"

namespace rock_hero::game::core
{

std::expected<common::core::PackageDescription, common::core::SongPackageError>
RockSongPackageDescriber::describe(const std::filesystem::path& package_path)
{
    return common::core::readRockSongPackageDescription(package_path);
}

} // namespace rock_hero::game::core
