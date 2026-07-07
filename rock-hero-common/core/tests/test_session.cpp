#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/song/song.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Builds the project-owned asset value used by session audio tests.
[[nodiscard]] AudioAsset makeAudioAsset(std::filesystem::path path)
{
    return AudioAsset{.path = std::move(path), .normalization = std::nullopt, .start_offset = {}};
}

// Builds a song with one loaded arrangement for session replacement tests.
[[nodiscard]] Song makeSongWithAudio(std::filesystem::path path, TimeDuration duration)
{
    Song song;
    song.metadata.title = "Test Song";
    song.arrangements.push_back(
        Arrangement{
            .id = "lead",
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset = makeAudioAsset(std::move(path)),
            .audio_duration = duration,
            .tone_document_ref = {},
            .tones = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
        });
    return song;
}

} // namespace

// Verifies audio asset references compare by their stored filesystem path.
TEST_CASE("AudioAsset equality compares stored paths", "[core][audio_asset]")
{
    const AudioAsset first{
        .path = std::filesystem::path{"mix.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    const AudioAsset matching{
        .path = std::filesystem::path{"mix.wav"}, .normalization = std::nullopt, .start_offset = {}
    };
    const AudioAsset different{
        .path = std::filesystem::path{"stem.wav"}, .normalization = std::nullopt, .start_offset = {}
    };

    CHECK(first == matching);
    CHECK_FALSE(first == different);
}

// Verifies a default asset is explicitly empty until a file-like path is assigned.
TEST_CASE("AudioAsset default construction is empty", "[core][audio_asset]")
{
    const AudioAsset audio_asset;

    CHECK(audio_asset.path.empty());
}

// Verifies a new session has no arrangement until project-file loading succeeds.
TEST_CASE("Session default construction is empty", "[core][session]")
{
    const Session session;

    CHECK(session.song().arrangements.empty());
    CHECK(session.arrangements().empty());
    CHECK(session.currentArrangement() == nullptr);
    CHECK(session.timeline() == TimeRange{});
}

// Verifies reset returns a loaded session to the same no-project state used at construction.
TEST_CASE("Session reset restores the empty project state", "[core][session]")
{
    Session session;
    REQUIRE(session.loadSong(
        makeSongWithAudio(std::filesystem::path{"mix.wav"}, TimeDuration{4.0}), 0));

    session.reset();

    CHECK(session.song().arrangements.empty());
    CHECK(session.arrangements().empty());
    CHECK(session.currentArrangement() == nullptr);
    CHECK(session.timeline() == TimeRange{});
}

// Verifies loading a prepared song replaces the empty no-project session.
TEST_CASE("Session loadSong replaces the current song", "[core][session]")
{
    Session session;
    const AudioAsset audio_asset = makeAudioAsset(std::filesystem::path{"mix.wav"});
    auto song = makeSongWithAudio(audio_asset.path, TimeDuration{8.0});

    const bool loaded = session.loadSong(std::move(song), 0);

    CHECK(loaded);
    REQUIRE(session.arrangements().size() == 1);
    const Arrangement& arrangement = session.arrangements().front();
    CHECK(arrangement.audio_asset == audio_asset);
    CHECK(arrangement.audio_duration == TimeDuration{8.0});
    CHECK(
        arrangement.audioTimelineRange() == TimeRange{
                                                .start = TimePosition{},
                                                .end = TimePosition{8.0},
                                            });
}

// Verifies stored arrangement audio defines the session timeline after project load.
TEST_CASE("Session loadSong derives the session timeline", "[core][session]")
{
    Session session;

    REQUIRE(session.loadSong(
        makeSongWithAudio(std::filesystem::path{"full_mix.wav"}, TimeDuration{7.5}), 0));

    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{7.5},
                              });
}

// Verifies a positive audio start offset extends the session timeline past the audio's new end
// while the timeline still begins at the song's first beat.
TEST_CASE("Session timeline extends past offset audio from zero", "[core][session]")
{
    Session session;
    Song song = makeSongWithAudio(std::filesystem::path{"full_mix.wav"}, TimeDuration{7.5});
    song.arrangements.front().audio_asset.start_offset = TimeDuration{2.0};

    REQUIRE(session.loadSong(std::move(song), 0));

    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{9.5},
                              });
}

