#include "editor_controller_impl.h"
#include "signal_chain/signal_chain_edits.h"

#include <cassert>
#include <rock_hero/common/core/shared/logger.h>

namespace rock_hero::editor::core
{

namespace
{

// Captures editor-owned signal-chain block placement with instance IDs for undo edits.
[[nodiscard]] std::vector<PluginBlockAssignment> pluginBlockAssignmentsFor(
    const std::vector<PluginViewState>& plugins)
{
    std::vector<PluginBlockAssignment> assignments;
    assignments.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        assignments.push_back(
            PluginBlockAssignment{
                .instance_id = plugin.instance_id,
                .block_index = plugin.block_index,
            });
    }

    return assignments;
}

// Checks whether a plugin-host mutation snapshot still contains a supposedly removed instance.
[[nodiscard]] bool pluginSnapshotContainsInstance(
    const common::audio::PluginChainSnapshot& snapshot, const std::string& instance_id)
{
    return std::ranges::any_of(snapshot.plugins, [&instance_id](const auto& plugin) {
        return plugin.instance_id == instance_id;
    });
}

// Reads the current display override for one plugin row.
[[nodiscard]] std::optional<PluginDisplayType> displayTypeOverrideFor(
    const std::vector<PluginViewState>& plugins, std::string_view instance_id)
{
    const auto plugin = std::ranges::find_if(plugins, [instance_id](const PluginViewState& item) {
        return item.instance_id == instance_id;
    });
    if (plugin == plugins.end())
    {
        return std::nullopt;
    }

    return plugin->display_type_override;
}

// Captures editor-owned visual state for one plugin row before an audio mutation removes it.
[[nodiscard]] std::optional<PluginVisualEditState> pluginVisualStateFor(
    const std::vector<PluginViewState>& plugins, std::string_view instance_id)
{
    const auto plugin = std::ranges::find_if(plugins, [instance_id](const PluginViewState& item) {
        return item.instance_id == instance_id;
    });
    if (plugin == plugins.end())
    {
        return std::nullopt;
    }

    return PluginVisualEditState{
        .instance_id = plugin->instance_id,
        .block_index = plugin->block_index,
        .display_type_override = plugin->display_type_override,
    };
}

} // namespace

// Requests cooperative worker cancellation, then keeps whatever the host already published as known
// and invalidates the pending scan completion.
void EditorController::Impl::cancelPluginCatalogScan()
{
    cancelActiveScanToken();
    m_busy.supersede();
    refreshKnownPluginCatalog();
    updateView();
}

// Trips the in-flight plugin scan's cancellation token so the scan worker stops at the next
// candidate. Used for explicit cancellation and whenever a scan is abandoned by a takeover (close,
// exit) or controller teardown, so a long scan never keeps running unobserved.
void EditorController::Impl::cancelActiveScanToken()
{
    if (m_plugin_scan_cancel.has_value())
    {
        m_plugin_scan_cancel->cancel();
        m_plugin_scan_cancel.reset();
    }
}

// Pushes a net-changed output-gain command into product-level history.
void EditorController::Impl::pushOutputGainUndoEntry(
    common::audio::Gain before_gain, common::audio::Gain after_gain)
{
    if (before_gain == after_gain)
    {
        return;
    }

    RH_LOG_INFO(
        "editor.controller",
        "Completed output gain edit before_db={} after_db={}",
        before_gain.db,
        after_gain.db);
    auto edit = std::make_unique<OutputGainEdit>();
    edit->before_gain = before_gain;
    edit->after_gain = after_gain;
    pushUndoEntry(std::move(edit));
}

