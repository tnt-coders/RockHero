#include "engine.h"

#include "audio_path_util.h"
#include "engine_impl.h"
#include "meter_reader.h"
#include "plugin_scan.h"
#include "tone_document.h"
#include "tracktion/live_rig_gain_plugin.h"
#include "tracktion/monitoring_mode_transition.h"
#include "tracktion/plugin_move_index.h"
#include "tracktion/tracktion_instrument_wave_device_mapping.h"
#include "tracktion/tracktion_thumbnail.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/core/application_identity.h>
#include <rock_hero/common/core/json.h>
#include <rock_hero/common/core/juce_path.h>
#include <rock_hero/common/core/logger.h>
#include <rock_hero/common/core/package_id.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tracktion_engine/tracktion_engine.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rock_hero::common::audio
{

// Named so the Impl declaration in engine_impl.h can reference it.
// Maps monitoring rebuild failures into plugin-host mutation errors.
[[nodiscard]] PluginHostError pluginHostErrorFromLiveInputError(const LiveInputError& error)
{
    switch (error.code)
    {
        case LiveInputErrorCode::MessageThreadRequired:
        {
            return PluginHostError{PluginHostErrorCode::MessageThreadRequired, error.message};
        }
        case LiveInputErrorCode::TrackMissing:
        {
            return PluginHostError{PluginHostErrorCode::TrackMissing, error.message};
        }
        default:
        {
            return PluginHostError{PluginHostErrorCode::MonitoringRouteFailed, error.message};
        }
    }
}

// Records recoverable instrument-route failures without turning an internal bind into a public
// error.
void logInstrumentMonitoringFailure(const juce::String& message)
{
    RH_LOG_WARNING(
        "audio.instrument_monitoring",
        "Instrument monitoring route failed detail={:?}",
        message.toStdString());
}

namespace
{

constexpr std::string_view g_plugin_scan_command_line_prefix{"--PluginScan:"};
constexpr auto g_plugin_scan_timeout = std::chrono::seconds{30};
constexpr auto g_plugin_dirty_transaction_quiet_debounce = std::chrono::milliseconds{750};

// After a programmatic state restore (undo/redo), the plugin asynchronously re-announces its
// parameters; immediate re-announces should not become user edits because that discards the redo
// stack. The window self-extends on each absorbed settle, but an already-open absorbed transaction
// that receives more dirty signals after the deadline is recorded so real user edits are not lost
// inside the longer quiet debounce.
constexpr auto g_plugin_post_restore_absorb_window = std::chrono::milliseconds{100};

[[nodiscard]] int normalizedAsciiKeyCode(int key_code) noexcept
{
    if (key_code >= 'A' && key_code <= 'Z')
    {
        return key_code - 'A' + 'a';
    }
    return key_code;
}

[[nodiscard]] bool hasCommandShortcutModifier(const juce::KeyPress& key) noexcept
{
    const juce::ModifierKeys modifiers = key.getModifiers();
    return modifiers.isCommandDown() && !modifiers.isAltDown();
}

[[nodiscard]] bool isUndoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'z';
}

[[nodiscard]] bool isRedoShortcut(const juce::KeyPress& key) noexcept
{
    return hasCommandShortcutModifier(key) && !key.getModifiers().isShiftDown() &&
           normalizedAsciiKeyCode(key.getKeyCode()) == 'y';
}

[[nodiscard]] bool isPlayPauseShortcut(const juce::KeyPress& key) noexcept
{
    return key == juce::KeyPress{juce::KeyPress::spaceKey};
}

// Converts a route-bind failure into the live-input error surface used by calibration setup.
[[nodiscard]] LiveInputError liveInputRouteUnavailable(const juce::String& message)
{
    logInstrumentMonitoringFailure(message);
    return LiveInputError{
        LiveInputErrorCode::InputRouteUnavailable,
        ("Live input route is unavailable: " + message).toStdString(),
    };
}

} // namespace

// Named (single-TU) definitions for the tracker types engine_impl.h forward-
// declares as Impl field element types.
// Observes one external plugin's parameter notifications and marks the whole plugin state dirty.
class PluginParameterDirtyTracker final : private tracktion::AutomatableParameter::Listener
{
public:
    using MarkDirty = std::function<void()>;

    PluginParameterDirtyTracker(tracktion::ExternalPlugin& plugin, MarkDirty mark_dirty)
        : m_mark_dirty(std::move(mark_dirty))
    {
        const int parameter_count = plugin.getNumAutomatableParameters();
        m_parameters.reserve(static_cast<std::size_t>(std::max(parameter_count, 0)));
        for (int index = 0; index < parameter_count; ++index)
        {
            tracktion::AutomatableParameter::Ptr parameter = plugin.getAutomatableParameter(index);
            if (parameter == nullptr)
            {
                continue;
            }

            parameter->addListener(this);
            m_parameters.push_back(std::move(parameter));
        }
    }

    ~PluginParameterDirtyTracker() override
    {
        for (const tracktion::AutomatableParameter::Ptr& parameter : m_parameters)
        {
            if (parameter != nullptr)
            {
                parameter->removeListener(this);
            }
        }
    }

    PluginParameterDirtyTracker(const PluginParameterDirtyTracker&) = delete;
    PluginParameterDirtyTracker& operator=(const PluginParameterDirtyTracker&) = delete;
    PluginParameterDirtyTracker(PluginParameterDirtyTracker&&) = delete;
    PluginParameterDirtyTracker& operator=(PluginParameterDirtyTracker&&) = delete;

private:
    void markDirty()
    {
        if (m_mark_dirty)
        {
            m_mark_dirty();
        }
    }

    void parameterChangeGestureBegin(tracktion::AutomatableParameter& /*parameter*/) override
    {
        markDirty();
    }

    void parameterChangeGestureEnd(tracktion::AutomatableParameter& /*parameter*/) override
    {
        markDirty();
    }

    void curveHasChanged(tracktion::AutomatableParameter& /*parameter*/) override
    {}

    void currentValueChanged(tracktion::AutomatableParameter& /*parameter*/) override
    {}

    void parameterChanged(
        tracktion::AutomatableParameter& /*parameter*/, float /*new_value*/) override
    {
        markDirty();
    }

    MarkDirty m_mark_dirty;
    std::vector<tracktion::AutomatableParameter::Ptr> m_parameters;
};

// Tracks the brief window after a plugin-state restore during which the plugin re-announces its
// own state. Two facts that always change together live here: the window deadline (armed and
// extended around restores) and whether the dirty transaction currently in flight began inside
// that window and should therefore be absorbed rather than emitted as an edit. Message-thread
// only; no synchronization.
class PostRestoreAbsorbWindow final
{
public:
    // Opens or extends the absorb window, measured from now.
    void arm() noexcept
    {
        m_deadline = Clock::now() + g_plugin_post_restore_absorb_window;
    }

    // Begins a dirty transaction; returns whether it started inside the absorb window.
    [[nodiscard]] bool beginTransaction() noexcept
    {
        m_transaction_absorbing = Clock::now() < m_deadline;
        return m_transaction_absorbing;
    }

    [[nodiscard]] bool isAbsorbingTransaction() const noexcept
    {
        return m_transaction_absorbing;
    }

    // Reports whether an armed window has closed. False on a never-armed window so callers cannot
    // read a default-constructed deadline as already elapsed.
    [[nodiscard]] bool hasElapsed() const noexcept
    {
        return m_deadline != Clock::time_point{} && Clock::now() >= m_deadline;
    }

    // Closes the in-flight transaction; returns whether it was still being absorbed.
    [[nodiscard]] bool finishTransaction() noexcept
    {
        return std::exchange(m_transaction_absorbing, false);
    }

    // Abandons the in-flight transaction's absorb flag without touching the window.
    void clearTransaction() noexcept
    {
        m_transaction_absorbing = false;
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_deadline{};
    bool m_transaction_absorbing{false};
};

// Owns one plugin's debounced dirty-state transaction and emits full-state edits.
class PluginDirtyStateTracker final : private tracktion::SelectableListener, private juce::Timer
{
public:
    using CaptureState = std::function<std::expected<PluginInstanceState, PluginHostError>(
        tracktion::ExternalPlugin&)>;
    using EmitEdit = std::function<void(PluginStateEdit)>;
    using PendingChanged = std::function<void()>;
    using ShouldDeferCapture = std::function<bool()>;

    PluginDirtyStateTracker(
        tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
        PendingChanged pending_changed, ShouldDeferCapture should_defer_capture,
        std::optional<PluginInstanceState> initial_baseline = std::nullopt,
        bool absorb_initial_reannounce = false)
        : m_plugin(plugin)
        , m_instance_id(plugin.itemID.toString().toStdString())
        , m_capture_state(std::move(capture_state))
        , m_emit_edit(std::move(emit_edit))
        , m_pending_changed(std::move(pending_changed))
        , m_should_defer_capture(std::move(should_defer_capture))
    {
        const bool has_initial_baseline = initial_baseline.has_value();
        if (initial_baseline.has_value())
        {
            m_baseline = std::move(initial_baseline);
        }
        else
        {
            refreshBaseline();
        }

        // Undo/redo rebuilds pass a known baseline; save capture rebuilds from the live plugin.
        // Both host-owned sync points can make plugins immediately re-announce state.
        if ((has_initial_baseline || absorb_initial_reannounce) && m_baseline.has_value())
        {
            m_post_restore_absorb.arm();
        }
        m_plugin.addSelectableListener(this);
    }

    ~PluginDirtyStateTracker() override
    {
        stopTimer();
        if (tracktion::Selectable::isSelectableValid(&m_plugin))
        {
            m_plugin.removeSelectableListener(this);
        }
    }

    PluginDirtyStateTracker(const PluginDirtyStateTracker&) = delete;
    PluginDirtyStateTracker& operator=(const PluginDirtyStateTracker&) = delete;
    PluginDirtyStateTracker(PluginDirtyStateTracker&&) = delete;
    PluginDirtyStateTracker& operator=(PluginDirtyStateTracker&&) = delete;

    [[nodiscard]] bool hasPendingEdit() const noexcept
    {
        return m_before.has_value();
    }

    [[nodiscard]] const std::string& instanceId() const noexcept
    {
        return m_instance_id;
    }

    void flushPendingEdit()
    {
        stopTimer();
        settlePendingEdit();
    }

    void markDirty()
    {
        beginPendingEdit();
    }

