#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <compare>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/mix/i_mix_controls.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/common/audio/testing/fake_tone_automation.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/tone_timeline/i_tone_timeline_player.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/game/core/session/gameplay_session.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

using common::audio::testing::ConfigurableAudioDeviceConfiguration;
using common::audio::testing::FakeLiveInput;
using common::audio::testing::FakeToneAutomation;
using common::audio::testing::InMemoryAudioConfigStore;
using common::audio::testing::LiveInputSetterCall;
using common::audio::testing::setCalibrationInputMonitoringCall;
using common::audio::testing::setInputGainCall;
using common::audio::testing::setLiveInputMonitoringCall;
using common::audio::testing::ToneAutomationWriteCall;

// Arrangement ids are canonical UUIDs (package validation rejects anything else); these two are
// the fixture's "lead" and "bass" arrangements in that order.
constexpr std::string_view g_first_arrangement_id{"4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4"};
constexpr std::string_view g_second_arrangement_id{"7b2d9e10-3c4f-45a8-9d21-e5f6a7b8c9d0"};

// A stable physical input route the monitoring tests calibrate against.
[[nodiscard]] common::audio::InputDeviceIdentity makeIdentity(std::string device = "Interface A")
{
    return common::audio::InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = std::move(device),
        .input_channel_index = 0,
        .input_channel_name = "Input 1",
    };
}

// A calibration record bound to one physical route at the given gain.
[[nodiscard]] common::audio::InputCalibrationState makeCalibration(
    const common::audio::InputDeviceIdentity& identity, double gain_db)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Test-local temp directory that owns the packages and workspaces one test case creates.
class TemporarySessionDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporarySessionDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-gameplay-session-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the test directory on a best-effort basis.
    ~TemporarySessionDirectory() noexcept
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_path, cleanup_error);
    }

    TemporarySessionDirectory(const TemporarySessionDirectory&) = delete;
    TemporarySessionDirectory(TemporarySessionDirectory&&) = delete;
    TemporarySessionDirectory& operator=(const TemporarySessionDirectory&) = delete;
    TemporarySessionDirectory& operator=(TemporarySessionDirectory&&) = delete;

    // Root path tests build fixture packages and session workspaces under.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

// Writes a placeholder audio file; FLAC content validation is behind the faked audio boundary,
// so package reading only needs the file to exist (same pattern as the package reader tests).
void writeAudioFile(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream{path, std::ios::binary};
    stream << "audio";
}

// Builds a minimal two-arrangement song referencing the placeholder audio file.
[[nodiscard]] common::core::Song makeSong(const std::filesystem::path& audio_path)
{
    common::core::Song song;
    for (const std::string_view arrangement_id : {g_first_arrangement_id, g_second_arrangement_id})
    {
        song.arrangements.push_back(
            common::core::Arrangement{
                .id = std::string{arrangement_id},
                .part = common::core::Part::Lead,
                .difficulty = common::core::DifficultyRating{},
                .audio_asset =
                    common::core::AudioAsset{
                        .path = audio_path, .normalization = std::nullopt, .start_offset = {}
                    },
                .audio_duration = common::core::TimeDuration{},
                .tones = {},
                .tone_track = {},
                .tone_automation = {},
                .chart_ref = {},
                .chart = {},
            });
    }
    return song;
}

// Writes a real `.rock` archive holding the supplied song.
[[nodiscard]] std::filesystem::path writePackage(
    const TemporarySessionDirectory& directory, const common::core::Song& song)
{
    const std::filesystem::path staging = directory.path() / "staging";
    const std::filesystem::path package_path = directory.path() / "song.rock";
    REQUIRE(common::core::writeRockSongPackage(package_path, staging, song).has_value());
    return package_path;
}

// Writes a real `.rock` archive the session's Loading stage can extract and parse.
[[nodiscard]] std::filesystem::path writePackage(const TemporarySessionDirectory& directory)
{
    const std::filesystem::path audio_path = directory.path() / "source.flac";
    writeAudioFile(audio_path);
    return writePackage(directory, makeSong(audio_path));
}

// Fake song-audio boundary: fills durations on prepare and records activation order.
class FakeSongAudio final : public common::audio::ISongAudio
{
public:
    // Fills every arrangement's duration like the engine's validation does, or fails on demand.
    [[nodiscard]] std::expected<void, common::audio::SongAudioError> prepareSong(
        common::core::Song& song) override
    {
        prepare_call_count += 1;
        if (fail_prepare)
        {
            return std::unexpected{common::audio::SongAudioError{
                common::audio::SongAudioErrorCode::UnreadableAudioFile, "fake prepare failure"
            }};
        }

        for (common::core::Arrangement& arrangement : song.arrangements)
        {
            arrangement.audio_duration = common::core::TimeDuration{10.0};
        }
        return {};
    }

