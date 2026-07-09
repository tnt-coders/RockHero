#include "tracktion/plugin_dirty_tracking.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <rock_hero/common/core/shared/logger.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr auto g_plugin_dirty_transaction_quiet_debounce = std::chrono::milliseconds{750};

} // namespace

PluginParameterDirtyTracker::PluginParameterDirtyTracker(
    tracktion::ExternalPlugin& plugin, MarkDirty mark_dirty, MarkUserIntent mark_user_intent)
    : m_mark_dirty(std::move(mark_dirty))
    , m_mark_user_intent(std::move(mark_user_intent))
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

PluginParameterDirtyTracker::~PluginParameterDirtyTracker()
{
    for (const tracktion::AutomatableParameter::Ptr& parameter : m_parameters)
    {
        if (parameter != nullptr)
        {
            parameter->removeListener(this);
        }
    }
}

void PluginParameterDirtyTracker::markDirty()
{
    if (m_mark_dirty)
    {
        m_mark_dirty();
    }
}

void PluginParameterDirtyTracker::markUserIntent()
{
    if (m_mark_user_intent)
    {
        m_mark_user_intent();
    }
}

void PluginParameterDirtyTracker::parameterChangeGestureBegin(
    tracktion::AutomatableParameter& /*parameter*/)
{
    // A gesture is only ever raised by the plugin's editor in response to a user grabbing a control,
    // so it is the authoritative "a human is editing this" signal.
    markUserIntent();
}

void PluginParameterDirtyTracker::parameterChangeGestureEnd(
    tracktion::AutomatableParameter& /*parameter*/)
{
    markUserIntent();
}

void PluginParameterDirtyTracker::curveHasChanged(tracktion::AutomatableParameter& /*parameter*/)
{}

void PluginParameterDirtyTracker::currentValueChanged(
    tracktion::AutomatableParameter& /*parameter*/)
{}

void PluginParameterDirtyTracker::parameterChanged(
    tracktion::AutomatableParameter& /*parameter*/, float /*new_value*/)
{
    // A bare value change carries no intent: the plugin fires it identically for a user move and for
    // its own post-load re-announcement. The state tracker decides intent from gestures / window.
    markDirty();
}

PluginDirtyStateTracker::PluginDirtyStateTracker(
    tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
    PendingChanged pending_changed, ShouldDeferCapture should_defer_capture,
    IsEditorWindowOpen is_editor_window_open, std::optional<PluginInstanceState> initial_baseline)
    : m_plugin(plugin)
    , m_instance_id(plugin.itemID.toString().toStdString())
    , m_capture_state(std::move(capture_state))
    , m_emit_edit(std::move(emit_edit))
    , m_pending_changed(std::move(pending_changed))
    , m_should_defer_capture(std::move(should_defer_capture))
    , m_is_editor_window_open(std::move(is_editor_window_open))
{
    if (initial_baseline.has_value())
    {
        m_baseline = std::move(initial_baseline);
    }
    else
    {
        refreshBaseline();
    }

    // A freshly attached tracker is about to hear the plugin's own instantiation/restore re-announce
    // (which is not a user edit). Expect it, so window-open alone does not misread it as intent.
    m_expect_self_report = true;
    m_plugin.addSelectableListener(this);
}

PluginDirtyStateTracker::~PluginDirtyStateTracker()
{
    stopTimer();
    if (tracktion::Selectable::isSelectableValid(&m_plugin))
    {
        m_plugin.removeSelectableListener(this);
    }
}

bool PluginDirtyStateTracker::hasPendingEdit() const noexcept
{
    return m_before.has_value();
}

const std::string& PluginDirtyStateTracker::instanceId() const noexcept
{
    return m_instance_id;
}

void PluginDirtyStateTracker::flushPendingEdit()
{
    stopTimer();
    settlePendingEdit();
}

void PluginDirtyStateTracker::markDirty()
{
    beginPendingEdit();
}

