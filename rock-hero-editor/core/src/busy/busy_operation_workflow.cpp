#include "busy_operation_workflow.h"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <utility>

namespace rock_hero::editor::core
{
namespace
{
[[nodiscard]] double liveRigProgressFraction(
    const common::audio::LiveRigLoadProgress& progress) noexcept
{
    if (progress.total_plugins == 0)
    {
        return 1.0;
    }

    const auto completed =
        static_cast<double>(std::min(progress.completed_plugins, progress.total_plugins));
    return completed / static_cast<double>(progress.total_plugins);
}

[[nodiscard]] std::string liveRigProgressMessage(const common::audio::LiveRigLoadProgress& progress)
{
    if (progress.total_plugins == 0)
    {
        return busyMessage(BusyOperation::LoadingLiveRig);
    }

    const std::size_t display_index =
        std::min(progress.active_plugin_index + 1, progress.total_plugins);
    std::string message = "Loading plugin (" + std::to_string(display_index) + "/" +
                          std::to_string(progress.total_plugins) + ")...";
    if (!progress.active_plugin_name.empty())
    {
        message += "\n" + progress.active_plugin_name;
    }

    return message;
}

[[nodiscard]] double pluginCatalogProgressFraction(
    const common::audio::PluginCatalogScanProgress& progress) noexcept
{
    if (progress.total_plugins == 0)
    {
        return 1.0;
    }

    const auto completed =
        static_cast<double>(std::min(progress.completed_plugins, progress.total_plugins));
    return completed / static_cast<double>(progress.total_plugins);
}

[[nodiscard]] std::string pluginCatalogProgressPathName(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {};
    }

    const std::string filename = path.filename().string();
    return filename.empty() ? path.string() : filename;
}

[[nodiscard]] std::string pluginCatalogProgressMessage(
    const common::audio::PluginCatalogScanProgress& progress)
{
    if (progress.total_plugins == 0)
    {
        return busyMessage(BusyOperation::ScanningPlugins);
    }

    const std::size_t display_index =
        std::min(progress.completed_plugins + 1, progress.total_plugins);
    const std::string plugin_name = pluginCatalogProgressPathName(progress.active_plugin_path);
    std::string message = "Scanning plugin (" + std::to_string(display_index) + "/" +
                          std::to_string(progress.total_plugins) + ")...";
    if (plugin_name.empty())
    {
        return message;
    }

    message += "\n" + plugin_name;
    return message;
}
} // namespace

