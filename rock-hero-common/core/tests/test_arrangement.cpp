#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>
#include <variant>

namespace rock_hero::common::core
{

// Verifies a default chart event sits on measure 1 beat 1, is non-sustained, and is an empty note.
TEST_CASE("ChartEvent default construction is an on-beat single note", "[core][arrangement]")
{
    const ChartEvent event;

    CHECK(event.start == GridPosition{});
    CHECK_FALSE(event.end.has_value());
    REQUIRE(std::holds_alternative<SingleNote>(event.content));
    CHECK(std::get<SingleNote>(event.content).string_number == 0);
    CHECK(std::get<SingleNote>(event.content).fret == 0);
}

// Verifies Arrangement's default value remains an unplayable draft until audio is assigned.
TEST_CASE("Arrangement default construction is lead unrated", "[core][arrangement]")
{
    const Arrangement arrangement;

    CHECK(arrangement.id.empty());
    CHECK(arrangement.part == Part::Lead);
    CHECK(arrangement.difficulty == DifficultyRating{});
    CHECK(difficultyTier(arrangement.difficulty) == DifficultyTier::Unknown);
    CHECK(arrangement.audio_asset.path.empty());
    CHECK(arrangement.audio_duration == TimeDuration{});
    CHECK(arrangement.tone_document_ref.empty());
    CHECK(arrangement.tuning.open_strings.empty());
    CHECK(arrangement.chord_templates.empty());
    CHECK(arrangement.events.empty());
}

// Verifies arrangements own playable route data with timing, tone, and notes intact.
TEST_CASE("Arrangement holds playable route data", "[core][arrangement]")
{
    Arrangement arr;
    arr.id = "lead";
    arr.part = Part::Lead;
    arr.difficulty = DifficultyRating{8};
    arr.audio_asset =
        AudioAsset{.path = std::filesystem::path{"lead.wav"}, .normalization = std::nullopt};
    arr.audio_duration = TimeDuration{12.0};
    arr.tone_document_ref = "tone/lead.json";
    arr.tuning = Tuning{.open_strings = {"E4", "B3", "G3", "D3", "A2", "E2"}};
    arr.chord_templates.push_back(
        ChordTemplate{
            .id = "Am",
            .name = "Am",
            .voicing = {ChordVoicingString{.string_number = 1, .fret = 0}},
        });
    arr.events.push_back(
        ChartEvent{
            .start = GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 4}},
            .end = GridPosition{.measure = 3, .beat = 1},
            .content = SingleNote{.string_number = 1, .fret = 5},
        });
    arr.events.push_back(
        ChartEvent{
            .start = GridPosition{.measure = 3, .beat = 2},
            .content = ChordInstance{.template_id = "Am"},
        });

    CHECK(arr.id == "lead");
    CHECK(arr.part == Part::Lead);
    CHECK(arr.difficulty == DifficultyRating{8});
    CHECK(difficultyTier(arr.difficulty) == DifficultyTier::Expert);
    CHECK(arr.audio_asset.path == std::filesystem::path{"lead.wav"});
    CHECK(arr.audio_duration == TimeDuration{12.0});
    CHECK(arr.tone_document_ref == "tone/lead.json");
    CHECK(
        arr.audioTimelineRange() == TimeRange{
                                        .start = TimePosition{},
                                        .end = TimePosition{12.0},
                                    });
    REQUIRE(arr.events.size() == 2);
    CHECK(arr.events[0].start == GridPosition{.measure = 2, .beat = 1, .offset = Fraction{1, 4}});
    REQUIRE(arr.events[0].end.has_value());
    CHECK(*arr.events[0].end == GridPosition{.measure = 3, .beat = 1});
    REQUIRE(std::holds_alternative<SingleNote>(arr.events[0].content));
    CHECK(std::get<SingleNote>(arr.events[0].content).string_number == 1);
    CHECK(std::get<SingleNote>(arr.events[0].content).fret == 5);
    CHECK(arr.events[1].start == GridPosition{.measure = 3, .beat = 2});
    REQUIRE(std::holds_alternative<ChordInstance>(arr.events[1].content));
    CHECK(std::get<ChordInstance>(arr.events[1].content).template_id == "Am");
}

// Verifies timeline value wrappers default to the start of the song.
TEST_CASE("Time value wrappers default to zero seconds", "[core][timeline]")
{
    const TimePosition position;
    const TimeDuration duration;

    CHECK(position == TimePosition{});
    CHECK(duration == TimeDuration{});
}

// Verifies timeline value types preserve their underlying second values.
TEST_CASE("Time value wrappers store seconds", "[core][timeline]")
{
    const TimePosition position{12.5};
    const TimeDuration duration{48.0};

    CHECK(position == TimePosition{12.5});
    CHECK(duration == TimeDuration{48.0});
}

// Verifies timeline equality compares the wrapped second value.
TEST_CASE("Time value wrappers compare stored seconds", "[core][timeline]")
{
    CHECK(TimePosition{12.5} == TimePosition{12.5});
    CHECK_FALSE(TimePosition{12.5} == TimePosition{12.0});
    CHECK(TimeDuration{48.0} == TimeDuration{48.0});
    CHECK_FALSE(TimeDuration{48.0} == TimeDuration{49.0});
}

} // namespace rock_hero::common::core
