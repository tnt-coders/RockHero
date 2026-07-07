#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <cassert>
#include <rock_hero/common/core/tone/tone_track_normalize.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>

namespace rock_hero::editor::core
{

namespace
{

// Uses the real filesystem to avoid adding another startup-restore callback seam.
[[nodiscard]] bool projectFileExists(const std::filesystem::path& project_file)
{
    std::error_code error;
    return std::filesystem::is_regular_file(project_file, error);
}

// Reports a boundary result that violated the product cap before it reaches view state.
[[nodiscard]] common::audio::LiveRigError signalChainLimitError(std::size_t plugin_count)
{
    return common::audio::LiveRigError{
        common::audio::LiveRigErrorCode::PluginChainLimitExceeded,
        common::audio::pluginChainLimitExceededMessage(plugin_count),
    };
}

// Closes an optional project after the caller has released subsystem references into its workspace.
[[nodiscard]] std::expected<void, ProjectError> closeExistingProject(
    std::optional<Project>& project)
{
    if (!project.has_value())
    {
        return {};
    }

    return project->close();
}

// Resolves the extracted native-song directory owned by an editor project workspace.
[[nodiscard]] std::filesystem::path songDirectoryForProject(const Project& project)
{
    return project.workspaceDirectory() /
           std::filesystem::path{std::string{project_io::g_song_directory_name}};
}

// Resolves persisted arrangement IDs to the current song order, falling back to the first item.
[[nodiscard]] std::size_t getSelectedArrangementIndex(
    const common::core::Song& song, const std::optional<std::string>& selected_arrangement)
{
    if (!selected_arrangement.has_value())
    {
        // A fresh open carries no saved choice: default to the Lead part so the primary guitar
        // shows first, falling back to the first arrangement when the song has no Lead. A future
        // user path preference will slot in ahead of this default.
        const auto lead = std::ranges::find(
            song.arrangements, common::core::Part::Lead, &common::core::Arrangement::part);
        return lead == song.arrangements.end()
                   ? 0
                   : static_cast<std::size_t>(std::distance(song.arrangements.begin(), lead));
    }

    const auto found = std::ranges::find_if(
        song.arrangements, [&selected_arrangement](const common::core::Arrangement& arrangement) {
            return arrangement.id == *selected_arrangement;
        });
    if (found == song.arrangements.end())
    {
        return 0;
    }

    return static_cast<std::size_t>(std::distance(song.arrangements.begin(), found));
}

// Maps Save to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::SaveProject& /*action*/) noexcept
{
    return BusyOperation::SavingProject;
}

// Maps Save As to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::SaveProjectAs& /*action*/) noexcept
{
    return BusyOperation::SavingProjectAs;
}

// Maps Publish to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::PublishProject& /*action*/) noexcept
{
    return BusyOperation::PublishingProject;
}

// Maps write actions to the busy operation shown while the worker owns Project IO.
[[nodiscard]] BusyOperation busyOperationForProjectWrite(
    const EditorAction::ProjectWriteAction& action)
{
    return std::visit(
        [](const auto& alternative) noexcept { return busyOperationForProjectWrite(alternative); },
        action);
}

// Keeps Save failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::SaveProject& /*action*/) noexcept
{
    return "Could not save: ";
}

// Keeps Save As failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::SaveProjectAs& /*action*/) noexcept
{
    return "Could not save as: ";
}

// Keeps Publish failure text coupled to the write alternative rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::PublishProject& /*action*/) noexcept
{
    return "Could not publish: ";
}

// Keeps write failure prefixes coupled to write alternatives rather than split by call site.
[[nodiscard]] std::string_view projectWriteErrorPrefix(
    const EditorAction::ProjectWriteAction& action)
{
    return std::visit(
        [](const auto& alternative) noexcept { return projectWriteErrorPrefix(alternative); },
        action);
}

} // namespace

// Opens an editor project package after any project-replacement prompt has been satisfied.
// Pushes busy state, dispatches package IO to the task runner, and returns immediately. The
// final commit runs in completeOpenProject() on the message thread once the worker reports the
// result.
void EditorController::Impl::openProject(
    const std::filesystem::path& file, bool clear_last_open_project_on_failure)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<OpenTaskState>();
    state->file = file;
    state->clear_last_open_project_on_failure = clear_last_open_project_on_failure;
    m_pending_restore_project_file = clear_last_open_project_on_failure
                                         ? std::optional<std::filesystem::path>{file}
                                         : std::nullopt;
    m_restore_interrupted_prompt_file.reset();
    if (clear_last_open_project_on_failure)
    {
        recordSettingsResultBestEffort(
            m_settings.setInterruptedRestoreProject(file), "mark interrupted restore");
    }
    else
    {
        recordSettingsResultBestEffort(
            m_settings.setInterruptedRestoreProject(std::nullopt),
            "clear interrupted restore before open");
    }
    const std::uint64_t token = beginBusy(BusyOperation::OpeningProject);
    EditorController::ProjectOperationProgress report_progress =
        makeBusyProjectOperationProgress(token);

    m_task_runner.submit(
        [state, open_function = m_open_function, report_progress = std::move(report_progress)] {
            state->result = open_function(state->project, state->file, report_progress);
        },
        safeCallback([this, state, token]() mutable {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completeOpenProject(state);
        }));
}

