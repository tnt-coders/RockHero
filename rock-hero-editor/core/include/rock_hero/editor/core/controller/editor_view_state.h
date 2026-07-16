/*!
\file editor_view_state.h
\brief Headless editor view state used by the controller and view contracts.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/tab/tab_view_state.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/audio/game_audio_source_error.h>
#include <rock_hero/editor/core/busy/busy_view_state.h>
#include <rock_hero/editor/core/controller/editor_action_id.h>
#include <rock_hero/editor/core/signal_chain/plugin_browser_view_state.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <rock_hero/editor/core/timeline/arrangement_view_state.h>
#include <rock_hero/editor/core/timeline/section_view_state.h>
#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <rock_hero/editor/core/tone_designer/tone_designer_view_state.h>
#include <rock_hero/editor/core/transport/transport_view_state.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief User choice returned from the unsaved-changes confirmation prompt. */
enum class UnsavedChangesDecision : std::uint8_t
{
    /*! \brief Save the current project before continuing the deferred action. */
    Save,

    /*! \brief Discard current project changes and continue the deferred action. */
    Discard,

    /*! \brief Cancel the deferred action and keep the current project unchanged. */
    Cancel,
};

/*! \brief User choice returned from the tone-import automation-drop confirmation. */
enum class ToneImportDecision : std::uint8_t
{
    /*! \brief Replace the active tone's rig and drop its automation. */
    Import,

    /*! \brief Keep the active tone unchanged. */
    Cancel,
};

/*! \brief User choice returned from the interrupted-restore recovery prompt. */
enum class RestoreInterruptedDecision : std::uint8_t
{
    /*! \brief Retry opening the project that was interrupted on the previous run. */
    Retry,

    /*! \brief Skip restoring the interrupted project and clear the recovery marker. */
    Cancel,
};

/*! \brief User choice returned from the startup game-audio recommendation prompt. */
enum class GameAudioRecommendationDecision : std::uint8_t
{
    /*! \brief Adopt the game's audio configuration (the recommended path). */
    UseGameSettings,

    /*! \brief Keep the editor's own audio settings. */
    UseCustomSettings,

    /*! \brief The prompt was closed without choosing; nothing is persisted and it may re-ask. */
    Dismissed,
};

/*!
\brief Describes the unsaved-changes prompt the view should present.

The prompt only appears in view state when the controller has a deferred action waiting on the
user's decision, so prompted_action has no meaningful default; callers must initialize it
explicitly with the deferred action's identity.
*/
struct UnsavedChangesPrompt
{
    /*!
    \brief Creates a prompt request for a deferred action.
    \param action Action the prompt is currently about.
    */
    explicit constexpr UnsavedChangesPrompt(EditorActionId action) noexcept
        : prompted_action(action)
    {}

    /*! \brief Action the prompt is currently about; controls the prompt text. */
    EditorActionId prompted_action;

    /*!
    \brief Compares two prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const UnsavedChangesPrompt& lhs, const UnsavedChangesPrompt& rhs) =
        default;
};

/*!
\brief Describes a controller-requested Save As chooser.

Same as UnsavedChangesPrompt, prompted_action only exists when the chooser is being requested for
a known deferred action and must be initialized explicitly.
*/
struct SaveAsPrompt
{
    /*!
    \brief Creates a Save As prompt request for a deferred action.
    \param action Action the chooser will continue.
    */
    explicit constexpr SaveAsPrompt(EditorActionId action) noexcept
        : prompted_action(action)
    {}

    /*! \brief Action the chooser will continue once the user selects a save destination. */
    EditorActionId prompted_action;

    /*!
    \brief Compares two Save As prompt requests by their stored values.
    \param lhs Left-hand Save As prompt request.
    \param rhs Right-hand Save As prompt request.
    \return True when both Save As prompt requests store equal values.
    */
    friend bool operator==(const SaveAsPrompt& lhs, const SaveAsPrompt& rhs) = default;
};

/*!
\brief Confirms that importing a tone file will drop the active tone's automation.

Automation curves may live in tone regions far from the visible timeline, so their destruction
is confirmed rather than merely undoable — the user might not notice the loss until long after
the undo window of attention. Import over an automation-free tone never prompts.
*/
struct ToneImportPrompt
{
    /*!
    \brief Creates a tone-import confirmation request.
    \param automation_parameter_count_value Automated parameter count the import would drop.
    */
    explicit constexpr ToneImportPrompt(std::size_t automation_parameter_count_value) noexcept
        : automation_parameter_count(automation_parameter_count_value)
    {}

