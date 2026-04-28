#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>

namespace rock_hero::core
{

// Verifies TrackId's invalid default and explicit valid value contract.
TEST_CASE("TrackId default and explicit construction", "[core][track]")
{
    const TrackId default_id;
    const TrackId explicit_id{42};

    CHECK_FALSE(default_id.isValid());
    CHECK(default_id.value == 0);
    CHECK(explicit_id.isValid());
    CHECK(explicit_id.value == 42);
    CHECK(explicit_id == TrackId{42});
    CHECK_FALSE(explicit_id == TrackId{43});
}

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

// Verifies the role-free track aggregate has an empty default state.
TEST_CASE("Track default construction is empty", "[core][track]")
{
    const Track track;

    CHECK_FALSE(track.id.isValid());
    CHECK(track.name.empty());
    CHECK_FALSE(track.audio_asset.has_value());
}

// Verifies a new session starts without any editor tracks.
TEST_CASE("Session default construction has no tracks", "[core][session]")
{
    const Session session;

    CHECK(session.tracks().empty());
}

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

// Verifies that addTrack stores the complete initial track payload.
TEST_CASE("Adding a populated track stores all fields", "[core][session]")
{
    Session session;
    const AudioAsset audio_asset{std::filesystem::path{"full_mix.wav"}};

    const auto track_id = session.addTrack("Full Mix", audio_asset);

    REQUIRE(session.tracks().size() == 1);
    const auto& track = session.tracks()[0];
    CHECK(track_id.isValid());
    CHECK(track.id == track_id);
    CHECK(track.name == "Full Mix");
    CHECK(track.audio_asset == audio_asset);
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

// Verifies lookup failure is observable without mutating the session.
TEST_CASE("findTrack returns nullptr for missing tracks", "[core][session]")
{
    Session session;
    session.addTrack("Full Mix");

    CHECK(session.findTrack(TrackId{999}) == nullptr);
}

// Verifies read-only session projections can use the const lookup overload.
TEST_CASE("const findTrack returns existing and missing tracks", "[core][session]")
{
    Session session;
    const auto track_id =
        session.addTrack("Full Mix", AudioAsset{std::filesystem::path{"mix.wav"}});
    const Session& const_session = session;

    const auto* found_track = const_session.findTrack(track_id);

    REQUIRE(found_track != nullptr);
    CHECK(found_track->id == track_id);
    CHECK(found_track->name == "Full Mix");
    CHECK(found_track->audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
    CHECK(const_session.findTrack(TrackId{999}) == nullptr);
}

// Verifies the mutable lookup overload returns the stored track object.
TEST_CASE("mutable findTrack returns editable session storage", "[core][session]")
{
    Session session;
    const auto track_id = session.addTrack("Full Mix");

    auto* track = session.findTrack(track_id);

    REQUIRE(track != nullptr);
    track->name = "Edited Mix";
    track->audio_asset = AudioAsset{std::filesystem::path{"edited.wav"}};

    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Edited Mix");
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"edited.wav"}});
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

// Verifies the common editor flow where an empty row receives its first asset.
TEST_CASE("Replacing an empty track asset stores the asset", "[core][session]")
{
    Session session;
    const auto track_id = session.addTrack("Full Mix");
    const AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};

    const auto replaced = session.replaceTrackAsset(track_id, audio_asset);

    CHECK(replaced);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio_asset == audio_asset);
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
