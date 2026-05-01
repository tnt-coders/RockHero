#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <string>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Creates a track through Session and commits the asset through the backend-accepted commit path.
TrackId addCommittedTrack(
    Session& session, std::string name, std::filesystem::path path, TimeRange timeline_range = {})
{
    const TrackId track_id = session.addTrack(std::move(name));
    const bool committed =
        session.commitTrackAudioAsset(track_id, AudioAsset{std::move(path)}, timeline_range);
    REQUIRE(committed);
    return track_id;
}

} // namespace

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
    CHECK(session.timeline() == TimeRange{});
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

// Verifies that addTrack stores identity and name while leaving audio uncommitted.
TEST_CASE("Adding a named track stores identity and name", "[core][session]")
{
    Session session;

    const auto track_id = session.addTrack("Full Mix");

    REQUIRE(session.tracks().size() == 1);
    const auto& track = session.tracks()[0];
    CHECK(track_id.isValid());
    CHECK(track.id == track_id);
    CHECK(track.name == "Full Mix");
    CHECK_FALSE(track.audio_asset.has_value());
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

// Verifies session projections can look up tracks without receiving mutable storage.
TEST_CASE("findTrack returns existing and missing tracks", "[core][session]")
{
    Session session;
    const auto track_id = addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});

    const auto* found_track = session.findTrack(track_id);

    REQUIRE(found_track != nullptr);
    CHECK(found_track->id == track_id);
    CHECK(found_track->name == "Full Mix");
    CHECK(found_track->audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
    CHECK(session.findTrack(TrackId{999}) == nullptr);
}

// Verifies user-visible names are updated through the explicit Session mutation boundary.
TEST_CASE("Renaming a track updates only that track", "[core][session]")
{
    Session session;
    const auto first_id = addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});
    addCommittedTrack(session, "Solo", std::filesystem::path{"solo.wav"});

    const auto renamed = session.renameTrack(first_id, "Edited Mix");

    CHECK(renamed);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].name == "Edited Mix");
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
    CHECK(session.tracks()[1].name == "Solo");
    CHECK(session.tracks()[1].audio_asset == AudioAsset{std::filesystem::path{"solo.wav"}});
}

// Verifies failed rename commands leave existing track data untouched.
TEST_CASE("Renaming a missing track fails cleanly", "[core][session]")
{
    Session session;
    addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});

    const auto renamed = session.renameTrack(TrackId{999}, "Edited Mix");

    CHECK_FALSE(renamed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
}

// Verifies committing one track asset does not disturb neighboring track data.
TEST_CASE("Committing a track asset updates only that track", "[core][session]")
{
    Session session;
    const auto first_id = addCommittedTrack(session, "Full Mix", std::filesystem::path{"old.wav"});
    addCommittedTrack(session, "Solo", std::filesystem::path{"solo.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{12.0},
    };

    const auto committed = session.commitTrackAudioAsset(
        first_id, AudioAsset{std::filesystem::path{"new.wav"}}, timeline_range);

    CHECK(committed);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"new.wav"}});
    CHECK(session.tracks()[1].audio_asset == AudioAsset{std::filesystem::path{"solo.wav"}});
    CHECK(session.timeline() == timeline_range);
}

// Verifies the common editor flow where an empty row receives its first committed asset.
TEST_CASE("Committing an empty track asset stores the asset", "[core][session]")
{
    Session session;
    const auto track_id = session.addTrack("Full Mix");
    const AudioAsset audio_asset{std::filesystem::path{"mix.wav"}};
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{8.0},
    };

    const auto committed = session.commitTrackAudioAsset(track_id, audio_asset, timeline_range);

    CHECK(committed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio_asset == audio_asset);
    CHECK(session.timeline() == timeline_range);
}

// Verifies missing-track commits remain recoverable and do not change the project timeline.
TEST_CASE("Committing a missing track asset fails cleanly", "[core][session]")
{
    Session session;
    const auto existing_id =
        addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{10.0},
    };

    const auto committed = session.commitTrackAudioAsset(
        TrackId{999}, AudioAsset{std::filesystem::path{"missing.wav"}}, timeline_range);

    CHECK_FALSE(committed);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio_asset == AudioAsset{std::filesystem::path{"mix.wav"}});
    CHECK(session.timeline() == TimeRange{});
}

} // namespace rock_hero::core
