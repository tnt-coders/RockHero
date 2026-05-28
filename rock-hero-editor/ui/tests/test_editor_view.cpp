#include "editor_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/i_audio_meter_source.h>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Returns NaN for empty optionals so Approx checks fail without unchecked optional access.
template <typename Value>
[[nodiscard]] Value optionalValueForApprox(const std::optional<Value>& value)
{
    return value.value_or(std::numeric_limits<Value>::quiet_NaN());
}

// Records editor intents emitted by EditorView child widgets.
class FakeEditorController final : public core::IEditorController
{
public:
    // Captures the file selected by the Open menu command.
    void onOpenRequested(std::filesystem::path file) override
    {
        last_open_file = std::move(file);
        open_request_count += 1;
    }

    // Captures the file selected by the Import menu command.
    void onImportRequested(std::filesystem::path file) override
    {
        last_import_file = std::move(file);
        import_request_count += 1;
    }

    // Counts Save menu command dispatches.
    void onSaveRequested() override
    {
        save_request_count += 1;
    }

    // Captures the destination selected by the Save As menu flow.
    void onSaveAsRequested(std::filesystem::path file) override
    {
        last_save_as_file = std::move(file);
        save_as_request_count += 1;
    }

    // Captures the destination selected by the Publish menu flow.
    void onPublishRequested(std::filesystem::path file) override
    {
        last_publish_file = std::move(file);
        publish_request_count += 1;
    }

    // Counts Save As cancellation notifications from the view.
    void onSaveAsCancelled() override
    {
        save_as_cancel_count += 1;
    }

    // Counts Close menu command dispatches.
    void onCloseRequested() override
    {
        close_request_count += 1;
    }

    // Counts Exit menu command dispatches.
    void onExitRequested() override
    {
        exit_request_count += 1;
    }

    // Captures prompt decisions selected through the unsaved-changes dialog.
    void onUnsavedChangesDecision(core::UnsavedChangesDecision decision) override
    {
        last_unsaved_changes_decision = decision;
        unsaved_changes_decision_count += 1;
    }

    // Captures interrupted-restore prompt decisions emitted by the view.
    void onRestoreInterruptedDecision(core::RestoreInterruptedDecision decision) override
    {
        last_restore_interrupted_decision = decision;
        restore_interrupted_decision_count += 1;
    }

    // Counts play/pause intents emitted by keyboard or transport controls.
    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    // Counts stop intents emitted by transport controls.
    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    // Captures the normalized timeline click emitted by waveform hit testing.
    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    // Counts plugin-browser open intents emitted by the signal-chain panel.
    void onPluginBrowserRequested() override
    {
        plugin_browser_request_count += 1;
    }

    // Counts plugin-browser close intents emitted by the browser window.
    void onPluginBrowserClosed() override
    {
        plugin_browser_close_count += 1;
    }

    // Counts plugin-catalog scan intents emitted by the browser window.
    void onPluginCatalogScanRequested() override
    {
        plugin_catalog_scan_request_count += 1;
    }

    // Captures plugin IDs selected in the browser window.
    void onAddPluginRequested(std::string plugin_id) override
    {
        last_plugin_id = std::move(plugin_id);
        plugin_add_request_count += 1;
    }

    // Captures plugin instances selected through the signal-chain panel.
    void onRemovePluginRequested(std::string instance_id) override
    {
        last_removed_plugin_instance_id = std::move(instance_id);
        remove_plugin_request_count += 1;
    }

    // Captures plugin instances selected for editor-window opening.
    void onOpenPluginRequested(std::string instance_id) override
    {
        last_opened_plugin_instance_id = std::move(instance_id);
        open_plugin_request_count += 1;
    }

    // Counts manual input calibration requests emitted by the signal-chain panel.
    void onInputCalibrationRequested() override
    {
        input_calibration_request_count += 1;
    }

    // Records calibration measurement setup through the controller contract.
    [[nodiscard]] std::expected<void, common::audio::LiveInputError>
    onInputCalibrationMeasurementStarted() override
    {
        input_calibration_measurement_start_count += 1;
        return {};
    }

    // Records calibration measurement cancellation through the controller contract.
    void onInputCalibrationMeasurementCancelled() override
    {
        input_calibration_measurement_cancel_count += 1;
    }

    // Records calibration completion through the controller contract.
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationSucceeded(
        double gain_db) override
    {
        last_input_calibration_gain_db = gain_db;
        input_calibration_success_count += 1;
        return {};
    }

    // Records manual calibration completion through the controller contract.
    [[nodiscard]] std::expected<void, common::audio::LiveInputError> onInputCalibrationManuallySet(
        double gain_db) override
    {
        last_input_calibration_gain_db = gain_db;
        input_calibration_manual_set_count += 1;
        return {};
    }

    // Counts dismissed input calibration prompts.
    void onInputCalibrationDismissed() override
    {
        input_calibration_dismiss_count += 1;
    }

    // Records output gain change intents emitted by the signal-chain panel.
    void onOutputGainChanged(double gain_db) override
    {
        last_output_gain_db = gain_db;
        output_gain_change_count += 1;
    }

    // Records audio-device change scheduling so EditorView tests stay agnostic of busy overlay
    // mechanics. The settings flow's own tests cover the dispatcher behavior end-to-end.
    void onAudioDeviceChangeRequested(std::function<void()> change_audio_device) override
    {
        last_audio_device_change = std::move(change_audio_device);
        audio_device_change_request_count += 1;
    }

    // Counts audio-device settings open notifications.
    void onAudioDeviceSettingsOpened() override
    {
        audio_device_settings_open_count += 1;
    }

    // Counts audio-device settings close notifications.
    void onAudioDeviceSettingsClosed() override
    {
        audio_device_settings_close_count += 1;
    }

    // Last file passed to onOpenRequested().
    std::optional<std::filesystem::path> last_open_file{};

    // Last file passed to onImportRequested().
    std::optional<std::filesystem::path> last_import_file{};

    // Last destination passed to onSaveAsRequested().
    std::optional<std::filesystem::path> last_save_as_file{};

