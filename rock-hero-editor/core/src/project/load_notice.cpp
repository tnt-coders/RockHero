#include "project/load_notice.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <string>
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

} // namespace rock_hero::editor::core
