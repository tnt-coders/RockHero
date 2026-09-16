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
    \brief Compares two caret states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartCaretViewState& lhs, const ChartCaretViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.string == rhs.string &&
               lhs.channel == rhs.channel;
    }
};

/*!
\brief Where the keyboard stands, for the view to keep in sight after every command.

The keyboard acts at its focus, so the focus must be visible once a command that acts there has
run: the view glides this anchor's measure into view after such a command
(\ref EditorView::perform), and never on a state push alone, so a click — which creates a focus
rather than acting under one —
never scrolls away from what was clicked. The anchor is the armed caret's slot; else the earliest
selected chart object or the selected automation point (a selection made without a caret is where
the selection verbs act, wherever the cursor was left); else, on a row reached by selection, the
column the keyboard stands at within the selected marker (the paused cursor while it lies inside
the marker's span, otherwise the marker's start, so acting on a chip clicked far from the cursor
reveals the chip); else the paused cursor. The view also zooms around it. Absent while the
transport plays, when playback follow owns the view. Carries its measure's span so the view never
re-derives measure bounds from the tempo map.
*/
struct FocusAnchorViewState
{
    /*! \brief Anchor position in seconds on the arrangement timeline. */
    double seconds{};

    /*! \brief Start of the anchor's measure in seconds. */
    double measure_start_seconds{};

    /*! \brief End of the anchor's measure (the next measure's start) in seconds. */
    double measure_end_seconds{};
};

/*!
\brief The verb that renames a section: the one starting at a position, under its current name.

Published as a marker chord's or a selection verb's answer
(\ref EditorViewState::section_chord_target, \ref EditorViewState::restate_target,
\ref EditorViewState::rename_target); the view opens the rename prompt this names and nothing else.
*/
struct RenameSectionTarget
{
    /*! \brief Measure downbeat the section starts on; the rename addresses it by this. */
    common::core::GridPosition position{};

    /*! \brief The section's current name, the prompt's starting text. */
    std::string name{};

    /*! \brief Compares two targets field by field. */
    bool operator==(const RenameSectionTarget&) const = default;
};

/*!
\brief The verb that inserts a section at a downbeat the core has already found free and able to
carry one.
*/
struct InsertSectionTarget
{
    /*! \brief Measure downbeat the new section will start on. */
    common::core::GridPosition downbeat{};

    /*! \brief Compares two targets field by field. */
    bool operator==(const InsertSectionTarget&) const = default;
};

/*!
\brief What `Ctrl+M` would do right now: rename the section standing exactly at the cursor's
downbeat, insert one there, or nothing.

THE section chord's answer, derived once in the core from the cursor — the armed caret, else the
paused cursor — and never from the selection. Nothing while the transport plays, with no song, or
where a section cannot stand (the terminal downbeat), so the chord is inert exactly where the
commit would refuse it, and the view keeps no gate of its own.
*/
using SectionChordTarget = std::variant<std::monostate, RenameSectionTarget, InsertSectionTarget>;

/*!
\brief The verb that repoints a tone region at another tone: the region, and the tone it sounds
now, which the picker leaves out because choosing it would change nothing.
*/
struct RetoneRegionTarget
{
    /*! \brief Stable id of the region to repoint. */
    std::string region_id{};

    /*! \brief Tone document the region references now; empty for the synthesized default. */
    std::string tone_document_ref{};

    /*! \brief Compares two targets field by field. */
    bool operator==(const RetoneRegionTarget&) const = default;
};

/*!
\brief The verb that splits a tone region at a position strictly inside it: the position, and the
tone on both sides of it now, which the picker leaves out.
*/
struct SplitToneRegionTarget
{
    /*! \brief Exact position the new tone change will stand on. */
    common::core::GridPosition position{};

    /*! \brief Tone document the containing region references; the new region cannot keep it. */
    std::string containing_tone_document_ref{};

    /*! \brief Compares two targets field by field. */
    bool operator==(const SplitToneRegionTarget&) const = default;
};

/*!
\brief What `Ctrl+T` would do right now: retone the region starting exactly at the cursor, split
the region the cursor stands inside, or nothing.

The tone chord's answer, the twin of \ref SectionChordTarget: derived from the cursor at the
placement quantum, nothing while playing, with no song, or on the terminal position.
*/
using ToneChordTarget = std::variant<std::monostate, RetoneRegionTarget, SplitToneRegionTarget>;