    // Last destination passed to onPublishRequested().
    std::optional<std::filesystem::path> last_publish_file{};

    // Last normalized timeline click emitted by the view.
    std::optional<double> last_normalized_x{};

    // Last plugin ID selected through the plugin browser.
    std::optional<std::string> last_plugin_id{};

    // Last plugin instance ID selected through the signal-chain panel.
    std::optional<std::string> last_removed_plugin_instance_id{};

    // Last plugin instance ID selected for editor-window opening.
    std::optional<std::string> last_opened_plugin_instance_id{};

    // Last unsaved-changes decision emitted by the view.
    std::optional<core::UnsavedChangesDecision> last_unsaved_changes_decision{};

    // Last interrupted-restore decision emitted by the view.
    std::optional<core::RestoreInterruptedDecision> last_restore_interrupted_decision{};

    // Number of open intents received.
    int open_request_count{0};

    // Number of import intents received.
    int import_request_count{0};

    // Number of save intents received.
    int save_request_count{0};

    // Number of Save As intents received.
    int save_as_request_count{0};

    // Number of publish intents received.
    int publish_request_count{0};

    // Number of Save As cancellation intents received.
    int save_as_cancel_count{0};

    // Number of close intents received.
    int close_request_count{0};

    // Number of exit intents received.
    int exit_request_count{0};

    // Number of unsaved-changes decisions received.
    int unsaved_changes_decision_count{0};

    // Number of interrupted-restore decisions received.
    int restore_interrupted_decision_count{0};

    // Number of play/pause intents received.
    int play_pause_press_count{0};

    // Number of stop intents received.
    int stop_press_count{0};

    // Number of waveform-click intents received.
    int waveform_click_count{0};

    // Number of plugin-browser open intents received.
    int plugin_browser_request_count{0};

    // Number of plugin-browser close intents received.
    int plugin_browser_close_count{0};

    // Number of plugin-catalog scan intents received.
    int plugin_catalog_scan_request_count{0};

    // Number of browser plugin-add intents received.
    int plugin_add_request_count{0};

    // Number of remove-plugin intents received.
    int remove_plugin_request_count{0};

    // Number of open-plugin intents received.
    int open_plugin_request_count{0};

    // Last input calibration gain value emitted by the calibration popup.
    std::optional<double> last_input_calibration_gain_db{};

    // Last output gain value emitted by the signal-chain panel.
    std::optional<double> last_output_gain_db{};

    // Number of input calibration intents received.
    int input_calibration_request_count{0};
    int input_calibration_measurement_start_count{0};
    int input_calibration_measurement_cancel_count{0};
    int input_calibration_success_count{0};
    int input_calibration_manual_set_count{0};
    int input_calibration_dismiss_count{0};

    // Number of output gain change intents received.
    int output_gain_change_count{0};

    // Last audio-device change callback handed to onAudioDeviceChangeRequested; tests can invoke it
    // directly to simulate the editor's busy overlay paint fence firing.
    std::function<void()> last_audio_device_change{};

    // Number of audio-device change requests received.
    int audio_device_change_request_count{0};

    // Number of audio-device settings lifecycle notifications received.
    int audio_device_settings_open_count{0};
    int audio_device_settings_close_count{0};
};

// Fake transport gives the cursor path a position source without exposing Engine.
class FakeTransport final : public common::audio::ITransport
{
public:
    // No-op because EditorView only reads position and never controls transport directly.
    void play() override
    {}

    // No-op because EditorView only emits controller intents.
    void pause() override
    {}

    // No-op because EditorView only emits controller intents.
    void stop() override
    {}

    // Records a manual position for cursor mapping tests.
    void seek(common::core::TimePosition position_value) override
    {
        current_position = position_value;
    }

    // Returns the manually controlled transport state.
    [[nodiscard]] common::audio::TransportState state() const noexcept override
    {
        return current_state;
    }

    // Counts current-position reads used by cursor rendering.
    [[nodiscard]] common::core::TimePosition position() const noexcept override
    {
        position_read_count += 1;
        return current_position;
    }

    // No-op because these tests do not exercise transport listener wiring.
    void addListener(Listener& /*listener*/) override
    {}

    // No-op because these tests do not exercise transport listener wiring.
    void removeListener(Listener& /*listener*/) override
    {}

    // Coarse transport state returned by state().
    common::audio::TransportState current_state{};

    // Current cursor position returned by position().
    common::core::TimePosition current_position{};

    // Number of current-position reads performed by the view.
    mutable int position_read_count{0};
};

// Supplies deterministic meter snapshots to EditorView without constructing Engine.
class FakeAudioMeterSource final : public common::audio::IAudioMeterSource
{
public:
    // Returns the currently configured snapshot and records that the view sampled it.
    [[nodiscard]] common::audio::AudioMeterSnapshot audioMeterSnapshot() const override
    {
        snapshot_read_count += 1;
        return snapshot;
    }

    // Snapshot returned from audioMeterSnapshot().
    common::audio::AudioMeterSnapshot snapshot{};

    // Number of snapshots read by the view.
    mutable int snapshot_read_count{0};
};

// Records thumbnail source updates installed through the arrangement view owned by EditorView.
class FakeThumbnail final : public common::audio::IThumbnail
{
public:
    // Records the thumbnail source applied through EditorView state projection.
    void setSource(const common::core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        has_source = true;
        set_source_call_count += 1;
    }

    // Reports whether setSource() supplied drawable source data.
    [[nodiscard]] bool hasSource() const override
    {
        return has_source;
    }

    // Reports that this fake never performs asynchronous proxy generation.
    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    // Reports fixed proxy progress for the synchronous fake.
    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    // Accepts draw requests so EditorView tests can focus on state and layout.
    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/,
        common::core::TimeRange /*visible_range*/, float /*vertical_zoom*/) override
    {
        return true;
    }

    // Last thumbnail source supplied by the view.
    std::optional<common::core::AudioAsset> last_source{};

    // Number of source assignments received.
    int set_source_call_count{0};

    // Source-readiness flag returned by hasSource().
    bool has_source{false};
};

