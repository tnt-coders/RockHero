/*!
\file editor_view_test_harness.h
\brief Shared concrete editor-view test fakes and setup helpers.
*/

#pragma once

#include "main_window/editor_view.h"

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
#include <rock_hero/common/audio/device/i_audio_device_configuration.h>
#include <rock_hero/common/audio/input/i_audio_meter_source.h>
#include <rock_hero/common/audio/input/i_live_input.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/configurable_audio_device_configuration.h>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/editor/core/testing/recording_editor_controller.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace Catch
{

/*!
\brief Prints optional gain values in assertion failures.

Catch has no default stringification for std::optional, so a failed comparison logs only
"{?} == {?}" — useless for CI-only failures where the log is the sole evidence. This prints the
held value (or "nullopt") instead.
*/
template <> struct StringMaker<std::optional<double>>
{
    static std::string convert(const std::optional<double>& value)
    {
        return value.has_value() ? StringMaker<double>::convert(*value) : "nullopt";
    }
};

} // namespace Catch

namespace rock_hero::editor::ui
{

using testing::findDescendant;
using testing::findRequiredDescendant;
using testing::makeMouseDownEvent;

/*! \brief Short alias for the shared recording thumbnail factory fake. */
using RecordingThumbnailFactory = common::audio::testing::RecordingThumbnailFactory;

/*!
\brief Returns NaN for empty optionals so Approx checks fail without unchecked optional access.
\tparam Value Numeric value type accepted by Catch2 Approx.
\param value Optional value read from view state.
\return Contained value, or quiet NaN when the optional is empty.
*/
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

/*!
\brief Supplies a default audio-device port for editor-view tests.
\return Process-lifetime configurable audio-device fake.
*/
[[nodiscard]] inline common::audio::testing::ConfigurableAudioDeviceConfiguration&
defaultAudioDevices() noexcept
{
    static common::audio::testing::ConfigurableAudioDeviceConfiguration g_audio_devices;
    return g_audio_devices;
}

/*!
\brief Supplies a default audio-meter source for editor-view tests.
\return Process-lifetime fake audio-meter source.
*/
[[nodiscard]] inline FakeAudioMeterSource& defaultAudioMeterSource() noexcept
{
    static FakeAudioMeterSource g_meter_source;
    return g_meter_source;
}

/*!
\brief Supplies a default live-input port for editor-view tests.
\return Process-lifetime fake live-input port.
*/
[[nodiscard]] inline FakeLiveInput& defaultLiveInput() noexcept
{
    static FakeLiveInput g_live_input;
    return g_live_input;
}

/*! \brief Playback clock that never publishes; the preview surface is untested-by-unit. */
struct FakePlaybackClock final : public common::audio::IPlaybackClock
{
    [[nodiscard]] common::audio::PlaybackClockSnapshot snapshot() const noexcept override
    {
        return {};
    }
};

/*!
\brief Supplies a default playback-clock port for editor-view tests.
\return Process-lifetime fake playback clock.
*/
[[nodiscard]] inline FakePlaybackClock& defaultPlaybackClock() noexcept
{
    static FakePlaybackClock g_playback_clock;
    return g_playback_clock;
}

/*! \brief Tone-automation port whose queries all resolve to empty for editor-view tests. */
struct FakeToneAutomation final : public common::audio::IToneAutomation
{
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

