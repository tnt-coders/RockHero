/*!
\file busy_operation_workflow.h
\brief Private editor busy-operation orchestration workflow.
*/

#pragma once

#include "busy/busy_operation_state.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_live_rig.h>
#include <rock_hero/common/audio/plugin_catalog_scan_progress.h>
#include <rock_hero/editor/core/i_message_thread_scheduler.h>
#include <string>

namespace rock_hero::editor::core
{
class BusyOperationWorkflow final
{
public:
    using RefreshCallback = std::function<void()>;
    using Continuation = std::function<void()>;
    using PresentationFence = std::function<void(Continuation)>;

    BusyOperationWorkflow(
        IMessageThreadScheduler& message_thread_scheduler, RefreshCallback refresh_view);
    ~BusyOperationWorkflow();

    BusyOperationWorkflow(const BusyOperationWorkflow&) = delete;
    BusyOperationWorkflow& operator=(const BusyOperationWorkflow&) = delete;
    BusyOperationWorkflow(BusyOperationWorkflow&&) = delete;
    BusyOperationWorkflow& operator=(BusyOperationWorkflow&&) = delete;

    void attachPresentation(PresentationFence ready, PresentationFence cleared);
    void detachPresentation();

    [[nodiscard]] bool isBusy() const noexcept;
    [[nodiscard]] std::uint64_t currentToken() const noexcept;
    [[nodiscard]] bool isCurrentToken(std::uint64_t token) const noexcept;
    [[nodiscard]] std::optional<BusyViewState> viewState() const;

    [[nodiscard]] std::uint64_t begin(BusyOperation operation);
    void transition(BusyOperation operation, std::uint64_t token);
    /*!
    \brief Transitions busy state, waits for presentation readiness, then returns to worker work.

    Worker-only: calling this from a real UI message thread blocks the paint it is waiting for
    until the timeout elapses.
    */
    void transitionAfterPaintAndWaitFromWorker(BusyOperation operation, std::uint64_t token);
    void finish();
    void supersede();

    void beginLiveRigLoadProgress();
    void updateLiveRigLoadProgress(const common::audio::LiveRigLoadProgress& progress);
    void updatePluginCatalogScanProgress(const common::audio::PluginCatalogScanProgress& progress);

    [[nodiscard]] bool setLiveRigLoadProgress(std::string message, double fraction);

    void runMessageThreadBusyOperation(
        BusyOperation operation, std::function<void()> work,
        std::function<void()> after_cleared = {});

    /*!
    \brief Runs a callback on the message thread once the busy overlay has painted.

    The callback is not offloaded to a worker thread; it runs synchronously on the message thread
    after the busy overlay is visible. This lets a blocking message-thread operation, such as plugin
    instantiation, start only after the "busy" overlay has actually appeared, so the user sees it
    before that operation freezes the thread.
    */
    void runAfterBusyPresentationReady(std::function<void()> callback);

    [[nodiscard]] bool postToMessageThread(std::function<void()> callback);
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> callback);

private:
    class PaintGate;

    void refresh();
    void runAfterBusyPresentationCleared(std::function<void()> callback);
    [[nodiscard]] std::function<void()> guard(std::function<void()> callback) const;

    IMessageThreadScheduler& m_message_thread_scheduler;
    RefreshCallback m_refresh_view;
    PresentationFence m_run_after_busy_presentation_ready;
    PresentationFence m_run_after_busy_presentation_cleared;
    BusyOperationState m_state;
    std::shared_ptr<bool> m_alive;
};
} // namespace rock_hero::editor::core
