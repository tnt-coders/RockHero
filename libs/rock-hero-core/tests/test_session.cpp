#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>

namespace rock_hero::core
{

// Verifies that new tracks receive stable nonzero identities.
TEST_CASE("Adding a track creates a stable nonzero TrackId", "[core][session]")
{
    Session session;

    const auto first_id = session.addTrack("Full Mix");
    const auto second_id = session.addTrack("Stem");

    CHECK(first_id.isValid());
    CHECK(second_id.isValid());
    CHECK(first_id != second_id);
    CHECK(session.findTrack(first_id) != nullptr);
    CHECK(session.findTrack(second_id) != nullptr);
}

// Verifies that a row can exist before the user has assigned any audio file.
TEST_CASE("Adding an empty track is valid", "[core][session]")
{
    Session session;

    const auto track_id = session.addTrack();
    const auto* track = session.findTrack(track_id);

    CHECK(track_id.isValid());
    REQUIRE(track != nullptr);
    CHECK(track->id == track_id);
    CHECK(track->name.empty());
    CHECK_FALSE(track->audio_asset.has_value());
}

// Verifies that session row projections can preserve the order chosen by the editor.
TEST_CASE("Tracks preserve insertion order", "[core][session]")
{
    Session session;

    session.addTrack("Drums");
    session.addTrack("Bass");
    session.addTrack("Guitar");

    REQUIRE(session.tracks().size() == 3);
    CHECK(session.tracks()[0].name == "Drums");
    CHECK(session.tracks()[1].name == "Bass");
    CHECK(session.tracks()[2].name == "Guitar");
}

// Verifies replacing one track asset does not disturb neighboring track data.
TEST_CASE("Replacing a track asset updates only that track", "[core][session]")
{
    Session session;
    const auto first_id =
        session.addTrack("Full Mix", AudioAsset{std::filesystem::path{"old.wav"}});
    session.addTrack("Solo", AudioAsset{std::filesystem::path{"solo.wav"}});

    const auto replaced =
        session.replaceTrackAsset(first_id, AudioAsset{std::filesystem::path{"new.wav"}});

    CHECK(replaced);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"new.wav"}});
    CHECK(session.tracks()[1].audio_asset == AudioAsset{std::filesystem::path{"solo.wav"}});
}

// Verifies missing-track replacement remains recoverable for controller error handling.
TEST_CASE("Replacing a missing track asset fails cleanly", "[core][session]")
{
    Session session;
    const auto existing_id =
        session.addTrack("Full Mix", AudioAsset{std::filesystem::path{"mix.wav"}});

    const auto replaced =
        session.replaceTrackAsset(TrackId{999}, AudioAsset{std::filesystem::path{"missing.wav"}});

    CHECK_FALSE(replaced);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
}

} // namespace rock_hero::core
