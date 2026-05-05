#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <rock_hero/core/track.h>
#include <rock_hero/core/track_audio.h>
#include <string>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Bundles a fixture path with the full asset duration Session stores as track audio.
TrackAudio makeTrackAudio(std::filesystem::path path, TimeDuration duration = {})
{
    return TrackAudio{
        .asset = AudioAsset{std::move(path)},
        .duration = duration,
    };
}

// Builds the identity-free track spec that Session commits with an allocated id.
[[nodiscard]] TrackSpec makeTrackSpec(std::string name = {})
{
    return TrackSpec{
        .name = std::move(name),
    };
}

// Creates a session-only track for focused core tests without involving the audio adapter.
TrackId addTestTrack(Session& session, std::string name = {})
{
    const TrackId track_id = session.allocateTrackId();
    const bool track_added = session.addTrack(track_id, makeTrackSpec(std::move(name)));
    REQUIRE(track_added);
    return track_id;
}

// Creates a track through Session and stores full-source audio on it.
TrackId addTrackWithAudio(
    Session& session, std::string name, std::filesystem::path path, TimeDuration duration = {})
{
    const TrackId track_id = addTestTrack(session, std::move(name));
    const bool audio_set =
        session.setTrackAudio(track_id, makeTrackAudio(std::move(path), duration));
    REQUIRE(audio_set);
    return track_id;
}

// Reads track audio by value so tests do not chain through nullable lookup results.
[[nodiscard]] std::optional<TrackAudio> findTrackAudio(const Session& session, TrackId id)
{
    const Track* const track = session.findTrack(id);
    if (track == nullptr)
    {
        return std::nullopt;
    }

    return track->audio;
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

// Verifies full-source track audio covers the timeline from zero through its duration.
TEST_CASE("TrackAudio timelineRange uses full asset duration", "[core][track-audio]")
{
    const TrackAudio audio = makeTrackAudio(std::filesystem::path{"mix.wav"}, TimeDuration{12.0});

    CHECK(
        audio.timelineRange() == TimeRange{
                                     .start = TimePosition{},
                                     .end = TimePosition{12.0},
                                 });
}

// Verifies the role-free track aggregate has an empty default state.
TEST_CASE("Track default construction is empty", "[core][track]")
{
    const Track track;

    CHECK_FALSE(track.id.isValid());
    CHECK(track.name.empty());
    CHECK_FALSE(track.audio.has_value());
}

// Verifies a new session starts without any editor tracks.
TEST_CASE("Session default construction has no tracks", "[core][session]")
{
    const Session session;

    CHECK(session.tracks().empty());
    CHECK(session.timeline() == TimeRange{});
}

// Verifies allocated ids can be committed as stable track identities.
TEST_CASE("Adding allocated tracks stores stable nonzero TrackIds", "[core][session]")
{
    Session session;

    const auto first_id = addTestTrack(session, "Full Mix");
    const auto second_id = addTestTrack(session, "Stem");

    CHECK(first_id.isValid());
    CHECK(second_id.isValid());
    CHECK(first_id != second_id);
    CHECK(session.findTrack(first_id) != nullptr);
    CHECK(session.findTrack(second_id) != nullptr);
}

// Verifies track ids can be allocated before a cross-boundary backend create.
TEST_CASE("Session allocates stable nonzero TrackIds", "[core][session]")
{
    Session session;

    const auto first_id = session.allocateTrackId();
    const auto second_id = session.allocateTrackId();

    CHECK(first_id.isValid());
    CHECK(second_id.isValid());
    CHECK(first_id != second_id);
}

// Verifies that addTrack stores an allocated identity and name while leaving audio unset.
TEST_CASE("Adding a named allocated track stores identity and name", "[core][session]")
{
    Session session;

    const auto track_id = addTestTrack(session, "Full Mix");

    REQUIRE(session.tracks().size() == 1);
    const auto& track = session.tracks()[0];
    CHECK(track_id.isValid());
    CHECK(track.id == track_id);
    CHECK(track.name == "Full Mix");
    CHECK_FALSE(track.audio.has_value());
}

// Verifies an allocated track id can be committed after allocation.
TEST_CASE("Adding an allocated track stores the supplied id", "[core][session]")
{
    Session session;
    const auto track_id = session.allocateTrackId();

    const bool track_added = session.addTrack(track_id, makeTrackSpec("Full Mix"));

    CHECK(track_added);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].id == track_id);
    CHECK(session.tracks()[0].name == "Full Mix");
}