    // Records the activated arrangement id, or fails on demand.
    [[nodiscard]] std::expected<void, common::audio::SongAudioError> setActiveArrangement(
        const common::core::Arrangement& arrangement) override
    {
        if (fail_activate)
        {
            return std::unexpected{common::audio::SongAudioError{
                common::audio::SongAudioErrorCode::UnreadableAudioFile, "fake activate failure"
            }};
        }

        active_arrangement_id = arrangement.id;
        activate_call_count += 1;
        return {};
    }

    // Records the clear so close() coverage can assert release semantics.
    [[nodiscard]] std::expected<void, common::audio::SongAudioError> clearActiveArrangement()
        override
    {
        clear_call_count += 1;
        active_arrangement_id.clear();
        return {};
    }

    // The session never mirrors tempo maps itself; recorded only to keep the fake honest.
    void mirrorTempoMap(const common::core::TempoMap& /*tempo_map*/) override
    {}

    // When set, prepareSong fails with a typed error.
    bool fail_prepare{false};

    // When set, setActiveArrangement fails with a typed error.
    bool fail_activate{false};

    // Id of the most recently activated arrangement.
    std::string active_arrangement_id;

    // Number of prepareSong calls observed.
    int prepare_call_count{0};

    // Number of setActiveArrangement calls observed.
    int activate_call_count{0};

    // Number of clearActiveArrangement calls observed.
    int clear_call_count{0};
};

// Fake transport recording session-driven playback commands and supporting manual transitions.
class FakeSessionTransport final : public common::audio::ITransport
{
public:
    // Records play and notifies listeners like the engine's synchronous state update does.
    void play() override
    {
        play_call_count += 1;
        current_state.playing = true;
        notifyListeners();
    }

    // Records pause and notifies listeners.
    void pause() override
    {
        pause_call_count += 1;
        current_state.playing = false;
        notifyListeners();
    }

    // Records stop; the session never calls it today.
    void stop() override
    {
        current_state.playing = false;
        notifyListeners();
    }

    // Records the latest seek target.
    void seek(common::core::TimePosition position) override
    {
        last_seek_position = position;
        seek_call_count += 1;
    }

    // Returns the manually controlled coarse state.
    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    // Returns the manually controlled position.
    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    // Mirrors the v1 speed contract.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setPlaybackSpeed(
        double factor) override
    {
        if (std::is_neq(factor <=> 1.0))
        {
            return std::unexpected{
                common::audio::TransportError{common::audio::TransportErrorCode::SpeedNotSupported}
            };
        }

        speed_call_count += 1;
        return {};
    }

    // Always the v1 speed factor.
    [[nodiscard]] double playbackSpeed() const noexcept override
    {
        return 1.0;
    }

    // Stores the loop region without contract checks; passthrough coverage only needs arrival.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setLoopRegion(
        common::core::TimeRange region) override
    {
        loop_region = region;
        return {};
    }

    // Disengages the stored loop region.
    void clearLoopRegion() override
    {
        loop_region.reset();
    }

    // Returns the engaged loop region.
    [[nodiscard]] std::optional<common::core::TimeRange> loopRegion() const noexcept override
    {
        return loop_region;
    }

    // Stores a non-owning listener pointer (the session registers itself).
    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    // Removes the registered listener.
    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Simulates the engine's end-of-content auto-stop: an unsolicited stop notification.
    void simulateAutoStop()
    {
        current_state.playing = false;
        notifyListeners();
    }

    // Sends the current coarse state to every registered listener.
    void notifyListeners()
    {
        for (Listener* listener : listeners)
        {
            listener->onTransportStateChanged(current_state);
        }
    }

    // Coarse transport state returned by state() and sent to listeners.
    common::audio::TransportState current_state{};

    // Position returned by position().
    common::core::TimePosition current_position{};

    // Latest loop region stored by setLoopRegion().
    std::optional<common::core::TimeRange> loop_region{};

    // Latest seek target observed.
    std::optional<common::core::TimePosition> last_seek_position{};

    // Call counters the transition tests assert on.
    int play_call_count{0};
    int pause_call_count{0};
    int seek_call_count{0};
    int speed_call_count{0};

    // Non-owning listeners registered by the session under test.
    std::vector<Listener*> listeners{};
};

