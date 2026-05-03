#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/core/session.h>
#include <string>
#include <utility>

namespace rock_hero::core
{

namespace
{

// Bundles a fixture path with the source range and timeline position Session stores as one clip.
AudioClip makeAudioClip(
    std::filesystem::path path, TimeRange source_range = {}, TimePosition position = {})
{
    return AudioClip{
        .id = AudioClipId{},
        .asset = AudioAsset{std::move(path)},
        .asset_duration = TimeDuration{source_range.end.seconds},
        .source_range = source_range,
        .position = position,
    };
}

// Builds the identity-free track payload that Session commits with an allocated id.
[[nodiscard]] TrackData makeTrackData(std::string name = {})
{
    return TrackData{
        .name = std::move(name),
    };
}

// Drops Session identity from a stored clip so tests can exercise the payload commit path.
[[nodiscard]] AudioClipData makeAudioClipData(const AudioClip& audio_clip)
{
    return AudioClipData{
        .asset = audio_clip.asset,
        .asset_duration = audio_clip.asset_duration,
        .source_range = audio_clip.source_range,
        .position = audio_clip.position,
    };
}

// Returns a copy with an explicit id so tests can compare stored clips.
[[nodiscard]] AudioClip withClipId(AudioClip audio_clip, AudioClipId id)
{
    audio_clip.id = id;
    return audio_clip;
}

// Allocates the next session-owned clip id and attaches it to a clip fixture.
[[nodiscard]] AudioClip withAllocatedClipId(Session& session, AudioClip audio_clip)
{
    audio_clip.id = session.allocateAudioClipId();
    return audio_clip;
}

// Commits a stored clip fixture through Session's identity-attaching API.
bool setTestAudioClip(Session& session, TrackId track_id, const AudioClip& audio_clip)
{
    return session.setAudioClip(track_id, audio_clip.id, makeAudioClipData(audio_clip));
}

// Creates a session-only track for focused core tests without involving the audio adapter.
TrackId addTestTrack(Session& session, std::string name = {})
{
    const TrackId track_id = session.allocateTrackId();
    const bool track_added = session.addTrack(track_id, makeTrackData(std::move(name)));
    REQUIRE(track_added);
    return track_id;
}

// Creates a track through Session and stores one clip on it.
TrackId addTrackWithAudioClip(
    Session& session, std::string name, std::filesystem::path path, TimeRange source_range = {},
    TimePosition position = {})
{
    const TrackId track_id = addTestTrack(session, std::move(name));
    const AudioClip audio_clip =
        withAllocatedClipId(session, makeAudioClip(std::move(path), source_range, position));
    const bool clip_set = setTestAudioClip(session, track_id, audio_clip);
    REQUIRE(clip_set);
    return track_id;
}

// Reads a track clip by value so tests do not chain through nullable lookup results.
[[nodiscard]] std::optional<AudioClip> findTrackAudioClip(const Session& session, TrackId id)
{
    const Track* const track = session.findTrack(id);
    if (track == nullptr)
    {
        return std::nullopt;
    }

    return track->audio_clip;
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

// Verifies AudioClipId's invalid default and explicit valid value contract.
TEST_CASE("AudioClipId default and explicit construction", "[core][track]")
{
    const AudioClipId default_id;
    const AudioClipId explicit_id{24};

    CHECK_FALSE(default_id.isValid());
    CHECK(default_id.value == 0);
    CHECK(explicit_id.isValid());
    CHECK(explicit_id.value == 24);
    CHECK(explicit_id == AudioClipId{24});
    CHECK_FALSE(explicit_id == AudioClipId{25});
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

// Verifies clip placement derives timeline range from position and selected source duration.
TEST_CASE("AudioClip timelineRange uses position and source duration", "[core][track]")
{
    const AudioClip clip{
        .id = AudioClipId{},
        .asset = AudioAsset{std::filesystem::path{"mix.wav"}},
        .asset_duration = TimeDuration{12.0},
        .source_range =
            TimeRange{
                .start = TimePosition{2.0},
                .end = TimePosition{5.0},
            },
        .position = TimePosition{7.0},
    };

    CHECK(
        clip.timelineRange() == TimeRange{
                                    .start = TimePosition{7.0},
                                    .end = TimePosition{10.0},
                                });
}

// Verifies identity-free clip data can describe timeline placement before identity exists.
TEST_CASE("AudioClipData timelineRange uses position and source duration", "[core][track]")
{
    const AudioClipData clip{
        .asset = AudioAsset{std::filesystem::path{"mix.wav"}},
        .asset_duration = TimeDuration{12.0},
        .source_range =
            TimeRange{
                .start = TimePosition{2.0},
                .end = TimePosition{5.0},
            },
        .position = TimePosition{7.0},
    };

    CHECK(
        clip.timelineRange() == TimeRange{
                                    .start = TimePosition{7.0},
                                    .end = TimePosition{10.0},
                                });
}

// Verifies the role-free track aggregate has an empty default state.
TEST_CASE("Track default construction is empty", "[core][track]")
{
    const Track track;

    CHECK_FALSE(track.id.isValid());
    CHECK(track.name.empty());
    CHECK_FALSE(track.audio_clip.has_value());
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
    CHECK_FALSE(track.audio_clip.has_value());
}

// Verifies an allocated track id can be committed after allocation.
TEST_CASE("Adding an allocated track stores the supplied id", "[core][session]")
{
    Session session;
    const auto track_id = session.allocateTrackId();

    const bool track_added = session.addTrack(track_id, makeTrackData("Full Mix"));

    CHECK(track_added);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].id == track_id);
    CHECK(session.tracks()[0].name == "Full Mix");
}

// Verifies addTrack is a commit operation only; allocateTrackId owns id generation.
TEST_CASE("Adding a supplied track id does not advance allocation", "[core][session]")
{
    Session session;

    const bool track_added = session.addTrack(TrackId{7}, makeTrackData("Imported"));
    const TrackId allocated_id = session.allocateTrackId();

    CHECK(track_added);
    CHECK(allocated_id == TrackId{1});
}

// Verifies explicit track commits reject invalid and duplicate ids.
TEST_CASE("Adding an allocated track rejects invalid or duplicate ids", "[core][session]")
{
    Session session;
    const auto track_id = session.allocateTrackId();

    REQUIRE(session.addTrack(track_id, makeTrackData("Full Mix")));

    CHECK_FALSE(session.addTrack(TrackId{}, makeTrackData("Invalid")));
    CHECK_FALSE(session.addTrack(track_id, makeTrackData("Duplicate")));
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
}

// Verifies clip ids are allocated explicitly before backend/session commit.
TEST_CASE("Session allocates stable nonzero AudioClipIds", "[core][session]")
{
    Session session;

    const auto first_id = session.allocateAudioClipId();
    const auto second_id = session.allocateAudioClipId();

    CHECK(first_id.isValid());
    CHECK(second_id.isValid());
    CHECK(first_id != second_id);
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
    CHECK_FALSE(track->audio_clip.has_value());
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
        addTrackWithAudioClip(session, "Full Mix", std::filesystem::path{"mix.wav"});

    const auto* found_track = session.findTrack(track_id);

    REQUIRE(found_track != nullptr);
    CHECK(found_track->id == track_id);
    CHECK(found_track->name == "Full Mix");
    CHECK(
        found_track->audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
    CHECK(session.findTrack(TrackId{999}) == nullptr);
}

// Verifies user-visible names are updated through the explicit Session mutation boundary.
TEST_CASE("Renaming a track updates only that track", "[core][session]")
{
    Session session;
    const auto first_id =
        addTrackWithAudioClip(session, "Full Mix", std::filesystem::path{"mix.wav"});
    addTrackWithAudioClip(session, "Solo", std::filesystem::path{"solo.wav"});

    const auto renamed = session.renameTrack(first_id, "Edited Mix");

    CHECK(renamed);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].name == "Edited Mix");
    CHECK(session.tracks()[1].name == "Solo");
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
    CHECK(
        session.tracks()[1].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"solo.wav"}), AudioClipId{2})});
}

