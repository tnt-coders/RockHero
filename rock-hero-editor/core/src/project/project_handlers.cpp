#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <rock_hero/common/core/tone/tone_track_normalize.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <variant>

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

// Chooses the arrangement shown when there is no usable saved choice: prefer Lead, then Rhythm,
// then Bass so the most guitar-forward part available shows first, falling back to the first
// arrangement only when the song carries none of those parts.
[[nodiscard]] std::size_t defaultArrangementIndex(const common::core::Song& song)
{
    for (const common::core::Part preferred :
         {common::core::Part::Lead, common::core::Part::Rhythm, common::core::Part::Bass})
    {
        const auto match =
            std::ranges::find(song.arrangements, preferred, &common::core::Arrangement::part);
        if (match != song.arrangements.end())
        {
            return static_cast<std::size_t>(std::distance(song.arrangements.begin(), match));
        }
    }
    return 0;
}

// Resolves a persisted arrangement id to the current song order. A missing choice, or a stored id
// that no longer matches any arrangement (reachable now that save no longer validates the id), both
// mean "no usable choice" and fall back to the guitar-forward default rather than raw index 0.
[[nodiscard]] std::size_t getSelectedArrangementIndex(
    const common::core::Song& song, const std::optional<std::string>& selected_arrangement)
{
    if (selected_arrangement.has_value())
    {
        const auto found = std::ranges::find(
            song.arrangements, *selected_arrangement, &common::core::Arrangement::id);
        if (found != song.arrangements.end())
        {
            return static_cast<std::size_t>(std::distance(song.arrangements.begin(), found));
        }
    }

    return defaultArrangementIndex(song);
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
    m_live_input_monitor.disableMonitoring();

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
        // Distinct capture name: clang's -Wshadow-uncaptured-local flags `x = std::move(x)`.
        [state,
         open_function = m_open_function,
         owned_report_progress = std::move(report_progress)] {
            state->result = open_function(state->project, state->file, owned_report_progress);
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
        // A failed startup restore reaches here with no designer yet; make the resting rig live
        // instead of leaving a dead editor. Opens from an existing state are unaffected (either
        // the old project or the old designer is still intact, so this no-ops).
        enterToneDesignerIfNoProject("open_failed");
        finishBusyOperation();
        reportError(std::string{"Could not open: "} + message);
        return;
    }

    common::core::Song song = std::move(*state->result);

    // Restore the last displayed arrangement from app-local settings; an unknown or stale id falls
    // back to the guitar-forward default inside loadSessionSong, and is never written back as if
    // the user chose it.
    auto song_loaded = loadSessionSong(
        std::move(song),
        songDirectoryForProject(state->project),
        m_settings.projectSelectedArrangementFor(state->file));
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
        // Session-load failure never committed the project; whichever resting state existed
        // before (old project or designer) is intact, so this only rescues the fresh-launch case.
        enterToneDesignerIfNoProject("open_session_load_failed");
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
        // The commit already left the designer and the teardown above ends project-less, so
        // stand the resting rig back up rather than leaving a dead chain.
        enterToneDesignerIfNoProject("open_live_rig_failed");
        finishBusyOperation();
        reportError(
            std::string{"Could not load live rig from: "} + state->file.string() + ": " +
            rig_result.error().message);
        return;
    }

    const bool next_has_unsaved_changes = state->project.audioNormalizationUpdatedOnLoad();
    const std::optional<EditorProjectMarker> next_marker = markerForOpenedProject(state->file);
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

    // Restore the marker exactly as stored (the marker model): a passive cursor seeks the
    // transport to its raw saved time; an armed caret seeks to its slot's musical time and
    // re-arms on the same exact grid address. A stored caret whose string no longer exists on
    // the loaded chart — or that belongs to a now-chartless project — demotes to a passive
    // cursor at the same musical time: restore never clamps onto a wrong string and never
    // invents a position. An unknown project starts passive at time zero (loadSessionSong's
    // clearChartEditingState already reset the marker itself).
    if (next_marker.has_value())
    {
        std::visit([this](const auto& stored) { restoreProjectMarker(stored); }, *next_marker);
    }
    else
    {
        m_transport.seek(common::core::TimePosition{0.0});
    }
    // Make the tone under the restored cursor active (without a formal selection) so a reopened
    // project shows its tone from the start; the baseline reaches back to time 0, so a lead-in chart
    // resolves to its default tone.
    activateToneAtCursor();
    if (state->clear_last_open_project_on_failure)
    {
        clearInterruptedRestoreMarker();
    }
    static_cast<void>(m_live_input_monitor.applyGate(monitoringContext()));

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
    m_live_input_monitor.disableMonitoring();

    auto state = std::make_shared<ImportTaskState>();
    state->file = file;

    m_pending_restore_project_file.reset();
    m_restore_interrupted_prompt_file.reset();
    clearInterruptedRestoreMarker();
    const std::uint64_t token = beginBusy(BusyOperation::ImportingProject);
    EditorController::ProjectOperationProgress report_progress =
        makeBusyProjectOperationProgress(token);

    m_task_runner.submit(
        // Distinct capture name: clang's -Wshadow-uncaptured-local flags `x = std::move(x)`.
        [state,
         import_function = m_import_function,
         owned_report_progress = std::move(report_progress)] {
            state->result = import_function(state->project, state->file, owned_report_progress);
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
        m_open_automation_lanes.clear();
        resetUndoHistory("undo.reset.import_live_rig_failed");
        // The teardown above ends project-less; stand the resting rig back up.
        enterToneDesignerIfNoProject("import_live_rig_failed");
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
    m_open_automation_lanes.clear();
    m_has_untracked_unsaved_changes = false;
    m_session_faulted = false;
    clearDeferredProjectAction();
    m_project_audio_ready = true;
    resetUndoHistory("undo.reset.import_project");
    markUndoHistoryClean("undo.mark_clean.import_project");
    static_cast<void>(m_live_input_monitor.applyGate(monitoringContext()));

    // Imports have no persisted editor cursor, so establish an explicit start position before the
    // view observes the new project load id and recenters the timeline.
    m_transport.seek(session().timeline().start);
    // Make the tone under the start cursor active (without a formal selection) so the imported
    // arrangement opens with its default tone; the baseline owns the pre-measure-1 lead-in.
    activateToneAtCursor();

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
                m_loaded_tone_refs.clear();
                m_loaded_tone_refs.reserve(rig_result->tone_chains.size());
                for (const common::audio::LoadedToneChainIdentities& chain :
                     rig_result->tone_chains)
                {
                    m_loaded_tone_refs.push_back(chain.tone_document_ref);
                }
                // One-way host-tempo mirror so hosted plugins see the song's real tempo map
                // instead of the backend default. A future tempo-editing flow must re-mirror
                // after every tempo-map change alongside rebuildToneAutomationCurves().
                m_song_audio.mirrorTempoMap(session().song().tempo_map);
                static_cast<void>(m_live_input_monitor.applyGate(monitoringContext()));
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

    // In designer mode the protected document is the tone file: "needs a destination" means the
    // document is untitled, Save runs the tone export, and Discard replays without any project
    // teardown (the replayed action replaces or tears down the designer itself).
    const bool designer_document = m_tone_designer.active && !m_project.has_value();
    const bool save_requires_destination = designer_document
                                               ? !m_tone_designer.document_path.has_value()
                                               : m_save_requires_destination;
    std::visit(
        [this, designer_document](auto&& step) {
            using Step = std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<Step, DeferredProjectActionState::Refresh>)
            {
                updateView();
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::SaveThenReplay>)
            {
                if (designer_document)
                {
                    // SaveThenReplay is only produced when no destination is needed, so the
                    // association exists by the deferral state machine's contract.
                    if (m_tone_designer.document_path.has_value())
                    {
                        runToneDesignerSave(*m_tone_designer.document_path);
                    }
                }
                else
                {
                    runProjectAction(EditorAction::SaveProject{});
                }
            }
            else if constexpr (std::is_same_v<Step, DeferredProjectActionState::DiscardAndReplay>)
            {
                replayDiscardedProjectAction(std::move(step.action));
            }
        },
        m_deferred_project_action_state.resolveUnsavedChanges(
            action.decision, save_requires_destination));
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
// resolves. The gate protects whichever document is live: project changes in project mode, the
// Tone Designer's file-backed document otherwise.
void EditorController::Impl::requestProjectAction(EditorAction::ProjectAction action)
{
    if (hasUnsavedChanges() || toneDesignerHasUnsavedChanges())
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
    // No designer re-entry on exit: standing up a fresh rig right before shutdown is pure waste.
    if (closeProject(false))
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
bool EditorController::Impl::closeProject(bool reenter_tone_designer)
{
    m_project_audio_ready = false;
    m_live_input_monitor.disableMonitoring();

    // Close resets to the resting state: the designer flag drops before teardown so the tail can
    // re-enter with a fresh clean document and passthrough rig (skipped only on app exit). A
    // pending import confirmation dies with the project whose tone it targeted.
    leaveToneDesigner("close_project");
    m_pending_tone_import.reset();
    m_pending_tone_import_automation_count = 0;

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
        if (reenter_tone_designer)
        {
            enterToneDesignerIfNoProject("close_empty_project");
        }
        return true;
    }

    saveCurrentProjectMarkerBestEffort("store project marker before close");
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
        m_open_automation_lanes.clear();
        m_plugin_catalog.hide();
        resetUndoHistory("undo.reset.close_project_failed");
        if (reenter_tone_designer)
        {
            enterToneDesignerIfNoProject("close_project_failed");
        }
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
    m_open_automation_lanes.clear();
    m_plugin_catalog.hide();
    resetUndoHistory("undo.reset.close_project");
    if (reenter_tone_designer)
    {
        enterToneDesignerIfNoProject("close_project");
    }
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

    // Bound once behind the has_value guard above: nothing in this function touches the project
    // optional, and the named reference keeps that guarantee visible to clang-tidy's optional
    // tracking across the calls below.
    const Project& project = *m_project;

    // Capture the outgoing arrangement's rig into its tone files so switching back restores any
    // unsaved tone edits; the capture also flushes pending plugin state.
    common::core::Song song = session().song();
    if (const auto captured = captureLiveRigToDisk(project); !captured.has_value())
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
    m_open_automation_lanes.clear();
    m_project_audio_ready = false;

    if (const auto loaded = loadSessionSong(
            std::move(song), songDirectoryForProject(project), action.arrangement_id);
        !loaded.has_value())
    {
        // The session commit only happens after the backend accepts the arrangement, so a
        // failure leaves the previous arrangement displayed; restore its rig below.
        reportError(
            std::string{"Could not activate the selected arrangement: "} + loaded.error().message);
    }
    else if (!m_project_file.empty())
    {
        // The switch succeeded: remember it as app-local view state so a later reopen returns to
        // this arrangement. A discrete switch is the only persistence point; there are no
        // close/save hooks because the value cannot drift between switches.
        recordSettingsResultBestEffort(
            m_settings.saveProjectSelectedArrangement(m_project_file, action.arrangement_id),
            "store project selected arrangement after switch");
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    updateView();
    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
            .song_directory = songDirectoryForProject(project),
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
                activateToneAtCursor();
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
    m_loaded_tone_refs.clear();
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
                        task_state->result = save_function(task_state->project, task_state->song);
                    }
                    else if constexpr (std::is_same_v<A, EditorAction::SaveProjectAs>)
                    {
                        task_state->result = save_as_function(
                            task_state->project, alternative.file, task_state->song);
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
    saveCurrentProjectMarkerBestEffort("store project marker after save");
}

void EditorController::Impl::applyProjectWriteSuccess(const EditorAction::SaveProjectAs& action)
{
    m_save_requires_destination = false;
    m_project_file = action.file;
    m_displaced_project_file.clear();
    m_has_untracked_unsaved_changes = false;
    markUndoHistoryClean("undo.mark_clean.save_project_as");
    saveCurrentProjectMarkerBestEffort("store project marker after save-as");
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
    // Save As is also the first moment the displayed arrangement can be keyed to a path, so a
    // selection made before the first save survives the reopen.
    if (const auto* arrangement = session().currentArrangement();
        arrangement != nullptr && !arrangement->id.empty())
    {
        recordSettingsResultBestEffort(
            m_settings.saveProjectSelectedArrangement(m_project_file, arrangement->id),
            "store project selected arrangement after save-as");
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
            // The resting rig stands up behind the recovery prompt; a retry replaces it and a
            // decline leaves the user in a live designer instead of a dead editor.
            enterToneDesignerIfNoProject("startup_restore_interrupted");
            m_restore_interrupted_prompt_file = *interrupted_project_file;
            updateView();
            return;
        }
    }

    const std::optional<std::filesystem::path> project_file = m_settings.lastOpenProject();
    if (!project_file.has_value())
    {
        enterToneDesignerIfNoProject("startup_no_last_project");
        return;
    }

    if (!projectFileExists(*project_file))
    {
        recordSettingsResultBestEffort(
            m_settings.setLastOpenProject(std::nullopt), "clear missing last project");
        clearInterruptedRestoreMarker();
        enterToneDesignerIfNoProject("startup_last_project_missing");
        return;
    }

    runAction(EditorAction::RestoreProject{*project_file});
}

// Reads the stored resume marker for a project open from app-local resume state; absent for
// unknown projects (which start passive at time zero).
std::optional<EditorProjectMarker> EditorController::Impl::markerForOpenedProject(
    const std::filesystem::path& project_file) const
{
    return m_settings.projectMarkerFor(project_file);
}

// Restores a stored passive marker: the transport seeks to the raw saved time (the cursor's
// native coordinate — no grid math) and the remembered string waits for the next arming.
void EditorController::Impl::restoreProjectMarker(const EditorProjectCursor& cursor)
{
    m_transport.seek(session().timeline().clamp(common::core::TimePosition{cursor.seconds}));
    m_chart_marker = ChartCursor{.string = cursor.string};
}

// Restores a stored armed marker: the transport seeks to the slot's musical time and the
// caret re-arms on the exact stored grid address. When the stored string no longer exists on
// the loaded chart (or the project is chartless) the marker demotes to a passive cursor at
// that same time instead — never a clamp onto a wrong string; the stored string stays as the
// arming memory, which itself clamps against the displayed chart.
void EditorController::Impl::restoreProjectMarker(const EditorProjectCaret& caret)
{
    m_transport.seek(
        session().timeline().clamp(
            common::core::TimePosition{session().song().tempo_map.secondsAtNote(
                caret.position.measure, caret.position.beat, caret.position.offset)}));
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const bool string_exists =
        arrangement != nullptr && arrangement->chart.has_value() &&
        caret.string <= static_cast<int>(arrangement->chart->tuning.strings.size());
    if (string_exists)
    {
        armChartCaret(caret.position, caret.string);
    }
    else
    {
        m_chart_marker = ChartCursor{.string = caret.string};
    }
}

// Chooses the grid note value restored for a project open from app-local editor settings, falling
// back to the quarter-note default for unknown projects or out-of-bounds stored values.
common::core::Fraction EditorController::Impl::gridNoteValueForOpenedProject(
    const std::filesystem::path& project_file) const
{
    if (const auto saved_note_value = m_settings.projectGridNoteValueFor(project_file);
        saved_note_value.has_value() && isValidTempoGridNoteValue(*saved_note_value))
    {
        return *saved_note_value;
    }

    return common::core::Fraction{1, 4};
}

// Chooses the timeline zoom restored for a project open from app-local editor settings, falling
// back to zero (view default) for unknown projects; the view clamps restored values to its own
// timeline bounds.
double EditorController::Impl::timelineZoomForOpenedProject(
    const std::filesystem::path& project_file) const
{
    if (const auto saved_zoom = m_settings.projectTimelineZoomFor(project_file);
        saved_zoom.has_value() && std::isfinite(*saved_zoom) && *saved_zoom > 0.0)
    {
        return *saved_zoom;
    }

    return 0.0;
}

// Saves the marker in whichever state it holds as app-local resume state for saved projects:
// an armed caret as its exact grid address, a passive cursor as the raw transport time (so
// chartless projects — always passive — resume where they were with no grid math).
void EditorController::Impl::saveCurrentProjectMarkerBestEffort(std::string_view context)
{
    if (m_project_file.empty())
    {
        return;
    }

    const ChartCaret* const caret = armedChartCaret();
    const EditorProjectMarker marker =
        caret != nullptr
            ? EditorProjectMarker{EditorProjectCaret{
                  .position = caret->position, .string = caret->string
              }}
            : EditorProjectMarker{EditorProjectCursor{
                  .seconds = m_transport.position().seconds, .string = chartMarkerString()
              }};
    saveProjectMarkerBestEffort(m_project_file, marker, context);
}

// Records a marker outside the .rhp package so marker movement never makes project content
// dirty.
void EditorController::Impl::saveProjectMarkerBestEffort(
    const std::filesystem::path& project_file, const EditorProjectMarker& marker,
    std::string_view context)
{
    if (project_file.empty())
    {
        return;
    }

    recordSettingsResultBestEffort(m_settings.saveProjectMarker(project_file, marker), context);
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
        if (!arrangement.tones.empty() || !arrangement.tone_track.regions.empty())
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
        arrangement.tones.push_back(
            common::core::Tone{.tone_document_ref = std::move(*minted), .name = "Default"});
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
        // A project session now owns the rig and the history; the designer document is over.
        leaveToneDesigner("project_session_committed");
        m_signal_chain.clear();
        m_output_gain_db = 0.0;
        m_output_gain_preview_before.reset();
        m_plugin_catalog.hide();
        // Chart selection keys are chart-local; the one commit seam every
        // open/import/restore/arrangement-switch passes through clears them before a different
        // chart can display.
        clearChartEditingState();
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