    // Retargets the baseline to a just-restored (undo/redo) memento and arms the absorb window so
    // the plugin's asynchronous post-restore re-announce is folded into the baseline instead of
    // recorded as a spurious user edit (which would discard the redo stack).
    void resetBaseline(PluginInstanceState baseline)
    {
        stopTimer();
        m_before.reset();
        m_post_restore_absorb.clearTransaction();
        m_baseline = std::move(baseline);
        m_post_restore_absorb.arm();
        notifyPendingChanged();
    }

private:
    [[nodiscard]] bool isCaptureDeferred() const
    {
        return m_should_defer_capture && m_should_defer_capture();
    }

    [[nodiscard]] std::expected<PluginInstanceState, PluginHostError> captureState()
    {
        if (!m_capture_state)
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginStateCaptureFailed,
                "Plugin state tracker has no capture callback",
            }};
        }

        return m_capture_state(m_plugin);
    }

    void notifyPendingChanged()
    {
        if (m_pending_changed)
        {
            m_pending_changed();
        }
    }

    void refreshBaseline()
    {
        auto captured = captureState();
        if (!captured.has_value())
        {
            RH_LOG_WARNING(
                "audio.engine",
                "Could not refresh plugin state baseline instance_id={:?} detail={:?}",
                m_instance_id,
                captured.error().message);
            m_baseline.reset();
            return;
        }

        m_baseline = std::move(*captured);
    }

    void selectableObjectChanged(tracktion::Selectable* selectable) override
    {
        if (selectable != &m_plugin || isCaptureDeferred())
        {
            return;
        }

        beginPendingEdit();
    }

    void beginPendingEdit()
    {
        if (isCaptureDeferred())
        {
            return;
        }

        // A transaction can begin as a post-restore re-announce, then receive a real user edit
        // after the short absorb window but before the longer debounce settles. Once the window has
        // elapsed, keep the transaction open but stop treating it as restore noise so the final
        // state change is emitted as a normal edit.
        if (m_before.has_value() && m_post_restore_absorb.isAbsorbingTransaction() &&
            m_post_restore_absorb.hasElapsed())
        {
            m_post_restore_absorb.clearTransaction();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin state edit left post-restore absorb window instance_id={:?} "
                "label_hint={:?}",
                m_instance_id,
                m_plugin.getName().toStdString());
        }

        if (!m_before.has_value())
        {
            if (!m_baseline.has_value())
            {
                refreshBaseline();
            }

            if (!m_baseline.has_value())
            {
                return;
            }

            m_before = *m_baseline;
            const bool absorbing = m_post_restore_absorb.beginTransaction();
            notifyPendingChanged();
            RH_LOG_INFO(
                "audio.engine",
                "Plugin state edit started instance_id={:?} label_hint={:?} absorbing={}",
                m_instance_id,
                m_plugin.getName().toStdString(),
                absorbing);
        }

        startTimer(static_cast<int>(g_plugin_dirty_transaction_quiet_debounce.count()));
    }

    void selectableObjectAboutToBeDeleted(tracktion::Selectable* selectable) override
    {
        if (selectable == &m_plugin)
        {
            stopTimer();
            m_before.reset();
            m_baseline.reset();
        }
    }

    void settlePendingEdit()
    {
        if (!m_before.has_value())
        {
            return;
        }

        auto after = captureState();
        PluginInstanceState before = std::move(*m_before);
        m_before.reset();
        const bool absorbing = m_post_restore_absorb.finishTransaction();
        if (!after.has_value())
        {
            RH_LOG_WARNING(
                "audio.engine",
                "Could not complete plugin state edit instance_id={:?} detail={:?}",
                m_instance_id,
                after.error().message);
            refreshBaseline();
            notifyPendingChanged();
            return;
        }

        m_baseline = *after;
        if (absorbing)
        {
            // Post-restore re-announce: fold it into the baseline and extend the window so the next
            // burst of the same re-announce (often triggered by this settle's own state flush) is
            // absorbed too. Never emitted as an edit.
            m_post_restore_absorb.arm();
            RH_LOG_INFO(
                "audio.engine",
                "Absorbed post-restore plugin re-announce instance_id={:?} label_hint={:?}",
                m_instance_id,
                m_plugin.getName().toStdString());
            notifyPendingChanged();
            return;
        }

        if (before != *after && m_emit_edit)
        {
            RH_LOG_INFO(
                "audio.engine",
                "Plugin state edit completed instance_id={:?} label_hint={:?}",
                m_instance_id,
                m_plugin.getName().toStdString());
            m_emit_edit(
                PluginStateEdit{
                    .instance_id = m_instance_id,
                    .before = std::move(before),
                    .after = std::move(*after),
                    .label_hint = m_plugin.getName().toStdString(),
                });
        }

        notifyPendingChanged();
    }

    void timerCallback() override
    {
        stopTimer();
        settlePendingEdit();
    }

    tracktion::ExternalPlugin& m_plugin;
    std::string m_instance_id;
    CaptureState m_capture_state;
    EmitEdit m_emit_edit;
    PendingChanged m_pending_changed;
    ShouldDeferCapture m_should_defer_capture;
    std::optional<PluginInstanceState> m_baseline;
    std::optional<PluginInstanceState> m_before;
    PostRestoreAbsorbWindow m_post_restore_absorb;
};

namespace
{

// Converts the project-owned compact channel role into the Tracktion channel identifier.
[[nodiscard]] juce::AudioChannelSet::ChannelType toTracktionChannelRole(
    InstrumentChannelRole role) noexcept
{
    switch (role)
    {
        case InstrumentChannelRole::Left:
        {
            return juce::AudioChannelSet::left;
        }
        case InstrumentChannelRole::Right:
        {
            return juce::AudioChannelSet::right;
        }
    }

    return juce::AudioChannelSet::unknown;
}

// Converts the testable Rock Hero route description into Tracktion's wave-device type.
[[nodiscard]] tracktion::WaveDeviceDescription toTracktionWaveDeviceDescription(
    const InstrumentWaveDescription& description)
{
    std::vector<tracktion::ChannelIndex> channels;
    channels.reserve(description.channels.size());

    for (const InstrumentChannelDescription& channel : description.channels)
    {
        channels.emplace_back(channel.compact_device_channel, toTracktionChannelRole(channel.role));
    }

    return tracktion::WaveDeviceDescription{
        description.name, channels.data(), static_cast<int>(channels.size()), true
    };
}

// Describes the single instrument input and stereo output that Rock Hero exposes to Tracktion.
class RockHeroEngineBehaviour final : public tracktion::EngineBehaviour
{
public:
    // Lets Engine construct its edit before explicitly opening the device manager.
    bool autoInitialiseDeviceManager() override
    {
        return false;
    }

    // Scans third-party plugins in a child process so bad scans cannot wedge the editor process.
    bool canScanPluginsOutOfProcess() override
    {
        return true;
    }

    // Rock Hero supplies compact wave-device descriptions for the staged JUCE route.
    bool isDescriptionOfWaveDevicesSupported() override
    {
        return true;
    }

    // Converts the currently open JUCE device masks into Tracktion-visible wave devices.
    void describeWaveDevices(
        std::vector<tracktion::WaveDeviceDescription>& descriptions, juce::AudioIODevice& device,
        bool is_input) override
    {
        const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
            createTracktionInstrumentWaveDeviceDescriptions(
                device.getName(),
                device.getActiveInputChannels(),
                device.getActiveOutputChannels(),
                device.getInputChannelNames(),
                device.getOutputChannelNames());
        if (!wave_descriptions.has_value())
        {
            return;
        }

        descriptions.push_back(toTracktionWaveDeviceDescription(
            is_input ? wave_descriptions->input : wave_descriptions->output));
    }

    // Creates Rock Hero-owned structural plugins from Tracktion plugin state.
    tracktion::Plugin::Ptr createCustomPlugin(tracktion::PluginCreationInfo info) override
    {
        if (info.state[tracktion::IDs::type].toString() == LiveRigGainPlugin::xmlTypeName)
        {
            // Tracktion's custom-plugin factory adopts this into Plugin::Ptr.
            return tracktion::Plugin::Ptr{new LiveRigGainPlugin{info}};
        }

        return {};
    }
};

// Top-level JUCE window that Tracktion owns through PluginWindowState::pluginWindow.
class PluginWindow final : public juce::DocumentWindow
{
public:
    // Creates a window only when Tracktion can supply a concrete plugin editor component.
    [[nodiscard]] static std::unique_ptr<juce::Component> create(
        tracktion::Plugin& plugin, PluginWindowCommandDispatcher command_dispatcher)
    {
        std::unique_ptr<tracktion::Plugin::EditorComponent> editor = plugin.createEditor();
        if (editor == nullptr)
        {
            return {};
        }

        return std::make_unique<PluginWindow>(
            plugin, std::move(editor), std::move(command_dispatcher));
    }