// Applies output gain previews immediately, but records only committed values in undo history.
void EditorController::Impl::applyOutputGainChange(double gain_db, OutputGainChangeIntent intent)
{
    if (!m_project_audio_ready || !hasLoadedArrangement() || isBusy() || m_session_faulted)
    {
        return;
    }

    const bool is_commit = intent == OutputGainChangeIntent::Commit;
    if (is_commit)
    {
        if (!m_output_gain_preview_before.has_value())
        {
            flushPendingPluginEdits("plugin_edit.output_gain_commit");
        }
    }
    else if (!m_output_gain_preview_before.has_value())
    {
        flushPendingPluginEdits("plugin_edit.output_gain_preview");
        m_output_gain_preview_before = common::audio::Gain{m_output_gain_db};
    }

    const common::audio::Gain before_gain =
        m_output_gain_preview_before.value_or(common::audio::Gain{m_output_gain_db});
    const auto gain = common::audio::clampGain(common::audio::Gain{gain_db});
    const bool needs_live_update = gain.db != m_output_gain_db;

    if (!needs_live_update)
    {
        if (is_commit && m_output_gain_preview_before.has_value())
        {
            pushOutputGainUndoEntry(before_gain, common::audio::Gain{m_output_gain_db});
            m_output_gain_preview_before.reset();
            updateView();
        }
        return;
    }

    const auto result = m_live_rig.setOutputGain(gain);
    if (!result.has_value())
    {
        reportError(std::string{"Could not set output gain: "} + result.error().message);
        if (is_commit && m_output_gain_preview_before.has_value())
        {
            pushOutputGainUndoEntry(before_gain, common::audio::Gain{m_output_gain_db});
        }
        m_output_gain_preview_before.reset();
        updateView();
        return;
    }

    m_output_gain_db = gain.db;
    if (is_commit)
    {
        pushOutputGainUndoEntry(before_gain, gain);
        m_output_gain_preview_before.reset();
    }
    updateView();
}

// Settles any host-observed plugin edit before a controller action runs.
void EditorController::Impl::flushPendingPluginEdits(std::string_view context)
{
    if (!m_plugin_host.hasPendingPluginEdits())
    {
        return;
    }

    RH_LOG_INFO("editor.controller", "Flushing pending plugin edit context={:?}", context);
    m_plugin_host.flushPendingPluginEdits();
}

// Reflects host plugin-edit observation state transitions into logs and derived view state.
void EditorController::Impl::onPluginEditPendingChanged(bool pending)
{
    RH_LOG_INFO("editor.controller", "Plugin edit pending changed pending={}", pending);
    updateView();
}

// Commits one settled host processor-wide state edit into product-level undo history.
void EditorController::Impl::onPluginStateEditCompleted(common::audio::PluginStateEdit edit)
{
    if (!hasLoadedArrangement() || !m_signal_chain.containsInstance(edit.instance_id))
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped stale plugin state edit instance_id={:?}",
            edit.instance_id);
        return;
    }

    if (edit.before == edit.after)
    {
        RH_LOG_INFO(
            "editor.controller",
            "Dropped unchanged plugin state edit instance_id={:?}",
            edit.instance_id);
        return;
    }

    RH_LOG_INFO(
        "editor.controller",
        "Completed plugin state edit instance_id={:?} label_hint={:?}",
        edit.instance_id,
        edit.label_hint);
    auto undo_edit = std::make_unique<PluginStateEdit>();
    undo_edit->instance_id = std::move(edit.instance_id);
    undo_edit->before_state = std::move(edit.before);
    undo_edit->after_state = std::move(edit.after);
    undo_edit->label_hint = std::move(edit.label_hint);
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Makes the browser visible and refreshes the lightweight in-memory catalog.
void EditorController::Impl::performActionImpl(EditorAction::ShowPluginBrowser /*action*/)
{
    if (!hasLoadedArrangement() || !m_signal_chain.hasInsertCapacity())
    {
        return;
    }

    m_signal_chain.requestAppend();
    m_plugin_catalog.open(m_plugin_host.knownPluginCatalog());
    updateView();
}

// Makes the browser visible for a specific chain slot and refreshes the lightweight catalog.
void EditorController::Impl::performActionImpl(EditorAction::BeginPluginInsert action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.requestInsertAt(action.chain_index))
    {
        return;
    }

    // Record the chosen visual block so the insertion snapshot keeps the authored gap layout.
    m_signal_chain.setPendingInsertBlock(action.block_index);
    m_plugin_catalog.open(m_plugin_host.knownPluginCatalog());
    updateView();
}

