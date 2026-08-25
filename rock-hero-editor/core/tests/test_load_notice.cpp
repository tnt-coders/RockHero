#include "project/load_notice.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/audio_normalization.h>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <string_view>
#include <utility>
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

// One arrangement per entry, each naming a backing file and whether its analysis produced a
// normalization record. This is the state a completed open or import hands the notice builder.
[[nodiscard]] common::core::Song songWithAudio(
    std::initializer_list<std::pair<std::string_view, bool>> assets)
{
    common::core::Song song;
    for (const auto& [file_name, normalized] : assets)
    {
        common::core::Arrangement arrangement;
        arrangement.audio_asset.path = std::filesystem::path{"workspace"} / file_name;
        if (normalized)
        {
            arrangement.audio_asset.normalization = common::core::AudioNormalization{
                .gain_db = -4.0,
                .validation_sha256 = std::string(64, 'a'),
            };
        }
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
            conversionAt(0, common::core::ChartRepair::OverlappingTail, "3:1 string 2"),
            conversionAt(0, common::core::ChartRepair::UnjustifiedLegato, "3:2 string 2"),
            conversionAt(0, common::core::ChartRepair::OverlappingTail, "5:1 string 4"),
        };
        const std::string text = loadConversionNoticeText(
            songWithParts({common::core::Part::Lead}), conversions, log_file);
        CHECK(text.find("3 notes were updated") != std::string::npos);
        CHECK(text.find("The file is unchanged until you save") != std::string::npos);
        CHECK(text.find("2 x a re-strike stops the ring") != std::string::npos);
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
            conversionAt(0, common::core::ChartRepair::OverlappingTail, "3:1 string 2"),
        };
        const std::string text =
            loadConversionNoticeText(songWithParts({common::core::Part::Lead}), conversions, {});
        CHECK(text.find("listed in the editor log.") != std::string::npos);
    }
}

// The audio half of the same repairs-and-reports rule: audio that states no loudness reading gets
// no gain, and the charter is told rather than blocked. The notice names the file, not the
// workspace path it was extracted into, and says the level it plays at.
TEST_CASE("Unnormalized audio notice names each unmeasured backing track", "[core][project]")
{
    SECTION("every asset normalized is no notice at all")
    {
        CHECK(unnormalizedAudioNoticeText(
                  songWithAudio({{"backing.flac", true}, {"bass.flac", true}}))
                  .empty());
    }

    SECTION("one unmeasured track reads as a sentence, not a list of one")
    {
        const std::string text =
            unnormalizedAudioNoticeText(songWithAudio({{"silence.flac", false}}));
        CHECK(text.find("silence.flac is silent or too quiet to measure") == 0);
        CHECK(text.find("plays at its raw level") != std::string::npos);
        CHECK(text.find("\n") == std::string::npos);
    }

    SECTION("only the unmeasured tracks are named")
    {
        const std::string text = unnormalizedAudioNoticeText(
            songWithAudio({{"lead.flac", true}, {"silence.flac", false}}));
        CHECK(text.find("silence.flac") != std::string::npos);
        CHECK(text.find("lead.flac") == std::string::npos);
    }

    SECTION("several unmeasured tracks are listed once each")
    {
        const std::string text = unnormalizedAudioNoticeText(songWithAudio(
            {{"silence.flac", false}, {"quiet.flac", false}, {"silence.flac", false}}));
        CHECK(text.find("They play at their raw level") != std::string::npos);
        CHECK(text.find("- silence.flac") != std::string::npos);
        CHECK(text.find("- quiet.flac") != std::string::npos);
        // A backing file shared by two arrangements is one line, not two.
        CHECK(text.find("- silence.flac") == text.rfind("- silence.flac"));
    }
}

} // namespace rock_hero::editor::core