// Creates fake thumbnails while recording the owner component passed by EditorView.
class FakeThumbnailFactory final : public common::audio::IThumbnailFactory
{
public:
    // Creates a fake thumbnail and records the component that requested it.
    [[nodiscard]] std::unique_ptr<common::audio::IThumbnail> createThumbnail(
        juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        last_thumbnail = thumbnail.get();
        return thumbnail;
    }

    // Last component that requested a thumbnail.
    juce::Component* last_owner{nullptr};

    // Last fake thumbnail returned to the view.
    FakeThumbnail* last_thumbnail{nullptr};

    // Number of thumbnails created by the factory.
    int create_call_count{0};
};

// Recursively searches a component tree because viewport-hosted children are nested.
[[nodiscard]] juce::Component* findChildRecursive(juce::Component& parent, const juce::String& id)
{
    if (parent.getComponentID() == id)
    {
        return &parent;
    }

    for (int index = 0; index < parent.getNumChildComponents(); ++index)
    {
        auto* const child = parent.getChildComponent(index);
        if (child == nullptr)
        {
            continue;
        }

        if (auto* const matched_child = findChildRecursive(*child, id); matched_child != nullptr)
        {
            return matched_child;
        }
    }

    return nullptr;
}

// Returns a required descendant component by id and type, failing the current test if missing.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredChild(juce::Component& parent, const juce::String& id)
{
    auto* child = findChildRecursive(parent, id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing child component: " + id.toStdString()};
    }

    auto* typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{"Child component has unexpected type: " + id.toStdString()};
    }

    return *typed_child;
}

// Returns a required desktop-level component by id and type for popups outside the view tree.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredTopLevelComponent(const juce::String& id)
{
    for (int index = 0; index < juce::TopLevelWindow::getNumTopLevelWindows(); ++index)
    {
        juce::TopLevelWindow* const window = juce::TopLevelWindow::getTopLevelWindow(index);
        if (window == nullptr || window->getComponentID() != id)
        {
            continue;
        }

        auto* typed_window = dynamic_cast<ComponentType*>(window);
        if (typed_window == nullptr)
        {
            throw std::runtime_error{"Unexpected top-level component type: " + id.toStdString()};
        }
        return *typed_window;
    }

    throw std::runtime_error{"Missing top-level component: " + id.toStdString()};
}

// Returns the play/pause button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    auto* button =
        dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("play_pause_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the stop button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("stop_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

// Synthesizes a simple left-button mouse-down event relative to one component.
[[nodiscard]] juce::MouseEvent makeMouseDownEvent(juce::Component& component, float x, float y)
{
    const auto position = juce::Point<float>{x, y};
    const auto event_time = juce::Time::getCurrentTime();

    return {
        juce::Desktop::getInstance().getMainMouseSource(),
        position,
        juce::ModifierKeys::leftButtonModifier,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &component,
        &component,
        event_time,
        position,
        event_time,
        1,
        false
    };
}

// Builds arrangement view state for editor-view tests that need thumbnail source propagation.
[[nodiscard]] core::ArrangementViewState makeArrangementState(
    std::filesystem::path path, double duration_seconds = 4.0)
{
    return core::ArrangementViewState{
        .audio_asset = common::core::AudioAsset{std::move(path)},
        .audio_duration = common::core::TimeDuration{duration_seconds},
    };
}

// Builds a loaded editor state with a full-source timeline for viewport layout tests.
[[nodiscard]] core::EditorViewState makeLoadedEditorState(double duration_seconds)
{
    return core::EditorViewState{
        .open_enabled = true,
        .import_enabled = true,
        .save_enabled = true,
        .save_as_enabled = true,
        .publish_enabled = true,
        .suggested_publish_file = std::filesystem::path{"song.rock"},
        .close_enabled = true,
        .project_loaded = true,
        .save_requires_destination = false,
        .transport =
            core::TransportViewState{
                .play_pause_enabled = true,
                .stop_enabled = false,
                .play_pause_shows_pause_icon = false,
            },
        .audio_devices_available = false,
        .visible_timeline =
            common::core::TimeRange{
                .start = common::core::TimePosition{},
                .end = common::core::TimePosition{duration_seconds},
            },
        .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}, duration_seconds),
        .signal_chain =
            core::SignalChainViewState{
                .add_plugin_enabled = true,
                .plugins = {},
            },
        .unsaved_changes_prompt = std::nullopt,
        .save_as_prompt = std::nullopt,
        .busy = std::nullopt,
    };
}

// Returns the default timeline-canvas height after reserving the horizontal scrollbar.
[[nodiscard]] int defaultUsableTrackViewportHeight(const juce::Viewport& viewport)
{
    return 720 - viewport.getScrollBarThickness();
}

// Returns the fixed track row height that allows three tracks at the default size.
[[nodiscard]] int defaultTrackHeight(const juce::Viewport& viewport)
{
    return defaultUsableTrackViewportHeight(viewport) / 3;
}

// Returns a plain menu item by id; these are not JUCE command-manager-backed items.
[[nodiscard]] juce::PopupMenu::Item requiredMenuItem(const juce::PopupMenu& menu, int item_id)
{
    juce::PopupMenu::MenuItemIterator iterator{menu, true};
    while (iterator.next())
    {
        const juce::PopupMenu::Item& item = iterator.getItem();
        if (item.itemID == item_id)
        {
            return item;
        }
    }

    throw std::runtime_error{"Missing menu item"};
}

} // namespace

// Verifies the arrangement thumbnail is created and later pointed at pushed audio.
TEST_CASE("EditorView applies arrangement audio to the thumbnail", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;

    EditorView view{controller, transport, thumbnail_factory};

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "arrangement_view");
    REQUIRE(thumbnail_factory.last_thumbnail != nullptr);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 0);

    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = false,
            .save_as_enabled = false,
            .publish_enabled = false,
            .suggested_publish_file = std::filesystem::path{},
            .close_enabled = false,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = false,
                    .play_pause_shows_pause_icon = false,
                },
            .audio_devices_available = false,
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"full_mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .add_plugin_enabled = false,
                    .plugins = {},
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_thumbnail->set_source_call_count == 1);
}

