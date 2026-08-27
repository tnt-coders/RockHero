/*!
\file editor_view_state.h
\brief Headless editor view state used by the controller and view contracts.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
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
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>
#include <rock_hero/editor/core/tone_designer/tone_designer_view_state.h>
#include <rock_hero/editor/core/transport/transport_view_state.h>
#include <string>
#include <utility>
#include <variant>
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
\brief User choice returned from the warning shown before grid snap turns off.

There is no third "dismissed" alternative on purpose: every way of leaving the dialog that is not
the explicit confirmation — the recommended button, Return, Escape, closing the window — resolves
to KeepSnappingOn, so a warning the user did not answer can never turn snapping off.
*/
enum class GridSnapWarningDecision : std::uint8_t
{
    /*! \brief Proceed into free placement: turn grid snapping off. */
    TurnSnappingOff,

    /*! \brief Leave grid snapping on (the recommended path). */
    KeepSnappingOn,
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

    Hand-written, not defaulted: input_gain_db is a double of this struct's own, and a defaulted
    comparison trips -Wfloat-equal on the strict compilers once odr-used. Exact equality is
    intended — the prompt re-presents only when something actually changed.

    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests store equal values.
    */
    friend bool operator==(const InputCalibrationPrompt& lhs, const InputCalibrationPrompt& rhs)
    {
        return lhs.message == rhs.message && std::is_eq(lhs.input_gain_db <=> rhs.input_gain_db);
    }
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
\brief In-flight marquee selection rectangle over the tablature lane.

Stored resolution-independent — a seconds span plus vertical fractions of the lane height — so
the view maps it through whatever geometry it is currently painting with.
*/
struct ChartMarqueeViewState
{
    /*! \brief Earlier edge of the marquee's time span. */
    double start_seconds{0.0};

    /*! \brief Later edge of the marquee's time span. */
    double end_seconds{0.0};

    /*! \brief Top edge as a fraction of the lane bounds height, in [0, 1]. */
    float top_fraction{0.0f};

    /*! \brief Bottom edge as a fraction of the lane bounds height, in [0, 1]. */
    float bottom_fraction{0.0f};

    /*!
    \brief Compares two marquee states by their stored values.
    \param lhs Left-hand marquee state.
    \param rhs Right-hand marquee state.
    \return True when both marquee states store equal values.
    */
    friend constexpr bool operator==(
        const ChartMarqueeViewState& lhs, const ChartMarqueeViewState& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) &&
               std::is_eq(lhs.top_fraction <=> rhs.top_fraction) &&
               std::is_eq(lhs.bottom_fraction <=> rhs.bottom_fraction);
    }
};

/*!
\brief The armed caret's rendered position while it sits on an empty grid slot.

The marker model: while the marker is armed the caret is THE paused position —
typing inserts here, play starts here. Published in seconds so the lane maps it through the
same visible-timeline convention as the notation.
*/
struct ChartCaretViewState
{
    /*! \brief Caret position in seconds on the arrangement timeline. */
    double seconds{};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*!
    \brief WHICH stop of the note here the caret sits on — the mark the square draws around.

    A note under a right-hand onset wears two marks in one column: its head, carrying what the
    picking hand sounds, and the satellite digit outboard of its posture bracket, carrying what the
    fretting hand holds. The caret visits both, so the square has to say which one it is on — and
    the surface reads THIS rather than re-deriving it from the note, because the controller is what
    decided the caret could be there at all.

    `Held` is published only where that satellite is drawn, which is the same invariant the caret
    itself holds.
    */
    common::core::ChartStopChannel channel{common::core::ChartStopChannel::Sounding};

    /*!
    \brief Start of the caret's measure in seconds, for the keep-in-view window glide.

    Caret navigation keeps its whole measure comfortably visible: the view glides until the
    measure fits (or, when the measure is wider than the view, until the caret itself is in
    view). Published with the caret so the view never re-derives measure bounds from the
    tempo map.
    */
    double measure_start_seconds{};

    /*! \brief End of the caret's measure (the next measure's start) in seconds. */
    double measure_end_seconds{};

    /*!
    \brief Compares two caret states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartCaretViewState& lhs, const ChartCaretViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.string == rhs.string &&
               lhs.channel == rhs.channel &&
               std::is_eq(lhs.measure_start_seconds <=> rhs.measure_start_seconds) &&
               std::is_eq(lhs.measure_end_seconds <=> rhs.measure_end_seconds);
    }
};

/*!
\brief An empty grid slot resolved for drawing: where an insert would land.

Two overlays draw at one. The Alt-held insert ghost (\ref ChartEditViewState::insert_ghost): while
Alt is held over an insertable empty slot the lane draws a hollow white ring where an Alt+click
would plant a fret-0 note — the neutral-create verb's mouse form (§9b), the chart sibling of the
automation lane's on-curve insert ghost, published only when the ring would be honest (absent over
occupied slots, where the press keeps its select meaning), so the affordance never advertises an
action it would not perform (§7). And the pending fret box of an entry begun on an empty caret
(\ref ChartPendingFretViewState), which draws at the slot because no head exists there yet. Stored
in seconds like the caret so the lane maps it through the same visible-timeline convention.
*/
struct ChartSlotViewState
{
    /*! \brief Slot position in seconds on the arrangement timeline. */
    double seconds{};