// Verifies failed rename commands leave existing track data untouched.
TEST_CASE("Renaming a missing track fails cleanly", "[core][session]")
{
    Session session;
    addTrackWithAudioClip(session, "Full Mix", std::filesystem::path{"mix.wav"});

    const auto renamed = session.renameTrack(TrackId{999}, "Edited Mix");

    CHECK_FALSE(renamed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
}

// Verifies setting one track clip does not disturb neighboring track data.
TEST_CASE("Setting a track clip updates only that track", "[core][session]")
{
    Session session;
    const auto first_id =
        addTrackWithAudioClip(session, "Full Mix", std::filesystem::path{"old.wav"});
    addTrackWithAudioClip(session, "Solo", std::filesystem::path{"solo.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{12.0},
    };
    const AudioClip replacement_clip = withAllocatedClipId(
        session, makeAudioClip(std::filesystem::path{"new.wav"}, timeline_range));

    const auto clip_set = setTestAudioClip(session, first_id, replacement_clip);

    CHECK(clip_set);
    REQUIRE(session.tracks().size() == 2);
    CHECK(session.tracks()[0].audio_clip == std::optional<AudioClip>{replacement_clip});
    CHECK(
        session.tracks()[1].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"solo.wav"}), AudioClipId{2})});
    CHECK(session.timeline() == timeline_range);
}

