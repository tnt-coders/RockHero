#include "tracktion/plugin_dirty_tracking.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/logger.h>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

constexpr auto g_plugin_dirty_transaction_quiet_debounce = std::chrono::milliseconds{750};

// After a programmatic state restore (undo/redo), the plugin asynchronously re-announces its
// parameters; immediate re-announces should not become user edits because that discards the redo
// stack. The window self-extends on each absorbed settle, but an already-open absorbed transaction
// that receives more dirty signals after the deadline is recorded so real user edits are not lost
// inside the longer quiet debounce.
constexpr auto g_plugin_post_restore_absorb_window = std::chrono::milliseconds{100};

} // namespace

PluginParameterDirtyTracker::PluginParameterDirtyTracker(
    tracktion::ExternalPlugin& plugin, MarkDirty mark_dirty)
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

void PluginParameterDirtyTracker::parameterChangeGestureBegin(
    tracktion::AutomatableParameter& /*parameter*/)
{
    markDirty();
}

void PluginParameterDirtyTracker::parameterChangeGestureEnd(
    tracktion::AutomatableParameter& /*parameter*/)
{
    markDirty();
}

void PluginParameterDirtyTracker::curveHasChanged(tracktion::AutomatableParameter& /*parameter*/)
{}

void PluginParameterDirtyTracker::currentValueChanged(
    tracktion::AutomatableParameter& /*parameter*/)
{}

void PluginParameterDirtyTracker::parameterChanged(
    tracktion::AutomatableParameter& /*parameter*/, float /*new_value*/)
{
    markDirty();
}

void PostRestoreAbsorbWindow::arm() noexcept
{
    m_deadline = Clock::now() + g_plugin_post_restore_absorb_window;
}

bool PostRestoreAbsorbWindow::beginTransaction() noexcept
{
    m_transaction_absorbing = Clock::now() < m_deadline;
    return m_transaction_absorbing;
}

bool PostRestoreAbsorbWindow::isAbsorbingTransaction() const noexcept
{
    return m_transaction_absorbing;
}

bool PostRestoreAbsorbWindow::hasElapsed() const noexcept
{
    return m_deadline != Clock::time_point{} && Clock::now() >= m_deadline;
}

bool PostRestoreAbsorbWindow::finishTransaction() noexcept
{
    return std::exchange(m_transaction_absorbing, false);
}

void PostRestoreAbsorbWindow::clearTransaction() noexcept
{
    m_transaction_absorbing = false;
}

PluginDirtyStateTracker::PluginDirtyStateTracker(
    tracktion::ExternalPlugin& plugin, CaptureState capture_state, EmitEdit emit_edit,
    PendingChanged pending_changed, ShouldDeferCapture should_defer_capture,
    std::optional<PluginInstanceState> initial_baseline, bool absorb_initial_reannounce)
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

void PluginDirtyStateTracker::resetBaseline(PluginInstanceState baseline)
{
    stopTimer();
    m_before.reset();
    m_post_restore_absorb.clearTransaction();
    m_baseline = std::move(baseline);
    m_post_restore_absorb.arm();
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

void PluginDirtyStateTracker::selectableObjectAboutToBeDeleted(tracktion::Selectable* selectable)
{
    if (selectable == &m_plugin)
    {
        stopTimer();
        m_before.reset();
        m_baseline.reset();
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

void PluginDirtyStateTracker::timerCallback()
{
    stopTimer();
    settlePendingEdit();
}

} // namespace rock_hero::common::audio
