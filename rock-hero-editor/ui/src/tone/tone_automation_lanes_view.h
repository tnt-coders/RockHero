/*!
\file tone_automation_lanes_view.h
\brief Automation lanes beneath the tone row: one lane per automated parameter plus a "+" lane.

One component paints every lane (dynamic count, per-lane vertical resize, dirty-rect locality) and
the trailing empty lane whose pinned "+" chip opens the parameter picker. Gestures follow the
editor-wide interaction model (docs/plans/in-progress/editing-interaction-model.md): a plain click never
mutates (points select; empty lane space passes through to the seek overlay), Alt is the insert
quasimode (click or press-drag-release places a point, with a ghost preview and copy cursor while
Alt is held; the point lands ON the curve at the snapped time and the drag phase pulls its value
by the pointer's delta, so placement is sonically silent until deliberately pulled — 2026-07-18),
Ctrl bypasses grid snap to the fine grid, Shift axis-locks point drags, and Esc
cancels the gesture in flight. Gestures preview locally and commit one full-point-list intent on
release; a state push mid-gesture is deferred until the gesture ends so it cannot reset the edit
in progress. The lanes never resize themselves: heights flow up through a callback and the track
viewport lays the component out, so the cursor overlay and content height stay authoritative.
*/

#pragma once

#include "timeline/cursor_overlay.h"

