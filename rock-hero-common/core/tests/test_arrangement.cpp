#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/core/arrangement.h>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>

namespace rock_hero::common::core
{

// Verifies an unset note event starts at the first beat with no fret/string assignment.
TEST_CASE("NoteEvent default construction is empty", "[core][arrangement]")
{
    const NoteEvent note;

    CHECK(note.measure == 1);
    CHECK(note.beat == 1);
    CHECK(note.offset == Fraction{});
    CHECK(note.duration_beats == Fraction{});
    CHECK(note.string_number == 0);
    CHECK(note.fret == 0);
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
    CHECK(arrangement.note_events.empty());
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
    arr.note_events.push_back(
        {.measure = 2,
         .beat = 1,
         .offset = Fraction{1, 4},
         .duration_beats = Fraction{1},
         .string_number = 1,
         .fret = 5});
    arr.note_events.push_back(
        {.measure = 3,
         .beat = 2,
         .offset = Fraction{},
         .duration_beats = Fraction{},
         .string_number = 6,
         .fret = 0});

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
    REQUIRE(arr.note_events.size() == 2);
    CHECK(arr.note_events[0].measure == 2);
    CHECK(arr.note_events[0].beat == 1);
    CHECK(arr.note_events[0].offset == Fraction{1, 4});
    CHECK(arr.note_events[0].duration_beats == Fraction{1});
    CHECK(arr.note_events[0].string_number == 1);
    CHECK(arr.note_events[0].fret == 5);
    CHECK(arr.note_events[1].measure == 3);
    CHECK(arr.note_events[1].beat == 2);
    CHECK(arr.note_events[1].offset == Fraction{});
    CHECK(arr.note_events[1].duration_beats == Fraction{});
    CHECK(arr.note_events[1].string_number == 6);
    CHECK(arr.note_events[1].fret == 0);
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
