#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tuning_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A 4/4 map whose terminal is 3:1, and one arrangement with a valid tone track and one valid
// automation entry, so each test breaks exactly the slice it names.
[[nodiscard]] SongTimeline makeValidTimeline()
{
    return SongTimeline{
        .tempo_map = TempoMap::defaultMap(TimeDuration{4.0}),
        .sections = {},
        .arrangements = {
            ArrangementTimeline{
                .id = "lead",
                .chart = std::nullopt,
                .tone_track =
                    ToneTrack{
                        .regions =
                            {
                                ToneRegion{
                                    .id = "5a1f0c3d-7e2b-4a9c-8d1e-2f3a4b5c6d7e",
                                    .start = measureDownbeat(1),
                                    .tone_document_ref =
                                        "tones/9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d/tone.json",
                                },
                            },
                    },
                .tone_automation = {
                    ToneParameterAutomation{
                        .plugin_id = "plugin",
                        .param_id = "gain",
                        .points = {
                            ToneAutomationPoint{.position = measureDownbeat(1), .norm_value = 0.5F},
                        },
                    },
                },
            },
        },
    };
}

} // namespace

// The tone slices are validated with the rest, by the arrangement that holds them: a meter edit
// writes them, so a timeline whose tone track or automation breaks a rule is refused like one
// whose chart does.
TEST_CASE("Song timeline rules validate every arrangement's tone slices", "[core][song]")
{
    CHECK(validateSongTimelineRules(makeValidTimeline()).has_value());

    SongTimeline bad_track = makeValidTimeline();
    bad_track.arrangements.front().tone_track.regions.front().id = "not-a-uuid";
    const auto track_result = validateSongTimelineRules(bad_track);
    REQUIRE_FALSE(track_result.has_value());
    REQUIRE(std::holds_alternative<ArrangementRuleError>(track_result.error()));
    const auto& track_error = std::get<ArrangementRuleError>(track_result.error());
    CHECK(track_error.arrangement_id == "lead");
    CHECK(track_error.code() == ArrangementRuleErrorCode::ToneTrackRule);
    REQUIRE(std::holds_alternative<ToneTrackError>(track_error.cause));
    const auto& tone_track_error = std::get<ToneTrackError>(track_error.cause);
    CHECK(tone_track_error.code == ToneTrackErrorCode::InvalidRegionId);
    CHECK(tone_track_error.region_ids == std::vector<std::string>{"not-a-uuid"});
    // The text is the cause's own diagnostic, prefixed with the arrangement.
    CHECK(
        songTimelineErrorText(track_result.error()) ==
        "arrangement lead: tone region id must be a canonical UUIDv4: not-a-uuid");

    SongTimeline bad_automation = makeValidTimeline();
    bad_automation.arrangements.front().tone_automation.front().points.clear();
    const auto automation_result = validateSongTimelineRules(bad_automation);
    REQUIRE_FALSE(automation_result.has_value());
    REQUIRE(std::holds_alternative<ArrangementRuleError>(automation_result.error()));
    const auto& automation_error = std::get<ArrangementRuleError>(automation_result.error());
    CHECK(automation_error.arrangement_id == "lead");
    CHECK(automation_error.code() == ArrangementRuleErrorCode::ToneAutomationRule);
    REQUIRE(std::holds_alternative<InvalidToneAutomationEntry>(automation_error.cause));
    const auto& entry = std::get<InvalidToneAutomationEntry>(automation_error.cause);
    CHECK(entry.plugin_id == "plugin");
    // The one diagnostic composed here rather than carried by the cause.
    CHECK(
        songTimelineErrorText(automation_result.error()) ==
        "arrangement lead: tone automation entry is invalid for plugin: plugin");
}

// An authored hand position is downstream of the grid: a re-address that leaves one uncovering
// drops it (the editor's timeline normalization), so the timeline rules never refuse over one,
// while the chart rules a reader asks of the same chart still name it.
TEST_CASE("Song timeline rules leave hand-position coverage to the drop", "[core][song]")
{
    // A fret-3 note struck at 1:1 under a fret-5 position there, which does not cover it.
    Chart chart;
    chart.tuning.strings = testing::standardTuning();
    chart.notes = {ChartNote{
        .position = measureDownbeat(1),
        .string = 1,
        .fret = 3,
        .sustain = Fraction{1, 4},
        .bend = 0.0,
        .keyframes = {},
    }};
    chart.fret_hand_positions = {
        FretHandPosition{.position = measureDownbeat(1), .fret = 5, .end_fret = {}},
    };
    SongTimeline uncovering = makeValidTimeline();
    REQUIRE_FALSE(validateChartRules(chart, uncovering.tempo_map).has_value());

    uncovering.arrangements.front().chart = std::move(chart);
    CHECK(validateSongTimelineRules(uncovering).has_value());
}

// The song-wide slices report their own rule's error unchanged, so a caller reads the records it
// names, and its text is the rule's own message.
TEST_CASE("Song timeline rules report a tempo-map violation as the tempo map's", "[core][song]")
{
    SongTimeline short_reign = makeValidTimeline();
    short_reign.tempo_map = TempoMap{
        {TimeSignatureChange{.place = {}, .meter = {.numerator = 4, .denominator = 4}},
         TimeSignatureChange{.place = Fraction{1, 2}, .meter = {.numerator = 4, .denominator = 4}}},
        short_reign.tempo_map.anchors()
    };
    const auto tempo_result = validateTempoMapRules(short_reign.tempo_map);
    REQUIRE_FALSE(tempo_result.has_value());
    REQUIRE(tempo_result.error().code == TempoMapErrorCode::ReignTooShort);

    const auto result = validateSongTimelineRules(short_reign);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(std::holds_alternative<TempoMapError>(result.error()));
    const auto& tempo_error = std::get<TempoMapError>(result.error());
    CHECK(tempo_error.code == TempoMapErrorCode::ReignTooShort);
    CHECK(tempo_error.meter_places == std::vector<Fraction>{Fraction{}, Fraction{1, 2}});
    CHECK(songTimelineErrorText(result.error()) == tempo_result.error().message);
}

} // namespace rock_hero::common::core
