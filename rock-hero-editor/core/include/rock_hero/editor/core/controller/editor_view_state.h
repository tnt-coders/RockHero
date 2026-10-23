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
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/highway/highway_player_settings.h>
#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/busy/busy_view_state.h>
#include <rock_hero/editor/core/controller/editor_action_id.h>
#include <rock_hero/editor/core/signal_chain/plugin_browser_view_state.h>
#include <rock_hero/editor/core/signal_chain/signal_chain_view_state.h>
#include <rock_hero/editor/core/timeline/arrangement_view_state.h>
#include <rock_hero/editor/core/timeline/section_view_state.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
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

/*! \brief User choice returned from the notice that the audio device is not running. */
enum class AudioDeviceLostDecision : std::uint8_t
{
    /*! \brief Open the audio settings window to choose or reopen a device. */
    OpenSettings,

    /*! \brief Close the notice and keep working on the silent device. */
    Close,
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
\brief Describes the notice that the user's audio device is not running: the saved device did not
open at startup, or the open device was lost.
*/
struct AudioDeviceLostPrompt
{
    /*! \brief The engine's reason, such as the backend's open failure or "Disconnected". */
    std::string reason;

    /*!
    \brief Compares two device-lost notices by their reasons.
    \param lhs Left-hand notice.
    \param rhs Right-hand notice.
    \return True when both notices carry the same reason.
    */
    friend bool operator==(const AudioDeviceLostPrompt& lhs, const AudioDeviceLostPrompt& rhs) =
        default;
};

/*! \brief Describes an active input calibration prompt requested by the controller. */
struct InputCalibrationPrompt
{
    /*! \brief The input route the prompt calibrates; a new route is a new prompt. */
    common::audio::InputDeviceIdentity route;

    /*! \brief The route's stored calibration gain, or empty while it is uncalibrated. */
    std::optional<double> stored_gain_db;