/*!
\brief The verb that renames a tone document: the document, under its current name.
*/
struct RenameToneTarget
{
    /*! \brief Package-relative tone document to rename. */
    std::string tone_document_ref{};

    /*! \brief The tone's current name, the prompt's starting text. */
    std::string name{};

    /*! \brief Compares two targets field by field. */
    bool operator==(const RenameToneTarget&) const = default;
};

/*! \brief The verb that opens the automation parameter picker the "+" row offers. */
struct OpenAutomationPickerTarget
{
    /*! \brief Two picker targets are always the same verb. */
    bool operator==(const OpenAutomationPickerTarget&) const = default;
};

/*!
\brief What `Enter` would do to the selection right now: rename a selected section, retone a
selected tone region, open the "+" row's picker, or nothing.

The selection's own verb, dispatched on its kind here rather than in the view: restating a section
is renaming it, restating a tone region is repointing it. A retone stays `Enter`'s meaning on a
region until the signal chain has a keyboard model to drill into (plan 53 Phase 5).
*/
using RestateTarget = std::variant<
    std::monostate, RenameSectionTarget, RetoneRegionTarget, OpenAutomationPickerTarget>;

/*!
\brief What `Ctrl+R` would do to the selection right now: rename a selected section, rename a
selected tone region's TONE, or nothing.

Nothing for every kind with no name of its own — tempo anchors, time signatures, the "+" row,
automation points, notes — and for a region on the synthesized default tone, which has no document
to name.
*/
using RenameTarget = std::variant<std::monostate, RenameSectionTarget, RenameToneTarget>;

/*!
\brief A grid slot resolved for drawing: where a typed value would land.

One overlay draws at it — an insert entry's pending fret box (\ref ChartPendingFretViewState),
which rides the slot rather than a head so it can still say what the projected mark cannot: a value
the plan refuses, or one landing where a head already stands. Stored in seconds like the caret so
the lane maps it through the same visible-timeline convention.
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
\brief One selected keyframe, located in the tab projection: which note, and which of its marks.

A keyframe needs two indices where a note needs one, because it belongs to a note rather than to a
flat array — the same shape its selection identity has, published as drawn positions instead of as
chart identity. The second index addresses \ref common::core::NoteViewState::slides, which holds
only the keyframes the lane actually draws.
*/
struct ChartKeyframeRef
{
    /*! \brief Index into the tab projection's note order. */
    std::size_t note_index{0};

    /*! \brief Index into that note's drawn keyframes. */
    std::size_t keyframe_index{0};

    /*!
    \brief Compares two located keyframes by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both name the same drawn keyframe.
    */
    friend constexpr bool operator==(
        const ChartKeyframeRef& lhs, const ChartKeyframeRef& rhs) noexcept = default;
};

/*!
\brief Where a retype entry's boxes draw: every selected stop the typed value would write.

A named alternative rather than a bare index list, because this is one arm of the pending entry's
sum and the other names a SLOT. Never empty of both kinds: an entry with no target is not a retype
entry at all.
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
    \brief The selected keyframes the value would write, located as the lane draws them.

    A keyframe's stop is a fret like a head's, typed through the same entry, so its box rides the
    linked head the lane paints at the junction — the same drawn-position addressing the selection
    ring uses (\ref ChartKeyframeRef), which is what lets a keyframe the trim clipped out of the
    tail simply wear no box, as it wears no ring.
    */
    std::vector<ChartKeyframeRef> keyframes{};

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
red would claim a precision the refusal does not have.

An entry that would CREATE something — a note at an empty caret, a point on a tail — wears its box
at the slot it began on, and a value that would apply is already drawn beneath the box as the head
or point it creates: the controller projects the plan into the published chart without storing
it. The box is therefore the ONE thing that says "provisional" for every entry kind, and its
disappearance is the settle becoming visible.
*/
struct ChartPendingFretViewState
{
    /*!
    \brief Where the box draws: over every affected object (a retype entry) or at the slot a
    create entry began on.

    One alternative or the other, never both and never neither: the entry itself began either on
    the selection or on the caret, and the two cases carry different data.
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
\brief One note under the live harmonic picker: the nodes its fret names, and which one is armed.

The whole of what the picker draws at one head. The armed node rides the accent-bordered pending
box in the form it will COMMIT — the diamond silhouette and the node label the committed head
prints — and the rest read outboard on the same baseline in the muted ink, because an unchosen row
is fully choosable and must never take dimming's "unavailable" signal.

Positions rather than text, so the surface prints them through the one label authority every other
stop on the lane goes through (\ref common::core::chartStopText) and the pending head can never
round differently from the committed one.
*/
struct ChartPendingHarmonicNode
{
    /*! \brief Index into the tab projection's note order. */
    std::size_t note{};

