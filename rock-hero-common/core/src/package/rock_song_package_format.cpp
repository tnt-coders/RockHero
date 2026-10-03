#include "rock_song_package_format.h"

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <set>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

[[nodiscard]] bool hasFlacExtension(const std::filesystem::path& path)
{
    return hasExtensionIgnoringCase(path, ".flac");
}

// Translates the shared tempo-map domain rules into the package error surface.
std::expected<void, SongPackageError> validateTempoMap(const TempoMap& tempo_map)
{
    if (const auto structural = validateTempoMapRules(tempo_map); !structural.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidSongDocument,
            "tempoMap: " + structural.error().message,
        }};
    }

    return std::expected<void, SongPackageError>{};
}

// Translates shared tone-track domain rules into the package error surface.
std::expected<void, SongPackageError> validateToneTrack(
    const ToneTrack& tone_track, const TempoMap& tempo_map)
{
    if (const auto structural = validateToneTrackRules(tone_track, tempo_map);
        !structural.has_value())
    {
        return std::unexpected{SongPackageError{
            SongPackageErrorCode::InvalidArrangement,
            structural.error().message,
        }};
    }

    return std::expected<void, SongPackageError>{};
}

std::expected<void, SongPackageError> validateToneAutomationEntries(
    const std::vector<ToneParameterAutomation>& entries, const TempoMap& tempo_map)
{
    std::set<std::pair<std::string, std::string>> seen_parameters;
    for (const ToneParameterAutomation& automation : entries)
    {
        if (!isValidToneParameterAutomation(automation, tempo_map))
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation entry is invalid for plugin: " + automation.plugin_id,
            }};
        }
        if (!seen_parameters.emplace(automation.plugin_id, automation.param_id).second)
        {
            return std::unexpected{SongPackageError{
                SongPackageErrorCode::InvalidArrangement,
                "toneAutomation repeats a plugin/parameter pair: " + automation.plugin_id + "/" +
                    automation.param_id,
            }};
        }
    }

    return std::expected<void, SongPackageError>{};
}

} // namespace rock_hero::common::core