    /*! \brief Number of automated parameters the import would remove. */
    std::size_t automation_parameter_count;

    /*!
    \brief Compares two tone-import prompts by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const ToneImportPrompt& lhs, const ToneImportPrompt& rhs) = default;
};

/*!
\brief Describes a startup project restore that was interrupted on the previous run.

The view presents this prompt instead of auto-opening the same project again, giving the user a
way to avoid a repeated restore loop while keeping healthy startup restore automatic.
*/
struct RestoreInterruptedPrompt
{
    /*!
    \brief Creates an interrupted-restore prompt request.
    \param project_file_value Project package path that did not finish opening previously.
    */
    explicit RestoreInterruptedPrompt(std::filesystem::path project_file_value)
        : project_file(std::move(project_file_value))
    {}

    /*! \brief Project path that did not finish opening on the previous editor run. */
    std::filesystem::path project_file;

    /*!
    \brief Compares two interrupted-restore prompt requests by their stored paths.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal project paths.
    */
    friend bool operator==(
        const RestoreInterruptedPrompt& lhs, const RestoreInterruptedPrompt& rhs) = default;
};

/*!
\brief Startup notice that the game's audio settings were requested but cannot be used.

Raised only when the persisted "use game audio settings" toggle is on but the game's configuration
regressed (file gone, route gone, or calibration gone): the controller has already written the
toggle off and fallen back to the editor's own settings, so this prompt reports why. The view
presents the carried canonical message once and opens the audio device settings window on
dismissal.
*/
struct GameAudioUnavailablePrompt
{
    /*!
    \brief Creates an unavailable-game prompt request.
    \param error_value Typed reason with the canonical user-facing message to display.
    */
    explicit GameAudioUnavailablePrompt(GameAudioSourceError error_value)
        : error(std::move(error_value))
    {}

    /*! \brief Typed reason the game's audio settings cannot be used, with canonical message. */
    GameAudioSourceError error;

    /*!
    \brief Compares two unavailable-game prompt requests by their stable reason codes.

    Message text is diagnostic payload, not identity: the view's present-once tracking only needs
    to distinguish prompts that report different reasons.

    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests carry the same reason code.
    */
    friend bool operator==(
        const GameAudioUnavailablePrompt& lhs, const GameAudioUnavailablePrompt& rhs)
    {
        return lhs.error.code == rhs.error.code;
    }
};

/*!
\brief Standing failure notice that no audio device is open, when raised.

Staged whenever the editor ends up without an open audio device outside the flows that
legitimately close it (a staging settings edit, an in-flight device operation, an unresolved
startup game-audio prompt). The view renders it as the editor-wide blocking failure overlay,
which follows this state directly: it appears while the prompt is staged, live-updates its text,
and retracts when a device opens or the audio settings window takes over.
*/
struct AudioDeviceFailurePrompt
{
    /*! \brief Reason no device is open: the backend's diagnostic, or "Disconnected". */
    std::string message;

    /*!
    \brief Compares two failure prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(
        const AudioDeviceFailurePrompt& lhs, const AudioDeviceFailurePrompt& rhs) = default;
};

/*! \brief User decisions available on the audio-device failure overlay. */
enum class AudioDeviceFailureDecision : std::uint8_t
{
    /*! \brief Re-apply the active source's saved route. */
    Retry,

    /*! \brief Open the audio device settings window to fix the route by hand. */
    OpenSettings,
};

/*! \brief Describes an active input calibration prompt requested by the controller. */
struct InputCalibrationPrompt
{
    /*! \brief Message shown by the calibration prompt. */
    std::string message;

    /*! \brief Input gain currently displayed by the calibration prompt. */
    double input_gain_db{0.0};

    /*!
    \brief Compares two input calibration prompt requests by their stored values.
    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const InputCalibrationPrompt& lhs, const InputCalibrationPrompt& rhs) =
        default;
};

/*!
\brief Full undo/redo stack contents for the editor's history inspector panel.

Populated on every state push so the panel reflects entries in real time. It mirrors the undo stack
for display only; it carries no policy and the view reads it only while the inspector is shown.
*/
struct UndoHistoryState
{
    /*! \brief Every entry label, oldest first. */
    std::vector<std::string> labels;

    /*! \brief Cursor: entries before this index are undoable, the rest are redoable. */
    std::size_t position{};

