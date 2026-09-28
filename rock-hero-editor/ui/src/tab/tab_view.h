/*!
\file tab_view.h
\brief JUCE component that renders the 2D tablature lane over the arrangement waveform.
*/

#pragma once

#include <cstddef>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <rock_hero/editor/core/chart/chart_pointer.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <vector>

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
    \brief Sink receiving a press on a scrolling fret-hand chip, as the index of the placement it
    states in the projection's placement order.
    */
    using FretHandChipCallback = std::function<void(std::size_t)>;

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
    \brief Installs the sink that selects the fret-hand placement whose chip a press lands on.

    The chip is hit-tested here rather than by the controller's chart hit model because its box is
    measured in this lane's label font (common::ui::tabFhpChipBounds), which the headless core does
    not have — the ruler's chips are hit-tested by their view for the same reason. A press on a
    chip goes to this sink and never reaches the chart pointer path, so it neither seeks nor lands
    on the string drawn under the chip.

    \param callback Callback receiving the pressed placement's index; empty leaves chips inert.
    */
    void setFretHandChipCallback(FretHandChipCallback callback);

    /*!
    \brief Publishes whether the marker plane is open for selection and editing.

    The core's availability answer as published in \ref core::EditorViewState::marker_edits_enabled;
    this lane derives nothing of its own from it. While false a press on a fret-hand chip is not a
    selection — there is none to make — so it goes to the chart like any other press on the lane,
    exactly as a ruler chip column seeks while the plane is closed.

    \param marker_edits_enabled Published marker-plane availability.
    */
    void setMarkerEditsEnabled(bool marker_edits_enabled);

    /*!
    \brief Applies the chart-editing overlay state (selection, caret, marquee, pending entry).

    The overlay also feeds the reveal: a SELECTED note and the note the CARET stands in draw to
    their ring ends (core::chartNoteRevealed), so a selection or caret change redraws those tails.

    \param edit Overlay state resolved against the same projection instance as setState's tab.
    */
    void setEditState(core::ChartEditViewState edit);

    /*!
    \brief Turns the whole-lane ring reveal on or off; repaints only when it changes.

    The lane-wide ground of the reveal (core::chartNoteRevealed carries the selection's and the
    caret's). While it is on, EVERY visible note draws to its ring's end — the ring the string
    really sounds for, every keyframe at its true instant — instead of stopping at its ink end.
    The editor holds it on exactly while the application is in the foreground and the Alt key —
    the sustain gesture's own modifier — is down, so the length being authored is visible while
    it is authored, and releasing crops every note back to its ink end. A reveal never moves a
    target (chart_reveal.h), so nothing a charter did a moment ago moves a mark under their
    pointer.

    A held state, not a mode: nothing here latches. The editor re-reads that predicate from the
    operating system every frame for its whole life, so a slide-out nothing delivered cannot strand
    the reveal on and where the pointer sits never matters. The key itself is the shell's business
    — this lane, like everything headless below it, knows only the state.

    \param revealed True while the reveal modifier is held in the foreground application.
    */
    void setRingReveal(bool revealed);

    /*!
    \brief Reports whether the lane wants the pointer at a lane-local position.

    The cursor overlay's pass-through predicate queries this: with a chart displayed the lane
    claims its whole band (the controller still turns empty clicks into seeks), and without one
    it stays transparent so the overlay's click-to-seek is untouched. The string legend's column
    is claimed like any other pixel of the band — it is INERT chrome, not a hole; the lane simply
    answers nothing there (`wantsNotationAt`).

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
    \brief Forwards the slide-out as a chart pointer Up intent.
    \param event Mouse event delivered by JUCE.
    */
    void mouseUp(const juce::MouseEvent& event) override;

    /*!
    \brief Stores the visible timeline range used to map note times to pixels.
    \param visible_timeline Timeline range represented by the component width.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Stores the content x of the viewport's left edge, which the string legend pins to.

    The lane is as wide as the whole canvas and scrolls inside the viewport, so "the left of the
    window" is a moving column in this component's own space — the same fact the tone rows take to
    pin their labels, pushed from the same place.

    \param content_left_x Content-coordinate x of the visible area's left edge.
    */
    void setVisibleContentLeft(int content_left_x);

    /*!
    \brief Applies the current tab projection and lane-count preference.

    The projection is compared by pointer identity: the controller rebuilds it only when the
    displayed arrangement or the chart revision changes, so an identical pointer means identical
    content.

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

    /*!
    \brief The head of the earliest selected note, in this component's coordinates.

    The glyph a verb on the selection keeps on screen. Empty with no selected note.

    \return The head's bounds, or empty.
    */
    [[nodiscard]] std::optional<juce::Rectangle<float>> selectedNoteHeadBounds() const;

    /*!
    \brief The head of one note by projection index, in this component's coordinates.

    The anchor for a popup about that note — the harmonic node picker opens on the head whose
    rows it lists, which need not be the earliest selected one. Empty when the lane has no
    metrics or the index is past the projection.

    \param index Index into the tab projection's note order.
    \return The head's bounds, or empty.
    */
    [[nodiscard]] std::optional<juce::Rectangle<float>> noteHeadBounds(std::size_t index) const;

    /*!
    \brief The head-sized square at one slot, in this component's coordinates.

    The anchor for a popup about an instant rather than a note — the bend picker opens on the
    point along a ring it asks about, which may not exist yet. Empty when the lane has no metrics
    or the slot's string is not on it.

    \param slot The instant and string.
    \return The square's bounds, or empty.
    */
    [[nodiscard]] std::optional<juce::Rectangle<float>> slotHeadBounds(
        const core::ChartSlotViewState& slot) const;

    /*!
    \brief Returns the string legend's panel column at the current pin, or an empty rectangle.

    Published for the CANVAS BENEATH this lane. The panel is an exclusion plus a tint rather than a
    scrim over finished ink, so everything drawn in that column has to stop at one rectangle: this
    lane excludes its own notation from it, and the canvas stops its tempo grid at it. The width is
    the paint core's answer (common::ui::tabStringLegendBounds) and a second measurement anywhere
    would be the drift that authority exists to prevent.

    Lane-local coordinates, which for a lane spanning the whole canvas are the canvas's own.

    \return The panel's rectangle at the current pin, empty where no legend draws.
    */
    [[nodiscard]] juce::Rectangle<int> legendBounds() const;