// Fake live rig that captures the load request and completion for manual firing.
class FakeLiveRig final : public common::audio::ILiveRig
{
public:
    // The session never captures during gameplay; fail loudly if it ever tries.
    [[nodiscard]] std::expected<common::audio::LiveRigSnapshot, common::audio::LiveRigError>
    captureActiveRig(const common::audio::LiveRigCaptureRequest& /*request*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest, "capture not expected in gameplay"
        }};
    }

    // The session never mints tones; fail loudly if it ever tries.
    [[nodiscard]] std::expected<std::string, common::audio::LiveRigError> mintEmptyTone(
        const std::filesystem::path& /*song_directory*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest, "mint not expected in gameplay"
        }};
    }

    // The session never adds branches incrementally; fail loudly if it ever tries.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> addEmptyToneBranch(
        const std::string& /*tone_document_ref*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest, "branch add not expected"
        }};
    }

    // Captures the request and completion; tests fire the completion manually to prove the
    // session never reports Ready before the preload actually finishes.
    void loadLiveRig(
        common::audio::LiveRigLoadRequest request,
        common::audio::LiveRigLoadResultCallback completion) override
    {
        load_call_count += 1;
        last_request = std::move(request);
        pending_completion = std::move(completion);
    }

    // The session tears down through close(), not clearLiveRig(); recorded only.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> clearLiveRig() override
    {
        clear_call_count += 1;
        return {};
    }

    // The session never switches audible tones itself (the timeline owns switching).
    [[nodiscard]] std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
    setAudibleTone(const std::string& /*tone_document_ref*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest, "audible switch not expected"
        }};
    }

    // The session never exports tone files; fail loudly if it ever tries.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> exportAudibleTone(
        const common::audio::ToneFileExportRequest& /*request*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest, "export not expected in gameplay"
        }};
    }

    // The session never captures chain mementos; fail loudly if it ever tries.
    [[nodiscard]] std::expected<common::audio::AudibleToneState, common::audio::LiveRigError>
    captureAudibleToneState() override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest,
            "chain capture not expected in gameplay"
        }};
    }

    // The session never imports tone files; fail loudly if it ever tries.
    void replaceAudibleToneFromFile(
        common::audio::ToneFileReplaceRequest /*request*/,
        common::audio::LiveRigLoadResultCallback completion) override
    {
        completion(
            std::unexpected{common::audio::LiveRigError{
                common::audio::LiveRigErrorCode::InvalidRequest,
                "chain replace not expected in gameplay"
            }});
    }

    // The session never restores chain mementos; fail loudly if it ever tries.
    [[nodiscard]] std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
    restoreAudibleToneState(const common::audio::AudibleToneState& /*state*/) override
    {
        return std::unexpected{common::audio::LiveRigError{
            common::audio::LiveRigErrorCode::InvalidRequest,
            "chain restore not expected in gameplay"
        }};
    }

    // Returns the stored monitor gain (the session's monitor volume forwards here).
    [[nodiscard]] common::audio::Gain outputGain() const override
    {
        return output_gain;
    }

    // Stores the requested monitor gain.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> setOutputGain(
        common::audio::Gain gain) override
    {
        output_gain = gain;
        return {};
    }

    // Fires the captured completion with a successful (empty) result.
    void completeSuccessfully()
    {
        completeSuccessfully(common::audio::LiveRigLoadResult{});
    }

    // Fires the captured completion with the supplied result (e.g. carrying tone-chain
    // identities for the automation rebuild).
    void completeSuccessfully(common::audio::LiveRigLoadResult result)
    {
        REQUIRE(pending_completion);
        auto completion = std::exchange(pending_completion, nullptr);
        completion(std::move(result));
    }

    // Fires the captured completion with a typed failure.
    void completeWithFailure()
    {
        REQUIRE(pending_completion);
        auto completion = std::exchange(pending_completion, nullptr);
        completion(
            std::unexpected{common::audio::LiveRigError{
                common::audio::LiveRigErrorCode::MissingToneDocument, "fake rig failure"
            }});
    }

    // Fires the captured completion with the aggregated missing-plugin refusal (21-Q1(A)).
    void completeWithMissingPlugins()
    {
        REQUIRE(pending_completion);
        auto completion = std::exchange(pending_completion, nullptr);
        completion(
            std::unexpected{common::audio::LiveRigError{
                common::audio::LiveRigErrorCode::MissingPlugins,
                "Missing plugins: Amp Sim (tones/a/tone.json)"
            }});
    }

    // Monitor gain most recently forwarded by the session.
    common::audio::Gain output_gain{};

    // Most recent load request observed.
    std::optional<common::audio::LiveRigLoadRequest> last_request{};

    // Captured completion awaiting a manual fire.
    common::audio::LiveRigLoadResultCallback pending_completion{};

    // Number of loadLiveRig calls observed (restart must never add to this).
    int load_call_count{0};

    // Number of clearLiveRig calls observed.
    int clear_call_count{0};
};

