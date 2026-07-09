/*!
\file plugin_dirty_tracking.h
\brief Trackers that turn Tracktion plugin change notifications into debounced full-state edits.
*/

#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <rock_hero/common/audio/plugin/i_plugin_host.h>
#include <string>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace rock_hero::common::audio
{

/*!
\brief Observes one external plugin's parameter notifications.

A plugin's own value re-announcements (fired asynchronously after instantiation or a state restore)
and a genuine user parameter move are indistinguishable by value alone, so this tracker forwards
them on two channels: every change marks the plugin state dirty, while a parameter gesture
(begin/end edit) additionally signals user intent. Only the plugin's editor GUI raises gestures, so
they are the reliable "a human did this" signal the state tracker uses to decide whether a settled
change is a real edit.
*/
class PluginParameterDirtyTracker final : private tracktion::AutomatableParameter::Listener
{
public:
    /*! \brief Callback invoked whenever any observed parameter reports a change. */
    using MarkDirty = std::function<void()>;

    /*! \brief Callback invoked when a change carries user intent (a parameter gesture). */
    using MarkUserIntent = std::function<void()>;

    /*!
    \brief Subscribes to every automatable parameter of the plugin.
    \param plugin Plugin whose parameters should be observed.
    \param mark_dirty Callback invoked on each parameter change notification.
    \param mark_user_intent Callback invoked on each parameter gesture (user GUI interaction).
    */
    PluginParameterDirtyTracker(
        tracktion::ExternalPlugin& plugin, MarkDirty mark_dirty, MarkUserIntent mark_user_intent);

    /*! \brief Unsubscribes from all observed parameters. */
    ~PluginParameterDirtyTracker() override;

    /*! \brief Copying is disabled; the tracker holds listener registrations. */
    PluginParameterDirtyTracker(const PluginParameterDirtyTracker&) = delete;

    /*! \brief Copy assignment is disabled; the tracker holds listener registrations. */
    PluginParameterDirtyTracker& operator=(const PluginParameterDirtyTracker&) = delete;

    /*! \brief Moving is disabled; parameters hold a stable listener pointer. */
    PluginParameterDirtyTracker(PluginParameterDirtyTracker&&) = delete;

    /*! \brief Move assignment is disabled; parameters hold a stable listener pointer. */
    PluginParameterDirtyTracker& operator=(PluginParameterDirtyTracker&&) = delete;

private:
    void markDirty();

    void markUserIntent();

    void parameterChangeGestureBegin(tracktion::AutomatableParameter& parameter) override;

    void parameterChangeGestureEnd(tracktion::AutomatableParameter& parameter) override;

    void curveHasChanged(tracktion::AutomatableParameter& parameter) override;

    void currentValueChanged(tracktion::AutomatableParameter& parameter) override;

    void parameterChanged(tracktion::AutomatableParameter& parameter, float new_value) override;

    MarkDirty m_mark_dirty;
    MarkUserIntent m_mark_user_intent;
    std::vector<tracktion::AutomatableParameter::Ptr> m_parameters;
};

/*! \brief Owns one plugin's debounced dirty-state transaction and emits full-state edits. */
class PluginDirtyStateTracker final : private tracktion::SelectableListener, private juce::Timer
{
public:
    /*! \brief Captures the plugin's full state as an opaque memento. */
    using CaptureState = std::function<std::expected<PluginInstanceState, PluginHostError>(
        tracktion::ExternalPlugin&)>;

    /*! \brief Receives one completed before/after full-state edit. */
    using EmitEdit = std::function<void(PluginStateEdit)>;

    /*! \brief Notified whenever the tracker's pending-edit state may have changed. */
    using PendingChanged = std::function<void()>;

    /*! \brief Reports whether state capture is currently deferred (undo/redo in flight). */
    using ShouldDeferCapture = std::function<bool()>;

    /*! \brief Reports whether the plugin's editor window is currently open. */
    using IsEditorWindowOpen = std::function<bool()>;

    /*!
    \brief Subscribes to the plugin's change notifications and establishes the edit baseline.

    A hosted plugin re-announces its own state asynchronously after it is instantiated or restored,
    with no bounded "settled" signal the host can wait for (a VST3 may call restartComponent at any
    time). Those self-reports are therefore never distinguished from a real edit by timing. Instead a
    settled transaction is emitted only when it carries **user intent** — a parameter gesture, or the
    plugin's editor window being open when a change arrives. Every other settled change (a fresh
    insert's re-announce, an undo/redo recreate's re-announce) is folded into the baseline and never
    recorded, so it cannot appear as a phantom edit or truncate the redo stack.

    \param plugin Plugin whose state edits should be tracked.
    \param capture_state Full-state capture callback.
    \param emit_edit Callback receiving completed edits.
    \param pending_changed Callback notified when pending-edit state changes.
    \param should_defer_capture Callback gating capture during host-owned restores.
    \param is_editor_window_open Callback reporting whether the plugin's editor window is open.
    \param initial_baseline Known post-restore baseline, or nullopt to capture one now.
    */
    PluginDirtyStateTracker(
        tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
        PendingChanged pending_changed, ShouldDeferCapture should_defer_capture,
        IsEditorWindowOpen is_editor_window_open,
        std::optional<PluginInstanceState> initial_baseline = std::nullopt);

    /*! \brief Stops the debounce timer and unsubscribes from the plugin. */
    ~PluginDirtyStateTracker() override;

    /*! \brief Copying is disabled; the tracker holds a listener registration. */
    PluginDirtyStateTracker(const PluginDirtyStateTracker&) = delete;

    /*! \brief Copy assignment is disabled; the tracker holds a listener registration. */
    PluginDirtyStateTracker& operator=(const PluginDirtyStateTracker&) = delete;

    /*! \brief Moving is disabled; the plugin holds a stable listener pointer. */
    PluginDirtyStateTracker(PluginDirtyStateTracker&&) = delete;

    /*! \brief Move assignment is disabled; the plugin holds a stable listener pointer. */
    PluginDirtyStateTracker& operator=(PluginDirtyStateTracker&&) = delete;

    /*!
    \brief Reports whether a dirty transaction is currently open.
    \return True when an edit has begun but not yet settled.
    */
    [[nodiscard]] bool hasPendingEdit() const noexcept;

    /*!
    \brief Identifies the tracked plugin instance.
    \return Stable Tracktion item ID of the tracked plugin.
    */
    [[nodiscard]] const std::string& instanceId() const noexcept;

    /*! \brief Settles the open transaction immediately instead of waiting for the debounce. */
    void flushPendingEdit();

    /*! \brief Opens or extends the dirty transaction, as if the plugin reported a change. */
    void markDirty();

    /*!
    \brief Opens or extends the dirty transaction and flags it as carrying user intent.

    Called for a parameter gesture: the transaction it belongs to is a real user edit and will be
    emitted on settle (when its state actually changed).
    */
    void markUserIntent();

    /*!
    \brief Retargets the baseline to a just-restored (undo/redo) memento.

    Drops any open transaction and its intent flag so the plugin's asynchronous post-restore
    re-announce (which carries no gesture and arrives with the editor closed) is folded into this
    baseline instead of recorded as a spurious user edit that would discard the redo stack.

    \param baseline Full plugin state that the restore just applied.
    */
    void resetBaseline(PluginInstanceState baseline);

private:
    [[nodiscard]] bool isCaptureDeferred() const;

    [[nodiscard]] std::expected<PluginInstanceState, PluginHostError> captureState();

    void notifyPendingChanged();

    void refreshBaseline();

    void selectableObjectChanged(tracktion::Selectable* selectable) override;

    void beginPendingEdit();

    void selectableObjectAboutToBeDeleted(tracktion::Selectable* selectable) override;

    void settlePendingEdit();

    void timerCallback() override;

    tracktion::ExternalPlugin& m_plugin;
    std::string m_instance_id;
    CaptureState m_capture_state;
    EmitEdit m_emit_edit;
    PendingChanged m_pending_changed;
    ShouldDeferCapture m_should_defer_capture;
    IsEditorWindowOpen m_is_editor_window_open;
    std::optional<PluginInstanceState> m_baseline;
    std::optional<PluginInstanceState> m_before;

    // True when the open transaction has seen user intent (a gesture, or a change while the editor
    // window was open). Only an intent-carrying transaction is emitted as an edit; reset at settle.
    bool m_user_intent{false};

    // True while the next settling transaction is expected to be the plugin's own re-announce that
    // follows an instantiation or a state restore. Such a re-announce is never a user edit even if
    // the editor window happens to be open (e.g. left open from prior editing while the user hits
    // undo), so window-open does not imply intent while this is set; a gesture still does and clears
    // it. Cleared when the transaction settles. This is the structural discriminator between "the
    // plugin reacting to our operation" and "the user editing", with no reliance on timing.
    bool m_expect_self_report{false};
};

} // namespace rock_hero::common::audio
