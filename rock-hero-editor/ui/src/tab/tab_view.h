/*!
\file tab_view.h
\brief JUCE component that renders the 2D tablature lane over the arrangement waveform.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

namespace rock_hero::common::ui
{
struct TabLaneMetrics;
} // namespace rock_hero::common::ui

namespace rock_hero::editor::ui
{

/*!
\brief String count the hosting row's height is sized against.

Six lanes across the default waveform row is the editor's reference density: TrackViewport scales
the row's height by the displayed string count relative to this number, so a four-string bass
shrinks the row and an eight-string display grows it rather than the lanes compressing or leaving
empty margins. The per-lane spacing that results is the shared lane geometry's, not a second rule —
see common::ui::tabLaneCenterY.
*/
inline constexpr int g_tab_reference_string_count{6};

/*!
\brief Returns the base display color for one string lane.

Delegates to common::ui::tabStringColor, which owns the rule and the palette; the editor keeps this
name so its lane and ruler code reads in its own vocabulary. Do not restate the derivation here —
the shared declaration is the one place it is described.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\return Base lane color the tablature style derives its surfaces from.
*/
[[nodiscard]] juce::Colour tabStringColor(int displayed_string, int displayed_string_count);

/*!
\brief Returns the vertical center of one string lane inside the tablature bounds.

Delegates to common::ui::tabLaneCenterY, which owns the lane stacking and spacing rule. The editor
supplies bounds sized against \ref g_tab_reference_string_count.

\param displayed_string Lane's string position, 1 = lowest displayed lane.
\param displayed_string_count Total number of displayed lanes.
\param bounds Full tablature lane bounds.
\return Vertical lane center in the bounds' coordinate space.
*/
[[nodiscard]] float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept;

/*!
\brief Renders the chart tablature over the arrangement waveform lane.

The notation itself is drawn by the shared paint core (common/ui tab_paint_core.h), which both
products render through and which describes what it draws; this view supplies the bounds and the
visible-timeline mapping, matching the waveform beneath it, from the controller's seconds-resolved
tab projection. On top of the notation it draws the chart-editing overlays the editor alone owns:
selection rings, the white square of the armed caret, and the in-flight marquee. While a chart is
displayed the lane owns its pointer events, converting them to lane-local chart pointer intents; the
controller decides what a press means (select, caret arming, marquee, or — while playing — a plain
seek). Without a chart the lane is pointer-transparent as before.
*/
class TabView final : public juce::Component
{
public:
    /*! \brief One pointer-intent sink receiving every phase of a lane gesture. */
    using PointerEventCallback =
        std::function<void(core::ChartPointerPhase, const core::ChartPointerEvent&)>;

    /*!
    \brief Receives the armed caret square's paused-column cut-out span in content coordinates.

    Pushed whenever the mask changes (empty while no caret is armed) so the track viewport never
    has to poll this lane's geometry to keep the paused cursor's gap in step with the caret.
    */
    using CaretMaskCallback = std::function<void(std::optional<juce::Range<float>>)>;

    /*! \brief Sink raising the keybind-discovery menu at a lane-local position. */
    using ContextMenuCallback = std::function<void(juce::Point<int>)>;

    /*!
    \brief Installs the sink that receives the lane's chart pointer intents.
    \param on_pointer_event Callback invoked for every gesture phase; empty disables forwarding.
    */
    void setPointerEventCallback(PointerEventCallback on_pointer_event);

    /*!
    \brief Installs the sink notified when the caret mask (paused-column cut-out) changes.
    \param callback Callback receiving the caret square's content-coordinate y span, or empty.
    */
    void setCaretMaskCallback(CaretMaskCallback callback);

    /*!
    \brief Installs the sink that raises the lane's keybind-discovery menu.

    The lane detects the popup gesture but does not build the menu: its items are registered
    commands invoked through the command manager, which the editor shell owns.

    \param callback Callback receiving the lane-local position of the popup gesture.
    */
    void setContextMenuCallback(ContextMenuCallback callback);