// Fake tone timeline recording the schedule handoff.
class FakeToneTimeline final : public common::audio::IToneTimelinePlayer
{
public:
    // Records the prepare call, or fails on demand.
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> prepareToneTimeline(
        const std::filesystem::path& song_directory,
        std::span<const common::core::ToneSwitchRegion> regions) override
    {
        if (fail_prepare)
        {
            return std::unexpected{common::audio::LiveRigError{
                common::audio::LiveRigErrorCode::InvalidRequest, "fake timeline failure"
            }};
        }

        prepare_call_count += 1;
        last_song_directory = song_directory;
        last_regions.assign(regions.begin(), regions.end());
        return {};
    }

    // Seek resync is not session-driven in Phase 2 (automation resyncs automatically).
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> setToneTimelinePosition(
        common::core::TimePosition /*position*/) override
    {
        return {};
    }

    // When set, prepareToneTimeline fails with a typed error.
    bool fail_prepare{false};

    // Number of successful prepare calls observed.
    int prepare_call_count{0};

    // Workspace directory the schedule was prepared against.
    std::filesystem::path last_song_directory{};

    // Schedule most recently handed over.
    std::vector<common::core::ToneSwitchRegion> last_regions{};
};

// Fake wait-free clock; the session only hands it through to consumers.
class FakePlaybackClock final : public common::audio::IPlaybackClock
{
public:
    // Returns the configured snapshot.
    [[nodiscard]] common::audio::PlaybackClockSnapshot snapshot() const noexcept override
    {
        return {};
    }
};

// Fake mix boundary recording the master and backing gains the session forwards.
class FakeMixControls final : public common::audio::IMixControls
{
public:
    // Stores the requested master gain.
    [[nodiscard]] std::expected<void, common::audio::MixControlsError> setMasterGain(
        common::audio::Gain gain) override
    {
        master_gain = gain;
        return {};
    }

    // Returns the stored master gain.
    [[nodiscard]] common::audio::Gain masterGain() const override
    {
        return master_gain;
    }

    // Stores the requested backing gain.
    [[nodiscard]] std::expected<void, common::audio::MixControlsError> setBackingGain(
        common::audio::Gain gain) override
    {
        backing_gain = gain;
        return {};
    }

    // Returns the stored backing gain.
    [[nodiscard]] common::audio::Gain backingGain() const override
    {
        return backing_gain;
    }

    // Gains most recently forwarded by the session.
    common::audio::Gain master_gain{};
    common::audio::Gain backing_gain{};
};

// Bundles the fakes and the session under test with a loaded fixture package.
struct SessionHarness
{
    TemporarySessionDirectory directory{};
    FakeSongAudio song_audio{};
    FakeSessionTransport transport{};
    FakeLiveRig live_rig{};
    FakeToneTimeline tone_timeline{};
    FakeToneAutomation tone_automation{};
    FakeMixControls mix_controls{};
    FakePlaybackClock clock{};

    // Shared live-input monitoring collaborators. The device configuration and store default to no
    // route and no calibration, so the wired gate stays silent until a test seeds a match.
    FakeLiveInput live_input{};
    ConfigurableAudioDeviceConfiguration devices{};
    InMemoryAudioConfigStore config_store{};
    common::audio::LiveInputMonitor live_input_monitor{live_input, devices, config_store};

    GameplaySession session{
        song_audio,
        transport,
        live_rig,
        tone_timeline,
        tone_automation,
        mix_controls,
        clock,
        live_input_monitor,
    };

    // Points the device configuration at a fixed route and seeds a matching calibration, so
    // reaching Ready arms monitoring for that route.
    void seedMatchingCalibration(double gain_db)
    {
        const common::audio::InputDeviceIdentity identity = makeIdentity();
        devices.current_input_identity = identity;
        REQUIRE(config_store.saveInputCalibration(makeCalibration(identity, gain_db)).has_value());
    }

    // Starts the session over a freshly written fixture package.
    [[nodiscard]] std::expected<void, GameplaySessionError> startFixture(
        const std::string& arrangement_id = {})
    {
        return session.start(
            GameplaySessionRequest{
                .package_path = writePackage(directory),
                .arrangement_id = arrangement_id,
                .workspace_directory = directory.path() / "workspace",
            });
    }
};

} // namespace

