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
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/plugin/plugin_catalog_scan_progress.h>
#include <rock_hero/editor/core/tasks/i_message_thread_scheduler.h>
#include <string>

namespace rock_hero::editor::core
{
/*!
\brief Sequences editor busy operations against the busy overlay's paint and clear.

Wraps BusyOperationState with the message-thread plumbing the controller needs: republishing view
state on every busy change, deferring work until the overlay has actually painted, and dropping
scheduled callbacks once the workflow is destroyed.
*/
class BusyOperationWorkflow final
{
public:
    /*! \brief Republishes editor view state after a busy-state change. */
    using RefreshCallback = std::function<void()>;

    /*! \brief Work deferred until the overlay reaches a presentation milestone. */
    using Continuation = std::function<void()>;

    /*! \brief View hook that runs a continuation once the overlay has painted or cleared. */
    using PresentationFence = std::function<void(Continuation)>;

    /*!
    \brief Creates an idle workflow.
    \param message_thread_scheduler Scheduler used to post guarded callbacks.
    \param refresh_view Callback that republishes view state after each busy change.
    */
    BusyOperationWorkflow(
        IMessageThreadScheduler& message_thread_scheduler, RefreshCallback refresh_view);

    /*! \brief Detaches the presentation and invalidates every outstanding guarded callback. */
    ~BusyOperationWorkflow();

    /*! \brief Copying is disabled because scheduled callbacks capture this workflow. */
    BusyOperationWorkflow(const BusyOperationWorkflow&) = delete;

    /*!
    \brief Copy assignment is disabled because scheduled callbacks capture this workflow.
    \return Reference to this workflow.
    */
    BusyOperationWorkflow& operator=(const BusyOperationWorkflow&) = delete;

    /*! \brief Moving is disabled because scheduled callbacks capture this workflow's address. */
    BusyOperationWorkflow(BusyOperationWorkflow&&) = delete;

    /*!
    \brief Move assignment is disabled because scheduled callbacks capture this workflow.
    \return Reference to this workflow.
    */
    BusyOperationWorkflow& operator=(BusyOperationWorkflow&&) = delete;

    /*!
    \brief Installs the view's paint and clear fences.
    \param ready Fence that runs its continuation once the busy overlay has painted.
    \param cleared Fence that runs its continuation once the busy overlay has cleared.
    */
    void attachPresentation(PresentationFence ready, PresentationFence cleared);

    /*! \brief Removes the view's fences; later fenced callbacks then run immediately. */
    void detachPresentation();

    /*!
    \brief Reports whether an operation is currently active.
    \return True when a busy operation is active.
    */
    [[nodiscard]] bool isBusy() const noexcept;

    /*!
    \brief Returns the current busy token generation.
    \return Current token value.
    */
    [[nodiscard]] std::uint64_t currentToken() const noexcept;

    /*!
    \brief Reports whether a token still names the active operation.
    \param token Token captured when an operation began.
    \return True when the token matches the active operation's generation.
    */
    [[nodiscard]] bool isCurrentToken(std::uint64_t token) const noexcept;

    /*!
    \brief Builds the current busy view-state snapshot.
    \return Busy view state, or empty when no operation is active.
    */
    [[nodiscard]] std::optional<BusyViewState> viewState() const;

    /*!
    \brief Starts a new operation, invalidating earlier tokens, and republishes view state.
    \param operation Operation to show.
    \return Token identifying the new operation.
    */
    [[nodiscard]] std::uint64_t begin(BusyOperation operation);

    /*!
    \brief Moves the active operation to a new phase and republishes view state.
    \param operation Operation phase to show next.
    \param token Token of the operation; a stale token is ignored.
    */
    void transition(BusyOperation operation, std::uint64_t token);

    /*!
    \brief Transitions busy state, waits for presentation readiness, then returns to worker work.

    Worker-only: calling this from a real UI message thread blocks the paint it is waiting for
    until the timeout elapses.

    \param operation Operation phase to show next.
    \param token Token of the operation; a stale token releases the wait immediately.
    */
    void transitionAfterPaintAndWaitFromWorker(BusyOperation operation, std::uint64_t token);

    /*! \brief Ends the active operation and republishes view state. */
    void finish();

    /*!
    \brief Ends the active operation without republishing view state.

    For callers that take over the busy state (close, exit, scan cancel) and publish the view
    themselves once their own teardown is done.
    */
    void supersede();

    /*! \brief Switches the active operation into determinate live-rig load progress. */
    void beginLiveRigLoadProgress();

    /*!
    \brief Publishes a live-rig load progress report as overlay message and fraction.
    \param progress Progress reported by the live rig while restoring plugins.
    */
    void updateLiveRigLoadProgress(const common::audio::LiveRigLoadProgress& progress);

    /*!
    \brief Publishes a plugin catalog scan progress report as overlay message and fraction.
    \param progress Progress reported by the plugin catalog scan.
    */
    void updatePluginCatalogScanProgress(const common::audio::PluginCatalogScanProgress& progress);

    /*!
    \brief Stores live-rig load progress without republishing view state.
    \param message User-facing progress message.
    \param fraction Determinate progress fraction.
    \return True when the active operation is a live-rig load and the progress was stored.
    */
    [[nodiscard]] bool setLiveRigLoadProgress(std::string message, double fraction);

    /*!
    \brief Runs blocking message-thread work behind the busy overlay.

    Begins the operation, runs work once the overlay has painted, finishes the operation, and runs
    after_cleared once the overlay has cleared. A supersede while work runs skips the rest.

    \param operation Operation to show while work runs.
    \param work Message-thread work to run after the overlay has painted.
    \param after_cleared Optional follow-up to run after the overlay has cleared.
    */
    void runMessageThreadBusyOperation(
        BusyOperation operation, std::function<void()> work,
        std::function<void()> after_cleared = {});

    /*!
    \brief Runs a callback on the message thread once the busy overlay has painted.

    The callback is not offloaded to a worker thread; it runs synchronously on the message thread
    after the busy overlay is visible. This lets a blocking message-thread operation, such as plugin
    instantiation, start only after the "busy" overlay has actually appeared, so the user sees it
    before that operation freezes the thread.

    \param callback Message-thread callback to run after the overlay has painted.
    */
    void runAfterBusyPresentationReady(std::function<void()> callback);

    /*!
    \brief Posts a callback that is dropped if this workflow is destroyed before it runs.
    \param callback Callback to invoke on the message thread.
    \return True when the scheduler accepted the callback.
    */
    [[nodiscard]] bool postToMessageThread(std::function<void()> callback);

    /*!
    \brief Posts a delayed callback that is dropped if this workflow is destroyed first.
    \param delay Minimum delay before the callback should run.
    \param callback Callback to invoke on the message thread.
    \return True when the scheduler accepted the callback.
    */
    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds delay, std::function<void()> callback);

private:
    // Blocks a worker until the message thread reports the overlay painted, with a timeout.
    class PaintGate;

    void refresh();
    void runAfterBusyPresentationCleared(std::function<void()> callback);

    // Wraps a callback so it becomes a no-op once this workflow has been destroyed.
    [[nodiscard]] std::function<void()> guard(std::function<void()> callback) const;

    IMessageThreadScheduler& m_message_thread_scheduler;
    RefreshCallback m_refresh_view;
    PresentationFence m_run_after_busy_presentation_ready;
    PresentationFence m_run_after_busy_presentation_cleared;
    BusyOperationState m_state;

    // Liveness token for guard(): callbacks hold a weak_ptr and skip once it expires.
    std::shared_ptr<bool> m_alive;
};
} // namespace rock_hero::editor::core