    /*!
    \brief The nodes this note's own fret names, ascending, in absolute fret units.

    This NOTE's, not the ladder's: the choice is shared across the scope, but where each node lands
    is the note's own stop plus the chosen offset, so a chord across two stops prints two numbers
    for one partial. Never empty — a note whose fret names nothing is not under the picker at all.
    */
    std::vector<double> nodes{};

    /*! \brief Index into \ref nodes of the armed one, which the settle would commit. */
    std::size_t chosen{};

    /*!
    \brief Compares two pending harmonic heads by their stored values.

    Defaulted on purpose: the floating values are reached through `std::vector<double>`, where the
    compare happens inside the standard library and the float-equal diagnostic does not reach — the
    case docs/design/coding-conventions.md names safe.

    \param lhs Left-hand head.
    \param rhs Right-hand head.
    \return True when both heads store equal values.
    */
    friend bool operator==(
        const ChartPendingHarmonicNode& lhs, const ChartPendingHarmonicNode& rhs) = default;
};

/*!
\brief The in-flight harmonic picker's rendered state: every head it is offering a node for.

Present exactly while the picker is armed, which is exactly while some note in the scope has a
typed fret naming two nodes. A press over an unambiguous scope commits in the same keystroke and
publishes nothing, so this state existing at all IS the ambiguity.
*/
struct ChartPendingHarmonicViewState
{
    /*! \brief The affected heads, ascending by projection index; never empty. */
    std::vector<ChartPendingHarmonicNode> notes{};

    /*!
    \brief Compares two picker states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(
        const ChartPendingHarmonicViewState& lhs,
        const ChartPendingHarmonicViewState& rhs) = default;
};

/*!
\brief One row of the harmonic picker's mouse form: a node to choose, and the partial naming it.

The ORDINAL is what the rows differ by, and the only stable name for a choice: our frets are
absolute where published tab is capo-relative, so under a capo of 2 the same two rows read "5.2"
and "4.7" while the partials stay the 6th and the 7th. Sounding pitch is deliberately not offered —
a partial is just-intoned (the 7th sits 31 cents below any equal-tempered name), so a pitch letter
beside 2.7 would be false.
*/
struct ChartHarmonicNodeChoice
{
    /*! \brief The node this row states, in absolute fret units on the note it was read from. */
    double node{};

    /*! \brief The partial that sounds there. */
    int partial{};

    /*!
    \brief Compares two choices by their stored values.

    Written out rather than defaulted: the node is a bare `double` here, and a defaulted comparison
    over one trips `-Wfloat-equal` on the CI compilers. Exactness is what is wanted — two rows are
    the same row only when they name the same node — so the spaceship result is asked directly, the
    form docs/design/coding-conventions.md states for it.

    \param lhs Left-hand choice.
    \param rhs Right-hand choice.
    \return True when both name the same node and partial.
    */
    friend bool operator==(const ChartHarmonicNodeChoice& lhs, const ChartHarmonicNodeChoice& rhs)
    {
        return std::is_eq(lhs.node <=> rhs.node) && lhs.partial == rhs.partial;
    }
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
    \brief The selected keyframes as drawn positions, in the selection's own order.

    Published beside the two index lists rather than folded into them because a keyframe is not
    addressable the way they are (\ref ChartKeyframeRef). Keys naming a keyframe that no longer
    draws — one an edit dissolved, one the presentation trim clipped — simply do not appear, the
    same resolve-or-drop rule the note indices follow.
    */
    std::vector<ChartKeyframeRef> selected_keyframes{};

    /*! \brief In-flight marquee rectangle, while an empty-lane drag is selecting. */
    std::optional<ChartMarqueeViewState> marquee{};

    /*!
    \brief The armed caret, present exactly while the position marker is armed.

    Rendered as a white rounded square at the caret's slot — on an empty slot it marks where a
    typed digit inserts; on a note or a keyframe it rides that object's selection highlight so the
    caret stays visible through a single selection. Its presence also positions the ruler's
    always-shown play-from-here mark at the caret and re-centers wheel zoom there; while passive
    (absent) both fall back to the transport position. Absent without a chart.
    */
    std::optional<ChartCaretViewState> caret{};