// Verifies the full happy-path state walk, including the pre-song preload guarantee: Ready is
// never reported before the rig completion fires.
TEST_CASE("Gameplay session walks the happy path to Ready", "[core][session]")
{
    SessionHarness harness;

    REQUIRE(harness.startFixture().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::PreparingRig);
    CHECK(harness.song_audio.active_arrangement_id == std::string{g_first_arrangement_id});
    REQUIRE(harness.live_rig.last_request.has_value());
    if (harness.live_rig.last_request.has_value())
    {
        CHECK(
            harness.live_rig.last_request->song_directory ==
            harness.directory.path() / "workspace");
        CHECK(harness.live_rig.last_request->audible_tone_ref.empty());
    }

    // The session must not be Ready until the rig preload actually completes.
    CHECK(harness.session.stage() != GameplaySessionStage::Ready);

    harness.live_rig.completeSuccessfully();
    CHECK(harness.session.stage() == GameplaySessionStage::Ready);
    CHECK(harness.tone_timeline.prepare_call_count == 1);
    REQUIRE(harness.session.activeArrangement() != nullptr);
    CHECK(harness.session.activeArrangement()->id == std::string{g_first_arrangement_id});
}

// Verifies arrangement selection by id and the typed miss.
TEST_CASE("Gameplay session selects arrangements by id", "[core][session]")
{
    SessionHarness harness;

    REQUIRE(harness.startFixture(std::string{g_second_arrangement_id}).has_value());
    CHECK(harness.song_audio.active_arrangement_id == std::string{g_second_arrangement_id});
}

// Verifies a missing arrangement id fails with the typed error and a Failed stage.
TEST_CASE("Gameplay session rejects unknown arrangement ids", "[core][session]")
{
    SessionHarness harness;

    const auto started = harness.startFixture("9e8d7c6b-5a49-4832-8171-605f4e3d2c1b");

    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().code == GameplaySessionErrorCode::ArrangementNotFound);
    CHECK(harness.session.stage() == GameplaySessionStage::Failed);
    REQUIRE(harness.session.error().has_value());
    if (harness.session.error().has_value())
    {
        CHECK(harness.session.error()->code == GameplaySessionErrorCode::ArrangementNotFound);
    }
}

// Verifies an unreadable package fails the Loading stage with the typed error.
TEST_CASE("Gameplay session reports unreadable packages", "[core][session]")
{
    SessionHarness harness;

    const auto started = harness.session.start(
        GameplaySessionRequest{
            .package_path = harness.directory.path() / "missing.rock",
            .arrangement_id = {},
            .workspace_directory = harness.directory.path() / "workspace",
        });

    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().code == GameplaySessionErrorCode::PackageUnreadable);
    CHECK(harness.session.stage() == GameplaySessionStage::Failed);
}

// Verifies preparation and activation failures land in their own typed Failed stages.
TEST_CASE("Gameplay session distinguishes prepare and activate failures", "[core][session]")
{
    SessionHarness prepare_harness;
    prepare_harness.song_audio.fail_prepare = true;
    const auto prepare_started = prepare_harness.startFixture();
    REQUIRE_FALSE(prepare_started.has_value());
    CHECK(prepare_started.error().code == GameplaySessionErrorCode::PreparationFailed);

    SessionHarness activate_harness;
    activate_harness.song_audio.fail_activate = true;
    const auto activate_started = activate_harness.startFixture();
    REQUIRE_FALSE(activate_started.has_value());
    CHECK(activate_started.error().code == GameplaySessionErrorCode::ActivationFailed);
}

// Verifies a rig-load failure lands in Failed with the rig's message preserved.
TEST_CASE("Gameplay session fails when the rig load fails", "[core][session]")
{
    SessionHarness harness;
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeWithFailure();

    CHECK(harness.session.stage() == GameplaySessionStage::Failed);
    REQUIRE(harness.session.error().has_value());
    if (harness.session.error().has_value())
    {
        CHECK(harness.session.error()->code == GameplaySessionErrorCode::RigLoadFailed);
    }
}

// Verifies the missing-plugin refusal maps to its own session code so UI can present an
// "install these plugins" flow (21-Q1: refuse to start, listing the missing plugins).
TEST_CASE("Gameplay session surfaces missing plugins distinctly", "[core][session]")
{
    SessionHarness harness;
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeWithMissingPlugins();

    CHECK(harness.session.stage() == GameplaySessionStage::Failed);
    REQUIRE(harness.session.error().has_value());
    if (harness.session.error().has_value())
    {
        CHECK(harness.session.error()->code == GameplaySessionErrorCode::MissingPlugins);
        CHECK(harness.session.error()->message.find("Amp Sim") != std::string::npos);
    }
}