// Verifies Session stores the clip id allocated by the caller for the edit transaction.
TEST_CASE("Setting track clips stores allocated clip ids", "[core][session]")
{
    Session session;
    const auto track_id = addTestTrack(session, "Full Mix");
    const auto first_clip = withAllocatedClipId(
        session,
        makeAudioClip(
            std::filesystem::path{"first.wav"},
            TimeRange{
                .start = TimePosition{},
                .end = TimePosition{4.0},
            }));
    const auto second_clip = withAllocatedClipId(
        session,
        makeAudioClip(
            std::filesystem::path{"second.wav"},
            TimeRange{
                .start = TimePosition{},
                .end = TimePosition{5.0},
            }));

    REQUIRE(setTestAudioClip(session, track_id, first_clip));
    CHECK(findTrackAudioClip(session, track_id) == std::optional<AudioClip>{first_clip});

    REQUIRE(setTestAudioClip(session, track_id, second_clip));
    CHECK(findTrackAudioClip(session, track_id) == std::optional<AudioClip>{second_clip});
}

// Verifies clips without allocated identities are rejected instead of silently assigned ids.
TEST_CASE("Setting a track clip rejects an invalid clip id", "[core][session]")
{
    Session session;
    const auto track_id = addTestTrack(session, "Full Mix");
    const AudioClip audio_clip = makeAudioClip(std::filesystem::path{"mix.wav"});

    const auto clip_set = setTestAudioClip(session, track_id, audio_clip);

    CHECK_FALSE(clip_set);
    CHECK(findTrackAudioClip(session, track_id) == std::nullopt);
    CHECK(session.timeline() == TimeRange{});
}

// Verifies one durable clip id cannot be attached to two different tracks.
TEST_CASE("Setting a duplicate audio clip id on another track fails", "[core][session]")
{
    Session session;
    const auto first_track_id = addTestTrack(session, "Full Mix");
    const auto second_track_id = addTestTrack(session, "Solo");
    const AudioClip first_clip =
        withAllocatedClipId(session, makeAudioClip(std::filesystem::path{"mix.wav"}));
    const AudioClip duplicate_clip =
        withClipId(makeAudioClip(std::filesystem::path{"solo.wav"}), first_clip.id);

    REQUIRE(setTestAudioClip(session, first_track_id, first_clip));
    const auto clip_set = setTestAudioClip(session, second_track_id, duplicate_clip);

    CHECK_FALSE(clip_set);
    CHECK(findTrackAudioClip(session, second_track_id) == std::nullopt);
}

// Verifies the common editor flow where an empty row receives its first clip.
TEST_CASE("Setting an empty track clip stores the clip", "[core][session]")
{
    Session session;
    const auto track_id = addTestTrack(session, "Full Mix");
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{8.0},
    };
    const AudioClip audio_clip = withAllocatedClipId(
        session, makeAudioClip(std::filesystem::path{"mix.wav"}, timeline_range));

    const auto clip_set = setTestAudioClip(session, track_id, audio_clip);

    CHECK(clip_set);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].audio_clip == std::optional<AudioClip>{audio_clip});
    CHECK(session.timeline() == timeline_range);
}

// Verifies stored clip placements define the aggregate project timeline.
TEST_CASE("Setting track clips expands the session timeline", "[core][session]")
{
    Session session;
    addTrackWithAudioClip(
        session,
        "Intro",
        std::filesystem::path{"intro.wav"},
        TimeRange{
            .start = TimePosition{},
            .end = TimePosition{3.0},
        },
        TimePosition{1.0});
    addTrackWithAudioClip(
        session,
        "Outro",
        std::filesystem::path{"outro.wav"},
        TimeRange{
            .start = TimePosition{},
            .end = TimePosition{7.5},
        },
        TimePosition{0.5});

    CHECK(
        session.timeline() == TimeRange{
                                  .start = TimePosition{0.5},
                                  .end = TimePosition{8.0},
                              });
}

// Verifies missing-track updates remain recoverable and do not change the project timeline.
TEST_CASE("Setting a missing track clip fails cleanly", "[core][session]")
{
    Session session;
    const auto existing_id =
        addTrackWithAudioClip(session, "Full Mix", std::filesystem::path{"mix.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{10.0},
    };
    const AudioClip missing_clip = withAllocatedClipId(
        session, makeAudioClip(std::filesystem::path{"missing.wav"}, timeline_range));

    const auto clip_set = setTestAudioClip(session, TrackId{999}, missing_clip);

    CHECK_FALSE(clip_set);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(session.tracks().size() == 1);
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
    CHECK(session.timeline() == TimeRange{});
}

} // namespace rock_hero::core