// Applies the worker's open result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeOpenProject(const std::shared_ptr<OpenTaskState>& state)
{
    assert(isBusy() && "completeOpenProject called outside a busy operation");

    if (!state->result.has_value())
    {
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt), "clear last project after open error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);
    const ProjectEditorState editor_state = state->project.editorState();

    auto song_loaded = loadSessionSong(
        std::move(song),
        songDirectoryForProject(state->project),
        editor_state.selected_arrangement);
    if (!song_loaded.has_value())
    {
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt),
                "clear last project after audio-load error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        finishBusyOperation();
        reportError(
            std::string{"Could not load audio from: "} + state->file.string() + ": " +
            song_loaded.error().message);
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_busy.currentToken(),
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state](std::expected<void, common::audio::LiveRigError> rig_result) {
                finishOpenProjectAfterLiveRigLoad(state, std::move(rig_result));
            },
        });
}

// Commits a fully loaded editor project, or tears down the partial session on rig-load failure.
// Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before this
// finalizer runs.
void EditorController::Impl::finishOpenProjectAfterLiveRigLoad(
    const std::shared_ptr<OpenTaskState>& state,
    std::expected<void, common::audio::LiveRigError> rig_result)
{
    assert(isBusy() && "finishOpenProjectAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        clearActiveArrangementBestEffort("open live-rig failure teardown");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        if (state->clear_last_open_project_on_failure)
        {
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt),
                "clear last project after live-rig error");
        }
        clearInterruptedRestoreMarker();
        m_pending_restore_project_file.reset();
        resetUndoHistory("undo.reset.open_live_rig_failed");
        finishBusyOperation();
        reportError(
            std::string{"Could not load live rig from: "} + state->file.string() + ": " +
            rig_result.error().message);
        return;
    }

    const bool next_has_unsaved_changes = state->project.audioNormalizationUpdatedOnLoad();
    const common::core::TimePosition next_cursor_position =
        cursorPositionForOpenedProject(state->file);
    m_grid_note_value = gridNoteValueForOpenedProject(state->file);
    m_timeline_zoom_pixels_per_second = timelineZoomForOpenedProject(state->file);
    std::filesystem::path next_project_file{state->file};

    m_project = std::move(state->project);
    m_project_file.swap(next_project_file);
    m_pending_restore_project_file.reset();
    m_displaced_project_file.clear();
    m_save_requires_destination = false;
    m_has_untracked_unsaved_changes = next_has_unsaved_changes;
    m_session_faulted = false;
    clearDeferredProjectAction();
    m_project_audio_ready = true;
    resetUndoHistory("undo.reset.open_project");
    markUndoHistoryClean("undo.mark_clean.open_project");

    m_transport.seek(next_cursor_position);
    if (state->clear_last_open_project_on_failure)
    {
        clearInterruptedRestoreMarker();
    }
    applyLiveInputGate();

    // Marks a completed load so the view can recenter on the restored cursor; the transport has
    // already been seeked above, so the state pushed by finishBusyOperation() carries both.
    ++m_project_load_id;

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Imports a song source after any current project-replacement prompt has been satisfied. Same
// shape as openProject(): busy + worker dispatch here, commit in completeImportSongSource().
void EditorController::Impl::importSongSource(const std::filesystem::path& file)
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    auto state = std::make_shared<ImportTaskState>();
    state->file = file;

    m_pending_restore_project_file.reset();
    m_restore_interrupted_prompt_file.reset();
    clearInterruptedRestoreMarker();
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    EditorController::ProjectOperationProgress report_progress =
        makeBusyProjectOperationProgress(token);

    m_task_runner.submit(
        [state, import_function = m_import_function, report_progress = std::move(report_progress)] {
            state->result = import_function(state->project, state->file, report_progress);
        },
        safeCallback([this, state, token]() mutable {
            if (!m_busy.isCurrentToken(token))
            {
                return;
            }
            completeImportSongSource(state);
        }));
}

// Applies the worker's import result on the message thread. The submitted completion already
// verified that the busy token still matches before invoking this finalizer.
void EditorController::Impl::completeImportSongSource(const std::shared_ptr<ImportTaskState>& state)
{
    assert(isBusy() && "completeImportSongSource called outside a busy operation");

    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        finishBusyOperation();
        reportError(std::string{"Could not import: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    auto song_loaded =
        loadSessionSong(std::move(song), songDirectoryForProject(state->project), std::nullopt);
    if (!song_loaded.has_value())
    {
        finishBusyOperation();
        reportError(
            std::string{"Could not load imported audio from: "} + state->file.string() + ": " +
            song_loaded.error().message);
        return;
    }

    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = m_busy.currentToken(),
            .song_directory = songDirectoryForProject(state->project),
            .finish = [this, state](std::expected<void, common::audio::LiveRigError> rig_result) {
                finishImportSongSourceAfterLiveRigLoad(state, std::move(rig_result));
            },
        });
}

// Commits a fully imported editor workspace, or tears down the partial session on rig-load
// failure. Busy-token and controller-liveness checks are owned by ProjectLoadLiveRigStage before
// this finalizer runs.
void EditorController::Impl::finishImportSongSourceAfterLiveRigLoad(
    const std::shared_ptr<ImportTaskState>& state,
    std::expected<void, common::audio::LiveRigError> rig_result)
{
    assert(isBusy() && "finishImportSongSourceAfterLiveRigLoad called outside a busy operation");

    if (!rig_result.has_value())
    {
        m_transport.stop();
        clearActiveArrangementBestEffort("import live-rig failure teardown");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_grid_note_value = common::core::Fraction{1, 4};
        m_timeline_zoom_pixels_per_second = 0.0;
        m_selected_tone_region_id.clear();
        resetUndoHistory("undo.reset.import_live_rig_failed");
        finishBusyOperation();
        reportError(
            std::string{"Could not load imported live rig from: "} + state->file.string() + ": " +
            rig_result.error().message);
        return;
    }

    m_displaced_project_file = !m_project_file.empty() ? m_project_file : m_displaced_project_file;
    m_project = std::move(state->project);
    m_project_file.clear();
    m_save_requires_destination = true;
    // A fresh import has no per-project grid note-value record to restore (no project path yet), so
    // the grid resets to the quarter-note default instead of inheriting the replaced project's
    // spacing.
    m_grid_note_value = common::core::Fraction{1, 4};
    m_timeline_zoom_pixels_per_second = 0.0;
    m_selected_tone_region_id.clear();
    m_has_untracked_unsaved_changes = false;
    m_session_faulted = false;
    clearDeferredProjectAction();
    m_project_audio_ready = true;
    resetUndoHistory("undo.reset.import_project");
    markUndoHistoryClean("undo.mark_clean.import_project");
    applyLiveInputGate();

    // Imports have no persisted editor cursor, so establish an explicit start position before the
    // view observes the new project load id and recenters the timeline.
    m_transport.seek(session().timeline().start);

    // Marks a completed load so the view can recenter on the established import cursor.
    ++m_project_load_id;

    // finishBusyOperation()'s view update also satisfies any deferred transport refresh that
    // may have arrived during the load window.
    finishBusyOperation();
}