// Verifies a tone-timeline failure after a successful rig load lands in its own typed Failed.
TEST_CASE("Gameplay session fails when the tone timeline fails", "[core][session]")
{
    SessionHarness harness;
    harness.tone_timeline.fail_prepare = true;
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeSuccessfully();

    CHECK(harness.session.stage() == GameplaySessionStage::Failed);
    REQUIRE(harness.session.error().has_value());
    if (harness.session.error().has_value())
    {
        CHECK(harness.session.error()->code == GameplaySessionErrorCode::ToneTimelineFailed);
    }
}

// Verifies playback transitions: play/pause round-trip, restart without re-preloading, and the
// auto-stop transition to Finished.
TEST_CASE("Gameplay session drives playback transitions", "[core][session]")
{
    SessionHarness harness;
    REQUIRE(harness.startFixture().has_value());
    harness.live_rig.completeSuccessfully();
    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);

    REQUIRE(harness.session.play().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::Playing);
    CHECK(harness.transport.play_call_count == 1);

    REQUIRE(harness.session.pause().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::Paused);
    CHECK(harness.transport.pause_call_count == 1);

    REQUIRE(harness.session.play().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::Playing);

    // Instant restart = seek(0) + play with no rig work.
    REQUIRE(harness.session.restart().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::Playing);
    CHECK(harness.transport.last_seek_position == std::optional{common::core::TimePosition{}});
    CHECK(harness.live_rig.load_call_count == 1);

    // The engine's end-of-content auto-stop is the only unsolicited stop: Playing -> Finished.
    harness.transport.simulateAutoStop();
    CHECK(harness.session.stage() == GameplaySessionStage::Finished);

    // Playing again from Finished restarts from the top.
    REQUIRE(harness.session.play().has_value());
    CHECK(harness.session.stage() == GameplaySessionStage::Playing);
    CHECK(harness.live_rig.load_call_count == 1);
}

// Verifies the session refuses operations in stages that forbid them and never touches the
// transport when it refuses.
TEST_CASE("Gameplay session refuses out-of-stage operations", "[core][session]")
{
    SessionHarness harness;

    const auto played_idle = harness.session.play();
    REQUIRE_FALSE(played_idle.has_value());
    CHECK(played_idle.error().code == GameplaySessionErrorCode::OperationUnavailable);

    REQUIRE(harness.startFixture().has_value());

    // PreparingRig: playback is not yet legal, and the transport must remain untouched.
    const auto played_preparing = harness.session.play();
    REQUIRE_FALSE(played_preparing.has_value());
    CHECK(played_preparing.error().code == GameplaySessionErrorCode::OperationUnavailable);
    CHECK(harness.transport.play_call_count == 0);

    // A second start is illegal while a load is in flight.
    const auto restarted = harness.startFixture();
    REQUIRE_FALSE(restarted.has_value());
    CHECK(restarted.error().code == GameplaySessionErrorCode::OperationUnavailable);
}

// Verifies close() releases the arrangement, deletes the scratch workspace, returns to Idle,
// and drops any stale in-flight rig completion.
TEST_CASE("Gameplay session close releases and ignores stale completions", "[core][session]")
{
    SessionHarness harness;
    REQUIRE(harness.startFixture().has_value());
    const std::filesystem::path workspace = harness.directory.path() / "workspace";
    CHECK(std::filesystem::exists(workspace));

    harness.session.close();

    CHECK(harness.session.stage() == GameplaySessionStage::Idle);
    CHECK(harness.song_audio.clear_call_count == 1);
    CHECK_FALSE(std::filesystem::exists(workspace));

    // The rig completion from the superseded load must not resurrect the session.
    harness.live_rig.completeSuccessfully();
    CHECK(harness.session.stage() == GameplaySessionStage::Idle);
    CHECK(harness.tone_timeline.prepare_call_count == 0);
}

// Verifies the three mix volumes round-trip to their single backend owners: master and backing
// through the mix boundary, monitor through the live rig's output gain (21-Q3: global in v1).
TEST_CASE("Gameplay session forwards mix volumes to their owners", "[core][session]")
{
    SessionHarness harness;

    REQUIRE(harness.session.setMasterVolume(common::audio::Gain{-3.0}).has_value());
    CHECK(harness.mix_controls.master_gain.db == Catch::Approx(-3.0));
    CHECK(harness.session.masterVolume().db == Catch::Approx(-3.0));

    REQUIRE(harness.session.setBackingVolume(common::audio::Gain{-6.0}).has_value());
    CHECK(harness.mix_controls.backing_gain.db == Catch::Approx(-6.0));
    CHECK(harness.session.backingVolume().db == Catch::Approx(-6.0));

    REQUIRE(harness.session.setMonitorVolume(common::audio::Gain{-9.0}).has_value());
    CHECK(harness.live_rig.output_gain.db == Catch::Approx(-9.0));
    CHECK(harness.session.monitorVolume().db == Catch::Approx(-9.0));
}

