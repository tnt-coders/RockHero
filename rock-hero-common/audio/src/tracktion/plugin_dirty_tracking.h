/*!
\file plugin_dirty_tracking.h
\brief Trackers that turn Tracktion plugin change notifications into debounced full-state edits.
*/

#pragma once

#include <chrono>
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
\brief Observes one external plugin's parameter notifications and marks the whole plugin state
dirty.
*/
class PluginParameterDirtyTracker final : private tracktion::AutomatableParameter::Listener
{
public:
    /*! \brief Callback invoked whenever any observed parameter reports a change. */
    using MarkDirty = std::function<void()>;

    /*!
    \brief Subscribes to every automatable parameter of the plugin.
    \param plugin Plugin whose parameters should be observed.
    \param mark_dirty Callback invoked on each parameter change notification.
    */
    PluginParameterDirtyTracker(tracktion::ExternalPlugin& plugin, MarkDirty mark_dirty);

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

    void parameterChangeGestureBegin(tracktion::AutomatableParameter& parameter) override;

    void parameterChangeGestureEnd(tracktion::AutomatableParameter& parameter) override;

    void curveHasChanged(tracktion::AutomatableParameter& parameter) override;

    void currentValueChanged(tracktion::AutomatableParameter& parameter) override;

    void parameterChanged(tracktion::AutomatableParameter& parameter, float new_value) override;

    MarkDirty m_mark_dirty;
    std::vector<tracktion::AutomatableParameter::Ptr> m_parameters;
};

/*!
\brief Tracks the brief window after a plugin-state restore during which the plugin re-announces
its own state.

Two facts that always change together live here: the window deadline (armed and extended around
restores) and whether the dirty transaction currently in flight began inside that window and
should therefore be absorbed rather than emitted as an edit. Message-thread only; no
synchronization.
*/
class PostRestoreAbsorbWindow final
{
public:
    /*! \brief Opens or extends the absorb window, measured from now. */
    void arm() noexcept;

    /*!
    \brief Begins a dirty transaction.
    \return True when the transaction started inside the absorb window.
    */
    [[nodiscard]] bool beginTransaction() noexcept;

    /*!
    \brief Reports whether the in-flight transaction is being absorbed.
    \return True when the current transaction began inside the absorb window.
    */
    [[nodiscard]] bool isAbsorbingTransaction() const noexcept;

    /*!
    \brief Reports whether an armed window has closed.
    \return True when a previously armed window has elapsed; false on a never-armed window so
    callers cannot read a default-constructed deadline as already elapsed.
    */
    [[nodiscard]] bool hasElapsed() const noexcept;

    /*!
    \brief Closes the in-flight transaction.
    \return True when the transaction was still being absorbed.
    */
    [[nodiscard]] bool finishTransaction() noexcept;

    /*! \brief Abandons the in-flight transaction's absorb flag without touching the window. */
    void clearTransaction() noexcept;

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point m_deadline{};
    bool m_transaction_absorbing{false};
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

    /*!
    \brief Subscribes to the plugin's change notifications and establishes the edit baseline.

    Undo/redo rebuilds pass a known baseline; save capture rebuilds from the live plugin. Both
    host-owned sync points can make plugins immediately re-announce state, so either arms the
    post-restore absorb window.

    \param plugin Plugin whose state edits should be tracked.
    \param capture_state Full-state capture callback.
    \param emit_edit Callback receiving completed edits.
    \param pending_changed Callback notified when pending-edit state changes.
    \param should_defer_capture Callback gating capture during host-owned restores.
    \param initial_baseline Known post-restore baseline, or nullopt to capture one now.
    \param absorb_initial_reannounce True when the immediate re-announce burst that follows
    construction should be folded into the baseline instead of recorded as a user edit.
    */
    PluginDirtyStateTracker(
        tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
        PendingChanged pending_changed, ShouldDeferCapture should_defer_capture,
        std::optional<PluginInstanceState> initial_baseline = std::nullopt,
        bool absorb_initial_reannounce = false);

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
    \brief Retargets the baseline to a just-restored (undo/redo) memento.

    Arms the absorb window so the plugin's asynchronous post-restore re-announce is folded into
    the baseline instead of recorded as a spurious user edit (which would discard the redo
    stack).

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
    std::optional<PluginInstanceState> m_baseline;
    std::optional<PluginInstanceState> m_before;
    PostRestoreAbsorbWindow m_post_restore_absorb;
};

} // namespace rock_hero::common::audio