    // Takes ownership of Tracktion's editor component and lets plugin size changes drive bounds.
    PluginWindow(
        tracktion::Plugin& plugin, std::unique_ptr<tracktion::Plugin::EditorComponent> editor,
        PluginWindowCommandDispatcher command_dispatcher)
        : juce::DocumentWindow(
              plugin.getName(), juce::Colours::darkgrey,
              juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        , m_plugin(plugin)
        , m_window_state(*plugin.windowState)
        , m_command_dispatcher(std::move(command_dispatcher))
    {
        setUsingNativeTitleBar(true);
        setWantsKeyboardFocus(true);

        // Configure the default ResizableWindow constrainer as a backstop. setEditor() will
        // replace this with the plugin editor's own constrainer when one is supplied; these
        // values only take effect for editors that allow resizing but don't provide a
        // constrainer of their own. The onscreen amounts also guard against restored bounds
        // landing on a monitor that no longer exists â€” the title bar (top) must stay fully
        // onscreen so the user can always drag the window back.
        getConstrainer()->setMinimumOnscreenAmounts(0x10000, 50, 30, 50);
        setResizeLimits(100, 50, 4000, 4000);

        setEditor(std::move(editor));

        // Restore the full saved rectangle on within-session reopen when the editor supports
        // resizing; otherwise fall back to the editor's natural size at Tracktion's chosen
        // position. Tracktion's own choosePositionForPluginWindow() returns only a Point, so
        // size would be lost across close/reopen without this branch. If an editor returns
        // empty bounds (signaling "host, pick a size"), the default ResizableWindow
        // constrainer's setResizeLimits floor of 100x50 takes over.
        const bool editor_allows_resizing = m_editor != nullptr && m_editor->allowWindowResizing();
        if (editor_allows_resizing && m_window_state.lastWindowBounds.has_value())
        {
            setBoundsConstrained(*m_window_state.lastWindowBounds);
        }
        else
        {
            setBoundsConstrained(getLocalBounds() + m_window_state.choosePositionForPluginWindow());
        }

        m_update_stored_bounds = true;
#if JUCE_WINDOWS
        registerWindowsShortcutWindow();
#endif
    }

    PluginWindow(const PluginWindow&) = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;
    PluginWindow(PluginWindow&&) = delete;
    PluginWindow& operator=(PluginWindow&&) = delete;

    // Flushes any plugin state touched by the editor before Tracktion releases the window.
    ~PluginWindow() override
    {
#if JUCE_WINDOWS
        unregisterWindowsShortcutWindow();
#endif
        m_update_stored_bounds = false;
        m_plugin.edit.flushPluginStateIfNeeded(m_plugin);
        setEditor(nullptr);
    }

    // Recreates the plugin editor when Tracktion asks the host window to refresh its content.
    // setEditor() always clears existing state before installing the new editor, so no
    // separate setEditor(nullptr) call is needed first.
    void recreateEditor()
    {
        setEditor(m_plugin.createEditor());
    }

    // Standard Tracktion-host pattern (see external/tracktion_engine/examples/common/PluginWindow.h
    // and the default PluginWindowState::recreateWindowIfShowing): drop the current editor
    // synchronously, then recreate it on a short timer.
    //
    // Tracktion calls this hook from ExternalPlugin::forceFullReinitialise() right before it
    // tears down and replaces the underlying AudioPluginInstance. The current editor is bound to
    // the dying instance, so it must be released now; the replacement editor must be created
    // *after* forceFullReinitialise() finishes installing the new instance, which is why
    // creation is deferred onto the message loop. The "Async" suffix in
    // UIBehaviour::recreatePluginWindowContentAsync is contractual â€” Tracktion depends on this
    // being deferred.
    void recreateEditorAsync()
    {
        setEditor(nullptr);

        juce::Timer::callAfterDelay(
            50, [safe_this = juce::Component::SafePointer<PluginWindow>{this}] {
                if (auto* const window = safe_this.getComponent())
                {
                    window->recreateEditor();
                }
            });
    }

    // Routes the close button back through Tracktion so its window state stays authoritative.
    void closeButtonPressed() override
    {
        m_window_state.closeWindowExplicitly();
    }

    // Routes native system-close requests through the same Tracktion-owned close path.
    void userTriedToCloseWindow() override
    {
        closeButtonPressed();
    }

    // Plugin editors receive native scale notifications themselves; avoid double scaling the peer.
    [[nodiscard]] float getDesktopScaleFactor() const override
    {
        return 1.0F;
    }

    // Forwards shortcuts that JUCE receives before the plugin editor consumes them.
    bool keyPressed(const juce::KeyPress& key) override
    {
        if (handleCommandShortcut(key, "window"))
        {
            return true;
        }

        return juce::DocumentWindow::keyPressed(key);
    }

    // Persists the latest bounds so reopening can restore the user's window position.
    void moved() override
    {
        storeWindowBounds();
    }

    // Persists resized bounds while leaving DocumentWindow to manage the content layout.
    void resized() override
    {
        juce::DocumentWindow::resized();
        storeWindowBounds();
    }

private:
    bool handleCommandShortcut(const juce::KeyPress& key, std::string_view source)
    {
        if (isUndoShortcut(key))
        {
            postCommandShortcut(PluginWindowCommand::Undo, source);
            return true;
        }

        if (isRedoShortcut(key))
        {
            postCommandShortcut(PluginWindowCommand::Redo, source);
            return true;
        }

        if (isPlayPauseShortcut(key))
        {
            postCommandShortcut(PluginWindowCommand::PlayPause, source);
            return true;
        }

        return false;
    }

    [[nodiscard]] static std::string_view commandName(PluginWindowCommand command) noexcept
    {
        switch (command)
        {
            case PluginWindowCommand::Undo:
            {
                return "Undo";
            }
            case PluginWindowCommand::Redo:
            {
                return "Redo";
            }
            case PluginWindowCommand::PlayPause:
            {
                return "Play/Pause";
            }
        }

        return "Unknown";
    }

    void dispatchCommandShortcut(PluginWindowCommand command, std::string_view source)
    {
        if (!m_command_dispatcher)
        {
            return;
        }

        RH_LOG_INFO(
            "audio.engine",
            "Plugin window shortcut forwarded source={:?} command={:?}",
            source,
            commandName(command));
        m_command_dispatcher(command);
    }

    // The source view must reference static-storage text (call sites pass literals): the dispatch
    // runs after this call returns, and capturing the view keeps every closure member
    // nothrow-copyable so posting the message cannot leak an exception.
    void postCommandShortcut(PluginWindowCommand command, std::string_view source)
    {
        const juce::Component::SafePointer<PluginWindow> safe_this{this};
        juce::MessageManager::callAsync([safe_this, command, source] {
            if (auto* const window = safe_this.getComponent())
            {
                window->dispatchCommandShortcut(command, source);
            }
        });
    }

#if JUCE_WINDOWS
    [[nodiscard]] static bool isKeyDown(int virtual_key) noexcept
    {
        return (GetKeyState(virtual_key) & 0x8000) != 0;
    }

    // Reports whether a text field currently owns keyboard input on this thread. Frameworks that
    // accept text create a Win32 system caret on focus and destroy it on blur (native edit controls
    // by design; JUCE in textInputRequired/dismissPendingTextInput), so a live caret is a
    // cross-framework signal that the focused control wants character keys. This is the fallback
    // branch of the Space union for plugins that take keys via native hooks instead of honoring the
    // VST3 onKeyDown contract (e.g. Archetype Nolly). The gap is plugins whose text fields create no
    // system caret (e.g. Gateway), which are covered by the onKeyDown branch instead.
    [[nodiscard]] static bool textInputHasFocus() noexcept
    {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(GetCurrentThreadId(), &info) == 0)
        {
            return false;
        }

        return info.hwndCaret != nullptr;
    }

    [[nodiscard]] static std::optional<PluginWindowCommand> commandForWindowsKeyMessage(
        const MSG& message)
    {
        if (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN)
        {
            return std::nullopt;
        }

        constexpr LPARAM repeat_flag = 1LL << 30;
        if ((message.lParam & repeat_flag) != 0)
        {
            return std::nullopt;
        }

        const bool control_down = isKeyDown(VK_CONTROL);
        const bool alt_down = isKeyDown(VK_MENU);
        const bool shift_down = isKeyDown(VK_SHIFT);

        if (!control_down && !alt_down && !shift_down && message.wParam == VK_SPACE)
        {
            return PluginWindowCommand::PlayPause;
        }

        if (!control_down || alt_down || shift_down)
        {
            return std::nullopt;
        }

        if (message.wParam == 'Z')
        {
            return PluginWindowCommand::Undo;
        }

        if (message.wParam == 'Y')
        {
            return PluginWindowCommand::Redo;
        }

        return std::nullopt;
    }

    [[nodiscard]] bool ownsNativeWindow(HWND window) const
    {
        const juce::ComponentPeer* const peer = getPeer();
        if (peer == nullptr)
        {
            return false;
        }

        auto* const native_handle = static_cast<HWND>(peer->getNativeHandle());
        return native_handle != nullptr &&
               (window == native_handle || IsChild(native_handle, window) != 0);
    }

    [[nodiscard]] static PluginWindow* windowForNativeMessage(HWND window)
    {
        for (PluginWindow* const plugin_window : s_windows_hook_state.windows)
        {
            if (plugin_window != nullptr && plugin_window->ownsNativeWindow(window))
            {
                return plugin_window;
            }
        }

        return nullptr;
    }

    // Delivers a key to the hosted plugin through the VST3 keyboard contract
    // (IPlugView::onKeyDown via our JUCE fork) and reports whether the plugin accepted it. The
    // contract says a false return means the key was not consumed; buggy plugins can still violate
    // that, so this speculative delivery is intentionally limited to the Space routing path.
    [[nodiscard]] bool pluginViewHandlesKey(juce::juce_wchar character, int key_code, int modifiers)
    {
        if (auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(&m_plugin))
        {
            if (juce::AudioPluginInstance* const instance =
                    external_plugin->getAudioPluginInstance())
            {
                return instance->sendKeyDownToPluginView(character, key_code, modifiers);
            }
        }

        return false;
    }

    // How the native hook should treat a key that matches a plugin-window shortcut.
    enum class CommandKeyDisposition : std::uint8_t
    {
        FireShortcut,   // No plugin control wants the key: post the command, swallow the message.
        PluginConsumed, // The plugin handled the key via onKeyDown (already delivered): swallow.
        PassToPlugin,   // A native text field owns input: leave the message for the plugin.
    };

    // Decides how to route a shortcut key over a focused plugin window. Space yields to the plugin's
    // focused control when it wants the key -- preferring the VST3 onKeyDown contract, falling back
    // to the system caret -- so typing into plugin text fields keeps working. Undo/Redo always go to
    // Rock Hero's global undo (single source of truth) and never yield.
    [[nodiscard]] CommandKeyDisposition disposeCommandKey(PluginWindowCommand command)
    {
        if (command != PluginWindowCommand::PlayPause)
        {
            return CommandKeyDisposition::FireShortcut;
        }

        if (pluginViewHandlesKey(' ', 0, 0))
        {
            return CommandKeyDisposition::PluginConsumed;
        }

        if (textInputHasFocus())
        {
            return CommandKeyDisposition::PassToPlugin;
        }

        return CommandKeyDisposition::FireShortcut;
    }

    static LRESULT CALLBACK windowsShortcutHook(int code, WPARAM w_param, LPARAM l_param)
    {
        if (code >= 0 && w_param == PM_REMOVE)
        {
            // The WH_GETMESSAGE contract delivers the MSG through LPARAM, so this
            // integer-to-pointer reinterpret_cast is imposed by the Win32 ABI.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
            auto* const message = reinterpret_cast<MSG*>(l_param);
            if (message != nullptr)
            {
                const std::optional<PluginWindowCommand> command =
                    commandForWindowsKeyMessage(*message);
                if (command.has_value())
                {
                    if (PluginWindow* const window = windowForNativeMessage(message->hwnd);
                        window != nullptr)
                    {
                        switch (window->disposeCommandKey(*command))
                        {
                            case CommandKeyDisposition::FireShortcut:
                            {
                                window->postCommandShortcut(*command, "native_hook");
                                message->message = WM_NULL;
                                message->wParam = 0;
                                message->lParam = 0;
                                break;
                            }
                            case CommandKeyDisposition::PluginConsumed:
                            {
                                // onKeyDown already delivered the key; drop the native copy so the
                                // plugin does not receive it twice.
                                message->message = WM_NULL;
                                message->wParam = 0;
                                message->lParam = 0;
                                break;
                            }
                            case CommandKeyDisposition::PassToPlugin:
                            {
                                // A native text field owns input; leave the message for the plugin.
                                break;
                            }
                        }
                    }
                }
            }
        }

        return CallNextHookEx(s_windows_hook_state.hook, code, w_param, l_param);
    }

