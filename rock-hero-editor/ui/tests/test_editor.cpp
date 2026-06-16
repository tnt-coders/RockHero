#include "editor_view.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_audio_meter_source.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/i_plugin_host.h>
#include <rock_hero/common/audio/i_song_audio.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/input_calibration_state.h>
#include <rock_hero/common/audio/testing/configurable_song_audio.h>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <rock_hero/editor/core/i_editor_settings.h>
#include <rock_hero/editor/core/i_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_editor_task_runner.h>
#include <rock_hero/editor/core/testing/immediate_message_thread_scheduler.h>
#include <rock_hero/editor/core/testing/null_editor_settings.h>
#include <rock_hero/editor/ui/editor.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
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
                                   public common::audio::IAudioMeterSource
{
public:
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept override
    {
        return m_device_manager;
    }

    [[nodiscard]] std::expected<void, common::audio::AudioDeviceConfigurationError>
    restoreSerializedDeviceState(const std::string&) override
    {
        return {};
    }

    [[nodiscard]] std::optional<std::string> serializedDeviceState() const override
    {
        return std::nullopt;
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

    [[nodiscard]] std::expected<void, common::audio::PluginHostError> setPluginParameterValue(
        const std::string&, const std::string&, int, double) override
    {
        return {};
    }

    void flushPendingPluginParameterEdits() override
    {}

    [[nodiscard]] bool hasPendingPluginParameterEdits() const override
    {
        return false;
    }

    void setPluginParameterEditObserver(common::audio::PluginParameterEditObserver) override
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

    Editor editor{
        Editor::AudioPorts{
            .transport = transport,
            .song_audio = song_audio,
            .thumbnail_factory = thumbnail_factory,
            .audio_devices = audio_ports,
            .plugin_host = audio_ports,
            .live_rig = audio_ports,
            .live_input = audio_ports,
            .meter_source = audio_ports,
        },
        Editor::Services{
            .settings = settings,
            .task_runner = task_runner,
            .message_thread_scheduler = message_thread_scheduler,
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