class BusyOperationWorkflow::PaintGate final
{
public:
    void release()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_released = true;
        }

        m_condition.notify_all();
    }

    void wait()
    {
        std::unique_lock lock(m_mutex);
        static constexpr auto g_max_wait = std::chrono::milliseconds(250);
        m_condition.wait_for(lock, g_max_wait, [this]() { return m_released; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_released{false};
};

BusyOperationWorkflow::BusyOperationWorkflow(
    IMessageThreadScheduler& message_thread_scheduler, RefreshCallback refresh_view)
    : m_message_thread_scheduler(message_thread_scheduler)
    , m_refresh_view(std::move(refresh_view))
    , m_alive(std::make_shared<bool>(true))
{}

BusyOperationWorkflow::~BusyOperationWorkflow()
{
    detachPresentation();
    m_alive.reset();
}

void BusyOperationWorkflow::attachPresentation(PresentationFence ready, PresentationFence cleared)
{
    m_run_after_busy_presentation_ready = std::move(ready);
    m_run_after_busy_presentation_cleared = std::move(cleared);
}

void BusyOperationWorkflow::detachPresentation()
{
    m_run_after_busy_presentation_ready = {};
    m_run_after_busy_presentation_cleared = {};
}

bool BusyOperationWorkflow::isBusy() const noexcept
{
    return m_state.isBusy();
}

std::uint64_t BusyOperationWorkflow::currentToken() const noexcept
{
    return m_state.currentToken();
}

bool BusyOperationWorkflow::isCurrentToken(const std::uint64_t token) const noexcept
{
    return m_state.isCurrentToken(token);
}

std::optional<BusyViewState> BusyOperationWorkflow::viewState() const
{
    return m_state.viewState();
}

std::uint64_t BusyOperationWorkflow::begin(BusyOperation operation)
{
    const auto token = m_state.begin(operation);
    refresh();
    return token;
}

void BusyOperationWorkflow::transition(BusyOperation operation, const std::uint64_t token)
{
    if (m_state.transition(operation, token))
    {
        refresh();
    }
}

void BusyOperationWorkflow::transitionAfterPaintAndWaitFromWorker(
    BusyOperation operation, const std::uint64_t token)
{
    const auto gate = std::make_shared<PaintGate>();
    const auto posted = postToMessageThread([this, gate, operation, token]() {
        if (!isBusy() || !isCurrentToken(token))
        {
            gate->release();
            return;
        }

        transition(operation, token);
        runAfterBusyPresentationReady([gate]() { gate->release(); });
    });

    if (!posted)
    {
        gate->release();
    }

    gate->wait();
}

void BusyOperationWorkflow::finish()
{
    m_state.end();
    refresh();
}

void BusyOperationWorkflow::supersede()
{
    m_state.end();
}

void BusyOperationWorkflow::beginLiveRigLoadProgress()
{
    if (m_state.beginLiveRigLoadProgress())
    {
        refresh();
    }
}

void BusyOperationWorkflow::updateLiveRigLoadProgress(
    const common::audio::LiveRigLoadProgress& progress)
{
    if (setLiveRigLoadProgress(liveRigProgressMessage(progress), liveRigProgressFraction(progress)))
    {
        refresh();
    }
}

void BusyOperationWorkflow::updatePluginCatalogScanProgress(
    const common::audio::PluginCatalogScanProgress& progress)
{
    if (m_state.setPluginCatalogScanProgress(
            pluginCatalogProgressMessage(progress), pluginCatalogProgressFraction(progress)))
    {
        refresh();
    }
}

bool BusyOperationWorkflow::setLiveRigLoadProgress(std::string message, const double fraction)
{
    return m_state.setLiveRigLoadProgress(std::move(message), fraction);
}

void BusyOperationWorkflow::runMessageThreadBusyOperation(
    BusyOperation operation, std::function<void()> work, std::function<void()> after_cleared)
{
    const auto token = begin(operation);
    runAfterBusyPresentationReady(
        [this, token, work = std::move(work), after_cleared = std::move(after_cleared)]() mutable {
            if (!isCurrentToken(token))
            {
                return;
            }

            if (work)
            {
                work();
            }

            if (!isCurrentToken(token))
            {
                return;
            }

            finish();
            runAfterBusyPresentationCleared(std::move(after_cleared));
        });
}

bool BusyOperationWorkflow::postToMessageThread(std::function<void()> callback)
{
    if (!callback)
    {
        return false;
    }

    return m_message_thread_scheduler.postToMessageThread(guard(std::move(callback)));
}

bool BusyOperationWorkflow::callAfterDelay(
    std::chrono::milliseconds delay, std::function<void()> callback)
{
    if (!callback)
    {
        return false;
    }

    return m_message_thread_scheduler.callAfterDelay(delay, guard(std::move(callback)));
}

void BusyOperationWorkflow::refresh()
{
    if (m_refresh_view)
    {
        m_refresh_view();
    }
}

void BusyOperationWorkflow::runAfterBusyPresentationReady(std::function<void()> callback)
{
    if (!callback)
    {
        return;
    }

    auto guarded_callback = guard(std::move(callback));
    if (!m_run_after_busy_presentation_ready)
    {
        guarded_callback();
        return;
    }

    m_run_after_busy_presentation_ready(std::move(guarded_callback));
}

void BusyOperationWorkflow::runAfterBusyPresentationCleared(std::function<void()> callback)
{
    if (!callback)
    {
        return;
    }

    auto guarded_callback = guard(std::move(callback));
    if (!m_run_after_busy_presentation_cleared)
    {
        guarded_callback();
        return;
    }

    m_run_after_busy_presentation_cleared(std::move(guarded_callback));
}

std::function<void()> BusyOperationWorkflow::guard(std::function<void()> callback) const
{
    return [alive = std::weak_ptr<bool>{m_alive}, callback = std::move(callback)]() mutable {
        if (alive.expired() || !callback)
        {
            return;
        }

        callback();
    };
}
} // namespace rock_hero::editor::core
