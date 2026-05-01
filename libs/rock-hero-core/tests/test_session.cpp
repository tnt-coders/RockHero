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

// Bundles a fixture path with the source range and timeline position Session commits as one clip.
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

// Returns a copy with the session-assigned id expected after commit.
[[nodiscard]] AudioClip withClipId(AudioClip audio_clip, AudioClipId id)
{
    audio_clip.id = id;
    return audio_clip;
}

// Creates a track through Session and commits the clip through the backend-accepted commit path.
TrackId addCommittedTrack(
    Session& session, std::string name, std::filesystem::path path, TimeRange source_range = {},
    TimePosition position = {})
{
    const TrackId track_id = session.addTrack(std::move(name));
    const bool committed = session.commitTrackAudioClip(
        track_id, makeAudioClip(std::move(path), source_range, position));
    REQUIRE(committed);
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
    CHECK_FALSE(track.audio_clip.has_value());
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
    CHECK_FALSE(track->audio_clip.has_value());
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
    const auto first_id = addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});
    addCommittedTrack(session, "Solo", std::filesystem::path{"solo.wav"});

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
    addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});

    const auto renamed = session.renameTrack(TrackId{999}, "Edited Mix");

    CHECK_FALSE(renamed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(session.tracks()[0].name == "Full Mix");
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
}

// Verifies committing one track clip does not disturb neighboring track data.
TEST_CASE("Committing a track clip updates only that track", "[core][session]")
{
    Session session;
    const auto first_id = addCommittedTrack(session, "Full Mix", std::filesystem::path{"old.wav"});
    addCommittedTrack(session, "Solo", std::filesystem::path{"solo.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{12.0},
    };

    const auto committed = session.commitTrackAudioClip(
        first_id, makeAudioClip(std::filesystem::path{"new.wav"}, timeline_range));

    CHECK(committed);
    REQUIRE(session.tracks().size() == 2);
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"new.wav"}, timeline_range), AudioClipId{3})});
    CHECK(
        session.tracks()[1].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"solo.wav"}), AudioClipId{2})});
    CHECK(session.timeline() == timeline_range);
}

// Verifies Session assigns clip ids at commit time instead of trusting caller-supplied ids.
TEST_CASE("Committing track clips assigns session-owned clip ids", "[core][session]")
{
    Session session;
    const auto track_id = session.addTrack("Full Mix");
    auto first_clip = makeAudioClip(
        std::filesystem::path{"first.wav"},
        TimeRange{
            .start = TimePosition{},
            .end = TimePosition{4.0},
        });
    first_clip.id = AudioClipId{999};
    const auto second_clip = makeAudioClip(
        std::filesystem::path{"second.wav"},
        TimeRange{
            .start = TimePosition{},
            .end = TimePosition{5.0},
        });

    REQUIRE(session.commitTrackAudioClip(track_id, first_clip));
    CHECK(
        findTrackAudioClip(session, track_id) ==
        std::optional<AudioClip>{withClipId(first_clip, AudioClipId{1})});

    REQUIRE(session.commitTrackAudioClip(track_id, second_clip));
    CHECK(
        findTrackAudioClip(session, track_id) ==
        std::optional<AudioClip>{withClipId(second_clip, AudioClipId{2})});
}

// Verifies the common editor flow where an empty row receives its first committed clip.
TEST_CASE("Committing an empty track clip stores the clip", "[core][session]")
{
    Session session;
    const auto track_id = session.addTrack("Full Mix");
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{8.0},
    };
    const AudioClip audio_clip = makeAudioClip(std::filesystem::path{"mix.wav"}, timeline_range);

    const auto committed = session.commitTrackAudioClip(track_id, audio_clip);

    CHECK(committed);
    REQUIRE(session.tracks().size() == 1);
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(audio_clip, AudioClipId{1})});
    CHECK(session.timeline() == timeline_range);
}

// Verifies committed clip placements define the aggregate project timeline.
TEST_CASE("Committing track clips expands the session timeline", "[core][session]")
{
    Session session;
    addCommittedTrack(
        session,
        "Intro",
        std::filesystem::path{"intro.wav"},
        TimeRange{
            .start = TimePosition{},
            .end = TimePosition{3.0},
        },
        TimePosition{1.0});
    addCommittedTrack(
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

// Verifies missing-track commits remain recoverable and do not change the project timeline.
TEST_CASE("Committing a missing track clip fails cleanly", "[core][session]")
{
    Session session;
    const auto existing_id =
        addCommittedTrack(session, "Full Mix", std::filesystem::path{"mix.wav"});
    const TimeRange timeline_range{
        .start = TimePosition{},
        .end = TimePosition{10.0},
    };

    const auto committed = session.commitTrackAudioClip(
        TrackId{999}, makeAudioClip(std::filesystem::path{"missing.wav"}, timeline_range));

    CHECK_FALSE(committed);
    REQUIRE(session.findTrack(existing_id) != nullptr);
    REQUIRE(session.tracks().size() == 1);
    CHECK(
        session.tracks()[0].audio_clip ==
        std::optional<AudioClip>{withClipId(
            makeAudioClip(std::filesystem::path{"mix.wav"}), AudioClipId{1})});
    CHECK(session.timeline() == TimeRange{});
}

} // namespace rock_hero::core
