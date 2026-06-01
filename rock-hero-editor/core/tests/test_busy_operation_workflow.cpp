#include "busy_operation_workflow.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <optional>
#include <rock_hero/editor/core/testing/immediate_message_thread_scheduler.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{
class RejectingMessageThreadScheduler final : public IMessageThreadScheduler
{
public:
    [[nodiscard]] bool postToMessageThread(std::function<void()> /*work*/) override
    {
        return false;
    }

    [[nodiscard]] bool callAfterDelay(
        std::chrono::milliseconds /*delay*/, std::function<void()> /*work*/) override
    {
        return false;
    }
};

[[nodiscard]] BusyOperation visibleBusyOperation(const BusyOperationWorkflow& workflow)
{
    const std::optional<BusyViewState> state = workflow.viewState();
    if (state.has_value())
    {
        return state->operation;
    }

    FAIL("Expected a visible busy operation");
    return BusyOperation::OpeningProject;
}
} // namespace

// The ordered helper starts busy state, waits for the presentation callback, then clears before
// running the post-clear continuation.
TEST_CASE("BusyOperationWorkflow runs operation after busy presentation", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow* workflow_ptr{};
    std::vector<std::optional<BusyViewState>> pushed_states;
    BusyOperationWorkflow workflow{scheduler, [&workflow_ptr, &pushed_states] {
                                       pushed_states.push_back(workflow_ptr->viewState());
                                   }};
    workflow_ptr = &workflow;

    std::function<void()> busy_ready;
    workflow.setRunAfterBusyPresentationReady(
        [&busy_ready](const std::function<void()>& callback) { busy_ready = callback; });
    std::function<void()> busy_cleared;
    workflow.setRunAfterBusyPresentationCleared(
        [&busy_cleared](const std::function<void()>& callback) { busy_cleared = callback; });

    bool work_ran = false;
    bool after_cleared_ran = false;
    bool after_cleared_saw_busy = true;
    workflow.runWithBusyOverlay(
        BusyOperation::OpeningAudioDevice,
        [&work_ran] { work_ran = true; },
        [&workflow, &after_cleared_ran, &after_cleared_saw_busy] {
            after_cleared_ran = true;
            after_cleared_saw_busy = workflow.isBusy();
        });

    CHECK_FALSE(work_ran);
    CHECK(visibleBusyOperation(workflow) == BusyOperation::OpeningAudioDevice);
    REQUIRE(busy_ready);

    busy_ready();

    CHECK(work_ran);
    CHECK_FALSE(after_cleared_ran);
    REQUIRE(busy_cleared);
    busy_cleared();

    CHECK(after_cleared_ran);
    CHECK_FALSE(after_cleared_saw_busy);
    REQUIRE_FALSE(pushed_states.empty());
    CHECK_FALSE(pushed_states.back().has_value());
}

// Superseding a busy token from inside the work callback prevents the old operation from clearing
// the new busy state or running its post-clear continuation.
TEST_CASE("BusyOperationWorkflow ignores stale busy completion", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};
    workflow.setRunAfterBusyPresentationReady([](const std::function<void()>& callback) {
        if (callback)
        {
            callback();
        }
    });

    bool after_cleared_ran = false;
    workflow.runWithBusyOverlay(
        BusyOperation::OpeningProject,
        [&workflow] {
            const std::uint64_t token = workflow.begin(BusyOperation::SavingProject);
            CHECK(token != 0U);
        },
        [&after_cleared_ran] { after_cleared_ran = true; });

    CHECK_FALSE(after_cleared_ran);
    CHECK(visibleBusyOperation(workflow) == BusyOperation::SavingProject);
}

// Worker progress transitions post to the message thread and wait until the presentation callback
// has resolved before returning to the worker.
TEST_CASE("BusyOperationWorkflow transitions worker progress after paint", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    int refresh_count = 0;
    int busy_ready_count = 0;
    BusyOperationWorkflow workflow{scheduler, [&refresh_count] { ++refresh_count; }};
    workflow.setRunAfterBusyPresentationReady(
        [&busy_ready_count](const std::function<void()>& callback) {
            ++busy_ready_count;
            if (callback)
            {
                callback();
            }
        });

    const std::uint64_t token = workflow.begin(BusyOperation::OpeningProject);
    workflow.transitionAfterPaintAndWaitFromWorker(BusyOperation::AnalyzingBackingAudio, token);

    CHECK(visibleBusyOperation(workflow) == BusyOperation::AnalyzingBackingAudio);
    CHECK(refresh_count == 2);
    CHECK(busy_ready_count == 1);
}

// A scheduler that refuses the worker progress post must release the worker instead of hanging.
TEST_CASE(
    "BusyOperationWorkflow releases worker when transition post fails", "[core][busy-workflow]")
{
    RejectingMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};

    const std::uint64_t token = workflow.begin(BusyOperation::OpeningProject);
    workflow.transitionAfterPaintAndWaitFromWorker(BusyOperation::AnalyzingBackingAudio, token);

    CHECK(visibleBusyOperation(workflow) == BusyOperation::OpeningProject);
}

// Delayed callbacks are routed through the injected scheduler so tests can resolve them inline.
TEST_CASE("BusyOperationWorkflow schedules delayed callbacks", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};

    int callback_count = 0;
    const bool scheduled = workflow.callAfterDelay(
        std::chrono::milliseconds{500}, [&callback_count] { ++callback_count; });

    CHECK(scheduled);
    CHECK(callback_count == 1);
}

} // namespace rock_hero::editor::core
