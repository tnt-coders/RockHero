/*!
\file editor_view_test_harness.h
\brief Shared concrete editor-view test fakes and setup helpers.
*/

#pragma once

#include "editor_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <limits>
#include <optional>
#include <rock_hero/common/audio/gain.h>
#include <rock_hero/common/audio/i_audio_device_configuration.h>
#include <rock_hero/common/audio/i_audio_meter_source.h>
#include <rock_hero/common/audio/i_live_input.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

using testing::findDescendant;
using testing::findRequiredDescendant;
using testing::makeMouseDownEvent;
using RecordingThumbnailFactory = common::audio::testing::RecordingThumbnailFactory;

// Returns NaN for empty optionals so Approx checks fail without unchecked optional access.
template <typename Value>
[[nodiscard]] inline Value optionalValueForApprox(const std::optional<Value>& value)
{
    return value.value_or(std::numeric_limits<Value>::quiet_NaN());
}

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

// Minimal live-input fake used by the calibration popup boundary.
class FakeLiveInput final : public common::audio::ILiveInput
{
public:
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
        return raw_input_meter_level;
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

    common::audio::AudioMeterLevel raw_input_meter_level{};
};

[[nodiscard]] inline common::audio::testing::ConfigurableAudioDeviceConfiguration&
defaultAudioDevices() noexcept
{
    static common::audio::testing::ConfigurableAudioDeviceConfiguration audio_devices;
    return audio_devices;
}

[[nodiscard]] inline FakeAudioMeterSource& defaultAudioMeterSource() noexcept
{
    static FakeAudioMeterSource meter_source;
    return meter_source;
}

[[nodiscard]] inline FakeLiveInput& defaultLiveInput() noexcept
{
    static FakeLiveInput live_input;
    return live_input;
}

[[nodiscard]] inline EditorView::AudioPorts viewAudioPorts(
    const FakeTransport& transport, RecordingThumbnailFactory& thumbnail_factory) noexcept
{
    return EditorView::AudioPorts{
        .transport = transport,
        .thumbnail_factory = thumbnail_factory,
        .audio_devices = defaultAudioDevices(),
        .meter_source = defaultAudioMeterSource(),
        .live_input = defaultLiveInput(),
    };
}

[[nodiscard]] inline EditorView::AudioPorts viewAudioPorts(
    const FakeTransport& transport, RecordingThumbnailFactory& thumbnail_factory,
    const FakeAudioMeterSource& meter_source) noexcept
{
    return EditorView::AudioPorts{
        .transport = transport,
        .thumbnail_factory = thumbnail_factory,
        .audio_devices = defaultAudioDevices(),
        .meter_source = meter_source,
        .live_input = defaultLiveInput(),
    };
}

// Returns a required desktop-level component by id and type for popups outside the view tree.
template <class ComponentType>
[[nodiscard]] inline ComponentType& findRequiredTopLevelComponent(const juce::String& id)
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
[[nodiscard]] inline juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
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
[[nodiscard]] inline juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("stop_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

// Builds arrangement view state for editor-view tests that need thumbnail source propagation.
[[nodiscard]] inline core::ArrangementViewState makeArrangementState(
    std::filesystem::path path, double duration_seconds = 4.0)
{
    return core::ArrangementViewState{
        .audio_asset =
            common::core::AudioAsset{.path = std::move(path), .normalization = std::nullopt},
        .audio_duration = common::core::TimeDuration{duration_seconds},
    };
}

// Builds a loaded editor state with a full-source timeline for viewport layout tests.
[[nodiscard]] inline core::EditorViewState makeLoadedEditorState(double duration_seconds)
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
                .disabled_message = {},
            },
        .plugin_browser = {},
        .unsaved_changes_prompt = std::nullopt,
        .save_as_prompt = std::nullopt,
        .restore_interrupted_prompt = std::nullopt,
        .input_calibration_prompt = std::nullopt,
        .busy = std::nullopt,
    };
}

// Returns the default timeline-canvas height after reserving the horizontal scrollbar.
[[nodiscard]] inline int defaultUsableTrackViewportHeight(const juce::Viewport& viewport)
{
    return 720 - viewport.getScrollBarThickness();
}

// Returns the fixed track row height that allows three tracks at the default size.
[[nodiscard]] inline int defaultTrackHeight(const juce::Viewport& viewport)
{
    return defaultUsableTrackViewportHeight(viewport) / 3;
}

// Returns a plain menu item by id; these are not JUCE command-manager-backed items.
[[nodiscard]] inline juce::PopupMenu::Item requiredMenuItem(
    const juce::PopupMenu& menu, int item_id)
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

} // namespace rock_hero::editor::ui
