#include "project/load_notice.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

std::string loadConversionNoticeText(
    const common::core::Song& song,
    const std::vector<common::core::SongPackageConversion>& conversions,
    const std::filesystem::path& log_file)
{
    if (conversions.empty())
    {
        return {};
    }
    // Arrangement, then rule, each in first-seen order: std::map keeps the enum's declaration
    // order for rules, and arrangements are visited in index order because the reader emits them
    // that way.
    std::map<std::size_t, std::map<common::core::ChartRepair, std::size_t>> counts;
    for (const common::core::SongPackageConversion& entry : conversions)
    {
        ++counts[entry.arrangement][entry.conversion.repair];
    }

    std::string text = std::to_string(conversions.size()) +
                       (conversions.size() == 1 ? " note was" : " notes were") +
                       " updated to the current chart rules. The file is unchanged until you "
                       "save.\n";
    for (const auto& [arrangement, rules] : counts)
    {
        if (song.arrangements.size() > 1)
        {
            const std::string part =
                arrangement < song.arrangements.size()
                    ? std::string{common::core::partToken(song.arrangements[arrangement].part)}
                    : "arrangement " + std::to_string(arrangement);
            text += "\n" + part + ":\n";
        }
        else
        {
            text += "\n";
        }
        for (const auto& [repair, count] : rules)
        {
            text += "- " + std::to_string(count) + " x " +
                    std::string{common::core::chartRepairText(repair)} + "\n";
        }
    }
    text += "\nEvery position is listed in the editor log";
    text += log_file.empty() ? "." : ":\n" + log_file.string();
    return text;
}

std::string unnormalizedAudioNoticeText(const common::core::Song& song)
{
    // File names, first-seen order, deduplicated: arrangements commonly share one backing file,
    // and the workspace directory those paths sit in is a temporary the charter never sees.
    std::vector<std::string> names;
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.normalization.has_value())
        {
            continue;
        }

        std::string name = arrangement.audio_asset.path.filename().string();
        if (std::ranges::find(names, name) == names.end())
        {
            names.push_back(std::move(name));
        }
    }

    if (names.empty())
    {
        return {};
    }

    // The one-file case is the common one and reads better as a sentence than as a list of one.
    if (names.size() == 1)
    {
        return names.front() +
               " is silent or too quiet to measure, so no normalization gain was applied. It "
               "plays at its raw level.";
    }

    std::string text = "These backing tracks are silent or too quiet to measure, so no "
                       "normalization gain was applied. They play at their raw level.\n";
    for (const std::string& name : names)
    {
        text += "\n- " + name;
    }
    return text;
}

} // namespace rock_hero::editor::core