// Verifies addTrack is a commit operation only; allocateTrackId owns id generation.
TEST_CASE("Adding a supplied track id does not advance allocation", "[core][session]")
{
    Session session;

    const bool track_added = session.addTrack(TrackId{7}, makeTrackSpec("Imported"));
    const TrackId allocated_id = session.allocateTrackId();

    CHECK(track_added);
    CHECK(allocated_id == TrackId{1});
}

// Verifies explicit track commits reject invalid and duplicate ids.
TEST_CASE("Adding an allocated track rejects invalid or duplicate ids", "[core][session]")
{
    Session session;
    const auto track_id = session.allocateTrackId();

    REQUIRE(session.addTrack(track_id, makeTrackSpec("Full Mix")));

    CHECK_FALSE(session.addTrack(TrackId{}, makeTrackSpec("Invalid")));
    CHECK_FALSE(session.addTrack(track_id, makeTrackSpec("Duplicate")));
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
}

// Verifies that a row can exist before the user has assigned any audio file.
TEST_CASE("Adding an empty track is valid", "[core][session]")
{
    Session session;

    const auto track_id = addTestTrack(session);
    const auto* track = session.findTrack(track_id);

    CHECK(track_id.isValid());
    REQUIRE(track != nullptr);
    CHECK(track->id == track_id);
    CHECK(track->name.empty());
    CHECK_FALSE(track->audio.has_value());
}

// Verifies that session row projections can preserve the order chosen by the editor.
TEST_CASE("Tracks preserve insertion order", "[core][session]")
{
    Session session;

    addTestTrack(session, "Drums");
    addTestTrack(session, "Bass");
    addTestTrack(session, "Guitar");

    REQUIRE(session.tracks().size() == 3);
    CHECK(session.tracks()[0].name == "Drums");
    CHECK(session.tracks()[1].name == "Bass");
    CHECK(session.tracks()[2].name == "Guitar");
}

// Verifies lookup failure is observable without mutating the session.
TEST_CASE("findTrack returns nullptr for missing tracks", "[core][session]")
{
    Session session;
    addTestTrack(session, "Full Mix");

    CHECK(session.findTrack(TrackId{999}) == nullptr);
}

// Verifies session projections can look up tracks without receiving mutable storage.
TEST_CASE("findTrack returns existing and missing tracks", "[core][session]")
{
    Session session;
    const auto track_id =
        addTrackWithAudio(session, "Full Mix", std::filesystem::path{"mix.wav"}, TimeDuration{6.0});

    const auto* found_track = session.findTrack(track_id);

    REQUIRE(found_track != nullptr);
    CHECK(found_track->id == track_id);
    CHECK(found_track->name == "Full Mix");
    CHECK(
        found_track->audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"mix.wav"}, TimeDuration{6.0})});
    CHECK(session.findTrack(TrackId{999}) == nullptr);
}

// Verifies user-visible names are updated through the explicit Session mutation boundary.
TEST_CASE("Renaming a track updates only that track", "[core][session]")
{
    Session session;
    const auto first_id =
        addTrackWithAudio(session, "Full Mix", std::filesystem::path{"old.wav"}, TimeDuration{4.0});
    addTrackWithAudio(session, "Solo", std::filesystem::path{"solo.wav"}, TimeDuration{5.0});

    const auto renamed = session.renameTrack(first_id, "Edited Mix");

    CHECK(renamed);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].name == "Edited Mix");
    CHECK(session.tracks()[1].name == "Solo");
    CHECK(
        session.tracks()[0].audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"old.wav"}, TimeDuration{4.0})});
    CHECK(
        session.tracks()[1].audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"solo.wav"}, TimeDuration{5.0})});
}

