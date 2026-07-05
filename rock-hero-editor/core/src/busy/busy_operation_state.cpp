#include "busy_operation_state.h"

#include <utility>

namespace rock_hero::editor::core
{
namespace
{

// Only the plugin catalog scan can be cancelled: it runs on a worker without touching the live
// session, so the user can hard-stop it and keep the plugins already discovered. Project
// open/import replace the active session as they load, so they run to completion.
[[nodiscard]] bool isCancellableBusyOperation(BusyOperation operation) noexcept
{
    switch (operation)
    {
        case BusyOperation::ScanningPlugins:
        {
            return true;
        }
        case BusyOperation::OpeningProject:
        case BusyOperation::ImportingProject:
        case BusyOperation::LoadingLiveRig:
        case BusyOperation::AnalyzingBackingAudio:
        case BusyOperation::SavingProject:
        case BusyOperation::SavingProjectAs:
        case BusyOperation::PublishingProject:
        case BusyOperation::OpeningAudioDevice:
        case BusyOperation::LoadingPlugin:
        {
            return false;
        }
    }

    return false;
}

} // namespace

bool BusyOperationState::isBusy() const noexcept
{
    return m_operation.has_value();
}

std::uint64_t BusyOperationState::begin(BusyOperation operation) noexcept
{
    m_current_token += 1;
    m_operation = operation;
    m_determinate_progress.reset();
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
    m_determinate_progress.reset();
    return true;
}

void BusyOperationState::end() noexcept
{
    m_operation.reset();
    m_determinate_progress.reset();
    m_current_token += 1;
}

bool BusyOperationState::beginLiveRigLoadProgress()
{
    if (!isBusy())
    {
        return false;
    }

    m_operation = BusyOperation::LoadingLiveRig;
    m_determinate_progress = DeterminateProgress{
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

    m_determinate_progress = DeterminateProgress{
        .message = std::move(message),
        .fraction = fraction,
    };
    return true;
}

bool BusyOperationState::setPluginCatalogScanProgress(std::string message, double fraction)
{
    if (!isBusy() || *m_operation != BusyOperation::ScanningPlugins)
    {
        return false;
    }

    m_determinate_progress = DeterminateProgress{
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
        .indicator = busyIndicator(*m_operation),
        .cancel_enabled = isCancellableBusyOperation(*m_operation),
    };
    if (m_determinate_progress.has_value())
    {
        busy.message = m_determinate_progress->message;
        busy.indicator = BusyIndicator::DeterminateProgress;
        busy.progress = m_determinate_progress->fraction;
    }

    return busy;
}

} // namespace rock_hero::editor::core
