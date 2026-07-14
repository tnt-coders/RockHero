#include "controller/editor_controller_impl.h"

#include <algorithm>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/logger.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Undo label for a tone-file open; the file stem is the tone's user-facing name.
[[nodiscard]] std::string openToneLabel(const std::filesystem::path& file)
{
    return "Open Tone \"" + file.stem().string() + "\"";
}

} // namespace

// True when a live chain exists to edit: a loaded project arrangement or the designer rig. The
// signal-chain intents gate on this, never on project presence alone.
bool EditorController::Impl::hasActiveSignalChain() const
{
    return hasLoadedArrangement() || m_tone_designer.active;
}

// Designer document dirtiness: edits past the clean marker, plus the untracked latch for the
// rare mutation that could not be represented in history.
bool EditorController::Impl::toneDesignerHasUnsavedChanges() const noexcept
{
    return m_tone_designer.active &&
           (m_undo_history.hasUnsavedEdits() || m_has_untracked_unsaved_changes);
}

// Enters the designer resting state: fresh clean history and the engine's passthrough rig. Safe
// to call from any flow that may end project-less; it no-ops whenever a project is open, the
// designer is already live, or the session is faulted (recovery owns the UI then).
void EditorController::Impl::enterToneDesignerIfNoProject(std::string_view context)
{
    if (m_project.has_value() || m_tone_designer.active || m_session_faulted)
    {
        return;
    }

    RH_LOG_INFO("editor.tone_designer", "Entering tone designer context={:?}", context);
    m_tone_designer.active = true;
    m_tone_designer.document_path.reset();
    m_tone_designer.pending_clean_reconcile.reset();
    resetUndoHistory("undo.reset.enter_tone_designer");
    markUndoHistoryClean("undo.mark_clean.enter_tone_designer");

    // The empty-refs load builds the passthrough branch the designer edits; it completes
    // synchronously by the engine's empty-load contract.
    m_live_rig.loadLiveRig(
        common::audio::LiveRigLoadRequest{},
        safeCallback(
            [this](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    result) {
                if (!result.has_value())
                {
                    reportError("Could not start the tone designer rig: " + result.error().message);
                    return;
                }

                // The empty-refs load contract returns an empty chain; refuse anything over the
                // cap like every other snapshot-apply site instead of trusting the backend.
                if (result->plugins.size() > common::audio::g_max_signal_chain_plugins)
                {
                    reportError(
                        "Tone designer rig reported an over-limit plugin chain; leaving the "
                        "panel empty.");
                    return;
                }

                m_signal_chain.replaceSnapshot(
                    common::audio::PluginChainSnapshot{.plugins = result->plugins});
                m_output_gain_db = result->output_gain.db;
                m_output_gain_preview_before.reset();
            }));
    static_cast<void>(m_live_input_monitor.applyGate(monitoringContext()));
    updateView();
}

// Leaves the designer at a project commit; the project flow owns rig and history transitions.
void EditorController::Impl::leaveToneDesigner(std::string_view context)
{
    if (!m_tone_designer.active)
    {
        return;
    }

    RH_LOG_INFO("editor.tone_designer", "Leaving tone designer context={:?}", context);
    m_tone_designer = ToneDesignerState{};
}

// Consumes a document-replace transition's cleanliness request after the history commit: a
// landed-on state that matches its file re-marks the history clean there, so undoing past an
// open back to a just-saved document reads clean again.
void EditorController::Impl::reconcileToneDesignerCleanMarker()
{
    if (!m_tone_designer.pending_clean_reconcile.has_value())
    {
        return;
    }

    const bool matches_file = *m_tone_designer.pending_clean_reconcile;
    m_tone_designer.pending_clean_reconcile.reset();
    if (matches_file)
    {
        markUndoHistoryClean("undo.mark_clean.tone_document_transition");
    }
}

// Captures the designer document as an edit-memento side: engine chain state plus the
// editor-owned panel layout the engine does not retain.
std::optional<ToneDesignerDocumentSnapshot> EditorController::Impl::captureToneDesignerSnapshot(
    bool matches_file)
{
    auto chain_state = m_live_rig.captureAudibleToneState();
    if (!chain_state.has_value())
    {
        reportError("Could not capture the tone document state: " + chain_state.error().message);
        return std::nullopt;
    }

    ToneDesignerDocumentSnapshot snapshot;
    snapshot.chain.chain_state = std::move(*chain_state);
    snapshot.chain.visual_states = currentChainVisualStates();
    snapshot.document_path = m_tone_designer.document_path;
    snapshot.matches_file = matches_file;
    return snapshot;
}