private:
    // The lane's geometry together with the projection it was derived FROM: laneMetrics answering
    // is what proves that chart non-null, so the two travel as one value and a caller cannot hold
    // the geometry while dereferencing a chart the derivation refused.
    struct DrawableLane
    {
        common::ui::TabLaneMetrics metrics;
        const common::core::ChartViewState& tab;
    };

    // The lane metrics and their chart for the current state and bounds, or nothing when there is
    // nothing to draw with. The one derivation every geometry question here goes through.
    [[nodiscard]] std::optional<DrawableLane> laneMetrics() const;

    // Re-derives the cached legend column from the facts that size it: the bounds, the chart's
    // string names, and the lane count. Called from every setter that can change one of them, and
    // it re-derives the pinned chip too, since a change to any of those moves that as well.
    void refreshLegendColumn();

    // Re-derives which fret-hand placement GOVERNS the window's left edge and the chip it draws
    // there. Its own refresh because a scroll moves it and moves nothing else about the panel: the
    // placement search and one text measurement are a per-scroll cost the panel's own width (210
    // text layouts) could never be. Takes the derivation rather than repeating it, so a refresh
    // that already has a lane in hand does not build the lane's three fonts twice.
    void refreshPinnedFhp(const std::optional<DrawableLane>& lane);

    // The screen-pinned chrome at the current pin: the legend panel, widened to hold the governing
    // fret-hand chip standing on it. ONE rectangle for what a scroll repaints and what the pointer
    // refuses, because the chip is as unclickable as the letters and as mobile. Arithmetic on the
    // two caches — a scroll must not re-measure fonts.
    [[nodiscard]] juce::Rectangle<int> pinnedChromeBounds() const;

    // Whether the pointer is over NOTATION: the lane claims the pixel (wantsPointerAt) and the
    // pinned chrome is not standing on it. The chrome is drawn over the notation, so a press there
    // would select, drag or insert on marks the reader cannot see.
    [[nodiscard]] bool wantsNotationAt(juce::Point<int> local_point) const;

    // The armed caret square's rectangle under the given lane, when one should draw: the single
    // geometry authority shared by the paint overlay and the cursor-mask query. Takes the lane
    // rather than bare metrics because it needs the chart's string bound too, and the two must be
    // the same derivation's.
    [[nodiscard]] std::optional<juce::Rectangle<float>> caretSquare(const DrawableLane& lane) const;

    // Builds the chart pointer event for a mouse event using the currently painted geometry.
    [[nodiscard]] core::ChartPointerEvent makePointerEvent(const juce::MouseEvent& event) const;

    // The index of the scrolling fret-hand chip under a lane-local point, the one drawn last where
    // chips overlap; nothing off every chip. The pinned chip is not asked: it is chrome, which
    // wantsNotationAt has already refused the press over.
    [[nodiscard]] std::optional<std::size_t> fretHandChipAt(juce::Point<float> local_point) const;

    // The column the placement at `index` has its chip drawn at: the pin's while the panel pins it,
    // else its own column. `index` must be inside the lane's placements.
    [[nodiscard]] float fretHandChipX(
        const common::ui::TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
        std::size_t index) const;

    // Recomputes the caret square's content-coordinate mask and pushes it to the sink when it
    // changed since the last publish. Called from every site that can move the square: edit-state
    // and projection pushes, and layout changes (resize/reposition). The caret is fixed to a string
    // row, so unlike the automation lanes this needs no per-frame tick.
    void publishCaretMask();

    // The chart projection, shared with the controller; null without a chart. It is the whole of
    // the pointer path too: the controller hit-tests, selects and inserts against the projection
    // it published.
    std::shared_ptr<const common::core::ChartViewState> m_tab{};

    // Chart-editing overlay state (selection indices, marquee) pushed by the editor.
    core::ChartEditViewState m_edit{};

    // Sink receiving the lane's chart pointer intents; empty disables pointer forwarding.
    PointerEventCallback m_on_pointer_event{};

    // Caret-mask sink into the track viewport's paused-column cut-out; empty disables it.
    CaretMaskCallback m_caret_mask_callback{};

    // Raises the keybind-discovery menu; empty until the shell installs it.
    ContextMenuCallback m_context_menu_callback{};

    // Selects the placement a chip press names; empty leaves chips inert.
    FretHandChipCallback m_fret_hand_chip_callback{};

    // The published marker-plane availability; chips are markers only while it is open.
    bool m_marker_edits_enabled{false};

    // Last caret mask handed to the sink, so a republish only fires on an actual change.
    std::optional<juce::Range<float>> m_published_caret_mask{};

    // True while the reveal modifier is held, so every visible note draws to its ring's end. Not
    // part of ChartEditViewState: the controller never learns of it, because which key is down is
    // a fact about this window and nothing headless may branch on it.
    bool m_ring_reveal{false};

    // User minimum lane count; zero means match the chart's string count.
    int m_minimum_displayed_strings{0};

    // Content x of the viewport's left edge: where the string legend pins. Zero is the canvas's
    // own left edge, which is where a lane that does not scroll (the tests) leaves it.
    int m_visible_content_left{0};

    // The legend column as it stands at pin zero — width measured from the tuning's longest name
    // in the lane's own font, spanning the lane's height; empty when no legend draws. Cached
    // because a scroll moves ONLY the pin: re-deriving it there would build three fonts and
    // measure every string name on every vblank of a playback follow, for a column whose size
    // none of that changes. Refreshed from the facts that DO size it (refreshLegendColumn).
    juce::Rectangle<int> m_legend_column{};

    // The fret-hand placement governing the window's left edge, which the panel states as its
    // current-state column: an FHP is a region-scoped value exactly like a tempo, so the one in
    // force at the edge pins there. Empty before the song's first placement, and empty again once
    // the next placement's own chip has come close enough for the pin to yield to it — the ruler's
    // pin law, which both rows read from one statement of it. Held as the placement's INDEX into
    // the lane's placements, so the selection outline and a pending entry find the pinned chip as
    // the placement it is.
    std::optional<std::size_t> m_pinned_fhp{};

    // The pinned chip's box AT PIN ZERO, empty while nothing pins. Cached beside the legend column
    // and translated by the same number, so what a scroll repaints covers the chip as well as the
    // letters.
    juce::Rectangle<int> m_pinned_fhp_column{};

    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};
};

} // namespace rock_hero::editor::ui