    void registerWindowsShortcutWindow()
    {
        s_windows_hook_state.windows.push_back(this);
        if (s_windows_hook_state.hook == nullptr)
        {
            s_windows_hook_state.hook = SetWindowsHookExW(
                WH_GETMESSAGE, windowsShortcutHook, nullptr, GetCurrentThreadId());
        }
    }

    void unregisterWindowsShortcutWindow()
    {
        std::erase(s_windows_hook_state.windows, this);
        if (s_windows_hook_state.windows.empty() && s_windows_hook_state.hook != nullptr)
        {
            UnhookWindowsHookEx(s_windows_hook_state.hook);
            s_windows_hook_state.hook = nullptr;
        }
    }

    struct WindowsHookState
    {
        HHOOK hook{};
        std::vector<PluginWindow*> windows;
    };

    // Defined after the class: clang cannot evaluate a nested class's default member initializers
    // in an inline initializer while the enclosing class is still incomplete.
    static WindowsHookState s_windows_hook_state;
#endif

    // Installs Tracktion's editor wrapper while preserving plugin-owned resize notifications.
    void setEditor(std::unique_ptr<tracktion::Plugin::EditorComponent> editor)
    {
        setConstrainer(nullptr);
        clearContentComponent();
        m_editor.reset();

        if (editor != nullptr)
        {
            m_editor = std::move(editor);
            setContentNonOwned(m_editor.get(), true);
        }

        const bool allow_window_resizing = m_editor == nullptr || m_editor->allowWindowResizing();
        setResizable(allow_window_resizing, false);

        if (m_editor != nullptr && allow_window_resizing)
        {
            setConstrainer(m_editor->getBoundsConstrainer());
        }
    }

    // Caches the latest user-driven bounds on Tracktion's window state so a closed-and-reopened
    // plugin window restores its last position within the session. Persistence across project
    // saves is handled at the project layer (see ProjectEditorState plumbing), so this path
    // intentionally does not call Edit::pluginChanged().
    void storeWindowBounds()
    {
        if (m_update_stored_bounds)
        {
            m_window_state.lastWindowBounds = getBounds();
        }
    }

    // Tracktion plugin whose editor is hosted by this window.
    tracktion::Plugin& m_plugin;

    // Tracktion owns this window and remains the source of truth for close/show state.
    tracktion::PluginWindowState& m_window_state;

    // Tracktion editor wrapper installed as the non-owned DocumentWindow content component.
    std::unique_ptr<tracktion::Plugin::EditorComponent> m_editor;

    // Routes host-window commands to the application without coupling this adapter to editor-core.
    PluginWindowCommandDispatcher m_command_dispatcher;

    // Prevents construction-time resized callbacks from overwriting Tracktion's default position.
    bool m_update_stored_bounds = false;
};

#if JUCE_WINDOWS
PluginWindow::WindowsHookState PluginWindow::s_windows_hook_state{};
#endif

// Supplies Tracktion with Rock Hero's minimal plugin editor window implementation.
class RockHeroUIBehaviour final : public tracktion::UIBehaviour
{
public:
    explicit RockHeroUIBehaviour(PluginWindowCommandDispatcher command_dispatcher)
        : m_command_dispatcher(std::move(command_dispatcher))
    {}

    // Creates windows only for normal plugin instances; rack windows will get their own UI later.
    std::unique_ptr<juce::Component> createPluginWindow(
        tracktion::PluginWindowState& window_state) override
    {
        auto* const plugin_window_state =
            dynamic_cast<tracktion::Plugin::WindowState*>(&window_state);
        if (plugin_window_state == nullptr)
        {
            return {};
        }

        return PluginWindow::create(plugin_window_state->plugin, m_command_dispatcher);
    }

    // Refreshes the editor contents without replacing Tracktion's owning plugin window.
    void recreatePluginWindowContentAsync(tracktion::Plugin& plugin) override
    {
        if (auto* const window =
                dynamic_cast<PluginWindow*>(plugin.windowState->pluginWindow.get()))
        {
            window->recreateEditorAsync();
            return;
        }

        tracktion::UIBehaviour::recreatePluginWindowContentAsync(plugin);
    }

private:
    PluginWindowCommandDispatcher m_command_dispatcher;
};

} // namespace

TransportState Engine::Impl::currentTransportState() const noexcept
{
    return TransportState{
        .playing = m_edit->getTransport().isPlaying(),
    };
}

void Engine::Impl::createEdit()
{
    m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
    auto audio_tracks = tracktion::getAudioTracks(*m_edit);
    tracktion::AudioTrack* const backing_track = audio_tracks.getFirst();

    if (backing_track != nullptr)
    {
        backing_track->setName("Backing");
        m_backing_track_id = backing_track->itemID;
    }
    else
    {
        logInstrumentMonitoringFailure("backing track was not created");
    }

    // Structural live-rig plugins are managed explicitly rather than relying on Tracktion's
    // default plugin insertion.
    constexpr bool add_default_plugins = false;
    const tracktion::AudioTrack::Ptr instrument_track = m_edit->insertNewAudioTrack(
        tracktion::TrackInsertPoint::getEndOfTracks(*m_edit), nullptr, add_default_plugins);

    if (instrument_track != nullptr)
    {
        instrument_track->setName("Instrument");
        m_instrument_track_id = instrument_track->itemID;
        if (auto structural_created = createStructuralLiveRigPlugins();
            !structural_created.has_value())
        {
            logInstrumentMonitoringFailure(toJuceString(structural_created.error().message));
        }
    }
    else
    {
        logInstrumentMonitoringFailure("instrument track was not created");
    }

    m_edit->playInStopEnabled = true;
}

void Engine::Impl::updateTransportState()
{
    const TransportState current_state = currentTransportState();
    if (m_last_notified_transport_state == current_state)
    {
        return;
    }

    // Project-owned transport listeners observe only coarse transport state. Position is
    // intentionally excluded so view code polls it at render cadence without forcing callbacks
    // on every playhead tick.
    m_last_notified_transport_state = current_state;
    m_transport_listeners.call(
        &ITransport::Listener::onTransportStateChanged, m_last_notified_transport_state);
}

void Engine::Impl::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &m_engine->getDeviceManager().deviceManager)
    {
        scheduleAudioDeviceConfigurationRefresh();
        return;
    }
    updateTransportState();
}

void Engine::Impl::scheduleAudioDeviceConfigurationRefresh()
{
    if (m_audio_device_configuration_refresh_pending)
    {
        return;
    }

    m_audio_device_configuration_refresh_pending = true;
    const std::weak_ptr<bool> alive_source{m_alive};
    const bool refresh_posted = juce::MessageManager::callAsync([this, alive = alive_source] {
        if (alive.expired())
        {
            return;
        }

        m_audio_device_configuration_refresh_pending = false;
        handleAudioDeviceConfigurationRefresh();
    });
    if (refresh_posted)
    {
        return;
    }

    m_audio_device_configuration_refresh_pending = false;
    logInstrumentMonitoringFailure("audio device refresh could not be posted");
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        handleAudioDeviceConfigurationRefresh();
    }
}

void Engine::Impl::handleAudioDeviceConfigurationRefresh()
{
    m_live_input_monitoring_enabled = false;
    m_calibration_input_monitoring_enabled = false;
    detachInstrumentMonitoringRoute();
    m_engine->getDeviceManager().dispatchPendingUpdates();
    m_audio_device_listeners.call(
        &IAudioDeviceConfiguration::Listener::onAudioDeviceConfigurationChanged);
}

void Engine::Impl::valueTreePropertyChanged(
    juce::ValueTree& /*tree*/, const juce::Identifier& property)
{
    if (property != tracktion::IDs::position)
    {
        return;
    }

    const double position_seconds = m_edit->getTransport().getPosition().inSeconds();
    if (loadedAudioEndReached(position_seconds))
    {
        stopTransport();
    }
}

double Engine::Impl::clampToLoadedRange(double seconds) const noexcept
{
    if (m_loaded_length_seconds <= 0.0)
    {
        return std::max(0.0, seconds);
    }

    return std::clamp(seconds, 0.0, m_loaded_length_seconds);
}

tracktion::AudioTrack* Engine::Impl::backingTrack() const
{
    return tracktion::findAudioTrackForID(*m_edit, m_backing_track_id);
}

tracktion::AudioTrack* Engine::Impl::instrumentTrack() const
{
    return tracktion::findAudioTrackForID(*m_edit, m_instrument_track_id);
}

std::unique_ptr<juce::PluginDescription> Engine::Impl::findKnownPlugin(
    const std::string& plugin_id) const
{
    return m_engine->getPluginManager().knownPluginList.getTypeForIdentifierString(
        juce::String{plugin_id});
}