    /*!
    \brief Applies the chart-editing overlay state (selection, marquee).

    The selection is more than an overlay here: a SELECTED note draws in its actual form, because
    the selection is the thing under scrutiny (\ref setActualRingReveal carries the pick's other
    input). Every chart verb already settles on a selection change, so deselecting is the moment
    presentation clips the tail back to the picture.

    \param edit Overlay state resolved against the same projection instance as setState's tab.
    */
    void setEditState(core::ChartEditViewState edit);

    /*!
    \brief Turns the whole-lane actual-ring reveal on or off; repaints only when it changes.

    One of the two inputs to the lane's per-note form pick, the other being the selection. While
    it is on, EVERY visible note draws in its ACTUAL form — the tail is the ring the string really
    sounds for, with its techniques and its payload riding it — in place of the presented picture.
    The editor holds it on exactly while the application is in the foreground and the Alt key —
    the sustain gesture's own modifier — is down, so the length being authored is visible while it
    is authored, and releasing clips every note back to its presented tail except the ones the
    selection still names.

    It is kept beside the selection because the selection cannot do its job. With a selection
    standing, typing a digit RETYPES those notes instead of inserting one, so a charter placing
    notes holds no selection at all — and placing the next note is exactly when the real tails
    around it matter. Alt is that lookahead: the whole passage's rings, including every note
    nothing is selected on.

    A held state, not a mode: nothing here latches. The editor re-reads that predicate from the
    operating system every frame for its whole life, so a release nothing delivered cannot strand
    the reveal on and where the pointer sits never matters. The key itself is the shell's business
    — this lane, like everything headless below it, knows only the state.

    \param revealed True while the reveal modifier is held in the foreground application.
    */
    void setActualRingReveal(bool revealed);

    /*!
    \brief Reports whether the lane wants the pointer at a lane-local position.

    The cursor overlay's pass-through predicate queries this: with a chart displayed the lane
    claims its whole band (the controller still turns empty clicks into seeks), and without one
    it stays transparent so the overlay's click-to-seek is untouched.

    \param local_point Position in this component's coordinates.
    \return True when the lane should receive the pointer event.
    */
    [[nodiscard]] bool wantsPointerAt(juce::Point<int> local_point) const;

    /*!
    \brief Claims pointer events exactly where wantsPointerAt does.
    \param x Pointer x position in local coordinates.
    \param y Pointer y position in local coordinates.
    \return True when the lane should receive the pointer event.
    */
    bool hitTest(int x, int y) override;

    /*!
    \brief Forwards a press as a chart pointer Down intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards held-button movement as a chart pointer Drag intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards the release as a chart pointer Up intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards a button-less hover as a chart pointer Move intent (the Alt insert ghost).
    \param event Mouse event delivered by JUCE.
    */
    void mouseMove(const juce::MouseEvent& event) override;

    /*!
    \brief Forwards the pointer leaving the lane as a chart pointer Exit intent, clearing hover.
    \param event Mouse event delivered by JUCE.
    */
    void mouseExit(const juce::MouseEvent& event) override;

    /*!
    \brief Stores the visible timeline range used to map note times to pixels.
    \param visible_timeline Timeline range represented by the component width.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Applies the current tab projections and lane-count preference.

    Both forms of one chart arrive together, because the pick chooses between them per note inside
    a single repaint with no controller round trip. The projections are compared by pointer
    identity: the controller rebuilds them only when the displayed arrangement or the chart
    revision changes, so identical pointers mean identical content.

    \param tab Seconds-resolved tab projection, or null when the arrangement has no chart.
    \param tab_actual The same chart with every note at its actual ring, or null with no chart.
           Absent, nothing can draw an actual ring and every note keeps its presented form.
    \param minimum_displayed_strings User minimum lane count; zero means match the chart.
    */
    void setState(
        std::shared_ptr<const common::core::ChartViewState> tab,
        std::shared_ptr<const common::core::ChartViewState> tab_actual,
        int minimum_displayed_strings);

    /*!
    \brief Draws the visible notes and sustains onto the lane.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Republishes the caret mask, whose content-coordinate span shifts when the lane moves.
    */
    void moved() override;

    /*! \brief Republishes the caret mask, whose row geometry shifts when the lane is resized. */
    void resized() override;