// Captures the editor-owned visual state of the current chain, in chain order.
std::vector<PluginVisualEditState> EditorController::Impl::currentChainVisualStates() const
{
    const std::vector<PluginViewState>& plugins = m_signal_chain.plugins();
    std::vector<PluginVisualEditState> visual_states;
    visual_states.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        visual_states.push_back(
            PluginVisualEditState{
                .instance_id = plugin.instance_id,
                .block_index = plugin.block_index,
                .display_type_override = plugin.display_type_override,
            });
    }
    return visual_states;
}

// Applies a completed document replacement: editor chain model, association, exactly one undo
// entry, and the clean marker at the new document position.
void EditorController::Impl::finishToneDesignerReplace(
    ToneDesignerDocumentSnapshot before, std::optional<std::filesystem::path> opened_file,
    std::string operation_label, const common::audio::LiveRigLoadResult& result)
{
    m_signal_chain.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = result.plugins});
    m_output_gain_db = result.output_gain.db;
    m_output_gain_preview_before.reset();
    m_tone_designer.document_path = std::move(opened_file);

    auto after = captureToneDesignerSnapshot(true);
    if (!after.has_value())
    {
        // The chain is already replaced but cannot be represented in history; record an
        // untracked edit (which also clears the now-partial history) rather than pushing a lying
        // half-entry.
        markUntrackedUnsavedEdit("undo.reset.tone_document_capture_failed");
        updateView();
        return;
    }

    auto edit = std::make_unique<ToneDocumentReplaceEdit>();
    edit->before = std::move(before);
    edit->after = std::move(*after);
    edit->operation_label = std::move(operation_label);
    pushUndoEntry(std::move(edit));
    markUndoHistoryClean("undo.mark_clean.tone_document_replaced");
    updateView();
}

// Replaces the designer document with an empty untitled chain as one undo entry.
void EditorController::Impl::runToneDesignerNew()
{
    if (!m_tone_designer.active)
    {
        return;
    }

    auto before = captureToneDesignerSnapshot(!toneDesignerHasUnsavedChanges());
    if (!before.has_value())
    {
        updateView();
        return;
    }

    auto restored = m_live_rig.restoreAudibleToneState(common::audio::AudibleToneState{});
    if (!restored.has_value())
    {
        reportError("Could not start a new tone: " + restored.error().message);
        updateView();
        return;
    }

    finishToneDesignerReplace(std::move(*before), std::nullopt, "New Tone", *restored);
}