// Offloads catalog scanning to the editor task runner because directory traversal and plugin
// inspection can execute slow third-party code.
void EditorController::Impl::performActionImpl(EditorAction::ScanPluginCatalog /*action*/)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    auto state = std::make_shared<PluginCatalogTaskState>();
    const std::uint64_t token = beginBusy(BusyOperation::ScanningPlugins);
    const common::core::CancellationToken cancel;
    m_plugin_scan_cancel = cancel;
    auto report_progress = makePluginCatalogScanProgress(token);
    m_task_runner.submit(
        [state,
         plugin_host = &m_plugin_host,
         report_progress = std::move(report_progress),
         cancel] { state->scan_result = plugin_host->scanPluginCatalog(report_progress, cancel); },
        safeCallback([this, state, token] {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completePluginCatalogScan(state);
        }));
}

// Begins inserting the selected browser plugin. The catalog is the authority for display
// metadata, while the audio boundary remains the authority for creating the runtime plugin.
void EditorController::Impl::performActionImpl(const EditorAction::InsertSelectedPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<common::audio::PluginCandidate> plugin_candidate =
        m_plugin_catalog.candidateForId(action.plugin_id);
    if (!plugin_candidate.has_value())
    {
        reportError("Could not insert plugin: selected plugin is no longer available");
        updateView();
        return;
    }

    const std::optional<std::size_t> chain_index = m_signal_chain.insertionIndexForSelection();
    if (!chain_index.has_value())
    {
        reportError("Could not insert plugin: insertion position is no longer available");
        updateView();
        return;
    }

    beginInsertKnownPlugin(*plugin_candidate, *chain_index);
}

// Inserts the selected browser plugin into the live chain after the loading state has painted.
void EditorController::Impl::completeSelectedPluginInsert(
    const std::shared_ptr<InsertSelectedPluginTaskState>& state)
{
    assert(isBusy() && "completeSelectedPluginInsert called outside a busy operation");

    const common::audio::PluginCandidate& plugin_candidate = state->plugin_candidate;
    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto insert_result = m_plugin_host.insertPlugin(plugin_candidate, state->chain_index);
    if (!insert_result.has_value())
    {
        const std::string message = insert_result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not insert plugin: "} + message);
        return;
    }

    const std::string inserted_instance_id = insert_result->inserted_instance_id;
    applySignalChainMutationSnapshot(insert_result->snapshot);
    auto inserted_state = m_plugin_host.capturePluginState(inserted_instance_id);
    const std::optional<PluginVisualEditState> visual_state =
        pluginVisualStateFor(m_signal_chain.plugins(), inserted_instance_id);
    if (!inserted_state.has_value() || !visual_state.has_value())
    {
        m_signal_chain.clearPendingInsertion();
        m_plugin_catalog.hide();
        const std::string undo_preparation_error = inserted_state.has_value()
                                                       ? "inserted plugin view state is missing"
                                                       : inserted_state.error().message;
        const InsertUndoPreparationRollbackResult rollback =
            rollbackInsertedPluginAfterUndoPreparationFailure(
                inserted_instance_id, before_placement);
        if (rollback.status == InsertUndoPreparationRollbackStatus::Failed)
        {
            markUntrackedUnsavedEdit("undo.reset.untracked_signal_chain_insert");
        }
        finishBusyOperation();

        switch (rollback.status)
        {
            case InsertUndoPreparationRollbackStatus::RolledBack:
            {
                reportError(
                    std::string{"Could not insert plugin: undo state could not be prepared; insert "
                                "was rolled back: "} +
                    undo_preparation_error);
                return;
            }
            case InsertUndoPreparationRollbackStatus::Failed:
            {
                reportError(
                    std::string{"Could not prepare plugin insert undo: "} + undo_preparation_error +
                    ". The inserted plugin remains loaded because rollback failed: " +
                    rollback.detail);
                return;
            }
            case InsertUndoPreparationRollbackStatus::RollbackContractViolation:
            {
                faultSessionAfterRollbackContractViolation(
                    "insert.undo_preparation_rollback", rollback.detail);
                return;
            }
        }

        return;
    }

    auto undo_edit = std::make_unique<PluginInsertEdit>();
    undo_edit->instance_id = inserted_instance_id;
    undo_edit->chain_index = state->chain_index;
    undo_edit->plugin_state = std::move(*inserted_state);
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    undo_edit->visual_state = *visual_state;
    pushUndoEntry(std::move(undo_edit));

    m_signal_chain.clearPendingInsertion();
    m_plugin_catalog.hide();

    finishBusyOperation();
}