    /*!
    \brief Returns the armed caret square's vertical span in local coordinates, if displayed.

    The track viewport cuts this span out of the behind-content paused play-from-here column,
    so ONLY the cursor hides behind the caret square — the grid dots and string lines the
    square overlaps keep showing through it.

    \return Outer vertical span of the caret square including its stroke, or empty while no
    caret square is displayed.
    */
    [[nodiscard]] std::optional<juce::Range<float>> caretMaskYRange() const;

private:
    // Rebuilds the visible-range index after the projections change.
    void rebuildVisibilityIndex();

    // The armed caret square's rectangle under the given metrics, when one should draw: the
    // single geometry authority shared by the paint overlay and the cursor-mask query.
    [[nodiscard]] std::optional<juce::Rectangle<float>> caretSquare(
        const common::ui::TabLaneMetrics& metrics) const;

    // Builds the chart pointer event for a mouse event using the currently painted geometry.
    [[nodiscard]] core::ChartPointerEvent makePointerEvent(const juce::MouseEvent& event) const;

    // Recomputes the caret square's content-coordinate mask and pushes it to the sink when it
    // changed since the last publish. Called from every site that can move the square: edit-state
    // and projection pushes, and layout changes (resize/reposition). The caret is fixed to a string
    // row, so unlike the automation lanes this needs no per-frame tick.
    void publishCaretMask();

    // The chart's two forms, shared with the controller; both null without a chart. They align by
    // index and differ ONLY in their notes, so the presented one is the lane's authority for
    // everything else it draws with — string count, capo, hand-shape spans, fret-hand placements —
    // and nothing outside the per-note pick has to ask which form is showing.
    //
    // The presented form is also the whole of the pointer path: the controller hit-tests, selects
    // and inserts against the projection it published, so an actual ring is drawn and nothing
    // more. That holds by construction rather than by enforcement — the only reads outside paint
    // are the string count and whether a chart exists.
    //
    // The actual form is null when the host published no second form, which simply leaves every
    // note presented.
    std::shared_ptr<const common::core::ChartViewState> m_presented{};
    std::shared_ptr<const common::core::ChartViewState> m_actual{};

    // Running maximum of the ACTUAL form's note ends (the presented form's when no actual one was
    // published), which bounds the visible note range inside the paint core.
    //
    // ONE table for a lane drawing both forms at once. Presentation only ever trims a tail, so a
    // presented end always falls at or before its own note's ring: culling against the rings keeps
    // every note in the candidate range for as long as ANY form of it could be drawn, and the
    // paint passes then drop each note whose DRAWN end really precedes the window. A second,
    // presented table would be a tighter bound on a cull that is already exact — and unusable
    // besides, since one member of a chord can draw actual while its neighbour draws presented.
    std::vector<double> m_prefix_max_end_seconds{};

    // The same running maximum over the SPANS, bounding the paint core's two span passes. Built
    // beside the notes' table and from either form indifferently: presentation moves no span, so
    // the two forms carry the identical shape list.
    std::vector<double> m_prefix_max_shape_end_seconds{};

    // Chart-editing overlay state (selection indices, marquee) pushed by the editor.
    core::ChartEditViewState m_edit{};

    // Sink receiving the lane's chart pointer intents; empty disables pointer forwarding.
    PointerEventCallback m_on_pointer_event{};

    // Caret-mask sink into the track viewport's paused-column cut-out; empty disables it.
    CaretMaskCallback m_caret_mask_callback{};

    // Raises the keybind-discovery menu; empty until the shell installs it.
    ContextMenuCallback m_context_menu_callback{};

    // Last caret mask handed to the sink, so a republish only fires on an actual change.
    std::optional<juce::Range<float>> m_published_caret_mask{};

    // True while the reveal modifier is held, so the pick answers "actual" for every visible note
    // rather than only for the selected ones. Not part of ChartEditViewState: the controller never
    // learns of it, because which key is down is a fact about this window and nothing headless may
    // branch on it.
    bool m_actual_ring_reveal{false};

    // User minimum lane count; zero means match the chart's string count.
    int m_minimum_displayed_strings{0};

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};
};

} // namespace rock_hero::editor::ui
