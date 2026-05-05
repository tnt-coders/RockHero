#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>

namespace rock_hero::core
{

namespace
{

// Builds the framework-free asset value used by session audio tests.
[[nodiscard]] AudioAsset makeAudioAsset(std::filesystem::path path)
{
    return AudioAsset{std::move(path)};
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

    REQUIRE(session.arrangements().size() == 1);
    CHECK(session.currentArrangement() == &session.arrangements().front());
    CHECK(session.arrangements().front().part == Part::Lead);
    CHECK_FALSE(session.arrangements().front().hasAudio());
    CHECK(session.timeline() == TimeRange{});
}

// Verifies accepted audio is stored on the displayed arrangement.
TEST_CASE("Setting current arrangement audio stores asset and duration", "[core][session]")
{
    Session session;
    const AudioAsset audio_asset = makeAudioAsset(std::filesystem::path{"mix.wav"});

    const bool audio_set = session.setCurrentArrangementAudio(audio_asset, TimeDuration{8.0});

    CHECK(audio_set);
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

// Verifies stored arrangement audio defines the session timeline.
TEST_CASE("Setting current arrangement audio expands the session timeline", "[core][session]")
{
    Session session;

    REQUIRE(session.setCurrentArrangementAudio(
        makeAudioAsset(std::filesystem::path{"full_mix.wav"}), TimeDuration{7.5}));

    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{7.5},
                              });
}

// Verifies assigning audio again replaces the previous asset and duration.
TEST_CASE("Setting current arrangement audio replaces existing audio", "[core][session]")
{
    Session session;
    const AudioAsset first_audio = makeAudioAsset(std::filesystem::path{"first.wav"});
    const AudioAsset second_audio = makeAudioAsset(std::filesystem::path{"second.wav"});

    REQUIRE(session.setCurrentArrangementAudio(first_audio, TimeDuration{4.0}));
    REQUIRE(session.setCurrentArrangementAudio(second_audio, TimeDuration{5.0}));

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

// Verifies obviously invalid accepted-audio values do not enter the session.
TEST_CASE("Setting invalid arrangement audio fails cleanly", "[core][session]")
{
    Session session;
    const AudioAsset original_audio = makeAudioAsset(std::filesystem::path{"mix.wav"});

    REQUIRE(session.setCurrentArrangementAudio(original_audio, TimeDuration{4.0}));

    CHECK_FALSE(session.setCurrentArrangementAudio(AudioAsset{}, TimeDuration{5.0}));
    CHECK_FALSE(session.setCurrentArrangementAudio(
        makeAudioAsset(std::filesystem::path{"bad.wav"}), TimeDuration{}));
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