    /*! \brief One-based string lane the slot is on. */
    int string{};

    /*!
    \brief Compares two slot states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartSlotViewState& lhs, const ChartSlotViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.string == rhs.string;
    }
};

/*!
\brief Where a retype entry's boxes draw: every selected note the typed value would write.

A named alternative rather than a bare index list, because this is one arm of the pending entry's
sum and the other names a SLOT. Never empty: an entry with no target is not a retype entry at all.
*/
struct ChartPendingFretTargets
{
    /*!
    \brief Ascending indices into the tab projection's note order.

    Silently-held stops are among them with no case of their own — a typed digit states a stop, and
    a hold's stop is a fret like any other — so the surface draws the pending box on whichever face
    the note has: its head, or its posture bracket.
    */
    std::vector<std::size_t> notes{};

    /*!
    \brief WHICH stop of those notes the entry states — and therefore where its box draws.

    One channel for the whole entry, fixed when it opened: on the sounding channel the box rides
    each affected note's own face, and on the held one it rides the satellite digit outboard of
    that note's posture bracket, which is the mark the value will land in. Carried rather than
    re-derived because the entry is what decided it — the caret's stop when the digits began — and a
    surface guessing from the note would show the box on the wrong mark for a note that states both.
    */
    common::core::ChartStopChannel channel{common::core::ChartStopChannel::Sounding};

    /*!
    \brief Compares two target sets by their stored values.
    \param lhs Left-hand targets.
    \param rhs Right-hand targets.
    \return True when both name the same objects.
    */
    friend bool operator==(const ChartPendingFretTargets& lhs, const ChartPendingFretTargets& rhs) =
        default;
};

/*!
\brief The in-flight pending fret entry's rendered state.

While a typed value is provisional the lane draws an entry box over each affected head — the
plate the mute heads already draw, with the editor accent as a border so pending reads as an
editor state — carrying the typed text: the ordinary digit ink while the value would apply, red
when it cannot. A selected silent hold gets the same box on its posture bracket, which is where
its stop prints once the entry settles. Red marks EVERY affected object, deliberately without
per-object attribution: relational refusals are properties of the whole selection, so a per-note
red would claim a precision the refusal does not have. For an entry that began on an empty caret
the box draws at the insert slot, where no head exists yet. Present exactly while a value is
provisional — the box disappearing IS the settle becoming visible.
*/
struct ChartPendingFretViewState
{
    /*!
    \brief Where the box draws: over every affected object (a retype entry) or at the empty slot
    an insert entry began on.

    One alternative or the other, never both and never neither: the entry itself began either on
    the selection or on an empty caret, and the two cases carry different data.
    */
    std::variant<ChartPendingFretTargets, ChartSlotViewState> at{};

    /*! \brief The provisional value exactly as typed. */
    std::string text{};

    /*! \brief False when the value cannot apply — the box draws its text red. */
    bool valid{true};

    /*!
    \brief Compares two pending-entry states by their stored values.

    Defaulted on purpose: the one floating member is reached through the slot alternative's own
    comparison, so the float-equal warning cannot fire here (the ChartNote precedent in
    coding-conventions.md).

    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(
        const ChartPendingFretViewState& lhs, const ChartPendingFretViewState& rhs) = default;
};

/*!
\brief One selected waypoint, located in the tab projection: which note, and which of its marks.

A waypoint needs two indices where a note needs one, because it belongs to a note rather than to a
flat array — the same shape its selection identity has, published as drawn positions instead of as
chart identity. The second index addresses \ref common::core::NoteViewState::slides, which holds
only the waypoints the lane actually draws.
*/
struct ChartWaypointRef
{
    /*! \brief Index into the tab projection's note order. */
    std::size_t note_index{0};

    /*! \brief Index into that note's drawn waypoints. */
    std::size_t waypoint_index{0};

    /*!
    \brief Compares two located waypoints by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both name the same drawn waypoint.
    */
    friend constexpr bool operator==(
        const ChartWaypointRef& lhs, const ChartWaypointRef& rhs) noexcept = default;
};

/*!
\brief Chart-editing selection state rendered as overlays above the tablature notation.

Selected notes are indices into the current tab projection's note order (which matches the
chart's note order one to one), valid exactly as long as the \ref EditorViewState::tab instance
they were derived with; the controller rebuilds both together.
*/
struct ChartEditViewState
{
    /*! \brief Ascending indices of selected notes in the tab projection's note order. */
    std::vector<std::size_t> selected_notes{};

    /*!
    \brief The selected waypoints as drawn positions, in the selection's own order.

    Published beside the two index lists rather than folded into them because a waypoint is not
    addressable the way they are (\ref ChartWaypointRef). Keys naming a waypoint that no longer
    draws — one an edit dissolved, one the presentation trim clipped — simply do not appear, the
    same resolve-or-drop rule the note indices follow.
    */
    std::vector<ChartWaypointRef> selected_waypoints{};