// Runs the shared project-load live-rig stage. Tone-bearing arrangements switch the busy overlay
// into determinate progress and wait for that state to paint before live-rig restore starts.
void EditorController::Impl::runLiveRigLoadStage(ProjectLoadLiveRigStage stage_state)
{
    if (!stage_state.finish || !m_busy.isCurrentToken(stage_state.token))
    {
        return;
    }

    const bool report_progress = shouldShowLiveRigLoadProgress();
    if (!report_progress)
    {
        startLiveRigLoadStage(std::move(stage_state), false);
        return;
    }

    m_busy.beginLiveRigLoadProgress();
    m_busy.runAfterBusyPresentationReady([this, captured_stage = std::move(stage_state)]() mutable {
        startLiveRigLoadStage(std::move(captured_stage), true);
    });
}

// Starts the audio-boundary live-rig restore and routes only current-token completions to the
// stage finalizer. Signal-chain view state is updated only after the token check so a
// superseded restore cannot repopulate plugins after close or replacement.
void EditorController::Impl::startLiveRigLoadStage(
    ProjectLoadLiveRigStage stage_state, bool report_progress)
{
    if (!stage_state.finish || !m_busy.isCurrentToken(stage_state.token))
    {
        return;
    }

    const std::uint64_t token = stage_state.token;

    // Resolve the directory before moving the stage into the completion lambda. MSVC may evaluate
    // later call arguments before earlier ones, so reading stage_state.song_directory inline would
    // risk reading after move.
    const std::filesystem::path song_directory = stage_state.song_directory;
    restoreLiveRig(
        song_directory,
        report_progress,
        token,
        safeCallback(
            [this, token, report_progress, captured_stage = std::move(stage_state)](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    rig_result) mutable {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                if (!rig_result.has_value())
                {
                    captured_stage.finish(std::unexpected{std::move(rig_result.error())});
                    return;
                }

                if (rig_result->plugins.size() > common::audio::g_max_signal_chain_plugins)
                {
                    captured_stage.finish(
                        std::unexpected{signalChainLimitError(rig_result->plugins.size())});
                    return;
                }

                m_signal_chain.replaceSnapshot(
                    common::audio::PluginChainSnapshot{.plugins = rig_result->plugins});
                m_output_gain_db = rig_result->output_gain.db;
                m_output_gain_preview_before.reset();
                // Load-fresh plugin identities key the arrangement's musical automation; merge
                // them, then rebuild the derived playback curves the sidecars no longer carry.
                mergeToneChainIdentities(rig_result->tone_chains);
                rebuildToneAutomationCurves();
                // One-way host-tempo mirror so hosted plugins see the song's real tempo map
                // instead of the backend default. A future tempo-editing flow must re-mirror
                // after every tempo-map change alongside rebuildToneAutomationCurves().
                m_song_audio.mirrorTempoMap(session().song().tempo_map);
                applyLiveInputGate();
                if (!report_progress)
                {
                    captured_stage.finish({});
                    return;
                }

                if (m_busy.setLiveRigLoadProgress("Live rig loaded.", 1.0))
                {
                    updateView();
                }

                // Wall-clock delay so the 100% state stays visible long enough for the user to see.
                constexpr std::chrono::milliseconds minimum_completion_display_time{500};
                const std::function<void()> finish_stage_after_hold =
                    [this, token, timer_stage = std::move(captured_stage)]() mutable {
                        if (!m_busy.isCurrentToken(token))
                        {
                            return;
                        }
                        timer_stage.finish({});
                    };

                if (!m_busy.callAfterDelay(
                        minimum_completion_display_time, finish_stage_after_hold))
                {
                    finish_stage_after_hold();
                }
            }));
}