    /*! \brief The pending fret entry, present exactly while a typed value is provisional. */
    std::optional<ChartPendingFretViewState> pending_fret{};

    /*!
    \brief The pending harmonic-node entry, present exactly while the picker is armed.

    Beside \ref pending_fret rather than inside it because the two carry different quantities: a
    typed value is one string over every affected object, while a node is a POSITION resolved
    against each note's own stop, so the same choice prints "3.2" on an open string and "8.2" on a
    note held at 5. Never both at once — one pending entry is live at a time — and both draw
    through the same box the paint core exports.
    */
    std::optional<ChartPendingHarmonicViewState> pending_harmonic{};

    /*!
    \brief The harmonic-node choices a menu can offer over the current selection.

    The picker's MOUSE form (\ref IEditorController::onChartHarmonicNodeRequested). Empty unless
    the selection holds a note whose typed fret names more than one node, which is the same
    ambiguity test the keyboard picker arms on. Ascending by position, exactly as the ladder is.
    */
    std::vector<ChartHarmonicNodeChoice> harmonic_node_choices{};

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

    /*! \brief Enables or disables the File > Export Song command. */
    bool export_enabled{false};

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

    /*! \brief Suggested .rock destination used to pre-fill the export chooser. */
    std::filesystem::path suggested_export_file{};

    /*! \brief Enables or disables the File > Close command. */
    bool close_enabled{false};

    /*!
    \brief One published answer for the whole marker plane: may a marker be selected or edited now?

    Derived as the availability of \ref EditorActionId::SelectSongSection, the marker verb with the
    WEAKEST base condition — a project, and a paused transport — so it answers for every marker kind
    (sections, tempo anchors, time signatures, tone regions, automation points) and for the "+" row
    at once. Every marker-row surface greys its affordances and refuses its gestures from this flag
    alone: no view derives marker enablement itself, and none reads the transport to decide it.

    It is not the whole availability answer for any one verb — a tone-region verb also needs a
    loaded arrangement, a lane point also needs its lane — so the core still refuses each verb on
    its own conditions. This says only "the marker plane is open", which is the question an
    affordance asks.
    */
    bool marker_edits_enabled{false};

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
    \brief The beat whose tempo chip is selected on the ruler, or nothing.

    Published as the anchor's beat because the ruler draws its tempo chips straight from
    \ref tempo_map, one per non-terminal anchor, so the beat is how it finds the chip.
    */
    std::optional<common::core::GridPosition> selected_tempo_anchor{};

    /*!
    \brief The measure whose time-signature chip is selected on the ruler, or nothing.
    */
    std::optional<int> selected_time_signature_measure{};

    /*!
    \brief Where the keyboard stands (\ref FocusAnchorViewState), or nothing while the transport
    plays or no arrangement is loaded.
    */
    std::optional<FocusAnchorViewState> focus_anchor{};

    /*!
    \brief What the section chord (`Ctrl+M`) would do at the cursor right now.

    The marker grammar, published as the VERB rather than as a position for the view to reason
    about: the core reads the cursor — the armed caret, else the paused cursor's tick, snapped to
    its measure's downbeat — and answers rename-this, insert-here or nothing. The view opens the
    prompt the verb names; it never looks up a section itself, and it keeps no playback or no-song
    gate of its own, since "nothing" is that gate.
    */
    SectionChordTarget section_chord_target{};

    /*!
    \brief What the tone chord (`Ctrl+T`) would do at the cursor right now.

    The tone twin of \ref section_chord_target, read at the placement quantum: retone the region
    starting exactly at the cursor, split the region the cursor stands inside, or nothing.
    */
    ToneChordTarget tone_chord_target{};

    /*!
    \brief What `Enter` would do to the selection right now.

    The selection's own verb, so the view dispatches on no kind of its own: the core names the
    rename, the retone or the picker, or nothing when nothing selected has a restate.
    */
    RestateTarget restate_target{};

    /*!
    \brief What `Ctrl+R` would do to the selection right now.

    Rename the selected section, rename the selected region's tone, or nothing for every kind
    without a name of its own.
    */
    RenameTarget rename_target{};

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

    It is not hit-testable and never scored: pointer resolution and selection both read \ref tab,
    and no game surface can obtain this form at all
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