// Opens a tone file as the designer document behind the busy overlay. The engine replace is
// transactional, so a corrupt file or missing plugins leave the current document untouched.
void EditorController::Impl::runToneDesignerOpen(std::filesystem::path file)
{
    if (!m_tone_designer.active)
    {
        return;
    }

    auto before = captureToneDesignerSnapshot(!toneDesignerHasUnsavedChanges());
    if (!before.has_value())
    {
        updateView();
        return;
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    m_busy.runAfterBusyPresentationReady(safeCallback([this,
                                                       token,
                                                       opened_file = std::move(file),
                                                       captured_before =
                                                           std::move(*before)]() mutable {
        if (!m_busy.isCurrentToken(token))
        {
            return;
        }

        common::audio::ToneFileReplaceRequest request{
            .tone_file_path = opened_file,
            .progress_callback = {},
            // Route the engine's per-step yield through the busy paint fence so multi-plugin
            // opens keep painting between instantiations.
            .yield_callback = safeCallback([this, token](std::function<void()> next) {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                m_busy.runAfterBusyPresentationReady(
                    [this, token, continuation = std::move(next)]() mutable {
                        if (!m_busy.isCurrentToken(token))
                        {
                            return;
                        }
                        if (continuation)
                        {
                            continuation();
                        }
                    });
            }),
        };

        m_live_rig.replaceAudibleToneFromFile(
            std::move(request),
            safeCallback(
                [this, token, opened_file, moved_before = std::move(captured_before)](
                    std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                        result) mutable {
                    if (!m_busy.isCurrentToken(token))
                    {
                        return;
                    }
                    finishBusyOperation();
                    if (!result.has_value())
                    {
                        reportError("Could not open tone file: " + result.error().message);
                        updateView();
                        return;
                    }

                    recordSettingsResultBestEffort(
                        m_settings.setToneFileDirectory(opened_file.parent_path()),
                        "store tone file directory");
                    finishToneDesignerReplace(
                        std::move(moved_before), opened_file, openToneLabel(opened_file), *result);
                }));
    }));
}

// Saves the designer document to a tone file: a pure engine read plus a checkpoint move — saves
// never enter the undo history.
void EditorController::Impl::runToneDesignerSave(std::filesystem::path destination)
{
    if (!m_tone_designer.active)
    {
        return;
    }

    auto exported = m_live_rig.exportAudibleTone(
        common::audio::ToneFileExportRequest{
            .tone_file_path = destination,
            .block_indices = m_signal_chain.blockIndices(),
            .display_type_overrides = m_signal_chain.displayTypeOverrideTokens(),
        });
    if (!exported.has_value())
    {
        // A failed protective save cannot continue into the deferred action; cancel it so the
        // user decides again after fixing the destination.
        clearDeferredProjectAction();
        reportError("Could not save tone file: " + exported.error().message);
        updateView();
        return;
    }

    m_tone_designer.document_path = destination;
    recordSettingsResultBestEffort(
        m_settings.setToneFileDirectory(destination.parent_path()), "store tone file directory");
    markUndoHistoryClean("undo.mark_clean.save_tone_file");
    replayDeferredActionAfterToneSave();
    updateView();
}

// Replays a project action that was waiting behind the designer's protective save.
void EditorController::Impl::replayDeferredActionAfterToneSave()
{
    std::optional<EditorAction::ProjectAction> replay =
        m_deferred_project_action_state.takeReplay();
    if (!replay.has_value())
    {
        return;
    }

    runProjectAction(std::move(*replay));
}

void EditorController::Impl::performActionImpl(EditorAction::NewToneDocument /*action*/)
{
    requestProjectAction(EditorAction::NewToneDocument{});
}

void EditorController::Impl::performActionImpl(EditorAction::OpenToneFile action)
{
    requestProjectAction(EditorAction::OpenToneFile{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveToneFile /*action*/)
{
    // Untitled documents route through the Save As chooser view-side; without an association
    // there is nothing to overwrite (mirrors SaveProject's destination guard).
    if (!m_tone_designer.document_path.has_value())
    {
        return;
    }

    runToneDesignerSave(*m_tone_designer.document_path);
}

void EditorController::Impl::performActionImpl(EditorAction::SaveToneFileAs action)
{
    // When this Save As continues a deferred action, dismiss the chooser phase (mirrors
    // SaveProjectAs); a standalone Save As leaves this a no-op.
    m_deferred_project_action_state.saveAsPathChosen();
    runToneDesignerSave(std::move(action.file));
}

void EditorController::Impl::runProjectActionImpl(EditorAction::NewToneDocument /*action*/)
{
    runToneDesignerNew();
}

void EditorController::Impl::runProjectActionImpl(EditorAction::OpenToneFile action)
{
    runToneDesignerOpen(std::move(action.file));
}

// Collects the active chain's durable plugin ids (in chain order) through the runtime identity
// association; instances the association does not know yet simply have no automation to drop.
std::vector<std::string> EditorController::Impl::activeChainDurablePluginIds() const
{
    std::vector<std::string> durable_ids;
    const std::vector<PluginViewState>& plugins = m_signal_chain.plugins();
    durable_ids.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        if (const auto identity = m_tone_plugin_identities.find(plugin.instance_id);
            identity != m_tone_plugin_identities.end())
        {
            durable_ids.push_back(identity->second.plugin_id);
        }
    }
    return durable_ids;
}

// Counts the arrangement automation entries keyed by the active chain's durable plugin ids: the
// work an import would destroy, and the fact that decides whether the confirm prompt shows.
std::size_t EditorController::Impl::activeToneAutomationEntryCount() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return 0;
    }

    const std::vector<std::string> durable_ids = activeChainDurablePluginIds();
    std::size_t count = 0;
    for (const common::core::ToneParameterAutomation& entry : arrangement->tone_automation)
    {
        if (std::ranges::find(durable_ids, entry.plugin_id) != durable_ids.end())
        {
            count += 1;
        }
    }
    return count;
}