    /*! \brief In-flight marquee rectangle, while an empty-lane drag is selecting. */
    std::optional<ChartMarqueeViewState> marquee{};

    /*!
    \brief The armed caret, present exactly while the position marker is armed.

    Rendered as a white rounded square at the caret's slot — on an empty slot it marks where a
    typed digit inserts; on a note it rides the note's selection highlight so the caret stays
    visible through a single selection. Its presence also positions the ruler's always-shown
    play-from-here mark at the caret and re-centers wheel zoom there; while passive (absent)
    both fall back to the transport position. Absent without a chart.
    */
    std::optional<ChartCaretViewState> caret{};

    /*!
    \brief The Alt-held insert ghost, present while Alt hovers an insertable empty slot.

    Rendered as a hollow white ring where an Alt+click would plant a fret-0 note — distinct from
    the caret's square so the two furniture kinds never read as one. Absent whenever an Alt+click
    would not insert (no Alt, over a note, or while playing), so the ring never lies.
    */
    std::optional<ChartSlotViewState> insert_ghost{};

    /*! \brief The pending fret entry, present exactly while a typed value is provisional. */
    std::optional<ChartPendingFretViewState> pending_fret{};

    /*!
    \brief Compares two chart-editing states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartEditViewState& lhs, const ChartEditViewState& rhs) = default;
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
    std::vector<SongSectionViewState> sections{};

    /*!
    \brief Grid step as a fraction of a whole note, shared by the track grid, ruler, and snapping.

    A 1/8 grid means eighth notes in every meter. Initialized to the editor default because the
    Fraction default of 0/1 is a degenerate step.
    */
    common::core::Fraction grid_note_value{g_default_tempo_grid_note_value};

    /*!
    \brief Whether grid snap is on, which is what decides the placement quantum.

    A session fact, never persisted and reset to true at every project boundary. The view derives
    the quantum from this and grid_note_value through the one authority
    (\ref placementQuantumNoteValue) rather than snapping by its own rule, and quiets the surfaces
    that say "grid" while it is false.
    */
    bool grid_snap{true};

    /*!
    \brief True while the warning that precedes turning grid snap off should be shown.

    Raised by a toggle that would turn snapping OFF, in place of flipping the switch; a toggle that
    turns snapping back on never raises it. The view presents the warning and answers through
    IEditorController::onGridSnapWarningDecision, which is the only path that can turn snapping
    off. Nothing suppresses it: the warning is deliberately unconditional, matching a mode that
    stores nothing anywhere and is meant to be entered one deliberate time at a time.
    */
    bool grid_snap_warning_prompt{false};

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
    std::shared_ptr<const common::core::ChartViewState> tab{};

    /*!
    \brief The same chart in \ref common::core::ChartNoteForm::Actual: every note at its real ring.

    Where the lane draws a note's real ring it draws it from here: the notation itself is swapped
    rather than annotated, so this carries the tails a chug or a trimmed sustain really rings for,
    with the payload the presentation rules clipped off with the tail — which no view-side end swap
    could put back. WHICH notes take this form is the lane's own per-note pick (see
    `TabView::setActualRingReveal`) — every visible note while Alt is held, and any selected note
    otherwise — and nothing here needs to know: the two forms align by index, so the view reads one
    or the other per note. Rebuilt and shared under exactly the rule \ref tab is, and null in
    exactly the same cases.

    It is not hit-testable and never scored: pointer resolution, selection and Alt+click insert all
    read \ref tab, and no game surface can obtain this form at all
    (`docs/plans/in-progress/note-sustain-model.md`, ruling 4).
    */
    std::shared_ptr<const common::core::ChartViewState> tab_actual{};

    /*! \brief Chart-editing selection and marquee overlays for the tablature lane. */
    ChartEditViewState chart_edit{};

    /*!
    \brief The grid-locked time-selection span resolved to seconds; absent when none is held.

    A full-height range across every surface (chart, tone, lanes) — a mutually-exclusive kind of
    the one editor-wide selection (decision D). The endpoints are display-grid positions resolved to
    seconds so the full-canvas overlay maps them to pixels the same way it maps the cursor,
    surviving zoom and scroll without a re-push. Present implies selection_present.
    */
    std::optional<common::core::TimeRange> time_selection{};

    /*!
    \brief True when the one editor-wide selection resolves to something published.

    Derived from the published per-surface states (selected chart notes, a selected tone
    region, a resolved automation point, or a time-selection span), so a stale selection reads as
    absent exactly as it renders. The view's Delete guard reads this single flag instead of
    re-deriving the union — an idle Delete keeps propagating to other key consumers.
    */
    bool selection_present{false};

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

    // Deliberately NOT comparable. timeline_zoom_pixels_per_second is a double of this struct's
    // own, so a defaulted operator== trips -Wfloat-equal on GCC, Clang, and clang-cl the day a
    // whole-state comparison is first odr-used — on a line nobody edited. Nothing compares whole
    // push payloads: views gate on the sub-states they consume, which carry their own warning-free
    // comparisons, and that is where any new gate belongs.
};

} // namespace rock_hero::editor::core