// Verifies setState projects transition-shaped state into child widgets without reading position.
TEST_CASE("EditorView setState projects controls without polling position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    auto& menu_bar = findRequiredChild<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& controls = findRequiredChild<TransportControls>(view, "transport_controls");
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredChild<SignalChainPanel>(view, "signal_chain_panel");
    auto& add_plugin_button = findRequiredChild<juce::TextButton>(view, "add_plugin_button");
    constexpr int save_command{3};
    constexpr int close_command{5};
    constexpr int exit_command{6};
    constexpr int publish_command{7};

    view.setState(core::EditorViewState{});

    CHECK(menu_bar.isVisible());
    const juce::StringArray menu_names = view.getMenuBarNames();
    REQUIRE(menu_names.size() == 1);
    CHECK(menu_names[0] == "File");
    CHECK_FALSE(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    CHECK(controller.save_request_count == 0);
    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());
    CHECK(track_viewport.isVisible());
    CHECK(track_content.isVisible());
    CHECK_FALSE(arrangement_view.isVisible());
    CHECK_FALSE(cursor_overlay.isVisible());
    CHECK(signal_chain_panel.isVisible());
    CHECK_FALSE(add_plugin_button.isEnabled());
    CHECK(transport.position_read_count == 0);

    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = true,
                    .play_pause_shows_pause_icon = true,
                },
            .audio_devices_available = false,
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{8.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .add_plugin_enabled = true,
                    .remove_plugins_enabled = true,
                    .plugins =
                        {
                            core::PluginViewState{
                                .instance_id = "instance",
                                .plugin_id = "plugin",
                                .name = "Amp Sim",
                                .manufacturer = "Example Audio",
                                .format_name = "VST3",
                                .chain_index = 0,
                            },
                        },
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), save_command).isEnabled);
    const auto publish_item = requiredMenuItem(view.getMenuForIndex(0, "File"), publish_command);
    CHECK(publish_item.isEnabled);
    CHECK(publish_item.text == "Publish...");
    CHECK(requiredMenuItem(view.getMenuForIndex(0, "File"), close_command).isEnabled);
    view.menuItemSelected(save_command, 0);
    view.menuItemSelected(close_command, 0);
    view.menuItemSelected(exit_command, 0);
    CHECK(controller.save_request_count == 1);
    CHECK(controller.close_request_count == 1);
    CHECK(controller.exit_request_count == 1);
    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
    CHECK(add_plugin_button.isEnabled());
    REQUIRE(add_plugin_button.onClick);
    add_plugin_button.onClick();
    CHECK(controller.plugin_browser_request_count == 1);
    CHECK(findRequiredChild<juce::TextButton>(view, "remove_plugin_button_instance").isEnabled());
    CHECK(arrangement_view.isVisible());
    CHECK(cursor_overlay.isVisible());
    CHECK_FALSE(getPlayPauseButton(controls).getToggleState());
    CHECK(transport.position_read_count == 0);
}

// Verifies plugin row remove controls reflect state and emit the selected instance ID.
TEST_CASE("EditorView emits plugin remove intents", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    core::EditorViewState state;
    state.signal_chain = core::SignalChainViewState{
        .add_plugin_enabled = true,
        .remove_plugins_enabled = false,
        .plugins = {
            core::PluginViewState{
                .instance_id = "instance",
                .plugin_id = "plugin",
                .name = "Amp Sim",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };
    view.setState(state);

    CHECK_FALSE(
        findRequiredChild<juce::TextButton>(view, "remove_plugin_button_instance").isEnabled());

    state.signal_chain.remove_plugins_enabled = true;
    view.setState(state);

    auto& remove_button =
        findRequiredChild<juce::TextButton>(view, "remove_plugin_button_instance");
    CHECK(remove_button.isEnabled());
    REQUIRE(remove_button.onClick);
    remove_button.onClick();
    CHECK(controller.remove_plugin_request_count == 1);
    CHECK(controller.last_removed_plugin_instance_id == std::optional<std::string>{"instance"});
}

// Verifies plugin row clicks request opening the selected plugin editor window.
TEST_CASE("EditorView emits plugin open intents", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    core::EditorViewState state;
    state.signal_chain = core::SignalChainViewState{
        .add_plugin_enabled = true,
        .remove_plugins_enabled = true,
        .plugins = {
            core::PluginViewState{
                .instance_id = "instance",
                .plugin_id = "plugin",
                .name = "Amp Sim",
                .manufacturer = "Example Audio",
                .format_name = "VST3",
                .chain_index = 0,
            },
        },
    };
    view.setState(state);

    auto& plugin_row = findRequiredChild<juce::Component>(view, "plugin_row_instance");
    plugin_row.mouseUp(makeMouseDownEvent(plugin_row, 4.0f, 4.0f));

    CHECK(controller.open_plugin_request_count == 1);
    CHECK(controller.last_opened_plugin_instance_id == std::optional<std::string>{"instance"});
}

// Verifies the menu-bar button reflects the current audio-device status and backend availability.
TEST_CASE("EditorView projects audio device menu button state", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};
    auto& audio_button = findRequiredChild<MenuBarButton>(view, "audio_device_button");

    view.setState(core::EditorViewState{});

    CHECK_FALSE(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[audio device closed]");

    core::EditorViewState state;
    state.audio_devices_available = true;
    view.setState(state);

    CHECK(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[audio device closed]");

    state.audio_device_status_text = "[48kHz 24bit: 2/2ch 128spls ~4.5/7.5ms ASIO]";
    view.setState(state);

    CHECK(audio_button.isEnabled());
    CHECK(audio_button.getText() == "[48kHz 24bit: 2/2ch 128spls ~4.5/7.5ms ASIO]");
}

// Pins the GlyphArrangement-based width measurement that EditorView uses to size the menu-bar
// action; a wider label must report a larger preferred width than a narrower one.
TEST_CASE("MenuBarButton preferred width grows with label text", "[ui][menu-bar-button]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    constexpr int menu_strip_height{24};

    MenuBarButton button;
    button.setText("[audio device closed]");
    const int narrow_width = button.preferredWidthForHeight(menu_strip_height);

    button.setText("[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]");
    const int wide_width = button.preferredWidthForHeight(menu_strip_height);

    CHECK(narrow_width > 0);
    CHECK(wide_width > narrow_width);
}

// Verifies the File menu and audio-device action share the top strip without overlap.
TEST_CASE("EditorView lays out menu strip actions without overlap", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 500, 200);

    auto& menu_bar = findRequiredChild<juce::MenuBarComponent>(view, "file_menu_bar");
    auto& audio_button = findRequiredChild<MenuBarButton>(view, "audio_device_button");
    CHECK(menu_bar.getBounds() == juce::Rectangle<int>{0, 0, 320, 24});
    CHECK(audio_button.getBounds() == juce::Rectangle<int>{320, 0, 180, 24});

    core::EditorViewState state;
    state.audio_device_status_text = "[48kHz 24bit: 8/8ch 128spls ~5.1/8.5ms ASIO]";
    view.setState(state);

    CHECK(audio_button.getRight() == 500);
    CHECK(menu_bar.getRight() == audio_button.getX());
    CHECK(audio_button.getWidth() > 260);
}

// Verifies the full-width transport strip sits directly above the track viewport.
TEST_CASE("EditorView lays out toolbar below the menu bar", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 500, 200);

    auto& controls = findRequiredChild<TransportControls>(view, "transport_controls");
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredChild<SignalChainPanel>(view, "signal_chain_panel");
    CHECK(controls.getBounds() == juce::Rectangle<int>{8, 28, 96, 32});
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 484, 80});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 160, 484, 32});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultTrackHeight(viewport)});
    CHECK(
        cursor_overlay.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultUsableTrackViewportHeight(viewport)});
}

