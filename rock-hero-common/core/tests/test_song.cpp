#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/song.h>
#include <vector>

namespace rock_hero::common::core
{

// Verifies that default song state is empty so loaders can detect unset metadata and assets.
TEST_CASE("Song default construction is empty", "[core][song]")
{
    const Song song;
    CHECK(song.metadata.title.empty());
    CHECK(song.metadata.artist.empty());
    CHECK(song.metadata.album.empty());
    CHECK(song.metadata.year == 0);
    CHECK(song.tempo_map == TempoMap{});
    CHECK(song.arrangements.empty());
}

// Verifies metadata fields remain plain value storage until validation rules are introduced.
TEST_CASE("Song metadata round-trip", "[core][song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.album = "Test Album";
    song.metadata.year = 2026;
    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.artist == "Test Artist");
    CHECK(song.metadata.album == "Test Album");
    CHECK(song.metadata.year == 2026);
}

// Verifies songs can aggregate multiple part/difficulty-rating variants directly.
TEST_CASE("Song holds multiple arrangements", "[core][song]")
{
    Song song;
    song.arrangements.push_back(
        Arrangement{
            .id = "lead",
            .part = Part::Lead,
            .difficulty = DifficultyRating{2},
            .audio_asset =
                AudioAsset{
                    .path = std::filesystem::path{"lead.wav"}, .normalization = std::nullopt
                },
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });
    song.arrangements.push_back(
        Arrangement{
            .id = "bass",
            .part = Part::Bass,
            .difficulty = DifficultyRating{9},
            .audio_asset =
                AudioAsset{
                    .path = std::filesystem::path{"bass.wav"}, .normalization = std::nullopt
                },
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .note_events = {},
        });

    REQUIRE(song.arrangements.size() == 2);
    CHECK(song.arrangements[0].part == Part::Lead);
    CHECK(song.arrangements[0].difficulty == DifficultyRating{2});
    CHECK(difficultyTier(song.arrangements[0].difficulty) == DifficultyTier::Easy);
    CHECK(song.arrangements[0].note_events.empty());
    CHECK(song.arrangements[1].part == Part::Bass);
    CHECK(song.arrangements[1].difficulty == DifficultyRating{9});
    CHECK(difficultyTier(song.arrangements[1].difficulty) == DifficultyTier::Master);
    CHECK(song.arrangements[1].note_events.empty());
}

// Verifies Song stores all top-level value fields without imposing validation policy.
TEST_CASE("Song stores top-level value fields", "[core][song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.album = "Test Album";
    song.metadata.year = 2026;
    song.tempo_map = TempoMap{
        std::vector{
            TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            TimeSignatureChange{.measure = 3, .numerator = 3, .denominator = 4},
        },
        std::vector{
            BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            BeatAnchor{.measure = 4, .beat = 1, .seconds = 5.5},
        },
    };
    song.arrangements.push_back(
        {.id = "rhythm",
         .part = Part::Rhythm,
         .difficulty = DifficultyRating{6},
         .audio_asset =
             AudioAsset{
                 .path = std::filesystem::path{"audio/rhythm.wav"}, .normalization = std::nullopt
             },
         .audio_duration = TimeDuration{42.0},
         .tone_document_ref = "tone/rhythm.json",
         .note_events = {
             {.measure = 2,
              .beat = 3,
              .offset = Fraction{1, 2},
              .duration_beats = Fraction{5, 4},
              .string_number = 2,
              .fret = 7}
         }});

    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.artist == "Test Artist");
    CHECK(song.metadata.album == "Test Album");
    CHECK(song.metadata.year == 2026);
    CHECK(song.tempo_map.timeSignatures().size() == 2);
    CHECK(song.tempo_map.anchors().size() == 2);
    REQUIRE(song.arrangements.size() == 1);
    const Arrangement& arrangement = song.arrangements[0];
    CHECK(arrangement.part == Part::Rhythm);
    CHECK(arrangement.difficulty == DifficultyRating{6});
    CHECK(difficultyTier(arrangement.difficulty) == DifficultyTier::Hard);
    CHECK(arrangement.audio_asset.path == std::filesystem::path{"audio/rhythm.wav"});
    CHECK(arrangement.audio_duration == TimeDuration{42.0});
    CHECK(arrangement.tone_document_ref == "tone/rhythm.json");
    REQUIRE(arrangement.note_events.size() == 1);
    CHECK(arrangement.note_events[0].measure == 2);
    CHECK(arrangement.note_events[0].beat == 3);
    CHECK(arrangement.note_events[0].offset == Fraction{1, 2});
    CHECK(arrangement.note_events[0].duration_beats == Fraction{5, 4});
    CHECK(arrangement.note_events[0].string_number == 2);
    CHECK(arrangement.note_events[0].fret == 7);
}

} // namespace rock_hero::common::core