std::expected<std::vector<PluginCandidate>, PluginHostError> Engine::Impl::
    scanPluginFileForCandidates(const std::filesystem::path& plugin_path)
{
    const std::filesystem::path scan_path = vst3DisplayPath(plugin_path).lexically_normal();
    const juce::File plugin_file = common::core::juceFileFromPath(scan_path);
    if (scan_path.empty() || !plugin_file.exists())
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::MissingPluginFile,
            "Plugin file does not exist: " + scan_path.string()
        }};
    }

    try
    {
        constexpr auto* vst3_format_name = "VST3";
        auto& plugin_manager = m_engine->getPluginManager();
        bool scan_session_finished = false;
        const auto finish_scan_session = [&plugin_manager, &scan_session_finished] {
            if (!scan_session_finished)
            {
                plugin_manager.knownPluginList.scanFinished();
                scan_session_finished = true;
            }
        };
        const juce::String& file_or_identifier = plugin_file.getFullPathName();
        juce::OwnedArray<juce::PluginDescription> found_descriptions;
        bool has_vst3_format = false;

        for (juce::AudioPluginFormat* const format :
             plugin_manager.pluginFormatManager.getFormats())
        {
            if (format == nullptr || format->getName() != vst3_format_name)
            {
                continue;
            }

            has_vst3_format = true;
            if (!format->fileMightContainThisPluginType(file_or_identifier))
            {
                continue;
            }

            PluginScanTimeout scan_timeout{
                [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                g_plugin_scan_timeout
            };
            plugin_manager.knownPluginList.scanAndAddFile(
                file_or_identifier, true, found_descriptions, *format);
            scan_timeout.finish();

            if (scan_timeout.timedOut())
            {
                plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                finish_scan_session();
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "Plugin scan timed out after " + std::to_string(g_plugin_scan_timeout.count()) +
                        " seconds: " + scan_path.string()
                }};
            }
        }

        if (!has_vst3_format)
        {
            finish_scan_session();
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::PluginScanFailed,
                "VST3 plugin hosting is not enabled in this build"
            }};
        }

        finish_scan_session();

        std::vector<PluginCandidate> plugin_candidates;
        plugin_candidates.reserve(static_cast<std::size_t>(found_descriptions.size()));

        for (const juce::PluginDescription* description : found_descriptions)
        {
            if (description != nullptr && description->pluginFormatName == vst3_format_name)
            {
                plugin_candidates.push_back(makePluginCandidate(
                    *description, pluginPathFromIdentifier(description->fileOrIdentifier)));
            }
        }

        if (plugin_candidates.empty())
        {
            return std::unexpected{PluginHostError{
                PluginHostErrorCode::NoCompatiblePlugin,
                "No VST3 plugin was found in: " + scan_path.string()
            }};
        }

        return plugin_candidates;
    }
    catch (const std::exception& error)
    {
        m_engine->getPluginManager().knownPluginList.scanFinished();
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            std::string{"Plugin scan failed: "} + error.what()
        }};
    }
}

juce::AudioPluginFormat* Engine::Impl::vst3PluginFormat() const
{
    constexpr auto* vst3_format_name = "VST3";
    for (juce::AudioPluginFormat* const format :
         m_engine->getPluginManager().pluginFormatManager.getFormats())
    {
        if (format != nullptr && format->getName() == vst3_format_name)
        {
            return format;
        }
    }

    return nullptr;
}

juce::File Engine::Impl::pluginScanDeadMansPedalFile() const
{
    return m_engine->getPropertyStorage().getAppCacheFolder().getChildFile(
        "PluginScanDeadMansPedal.txt");
}

juce::FileSearchPath Engine::Impl::pluginSearchPathFromRoots(
    const std::vector<std::filesystem::path>& roots)
{
    juce::FileSearchPath search_path;
    for (const std::filesystem::path& root : roots)
    {
        if (!root.empty())
        {
            search_path.addIfNotAlreadyThere(common::core::juceFileFromPath(root));
        }
    }

    search_path.removeRedundantPaths();
    return search_path;
}

std::filesystem::path Engine::Impl::pluginPathFromIdentifier(const juce::String& file_or_identifier)
{
    return common::core::pathFromJuceString(file_or_identifier);
}

std::expected<juce::StringArray, PluginHostError> Engine::Impl::scanVst3SearchPath(
    juce::FileSearchPath search_path, const PluginCatalogScanProgressCallback& progress_callback,
    const common::core::CancellationToken& cancel)
{
    juce::AudioPluginFormat* const format = vst3PluginFormat();
    if (format == nullptr)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            "VST3 plugin hosting is not enabled in this build"
        }};
    }

    const auto scan_started_at = std::chrono::steady_clock::now();
    search_path.removeRedundantPaths();
    juce::StringArray files = format->searchPathsForPlugins(search_path, true, true);
    files.removeEmptyStrings();
    files.removeDuplicates(true);
    const auto total_plugins = static_cast<std::size_t>(files.size());

    try
    {
        auto& plugin_manager = m_engine->getPluginManager();
        juce::PluginDirectoryScanner scanner{
            plugin_manager.knownPluginList,
            *format,
            juce::FileSearchPath{},
            true,
            pluginScanDeadMansPedalFile(),
            true
        };
        scanner.setFilesOrIdentifiersToScan(files);

        // Progress is reported before scanning so the active path names the file about to be
        // validated. Asking the scanner for the next file keeps the path and any timeout
        // message aligned with its own dead-man-pedal reordering. For VST3 the returned
        // identifier is the file path.
        for (std::size_t completed_plugins = 0; completed_plugins < total_plugins;
             ++completed_plugins)
        {
            // Stop at the next candidate boundary on cancellation. Candidates already validated
            // stay in the known-plugin list, so a cancelled scan still keeps partial progress.
            if (cancel.isCancelled())
            {
                break;
            }

            const juce::String file_or_identifier = scanner.getNextPluginFileThatWillBeScanned();
            reportPluginCatalogScanProgress(
                progress_callback,
                completed_plugins,
                total_plugins,
                pluginPathFromIdentifier(file_or_identifier));

            juce::String name_of_plugin_being_scanned;
            PluginScanTimeout scan_timeout{
                [&plugin_manager] { plugin_manager.abortCurrentPluginScan(); },
                g_plugin_scan_timeout
            };
            scanner.scanNextFile(true, name_of_plugin_being_scanned);
            scan_timeout.finish();

            if (scan_timeout.timedOut())
            {
                plugin_manager.knownPluginList.removeFromBlacklist(file_or_identifier);
                return std::unexpected{PluginHostError{
                    PluginHostErrorCode::PluginScanFailed,
                    "Plugin scan timed out after " + std::to_string(g_plugin_scan_timeout.count()) +
                        " seconds: " + file_or_identifier.toStdString()
                }};
            }
        }

        reportPluginCatalogScanProgress(progress_callback, total_plugins, total_plugins, {});
    }
    catch (const std::exception& error)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginScanFailed,
            std::string{"Plugin catalog scan failed: "} + error.what()
        }};
    }

    logPluginCatalogScanSummary(total_plugins, elapsedMilliseconds(scan_started_at));
    return files;
}

std::vector<PluginCandidate> Engine::Impl::knownPluginCatalog() const
{
    constexpr auto* vst3_format_name = "VST3";
    std::vector<PluginCandidate> plugin_candidates;
    std::unordered_set<std::string> seen_plugin_ids;
    std::unordered_set<std::string> seen_plugin_paths;
    const auto& known_types = m_engine->getPluginManager().knownPluginList.getTypes();
    plugin_candidates.reserve(static_cast<std::size_t>(known_types.size()));
    seen_plugin_ids.reserve(plugin_candidates.capacity());
    seen_plugin_paths.reserve(plugin_candidates.capacity());

    const auto append_candidate = [&plugin_candidates, &seen_plugin_ids, &seen_plugin_paths](
                                      PluginCandidate plugin_candidate) {
        const std::string path_key = normalizedPluginPathKey(plugin_candidate.file_path);
        if (seen_plugin_ids.contains(plugin_candidate.id) || seen_plugin_paths.contains(path_key))
        {
            return;
        }

        seen_plugin_ids.insert(plugin_candidate.id);
        seen_plugin_paths.insert(path_key);
        plugin_candidates.push_back(std::move(plugin_candidate));
    };

    for (const juce::PluginDescription& description : known_types)
    {
        if (description.pluginFormatName != vst3_format_name)
        {
            continue;
        }

        append_candidate(makePluginCandidate(
            description, pluginPathFromIdentifier(description.fileOrIdentifier)));
    }

    return plugin_candidates;
}

std::vector<PluginCandidate> Engine::Impl::knownPluginCatalogForScannedFiles(
    const juce::StringArray& scanned_files) const
{
    std::unordered_set<std::string> scanned_paths;
    scanned_paths.reserve(static_cast<std::size_t>(scanned_files.size()));
    for (const juce::String& file_or_identifier : scanned_files)
    {
        scanned_paths.insert(normalizedPluginPathKey(pluginPathFromIdentifier(file_or_identifier)));
    }

    std::vector<PluginCandidate> plugin_candidates;
    for (PluginCandidate plugin_candidate : knownPluginCatalog())
    {
        if (scanned_paths.contains(normalizedPluginPathKey(plugin_candidate.file_path)))
        {
            plugin_candidates.push_back(std::move(plugin_candidate));
        }
    }

    return plugin_candidates;
}

PluginChainSnapshot Engine::Impl::pluginChainSnapshot() const
{
    PluginChainSnapshot snapshot;
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return snapshot;
    }

    const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
    snapshot.plugins.reserve(static_cast<std::size_t>(plugins.size()));
    for (const tracktion::Plugin* const plugin : plugins)
    {
        if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        snapshot.plugins.push_back(makePluginChainEntry(*plugin, snapshot.plugins.size()));
    }

    return snapshot;
}

std::size_t Engine::Impl::userVisiblePluginCount(const tracktion::Plugin* ignored_plugin) const
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return 0;
    }

    std::size_t count = 0;
    for (const tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin != nullptr && plugin != ignored_plugin && !isStructuralLiveRigPlugin(plugin))
        {
            ++count;
        }
    }
    return count;
}

std::expected<int, PluginHostError> Engine::Impl::tracktionIndexForUserPluginSlot(
    std::size_t chain_index, const tracktion::Plugin* ignored_plugin) const
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::TrackMissing}};
    }

    std::size_t user_index = 0;
    for (int raw_index = 0; raw_index < instrument_track->pluginList.size(); ++raw_index)
    {
        const tracktion::Plugin* const plugin = instrument_track->pluginList[raw_index];
        if (plugin == nullptr || plugin == ignored_plugin || isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        if (user_index == chain_index)
        {
            return raw_index;
        }
        ++user_index;
    }

    if (user_index != chain_index)
    {
        return std::unexpected{PluginHostError{PluginHostErrorCode::InvalidChainIndex}};
    }

    const tracktion::Plugin* const output_gain = findStructuralGainPlugin(m_output_gain_plugin_id);
    const int output_gain_index =
        output_gain != nullptr ? instrument_track->pluginList.indexOf(output_gain) : -1;
    if (output_gain_index < 0)
    {
        return std::unexpected{PluginHostError{
            PluginHostErrorCode::PluginInsertionFailed,
            "Structural live rig output gain plugin is missing",
        }};
    }
    return output_gain_index;
}