// Verifies the default editor size gives the viewport its planned fixed canvas dimensions.
TEST_CASE("EditorView lays out the default track viewport", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredChild<SignalChainPanel>(view, "signal_chain_panel");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1264, 472});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 552, 1264, 240});
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 1264, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

// Verifies the default zoom maps ten seconds of timeline to the canonical width.
TEST_CASE("EditorView default zoom maps ten seconds", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
    CHECK(viewport.getViewWidth() <= track_viewport.getWidth());
    CHECK(viewport.getViewHeight() < track_content.getHeight());
}

// Verifies mouse wheel zoom scales the timeline content instead of seeking transport.
TEST_CASE("EditorView wheel zoom scales track width", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    const int default_width = track_content.getWidth();

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() > default_width);
    CHECK(controller.waveform_click_count == 0);
}

// Verifies zooming all the way out can fit a long timeline into the viewport.
TEST_CASE("EditorView wheel zoom out fits full timeline", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(240.0));

    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = -100.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    CHECK(track_content.getWidth() == viewport.getViewWidth());
}

// Verifies wheel zoom uses the visible playhead cursor as the zoom center.
TEST_CASE("EditorView wheel zoom centers visible cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    transport.current_position = common::core::TimePosition{10.0};
    viewport.setViewPosition(400, 0);
    const auto initial_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(initial_cursor_x.has_value());
    const float initial_cursor_position = initial_cursor_x.value_or(0.0f);
    REQUIRE(initial_cursor_position >= static_cast<float>(viewport.getViewPositionX()));
    REQUIRE(
        initial_cursor_position <
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto zoomed_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(zoomed_cursor_x.has_value());
    const double zoomed_cursor_position = static_cast<double>(zoomed_cursor_x.value_or(0.0f));
    const double zoomed_screen_x =
        zoomed_cursor_position - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        zoomed_screen_x ==
        Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies wheel zoom scrolls to center the playhead cursor even when it starts offscreen.
TEST_CASE("EditorView wheel zoom centers offscreen cursor", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    const auto state = makeLoadedEditorState(20.0);
    view.setState(state);

    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    transport.current_position = common::core::TimePosition{15.0};
    viewport.setViewPosition(0, 0);
    const auto cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(cursor_x.has_value());
    const float cursor_position = cursor_x.value_or(0.0f);
    REQUIRE(
        cursor_position >=
        static_cast<float>(viewport.getViewPositionX() + viewport.getViewWidth()));

    track_content.mouseWheelMove(
        makeMouseDownEvent(track_content, 20.0f, 20.0f),
        juce::MouseWheelDetails{
            .deltaX = 0.0f,
            .deltaY = 1.0f,
            .isReversed = false,
            .isSmooth = false,
            .isInertial = false,
        });

    const auto zoomed_cursor_x = cursorXForTimelinePosition(
        transport.current_position, state.visible_timeline, track_content.getWidth());
    REQUIRE(zoomed_cursor_x.has_value());
    const double zoomed_cursor_position = static_cast<double>(zoomed_cursor_x.value_or(0.0f));
    const double zoomed_screen_x =
        zoomed_cursor_position - static_cast<double>(viewport.getViewPositionX());
    CHECK(
        zoomed_screen_x ==
        Catch::Approx(static_cast<double>(viewport.getViewWidth()) / 2.0).margin(1.0));
}

// Verifies Stop-command state resets the horizontal viewport without treating pause as stop.
TEST_CASE("EditorView stop reset snaps track viewport to start", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);

    auto state = makeLoadedEditorState(20.0);
    state.transport.stop_enabled = true;
    state.transport.play_pause_shows_pause_icon = true;
    transport.current_position = common::core::TimePosition{5.0};
    view.setState(state);

    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    REQUIRE(track_content.getWidth() > viewport.getViewWidth());

    viewport.setViewPosition(400, 0);
    REQUIRE(viewport.getViewPositionX() == 400);

    state.transport.play_pause_shows_pause_icon = false;
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 400);

    state.transport.stop_enabled = false;
    transport.current_position = common::core::TimePosition{};
    view.setState(state);
    CHECK(viewport.getViewPositionX() == 0);
}