    // Parses a plain numeric normalised value, the inverse of the "[0.NN]" formatting above.
    [[nodiscard]] std::expected<float, common::audio::ToneAutomationError> parseParameterValue(
        const std::string&, const std::string&, const std::string&,
        const std::string& text) const override
    {
        return juce::String{text}.retainCharacters("0123456789.-").getFloatValue();
    }
};

/*!
\brief Supplies a default tone-automation port for editor-view tests.
\return Process-lifetime fake tone-automation port.
*/
[[nodiscard]] inline FakeToneAutomation& defaultToneAutomation() noexcept
{
    static FakeToneAutomation g_tone_automation;
    return g_tone_automation;
}

/*!
\brief Builds the editor view audio-port bundle used by most tests.
\param transport Transport fake used by the view under test.
\param thumbnail_factory Thumbnail factory fake used by the view under test.
\return EditorView audio-port bundle.
*/
[[nodiscard]] inline EditorView::AudioPorts viewAudioPorts(
    const FakeTransport& transport, RecordingThumbnailFactory& thumbnail_factory) noexcept
{
    return EditorView::AudioPorts{
        .transport = transport,
        .playback_clock = defaultPlaybackClock(),
        .thumbnail_factory = thumbnail_factory,
        .audio_devices = defaultAudioDevices(),
        .meter_source = defaultAudioMeterSource(),
        .live_input = defaultLiveInput(),
        .tone_automation = defaultToneAutomation(),
    };
}

/*!
\brief Builds the editor view audio-port bundle with a custom meter source.
\param transport Transport fake used by the view under test.
\param thumbnail_factory Thumbnail factory fake used by the view under test.
\param meter_source Meter-source fake used by the view under test.
\return EditorView audio-port bundle.
*/
[[nodiscard]] inline EditorView::AudioPorts viewAudioPorts(
    const FakeTransport& transport, RecordingThumbnailFactory& thumbnail_factory,
    const FakeAudioMeterSource& meter_source) noexcept
{
    return EditorView::AudioPorts{
        .transport = transport,
        .playback_clock = defaultPlaybackClock(),
        .thumbnail_factory = thumbnail_factory,
        .audio_devices = defaultAudioDevices(),
        .meter_source = meter_source,
        .live_input = defaultLiveInput(),
        .tone_automation = defaultToneAutomation(),
    };
}

/*!
\brief Returns a required desktop-level component by id and type for popups outside the view tree.
\tparam ComponentType Expected top-level JUCE component type.
\param id Component ID to find.
\return Matching top-level component.
\throws std::runtime_error when the component is missing or has the wrong type.
*/
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

/*!
\brief Returns the play/pause button from the transport-controls child.
\param controls Transport controls to inspect.
\return Play/pause button child.
\throws std::runtime_error when the child is missing.
*/
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

/*!
\brief Returns the stop button from the transport-controls child.
\param controls Transport controls to inspect.
\return Stop button child.
\throws std::runtime_error when the child is missing.
*/
[[nodiscard]] inline juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("stop_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

/*!
\brief Builds arrangement view state for editor-view tests that need thumbnail source propagation.
\param path Backing audio path assigned to the arrangement state.
\param duration_seconds Audio duration assigned to the arrangement state.
\return Arrangement view-state fixture.
*/
[[nodiscard]] inline core::ArrangementViewState makeArrangementState(
    std::filesystem::path path, double duration_seconds = 4.0)
{
    return core::ArrangementViewState{
        .audio_asset =
            common::core::AudioAsset{
                .path = std::move(path), .normalization = std::nullopt, .start_offset = {}
            },
        .audio_duration = common::core::TimeDuration{duration_seconds},
        .choices = {},
    };
}

/*!
\brief Builds a loaded editor state with a full-source timeline for viewport layout tests.
\param duration_seconds Audio duration and visible timeline end.
\return Loaded editor view-state fixture.
*/
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
        .tempo_map =
            common::core::TempoMap::defaultMap(common::core::TimeDuration{duration_seconds}),
        .arrangement = makeArrangementState(std::filesystem::path{"mix.wav"}, duration_seconds),
        .signal_chain =
            core::SignalChainViewState{
                .insert_plugin_enabled = true,
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

/*!
\brief Returns the default timeline-canvas height after reserving the horizontal scrollbar.
\param viewport Viewport whose scrollbar thickness should be reserved.
\return Default usable track viewport height in pixels.
*/
[[nodiscard]] inline int defaultUsableTrackViewportHeight(const juce::Viewport& viewport)
{
    return 720 - viewport.getScrollBarThickness();
}

/*!
\brief Returns the fixed track row height that allows three tracks at the default size.
\param viewport Viewport whose usable height should be divided into track rows.
\return Default track row height in pixels.
*/
[[nodiscard]] inline int defaultTrackHeight(const juce::Viewport& viewport)
{
    return defaultUsableTrackViewportHeight(viewport) / 3;
}

/*!
\brief Returns a plain menu item by id; these are not JUCE command-manager-backed items.
\param menu Popup menu to inspect.
\param item_id JUCE menu item ID to find.
\return Matching popup menu item.
\throws std::runtime_error when the item is missing.
*/
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