std::optional<std::size_t> Engine::Impl::userVisiblePluginIndexOf(
    const tracktion::Plugin* target_plugin) const
{
    if (target_plugin == nullptr || isStructuralLiveRigPlugin(target_plugin))
    {
        return std::nullopt;
    }

    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::nullopt;
    }

    std::size_t user_index = 0;
    for (const tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        if (plugin == target_plugin)
        {
            return user_index;
        }
        ++user_index;
    }

    return std::nullopt;
}

bool Engine::Impl::hasKnownPluginForIdentity(const PluginIdentity& identity) const
{
    const juce::PluginDescription persisted_description = makePluginDescription(identity);
    auto& known_plugin_list = m_engine->getPluginManager().knownPluginList;
    if (!identity.juce_identifier_hint.empty() &&
        known_plugin_list
                .getTypeForIdentifierString(
                    juce::String::fromUTF8(identity.juce_identifier_hint.c_str()))
                .get() != nullptr)
    {
        return true;
    }

    if (!identity.tracktion_identifier_hint.empty() &&
        known_plugin_list
                .getTypeForIdentifierString(
                    juce::String::fromUTF8(identity.tracktion_identifier_hint.c_str()))
                .get() != nullptr)
    {
        return true;
    }

    for (const juce::PluginDescription& known_description : known_plugin_list.getTypes())
    {
        if (persisted_description.isDuplicateOf(known_description))
        {
            return true;
        }
    }

    return false;
}

std::expected<void, LiveRigError> Engine::Impl::ensureKnownPluginForIdentity(
    const PluginIdentity& identity)
{
    if (hasKnownPluginForIdentity(identity))
    {
        return {};
    }

    if (!identity.original_file_or_identifier.empty())
    {
        const std::filesystem::path plugin_path{identity.original_file_or_identifier};
        std::error_code error;
        if (std::filesystem::exists(plugin_path, error))
        {
            const auto scan_result = scanPluginFileForCandidates(plugin_path);
            if (!scan_result.has_value())
            {
                return std::unexpected{LiveRigError{
                    LiveRigErrorCode::PluginScanFailed,
                    scan_result.error().message,
                }};
            }

            if (hasKnownPluginForIdentity(identity))
            {
                return {};
            }
        }
    }

    return std::unexpected{LiveRigError{
        LiveRigErrorCode::PluginNotFound, "Tone plugin was not found: " + identity.name
    }};
}

tracktion::Plugin* Engine::Impl::findInstrumentPluginInstance(const std::string& instance_id) const
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return nullptr;
    }

    const juce::String target_id{instance_id};
    // The returned plugin may be mutated by callers such as removePlugin().
    // NOLINTNEXTLINE(misc-const-correctness)
    for (tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin != nullptr && plugin->itemID.toString() == target_id)
        {
            return plugin;
        }
    }

    return nullptr;
}

bool Engine::Impl::isStructuralLiveRigPlugin(const tracktion::Plugin* plugin) const
{
    if (plugin == nullptr)
    {
        return false;
    }
    return plugin->itemID == m_input_gain_plugin_id || plugin->itemID == m_input_meter_plugin_id ||
           plugin->itemID == m_output_gain_plugin_id || plugin->itemID == m_output_meter_plugin_id;
}

void Engine::Impl::commitPluginRemoval(tracktion::Plugin& plugin) const
{
    if (auto* const macro_parameters = plugin.getMacroParameterList(); macro_parameters != nullptr)
    {
        macro_parameters->hideMacroParametersFromTracks();
    }

    for (tracktion::Track* const track : tracktion::getAllTracks(*m_edit))
    {
        if (track != nullptr)
        {
            track->hideAutomatableParametersForSource(plugin.itemID);
        }
    }

    plugin.hideWindowForShutdown();
    plugin.deselect();
}

bool Engine::Impl::hasPendingPluginEdits() const
{
    const bool has_state_edit = std::ranges::any_of(
        m_plugin_state_trackers, [](const std::unique_ptr<PluginDirtyStateTracker>& tracker) {
            return tracker != nullptr && tracker->hasPendingEdit();
        });
    return has_state_edit;
}

void Engine::Impl::notifyPluginEditPendingStateChanged()
{
    const bool pending = hasPendingPluginEdits();
    if (pending == m_plugin_edit_pending_notified)
    {
        return;
    }

    m_plugin_edit_pending_notified = pending;
    if (m_plugin_edit_observer.pending_changed)
    {
        m_plugin_edit_observer.pending_changed(pending);
    }
}

bool Engine::Impl::shouldDeferPluginUndoCapture() const
{
    return m_plugin_undo_capture_deferred;
}

void Engine::Impl::clearPluginUndoCaptureDeferral()
{
    m_plugin_undo_capture_deferred = false;
}

void Engine::Impl::beginPluginUndoCaptureDeferral()
{
    m_plugin_undo_capture_deferred = true;
}

void Engine::Impl::endPluginUndoCaptureDeferral(bool absorb_reannounce)
{
    clearPluginUndoCaptureDeferral();
    refreshPluginEditObservers(std::nullopt, absorb_reannounce);
}

void Engine::Impl::emitPluginStateEdit(PluginStateEdit edit)
{
    if (shouldDeferPluginUndoCapture())
    {
        return;
    }

    if (m_plugin_state_edit_observer.edit_completed)
    {
        m_plugin_state_edit_observer.edit_completed(std::move(edit));
    }
}

void Engine::Impl::dispatchPluginWindowCommand(PluginWindowCommand command)
{
    switch (command)
    {
        case PluginWindowCommand::Undo:
        {
            if (m_plugin_window_command_observer.undo_requested)
            {
                m_plugin_window_command_observer.undo_requested();
            }
            break;
        }
        case PluginWindowCommand::Redo:
        {
            if (m_plugin_window_command_observer.redo_requested)
            {
                m_plugin_window_command_observer.redo_requested();
            }
            break;
        }
        case PluginWindowCommand::PlayPause:
        {
            if (m_plugin_window_command_observer.play_pause_requested)
            {
                m_plugin_window_command_observer.play_pause_requested();
            }
            break;
        }
    }
}

void Engine::Impl::clearPluginEditObservers()
{
    m_plugin_parameter_dirty_trackers.clear();
    m_plugin_state_trackers.clear();
    notifyPluginEditPendingStateChanged();
}

void Engine::Impl::refreshPluginEditObservers(
    std::optional<KnownPluginBaseline> known_baseline, bool absorb_initial_reannounce)
{
    m_plugin_parameter_dirty_trackers.clear();
    m_plugin_state_trackers.clear();
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        notifyPluginEditPendingStateChanged();
        return;
    }

    for (tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin == nullptr || isStructuralLiveRigPlugin(plugin))
        {
            continue;
        }

        auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
        if (external_plugin == nullptr)
        {
            continue;
        }

        std::optional<PluginInstanceState> initial_baseline;
        if (known_baseline.has_value() &&
            known_baseline->instance_id == external_plugin->itemID.toString().toStdString())
        {
            initial_baseline = known_baseline->state;
        }

        auto state_tracker = std::make_unique<PluginDirtyStateTracker>(
            *external_plugin,
            [](tracktion::ExternalPlugin& observed_plugin)
                -> std::expected<PluginInstanceState, PluginHostError> {
                observed_plugin.flushPluginStateToValueTree();
                auto state = makePluginInstanceState(observed_plugin.state.createCopy());
                if (!state.has_value())
                {
                    return std::unexpected{std::move(state.error())};
                }

                return std::move(*state);
            },
            [this](PluginStateEdit edit) { emitPluginStateEdit(std::move(edit)); },
            [this] { notifyPluginEditPendingStateChanged(); },
            [this] { return shouldDeferPluginUndoCapture(); },
            std::move(initial_baseline),
            absorb_initial_reannounce);
        // The pointer targets the heap object owned by the unique_ptr, so vector growth does
        // not invalidate the callback target. Parameter trackers are cleared before state
        // trackers, so their callbacks cannot outlive the target.
        PluginDirtyStateTracker* const state_tracker_ptr = state_tracker.get();
        m_plugin_state_trackers.push_back(std::move(state_tracker));
        m_plugin_parameter_dirty_trackers.push_back(
            std::make_unique<PluginParameterDirtyTracker>(
                *external_plugin, [state_tracker_ptr] { state_tracker_ptr->markDirty(); }));
    }

    notifyPluginEditPendingStateChanged();
}

void Engine::Impl::refreshRestoredPluginEditObserver(
    const std::string& instance_id, PluginInstanceState restored_state)
{
    tracktion::Plugin* const plugin = findInstrumentPluginInstance(instance_id);
    auto* const external_plugin = dynamic_cast<tracktion::ExternalPlugin*>(plugin);
    if (external_plugin == nullptr)
    {
        refreshPluginEditObservers(
            KnownPluginBaseline{
                .instance_id = instance_id,
                .state = std::move(restored_state),
            });
        return;
    }

    for (std::size_t index = 0; index < m_plugin_state_trackers.size(); ++index)
    {
        PluginDirtyStateTracker* const state_tracker = m_plugin_state_trackers[index].get();
        if (state_tracker == nullptr || state_tracker->instanceId() != instance_id)
        {
            continue;
        }

        state_tracker->resetBaseline(std::move(restored_state));
        if (index < m_plugin_parameter_dirty_trackers.size())
        {
            m_plugin_parameter_dirty_trackers[index].reset();
            m_plugin_parameter_dirty_trackers[index] =
                std::make_unique<PluginParameterDirtyTracker>(
                    *external_plugin, [state_tracker] { state_tracker->markDirty(); });
        }
        notifyPluginEditPendingStateChanged();
        return;
    }

    refreshPluginEditObservers(
        KnownPluginBaseline{
            .instance_id = instance_id,
            .state = std::move(restored_state),
        });
}

void Engine::Impl::flushPendingPluginEdits()
{
    for (const std::unique_ptr<PluginDirtyStateTracker>& tracker : m_plugin_state_trackers)
    {
        if (tracker != nullptr)
        {
            tracker->flushPendingEdit();
        }
    }
    notifyPluginEditPendingStateChanged();
}

LiveRigGainPlugin* Engine::Impl::findStructuralGainPlugin(tracktion::EditItemID plugin_id) const
{
    if (!plugin_id.isValid())
    {
        return nullptr;
    }
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return nullptr;
    }
    for (tracktion::Plugin* const plugin : instrument_track->pluginList)
    {
        if (plugin != nullptr && plugin->itemID == plugin_id)
        {
            return dynamic_cast<LiveRigGainPlugin*>(plugin);
        }
    }
    return nullptr;
}