// Verifies editor resizing does not scale the fixed waveform track.
TEST_CASE("EditorView keeps waveform track fixed on resize", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);
    view.setState(makeLoadedEditorState(20.0));
    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    const juce::Rectangle<int> track_bounds = arrangement_view.getBounds();
    const juce::Rectangle<int> content_bounds = track_content.getBounds();

    view.setBounds(0, 0, 1000, 500);

    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 984, 252});
    CHECK(track_content.getBounds() == content_bounds);
    CHECK(arrangement_view.getBounds() == track_bounds);
}

// Verifies larger windows extend cursor height without changing zoom-derived width.
TEST_CASE("EditorView keeps zoomed cursor width on larger viewport", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1600, 1000);
    view.setState(makeLoadedEditorState(20.0));

    auto& track_viewport = findRequiredChild<juce::Component>(view, "track_viewport");
    auto& viewport = findRequiredChild<juce::Viewport>(view, "track_viewport_scroll");
    auto& track_content = findRequiredChild<juce::Component>(view, "track_viewport_content");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& signal_chain_panel = findRequiredChild<SignalChainPanel>(view, "signal_chain_panel");
    CHECK(track_viewport.getBounds() == juce::Rectangle<int>{8, 72, 1584, 652});
    CHECK(signal_chain_panel.getBounds() == juce::Rectangle<int>{8, 732, 1584, 260});
    CHECK(
        track_content.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultUsableTrackViewportHeight(viewport)});
    CHECK(
        arrangement_view.getBounds() ==
        juce::Rectangle<int>{0, 0, 2528, defaultTrackHeight(viewport)});
    CHECK(cursor_overlay.getBounds() == track_content.getLocalBounds());
}

// Verifies editor-wide timeline clicks are forwarded to the controller.
TEST_CASE("EditorView forwards timeline clicks to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};
    view.setBounds(0, 0, 1600, 1000);
    view.setState(
        core::EditorViewState{
            .open_enabled = true,
            .import_enabled = true,
            .save_enabled = true,
            .save_as_enabled = true,
            .publish_enabled = true,
            .suggested_publish_file = std::filesystem::path{"song.rock"},
            .close_enabled = true,
            .project_loaded = true,
            .save_requires_destination = false,
            .transport =
                core::TransportViewState{
                    .play_pause_enabled = true,
                    .stop_enabled = false,
                    .play_pause_shows_pause_icon = false,
                },
            .audio_devices_available = false,
            .visible_timeline =
                common::core::TimeRange{
                    .start = common::core::TimePosition{},
                    .end = common::core::TimePosition{4.0},
                },
            .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}),
            .signal_chain =
                core::SignalChainViewState{
                    .add_plugin_enabled = true,
                    .plugins = {},
                },
            .unsaved_changes_prompt = std::nullopt,
            .save_as_prompt = std::nullopt,
            .busy = std::nullopt,
        });

    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    auto& arrangement_view = findRequiredChild<ArrangementView>(view, "arrangement_view");
    CHECK(cursor_overlay.isVisible());
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = std::floor(static_cast<float>(cursor_overlay.getWidth()) * 0.25f) + 0.5f;
    const auto click_y = static_cast<float>(cursor_overlay.getHeight() - 20);
    REQUIRE(click_y > static_cast<float>(arrangement_view.getBottom()));
    REQUIRE(click_y < static_cast<float>(cursor_overlay.getHeight()));
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, click_y));

    CHECK(controller.waveform_click_count == 1);
    const auto last_normalized_x = controller.last_normalized_x;
    REQUIRE(last_normalized_x.has_value());
    const double expected_normalized_x =
        static_cast<double>(click_x) / static_cast<double>(cursor_overlay.getWidth());
    CHECK(optionalValueForApprox(last_normalized_x) == Catch::Approx(expected_normalized_x));
}

// Verifies the focusable editor root maps keyboard play/pause to the transport intent.
TEST_CASE("EditorView forwards space key to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    CHECK(view.getWantsKeyboardFocus());
    CHECK(view.keyPressed(juce::KeyPress{juce::KeyPress::spaceKey}));
    CHECK(controller.play_pause_press_count == 1);
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    const auto midpoint_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{5.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{10.0}
        },
        201);
    REQUIRE(midpoint_cursor.has_value());
    CHECK(optionalValueForApprox(midpoint_cursor) == Catch::Approx(100.0f));

    const auto offset_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{12.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{10.0}, .end = common::core::TimePosition{14.0}
        },
        101);
    REQUIRE(offset_cursor.has_value());
    CHECK(optionalValueForApprox(offset_cursor) == Catch::Approx(50.0f));

    const auto fractional_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{1.25},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(fractional_cursor.has_value());
    CHECK(optionalValueForApprox(fractional_cursor) == Catch::Approx(31.25f));

    const auto before_start_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{-1.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(before_start_cursor.has_value());
    CHECK(optionalValueForApprox(before_start_cursor) == Catch::Approx(0.0f));

    const auto after_end_cursor = cursorXForTimelinePosition(
        common::core::TimePosition{9.0},
        common::core::TimeRange{
            .start = common::core::TimePosition{}, .end = common::core::TimePosition{4.0}
        },
        101);
    REQUIRE(after_end_cursor.has_value());
    CHECK(optionalValueForApprox(after_end_cursor) == Catch::Approx(100.0f));

    CHECK_FALSE(cursorXForTimelinePosition(
                    common::core::TimePosition{1.0},
                    common::core::TimeRange{
                        .start = common::core::TimePosition{}, .end = common::core::TimePosition{}
                    },
                    101)
                    .has_value());
}

