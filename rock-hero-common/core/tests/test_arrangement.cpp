#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/timeline.h>

namespace rock_hero::common::core
{

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
    CHECK(arrangement.tones.empty());
}

// Verifies arrangements own playable route audio and tone metadata intact.
TEST_CASE("Arrangement holds playable route data", "[core][arrangement]")
{
    Arrangement arr;
    arr.id = "lead";
    arr.part = Part::Lead;
    arr.difficulty = DifficultyRating{8};
    arr.audio_asset = AudioAsset{
        .path = std::filesystem::path{"lead.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    arr.audio_duration = TimeDuration{12.0};
    arr.tones = {Tone{.tone_document_ref = "tones/lead/tone.json", .name = "Lead"}};

    CHECK(arr.id == "lead");
    CHECK(arr.part == Part::Lead);
    CHECK(arr.difficulty == DifficultyRating{8});
    CHECK(difficultyTier(arr.difficulty) == DifficultyTier::Expert);
    CHECK(arr.audio_asset.path == std::filesystem::path{"lead.wav"});
    CHECK(arr.audio_duration == TimeDuration{12.0});
    CHECK(toneNameFor(arr, "tones/lead/tone.json") == "Lead");
    CHECK(
        arr.audioTimelineRange() == TimeRange{
                                        .start = TimePosition{},
                                        .end = TimePosition{12.0},
                                    });
}

// Verifies a positive audio start offset shifts the arrangement's audio later on the timeline,
// so a backing recording that begins after the song's first beat sits with silence before it.
TEST_CASE("Arrangement audio timeline range honors the start offset", "[core][arrangement]")
{
    Arrangement arr;
    arr.audio_asset = AudioAsset{
        .path = std::filesystem::path{"lead.wav"},
        .normalization = std::nullopt,
        .start_offset = TimeDuration{1.5},
    };
    arr.audio_duration = TimeDuration{12.0};

    CHECK(
        arr.audioTimelineRange() == TimeRange{
                                        .start = TimePosition{1.5},
                                        .end = TimePosition{13.5},
                                    });
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