// Removes a just-inserted plugin when undo-entry preparation fails, preserving existing history.
auto EditorController::Impl::rollbackInsertedPluginAfterUndoPreparationFailure(
    const std::string& inserted_instance_id,
    const std::vector<PluginBlockAssignment>& before_placement)
    -> InsertUndoPreparationRollbackResult
{
    auto rollback_snapshot = m_plugin_host.removePlugin(inserted_instance_id);
    if (!rollback_snapshot.has_value())
    {
        if (rollback_snapshot.error().code ==
            common::audio::PluginHostErrorCode::RollbackContractViolation)
        {
            return InsertUndoPreparationRollbackResult{
                .status = InsertUndoPreparationRollbackStatus::RollbackContractViolation,
                .detail = rollback_snapshot.error().message,
            };
        }

        return InsertUndoPreparationRollbackResult{
            .status = InsertUndoPreparationRollbackStatus::Failed,
            .detail = rollback_snapshot.error().message,
        };
    }

    if (pluginSnapshotContainsInstance(*rollback_snapshot, inserted_instance_id))
    {
        return InsertUndoPreparationRollbackResult{
            .status = InsertUndoPreparationRollbackStatus::RollbackContractViolation,
            .detail = "inserted plugin was still present after rollback removal",
        };
    }

    applySignalChainMutationSnapshot(*rollback_snapshot);
    (void)m_signal_chain.setBlockPlacement(before_placement);
    return {};
}

// Starts the blocking plugin-instantiation phase after pushing LoadingPlugin state first.
void EditorController::Impl::beginInsertKnownPlugin(
    const common::audio::PluginCandidate& plugin_candidate, std::size_t chain_index)
{
    auto state = std::make_shared<InsertSelectedPluginTaskState>();
    state->plugin_candidate = plugin_candidate;
    state->chain_index = chain_index;

    const std::uint64_t token = beginBusy(BusyOperation::LoadingPlugin);
    m_busy.runAfterBusyPresentationReady([this, state, token]() {
        if (!m_busy.isCurrentToken(token))
        {
            return;
        }
        completeSelectedPluginInsert(state);
    });
}

// Applies a successful structural chain mutation from the audio boundary.
void EditorController::Impl::applySignalChainMutationSnapshot(
    const common::audio::PluginChainSnapshot& snapshot)
{
    m_signal_chain.replaceSnapshot(snapshot);
}

// Replaces the browser catalog with the latest scan result while keeping the browser open.
void EditorController::Impl::completePluginCatalogScan(
    const std::shared_ptr<PluginCatalogTaskState>& state)
{
    assert(isBusy() && "completePluginCatalogScan called outside a busy operation");

    m_plugin_scan_cancel.reset();

    if (!state->scan_result.has_value())
    {
        const std::string message = state->scan_result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not scan plugins: "} + message);
        return;
    }

    // Rescan refreshes the host-owned catalog on a worker thread. The controller reads the
    // canonical known catalog here on the message thread and keeps only a sorted UI snapshot.
    refreshKnownPluginCatalog();
    finishBusyOperation();
}

// Refreshes the browser from Tracktion's already-known plugins without touching the filesystem.
void EditorController::Impl::refreshKnownPluginCatalog()
{
    m_plugin_catalog.replaceCatalog(m_plugin_host.knownPluginCatalog());
}