tracktion::LevelMeterPlugin* Engine::Impl::findLevelMeter(
    tracktion::PluginList& list, tracktion::EditItemID plugin_id)
{
    if (!plugin_id.isValid())
    {
        return nullptr;
    }
    for (tracktion::Plugin* const plugin : list)
    {
        if (plugin != nullptr && plugin->itemID == plugin_id)
        {
            return dynamic_cast<tracktion::LevelMeterPlugin*>(plugin);
        }
    }
    return nullptr;
}

tracktion::LevelMeterPlugin* Engine::Impl::findStructuralMeterPlugin(
    tracktion::EditItemID plugin_id) const
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return nullptr;
    }
    return findLevelMeter(instrument_track->pluginList, plugin_id);
}

tracktion::LevelMeterPlugin* Engine::Impl::findStructuralMasterMeterPlugin(
    tracktion::EditItemID plugin_id) const
{
    if (m_edit == nullptr)
    {
        return nullptr;
    }
    return findLevelMeter(m_edit->getMasterPluginList(), plugin_id);
}

std::expected<LiveRigGainPlugin*, LiveRigError> Engine::Impl::createLiveRigGainPlugin(
    int insert_index)
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }
    const tracktion::Plugin::Ptr plugin =
        m_edit->getPluginCache().createNewPlugin(LiveRigGainPlugin::createState());
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create structural live rig gain plugin",
        }};
    }
    instrument_track->pluginList.insertPlugin(plugin, insert_index, nullptr);
    auto* const live_rig_gain = dynamic_cast<LiveRigGainPlugin*>(plugin.get());
    if (live_rig_gain == nullptr || !instrument_track->pluginList.contains(live_rig_gain))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not insert structural live rig gain plugin",
        }};
    }

    return live_rig_gain;
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::createLevelMeter(
    tracktion::PluginList& list, int insert_index)
{
    const tracktion::Plugin::Ptr plugin =
        m_edit->getPluginCache().createNewPlugin(tracktion::LevelMeterPlugin::xmlTypeName, {});
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not create structural meter plugin",
        }};
    }
    list.insertPlugin(plugin, insert_index, nullptr);
    auto* const level_meter = dynamic_cast<tracktion::LevelMeterPlugin*>(plugin.get());
    if (level_meter == nullptr || !list.contains(level_meter))
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Could not insert structural meter plugin",
        }};
    }

    return level_meter;
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::createLevelMeterPlugin(
    int insert_index)
{
    tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }
    return createLevelMeter(instrument_track->pluginList, insert_index);
}

std::expected<tracktion::LevelMeterPlugin*, LiveRigError> Engine::Impl::
    createMasterLevelMeterPlugin()
{
    if (m_edit == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Edit is not available for master meter creation",
        }};
    }
    return createLevelMeter(m_edit->getMasterPluginList(), -1);
}

void Engine::Impl::attachMeterReader(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
{
    if (meter != nullptr)
    {
        reader.attach(&meter->measurer);
    }
    else
    {
        reader.detach();
    }
}

void Engine::Impl::detachAndClearMeter(MeterReader& reader, tracktion::LevelMeterPlugin* meter)
{
    reader.detach();
    if (meter != nullptr)
    {
        meter->measurer.clear();
    }
}

std::expected<void, LiveRigError> Engine::Impl::validateStructuralLiveRigPlugins() const
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    const auto* const input_plugin = findStructuralGainPlugin(m_input_gain_plugin_id);
    const auto* const input_meter = findStructuralMeterPlugin(m_input_meter_plugin_id);
    const auto* const output_plugin = findStructuralGainPlugin(m_output_gain_plugin_id);
    const auto* const output_meter = findStructuralMeterPlugin(m_output_meter_plugin_id);
    if (input_plugin == nullptr || input_meter == nullptr || output_plugin == nullptr ||
        output_meter == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are missing",
        }};
    }

    const auto& plugin_list = instrument_track->pluginList.getPlugins();
    if (plugin_list.size() < 4 || plugin_list[0] == nullptr || plugin_list[1] == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }
    if (plugin_list.getLast() == nullptr || plugin_list[plugin_list.size() - 2] == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }

    if (plugin_list[0]->itemID != m_input_gain_plugin_id ||
        plugin_list[1]->itemID != m_input_meter_plugin_id ||
        plugin_list[plugin_list.size() - 2]->itemID != m_output_gain_plugin_id ||
        plugin_list.getLast()->itemID != m_output_meter_plugin_id)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig plugins are not in fixed slots",
        }};
    }

    return {};
}

std::expected<void, LiveRigError> Engine::Impl::createStructuralLiveRigPlugins()
{
    if (instrumentTrack() == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    auto created_input_plugin = createLiveRigGainPlugin(0);
    if (!created_input_plugin.has_value())
    {
        return std::unexpected{std::move(created_input_plugin.error())};
    }
    m_input_gain_plugin_id = (*created_input_plugin)->itemID;

    auto created_input_meter = createLevelMeterPlugin(1);
    if (!created_input_meter.has_value())
    {
        return std::unexpected{std::move(created_input_meter.error())};
    }
    m_input_meter_plugin_id = (*created_input_meter)->itemID;

    auto created_output_plugin = createLiveRigGainPlugin(-1);
    if (!created_output_plugin.has_value())
    {
        return std::unexpected{std::move(created_output_plugin.error())};
    }
    m_output_gain_plugin_id = (*created_output_plugin)->itemID;

    auto created_output_meter = createLevelMeterPlugin(-1);
    if (!created_output_meter.has_value())
    {
        return std::unexpected{std::move(created_output_meter.error())};
    }
    m_output_meter_plugin_id = (*created_output_meter)->itemID;

    auto created_master_meter = createMasterLevelMeterPlugin();
    if (!created_master_meter.has_value())
    {
        return std::unexpected{std::move(created_master_meter.error())};
    }
    m_master_meter_plugin_id = (*created_master_meter)->itemID;

    return validateStructuralLiveRigPlugins();
}

std::expected<void, LiveRigError> Engine::Impl::clearUserLiveRigPlugins()
{
    const tracktion::AudioTrack* const instrument_track = instrumentTrack();
    if (instrument_track == nullptr)
    {
        return std::unexpected{LiveRigError{LiveRigErrorCode::TrackMissing}};
    }

    clearPluginEditObservers();
    const tracktion::Plugin::Array plugins = instrument_track->pluginList.getPlugins();
    for (tracktion::Plugin* const plugin : plugins)
    {
        if (plugin != nullptr && !isStructuralLiveRigPlugin(plugin))
        {
            plugin->removeFromParent();
        }
    }

    return validateStructuralLiveRigPlugins();
}

void Engine::Impl::clearRetainedLiveRigMeterState()
{
    detachAndClearMeter(m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
    detachAndClearMeter(m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
    detachAndClearMeter(
        m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));
}

std::expected<void, LiveRigError> Engine::Impl::resetLiveRigProjectState()
{
    auto output_reset = applyGainToPlugin(m_output_gain_plugin_id, Gain{defaultGainDb()});
    if (!output_reset.has_value())
    {
        return std::unexpected{std::move(output_reset.error())};
    }

    clearRetainedLiveRigMeterState();
    return {};
}

Gain Engine::Impl::readGainFromPlugin(tracktion::EditItemID plugin_id) const
{
    const auto* const plugin = findStructuralGainPlugin(plugin_id);
    if (plugin == nullptr)
    {
        return Gain{};
    }
    return plugin->gain();
}

std::expected<void, LiveRigError> Engine::Impl::applyGainToPlugin(
    tracktion::EditItemID plugin_id, Gain gain)
{
    auto* const plugin = findStructuralGainPlugin(plugin_id);
    if (plugin == nullptr)
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::PluginRestoreFailed,
            "Structural live rig gain plugin is missing",
        }};
    }

    plugin->setGain(gain);
    return {};
}

bool Engine::Impl::loadedAudioEndReached(double position_seconds) const
{
    return m_loaded_length_seconds > 0.0 && m_edit->getTransport().isPlaying() &&
           position_seconds >= m_loaded_length_seconds;
}

void Engine::Impl::stopTracktionPlayback()
{
    constexpr bool discard_recordings = false;
    constexpr bool clear_devices = false;
    m_edit->getTransport().stop(discard_recordings, clear_devices);
}

void Engine::Impl::pauseTransport()
{
    stopTracktionPlayback();
}

void Engine::Impl::stopTransportAndReleaseContext()
{
    constexpr bool discard_recordings = false;
    constexpr bool clear_devices = true;
    auto& transport = m_edit->getTransport();
    m_input_meter_reader.detach();
    m_output_meter_reader.detach();
    m_master_meter_reader.detach();
    transport.stop(discard_recordings, clear_devices);
    transport.freePlaybackContext();
}

void Engine::Impl::clearInstrumentInputAssignments()
{
    if (tracktion::AudioTrack* const backing_track = backingTrack(); backing_track != nullptr)
    {
        m_edit->getEditInputDevices().clearAllInputs(*backing_track, nullptr);
    }

    if (tracktion::AudioTrack* const instrument_track = instrumentTrack();
        instrument_track != nullptr)
    {
        m_edit->getEditInputDevices().clearAllInputs(*instrument_track, nullptr);
    }
}

void Engine::Impl::detachInstrumentMonitoringRoute()
{
    auto& transport = m_edit->getTransport();
    const bool should_release_context = !transport.isPlaying();
    if (should_release_context && m_edit->getCurrentPlaybackContext() == nullptr)
    {
        // clearAllInputs enumerates only the current playback context. Allocate a stopped
        // context long enough to remove persisted targets, then release it below.
        transport.ensureContextAllocated(true);
    }

    clearInstrumentInputAssignments();
    m_raw_input_meter_reader.detach();

    if (should_release_context)
    {
        m_input_meter_reader.detach();
        m_output_meter_reader.detach();
        m_master_meter_reader.detach();
        transport.freePlaybackContext();
    }
}

LiveInputError Engine::Impl::failInstrumentMonitoringRoute(const juce::String& reason)
{
    detachInstrumentMonitoringRoute();
    return liveInputRouteUnavailable(reason);
}