// Verifies the speed and loop passthroughs reach the transport port.
TEST_CASE("Gameplay session forwards speed and loop to the transport", "[core][session]")
{
    SessionHarness harness;
    REQUIRE(harness.startFixture().has_value());
    harness.live_rig.completeSuccessfully();

    CHECK(harness.session.setPlaybackSpeed(1.0).has_value());
    CHECK(harness.transport.speed_call_count == 1);

    const common::core::TimeRange region{
        .start = common::core::TimePosition{1.0},
        .end = common::core::TimePosition{4.0},
    };
    CHECK(harness.session.setLoopRegion(region).has_value());
    CHECK(harness.transport.loop_region == std::optional{region});

    harness.session.clearLoopRegion();
    CHECK_FALSE(harness.transport.loop_region.has_value());
}

// Verifies the wired-but-silent gate arms at Ready when the game's own store holds a calibration
// matching the active input route: the rig completion (a message-thread edge) drives the shared
// monitor, which sets the calibrated gain and enables processed monitoring in the pinned order.
TEST_CASE("Gameplay session arms live-input monitoring at Ready", "[core][session][live-input]")
{
    SessionHarness harness;
    harness.seedMatchingCalibration(5.0);
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeSuccessfully();

    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);
    CHECK(
        harness.live_input.calls == std::vector<LiveInputSetterCall>{
                                        setCalibrationInputMonitoringCall(false),
                                        setInputGainCall(5.0),
                                        setLiveInputMonitoringCall(true),
                                    });
    CHECK(harness.live_input.live_input_monitoring_enabled);
    CHECK(harness.live_input.current_input_gain.db == Catch::Approx(5.0));
}

// Verifies the gate stays silent (never arms processed monitoring, never applies a calibrated gain)
// when the store holds no calibration for the active route -- the wired-but-silent default until a
// game-side calibration exists. Reaching Ready is unaffected: disabled monitoring is non-fatal.
TEST_CASE(
    "Gameplay session leaves monitoring silent without calibration", "[core][session][live-input]")
{
    SessionHarness harness;
    harness.devices.current_input_identity = makeIdentity();
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeSuccessfully();

    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
    CHECK(harness.live_input.set_input_gain_call_count == 0);
}

// Verifies a calibration bound to a different physical route does not arm the active route: the
// store read for the current identity finds no match, so the gate stays silent.
TEST_CASE(
    "Gameplay session leaves monitoring silent for a mismatched route",
    "[core][session][live-input]")
{
    SessionHarness harness;
    harness.devices.current_input_identity = makeIdentity("Interface A");
    REQUIRE(
        harness.config_store.saveInputCalibration(makeCalibration(makeIdentity("Interface B"), 4.0))
            .has_value());
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeSuccessfully();

    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
    CHECK(harness.live_input.set_input_gain_call_count == 0);
}

// Verifies a corrupt store read is non-fatal: the session still reaches Ready and monitoring is
// left disabled rather than armed on an unverified route.
TEST_CASE(
    "Gameplay session tolerates a corrupt calibration store at Ready",
    "[core][session][live-input]")
{
    SessionHarness harness;
    harness.devices.current_input_identity = makeIdentity();
    harness.config_store.next_input_calibration_for_error = common::audio::AudioConfigError{
        common::audio::AudioConfigErrorCode::InvalidInputCalibrationHistory,
        "fake store read failure"
    };
    REQUIRE(harness.startFixture().has_value());

    harness.live_rig.completeSuccessfully();

    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);
    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// Verifies close() disables the gate the Ready edge armed, so monitoring never outlives the session.
TEST_CASE("Gameplay session disables monitoring on close", "[core][session][live-input]")
{
    SessionHarness harness;
    harness.seedMatchingCalibration(5.0);
    REQUIRE(harness.startFixture().has_value());
    harness.live_rig.completeSuccessfully();
    REQUIRE(harness.live_input.live_input_monitoring_enabled);

    harness.session.close();

    CHECK_FALSE(harness.live_input.live_input_monitoring_enabled);
}