void EditorController::Impl::performActionImpl(const EditorAction::RemovePlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.containsInstance(action.instance_id))
    {
        return;
    }

    const std::optional<std::size_t> chain_index =
        m_signal_chain.chainIndexForInstance(action.instance_id);
    const std::optional<PluginVisualEditState> visual_state =
        pluginVisualStateFor(m_signal_chain.plugins(), action.instance_id);
    if (!chain_index.has_value() || !visual_state.has_value())
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto plugin_state = m_plugin_host.capturePluginState(action.instance_id);
    if (!plugin_state.has_value())
    {
        reportError(std::string{"Could not remove plugin: "} + plugin_state.error().message);
        updateView();
        return;
    }

    auto snapshot = m_plugin_host.removePlugin(action.instance_id);
    if (!snapshot.has_value())
    {
        reportError(std::string{"Could not remove plugin: "} + snapshot.error().message);
        updateView();
        return;
    }

    applySignalChainMutationSnapshot(*snapshot);
    auto undo_edit = std::make_unique<PluginRemoveEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->chain_index = *chain_index;
    undo_edit->plugin_state = std::move(*plugin_state);
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    undo_edit->visual_state = *visual_state;
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Moves a plugin through the audio boundary so backend order remains authoritative.
void EditorController::Impl::performActionImpl(const EditorAction::MovePlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<std::size_t> current_index =
        m_signal_chain.chainIndexForInstance(action.instance_id);
    if (!current_index.has_value() || action.destination_index >= m_signal_chain.appendIndex() ||
        *current_index == action.destination_index)
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    auto snapshot = m_plugin_host.movePlugin(action.instance_id, action.destination_index);
    if (!snapshot.has_value())
    {
        reportError(std::string{"Could not move plugin: "} + snapshot.error().message);
        updateView();
        return;
    }

    applySignalChainMutationSnapshot(*snapshot);
    // The reorder already changed chain state, so the view must refresh whether or not the
    // instance-keyed placement differed; the [[nodiscard]] result is intentionally ignored.
    (void)m_signal_chain.setBlockPlacement(action.placement);
    auto undo_edit = std::make_unique<PluginMoveEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->before_index = *current_index;
    undo_edit->after_index = action.destination_index;
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

// Stores a placement-only edit at the controller boundary that receives user placement intents.
void EditorController::Impl::performActionImpl(const EditorAction::SetSignalChainPlacement& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::vector<PluginBlockAssignment> before_placement =
        pluginBlockAssignmentsFor(m_signal_chain.plugins());
    if (!m_signal_chain.setBlockPlacement(action.placement))
    {
        return;
    }

    auto undo_edit = std::make_unique<PluginPlacementEdit>();
    undo_edit->before_placement = before_placement;
    undo_edit->after_placement = pluginBlockAssignmentsFor(m_signal_chain.plugins());
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

void EditorController::Impl::performActionImpl(
    const EditorAction::SetPluginDisplayTypeOverride& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    const std::optional<PluginDisplayType> before_type =
        displayTypeOverrideFor(m_signal_chain.plugins(), action.instance_id);
    if (!m_signal_chain.setPluginDisplayTypeOverride(action.instance_id, action.display_type))
    {
        return;
    }

    auto undo_edit = std::make_unique<PluginDisplayTypeEdit>();
    undo_edit->instance_id = action.instance_id;
    undo_edit->before_type = before_type;
    undo_edit->after_type = action.display_type;
    pushUndoEntry(std::move(undo_edit));
    updateView();
}

void EditorController::Impl::performActionImpl(const EditorAction::OpenPlugin& action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    if (!m_signal_chain.containsInstance(action.instance_id))
    {
        return;
    }

    const auto result = m_plugin_host.openPluginWindow(action.instance_id);
    if (!result.has_value())
    {
        reportError(std::string{"Could not open plugin: "} + result.error().message);
    }
}

// Applies a live output gain preview without adding an undo entry until the final commit arrives.
void EditorController::Impl::onOutputGainPreviewChanged(double gain_db)
{
    applyOutputGainChange(gain_db, OutputGainChangeIntent::Preview);
}

// Applies a clamped output gain to the live rig and records the committed value in undo history.
void EditorController::Impl::onOutputGainChanged(double gain_db)
{
    applyOutputGainChange(gain_db, OutputGainChangeIntent::Commit);
}

} // namespace rock_hero::editor::core