void EditorController::Impl::performActionImpl(EditorAction::ImportToneFile action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    // Automation destruction may live in off-screen tone regions where undo alone is not enough
    // protection, so it confirms first; an automation-free tone imports silently.
    const std::size_t automation_count = activeToneAutomationEntryCount();
    if (automation_count > 0)
    {
        m_pending_tone_import = std::move(action.file);
        m_pending_tone_import_automation_count = automation_count;
        updateView();
        return;
    }

    runToneImport(std::move(action.file));
}

void EditorController::Impl::performActionImpl(EditorAction::ResolveToneImportPrompt action)
{
    if (!m_pending_tone_import.has_value())
    {
        return;
    }

    std::filesystem::path file = std::move(*m_pending_tone_import);
    m_pending_tone_import.reset();
    m_pending_tone_import_automation_count = 0;
    if (action.decision == ToneImportDecision::Import)
    {
        runToneImport(std::move(file));
        return;
    }

    updateView();
}

void EditorController::Impl::performActionImpl(EditorAction::ExportToneFile action)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    auto exported = m_live_rig.exportAudibleTone(
        common::audio::ToneFileExportRequest{
            .tone_file_path = action.file,
            .block_indices = m_signal_chain.blockIndices(),
            .display_type_overrides = m_signal_chain.displayTypeOverrideTokens(),
        });
    if (!exported.has_value())
    {
        reportError("Could not export tone file: " + exported.error().message);
        updateView();
        return;
    }

    // Export is a pure read: no dirty state, no undo entry, only the remembered directory.
    recordSettingsResultBestEffort(
        m_settings.setToneFileDirectory(action.file.parent_path()), "store tone file directory");
    updateView();
}

// Replaces the active project tone's chain from a tone file behind the busy overlay: the tone
// keeps its catalog identity and regions; dropped automation travels into the undo entry.
void EditorController::Impl::runToneImport(std::filesystem::path file)
{
    if (!hasLoadedArrangement())
    {
        return;
    }

    auto before_state = m_live_rig.captureAudibleToneState();
    if (!before_state.has_value())
    {
        reportError("Could not capture the tone state: " + before_state.error().message);
        updateView();
        return;
    }

    ToneChainSnapshot before;
    before.chain_state = std::move(*before_state);
    before.visual_states = currentChainVisualStates();

    // Capture the pre-import identity facts now: the replace destroys the instances that key
    // them, and undo's curve rebuild needs the old instance ids.
    std::vector<std::pair<std::string, std::string>> prior_instance_to_durable;
    prior_instance_to_durable.reserve(before.visual_states.size());
    for (const PluginVisualEditState& visual_state : before.visual_states)
    {
        if (const auto identity = m_tone_plugin_identities.find(visual_state.instance_id);
            identity != m_tone_plugin_identities.end())
        {
            prior_instance_to_durable.emplace_back(
                visual_state.instance_id, identity->second.plugin_id);
        }
    }
    const std::string tone_ref = activeToneDocumentRef();
    const std::string tone_name = activeToneName();

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    m_busy.runAfterBusyPresentationReady(
        safeCallback([this,
                      token,
                      import_file = std::move(file),
                      captured_before = std::move(before),
                      prior_ids = std::move(prior_instance_to_durable),
                      tone_ref,
                      tone_name]() mutable {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }

            common::audio::ToneFileReplaceRequest request{
                .tone_file_path = import_file,
                .progress_callback = {},
                // Same paint-fence yield as the designer open, so multi-plugin imports keep
                // painting between instantiations.
                .yield_callback = safeCallback([this, token](std::function<void()> next) {
                    if (!m_busy.isCurrentToken(token))
                    {
                        return;
                    }
                    m_busy.runAfterBusyPresentationReady(
                        [this, token, continuation = std::move(next)]() mutable {
                            if (!m_busy.isCurrentToken(token))
                            {
                                return;
                            }
                            if (continuation)
                            {
                                continuation();
                            }
                        });
                }),
            };

            m_live_rig.replaceAudibleToneFromFile(
                std::move(request),
                safeCallback(
                    [this,
                     token,
                     import_file,
                     moved_before = std::move(captured_before),
                     moved_prior_ids = std::move(prior_ids),
                     tone_ref,
                     tone_name](
                        std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                            result) mutable {
                        if (!m_busy.isCurrentToken(token))
                        {
                            return;
                        }
                        finishBusyOperation();
                        if (!result.has_value())
                        {
                            reportError("Could not import tone file: " + result.error().message);
                            updateView();
                            return;
                        }

                        recordSettingsResultBestEffort(
                            m_settings.setToneFileDirectory(import_file.parent_path()),
                            "store tone file directory");
                        finishToneImport(
                            std::move(moved_before),
                            std::move(moved_prior_ids),
                            tone_ref,
                            tone_name,
                            import_file,
                            *result);
                    }));
        }));
}