tracktion::WaveInputDevice* Engine::Impl::findInstrumentWaveInput(
    const InstrumentWaveDescription& description) const
{
    const std::vector<tracktion::WaveInputDevice*> wave_inputs =
        m_engine->getDeviceManager().getWaveInputDevices();

    const auto matching_input = std::ranges::find_if(
        wave_inputs, [&description](const tracktion::WaveInputDevice* wave_input) {
            return wave_input != nullptr && wave_input->getName() == description.name;
        });

    if (matching_input == wave_inputs.end())
    {
        return nullptr;
    }

    return *matching_input;
}

tracktion::WaveInputDevice* Engine::Impl::currentInstrumentWaveInput() const
{
    const tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
    juce::AudioIODevice* const current_device =
        tracktion_device_manager.deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return nullptr;
    }

    const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
        createTracktionInstrumentWaveDeviceDescriptions(
            current_device->getName(),
            current_device->getActiveInputChannels(),
            current_device->getActiveOutputChannels(),
            current_device->getInputChannelNames(),
            current_device->getOutputChannelNames());
    if (!wave_descriptions.has_value())
    {
        return nullptr;
    }

    return findInstrumentWaveInput(wave_descriptions->input);
}

std::expected<void, LiveInputError> Engine::Impl::applyInstrumentMonitoringRoute()
{
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        return std::unexpected{LiveInputError{LiveInputErrorCode::MessageThreadRequired}};
    }

    if (!m_live_input_monitoring_enabled && !m_calibration_input_monitoring_enabled)
    {
        detachInstrumentMonitoringRoute();
        return {};
    }

    const tracktion::AudioTrack* const monitoring_target =
        m_calibration_input_monitoring_enabled ? backingTrack() : instrumentTrack();
    if (monitoring_target == nullptr)
    {
        return std::unexpected{liveInputRouteUnavailable(
            m_calibration_input_monitoring_enabled ? "backing track is missing"
                                                   : "instrument track is missing")};
    }

    tracktion::DeviceManager& tracktion_device_manager = m_engine->getDeviceManager();
    juce::AudioIODevice* const current_device =
        tracktion_device_manager.deviceManager.getCurrentAudioDevice();
    if (current_device == nullptr)
    {
        return std::unexpected{failInstrumentMonitoringRoute("no current audio device")};
    }

    const std::optional<InstrumentWaveDeviceDescriptions> wave_descriptions =
        createTracktionInstrumentWaveDeviceDescriptions(
            current_device->getName(),
            current_device->getActiveInputChannels(),
            current_device->getActiveOutputChannels(),
            current_device->getInputChannelNames(),
            current_device->getOutputChannelNames());
    if (!wave_descriptions.has_value())
    {
        return std::unexpected{failInstrumentMonitoringRoute(
            "selected route is not one mono input and one stereo output pair")};
    }

    tracktion_device_manager.dispatchPendingUpdates();

    tracktion::WaveInputDevice* const wave_input =
        findInstrumentWaveInput(wave_descriptions->input);
    if (wave_input == nullptr)
    {
        return std::unexpected{failInstrumentMonitoringRoute(
            "selected mono input is not available to Tracktion")};
    }

    clearInstrumentInputAssignments();

    auto& transport = m_edit->getTransport();
    transport.ensureContextAllocated(true);
    wave_input->setStereoPair(false);

    tracktion::InputDeviceInstance* const input_instance =
        m_edit->getCurrentInstanceForInputDevice(wave_input);
    if (input_instance == nullptr)
    {
        transport.ensureContextAllocated(true);
        return std::unexpected{liveInputRouteUnavailable(
            "selected mono input has no playback instance")};
    }

    const auto target_result =
        input_instance->setTarget(monitoring_target->itemID, true, nullptr, std::optional<int>{0});
    if (!target_result)
    {
        transport.ensureContextAllocated(true);
        return std::unexpected{liveInputRouteUnavailable(
            "could not assign live input to monitoring track: " + target_result.error())};
    }

    input_instance->setRecordingEnabled(monitoring_target->itemID, false);
    wave_input->setMonitorMode(
        (m_live_input_monitoring_enabled || m_calibration_input_monitoring_enabled)
            ? tracktion::InputDevice::MonitorMode::on
            : tracktion::InputDevice::MonitorMode::off);
    transport.ensureContextAllocated(true);
    return {};
}

void Engine::Impl::stopTransport()
{
    auto& transport = m_edit->getTransport();
    stopTracktionPlayback();
    transport.setPosition(tracktion::TimePosition{});
}

std::expected<void, LiveInputError> Engine::Impl::rebuildInstrumentMonitoringGraph()
{
    auto route_result = applyInstrumentMonitoringRoute();
    updateTransportState();
    return route_result;
}

void Engine::Impl::rebuildInstrumentMonitoringGraphBestEffort(std::string_view context)
{
    auto route_result = rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        logInstrumentMonitoringFailure(
            toJuceString(context) + ": " + toJuceString(route_result.error().message));
    }
}

std::expected<void, LiveInputError> Engine::Impl::setMonitoringChannelEnabled(
    MonitorChannel channel, bool enabled, bool input_device_available,
    std::string_view rollback_context)
{
    const MonitoringFlags current{
        .live_input = m_live_input_monitoring_enabled,
        .calibration = m_calibration_input_monitoring_enabled
    };
    const std::optional<MonitoringFlags> requested =
        monitoringFlagsForRequest(current, channel, enabled, input_device_available);

    if (!requested.has_value())
    {
        // No input device to route from: force both modes off and report the route failure.
        m_live_input_monitoring_enabled = false;
        m_calibration_input_monitoring_enabled = false;
        // If monitoring was already off, there is no route to tear down. Tracktion graph
        // rebuilds can allocate playback contexts, so skip them when no state changes.
        if (current.live_input || current.calibration)
        {
            rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
        }
        return std::unexpected{LiveInputError{LiveInputErrorCode::InputRouteUnavailable}};
    }

    if (requested->live_input == current.live_input &&
        requested->calibration == current.calibration)
    {
        // Lifecycle gates can repeat monitoring requests that leave both flags unchanged.
        // Rebuilding Tracktion routing in that case does work for an identical graph.
        return {};
    }

    m_live_input_monitoring_enabled = requested->live_input;
    m_calibration_input_monitoring_enabled = requested->calibration;

    auto route_result = rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        LiveInputError route_error = std::move(route_result.error());
        if (enabled)
        {
            m_live_input_monitoring_enabled = false;
            m_calibration_input_monitoring_enabled = false;
            rebuildInstrumentMonitoringGraphBestEffort(rollback_context);
        }
        return std::unexpected{std::move(route_error)};
    }

    return {};
}

AudioMeterSnapshot Engine::Impl::audioMeterSnapshot() const
{
    attachMeterReader(m_input_meter_reader, findStructuralMeterPlugin(m_input_meter_plugin_id));
    attachMeterReader(m_output_meter_reader, findStructuralMeterPlugin(m_output_meter_plugin_id));
    attachMeterReader(
        m_master_meter_reader, findStructuralMasterMeterPlugin(m_master_meter_plugin_id));

    return AudioMeterSnapshot{
        .live_rig_input = m_input_meter_reader.read(),
        .live_rig_output = m_output_meter_reader.read(),
        .master_output = m_master_meter_reader.read(),
    };
}

AudioMeterLevel Engine::Impl::rawInputMeterLevel() const
{
    if (m_audio_device_configuration_refresh_pending)
    {
        return {};
    }

    if (tracktion::WaveInputDevice* const wave_input = currentInstrumentWaveInput();
        wave_input != nullptr)
    {
        m_raw_input_meter_reader.attach(&wave_input->levelMeasurer);
    }
    else
    {
        m_raw_input_meter_reader.detach();
    }

    return m_raw_input_meter_reader.read();
}

// Handles the child-process entry point used by Tracktion's isolated plugin scanner.
bool Engine::startPluginScanChildProcess(std::string_view command_line)
{
    return tracktion::PluginManager::startChildProcessPluginScan(toJuceString(command_line));
}

// Checks whether a command line is addressed to Tracktion's isolated plugin scanner.
bool Engine::isPluginScanChildProcessCommandLine(std::string_view command_line)
{
    return toJuceString(command_line)
        .trim()
        .startsWith(toJuceString(g_plugin_scan_command_line_prefix));
}

// Creates the Tracktion engine and a minimal two-track edit for playback and instrument monitoring.
Engine::Engine()
    : m_impl(std::make_unique<Impl>())
{
    // Tracktion uses the engine application name as its property-storage folder.
    Impl* const impl = m_impl.get();
    m_impl->m_engine = std::make_unique<tracktion::Engine>(
        toJuceString(core::applicationDataFolderName()),
        std::make_unique<RockHeroUIBehaviour>(
            [impl](PluginWindowCommand command) { impl->dispatchPluginWindowCommand(command); }),
        std::make_unique<RockHeroEngineBehaviour>());
    m_impl->m_engine->getPluginManager().setUsesSeparateProcessForScanning(true);

    // createSingleTrackEdit already provides one AudioTrack ready for media.
    m_impl->createEdit();

    // Start with one instrument input and stereo output; the dialog can reconfigure either at
    // runtime.
    m_impl->m_engine->getDeviceManager().initialise(1, 2);
    m_impl->rebuildInstrumentMonitoringGraphBestEffort("initial monitoring route setup failed");

    auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
    device_manager.addChangeListener(m_impl.get());

    // TransportControl derives from juce::ChangeBroadcaster and notifies on any transport
    // state change; Impl::changeListenerCallback filters to genuine play/pause transitions.
    m_impl->m_edit->getTransport().addChangeListener(m_impl.get());

    // Tracktion mirrors current playhead position into this public ValueTree property from its
    // transport loop. Listening here keeps the adapter event-driven from the UI perspective.
    m_impl->m_edit->getTransport().state.addListener(m_impl.get());

    // Seeds the project-owned state from the freshly created empty edit.
    m_impl->updateTransportState();
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    m_impl->m_alive.reset();
    m_impl->m_load_op.reset();

    if (m_impl->m_engine)
    {
        auto& device_manager = m_impl->m_engine->getDeviceManager().deviceManager;
        device_manager.removeChangeListener(m_impl.get());
    }

    if (m_impl->m_edit)
    {
        m_impl->m_edit->getTransport().state.removeListener(m_impl.get());
        m_impl->m_edit->getTransport().removeChangeListener(m_impl.get());
        m_impl->stopTransportAndReleaseContext();
    }

    m_impl->m_edit.reset();
    m_impl->m_engine.reset();
}

// Creates an IThumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<IThumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<TracktionThumbnail>(*m_impl->m_engine, owner);
}

} // namespace rock_hero::common::audio
