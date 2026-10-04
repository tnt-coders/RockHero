#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>

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
    CHECK(track_result.error().starts_with("arrangement lead: "));

    SongTimeline bad_automation = makeValidTimeline();
    bad_automation.arrangements.front().tone_automation.front().points.clear();
    const auto automation_result = validateSongTimelineRules(bad_automation);
    REQUIRE_FALSE(automation_result.has_value());
    CHECK(automation_result.error().starts_with("arrangement lead: "));
}

} // namespace rock_hero::common::core