    /*! \brief Reachable clean-marker position, when a clean marker is set. */
    std::optional<std::size_t> clean_position{};

    /*!
    \brief Compares two undo-history states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const UndoHistoryState& lhs, const UndoHistoryState& rhs) = default;
};

/*!
\brief Full message-thread state rendered by the editor view.

The controller derives this state from transport, audio, and session information, then pushes it to
the concrete JUCE view through IEditorView.
*/
struct EditorViewState
{
    /*! \brief Enables or disables the File > Open command. */
    bool open_enabled{false};

    /*! \brief Enables or disables the File > Import command. */
    bool import_enabled{false};

    /*! \brief Enables or disables the File > Save command. */
    bool save_enabled{false};

    /*! \brief Enables or disables the File > Save As command. */
    bool save_as_enabled{false};

    /*! \brief Enables or disables the File > Publish command. */
    bool publish_enabled{false};

    /*! \brief Enables or disables the Edit > Undo command. */
    bool undo_enabled{false};

    /*! \brief Label for the undoable edit, if a command-specific label is available. */
    std::optional<std::string> undo_label{};

    /*! \brief Enables or disables the Edit > Redo command. */
    bool redo_enabled{false};

    /*! \brief Label for the redoable edit, if a command-specific label is available. */
    std::optional<std::string> redo_label{};

    /*! \brief Full undo/redo stack contents for the history inspector panel (toggled with F8). */
    UndoHistoryState undo_history{};

    /*! \brief Suggested .rock destination used to pre-fill the publish chooser. */
    std::filesystem::path suggested_publish_file{};

    /*! \brief Enables or disables the File > Close command. */
    bool close_enabled{false};

    /*! \brief Reports whether a project arrangement is currently loaded for display. */
    bool project_loaded{false};

    /*! \brief Open project package path, or empty when the loaded work has no project file yet. */
    std::optional<std::filesystem::path> project_file{};

    /*!
    \brief Monotonic id of the loaded project, bumped on each successful open/restore/import.

    The view recognizes a freshly loaded project by a change in this value versus the previously
    rendered state, and uses it to trigger one-shot load behavior such as centering the timeline on
    the restored cursor. It stays constant across non-load state pushes (so re-rendering identical
    state never re-triggers) and is left unchanged on a failed load.
    */
    std::uint64_t project_load_id{0};

    /*! \brief Selects whether File > Save should ask for a destination first. */
    bool save_requires_destination{false};

    /*! \brief Current transport state shown by the editor. */
    TransportViewState transport{};

    /*! \brief Menu-bar status text for the current audio-device route. */
    std::string audio_device_status_text{"[audio device closed]"};

    /*! \brief Enables or disables opening audio-device settings. */
    bool audio_device_settings_enabled{true};

    /*!
    \brief True when the editor sources the game's audio configuration (resolved toggle).

    Resolves the persisted "use game settings" toggle through its off default. A true value means
    adoption actually succeeded — the controller never leaves the toggle on while running on the
    editor's own settings — so when true both the device settings window and the calibration window
    render as read-only reflections of the game's configuration.
    */
    bool use_game_audio_settings{false};

    /*!
    \brief Startup notice that the requested game audio settings cannot be used, when raised.

    Present only after a startup that found the toggle on but the game's configuration unusable;
    the toggle has already been written off and the editor runs on its own settings. The view shows
    the carried message once and opens the audio device settings window on dismissal.
    */
    std::optional<GameAudioUnavailablePrompt> game_audio_unavailable_prompt{};

    /*!
    \brief True while the startup game-audio recommendation prompt should be shown.

    Raised only when the toggle is off, a calibrated game configuration exists to adopt, and the
    user has not suppressed the recommendation — so accepting it always succeeds. The view answers
    through IEditorController::onGameAudioRecommendationDecision.
    */
    bool game_audio_recommendation_prompt{false};

    /*!
    \brief Standing notice that no audio device is open, when raised.

    Present whenever the editor runs without an open audio device and no other flow owns the
    situation (settings window open, busy device operation in flight, startup game-audio prompt
    unresolved). The view renders the blocking failure overlay while present and answers through
    IEditorController::onAudioDeviceFailureDecision.
    */
    std::optional<AudioDeviceFailurePrompt> audio_device_failure_prompt{};

    /*!
    \brief Visible timeline range used to map cursor position and waveform content to pixels.
    */
    common::core::TimeRange visible_timeline{};

