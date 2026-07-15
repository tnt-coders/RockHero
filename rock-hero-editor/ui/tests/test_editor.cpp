#include "main_window/editor_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <expected>
#include <filesystem>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/clock/playback_clock_snapshot.h>
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_audio_meter_source.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/testing/configurable_song_audio.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/editor/core/settings/i_editor_settings.h>
#include <rock_hero/editor/core/tasks/i_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/null_editor_settings.h>
#include <rock_hero/editor/ui/main_window/editor.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using RecordingThumbnailFactory = common::audio::testing::RecordingThumbnailFactory;
using testing::findRequiredDirectChild;

// Records transport listeners and state so the controller can subscribe during construction.
class FakeTransport final : public common::audio::ITransport
{
public:
    // Simulates starting playback for the composed controller.
    void play() override
    {
        current_state.playing = true;
    }

    // Simulates pausing playback for the composed controller.
    void pause() override
    {
        current_state.playing = false;
    }

    // Simulates stop by clearing playback and resetting the cursor position.
    void stop() override
    {
        current_state.playing = false;
        current_position = common::core::TimePosition{};
    }

    // Records the latest seek target requested by the composed controller.
    void seek(common::core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    // Returns the manually controlled coarse state.
    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    // Returns the manually controlled current cursor position.
    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        return current_position;
    }

    // Contract-shaped stub: this construction test never drives speed, so only 1.0 is accepted.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setPlaybackSpeed(
        double factor) override
    {
        if (std::is_neq(factor <=> 1.0))
        {
            return std::unexpected{
                common::audio::TransportError{common::audio::TransportErrorCode::SpeedNotSupported}
            };
        }

        return {};
    }

    // Always the v1 speed factor; this test never changes it.
    [[nodiscard]] double playbackSpeed() const noexcept override
    {
        return 1.0;
    }

    // Contract-shaped stub storing the normalized region; this test does not drive loops.
    [[nodiscard]] std::expected<void, common::audio::TransportError> setLoopRegion(
        common::core::TimeRange region) override
    {
        const common::core::TimeRange normalized{
            .start = common::core::TimePosition{std::min(region.start.seconds, region.end.seconds)},
            .end = common::core::TimePosition{std::max(region.start.seconds, region.end.seconds)},
        };
        if (normalized.duration().seconds < common::audio::g_minimum_loop_region_duration.seconds)
        {
            return std::unexpected{
                common::audio::TransportError{common::audio::TransportErrorCode::LoopRegionTooShort}
            };
        }

        loop_region = normalized;
        return {};
    }

    // Disengages the stub's loop region.
    void clearLoopRegion() override
    {
        loop_region.reset();
    }

    // Returns the engaged loop region, or nullopt when looping is disengaged.
    [[nodiscard]] std::optional<common::core::TimeRange> loopRegion() const noexcept override
    {
        return loop_region;
    }

    // Registers a non-owning listener pointer during controller construction.
    void addListener(Listener& listener) override
    {
        listeners.push_back(&listener);
    }

    // Removes the listener pointer registered by the controller.
    void removeListener(Listener& listener) override
    {
        std::erase(listeners, &listener);
    }

    // Engaged normalized loop region; nullopt while looping is disengaged.
    std::optional<common::core::TimeRange> loop_region{};

    // Coarse transport state returned by state().
    common::audio::TransportState current_state{};

    // Current cursor position returned by position().
    common::core::TimePosition current_position{};

    // Non-owning listeners registered by the composed controller.
    std::vector<Listener*> listeners{};
};