// Verifies replay does no rig work, so monitoring armed at Ready stays armed across
// play/pause/restart and the auto-stop to Finished -- only close() tears it down.
TEST_CASE("Gameplay session keeps monitoring armed across replay", "[core][session][live-input]")
{
    SessionHarness harness;
    harness.seedMatchingCalibration(5.0);
    REQUIRE(harness.startFixture().has_value());
    harness.live_rig.completeSuccessfully();
    REQUIRE(harness.live_input.live_input_monitoring_enabled);

    REQUIRE(harness.session.play().has_value());
    REQUIRE(harness.session.pause().has_value());
    REQUIRE(harness.session.play().has_value());
    REQUIRE(harness.session.restart().has_value());
    harness.transport.simulateAutoStop();
    REQUIRE(harness.session.stage() == GameplaySessionStage::Finished);

    // No rig reload happened, so the gate was never re-driven and monitoring stayed enabled.
    CHECK(harness.live_rig.load_call_count == 1);
    CHECK(harness.live_input.live_input_monitoring_enabled);
}

// Verifies the rig-completion automation rebuild: the package's persisted musical automation
// reaches the automation boundary as derived edit-timeline seconds, resolved through the
// load-reported plugin identities; entries whose durable plugin id no loaded plugin reports are
// skipped (they stay persisted and reconcile on a later load).
TEST_CASE(
    "Gameplay session rebuilds derived tone automation curves at rig completion", "[core][session]")
{
    SessionHarness harness;

    const std::filesystem::path audio_path = harness.directory.path() / "source.flac";
    writeAudioFile(audio_path);
    common::core::Song song = makeSong(audio_path);
    song.tempo_map = common::core::TempoMap::defaultMap(common::core::TimeDuration{4.0});
    song.arrangements.front().tone_automation = {
        common::core::ToneParameterAutomation{
            .plugin_id = "3f8a2b1c-4d5e-4f60-8a9b-0c1d2e3f4a5b",
            .param_id = "gain",
            .points =
                {
                    common::core::ToneAutomationPoint{
                        .position =
                            common::core::GridPosition{.measure = 1, .beat = 1, .offset = {}},
                        .norm_value = 0.25F,
                        .curve_shape = 0.0F,
                    },
                    // A sub-beat position exercises the full musical-to-seconds conversion.
                    common::core::ToneAutomationPoint{
                        .position =
                            common::core::GridPosition{
                                .measure = 2,
                                .beat = 3,
                                .offset = common::core::Fraction{1, 2},
                            },
                        .norm_value = 0.75F,
                        .curve_shape = -0.5F,
                    },
                },
        },
        // No loaded plugin reports this durable id, so the entry must be skipped.
        common::core::ToneParameterAutomation{
            .plugin_id = "9a8b7c6d-5e4f-4a3b-8c2d-1e0f9a8b7c6d",
            .param_id = "mix",
            .points = {
                common::core::ToneAutomationPoint{
                    .position = common::core::GridPosition{.measure = 1, .beat = 2, .offset = {}},
                    .norm_value = 0.5F,
                    .curve_shape = 0.0F,
                },
            },
        },
    };
    const common::core::TempoMap tempo_map = song.tempo_map;

    REQUIRE(harness.session
                .start(
                    GameplaySessionRequest{
                        .package_path = writePackage(harness.directory, song),
                        .arrangement_id = {},
                        .workspace_directory = harness.directory.path() / "workspace",
                    })
                .has_value());

    harness.live_rig.completeSuccessfully(
        common::audio::LiveRigLoadResult{
            .plugins = {},
            .output_gain = {},
            .tone_chains = {
                common::audio::LoadedToneChainIdentities{
                    .tone_document_ref = "tones/x/tone.json",
                    .plugins =
                        {
                            common::audio::LoadedTonePluginIdentity{
                                .instance_id = "instance-1",
                                .stable_id = "3f8a2b1c-4d5e-4f60-8a9b-0c1d2e3f4a5b",
                            },
                        },
                    .summed_reported_latency_seconds = 0.0,
                },
            },
        });
    REQUIRE(harness.session.stage() == GameplaySessionStage::Ready);

    REQUIRE(harness.tone_automation.write_calls.size() == 1);
    const ToneAutomationWriteCall& call = harness.tone_automation.write_calls.front();
    CHECK(call.tone_document_ref == "tones/x/tone.json");
    CHECK(call.instance_id == "instance-1");
    CHECK(call.param_id == "gain");
    REQUIRE(call.points.size() == 2);
    CHECK(call.points.front().seconds == Catch::Approx(tempo_map.secondsAtNote(1, 1, {})));
    CHECK(std::is_eq(call.points.front().norm_value <=> 0.25F));
    CHECK(
        call.points.back().seconds ==
        Catch::Approx(tempo_map.secondsAtNote(2, 3, common::core::Fraction{1, 2})));
    CHECK(std::is_eq(call.points.back().norm_value <=> 0.75F));
    CHECK(std::is_eq(call.points.back().curve_shape <=> -0.5F));
}

} // namespace rock_hero::game::core