// Verifies loading a later song replaces the previous aggregate.
TEST_CASE("Session loadSong replaces existing project data", "[core][session]")
{
    Session session;
    const AudioAsset first_audio = makeAudioAsset(std::filesystem::path{"first.wav"});
    const AudioAsset second_audio = makeAudioAsset(std::filesystem::path{"second.wav"});

    REQUIRE(session.loadSong(makeSongWithAudio(first_audio.path, TimeDuration{4.0}), 0));
    REQUIRE(session.loadSong(makeSongWithAudio(second_audio.path, TimeDuration{5.0}), 0));

    REQUIRE(session.arrangements().size() == 1);
    const Arrangement& arrangement = session.arrangements().front();
    CHECK(arrangement.audio_asset == second_audio);
    CHECK(arrangement.audio_duration == TimeDuration{5.0});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{5.0},
                              });
}

// Verifies the requested arrangement index controls the displayed arrangement and timeline.
TEST_CASE("Session loadSong stores the selected arrangement index", "[core][session]")
{
    Session session;
    Song song;
    song.arrangements.push_back(
        Arrangement{
            .id = "lead",
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset = makeAudioAsset(std::filesystem::path{"lead.wav"}),
            .audio_duration = TimeDuration{9.0},
            .tone_document_ref = {},
            .tones = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
        });
    song.arrangements.push_back(
        Arrangement{
            .id = "bass",
            .part = Part::Bass,
            .difficulty = DifficultyRating{},
            .audio_asset = makeAudioAsset(std::filesystem::path{"bass.wav"}),
            .audio_duration = TimeDuration{5.0},
            .tone_document_ref = {},
            .tones = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
        });

    const bool loaded = session.loadSong(std::move(song), 1);
    REQUIRE(loaded);

    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->part == Part::Bass);
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{5.0},
                              });
}

// Verifies every arrangement must have backend-accepted playable duration.
TEST_CASE("Session loadSong rejects arrangement without duration", "[core][session]")
{
    Session session;
    const AudioAsset original_audio = makeAudioAsset(std::filesystem::path{"mix.wav"});

    REQUIRE(session.loadSong(makeSongWithAudio(original_audio.path, TimeDuration{4.0}), 0));

    Song song;
    song.arrangements.push_back(
        Arrangement{
            .id = "lead",
            .part = Part::Lead,
            .difficulty = DifficultyRating{},
            .audio_asset = makeAudioAsset(std::filesystem::path{"lead.wav"}),
            .audio_duration = TimeDuration{6.0},
            .tone_document_ref = {},
            .tones = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
        });
    song.arrangements.push_back(
        Arrangement{
            .id = "bass",
            .part = Part::Bass,
            .difficulty = DifficultyRating{},
            .audio_asset = makeAudioAsset(std::filesystem::path{"bass.wav"}),
            .audio_duration = TimeDuration{},
            .tone_document_ref = {},
            .tones = {},
            .tone_track = {},
            .chart_ref = {},
            .chart = {},
        });

    const bool loaded = session.loadSong(std::move(song), 0);
    CHECK_FALSE(loaded);
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == original_audio);
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{4.0},
                              });
}

// Verifies invalid song replacements leave the existing session intact.
TEST_CASE("Session loadSong rejects invalid replacement data", "[core][session]")
{
    Session session;
    const AudioAsset original_audio = makeAudioAsset(std::filesystem::path{"mix.wav"});

    REQUIRE(session.loadSong(makeSongWithAudio(original_audio.path, TimeDuration{4.0}), 0));

    Song empty_song;
    const bool loaded_empty_song = session.loadSong(std::move(empty_song), 0);
    CHECK_FALSE(loaded_empty_song);
    CHECK_FALSE(session.loadSong(
        makeSongWithAudio(std::filesystem::path{"bad.wav"}, TimeDuration{5.0}), 1));
    REQUIRE(session.arrangements().size() == 1);
    CHECK(session.arrangements().front().audio_asset == original_audio);
    CHECK(session.arrangements().front().audio_duration == TimeDuration{4.0});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{4.0},
                              });
}

} // namespace rock_hero::common::core
