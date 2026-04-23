#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>

// Verifies that new tracks receive stable nonzero identities.
TEST_CASE("Adding a track creates a stable nonzero TrackId", "[session]")
{
    rock_hero::core::Session session;

    const auto first_id = session.addTrack("Full Mix");
    const auto second_id = session.addTrack("Stem");

    REQUIRE(first_id.isValid());
    REQUIRE(second_id.isValid());
    REQUIRE(first_id != second_id);
    REQUIRE(session.findTrack(first_id) != nullptr);
    REQUIRE(session.findTrack(second_id) != nullptr);
}

// Verifies that a row can exist before the user has assigned any audio file.
TEST_CASE("Adding an empty track is valid", "[session]")
{
    rock_hero::core::Session session;

    const auto track_id = session.addTrack();
    const auto* track = session.findTrack(track_id);

    REQUIRE(track_id.isValid());
    REQUIRE(track != nullptr);
    REQUIRE(track->id == track_id);
    REQUIRE(track->name.empty());
    REQUIRE_FALSE(track->audio_asset.has_value());
}

// Verifies that session row projections can preserve the order chosen by the editor.
TEST_CASE("Tracks preserve insertion order", "[session]")
{
    rock_hero::core::Session session;

    session.addTrack("Drums");
    session.addTrack("Bass");
    session.addTrack("Guitar");

    REQUIRE(session.tracks().size() == 3);
    REQUIRE(session.tracks()[0].name == "Drums");
    REQUIRE(session.tracks()[1].name == "Bass");
    REQUIRE(session.tracks()[2].name == "Guitar");
}

// Verifies replacing one track asset does not disturb neighboring track data.
TEST_CASE("Replacing a track asset updates only that track", "[session]")
{
    rock_hero::core::Session session;
    const auto first_id =
        session.addTrack("Full Mix", rock_hero::core::AudioAsset{std::filesystem::path{"old.wav"}});
    session.addTrack("Solo", rock_hero::core::AudioAsset{std::filesystem::path{"solo.wav"}});

    const auto replaced = session.replaceTrackAsset(
        first_id, rock_hero::core::AudioAsset{std::filesystem::path{"new.wav"}});

    REQUIRE(replaced);
    REQUIRE(
        session.tracks()[0].audio_asset ==
        rock_hero::core::AudioAsset{std::filesystem::path{"new.wav"}});
    REQUIRE(
        session.tracks()[1].audio_asset ==
        rock_hero::core::AudioAsset{std::filesystem::path{"solo.wav"}});
}

// Verifies missing-track replacement remains recoverable for controller error handling.
TEST_CASE("Replacing a missing track asset fails cleanly", "[session]")
{
    rock_hero::core::Session session;
    const auto existing_id =
        session.addTrack("Full Mix", rock_hero::core::AudioAsset{std::filesystem::path{"mix.wav"}});

    const auto replaced = session.replaceTrackAsset(
        rock_hero::core::TrackId{999},
        rock_hero::core::AudioAsset{std::filesystem::path{"missing.wav"}});

    REQUIRE_FALSE(replaced);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(
        session.tracks()[0].audio_asset ==
        rock_hero::core::AudioAsset{std::filesystem::path{"mix.wav"}});
}
