#include "project/load_notice.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// How many places one rule lists before the notice points at the log instead. Eight keeps a dialog
// readable on any screen while still naming every position in the ordinary case of a handful.
constexpr std::size_t g_places_listed_per_rule{8};

} // namespace

std::string loadConversionNoticeText(
    const common::core::Song& song,
    const std::vector<common::core::SongPackageConversion>& conversions)
{
    if (conversions.empty())
    {
        return {};
    }
    // Arrangement, then rule, each in first-seen order: std::map keeps the enum's declaration
    // order for rules, and arrangements are visited in index order because the reader emits them
    // that way.
    std::map<std::size_t, std::map<common::core::ChartRepair, std::vector<std::string>>> grouped;
    for (const common::core::SongPackageConversion& entry : conversions)
    {
        grouped[entry.arrangement][entry.conversion.repair].push_back(entry.conversion.where);
    }

    std::string text = "This chart was updated to the current chart rules. The file is unchanged "
                       "until you save.\n";
    for (const auto& [arrangement, rules] : grouped)
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
        for (const auto& [repair, places] : rules)
        {
            text += "- " + std::to_string(places.size()) + " x " +
                    std::string{common::core::chartRepairText(repair)} + ":\n    ";
            const std::size_t listed = std::min(places.size(), g_places_listed_per_rule);
            for (std::size_t index = 0; index < listed; ++index)
            {
                text += (index == 0 ? "" : ", ") + places[index];
            }
            if (places.size() > listed)
            {
                text += ", and " + std::to_string(places.size() - listed) + " more (see the log)";
            }
            text += "\n";
        }
    }
    return text;
}

} // namespace rock_hero::editor::core