// EditorView reveals the busy overlay when state.busy is present and hides it again when busy
// clears. The overlay is identified by its componentID so the test does not depend on the
// concrete BusyOverlay type.
TEST_CASE("EditorView shows the busy overlay while state.busy is set", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;

    EditorView view{controller, transport, thumbnail_factory};

    const juce::Component* const overlay = view.findChildWithID("busy_overlay");
    REQUIRE(overlay != nullptr);
    auto& progress_bar = findRequiredChild<juce::ProgressBar>(view, "busy_progress_bar");
    CHECK_FALSE(overlay->isVisible());

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
        .presentation = core::BusyPresentation::Animated,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    CHECK(progress_bar.isVisible());

    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Loading plugin 1 of 4: Amp Sim",
        .presentation = core::BusyPresentation::Animated,
        .progress = std::optional<double>{0.25},
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    CHECK(progress_bar.isVisible());

    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::LoadingPlugin,
        .message = "Loading plugin: Amp Sim",
        .presentation = core::BusyPresentation::Blocking,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    CHECK_FALSE(progress_bar.isVisible());

    core::EditorViewState idle_state;
    idle_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    idle_state.arrangement = makeArrangementState(std::filesystem::path{});
    idle_state.signal_chain = core::SignalChainViewState{};
    view.setState(idle_state);

    CHECK_FALSE(overlay->isVisible());
}

// The busy-overlay fence waits for an actual overlay paint and then posts the callback once.
TEST_CASE("EditorView runs busy callback after overlay paint", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;

    EditorView view{controller, transport, thumbnail_factory};
    juce::Component* const overlay = view.findChildWithID("busy_overlay");
    REQUIRE(overlay != nullptr);

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::LoadingPlugin,
        .message = "Loading plugin...",
        .presentation = core::BusyPresentation::Blocking,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    int callback_count = 0;
    view.runAfterBusyOverlayPainted([&callback_count] { callback_count += 1; });

    const juce::Image image{juce::Image::RGB, 320, 200, true};
    juce::Graphics graphics{image};
    overlay->paint(graphics);

    CHECK(callback_count == 1);

    overlay->paint(graphics);
    CHECK(callback_count == 1);
}

// A hidden view cannot satisfy a paint fence; startup restore must continue instead of waiting.
TEST_CASE("EditorView runs busy callback when hidden", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;

    EditorView view{controller, transport, thumbnail_factory};

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
        .presentation = core::BusyPresentation::Blocking,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    int callback_count = 0;
    view.runAfterBusyOverlayPainted([&callback_count] { callback_count += 1; });

    CHECK(callback_count == 1);
}

// Verifies that input calibration and output gain controls exist and are disabled by default.
TEST_CASE("Signal-chain controls present and disabled by default", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    auto& calibrate_button = findRequiredChild<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredChild<juce::Slider>(view, "output_gain_slider");

    CHECK_FALSE(calibrate_button.isEnabled());
    CHECK_FALSE(output_slider.isEnabled());
    CHECK(output_slider.isDoubleClickReturnEnabled());
    CHECK(output_slider.getTextBoxPosition() == juce::Slider::TextBoxBelow);
    CHECK(output_slider.isTextBoxEditable());
    CHECK(output_slider.getTextBoxWidth() == 72);
    CHECK(output_slider.getTextBoxHeight() == 20);
    CHECK(output_slider.getMinimum() == common::audio::minimumGainDb());
    CHECK(output_slider.getMaximum() == common::audio::maximumGainDb());
    CHECK(output_slider.getDoubleClickReturnValue() == common::audio::defaultGainDb());
}

// Verifies the global and live-rig meter widgets are present in the composed editor view.
TEST_CASE("EditorView creates audio meter components", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    auto& master_meter = findRequiredChild<AudioLevelMeter>(view, "master_output_meter");
    auto& input_meter = findRequiredChild<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredChild<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(master_meter.level() == common::audio::AudioMeterLevel{});
    CHECK(input_meter.level() == common::audio::AudioMeterLevel{});
    CHECK(output_meter.level() == common::audio::AudioMeterLevel{});
}

// Verifies the signal-chain meters use the intended input and output control layout.
TEST_CASE("Signal chain meters sit with their controls", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setBounds(0, 0, 1280, 800);

    auto& calibrate_button = findRequiredChild<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredChild<juce::Slider>(view, "output_gain_slider");
    auto& input_meter = findRequiredChild<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredChild<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(input_meter.getBottom() <= calibrate_button.getY());
    CHECK(output_meter.getHeight() == input_meter.getHeight());
    CHECK(output_meter.getY() == input_meter.getY());
    CHECK(output_slider.getBottom() == calibrate_button.getBottom());
    CHECK(output_meter.getX() > output_slider.getX());
    CHECK(output_meter.getRight() <= output_slider.getRight());
    CHECK(output_meter.getX() - output_slider.getX() <= (output_slider.getWidth() / 2) + 4);
}

// Verifies EditorView samples the optional meter port and forwards the values to meter widgets.
TEST_CASE("EditorView samples audio meter source", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    FakeAudioMeterSource meter_source;
    meter_source.snapshot = common::audio::AudioMeterSnapshot{
        .live_rig_input = common::audio::AudioMeterLevel{.peak_db = -18.0},
        .live_rig_output = common::audio::AudioMeterLevel{.peak_db = -2.0, .clipping = true},
        .master_output = common::audio::AudioMeterLevel{.peak_db = -6.0},
    };
    EditorView view{controller, transport, thumbnail_factory, nullptr, &meter_source};

    view.setState(core::EditorViewState{});

    auto& master_meter = findRequiredChild<AudioLevelMeter>(view, "master_output_meter");
    auto& input_meter = findRequiredChild<AudioLevelMeter>(view, "input_meter");
    auto& output_meter = findRequiredChild<AudioLevelMeter>(view, "output_gain_meter");

    CHECK(meter_source.snapshot_read_count >= 1);
    CHECK(master_meter.level() == meter_source.snapshot.master_output);
    CHECK(input_meter.level() == meter_source.snapshot.live_rig_input);
    CHECK(output_meter.level() == meter_source.snapshot.live_rig_output);
}