// Supplies the required editor audio ports that this construction test does not exercise.
class FakeEditorAudioPorts final : public common::audio::IAudioDeviceConfiguration,
                                   public common::audio::IPluginHost,
                                   public common::audio::ILiveRig,
                                   public common::audio::ILiveInput,
                                   public common::audio::IAudioMeterSource,
                                   public common::audio::IToneAutomation,
                                   public common::audio::IPlaybackClock
{
public:
    // Playback clock port: never publishes; the preview never opens in these tests.
    [[nodiscard]] common::audio::PlaybackClockSnapshot snapshot() const noexcept override
    {
        return {};
    }
    // Tone automation port: no tones are loaded in these view tests, so every query is empty.
    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomatableParamInfo>, common::audio::ToneAutomationError>
    listAutomatableParameters(const std::string&) const override
    {
        return std::vector<common::audio::AutomatableParamInfo>{};
    }

    [[nodiscard]] std::expected<
        std::vector<common::audio::AutomationCurvePoint>, common::audio::ToneAutomationError>
    readParameterCurve(const std::string&, const std::string&, const std::string&) const override
    {
        return std::vector<common::audio::AutomationCurvePoint>{};
    }

    [[nodiscard]] std::expected<void, common::audio::ToneAutomationError> writeParameterCurve(
        const std::string&, const std::string&, const std::string&,
        std::span<const common::audio::AutomationCurvePoint>) override
    {
        return {};
    }

    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> readParameterNormValue(
        const std::string&, const std::string&, const std::string&) const override
    {
        return 0.0F;
    }

    [[nodiscard]] std::expected<std::string, common::audio::ToneAutomationError>
    formatParameterValue(
        const std::string&, const std::string&, const std::string&, float norm_value) const override
    {
        return ("[" + juce::String(norm_value, 2) + "]").toStdString();
    }

    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> parseParameterValue(
        const std::string&, const std::string&, const std::string&,
        const std::string& text) const override
    {
        return juce::String{text}.retainCharacters("0123456789.-").getFloatValue();
    }

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return m_device_manager;
    }

    [[nodiscard]] std::expected<
        common::audio::DeviceRestoreOutcome, common::audio::AudioDeviceConfigurationError>
    restoreSerializedDeviceState(const std::string&) override
    {
        return common::audio::DeviceRestoreOutcome::Opened;
    }

    [[nodiscard]] std::optional<std::string> serializedDeviceState() const override
    {
        return std::nullopt;
    }

    [[nodiscard]] bool deviceStateMatchesActive(const std::string&) const override
    {
        return false;
    }

    [[nodiscard]] common::audio::AudioDeviceStatus currentDeviceStatus() const override
    {
        return {};
    }

    [[nodiscard]] std::optional<common::audio::InputDeviceIdentity> currentInputDeviceIdentity()
        const override
    {
        return std::nullopt;
    }

    void addListener(common::audio::IAudioDeviceConfiguration::Listener&) override
    {}

    void removeListener(common::audio::IAudioDeviceConfiguration::Listener&) override
    {}

    [[nodiscard]] std::expected<void, common::audio::PluginHostError> scanPluginCatalog(
        common::audio::PluginCatalogScanProgressCallback = {},
        const common::core::CancellationToken& = {}) override
    {
        return {};
    }

    [[nodiscard]] std::expected<
        std::vector<common::audio::PluginCandidate>, common::audio::PluginHostError>
    scanPluginLocations(
        const std::vector<std::filesystem::path>&,
        common::audio::PluginCatalogScanProgressCallback = {}) override
    {
        return std::vector<common::audio::PluginCandidate>{};
    }

    [[nodiscard]] std::vector<common::audio::PluginCandidate> knownPluginCatalog() const override
    {
        return {};
    }

    [[nodiscard]] std::expected<common::audio::PluginInsertResult, common::audio::PluginHostError>
    insertPlugin(const common::audio::PluginCandidate&, std::size_t) override
    {
        return common::audio::PluginInsertResult{};
    }

    [[nodiscard]] std::expected<common::audio::PluginChainSnapshot, common::audio::PluginHostError>
    movePlugin(const std::string&, std::size_t) override
    {
        return common::audio::PluginChainSnapshot{};
    }

    [[nodiscard]] std::expected<common::audio::PluginChainSnapshot, common::audio::PluginHostError>
    removePlugin(const std::string&) override
    {
        return common::audio::PluginChainSnapshot{};
    }

    [[nodiscard]] std::expected<common::audio::PluginInstanceState, common::audio::PluginHostError>
    capturePluginState(const std::string&) override
    {
        return common::audio::PluginInstanceState{};
    }

    [[nodiscard]] std::expected<common::audio::PluginChainSnapshot, common::audio::PluginHostError>
    recreatePluginStatePreservingId(const common::audio::PluginInstanceState&, std::size_t) override
    {
        return common::audio::PluginChainSnapshot{};
    }

    [[nodiscard]] std::expected<void, common::audio::PluginHostError> setPluginState(
        const std::string&, const common::audio::PluginInstanceState&) override
    {
        return {};
    }

    void flushPendingPluginEdits() override
    {}

    [[nodiscard]] bool hasPendingPluginEdits() const override
    {
        return false;
    }

    void setPluginEditObserver(common::audio::PluginEditObserver) override
    {}

    void setPluginStateEditObserver(common::audio::PluginStateEditObserver) override
    {}

    void setPluginWindowCommandObserver(common::audio::PluginWindowCommandObserver) override
    {}

    [[nodiscard]] std::expected<void, common::audio::PluginHostError> openPluginWindow(
        const std::string&) override
    {
        return {};
    }

    [[nodiscard]] std::expected<common::audio::LiveRigSnapshot, common::audio::LiveRigError>
    captureActiveRig(const common::audio::LiveRigCaptureRequest&) override
    {
        return common::audio::LiveRigSnapshot{};
    }

    [[nodiscard]] std::expected<std::string, common::audio::LiveRigError> mintEmptyTone(
        const std::filesystem::path&) override
    {
        return std::string{"tones/2f0e9d8c-7b6a-4c5d-8e9f-0a1b2c3d4e5f/tone.json"};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveRigError> addEmptyToneBranch(
        const std::string&) override
    {
        return {};
    }

    void loadLiveRig(
        common::audio::LiveRigLoadRequest,
        common::audio::LiveRigLoadResultCallback completion) override
    {
        completion(common::audio::LiveRigLoadResult{});
    }

    [[nodiscard]] std::expected<void, common::audio::LiveRigError> clearLiveRig() override
    {
        return {};
    }

    [[nodiscard]] std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
    setAudibleTone(const std::string&) override
    {
        return common::audio::LiveRigLoadResult{};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveRigError> exportAudibleTone(
        const common::audio::ToneFileExportRequest&) override
    {
        return {};
    }

    [[nodiscard]] std::expected<common::audio::AudibleToneState, common::audio::LiveRigError>
    captureAudibleToneState() override
    {
        return common::audio::AudibleToneState{};
    }

    void replaceAudibleToneFromFile(
        common::audio::ToneFileReplaceRequest,
        common::audio::LiveRigLoadResultCallback completion) override
    {
        completion(common::audio::LiveRigLoadResult{});
    }

    [[nodiscard]] std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
    restoreAudibleToneState(const common::audio::AudibleToneState&) override
    {
        return common::audio::LiveRigLoadResult{};
    }

    [[nodiscard]] common::audio::Gain outputGain() const override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveRigError> setOutputGain(
        common::audio::Gain) override
    {
        return {};
    }

    [[nodiscard]] common::audio::Gain inputGain() const override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setInputGain(
        common::audio::Gain) override
    {
        return {};
    }

    [[nodiscard]] common::audio::AudioMeterLevel rawInputMeterLevel() const override
    {
        return {};
    }

    [[nodiscard]] bool liveInputMonitoringEnabled() const override
    {
        return false;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError> setLiveInputMonitoringEnabled(
        bool) override
    {
        return {};
    }

    [[nodiscard]] bool calibrationInputMonitoringEnabled() const override
    {
        return false;
    }

    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    setCalibrationInputMonitoringEnabled(bool) override
    {
        return {};
    }

    [[nodiscard]] common::audio::AudioMeterSnapshot audioMeterSnapshot() const override
    {
        return {};
    }

private:
    juce::AudioDeviceManager m_device_manager{};
};

} // namespace

// Verifies Editor owns the concrete view and pushes initial controller state during construction.
TEST_CASE("Editor constructs a wired editor view", "[ui][editor]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransport transport;
    common::audio::testing::ConfigurableSongAudio song_audio;
    RecordingThumbnailFactory thumbnail_factory;
    FakeEditorAudioPorts audio_ports;
    core::testing::NullEditorSettings settings;
    core::testing::ImmediateEditorTaskRunner task_runner;
    core::testing::ImmediateMessageThreadScheduler message_thread_scheduler;
    common::audio::testing::InMemoryAudioConfigStore audio_config_store;
    common::audio::LiveInputMonitor live_input_monitor{
        audio_ports, audio_ports, audio_config_store
    };

    Editor editor{
        Editor::AudioPorts{
            .transport = transport,
            .song_audio = song_audio,
            .thumbnail_factory = thumbnail_factory,
            .audio_devices = audio_ports,
            .plugin_host = audio_ports,
            .live_rig = audio_ports,
            .tone_automation = audio_ports,
            .live_input = audio_ports,
            .meter_source = audio_ports,
            .playback_clock = audio_ports,
        },
        Editor::Services{
            .settings = settings,
            .task_runner = task_runner,
            .message_thread_scheduler = message_thread_scheduler,
            .audio_config_store = audio_config_store,
            .live_input_monitor = live_input_monitor,
        },
        [] {}
    };
    auto& component = editor.component();

    CHECK(dynamic_cast<EditorView*>(&component) != nullptr);
    auto& menu_bar = findRequiredDirectChild<juce::MenuBarComponent>(component, "file_menu_bar");
    CHECK(menu_bar.isVisible());
    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "arrangement_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 0);
    CHECK(song_audio.set_active_arrangement_call_count == 0);
    CHECK_FALSE(song_audio.last_active_audio_asset.has_value());
    CHECK(transport.listeners.size() == 1);
}

} // namespace rock_hero::editor::ui