// Verifies failed rename commands leave existing track specs untouched.
TEST_CASE("Renaming a missing track fails cleanly", "[core][session]")
{
    Session session;
    addTrackWithAudio(session, "Full Mix", std::filesystem::path{"mix.wav"}, TimeDuration{6.0});

    const auto renamed = session.renameTrack(TrackId{999}, "Edited Mix");

    CHECK_FALSE(renamed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK(
        session.tracks()[0].audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"mix.wav"}, TimeDuration{6.0})});
}

// Verifies setting one track audio does not disturb neighboring track specs.
TEST_CASE("Setting track audio updates only that track", "[core][session]")
{
    Session session;
    const auto first_id = addTrackWithAudio(session, "Full Mix", std::filesystem::path{"old.wav"});
    addTrackWithAudio(session, "Solo", std::filesystem::path{"solo.wav"}, TimeDuration{3.0});
    const TrackAudio replacement_audio =
        makeTrackAudio(std::filesystem::path{"new.wav"}, TimeDuration{12.0});

    const auto audio_set = session.setTrackAudio(first_id, replacement_audio);

    CHECK(audio_set);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].audio == std::optional{replacement_audio});
    CHECK(
        session.tracks()[1].audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"solo.wav"}, TimeDuration{3.0})});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{12.0},
                              });
}

// Verifies assigning audio to the same track replaces the previous full-source asset.
TEST_CASE("Setting track audio replaces existing audio", "[core][session]")
{
    Session session;
    const auto track_id = addTestTrack(session, "Full Mix");
    const TrackAudio first_audio =
        makeTrackAudio(std::filesystem::path{"first.wav"}, TimeDuration{4.0});
    const TrackAudio second_audio =
        makeTrackAudio(std::filesystem::path{"second.wav"}, TimeDuration{5.0});

    REQUIRE(session.setTrackAudio(track_id, first_audio));
    CHECK(findTrackAudio(session, track_id) == std::optional{first_audio});

    REQUIRE(session.setTrackAudio(track_id, second_audio));
    CHECK(findTrackAudio(session, track_id) == std::optional{second_audio});
}

// Verifies the common editor flow where an empty row receives its first audio file.
TEST_CASE("Setting empty track audio stores the audio", "[core][session]")
{
    Session session;
    const auto track_id = addTestTrack(session, "Full Mix");
    const TrackAudio audio = makeTrackAudio(std::filesystem::path{"mix.wav"}, TimeDuration{8.0});

    const auto audio_set = session.setTrackAudio(track_id, audio);

    CHECK(audio_set);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio == std::optional{audio});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{8.0},
                              });
}

// Verifies stored track audio defines the aggregate project timeline.
TEST_CASE("Setting track audio expands the session timeline", "[core][session]")
{
    Session session;
    addTrackWithAudio(session, "Intro", std::filesystem::path{"intro.wav"}, TimeDuration{3.0});
    addTrackWithAudio(session, "Outro", std::filesystem::path{"outro.wav"}, TimeDuration{7.5});

    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{7.5},
                              });
}

// Verifies missing-track updates remain recoverable and do not change the project timeline.
TEST_CASE("Setting missing track audio fails cleanly", "[core][session]")
{
    Session session;
    const auto existing_id =
        addTrackWithAudio(session, "Full Mix", std::filesystem::path{"mix.wav"}, TimeDuration{4.0});
    const TrackAudio missing_audio =
        makeTrackAudio(std::filesystem::path{"missing.wav"}, TimeDuration{10.0});

    const auto audio_set = session.setTrackAudio(TrackId{999}, missing_audio);

    CHECK_FALSE(audio_set);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(session.tracks().size() == 1);
    CHECK(
        session.tracks()[0].audio ==
        std::optional{makeTrackAudio(std::filesystem::path{"mix.wav"}, TimeDuration{4.0})});
    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{},
                                  .end = TimePosition{4.0},
                              });
}

} // namespace rock_hero::core
