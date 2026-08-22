#include "project/load_notice.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
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
// text is what makes the repair honest. It is a glance, not a listing: a total, one count per rule,
// the arrangement named only when there is more than one to tell apart, the file-is-untouched
// fact, and the log as the place every position went.
TEST_CASE("Load conversion notice summarizes repairs and points at the log", "[core][project]")
{
    const std::filesystem::path log_file{"C:/logs/Rock Hero Editor.log"};

    SECTION("no conversions is no notice at all")
    {
        CHECK(loadConversionNoticeText(songWithParts({common::core::Part::Lead}), {}, log_file)
                  .empty());
    }

    SECTION("a single arrangement counts each rule without naming the part or any place")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::MutedTail, "3:1 string 2"),
            conversionAt(0, common::core::ChartRepair::UnjustifiedLegato, "3:2 string 2"),
            conversionAt(0, common::core::ChartRepair::MutedTail, "5:1 string 4"),
        };
        const std::string text = loadConversionNoticeText(
            songWithParts({common::core::Part::Lead}), conversions, log_file);
        CHECK(text.find("3 notes were updated") != std::string::npos);
        CHECK(text.find("The file is unchanged until you save") != std::string::npos);
        CHECK(text.find("2 x a dead note rings nothing") != std::string::npos);
        CHECK(text.find("1 x a legato mark had nothing to connect to") != std::string::npos);
        // Positions belong to the log, and the notice says where that is.
        CHECK(text.find("3:1 string 2") == std::string::npos);
        CHECK(text.find(log_file.string()) != std::string::npos);
        CHECK(text.find("lead") == std::string::npos);
    }

    SECTION("one repair reads in the singular")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::FretPastBoard, "1:1 string 1"),
        };
        const std::string text = loadConversionNoticeText(
            songWithParts({common::core::Part::Lead}), conversions, log_file);
        CHECK(text.find("1 note was updated") != std::string::npos);
    }

    SECTION("more than one arrangement names each by its part")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::FretPastBoard, "1:1 string 1"),
            conversionAt(1, common::core::ChartRepair::FretPastBoard, "2:1 string 1"),
        };
        const std::string text = loadConversionNoticeText(
            songWithParts({common::core::Part::Lead, common::core::Part::Bass}),
            conversions,
            log_file);
        const std::size_t lead =
            text.find(std::string{common::core::partToken(common::core::Part::Lead)} + ":");
        const std::size_t bass =
            text.find(std::string{common::core::partToken(common::core::Part::Bass)} + ":");
        REQUIRE(lead != std::string::npos);
        REQUIRE(bass != std::string::npos);
        CHECK(lead < bass);
    }

    SECTION("without a log file the pointer still names the log")
    {
        const std::vector<common::core::SongPackageConversion> conversions{
            conversionAt(0, common::core::ChartRepair::MutedTail, "3:1 string 2"),
        };
        const std::string text =
            loadConversionNoticeText(songWithParts({common::core::Part::Lead}), conversions, {});
        CHECK(text.find("listed in the editor log.") != std::string::npos);
    }
}

} // namespace rock_hero::editor::core