    /*! \brief Song-level tempo map used to render the editor beat grid. */
    common::core::TempoMap tempo_map{};

    /*!
    \brief Song-structure sections resolved to seconds for the ruler's section chip row; empty
    when the song defines none.
    */
    std::vector<SongSectionView> sections{};

    /*!
    \brief Grid step as a fraction of a whole note, shared by the track grid, ruler, and snapping.

    A 1/8 grid means eighth notes in every meter. Initialized to the quarter-note default because
    the Fraction default of 0/1 is a degenerate step.
    */
    common::core::Fraction grid_note_value{1, 4};

    /*!
    \brief Horizontal timeline scale to restore on a fresh project load.

    Zero means no per-project zoom is stored and the view keeps its default. The view applies
    this only when project_load_id changes; ordinary state pushes never fight user zooming.
    */
    double timeline_zoom_pixels_per_second{0.0};

    /*! \brief Current arrangement waveform state shown by the editor. */
    ArrangementViewState arrangement{};

    /*! \brief Current tone track row state shown below the backing waveform. */
    ToneTrackViewState tone_track{};

    /*! \brief Automation lanes for the selected tone, shown beneath the tone strip. */
    ToneAutomationViewState tone_automation{};

    /*!
    \brief Seconds-resolved chart content for the current arrangement's tablature lane.

    Shared immutably because charts hold thousands of notes: the controller rebuilds the
    projection only when the displayed arrangement or the chart revision changes, and every
    state copy shares one instance. Null when the arrangement has no chart. Pointer identity
    stands in for content equality in view-state comparisons, matching the rebuild rule.
    */
    std::shared_ptr<const common::core::TabViewState> tab{};

    /*!
    \brief Seconds-resolved 3D highway projection of the displayed arrangement.

    The shared scene model the 3D preview window renders (plan 44) — the same projection the
    game highway consumes, so what the charter previews is what the player gets. Rebuilt under
    the same rule as \ref tab (only when the displayed arrangement changes) and shared immutably
    across state copies. Null when the arrangement has no chart.
    */
    std::shared_ptr<const common::core::HighwayViewState> highway{};

    /*! \brief True when the waveform draws behind the tablature lane; app-wide preference. */
    bool waveform_visible{true};

    /*!
    \brief App-wide minimum number of tablature string lanes to display.

    Zero means match the chart's string count. The rendered lane count is the larger of this and
    the chart's own count, so raising it only ever adds empty lanes and never hides notes.
    */
    int tab_minimum_displayed_strings{0};

    /*! \brief Current signal-chain view state. */
    SignalChainViewState signal_chain{};

    /*! \brief Current plugin browser window state. */
    PluginBrowserViewState plugin_browser{};

    /*!
    \brief Tone Designer document state for the signal-chain panel header and button strip.

    Active exactly when no project is open: the resting editor is a live rig editing a
    file-backed tone document. The unsaved-changes and Save As prompts below serve the designer
    document whenever this is active (the view keys tone-flavored copy off it).
    */
    ToneDesignerViewState tone_designer{};

    /*! \brief Tone-import confirmation to present, if an import would drop automation. */
    std::optional<ToneImportPrompt> tone_import_prompt{};

    /*! \brief Unsaved-changes prompt to present, if the controller is awaiting a decision. */
    std::optional<UnsavedChangesPrompt> unsaved_changes_prompt{};

    /*! \brief Save As chooser request to present, if the controller needs a destination. */
    std::optional<SaveAsPrompt> save_as_prompt{};

    /*! \brief Interrupted startup restore prompt to present, if recovery input is needed. */
    std::optional<RestoreInterruptedPrompt> restore_interrupted_prompt{};

    /*! \brief Input calibration prompt to present, if live input setup is required. */
    std::optional<InputCalibrationPrompt> input_calibration_prompt{};

    /*!
    \brief Active editor-wide busy state, if any.

    When set, the view displays the busy overlay, blocks input, and the controller drops new
    intents until the operation completes or is superseded.
    */
    std::optional<BusyViewState> busy{};

    /*!
    \brief Compares two editor view states by their stored values.
    \param lhs Left-hand editor view state.
    \param rhs Right-hand editor view state.
    \return True when both editor view states store equal values.
    */
    friend bool operator==(const EditorViewState& lhs, const EditorViewState& rhs) = default;
};

} // namespace rock_hero::editor::core