void PluginDirtyStateTracker::markUserIntent()
{
    // A gesture is genuine user interaction, so it overrides an expected self-report: the plugin is
    // now being edited, not merely settling after our operation.
    RH_LOG_INFO(
        "audio.engine",
        "Plugin user intent (gesture) instance_id={:?} label_hint={:?}",
        m_instance_id,
        m_plugin.getName().toStdString());
    m_user_intent = true;
    m_expect_self_report = false;
    beginPendingEdit();
}

void PluginDirtyStateTracker::resetBaseline(PluginInstanceState baseline)
{
    stopTimer();
    m_before.reset();
    m_user_intent = false;
    // The restore will make the plugin re-announce its own state; expect it so the re-announce is
    // folded even when the editor window is open (as it is when the user undoes mid-edit).
    m_expect_self_report = true;
    m_baseline = std::move(baseline);
    notifyPendingChanged();
}

bool PluginDirtyStateTracker::isCaptureDeferred() const
{
    return m_should_defer_capture && m_should_defer_capture();
}

std::expected<PluginInstanceState, PluginHostError> PluginDirtyStateTracker::captureState()
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

void PluginDirtyStateTracker::notifyPendingChanged()
{
    if (m_pending_changed)
    {
        m_pending_changed();
    }
}

void PluginDirtyStateTracker::refreshBaseline()
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

void PluginDirtyStateTracker::selectableObjectChanged(tracktion::Selectable* selectable)
{
    if (selectable != &m_plugin || isCaptureDeferred())
    {
        return;
    }

    beginPendingEdit();
}

void PluginDirtyStateTracker::beginPendingEdit()
{
    if (isCaptureDeferred())
    {
        return;
    }

    // In this app a plugin's parameters can only be changed through its own editor window (there is
    // no host-side parameter UI, and automation is a separate undo domain), so an open editor is the
    // corroborating intent signal for changes that arrive without a gesture, such as an in-plugin
    // preset load. Sample it on every dirty signal, not only at settle, because the window may close
    // before the debounce fires. It does not count while we expect the plugin's own re-announce,
    // because then the open window is incidental (e.g. left open while the user undoes).
    const bool window_open = m_is_editor_window_open && m_is_editor_window_open();
    if (window_open && !m_expect_self_report)
    {
        m_user_intent = true;
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
        notifyPendingChanged();
        RH_LOG_INFO(
            "audio.engine",
            "Plugin state edit started instance_id={:?} label_hint={:?} window_open={} "
            "expect_self_report={}",
            m_instance_id,
            m_plugin.getName().toStdString(),
            window_open,
            m_expect_self_report);
    }

    startTimer(static_cast<int>(g_plugin_dirty_transaction_quiet_debounce.count()));
}

void PluginDirtyStateTracker::selectableObjectAboutToBeDeleted(tracktion::Selectable* selectable)
{
    if (selectable == &m_plugin)
    {
        stopTimer();
        m_before.reset();
        m_baseline.reset();
        m_user_intent = false;
    }
}

void PluginDirtyStateTracker::settlePendingEdit()
{
    if (!m_before.has_value())
    {
        return;
    }

    auto after = captureState();
    PluginInstanceState before = std::move(*m_before);
    m_before.reset();
    const bool had_user_intent = std::exchange(m_user_intent, false);
    // The self-report we were expecting has now been absorbed; later transactions are ordinary and
    // may take intent from the open editor window again.
    m_expect_self_report = false;
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

    if (before != *after && !had_user_intent)
    {
        // The plugin changed its own state with no user intent behind it (an instantiation or
        // undo/redo re-announce). Fold it into the baseline; recording it would put a phantom edit
        // on the undo stack and, mid-redo, truncate the redo branch.
        RH_LOG_INFO(
            "audio.engine",
            "Folded plugin self-report (no user intent) instance_id={:?} label_hint={:?}",
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

void PluginDirtyStateTracker::timerCallback()
{
    stopTimer();
    settlePendingEdit();
}

} // namespace rock_hero::common::audio