void EditorController::Impl::performActionImpl(EditorAction::OpenProject action)
{
    requestProjectAction(EditorAction::OpenProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::RestoreProject action)
{
    requestProjectAction(EditorAction::RestoreProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::ImportSong action)
{
    requestProjectAction(EditorAction::ImportSong{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProject /*action*/)
{
    if (m_save_requires_destination)
    {
        return;
    }
    runProjectAction(EditorAction::SaveProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::SaveProjectAs action)
{
    // When this Save As is continuing a deferred action, dismiss the chooser phase after the
    // action gate accepts the save. A standalone Save As leaves this a no-op.
    m_deferred_project_action_state.saveAsPathChosen();
    runProjectAction(EditorAction::SaveProjectAs{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::PublishProject action)
{
    runProjectAction(EditorAction::PublishProject{std::move(action.file)});
}

void EditorController::Impl::performActionImpl(EditorAction::CloseProject /*action*/)
{
    requestProjectAction(EditorAction::CloseProject{});
}

void EditorController::Impl::performActionImpl(EditorAction::ExitApplication /*action*/)
{
    requestProjectAction(EditorAction::ExitApplication{});
}

void EditorController::Impl::performActionImpl(EditorAction::ResolveUnsavedChangesPrompt action)
{
    if (m_session_faulted && action.decision == UnsavedChangesDecision::Save)
    {
        reportError(
            "Cannot save while the live editor state is untrusted. Discard changes to continue, "
            "or cancel and reopen the project.");
        updateView();
        return;
    }

    std::visit(
        [this](auto&& step) {
            using Step = std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<Step, DeferredProjectActionState::Refresh>)
            {
                updateView();
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::SaveThenReplay>)
            {
                runProjectAction(EditorAction::SaveProject{});
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::DiscardAndReplay>)
            {
                replayDiscardedProjectAction(std::move(step.action));
            }
        },
        m_deferred_project_action_state.resolveUnsavedChanges(
            action.decision, m_save_requires_destination));
}

// Discards the current project's unsaved changes and replays the released deferred action now.
void EditorController::Impl::replayDiscardedProjectAction(EditorAction::ProjectAction action)
{
    const EditorAction::Id deferred_id = idOf(action);
    std::filesystem::path displaced_by_import;
    if (deferred_id == EditorAction::Id::ImportSong)
    {
        displaced_by_import = !m_project_file.empty() ? m_project_file : m_displaced_project_file;
    }
    m_has_untracked_unsaved_changes = false;
    m_save_requires_destination = false;
    // CloseProject and ExitApplication both close the current project as part of their own action
    // handler, and ExitApplication additionally needs m_project_file alive when it captures the
    // value to persist as last_open_project. Closing here first would zero that path out, so let
    // the replay action do its own close + capture.
    if (deferred_id == EditorAction::Id::CloseProject ||
        deferred_id == EditorAction::Id::ExitApplication)
    {
        runProjectAction(std::move(action));
        return;
    }

    if (closeProject())
    {
        if (!displaced_by_import.empty())
        {
            m_displaced_project_file = std::move(displaced_by_import);
        }
        runProjectAction(std::move(action));
    }
}

void EditorController::Impl::performActionImpl(EditorAction::CancelSaveAsPrompt /*action*/)
{
    if (!m_deferred_project_action_state.cancelSaveAsPrompt())
    {
        return;
    }

    updateView();
}

// Starts a project-level action or asks the view to confirm unsaved changes first. Callers pass
// the original action; on dirty state the action itself is stashed for replay after the prompt
// resolves.
void EditorController::Impl::requestProjectAction(EditorAction::ProjectAction action)
{
    if (hasUnsavedChanges())
    {
        m_deferred_project_action_state.defer(std::move(action));
        updateView();
        return;
    }

    runProjectAction(std::move(action));
}

// Runs a project-level action once dirty-state gates have been satisfied. Visits the variant once
// and dispatches to a typed overload per case, mirroring performAction; each alternative arrives by
// value so a write-side move into a fresh ProjectWriteAction cannot dangle into the source variant.
void EditorController::Impl::runProjectAction(EditorAction::ProjectAction action)
{
    std::visit(
        [this](auto&& a) { runProjectActionImpl(std::forward<decltype(a)>(a)); },
        std::move(action));
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::OpenProject& action)
{
    openProject(action.file, false);
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::RestoreProject& action)
{
    openProject(action.file, true);
}

void EditorController::Impl::runProjectActionImpl(const EditorAction::ImportSong& action)
{
    importSongSource(action.file);
}

void EditorController::Impl::runProjectActionImpl(EditorAction::SaveProject action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{action});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::SaveProjectAs action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{std::move(action)});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::PublishProject action)
{
    runProjectWriteAction(EditorAction::ProjectWriteAction{std::move(action)});
}

void EditorController::Impl::runProjectActionImpl(EditorAction::CloseProject /*action*/)
{
    // Capture the displaced path before closeProject() clears it so we can re-open the project
    // that was displaced by an import the user is now discarding.
    const std::filesystem::path displaced = m_displaced_project_file;
    if (closeProject())
    {
        clearDeferredProjectAction();
        if (!displaced.empty())
        {
            openProject(displaced, false);
        }
        else
        {
            updateView();
        }
    }
}

void EditorController::Impl::runProjectActionImpl(EditorAction::ExitApplication /*action*/)
{
    const std::optional<std::filesystem::path> restorable_project_file =
        restorableProjectFileForExit();
    if (closeProject())
    {
        m_pending_restore_project_file.reset();
        clearDeferredProjectAction();
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(restorable_project_file), "store exit restore project");
        updateView();
        m_exit_function();
    }
}

// Closes the current editor document across transport, backend audio, session, and workspace.
// Always supersedes any in-flight busy operation so closing or exiting during background work
// invalidates the worker's busy token; the worker's completion then sees a mismatch and
// discards itself rather than committing on top of a now-empty session.
bool EditorController::Impl::closeProject()
{
    m_project_audio_ready = false;
    applyLiveInputGate();

    if (!m_project.has_value())
    {
        if (hasLoadedArrangement())
        {
            m_transport.stop();
        }
        clearLiveRig();
        clearActiveArrangementBestEffort("close empty project");
        m_session.reset();
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_untracked_unsaved_changes = false;
        m_session_faulted = false;
        m_plugin_catalog.hide();
        resetUndoHistory("undo.reset.close_empty_project");
        return true;
    }

    saveCurrentProjectCursorPositionBestEffort("store project cursor before close");
    m_transport.stop();
    clearLiveRig();
    clearActiveArrangementBestEffort("close project");
    m_session.reset();
    m_signal_chain.clear();
    m_output_gain_db = 0.0;

    auto closed = closeExistingProject(m_project);
    if (!closed.has_value())
    {
        reportError(std::string{"Could not close: "} + closed.error().message);
        m_project.reset();
        m_project_file.clear();
        m_displaced_project_file.clear();
        m_save_requires_destination = false;
        m_has_untracked_unsaved_changes = false;
        m_session_faulted = false;
        m_grid_note_value = common::core::Fraction{1, 4};
        m_timeline_zoom_pixels_per_second = 0.0;
        m_selected_tone_region_id.clear();
        m_plugin_catalog.hide();
        resetUndoHistory("undo.reset.close_project_failed");
        updateView();
        return false;
    }

    m_project.reset();
    m_project_file.clear();
    m_displaced_project_file.clear();
    m_save_requires_destination = false;
    m_has_untracked_unsaved_changes = false;
    m_session_faulted = false;
    m_grid_note_value = common::core::Fraction{1, 4};
    m_timeline_zoom_pixels_per_second = 0.0;
    m_selected_tone_region_id.clear();
    m_plugin_catalog.hide();
    resetUndoHistory("undo.reset.close_project");
    return true;
}

// Snapshots message-thread state and transfers Project ownership to a write task so worker-side
// package IO cannot race with controller-owned Project mutation.
auto EditorController::Impl::takeProjectForWrite(EditorAction::ProjectWriteAction action)
    -> std::shared_ptr<ProjectWriteTaskState>
{
    if (!m_project.has_value())
    {
        return {};
    }

    Project& project = m_project.value();
    auto state = std::make_shared<ProjectWriteTaskState>(std::move(action));
    common::core::Song song = session().song();
    if (const auto rig_captured = captureLiveRigToDisk(project); !rig_captured.has_value())
    {
        reportError(std::string{"Could not capture live rig: "} + rig_captured.error().message);
        return {};
    }

    state->project = std::move(project);
    state->song = std::move(song);
    state->editor_state = projectEditorStateForSave();
    m_project.reset();
    return state;
}

void EditorController::Impl::onArrangementSelected(std::string arrangement_id)
{
    runAction(EditorAction::SelectArrangement{std::move(arrangement_id)});
}

// Switches the displayed arrangement: captures the current live rig into its tone files, reloads
// the session around the new arrangement, and restores that arrangement's rig behind the busy
// presentation. Arrangement switching is deliberately not the instant tone-region switch path.
void EditorController::Impl::performActionImpl(const EditorAction::SelectArrangement& action)
{
    const common::core::Arrangement* const current = session().currentArrangement();
    if (current == nullptr || !m_project.has_value() || current->id == action.arrangement_id)
    {
        return;
    }

    const bool target_exists = std::ranges::any_of(
        session().arrangements(), [&action](const common::core::Arrangement& arrangement) {
            return arrangement.id == action.arrangement_id;
        });
    if (!target_exists)
    {
        RH_LOG_WARNING(
            "editor.project",
            "Ignored switch to unknown arrangement arrangement_id={:?}",
            action.arrangement_id);
        return;
    }

    // Capture the outgoing arrangement's rig into its tone files so switching back restores any
    // unsaved tone edits; the capture also flushes pending plugin state.
    common::core::Song song = session().song();
    if (const auto captured = captureLiveRigToDisk(*m_project); !captured.has_value())
    {
        reportError(
            std::string{"Could not capture the current arrangement's tones: "} +
            captured.error().message);
        return;
    }

    m_transport.stop();
    clearLiveRig();
    resetUndoHistory("undo.reset.arrangement_switch");
    m_selected_tone_region_id.clear();
    m_project_audio_ready = false;

    if (const auto loaded = loadSessionSong(
            std::move(song), songDirectoryForProject(*m_project), action.arrangement_id);
        !loaded.has_value())
    {
        // The session commit only happens after the backend accepts the arrangement, so a
        // failure leaves the previous arrangement displayed; restore its rig below.
        reportError(
            std::string{"Could not activate the selected arrangement: "} + loaded.error().message);
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    updateView();
    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
            .song_directory = songDirectoryForProject(*m_project),
            .finish = [this](std::expected<void, common::audio::LiveRigError> rig_result) {
                if (!rig_result.has_value())
                {
                    finishBusyOperation();
                    reportError(
                        std::string{"Could not load the arrangement's tones: "} +
                        rig_result.error().message);
                    updateView();
                    return;
                }

                m_project_audio_ready = true;
                applyToneSelection(toneRegionIdAt(common::core::TimePosition{}));
                finishBusyOperation();
                updateView();
            },
        });
}

// Captures the selected arrangement's live rig into its tone files before Project IO leaves the
// message thread. Every loaded tone branch persists to its own document; the model needs no
// write-back because tone references are established at load and never change during capture.
std::expected<void, common::audio::LiveRigError> EditorController::Impl::captureLiveRigToDisk(
    const Project& project)
{
    const common::core::Arrangement* const current_arrangement = session().currentArrangement();
    if (current_arrangement == nullptr)
    {
        return {};
    }

    auto snapshot = m_live_rig.captureActiveRig(
        common::audio::LiveRigCaptureRequest{
            .song_directory = songDirectoryForProject(project),
            .arrangement_id = current_arrangement->id,
            .block_indices = m_signal_chain.blockIndices(),
            .display_type_overrides = m_signal_chain.displayTypeOverrideTokens(),
            .stable_ids = captureStableIds(),
        });
    if (!snapshot.has_value())
    {
        return std::unexpected{std::move(snapshot.error())};
    }

    if (snapshot->plugins.size() > common::audio::g_max_signal_chain_plugins)
    {
        return std::unexpected{signalChainLimitError(snapshot->plugins.size())};
    }

    m_signal_chain.replaceSnapshot(
        common::audio::PluginChainSnapshot{.plugins = snapshot->plugins});
    m_output_gain_db = snapshot->output_gain.db;
    m_output_gain_preview_before.reset();
    return {};
}

// Restores the selected arrangement's saved tone document after the backing audio is active.
// Live rig restore runs cooperatively on the message thread inside the audio adapter, so this
// method always returns immediately and routes the audio load result through the on_loaded callback
// without mutating controller state.
void EditorController::Impl::restoreLiveRig(
    const std::filesystem::path& song_directory, bool report_progress, std::uint64_t token,
    std::function<
        void(std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>)>
        on_loaded)
{
    if (!on_loaded)
    {
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        on_loaded(common::audio::LiveRigLoadResult{});
        return;
    }

    // Every tone the arrangement's regions reference preloads into its own rig branch so
    // selection switching never rebuilds plugins. The load baseline guarantees explicit regions,
    // so the region set is the complete tone set. The audible tone defaults to the first (the
    // engine's fallback); selection sync moves it once the load finishes.
    std::vector<std::string> tone_document_refs;
    for (const common::core::ToneRegion& region : arrangement->tone_track.regions)
    {
        if (!region.tone_document_ref.empty() &&
            std::ranges::find(tone_document_refs, region.tone_document_ref) ==
                tone_document_refs.end())
        {
            tone_document_refs.push_back(region.tone_document_ref);
        }
    }

    common::audio::LiveRigLoadRequest request{
        .song_directory = song_directory,
        .tone_document_refs = std::move(tone_document_refs),
        .audible_tone_ref = {},
        .progress_callback = {},
        .yield_callback = {},
    };
    if (report_progress)
    {
        request.progress_callback =
            safeCallback([this, token](const common::audio::LiveRigLoadProgress& progress) {
                if (!m_busy.isCurrentToken(token))
                {
                    return;
                }
                m_busy.updateLiveRigLoadProgress(progress);
            });
        // Route the engine's per-step yield through the busy-overlay paint fence so each plugin's
        // progress update actually paints before the next step blocks the message thread.
        request.yield_callback = safeCallback([this, token](std::function<void()> next) {
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
        });
    }

    m_live_rig.loadLiveRig(
        std::move(request),
        safeCallback(
            [completion = std::move(on_loaded)](
                std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError>
                    loaded) {
                if (!loaded.has_value())
                {
                    completion(std::unexpected{std::move(loaded.error())});
                    return;
                }
                completion(std::move(*loaded));
            }));
}

// Resolves the current project's native-song directory. Returns empty when no project is loaded;
// callers already guard that a project is loaded before minting or reloading against this path.
std::filesystem::path EditorController::Impl::currentSongDirectory() const
{
    if (!m_project.has_value())
    {
        return {};
    }
    return songDirectoryForProject(*m_project);
}

// Clears the audio backend's live rig chain as part of project teardown.
void EditorController::Impl::clearLiveRig()
{
    const auto cleared = m_live_rig.clearLiveRig();
    if (!cleared.has_value())
    {
        reportError(std::string{"Could not clear live rig: "} + cleared.error().message);
    }
}

// Runs project write actions through one task-runner path so busy lifetime, stale completion
// checks, and project restoration stay consistent across save, save-as, and publish.
void EditorController::Impl::runProjectWriteAction(EditorAction::ProjectWriteAction&& action)
{
    auto state = takeProjectForWrite(std::move(action));
    if (state == nullptr)
    {
        return;
    }

    runWorkerThreadBusyOperation(
        busyOperationForProjectWrite(state->action),
        state,
        [save_function = m_save_function,
         save_as_function = m_save_as_function,
         publish_function =
             m_publish_function](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            std::visit(
                [&task_state, &save_function, &save_as_function, &publish_function](
                    auto&& alternative) {
                    using A = std::decay_t<decltype(alternative)>;
                    if constexpr (std::is_same_v<A, EditorAction::SaveProject>)
                    {
                        task_state->result = save_function(
                            task_state->project, task_state->song, task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
                    {
                        task_state->result = save_as_function(
                            task_state->project,
                            alternative.file,
                            task_state->song,
                            task_state->editor_state);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::PublishProject>)
                    {
                        task_state->result = publish_function(
                            task_state->project, alternative.file, task_state->song);
                    }
                },
                task_state->action);
        },
        [this](const std::shared_ptr<ProjectWriteTaskState>& task_state) {
            completeProjectWriteAction(task_state);
        });
}

// Restores the Project context, clears busy before errors, and applies the successful write's
// action-specific state transition on the message thread.
void EditorController::Impl::completeProjectWriteAction(
    const std::shared_ptr<ProjectWriteTaskState>& state)
{
    assert(isBusy() && "completeProjectWriteAction called outside a busy operation");

    const EditorAction::Id action_id = idOf(state->action);
    m_project = std::move(state->project);
    if (!state->result.has_value())
    {
        const std::string message = state->result.error().message;
        // A deferred action present at the moment of a failed save means this save was the one
        // synthesized by the unsaved-changes prompt's Save branch; the user wanted to protect
        // their work first, so dropping the deferred replay is the right escape.
        if (m_deferred_project_action_state.hasDeferredAction())
        {
            clearDeferredProjectAction();
        }
        finishBusyOperation();
        reportError(std::string{projectWriteErrorPrefix(state->action)} + message);
        return;
    }

    std::visit(
        [this](const auto& alternative) { applyProjectWriteSuccess(alternative); }, state->action);

    finishBusyOperation();
    if ((action_id == EditorAction::Id::SaveProject ||
         action_id == EditorAction::Id::SaveProjectAs) &&
        m_deferred_project_action_state.hasDeferredAction())
    {
        replayDeferredProjectActionAfterSave();
    }
}

void EditorController::Impl::applyProjectWriteSuccess(const EditorAction::SaveProject& /*action*/)
{
    m_has_untracked_unsaved_changes = false;
    markUndoHistoryClean("undo.mark_clean.save_project");
    saveCurrentProjectCursorPositionBestEffort("store project cursor after save");
}

void EditorController::Impl::applyProjectWriteSuccess(const EditorAction::SaveProjectAs& action)
{
    m_save_requires_destination = false;
    m_project_file = action.file;
    m_displaced_project_file.clear();
    m_has_untracked_unsaved_changes = false;
    markUndoHistoryClean("undo.mark_clean.save_project_as");
    saveCurrentProjectCursorPositionBestEffort("store project cursor after save-as");
    // Save As is the first moment an imported project has a path (and an existing project adopts
    // a new one), so the active grid note value is persisted here or a selection made before the
    // first save is lost on reopen.
    recordSettingsResultBestEffort(
        m_settings.saveProjectGridNoteValue(m_project_file, m_grid_note_value),
        "store project grid note value after save-as");
    if (m_timeline_zoom_pixels_per_second > 0.0)
    {
        recordSettingsResultBestEffort(
            m_settings.saveProjectTimelineZoom(m_project_file, m_timeline_zoom_pixels_per_second),
            "store project timeline zoom after save-as");
    }
}

void EditorController::Impl::applyProjectWriteSuccess(
    const EditorAction::PublishProject& /*action*/)
{
    // Publish does not change save destination or dirty state.
}

// Resumes a deferred project action after Save or Save As has protected user changes.
void EditorController::Impl::replayDeferredProjectActionAfterSave()
{
    std::optional<EditorAction::ProjectAction> deferred_action =
        m_deferred_project_action_state.takeReplay();
    if (!deferred_action.has_value())
    {
        updateView();
        return;
    }

    runProjectAction(std::move(*deferred_action));
}

// Clears all prompt-related state without changing the currently loaded project.
void EditorController::Impl::clearDeferredProjectAction() noexcept
{
    m_deferred_project_action_state.clear();
}

// Clears the restore-interrupted marker without touching the normal last-project path.
void EditorController::Impl::clearInterruptedRestoreMarker()
{
    recordSettingsResultBestEffort(
        m_settings.setInterruptedRestoreProject(std::nullopt), "clear interrupted restore marker");
}

// Removes the regular last-project path only when it refers to a now-invalid restore target.
void EditorController::Impl::clearLastOpenProjectIfMatches(
    const std::filesystem::path& project_file)
{
    const std::optional<std::filesystem::path> last_project = m_settings.lastOpenProject();
    if (last_project.has_value() && *last_project == project_file)
    {
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(std::nullopt), "clear missing restore project");
    }
}

// Selects the project path persisted for next launch before exit tears down controller state.
std::optional<std::filesystem::path> EditorController::Impl::restorableProjectFileForExit() const
{
    std::optional<std::filesystem::path> restorable_project_file = currentProjectFile();
    if (!restorable_project_file.has_value() && !m_displaced_project_file.empty())
    {
        restorable_project_file = m_displaced_project_file;
    }
    if (!restorable_project_file.has_value() && m_pending_restore_project_file.has_value())
    {
        restorable_project_file = m_pending_restore_project_file;
    }

    return restorable_project_file;
}

// Restores a settings-backed project, or prompts first if the previous restore was interrupted.
void EditorController::Impl::restoreLastOpenProject()
{
    m_restore_interrupted_prompt_file.reset();
    const std::optional<std::filesystem::path> interrupted_project_file =
        m_settings.interruptedRestoreProject();
    if (interrupted_project_file.has_value())
    {
        if (!projectFileExists(*interrupted_project_file))
        {
            clearLastOpenProjectIfMatches(*interrupted_project_file);
            clearInterruptedRestoreMarker();
        }
        else
        {
            m_restore_interrupted_prompt_file = *interrupted_project_file;
            updateView();
            return;
        }
    }

    const std::optional<std::filesystem::path> project_file = m_settings.lastOpenProject();
    if (!project_file.has_value())
    {
        return;
    }

    if (!projectFileExists(*project_file))
    {
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(std::nullopt), "clear missing last project");
        clearInterruptedRestoreMarker();
        return;
    }

    runAction(EditorAction::RestoreProject{*project_file});
}

// Captures editor-only persistence state from the current transport and displayed arrangement.
ProjectEditorState EditorController::Impl::projectEditorStateForSave() const
{
    ProjectEditorState editor_state{.selected_arrangement = std::nullopt};

    const auto* arrangement = session().currentArrangement();
    if (arrangement != nullptr && !arrangement->id.empty())
    {
        editor_state.selected_arrangement = arrangement->id;
    }

    return editor_state;
}

// Chooses the cursor restored for a project open from app-local resume state.
common::core::TimePosition EditorController::Impl::cursorPositionForOpenedProject(
    const std::filesystem::path& project_file) const
{
    const auto saved_position = m_settings.projectCursorPositionFor(project_file);
    if (saved_position.has_value())
    {
        if (saved_position->has_value())
        {
            return session().timeline().clamp(**saved_position);
        }
    }
    else
    {
        logEditorControllerBestEffortFailure(
            "restore project cursor position", saved_position.error().message);
    }

    return session().timeline().start;
}

// Chooses the grid note value restored for a project open from app-local editor settings, falling
// back to the quarter-note default for unknown projects or out-of-bounds stored values.
common::core::Fraction EditorController::Impl::gridNoteValueForOpenedProject(
    const std::filesystem::path& project_file) const
{
    const auto saved_note_value = m_settings.projectGridNoteValueFor(project_file);
    if (saved_note_value.has_value())
    {
        if (saved_note_value->has_value() && isValidTempoGridNoteValue(**saved_note_value))
        {
            return **saved_note_value;
        }
    }
    else
    {
        logEditorControllerBestEffortFailure(
            "restore project grid note value", saved_note_value.error().message);
    }

    return common::core::Fraction{1, 4};
}

// Chooses the timeline zoom restored for a project open from app-local editor settings, falling
// back to zero (view default) for unknown projects; the view clamps restored values to its own
// timeline bounds.
double EditorController::Impl::timelineZoomForOpenedProject(
    const std::filesystem::path& project_file) const
{
    const auto saved_zoom = m_settings.projectTimelineZoomFor(project_file);
    if (saved_zoom.has_value())
    {
        if (saved_zoom->has_value() && std::isfinite(**saved_zoom) && **saved_zoom > 0.0)
        {
            return **saved_zoom;
        }
    }
    else
    {
        logEditorControllerBestEffortFailure(
            "restore project timeline zoom", saved_zoom.error().message);
    }

    return 0.0;
}

// Saves the current cursor as app-local resume state for saved projects.
void EditorController::Impl::saveCurrentProjectCursorPositionBestEffort(std::string_view context)
{
    if (m_project_file.empty())
    {
        return;
    }

    saveProjectCursorPositionBestEffort(m_project_file, m_transport.position(), context);
}

// Records a cursor position outside the .rhp package so cursor movement never makes project
// content dirty.
void EditorController::Impl::saveProjectCursorPositionBestEffort(
    const std::filesystem::path& project_file, common::core::TimePosition cursor_position,
    std::string_view context)
{
    if (project_file.empty())
    {
        return;
    }

    recordSettingsResultBestEffort(
        m_settings.saveProjectCursorPosition(project_file, cursor_position), context);
}

// Prepares project audio, activates the selected arrangement, and commits the song to Session.
std::expected<void, common::audio::SongAudioError> EditorController::Impl::loadSessionSong(
    common::core::Song song, const std::filesystem::path& song_directory,
    const std::optional<std::string>& selected_arrangement)
{
    if (song.arrangements.empty())
    {
        return std::unexpected{common::audio::SongAudioError{
            common::audio::SongAudioErrorCode::MissingAudioAssetPath,
            "Project song contains no arrangements",
        }};
    }

    // Establish the tone baseline before the song is committed: every arrangement gets a real
    // default tone document (minted here for tone-less imports and pre-tone packages) plus an
    // explicit catalog entry and whole-song region. This is the single seam that guarantees the
    // invariant; capture, restore, and the projections no longer handle the tone-less shape. The
    // materialized state joins the loaded (clean) baseline rather than showing up as an unsaved
    // change.
    for (common::core::Arrangement& arrangement : song.arrangements)
    {
        if (!arrangement.tone_document_ref.empty())
        {
            continue;
        }
        auto minted = m_live_rig.mintEmptyTone(song_directory);
        if (!minted.has_value())
        {
            return std::unexpected{common::audio::SongAudioError{
                common::audio::SongAudioErrorCode::ToneBaselineFailed,
                "Could not create a default tone for arrangement " + arrangement.id + ": " +
                    minted.error().message,
            }};
        }
        arrangement.tone_document_ref = std::move(*minted);
    }
    common::core::ensureExplicitToneRegions(song);

    auto prepared = m_song_audio.prepareSong(song);
    if (!prepared.has_value())
    {
        return std::unexpected{std::move(prepared.error())};
    }

    const std::size_t selected_index = getSelectedArrangementIndex(song, selected_arrangement);
    m_session_load_in_progress = true;
    auto active_arrangement_set =
        m_song_audio.setActiveArrangement(song.arrangements[selected_index]);
    bool committed = false;
    if (active_arrangement_set.has_value())
    {
        committed = m_session.loadSong(std::move(song), selected_index);
        assert(committed && "Session rejected backend-accepted project song");
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_output_gain_preview_before.reset();
        m_plugin_catalog.hide();
    }
    m_session_load_in_progress = false;

    if (!active_arrangement_set.has_value())
    {
        return std::unexpected{std::move(active_arrangement_set.error())};
    }

    if (!committed)
    {
        return std::unexpected{common::audio::SongAudioError{
            common::audio::SongAudioErrorCode::BackendClipInsertionFailed,
            "Editor session rejected backend-accepted project song",
        }};
    }

    return {};
}

// Resolves the next-launch recovery prompt shown after startup restore was interrupted.
void EditorController::Impl::onRestoreInterruptedDecision(RestoreInterruptedDecision decision)
{
    if (!m_restore_interrupted_prompt_file.has_value())
    {
        updateView();
        return;
    }

    const std::filesystem::path project_file = *m_restore_interrupted_prompt_file;
    m_restore_interrupted_prompt_file.reset();
    switch (decision)
    {
        case RestoreInterruptedDecision::Retry:
        {
            if (!projectFileExists(project_file))
            {
                clearLastOpenProjectIfMatches(project_file);
                clearInterruptedRestoreMarker();
                updateView();
                return;
            }

            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(project_file), "store retry restore project");
            runAction(EditorAction::RestoreProject{project_file});
            break;
        }
        case RestoreInterruptedDecision::Cancel:
        {
            m_pending_restore_project_file.reset();
            recordSettingsResultBestEffort(
                m_settings.setLastOpenProject(std::nullopt), "clear canceled restore project");
            clearInterruptedRestoreMarker();
            updateView();
            break;
        }
    }
}

} // namespace rock_hero::editor::core