#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/editor/core/tone/tone_automation_pointer.h>
#include <rock_hero/editor/core/tone/tone_automation_view_state.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::ui
{

/*! \brief Automation lanes row rendered beneath the tone track. */
class ToneAutomationLanesView final : public juce::Component
{
public:
    /*! \brief Listener for user intents emitted by the automation lanes. */
    class Listener
    {
    public:
        /*! \brief Destroys the listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Called when the "+" picker chooses a parameter to open a lane for.
        \param instance_id Plugin instance owning the parameter.
        \param param_id Parameter id within the plugin.
        */
        virtual void onToneAutomationLaneAddRequested(
            std::string instance_id, std::string param_id) = 0;

        /*!
        \brief Called when a gesture commits a lane's full replacement point list.
        \param instance_id Plugin instance owning the parameter.
        \param param_id Parameter id within the plugin.
        \param points Replacement musical points in ascending order.
        */
        virtual void onToneAutomationPointsEditRequested(
            std::string instance_id, std::string param_id,
            std::vector<common::core::ToneAutomationPoint> points) = 0;

        /*!
        \brief Called when an unauthored tracking lane asks to be closed.
        \param instance_id Plugin instance owning the parameter.
        \param param_id Parameter id within the plugin.
        */
        virtual void onToneAutomationLaneRemoveRequested(
            std::string instance_id, std::string param_id) = 0;

        /*!
        \brief Called when a gesture makes a point the editor-wide selection.

        Selection is controller-owned (one selection editor-wide, 2026-07-18): the view emits
        the durable point identity and renders whatever selection the next state push
        publishes back.

        \param instance_id Plugin instance owning the parameter.
        \param param_id Parameter id within the plugin.
        \param position Exact musical position of the selected point.
        */
        virtual void onToneAutomationPointSelectRequested(
            std::string instance_id, std::string param_id, common::core::GridPosition position) = 0;

        /*!
        \brief Called on a button-less hover over an editable lane area (the Alt insert ghost).

        The controller resolves whether Alt is held over an insertable slot while paused and
        publishes the ghost through \ref core::ToneAutomationViewState::insert_ghost, snapping
        exactly as an Alt+click would: the event carries the raw lane-local pixel x plus the
        geometry it was mapped against, so the controller inverts the pixel through the same
        placement seam the click uses instead of trusting a view-computed time. The view forwards
        every lane-area hover (Alt or not) and renders whatever ghost the next state push publishes
        back — the ghost is controller-owned, exactly like the selection and the lane caret.

        \param event Lane-local pointer state (hovered lane identity, geometry, pixel x/y, mods).
        */
        virtual void onToneAutomationPointerMove(const core::ToneAutomationPointerEvent& event) = 0;

        /*! \brief Called when the pointer leaves the lanes, clearing any Alt insert ghost. */
        virtual void onToneAutomationPointerExit() = 0;

        /*!
        \brief Called on a primary-button press over a point or empty editable lane area.

        The controller owns the resulting gesture: it re-resolves the point-vs-empty-area hit and
        arms a point move, an Alt-insert placement, or the lane caret. The view forwards only these
        two editing zones — resize bands, name chips, the "+" picker, and right-clicks it handles
        itself — and paints whatever preview the controller publishes back.

        \param event Lane-local pointer state (pressed lane identity, geometry, value-band extents,
        pixel x/y, click count, modifiers).
        */
        virtual void onToneAutomationPointerDown(const core::ToneAutomationPointerEvent& event) = 0;

        /*!
        \brief Called as the pressed pointer moves, advancing the controller's drag preview.
        \param event Lane-local pointer state; the controller reads the live pixel x/y and modifiers.
        */
        virtual void onToneAutomationPointerDrag(const core::ToneAutomationPointerEvent& event) = 0;

        /*!
        \brief Called when the pressed pointer releases, ending the controller's drag.
        \param event Lane-local pointer state; the controller reads the live pixel x/y and modifiers.
        */
        virtual void onToneAutomationPointerUp(const core::ToneAutomationPointerEvent& event) = 0;

    protected:
        /*! \brief Creates the listener interface. */
        Listener() = default;

        /*! \brief Copies the listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the listener interface from another interface.
        \return Reference to this listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*! \brief Receives the transient snap guide while a point drag is active. */
    using SnapGuideCallback = std::function<void(std::optional<TimelineSnapGuide>)>;

    /*! \brief Notified when lane heights (and so the row's total height) changed. */
    using HeightsChangedCallback = std::function<void()>;

    /*!
    \brief Creates the automation lanes row.
    \param listener Listener that receives automation intents.
    \param tempo_map Tempo map used for musical snapping; referenced, not copied, so the owner must
    keep it alive for this view's lifetime.
    \param tone_automation Automation port polled read-only at render cadence so lanes without
    authored points track the parameter's live value; referenced for this view's lifetime.
    */
    ToneAutomationLanesView(
        Listener& listener, const common::core::TempoMap& tempo_map,
        const common::audio::IToneAutomation& tone_automation);

    /*!
    \brief Sets the timeline range represented by the full content width.
    \param visible_timeline Timeline range shared with every other timeline row.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Pins lane name chips and the "+" chip to the visible left edge.
    \param content_left_x Content x coordinate currently at the viewport's left edge.
    */
    void setVisibleContentLeft(int content_left_x);

    /*!
    \brief Sets the grid step used for snapped point placement.
    \param note_value Grid step as a fraction of a whole note, shared with grid rendering.
    */
    void setGridNoteValue(common::core::Fraction note_value);

    /*!
    \brief Replaces the rendered automation state, or defers it while a gesture is in flight.

    A state push that lands between mouseDown and mouseUp is stashed and applied when the gesture
    ends, so the engine's frequent state pushes cannot reset an edit in progress.

    \param state Automation lanes for the selected tone.
    */
    void setState(const core::ToneAutomationViewState& state);

    /*!
    \brief Sets the editable window (the selected tone region's span); edits clamp inside it.
    \param window Selected region's time range, or empty when nothing is selected.
    */
    void setEditableWindow(common::core::TimeRange window);

    /*!
    \brief Installs the shared snap-guide sink used during point drags.
    \param callback Callback receiving guide updates; empty clears the guide immediately.
    */
    void setSnapGuideCallback(SnapGuideCallback callback);

    /*!
    \brief Installs the sink notified when the row's total height changes.
    \param callback Callback invoked after lane add/remove or a lane resize drag.
    */
    void setHeightsChangedCallback(HeightsChangedCallback callback);

    /*!
    \brief Reports the total height of every lane plus the trailing "+" lane.
    \return Row height in pixels for the track viewport's content layout.
    */
    [[nodiscard]] int totalHeight() const;

    /*!
    \brief Reports whether a pointer at a local position lands on interactive lane content.

    The cursor overlay's hit-test pass-through uses this to decide between lane editing and
    click-to-seek: point handles, lane name chips, resize bands, the "+" chip, and empty
    editable lane area all claim the pointer — a plain click on empty lane area seeks and arms
    the caret on that lane (§9b), and with Alt held it is the insert quasimode's target.
    Disabled lanes, out-of-window areas, and empty strip space pass through.

    \param local_point Position in this component's coordinates.
    \return True when the pointer should reach the lanes instead of the cursor overlay.
    */
    [[nodiscard]] bool wantsPointerAt(juce::Point<int> local_point) const;

    /*! \brief Paints every lane, its curve and points, the insert ghost, and the "+" lane. */
    void paint(juce::Graphics& graphics) override;

    /*! \brief Updates the hover cursor and readout, and forwards the hover (the insert ghost). */
    void mouseMove(const juce::MouseEvent& event) override;

    /*! \brief Classifies and starts a gesture: Alt-insert, move point, resize lane, or menu. */
    void mouseDown(const juce::MouseEvent& event) override;

    /*! \brief Advances the active gesture's local preview; Shift locks the dominant axis. */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*! \brief Commits the active gesture as one intent when it changed anything. */
    void mouseUp(const juce::MouseEvent& event) override;

    /*! \brief Opens the typed exact-value editor for a double-clicked point. */
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    /*! \brief Clears the hover readout and ends the hover (clearing the ghost) mid-hover. */
    void mouseExit(const juce::MouseEvent& event) override;

    /*!
    \brief Reports the value-readout text currently shown next to the cursor, for tests.
    \return Readout text while a point is hovered or dragged, or empty when none is shown.
    */
    [[nodiscard]] std::optional<juce::String> valueReadoutTextForTest() const;

    /*!
    \brief Cancels the gesture in flight, restoring the pre-gesture state.

    The editor routes Esc here so an unwanted drag can be abandoned without committing: a point
    move or Alt-insert simply never commits, and a lane resize restores its starting height. A
    state push deferred during the gesture is adopted, exactly as an uncommitted release would.

    \return True when a gesture was active and has been cancelled.
    */
    [[nodiscard]] bool cancelActiveGesture();

    /*!
    \brief Opens the typed-value editor at the armed lane caret, seeded with a typed digit.

    The keyboard mirror of double-click value entry (the typing rule on lane rows, §9b):
    committing the text creates an on-curve-positioned point at the caret slot with the typed
    value, or retypes the point already there.

    \param digit First typed digit in [0, 9], seeding the editor text.
    \return True when an armed lane caret existed and the editor was requested.
    */
    [[nodiscard]] bool beginCaretValueEntry(int digit);

    /*!
    \brief Outer vertical span of the lane caret square, for the paused-column cut-out.
    \return The square's y range in this component's coordinates, or empty while no lane caret
    is published.
    */
    [[nodiscard]] std::optional<juce::Range<float>> caretMaskYRange() const;

private:
    // One lane's vertical extent in component coordinates.
    struct LaneExtent
    {
        int top{0};
        int height{0};
    };

    // The lane-height resize drag, the one gesture the view still owns: a pure presentation
    // concern (lane heights flow up through the height callback, never the model). The point
    // move/insert drag moved to the controller (it owns hit resolution, snap, and the delta-value
    // state machine); the view forwards its pointer events and paints the published preview.
    struct ResizeLaneDrag
    {
        std::size_t lane_index{};
        int start_height{};
        int start_y{};
    };

    // Transient "position · value" chip drawn next to the cursor while a point is hovered or
    // dragged, so the value the gesture is producing is legible without a separate panel.
    struct ValueReadout
    {
        juce::Point<int> anchor{};
        juce::String text;
        friend bool operator==(const ValueReadout& lhs, const ValueReadout& rhs) = default;
    };

    // Hit zones resolved by hitAt(); the pass-through predicate and mouseDown share this result.
    // LaneAreaHit is empty editable lane area: Alt makes it the insert quasimode's target, a
    // plain click seeks and arms the caret on the lane (§9b). LaneChipHit is the lane's pinned
    // name chip — the lane handle that opens the lane menu on any click.
    struct PointHit
    {
        std::size_t lane_index{};
        std::size_t point_index{};
    };
    struct LaneAreaHit
    {
        std::size_t lane_index{};
    };
    struct LaneChipHit
    {
        std::size_t lane_index{};
    };
    struct ResizeBandHit
    {
        std::size_t lane_index{};
    };
    struct PlusChipHit
    {
    };
    using Hit = std::variant<PointHit, LaneAreaHit, LaneChipHit, ResizeBandHit, PlusChipHit>;

    // The durable identity of an automation point (lane keys plus exact musical position). The
    // selection itself is controller-owned (one selection editor-wide); the view resolves the
    // published \ref core::ToneAutomationViewState::selected_point back to this triple when a
    // verb needs the durable identity.
    struct SelectedPoint
    {
        std::string instance_id;
        std::string param_id;
        common::core::GridPosition position;
    };

    // Applies a pushed state immediately: replaces the model and clears any transient gesture
    // overlay. Called directly when no gesture is active, and drained from mouseUp for a snapshot
    // that arrived (and was deferred) during a gesture. Never called while m_drag is set.
    void applyState(const core::ToneAutomationViewState& state);

    // Resolves the interactive zone at a local point, or nullopt for pass-through space.
    [[nodiscard]] std::optional<Hit> hitAt(juce::Point<int> local_point) const;

    // The lane name chip's bounds, shared by painting and hit-testing so they cannot diverge.
    [[nodiscard]] juce::Rectangle<int> laneChipBounds(
        const core::ToneAutomationLaneViewState& lane, const LaneExtent& extent) const;

    // The lane name chip's text ("Plugin · Param", with a missing-plugin suffix when unresolved).
    [[nodiscard]] static juce::String laneChipText(const core::ToneAutomationLaneViewState& lane);

    // Returns the lane extents in display order followed by the trailing "+" lane extent.
    [[nodiscard]] std::vector<LaneExtent> laneExtents() const;

    // Returns the configured or default height for one lane key.
    [[nodiscard]] int laneHeight(const std::string& instance_id, const std::string& param_id) const;

    // Converts a content x to an exact musical position: grid-snapped unless Ctrl bypasses, then
    // quantized to the 1/960-beat fine grid so stored positions stay exact rationals.
    [[nodiscard]] std::optional<common::core::GridPosition> musicalPositionForX(
        float content_x, const juce::ModifierKeys& mods) const;

    // Converts a musical position to a content x for drawing, when the geometry is valid.
    [[nodiscard]] std::optional<float> xForSeconds(double seconds) const;

    // The curve's value at a time, matching exactly what paint draws: linear segments on a
    // continuous lane, held steps on a discrete one, flat extensions outside the authored span,
    // and the live tracking value when the lane has no points yet. Point placement lands here
    // (on the curve) so insertion is sonically silent until the point is deliberately pulled.
    [[nodiscard]] float curveValueAt(
        const core::ToneAutomationLaneViewState& lane, double seconds) const;

    // Emits the points-edit intent that inserts a new point into a lane and selects it. Drives the
    // typed-value editor's create-at-the-caret branch; the keyboard create-then-nudge is the
    // controller's now (it moved with the pointer pipeline).
    void requestPointInsert(
        const core::ToneAutomationLaneViewState& lane, const common::core::GridPosition& position,
        float value);

    // The lane caret square's rectangle in component coordinates, shared by paint and the
    // paused-column mask so they cannot diverge; empty while no lane caret is published.
    [[nodiscard]] std::optional<juce::Rectangle<float>> laneCaretSquare() const;

    // Snaps a raw normalised value to the nearest discrete step for a stepped parameter (so a
    // toggle moves only between its states); returns the value unchanged for a continuous lane.
    [[nodiscard]] static float snappedValueForLane(
        float raw_value, const core::ToneAutomationLaneViewState& lane);

    // Builds the framework-free pointer event the controller owns the gesture policy for: the raw
    // lane-local pixels plus the geometry, value-band extents, click count, and modifiers the
    // controller re-derives snap, value, and hit resolution from. `lane_index` names the pressed
    // or hovered lane when one is resolved (Down/Move); a Drag/Up rides the frozen gesture and
    // leaves the lane identity default.
    [[nodiscard]] core::ToneAutomationPointerEvent makePointerEvent(
        const juce::MouseEvent& event, std::optional<std::size_t> lane_index) const;

    // The value-band pixel span (top + height) of every displayed lane, in published-lane display
    // order, carried on the pointer event so the controller maps y to a value without the view's
    // band-layout constants. The trailing "+" lane is excluded.
    [[nodiscard]] std::vector<core::ToneAutomationLaneExtent> laneValueBandExtents() const;

    // Opens the "+" parameter picker as an async popup menu, grouped per chain plugin.
    void showParameterPicker();

    // Opens the delete menu for a right-clicked point.
    void showPointMenu(const PointHit& hit);

    // Opens the remove menu for a right-clicked lane row (authored or tracking).
    void showLaneMenu(std::size_t lane_index);

    // Emits the points-edit intent that removes the point at a position from its lane; shared by the
    // right-click "Delete Point" menu and the keyboard-Delete path. A no-op when it no longer exists.
    void requestPointDelete(
        const std::string& instance_id, const std::string& param_id,
        const common::core::GridPosition& position);

    // Emits the points-edit intent that replaces one point's position and value (echoing every
    // other point bit-identically) and selects the edited point. Shared by the typed value editor
    // and the menu's reset-to-default (keyboard nudges are the controller's now). A no-op when the
    // point no longer exists.
    void requestPointReplace(
        const std::string& instance_id, const std::string& param_id,
        const common::core::GridPosition& position, const common::core::GridPosition& new_position,
        float new_value);

    // Opens the typed exact-value editor (a callout with one text field) for a point; parses the
    // entered text through the automation port in the parameter's native units.
    void showPointValueEditor(const PointHit& hit);

    // Reports whether a specific lane point is the published editor-wide selection.
    [[nodiscard]] bool isPointSelected(
        const core::ToneAutomationLaneViewState& lane,
        const common::core::GridPosition& position) const;

    // Resolves the published selection reference back to the durable point identity, or empty
    // when the state publishes no (or a stale) selection.
    [[nodiscard]] std::optional<SelectedPoint> selectedPointFromState() const;

    // Resolves the real lane row (not the trailing "+" lane) containing a local y, or empty.
    [[nodiscard]] std::optional<std::size_t> laneIndexAtY(int y) const;

    // Current tracking-line value for a lane: the live provider when available, else state.
    [[nodiscard]] float trackingValueFor(const core::ToneAutomationLaneViewState& lane) const;

    // Vblank tick: repaints unauthored lanes whose live value moved since the last frame.
    void repaintMovedTrackingLanes();

    // Publishes the snap guide (or clears it when empty).
    void publishSnapGuide(std::optional<TimelineSnapGuide> guide);

    // Builds the "position · value" readout text for a point at a musical position and value,
    // formatting the value in the parameter's native units through the automation port.
    [[nodiscard]] juce::String readoutTextFor(
        const common::core::GridPosition& position, const core::ToneAutomationLaneViewState& lane,
        float norm_value) const;

    // Positions the readout chip next to the cursor anchor, flipped to stay inside the component.
    [[nodiscard]] juce::Rectangle<int> readoutBounds(const ValueReadout& readout) const;

    // Sets or clears the value readout, repainting only the vacated and freshly covered chip areas.
    void setValueReadout(std::optional<ValueReadout> readout);

    // Seeds the cursor readout from the controller's published drag preview (position + value),
    // anchored at the pointer: the presentation half of the move/insert drag, whose numbers the
    // controller owns. A no-op when no preview is standing.
    void applyDragPreviewReadout(juce::Point<int> anchor);

    // Paints the value-readout chip when one is active.
    void paintValueReadout(juce::Graphics& graphics) const;

    // Intent sink for lane add and point edits.
    Listener& m_listener;

    // Tempo map used for musical snapping and readouts; owned by the editor session.
    const common::core::TempoMap& m_tempo_map;

    // Timeline range represented by the full content width.
    common::core::TimeRange m_visible_timeline{};

    // Content x currently at the viewport's left edge, for pinned chips.
    int m_visible_content_left{0};

    // Grid step for snapped placement, shared with the ruler and grid rendering.
    common::core::Fraction m_grid_note_value{1, 4};

    // Automation lanes for the selected tone.
    core::ToneAutomationViewState m_state{};

    // Selected region's span; edits clamp inside it and outside content draws dimmed.
    common::core::TimeRange m_editable_window{};

    // Per-lane heights keyed by (instance id, param id) so they survive reordering state pushes.
    std::map<std::pair<std::string, std::string>, int> m_lane_heights{};

    // Active lane-resize drag, if any. While it is set, incoming state pushes are deferred (not
    // applied) so the resize keeps against the heights it started with; the controller-owned point
    // move/insert drag needs no such deferral because its preview is part of every state build.
    std::optional<ResizeLaneDrag> m_drag{};

    // Latest state pushed while a resize was in flight, applied when the resize ends.
    std::optional<core::ToneAutomationViewState> m_pending_state{};

    // Transient value readout shown next to the cursor while a point is hovered or dragged.
    std::optional<ValueReadout> m_value_readout{};

    // Automation port polled read-only by unauthored tracking lanes; owned by the composition.
    const common::audio::IToneAutomation& m_tone_automation;

    // Shared snap-guide sink; empty publishes clear immediately.
    SnapGuideCallback m_snap_guide_callback{};

    // Height-change sink into the track viewport's height-only relayout.
    HeightsChangedCallback m_heights_changed_callback{};

    // Last drawn tracking values keyed like lane heights, so the vblank tick only repaints lanes
    // whose live value actually moved.
    mutable std::map<std::pair<std::string, std::string>, float> m_drawn_tracking_values{};

    // Render-cadence tick that repaints unauthored lanes when their live value moves.
    juce::VBlankAttachment m_vblank_attachment;
};

} // namespace rock_hero::editor::ui
