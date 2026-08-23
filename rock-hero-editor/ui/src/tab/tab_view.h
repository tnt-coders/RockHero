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
    \param edit Overlay state resolved against the same projection instance as setState's tab.
    */
    void setEditState(core::ChartEditViewState edit);

    /*!
    \brief Turns the actual-ring reveal on or off; repaints only when the state changes.

    While it is on, every visible note additionally outlines the ring the string ACTUALLY sounds
    for (\ref common::core::ChartViewState::actual_end_seconds) over the presented tail this lane
    draws. The editor holds it on for as long as the Alt key is — the sustain gesture's own
    modifier — so the length being authored is visible while it is authored, and releasing snaps
    the lane back to the presented picture.

    A held state, not a mode: nothing here latches, and the editor re-samples the live key state
    at every point a change could have gone unseen — its modifier callback, focus gain, and
    pointer motion anywhere in its window — so a release nothing delivered cannot strand the
    outlines on. The key itself is the shell's business — this lane, like everything headless
    below it, knows only the state.

    \param revealed True while the reveal modifier is held.
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
    \brief Applies the current tab projection and lane-count preference.

    The projection is compared by pointer identity: the controller rebuilds it only when the
    displayed arrangement changes, so identical pointers mean identical content.

    \param tab Seconds-resolved tab projection, or null when the arrangement has no chart.
    \param minimum_displayed_strings User minimum lane count; zero means match the chart.
    */
    void setState(
        std::shared_ptr<const common::core::ChartViewState> tab, int minimum_displayed_strings);

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
    // Rebuilds the prefix-maximum end tables (presented tails, actual rings) after the projection
    // changes.
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

    // Seconds-resolved tab projection shared with the controller; null without a chart.
    std::shared_ptr<const common::core::ChartViewState> m_tab{};

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

    // True while the reveal modifier is held, so paint adds every visible note's actual-ring
    // outline. Not part of ChartEditViewState: the controller never learns of it, because which
    // key is down is a fact about this window and nothing headless may branch on it.
    bool m_actual_ring_reveal{false};

    // User minimum lane count; zero means match the chart's string count.
    int m_minimum_displayed_strings{0};

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};

    // Running maximum of note end times, aligned with the projection's note order.
    std::vector<double> m_prefix_max_end_seconds{};

    // The same running maximum over the notes' ACTUAL ring ends, which the reveal culls by: a
    // ring outlasting its presented tail must stay in visible range for exactly as long as its
    // outline is drawn, and the table above stops at the tails.
    std::vector<double> m_prefix_max_actual_end_seconds{};
};

} // namespace rock_hero::editor::ui
