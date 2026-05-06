#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/song.h>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Builds the framework-free asset value used by session audio tests.
[[nodiscard]] AudioAsset makeAudioAsset(std::filesystem::path path)
{
    return AudioAsset{std::move(path)};
}

// Builds a song with one loaded arrangement for session replacement tests.
[[nodiscard]] Song makeSongWithAudio(std::filesystem::path path, TimeDuration duration)
{
    Song song;
    song.metadata.title = "Test Song";
    song.chart.arrangements.push_back(
        Arrangement{
            .part = Part::Lead,
            .audio_asset = makeAudioAsset(std::move(path)),
            .audio_duration = duration,
        });
    return song;
}

} // namespace

// Verifies audio asset references compare by their stored filesystem path.
TEST_CASE("AudioAsset equality compares stored paths", "[core][audio_asset]")
{
    const AudioAsset first{std::filesystem::path{"mix.wav"}};
    const AudioAsset matching{std::filesystem::path{"mix.wav"}};
    const AudioAsset different{std::filesystem::path{"stem.wav"}};

    CHECK(first == matching);
    CHECK_FALSE(first == different);
}

// Verifies a default asset is explicitly empty until a file-like path is assigned.
TEST_CASE("AudioAsset default construction is empty", "[core][audio_asset]")
{
    const AudioAsset audio_asset;

    CHECK(audio_asset.path.empty());
}

// Verifies a new session exposes one editable shell while project-file loading is absent.
TEST_CASE("Session default construction has one empty arrangement", "[core][session]")
{
    const Session session;

    CHECK(session.song().chart.arrangements.size() == 1);
    REQUIRE(session.arrangements().size() == 1);
    CHECK(session.currentArrangement() == &session.arrangements().front());
    CHECK(session.arrangements().front().part == Part::Lead);
    CHECK_FALSE(session.arrangements().front().hasAudio());
    CHECK(session.timeline() == TimeRange{});
}

// Verifies loading a prepared song replaces the temporary empty arrangement shell.
TEST_CASE("Session loadSong replaces the current song", "[core][session]")
{
    Session session;
    const AudioAsset audio_asset = makeAudioAsset(std::filesystem::path{"mix.wav"});
    auto song = makeSongWithAudio(audio_asset.path, TimeDuration{8.0});

    const bool loaded = session.loadSong(std::move(song), 0);

    CHECK(loaded);
    REQUIRE(session.arrangements().size() == 1);
    const Arrangement& arrangement = session.arrangements().front();
    CHECK(arrangement.audio_asset == std::optional{audio_asset});
    CHECK(arrangement.audio_duration == TimeDuration{8.0});
    CHECK(arrangement.hasAudio());
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
    CHECK(arrangement.audio_asset == std::optional{second_audio});
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
    song.chart.arrangements.push_back(
        Arrangement{
            .part = Part::Lead,
            .audio_asset = makeAudioAsset(std::filesystem::path{"lead.wav"}),
            .audio_duration = TimeDuration{9.0},
        });
    song.chart.arrangements.push_back(
        Arrangement{
            .part = Part::Bass,
            .audio_asset = makeAudioAsset(std::filesystem::path{"bass.wav"}),
            .audio_duration = TimeDuration{5.0},
        });

    REQUIRE(session.loadSong(std::move(song), 1));

    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->part == Part::Bass);
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{5.0},
                              });
}

// Verifies the selected arrangement must have backend-accepted playable audio.
TEST_CASE("Session loadSong rejects current arrangement without audio", "[core][session]")
{
    Session session;
    const AudioAsset original_audio = makeAudioAsset(std::filesystem::path{"mix.wav"});

    REQUIRE(session.loadSong(makeSongWithAudio(original_audio.path, TimeDuration{4.0}), 0));

    Song song;
    song.chart.arrangements.push_back(
        Arrangement{
            .part = Part::Lead,
            .audio_asset = makeAudioAsset(std::filesystem::path{"lead.wav"}),
            .audio_duration = TimeDuration{6.0},
        });
    song.chart.arrangements.push_back(
        Arrangement{
            .part = Part::Bass,
            .audio_asset = makeAudioAsset(std::filesystem::path{"bass.wav"}),
        });

    CHECK_FALSE(session.loadSong(std::move(song), 1));
    REQUIRE(session.currentArrangement() != nullptr);
    CHECK(session.currentArrangement()->audio_asset == std::optional{original_audio});
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
    CHECK_FALSE(session.loadSong(std::move(empty_song), 0));
    CHECK_FALSE(session.loadSong(
        makeSongWithAudio(std::filesystem::path{"bad.wav"}, TimeDuration{5.0}), 1));
    REQUIRE(session.arrangements().size() == 1);
    CHECK(session.arrangements().front().audio_asset == std::optional{original_audio});
    CHECK(session.arrangements().front().audio_duration == TimeDuration{4.0});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{4.0},
                              });
}

} // namespace rock_hero::core