    /*!
    \brief Compares two prompt requests by the route they calibrate, their identity.

    The stored gain is excluded on purpose: the open popup owns its own gain once it opens, and
    only a new route re-presents it.

    \param lhs Left-hand prompt request.
    \param rhs Right-hand prompt request.
    \return True when both prompt requests calibrate the same route.
    */
    friend bool operator==(const InputCalibrationPrompt& lhs, const InputCalibrationPrompt& rhs)
    {
        return lhs.route == rhs.route;
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
\brief WHICH face of the object at its slot the armed caret stands on.

The caret addresses a slot, and an object there can wear more than one face the charter works on
alone. The MARK is the face every object has: a head, a keyframe's mark, where the digits state the
stop that sounds and `Delete` takes the object (or a keyframe's fret). An object stating a bend
wears the BEND CHIP printing it, where `Delete` takes the bend and `Enter` opens the bend picker;
digits there type into the mark.

A face is reachable only while it is drawn, so an edit that takes it away leaves the caret on the
mark.
*/
enum class ChartCaretFace : std::uint8_t
{
    /*! \brief The face every object has: its head, or a keyframe's mark. */
    Mark,

    /*! \brief The chip printing the object's bend. */
    BendChip,
};

/*!
\brief The armed caret's rendered position while it sits on an empty grid slot.

While the marker is armed the caret is THE paused position — typing inserts here, play starts
here. Published in seconds so the lane maps it through the same visible-timeline convention as the
notation.
*/
struct ChartCaretViewState
{
    /*! \brief Caret position in seconds on the arrangement timeline. */
    double seconds{};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*!
    \brief WHICH face of the object here the caret stands on (\ref ChartCaretFace).

    The surface reads THIS rather than re-deriving it from the note, because the controller is what
    decided the caret could be there at all: the square stays on the slot on every face, and the
    selection ring moves onto the face — the bend chip — in place of the head. A face is published
    only where it is drawn, which is the same invariant the caret itself holds.
    */
    ChartCaretFace face{ChartCaretFace::Mark};

    /*!
    \brief Compares two caret states by their stored values.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartCaretViewState& lhs, const ChartCaretViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.string == rhs.string &&
               lhs.face == rhs.face;
    }
};

/*!
\brief Where the keyboard stands on the timeline, with the measure it stands in.

The armed caret's slot; else the moving edge of a time selection; else the paused cursor. The
view keeps this position's measure in view: whenever the core counts a move
(\ref EditorViewState::keyboard_move_count) the window fits the whole measure it landed in, the
minimal shift in either direction, as Guitar Pro does. Absent while the transport plays, when
playback follow owns the view, and with no arrangement. Carries the measure's span so the view
never re-derives measure bounds from the tempo map.
*/
struct KeyboardPositionViewState
{
    /*! \brief Position in seconds on the arrangement timeline. */
    double seconds{};

    /*! \brief Start of the position's measure in seconds. */
    double measure_start_seconds{};

    /*! \brief End of the position's measure (the next measure's start) in seconds. */
    double measure_end_seconds{};

    /*!
    \brief Compares two positions by their stored values.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return True when both store equal values.
    */
    friend bool operator==(
        const KeyboardPositionViewState& lhs, const KeyboardPositionViewState& rhs)
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.measure_start_seconds <=> rhs.measure_start_seconds) &&
               std::is_eq(lhs.measure_end_seconds <=> rhs.measure_end_seconds);
    }
};

/*!
\brief The verb that renames a section: the one starting at a position, under its current name.

The prompt the section chord, `Enter` and `Ctrl+R` ask for on a section (\ref MarkerPrompt); the
view opens the rename prompt this names and nothing else.
*/
struct RenameSectionTarget
{
    /*! \brief Position the section starts on; the rename addresses it by this. */
    common::core::GridPosition position{};

    /*! \brief The section's current name, the prompt's starting text. */
    std::string name{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same section under the same name.
    */
    bool operator==(const RenameSectionTarget&) const = default;
};

/*!
\brief The verb that inserts a section at a slot the core has already found free and able to carry
one.
*/
struct InsertSectionTarget
{
    /*! \brief Grid position the new section will start on. */
    common::core::GridPosition position{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same position.
    */
    bool operator==(const InsertSectionTarget&) const = default;
};

/*!
\brief The verb that types the tempo of the span a tempo anchor opens.

The prompt the tempo chord and `Enter` ask for on an anchor (\ref MarkerPrompt); the view opens
the BPM prompt this names and nothing else.
*/
struct SetTempoTarget
{
    /*! \brief Grid position the anchor stands at; the typed tempo addresses its span by this. */
    common::core::GridPosition position{};

    /*! \brief The span's tempo as its chip prints it, the prompt's starting text. */
    std::string bpm_text{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same anchor under the same printed tempo.
    */
    bool operator==(const SetTempoTarget&) const = default;
};

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

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same region and current tone.
    */
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

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same position and containing tone.
    */
    bool operator==(const SplitToneRegionTarget&) const = default;
};

/*!
\brief The verb that renames a tone document: the document, under its current name.
*/
struct RenameToneTarget
{
    /*! \brief Package-relative tone document to rename. */
    std::string tone_document_ref{};

    /*! \brief The tone's current name, the prompt's starting text. */
    std::string name{};

    /*!
    \brief Compares two targets field by field.
    \return True when both name the same tone document under the same name.
    */
    bool operator==(const RenameToneTarget&) const = default;
};

/*!
\brief Scale Tempo's factor prompt, titled by the form the Note Lock chooses (\ref scaleTempoLabel),
so the charter reads before typing whether the notes keep their time or follow the beat.
*/
struct ScaleTempoTarget
{
    /*! \brief The prompt's title: the undo label of the form that will run. */
    std::string label{};

    /*!
    \brief Compares two targets field by field.
    \return True when both carry the same title.
    */
    bool operator==(const ScaleTempoTarget&) const = default;
};

/*! \brief The verb that opens the automation parameter picker the "+" row offers. */
struct OpenAutomationPickerTarget
{
    /*!
    \brief Compares two picker targets, which are always the same verb.
    \return Always true.
    */
    bool operator==(const OpenAutomationPickerTarget&) const = default;
};

/*!
\brief A prompt or picker a marker verb asks the view to open (\ref IEditorView::showMarkerPrompt):
rename or insert a section, type a span's tempo, restate or insert a meter, retone or split a tone
region, rename a tone, type Scale Tempo's factor, or open the "+" row's parameter picker.

Every marker verb decides at the press, in the core, exactly as the hold picker is decided: the
chords read the cursor, the menu rows the place clicked, and the selection verbs (`Enter`,
`Ctrl+R`) the selection; the lock governing the verb answers first, the precedence is applied
there, and only the prompt it lands on reaches the view, which looks nothing up. A verb
that needs no prompt (a tempo anchor inserted at a free beat) acts without one, one whose picker
has a request of its own (`Enter` on a bend chip, which opens the bend picker) makes that request,
and one with nothing to do sends nothing.
*/
using MarkerPrompt = std::variant<
    RenameSectionTarget, InsertSectionTarget, SetTempoTarget, SetTimeSignatureTarget,
    InsertTimeSignatureTarget, RetoneRegionTarget, SplitToneRegionTarget, RenameToneTarget,
    ScaleTempoTarget, OpenAutomationPickerTarget>;

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
chart identity. The second index addresses \ref common::core::NoteViewState::keyframes, which holds
every keyframe the note stores; whether its mark is drawn is the lane's extent to decide.
*/
struct ChartKeyframeRef
{
    /*! \brief Index into the tab projection's note order. */
    std::size_t note_index{0};

    /*! \brief Index into that note's keyframes. */
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
    /*! \brief Ascending indices into the tab projection's note order. */
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
    \brief Compares two target sets by their stored values.
    \param lhs Left-hand targets.
    \param rhs Right-hand targets.
    \return True when both name the same objects.
    */
    friend bool operator==(const ChartPendingFretTargets& lhs, const ChartPendingFretTargets& rhs) =
        default;
};

/*!
\brief Where a fret-hand placement's fret entry draws its box: filling the placement's lane chip.

The chip is the placement's one mark on the lane, and its fret is the value the chip prints, so the
box fills the chip's own box rather than a head's plate or a slot. A value that would apply is
previewed into the published chart like a creating entry's product, so the chip already prints what
the settle will leave and the box carries that; a refused value previews nothing, so the box
carries the chip the typed value WOULD have made — printed by the chip's own notation, so a refused
end on a start of 5 reads "5-3" rather than a bare "3" that could be either half.
*/
struct ChartPendingFretHandPosition
{
    /*! \brief Index of the placement in the tab projection's placement order. */
    std::size_t index{};

    /*!
    \brief The placement's chip as the typed value would leave it: its start fret, its width, and
    whether its end is authored. Read only while the value is refused.
    */
    common::core::FhpViewState typed{};

    /*!
    \brief Compares two targets field by field.
    \param other Target to compare with.
    \return True when both name the same placement.
    */
    bool operator==(const ChartPendingFretHandPosition& other) const = default;
};

/*!
\brief The in-flight pending fret entry's rendered state.

While a typed value is provisional the lane draws an entry box over each affected head — the
plate the mute heads already draw, with the editor accent as a border so pending reads as an
editor state — carrying the typed text: the ordinary digit ink while the value would apply, red
when it cannot. Red marks EVERY affected object, deliberately without per-object attribution:
relational refusals are properties of the whole selection, so a per-note red would claim a
precision the refusal does not have.

An entry that would CREATE something — a note at an empty caret, a point on a tail — wears its box
at the slot it began on, and a value that would apply is already drawn beneath the box as the head
or point it creates: the controller projects the plan into the published chart without storing
it. A placement's fret is previewed the same way, and its box fills the placement's chip. The box
is therefore the ONE thing that says "provisional" for every entry kind, and its disappearance is
the settle becoming visible.
*/
struct ChartPendingFretViewState
{
    /*!
    \brief Where the box draws: over every affected object (a retype entry), at the slot a create
    entry began on, or on the chip of a selected fret-hand placement whose fret is being typed.

    Exactly one alternative: the entry began on the chart selection, on the caret, or on a selected
    placement, and the three cases carry different data.
    */
    std::variant<ChartPendingFretTargets, ChartSlotViewState, ChartPendingFretHandPosition> at{};

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
\brief One row of the harmonic node picker the controller asks the view to show
(\ref IEditorView::showChartHarmonicNodePicker): a node to choose, and the partial naming it.

The ORDINAL is what the rows differ by, and the only stable name for a choice: our frets are
absolute where published tab is capo-relative, so under a capo of 2 the same rows read "5.2" and
"4.7" for the 6th and the 7th. Sounding pitch is deliberately not offered — a partial is
just-intoned (the 7th sits 31 cents below any equal-tempered name), so a pitch letter beside 2.7
would be false.
*/
struct ChartHarmonicNodeChoice
{
    /*! \brief The node this row states, in absolute fret units on the note it was read from. */
    double node{};

    /*! \brief The partial that sounds there. */
    int partial{};

    /*!
    \brief True when the note the rows describe already touches this node, so the row is drawn
    ticked: the menu shows where the finger is before it is moved.
    */
    bool current{};
};

/*!
\brief The "No harmonic" row of the harmonic node picker: choosing it clears every fret-hand
harmonic the selection carries.

Carries nothing; it is one alternative of \ref ChartHarmonicChoice so that a picker is one list of
rows, each an answer, and "a clear row that was never offered" cannot be preselected or chosen.
*/
struct ChartHarmonicClearChoice
{
};

/*! \brief One row of the harmonic node picker: a node to move to, or no harmonic at all. */
using ChartHarmonicChoice = std::variant<ChartHarmonicNodeChoice, ChartHarmonicClearChoice>;

/*!
\brief The harmonic node picker the controller asks the view to show: which note the rows were
read from, the rows, and which row Return takes.

The note is named so the view can anchor the popup on the head the numbers describe. The node rows
come from the selected member whose label names the most nodes (the first such, in chart order),
and that member need not be the earliest selected note — a chord of a 7 and a 5 offers the 5's rows
— so "the selected head" would put the menu over a note the rows have nothing to do with. The clear
row, when the selection carries anything to clear, is FIRST. Every row is shown, the one the named
note is touching ticked; a ticked row may change nothing when chosen, which is why the preselected
row is the controller's to name: what a toggle would have done — the clear when every selected note
carries a harmonic, else the lowest partial that changes something.
*/
struct ChartHarmonicNodePicker
{
    /*! \brief Index of the note the rows describe, in the tab projection's note order. */
    std::size_t note{};

    /*!
    \brief The rows to offer, in the order to show them: the clear if offered, then the nodes
    ascending by partial; never fewer than two.
    */
    std::vector<ChartHarmonicChoice> choices{};

    /*! \brief Index into `choices` of the row that opens selected, so Return takes it. */
    std::size_t preselected{};
};

/*!
\brief One amount row of the bend picker (\ref IEditorView::showChartBendPicker): the push the
anchors take, in semitones — quarter steps from rest to three whole steps.
*/
struct ChartBendAmountChoice
{
    /*! \brief The amount this row states, in semitones. */
    double semitones{};

    /*!
    \brief True when every anchor the question names already states this amount, so the row is
    drawn ticked: the menu shows the value before it is changed.
    */
    bool current{};
};

/*!
\brief The "No bend" row of the bend picker: choosing it takes the bend statement away from
every point the question names, leaving whatever else each point states.

Offered only where a named point states a bend, since an onset's bend is always stated — its rest
is the amount 0 — and a point stating none has nothing to take away.
*/
struct ChartBendClearChoice
{
};

/*! \brief One row of the bend picker: an amount to state, or no bend at all. */
using ChartBendChoice = std::variant<ChartBendAmountChoice, ChartBendClearChoice>;

/*!
\brief The bend picker the controller asks the view to show: where its question is asked, and the
rows.

The anchor is an INSTANT on a string rather than a note, because the question may be about a point
along a ring that does not exist yet — a bare `B` on a covered slot plants one — and the view
anchors the popup on the lane there. The amounts ascend from rest; the clear, when offered, is
FIRST. The first row opens selected, so Return takes the clear where it is offered and rest
elsewhere: the answer that changes the least.
*/
struct ChartBendPicker
{
    /*! \brief The instant and string the question is asked at: where the popup anchors. */
    ChartSlotViewState anchor{};

    /*! \brief The rows to offer, in the order to show them; the first opens selected. */
    std::vector<ChartBendChoice> choices{};
};

/*!
\brief One row of the hold picker (\ref IEditorView::showChartHoldPicker): what the selected
extent becomes, with the words the view shows for it.
*/
struct ChartHoldChoice
{
    /*!
    \brief The grip the hold over the extent states — an unnamed shape or none — or empty to clear
    the holds there, so the notes decide the spans again.
    */
    std::optional<common::core::HoldGrip> grip{};

    /*! \brief The row's label: "Shape", "None" or "Auto". */
    std::string label;

    /*! \brief The row's tooltip: what choosing it does, in a line. */
    std::string tooltip;
};

/*!
\brief The hold picker the controller asks the view to show: the choices that change the selected
extent, never fewer than two.
*/
struct ChartHoldPicker
{
    /*! \brief The rows to offer, in the order to show them; the first opens selected. */
    std::vector<ChartHoldChoice> choices{};
};

/*! \brief A chip row of the timeline ruler. */
enum class RulerChipRow : std::uint8_t
{
    /*! \brief The section row. */
    Section,

    /*! \brief The tempo-anchor row. */
    Tempo,

    /*! \brief The time-signature row. */
    TimeSignature,
};

/*! \brief A ruler chip, named by its row and its marker's start. */
struct RulerChipKey
{
    /*! \brief The row the chip stands in. */
    RulerChipRow row{RulerChipRow::Section};

    /*! \brief Where the chip's marker starts; a meter reign's chip names its first downbeat. */
    common::core::GridPosition start{};

    /*!
    \brief Orders two chip names by row, then by start.
    \param lhs Left-hand chip.
    \param rhs Right-hand chip.
    \return Ordering of lhs relative to rhs.
    */
    friend auto operator<=>(const RulerChipKey& lhs, const RulerChipKey& rhs) = default;
};

/*! \brief A lock toggle on the toolbar, in the order the toolbar shows them. */
enum class LockControl : std::uint8_t
{
    /*! \brief The Tempo Lock, which freezes the tempo map and the time signatures. */
    Tempo,

    /*! \brief The Note Lock, which freezes the song's content in time. */
    Note,
};

/*!
\brief Everything a refused edit lights, for the view to flash red as one: the thing the edit tried
to change and the thing that stood in its way, on whichever surfaces draw them, and the sentence
saying why.

One flash type across every lane, so a refusal naming a note and the hold beside it, or a chip and
the notes under it, glows as one pulse. Each surface glows the fields it draws and never knows which
target was the subject and which the culprit. The tab-lane fields are indices into the projections
\ref EditorViewState::tab carries (the same object as the highway's chart), valid for the instance
they were derived with, exactly as the selection's are (\ref ChartEditViewState); the ruler chips,
the tone regions and the toolbar locks are named as their surfaces resolve them.
*/
struct RefusalFlash
{
    /*!
    \brief Indices of the refused notes in the tab projection's note order, ascending, each once.
    */
    std::vector<std::size_t> notes{};

    /*!
    \brief Indices of the refused spans in the tab projection's span order, ascending, each once.
    */
    std::vector<std::size_t> spans{};

    /*!
    \brief Indices of the refused none holds in the tab projection's order, ascending, each once.
    */
    std::vector<std::size_t> none_holds{};

    /*!
    \brief Indices of the refused fret-hand position chips in placement order, ascending, each once.
    */
    std::vector<std::size_t> fret_hand_positions{};

    /*! \brief The refused keyframes as projected positions, ascending, each once. */
    std::vector<ChartKeyframeRef> keyframes{};

    /*!
    \brief The refused ruler chips, ascending by row then start, each once; the ruler finds them by
    row and start as it draws them.
    */
    std::vector<RulerChipKey> ruler_chips{};

    /*! \brief The refused tone regions by region id, ascending, each once. */
    std::vector<std::string> tone_regions{};

    /*! \brief The toolbar locks that stood in the way, ascending, each once. */
    std::vector<LockControl> locks{};

    /*!
    \brief Why the edit was refused: the reason the refusal log records, which the tab lane's
    status line shows while the flash runs. The subject is not spelled: the glow is the subject.
    */
    std::string sentence{};
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
    \brief The selected keyframes as projected positions, ascending by note index.

    Published beside the two index lists rather than folded into them because a keyframe is not
    addressable the way they are (\ref ChartKeyframeRef). Keys naming a keyframe an edit dissolved
    simply do not appear, the same resolve-or-drop rule the note indices follow.
    */
    std::vector<ChartKeyframeRef> selected_keyframes{};

    /*!
    \brief Index of the selected fret-hand position in the tab projection's placement order, the
    chip the lane outlines; empty while no placement is selected or the selected one is gone.
    */
    std::optional<std::size_t> selected_fret_hand_position{};

    /*!
    \brief Index of the selected span, derived or held, in the tab projection's span order: the
    span whose rails the lane outlines and whose ruler chip is outlined. Empty while no span is
    selected or the selected one is gone.
    */
    std::optional<std::size_t> selected_span{};

    /*!
    \brief Index of the selected none hold in the tab projection's none holds, whose hatched rails
    the lane outlines; empty while no none hold is selected or the selected one is gone.
    */
    std::optional<std::size_t> selected_none_hold{};

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

    /*! \brief Enables or disables the File > Song Information command. */
    bool song_information_enabled{false};

    /*!
    \brief The song's information as stored, which the Song Information dialog opens on; empty
    with no project.
    */
    common::core::SongMetadata song_metadata{};

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
    (sections, tempo anchors, time signatures, fret-hand positions, tone regions, automation points)
    and for the "+" row at once. Every marker-row surface greys its affordances and refuses its
    gestures from this flag alone: no view derives marker enablement itself, and none reads the
    transport to decide it.

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
    \brief True while the audio-device settings window should be open; the view opens it from
    this fact and reports its closing back.
    */
    bool audio_device_settings_open{false};

    /*!
    \brief Why the whole editor is unavailable, drawn over it while a modal window holds it and
    nothing is busy; empty when there is nothing to explain.
    */
    std::string editor_notice{};

    /*!
    \brief Visible timeline range used to map cursor position and waveform content to pixels.
    */
    common::core::TimeRange visible_timeline{};

    /*!
    \brief The session's song-level tempo map: the map every published time in this state was
    resolved under, and the one clicks snap through.
    */
    common::core::TempoMap tempo_map{};

    /*!
    \brief The map the beat grid and the ruler (its labels and its tempo and time-signature chips)
    draw from: \ref tempo_map, or while a tempo-chip drag is in flight, the map its release would
    land.

    A drag writes nothing until its release commits it once, so \ref tempo_map stays the session's
    map throughout; this map is how the grid and the dragged chip follow the pointer meanwhile.
    */
    common::core::TempoMap grid_map{};

    /*!
    \brief The map content draws under: \ref grid_map with the Note Lock off, so content moves
    with the beat, and \ref tempo_map with it on, so content holds its time — what the release
    lands either way.

    Every published time is re-timed from \ref tempo_map to this map at each content surface's
    seconds-to-pixel seam (common::core::TempoMap::secondsRetimedFrom), which is the identity
    whenever the two are equal. The re-time is exact for every onset and end, so notes, rings,
    keyframes, fret-hand positions, sections, tone regions and automation stand where the release
    puts them. Only notation a law states in seconds — a tail's crop, the kept-sustain floor, a
    head's margin — was decided under \ref tempo_map and is off by the drag's tempo ratio until the
    release re-derives it. The waveform never re-times: audio keeps its time.
    */
    common::core::TempoMap content_map{};

    /*!
    \brief Where the cursor draws while a tempo-chip drag holds a move: the held anchor's time,
    which is where the release seeks it; nothing otherwise, when it draws at the transport.

    The cursor rides the chip without a seek, so no event moves the transport or scrolls the view.
    */
    std::optional<double> drag_cursor_seconds{};

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
    \brief Where the keyboard stands (\ref KeyboardPositionViewState), or nothing while the
    transport plays or no arrangement is loaded.
    */
    std::optional<KeyboardPositionViewState> keyboard_position{};

    /*!
    \brief The first member of what a selection verb acts on, in seconds on the arrangement
    timeline.

    The earliest selected note or keyframe, the selected automation point, or the selected
    marker's start — the chip, whichever way the cursor stands to it — and, with nothing selected,
    the armed caret's slot, where typing acts. After a verb that acts on the selection, the view
    leaves the window alone while this member is fully on screen and centres it otherwise, and
    after an edit that bumps \ref focus_count it centres it when it is off screen. Selecting alone
    never moves the view. Nothing while passive with no caret, while the transport plays, or with
    no arrangement.
    */
    std::optional<double> selection_start_seconds{};

    /*!
    \brief How many edits have asked the view to keep their result in sight.

    An undo or redo selects what it changed and moves the keyboard position onto it; a `Delete` of
    notes or of an automation point leaves the caret on the emptied slot, or the point's durable
    selection, where it struck. Each bumps this count, and on a change the view centres
    \ref selection_start_seconds when that is not on screen. Judged after the edit rather than
    before it like a selection verb, because what the edit left is the whole question — and
    published rather than inferred from the command, because a transition that loads a plugin
    completes later, one off the timeline focuses nothing, and a marker's `Delete` (which only
    selects the marker before it, as a chip click would) asks for nothing. Counted, not flagged,
    so two edits in a row each register.
    */
    std::uint64_t focus_count{0};

    /*!
    \brief How many times the keyboard has moved to a new time, the view's cue to fit
    \ref keyboard_position's measure.

    Counted by the core, which knows what moved it: a step, a click that arms, Tab onto the next
    marker, an extend, a seek. A whole-grid commit is never a move, though it can change the
    position's seconds and its measure's span: the keyboard stays on the music it stood on, and the
    window stays where the charter left it. Neither is the position appearing, at a pause or a
    load. Counted, not flagged, so two moves in a row each register.
    */
    std::uint64_t keyboard_move_count{0};

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
    \brief Whether the tempo lock is on, freezing the tempo map and the time signatures: every
    tempo and meter edit is refused.

    A session fact like grid_snap: never persisted, and reset to true at every project boundary.
    The toolbar button and the menu tick render it; the controller alone refuses the edits.
    */
    bool tempo_lock{true};

    /*!
    \brief Whether the Note Lock is on: a tempo edit then holds every object's time and moves
    the grid under it, instead of moving it with the beat, and every edit that would change the
    song's content is refused.

    A session fact like grid_snap: never persisted, and reset to false at every project boundary.
    The toolbar button and the menu tick render it; the controller alone decides what an edit
    moves and what it refuses.
    */
    bool note_lock{false};

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

    /*! \brief Chart-editing selection and marquee overlays for the tablature lane. */
    ChartEditViewState chart_edit{};

    /*!
    \brief The grid-locked time-selection span resolved to seconds; absent when none is held.

    A full-height range across every surface (chart, tone, lanes) — a mutually-exclusive kind of
    the one editor-wide selection. The endpoints are display-grid positions resolved to seconds so
    the full-canvas overlay maps them to pixels the same way it maps the cursor, surviving zoom and
    scroll without a re-push. Present implies selection_present.
    */
    std::optional<common::core::TimeRange> time_selection{};

    /*!
    \brief True when the one editor-wide selection resolves to something published.

    Derived from the published per-surface states (selected chart notes or keyframes, a fret-hand
    position, a section, a tempo or time-signature chip, a tone region, a resolved automation point,
    or a time-selection span), so a stale selection reads as absent exactly as it renders. Every
    kind counts whether or not it has a Delete; the core's dispatch declines the ones without. The
    view's Delete guard reads this single flag instead of re-deriving the union.
    */
    bool selection_present{false};

    /*!
    \brief Seconds-resolved 3D highway projection of the displayed arrangement.

    The shared scene model the 3D preview window renders — the same projection the game highway
    consumes, so what the charter previews is what the player gets. Rebuilt under the same rule as
    \ref tab and shared immutably across state copies; the preview pads its lanes to
    \ref tab_minimum_displayed_strings itself, so that setting never rebuilds it. Null when the
    arrangement has no chart.
    */
    std::shared_ptr<const common::core::HighwayViewState> highway{};

    /*! \brief True when the waveform draws behind the tablature lane; app-wide preference. */
    bool waveform_visible{true};

    /*!
    \brief App-wide minimum number of tablature string lanes to display.

    Zero means match the chart's string count. The rendered lane count is the larger of this and
    the chart's own count, so raising it only ever adds empty lanes and never hides notes. The 3D
    preview reads it too, padding its board's lanes the same way.
    */
    int tab_minimum_displayed_strings{0};

    /*!
    \brief The player's highway settings as last stored: the picked theme, the pinned string
    palette and how they read the highway.

    Shared with the game, so never project content. The view resolves the theme and palette the 3D
    preview and the tablature lane draw with, and the pickers' ticks, against its theme registry.
    */
    common::core::HighwayPlayerSettings highway_settings{};

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

    /*! \brief Notice to present when the user's audio device is not running. */
    std::optional<AudioDeviceLostPrompt> audio_device_lost_prompt{};

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
