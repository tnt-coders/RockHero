#include "busy_operation_workflow.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
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
    std::function<void()> busy_cleared;
    workflow.attachPresentation(
        [&busy_ready](const std::function<void()>& callback) { busy_ready = callback; },
        [&busy_cleared](const std::function<void()>& callback) { busy_cleared = callback; });

    bool work_ran = false;
    bool after_cleared_ran = false;
    bool after_cleared_saw_busy = true;
    workflow.runMessageThreadBusyOperation(
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

// Detaching presentation callbacks returns the workflow to immediate continuations.
TEST_CASE("BusyOperationWorkflow detaches presentation callbacks", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};

    int ready_callback_count = 0;
    int cleared_callback_count = 0;
    workflow.attachPresentation(
        [&ready_callback_count](const std::function<void()>& callback) {
            ++ready_callback_count;
            if (callback)
            {
                callback();
            }
        },
        [&cleared_callback_count](const std::function<void()>& callback) {
            ++cleared_callback_count;
            if (callback)
            {
                callback();
            }
        });
    workflow.detachPresentation();

    bool work_ran = false;
    bool after_cleared_ran = false;
    workflow.runMessageThreadBusyOperation(
        BusyOperation::OpeningAudioDevice,
        [&work_ran] { work_ran = true; },
        [&after_cleared_ran] { after_cleared_ran = true; });

    CHECK(work_ran);
    CHECK(after_cleared_ran);
    CHECK(ready_callback_count == 0);
    CHECK(cleared_callback_count == 0);
}

// Superseding a busy token from inside the work callback prevents the old operation from clearing
// the new busy state or running its post-clear continuation.
TEST_CASE("BusyOperationWorkflow ignores stale busy completion", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};
    workflow.attachPresentation(
        [](const std::function<void()>& callback) {
            if (callback)
            {
                callback();
            }
        },
        {});

    bool after_cleared_ran = false;
    workflow.runMessageThreadBusyOperation(
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
    workflow.attachPresentation(
        [&busy_ready_count](const std::function<void()>& callback) {
            ++busy_ready_count;
            if (callback)
            {
                callback();
            }
        },
        {});

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

// Plugin catalog progress formats count-based scan updates for the busy overlay.
TEST_CASE("BusyOperationWorkflow formats plugin scan progress", "[core][busy-workflow]")
{
    testing::ImmediateMessageThreadScheduler scheduler;
    BusyOperationWorkflow workflow{scheduler, [] {}};

    const std::uint64_t token = workflow.begin(BusyOperation::ScanningPlugins);
    workflow.updatePluginCatalogScanProgress(
        common::audio::PluginCatalogScanProgress{
            .completed_plugins = 1,
            .total_plugins = 4,
            .active_plugin_path = std::filesystem::path{"Cab.vst3"},
        });

    const std::optional<BusyViewState> state = workflow.viewState();
    REQUIRE(state.has_value());
    if (state.has_value())
    {
        CHECK(workflow.isCurrentToken(token));
        CHECK(state->operation == BusyOperation::ScanningPlugins);
        CHECK(state->message == "Scanning plugin (2/4)...\nCab.vst3");
        CHECK(state->indicator == BusyIndicator::DeterminateProgress);
        CHECK(state->progress == std::optional<double>{0.25});
    }
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
