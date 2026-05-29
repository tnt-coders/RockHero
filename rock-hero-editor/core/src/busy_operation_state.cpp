#include "busy_operation_state.h"

#include <utility>

namespace rock_hero::editor::core
{

bool BusyOperationState::isBusy() const noexcept
{
    return m_operation.has_value();
}

std::uint64_t BusyOperationState::begin(BusyOperation operation) noexcept
{
    m_current_token += 1;
    m_operation = operation;
    m_live_rig_progress.reset();
    return m_current_token;
}

bool BusyOperationState::isCurrentToken(std::uint64_t token) const noexcept
{
    return isBusy() && token == m_current_token;
}

std::uint64_t BusyOperationState::currentToken() const noexcept
{
    return m_current_token;
}

bool BusyOperationState::transition(BusyOperation operation, std::uint64_t token) noexcept
{
    if (!isBusy() || !isCurrentToken(token))
    {
        return false;
    }

    m_operation = operation;
    m_live_rig_progress.reset();
    return true;
}

void BusyOperationState::end() noexcept
{
    m_operation.reset();
    m_live_rig_progress.reset();
    m_current_token += 1;
}

bool BusyOperationState::beginLiveRigLoadProgress()
{
    if (!isBusy())
    {
        return false;
    }

    m_operation = BusyOperation::LoadingLiveRig;
    m_live_rig_progress = LiveRigProgress{
        .message = busyMessage(BusyOperation::LoadingLiveRig),
        .fraction = 0.0,
    };
    return true;
}

bool BusyOperationState::setLiveRigLoadProgress(std::string message, double fraction)
{
    if (!isBusy() || *m_operation != BusyOperation::LoadingLiveRig)
    {
        return false;
    }

    m_live_rig_progress = LiveRigProgress{
        .message = std::move(message),
        .fraction = fraction,
    };
    return true;
}

std::optional<BusyViewState> BusyOperationState::viewState() const
{
    if (!m_operation.has_value())
    {
        return std::nullopt;
    }

    BusyViewState busy{
        .operation = *m_operation,
        .message = busyMessage(*m_operation),
        .presentation = busyPresentation(*m_operation),
        .cancel_enabled = false,
    };
    if (*m_operation == BusyOperation::LoadingLiveRig && m_live_rig_progress.has_value())
    {
        busy.message = m_live_rig_progress->message;
        busy.progress = m_live_rig_progress->fraction;
    }

    return busy;
}

} // namespace rock_hero::editor::core
