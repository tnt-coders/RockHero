#include "busy_operation_state.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>

namespace rock_hero::editor::core
{

// Idle state has no current operation token, even before the first operation begins.
TEST_CASE("BusyOperationState has no current token while idle", "[core][busy-operation-state]")
{
    const BusyOperationState state;

    CHECK_FALSE(state.isBusy());
    CHECK_FALSE(state.isCurrentToken(state.currentToken()));
    CHECK_FALSE(state.viewState().has_value());
}

// Starting a busy operation creates a current token and the default view-state snapshot.
TEST_CASE("BusyOperationState begins operation", "[core][busy-operation-state]")
{
    BusyOperationState state;

    const std::uint64_t token = state.begin(BusyOperation::OpeningProject);

    CHECK(state.isBusy());
    CHECK(token == 1);
    CHECK(state.isCurrentToken(token));
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::OpeningProject);
        CHECK(busy->message == "Opening project...");
        CHECK(busy->indicator == BusyIndicator::IndeterminateProgress);
        CHECK_FALSE(busy->progress.has_value());
        CHECK(busy->cancel_enabled == false);
    }
}

// Token-matched transitions update the visible phase without invalidating the operation.
TEST_CASE("BusyOperationState transitions current token", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::OpeningProject);

    const bool transitioned = state.transition(BusyOperation::AnalyzingBackingAudio, token);

    CHECK(transitioned);
    CHECK(state.isCurrentToken(token));
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::AnalyzingBackingAudio);
        CHECK(busy->message == "Analyzing audio...");
    }
}

// Stale tokens are rejected and cannot replace the current visible operation.
TEST_CASE("BusyOperationState rejects stale transition", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t stale_token = state.begin(BusyOperation::OpeningProject);
    state.end();
    const std::uint64_t current_token = state.begin(BusyOperation::SavingProject);

    const bool transitioned = state.transition(BusyOperation::AnalyzingBackingAudio, stale_token);

    CHECK_FALSE(transitioned);
    CHECK(state.isCurrentToken(current_token));
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::SavingProject);
    }
}

// Ending busy state clears the snapshot and invalidates work that captured the old token.
TEST_CASE("BusyOperationState ends operation", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::ImportingProject);

    state.end();

    CHECK_FALSE(state.isBusy());
    CHECK_FALSE(state.isCurrentToken(token));
    CHECK_FALSE(state.viewState().has_value());
}

// Live-rig load progress overrides the default message only for the determinate live-rig phase.
TEST_CASE("BusyOperationState exposes live rig progress", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::OpeningProject);

    const bool started = state.beginLiveRigLoadProgress();
    const bool updated = state.setLiveRigLoadProgress("Loading Amp (1 of 2)...", 0.5);

    CHECK(state.isCurrentToken(token));
    CHECK(started);
    CHECK(updated);
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::LoadingLiveRig);
        CHECK(busy->message == "Loading Amp (1 of 2)...");
        CHECK(busy->indicator == BusyIndicator::DeterminateProgress);
        CHECK(busy->progress == std::optional{0.5});
    }
}

// Plugin catalog progress switches scanning from its default indeterminate state to determinate.
TEST_CASE("BusyOperationState exposes plugin scan progress", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::ScanningPlugins);

    const bool updated = state.setPluginCatalogScanProgress("Scanning Amp (1 of 2)...", 0.5);

    CHECK(state.isCurrentToken(token));
    CHECK(updated);
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::ScanningPlugins);
        CHECK(busy->message == "Scanning Amp (1 of 2)...");
        CHECK(busy->indicator == BusyIndicator::DeterminateProgress);
        CHECK(busy->progress == std::optional{0.5});
    }
}

// Generic transitions clear any determinate progress payload before exposing the new phase.
TEST_CASE("BusyOperationState transition clears live rig progress", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::OpeningProject);
    REQUIRE(state.beginLiveRigLoadProgress());
    REQUIRE(state.setLiveRigLoadProgress("Loading Amp (1 of 2)...", 0.5));

    const bool transitioned = state.transition(BusyOperation::AnalyzingBackingAudio, token);

    CHECK(transitioned);
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::AnalyzingBackingAudio);
        CHECK_FALSE(busy->progress.has_value());
    }
}

// Progress updates outside their matching phase are ignored.
TEST_CASE(
    "BusyOperationState ignores progress outside live rig phase", "[core][busy-operation-state]")
{
    BusyOperationState state;
    const std::uint64_t token = state.begin(BusyOperation::OpeningProject);

    const bool updated = state.setLiveRigLoadProgress("Ignored", 0.5);

    CHECK(state.isCurrentToken(token));
    CHECK_FALSE(updated);
    const std::optional<BusyViewState> busy = state.viewState();
    REQUIRE(busy.has_value());
    if (busy.has_value())
    {
        CHECK(busy->operation == BusyOperation::OpeningProject);
        CHECK_FALSE(busy->progress.has_value());
    }

    const bool plugin_scan_updated = state.setPluginCatalogScanProgress("Ignored", 0.5);
    CHECK_FALSE(plugin_scan_updated);
}

} // namespace rock_hero::editor::core
