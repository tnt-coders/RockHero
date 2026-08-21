#include "project/load_notice.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

[[nodiscard]] common::core::SongPackageConversion conversionAt(
    const std::size_t arrangement, const common::core::ChartRepair repair, std::string where)
{
    return common::core::SongPackageConversion{
        .arrangement = arrangement,
        .conversion = common::core::ChartConversion{.repair = repair, .where = std::move(where)},
    };
}

[[nodiscard]] common::core::Song songWithParts(std::initializer_list<common::core::Part> parts)
{
    common::core::Song song;
    for (const common::core::Part part : parts)
    {
        common::core::Arrangement arrangement;
        arrangement.part = part;
        song.arrangements.push_back(std::move(arrangement));
    }
    return song;
}

} // namespace

// The notice is the load's report: a rule change repairs-and-reports instead of bricking, and this
// text is what makes the repair honest. It groups by rule, lists the places, names the arrangement
// only when there is more than one to tell apart, and always says the file is untouched until a
// save.
TEST_CASE("Load conversion notice groups repairs by rule and place", "[core][project]")
{
    SECTION("no conversions is no notice at all")
    {
        CHECK(loadConversionNoticeText(songWithParts({common::core::Part::Lead}), {}).empty());
    }

    SECTION("a single arrangement lists rules and places without naming the part")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::MutedTail, "3:1 string 2"),
            conversionAt(0, common::core::ChartRepair::UnjustifiedLegato, "3:2 string 2"),
            conversionAt(0, common::core::ChartRepair::MutedTail, "5:1 string 4"),
        };
        const std::string text =
            loadConversionNoticeText(songWithParts({common::core::Part::Lead}), conversions);
        CHECK(text.find("The file is unchanged until you save") != std::string::npos);
        // Same-rule repairs coalesce into one line with both places, in load order.
        CHECK(text.find("2 x a dead note rings nothing") != std::string::npos);
        CHECK(text.find("3:1 string 2, 5:1 string 4") != std::string::npos);
        CHECK(text.find("1 x a legato mark had nothing to connect to") != std::string::npos);
        CHECK(text.find("lead") == std::string::npos);
    }

    SECTION("more than one arrangement names each by its part")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::FretPastBoard, "1:1 string 1"),
            conversionAt(1, common::core::ChartRepair::FretPastBoard, "2:1 string 1"),
        };
        const std::string text = loadConversionNoticeText(
            songWithParts({common::core::Part::Lead, common::core::Part::Bass}), conversions);
        const std::size_t lead =
            text.find(std::string{common::core::partToken(common::core::Part::Lead)} + ":");
        const std::size_t bass =
            text.find(std::string{common::core::partToken(common::core::Part::Bass)} + ":");
        REQUIRE(lead != std::string::npos);
        REQUIRE(bass != std::string::npos);
        CHECK(lead < text.find("1:1 string 1"));
        CHECK(text.find("1:1 string 1") < bass);
        CHECK(bass < text.find("2:1 string 1"));
    }

    SECTION("a long list per rule is capped and points at the log")
    {
        std::vector<common::core::SongPackageConversion> conversions;
        for (int measure = 1; measure <= 11; ++measure)
        {
            conversions.push_back(conversionAt(
                0, common::core::ChartRepair::MutedTail, std::to_string(measure) + ":1 string 1"));
        }
        const std::string text =
            loadConversionNoticeText(songWithParts({common::core::Part::Lead}), conversions);
        CHECK(text.find("11 x ") != std::string::npos);
        CHECK(text.find("8:1 string 1") != std::string::npos);
        CHECK(text.find("9:1 string 1") == std::string::npos);
        CHECK(text.find("and 3 more (see the log)") != std::string::npos);
    }
}

} // namespace rock_hero::editor::core