// Verifies that signal-chain controls follow their independent view-state gates.
TEST_CASE("Signal-chain controls follow view-state gates", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    auto& calibrate_button = findRequiredChild<juce::TextButton>(view, "input_calibrate_button");
    auto& output_slider = findRequiredChild<juce::Slider>(view, "output_gain_slider");

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .input_calibrate_enabled = true,
                .output_gain_controls_enabled = true,
                .output_gain_db = -24.0,
            },
        });

    CHECK(calibrate_button.isEnabled());
    CHECK(output_slider.isEnabled());
    CHECK(output_slider.getValue() == -24.0);
}

// Verifies that pressing the input calibration button emits a controller intent.
TEST_CASE("Input calibration button emits controller intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .input_calibrate_enabled = true,
            },
        });

    auto& calibrate_button = findRequiredChild<juce::TextButton>(view, "input_calibrate_button");
    calibrate_button.onClick();

    CHECK(controller.input_calibration_request_count == 1);
}

// Verifies the calibration popup starts with target, status, and documentation controls.
TEST_CASE("Calibration prompt starts with target and status", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};
    view.setBounds(0, 0, 1280, 800);

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = 2.0,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    REQUIRE(window.getContentComponent() != nullptr);
    auto& target_label = findRequiredChild<juce::Label>(window, "input_calibration_target");
    auto& help_button =
        findRequiredChild<juce::DrawableButton>(window, "input_calibration_help_button");
    auto& status = findRequiredChild<juce::Label>(window, "input_calibration_status");
    auto& meter = findRequiredChild<juce::Component>(window, "input_calibration_meter");
    auto& manual_label = findRequiredChild<juce::Label>(window, "input_calibration_manual_label");
    auto& slider = findRequiredChild<juce::Slider>(window, "input_calibration_manual_gain");
    auto& start_button =
        findRequiredChild<juce::TextButton>(window, "input_calibration_start_button");
    auto& master_meter = findRequiredChild<AudioLevelMeter>(view, "master_output_meter");

    CHECK(target_label.getText() == "Target: -12 dBFS average, -6 dBFS peak");
    CHECK(
        status.getText() ==
        "Click \"Calibrate\" to run automatic calibration, or adjust gain manually and click "
        "\"Apply\".");
    CHECK(status.isVisible());
    CHECK(status.isColourSpecified(juce::Label::backgroundColourId));
    CHECK(status.getMinimumHorizontalScale() == 1.0f);
    CHECK_FALSE(status.getText().startsWith("Info:"));
    REQUIRE(help_button.onClick);
    CHECK(help_button.getTooltip() == "Open input calibration guide");
    CHECK(start_button.getButtonText() == "Calibrate");
    CHECK(manual_label.getText() == "Gain:");
    CHECK(meter.getWidth() == master_meter.getWidth());
    CHECK(window.getContentComponent()->getWidth() < 520);
    CHECK(findChildRecursive(window, "input_calibration_gain") == nullptr);
    CHECK(findChildRecursive(window, "input_calibration_recommendation") == nullptr);
    CHECK(findChildRecursive(window, "input_calibration_docs_link") == nullptr);
    CHECK(target_label.getBounds().getRight() <= help_button.getBounds().getX());
    CHECK(help_button.getBounds().getCentreY() == target_label.getBounds().getCentreY());
    CHECK(target_label.getBounds().getBottom() <= status.getBounds().getY());
    CHECK(status.getBounds().getBottom() <= meter.getBounds().getY());
    CHECK(slider.getBounds().getY() >= meter.getBounds().getBottom());
    CHECK(manual_label.getBounds().getY() == slider.getBounds().getY());
    CHECK(manual_label.getBounds().getRight() <= slider.getBounds().getX());
    CHECK(start_button.getBounds().getY() >= slider.getBounds().getBottom());
    CHECK(window.getContentComponent()->getHeight() < 235);
}

// Verifies calibration gain controls do not expose negative zero after one-decimal rounding.
TEST_CASE("Calibration gain control hides negative rounded zero", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = -0.04,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredChild<juce::Slider>(window, "input_calibration_manual_gain");

    CHECK(slider.getValue() == Catch::Approx(0.0));
    CHECK_FALSE(slider.getTextFromValue(slider.getValue()).startsWith("-0.0"));
    CHECK(findChildRecursive(window, "input_calibration_gain") == nullptr);
}

// Verifies manual gain remains adjustable after a manual calibration save.
TEST_CASE("Manual calibration stays editable after saving", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    core::EditorViewState state;
    state.input_calibration_prompt = core::InputCalibrationPrompt{
        .message = "Live input disabled: input calibration required.",
        .input_gain_db = 2.0,
    };
    view.setState(state);

    auto& window = findRequiredTopLevelComponent<juce::DocumentWindow>("input_calibration_window");
    auto& slider = findRequiredChild<juce::Slider>(window, "input_calibration_manual_gain");
    auto& apply_button =
        findRequiredChild<juce::TextButton>(window, "input_calibration_manual_apply_button");
    auto& status = findRequiredChild<juce::Label>(window, "input_calibration_status");

    slider.setValue(3.5, juce::sendNotificationSync);
    REQUIRE(apply_button.onClick);
    apply_button.onClick();

    CHECK(controller.input_calibration_manual_set_count == 1);
    CHECK(controller.last_input_calibration_gain_db == std::optional{3.5});
    CHECK(slider.isEnabled());
    CHECK(apply_button.isEnabled());
    CHECK(status.getText() == "Manual calibration saved. Gain set to 3.5 dB.");
}

// Verifies that moving the output gain slider emits a controller intent.
TEST_CASE("Output gain slider emits controller intent", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    const FakeTransport transport;
    FakeThumbnailFactory thumbnail_factory;
    EditorView view{controller, transport, thumbnail_factory};

    view.setState(
        core::EditorViewState{
            .signal_chain = core::SignalChainViewState{
                .output_gain_controls_enabled = true,
            },
        });

    auto& output_slider = findRequiredChild<juce::Slider>(view, "output_gain_slider");
    output_slider.setValue(-6.0, juce::sendNotificationSync);

    CHECK(controller.output_gain_change_count == 1);
    CHECK(controller.last_output_gain_db == std::optional{-6.0});
}

} // namespace rock_hero::editor::ui