// Applies a completed tone import: extracts the replaced chain's automation into the undo entry,
// mints fresh durable ids for the imported plugins, and pushes exactly one undo entry.
void EditorController::Impl::finishToneImport(
    ToneChainSnapshot before, std::vector<std::pair<std::string, std::string>> prior_ids,
    const std::string& tone_ref, const std::string& tone_name,
    const std::filesystem::path& import_file, const common::audio::LiveRigLoadResult& result)
{
    // Extract the automation keyed by the replaced chain's durable ids; the entries travel into
    // the undo entry with the instance ids their restored curves rebuild on.
    std::vector<ToneImportRemovedAutomation> removed_automation;
    if (std::vector<common::core::ToneParameterAutomation>* const automation =
            m_session.currentToneAutomation();
        automation != nullptr)
    {
        for (const auto& [instance_id, durable_id] : prior_ids)
        {
            for (const common::core::ToneParameterAutomation& entry : *automation)
            {
                if (entry.plugin_id == durable_id)
                {
                    removed_automation.push_back(
                        ToneImportRemovedAutomation{
                            .entry = entry,
                            .instance_id = instance_id,
                        });
                }
            }
        }
        for (const auto& [instance_id, durable_id] : prior_ids)
        {
            std::erase_if(
                *automation, [&durable_id](const common::core::ToneParameterAutomation& entry) {
                    return entry.plugin_id == durable_id;
                });
        }
    }
    for (const auto& [instance_id, durable_id] : prior_ids)
    {
        std::erase_if(m_open_automation_lanes, [&durable_id](const OpenAutomationLane& lane) {
            return lane.plugin_id == durable_id;
        });
    }

    // Mint fresh durable ids for the imported plugins (file-carried ids are never adopted) and
    // bind them to the live instances. Both maps stay upsert-only: the prior chain's entries
    // remain so an undo that revives those instance ids finds its associations intact.
    for (const common::audio::PluginChainEntry& plugin : result.plugins)
    {
        const std::string minted_id = common::core::generatePackageId();
        m_tone_plugin_identities.insert_or_assign(
            plugin.instance_id,
            ToneAutomationIdentity{
                .plugin_id = minted_id,
                .tone_document_ref = tone_ref,
            });
        m_tone_plugin_bindings.insert_or_assign(
            minted_id,
            ToneAutomationBinding{
                .instance_id = plugin.instance_id,
                .tone_document_ref = tone_ref,
            });
    }

    m_signal_chain.replaceSnapshot(common::audio::PluginChainSnapshot{.plugins = result.plugins});
    m_output_gain_db = result.output_gain.db;
    m_output_gain_preview_before.reset();

    auto after_state = m_live_rig.captureAudibleToneState();
    if (!after_state.has_value())
    {
        // The chain is already replaced but cannot be represented in history; record an
        // untracked edit (which also clears the now-partial history) rather than pushing a
        // lying half-entry.
        reportError("Could not capture the imported tone state: " + after_state.error().message);
        markUntrackedUnsavedEdit("undo.reset.tone_import_capture_failed");
        updateView();
        return;
    }

    auto edit = std::make_unique<ToneImportEdit>();
    edit->before = std::move(before);
    edit->after.chain_state = std::move(*after_state);
    edit->after.visual_states = currentChainVisualStates();
    edit->removed_automation = std::move(removed_automation);
    edit->tone_document_ref = tone_ref;
    edit->tone_name = tone_name;
    edit->file_name = import_file.stem().string();
    pushUndoEntry(std::move(edit));
    updateView();
}

} // namespace rock_hero::editor::core
