/*!
\file tone_automation_lanes_view.h
\brief Automation lanes beneath the tone row: one lane per automated parameter plus a "+" lane.

One component paints every lane (dynamic count, per-lane vertical resize, dirty-rect locality) and
the trailing empty lane whose pinned "+" chip opens the parameter picker. Gestures preview locally
and commit one full-point-list intent on release; a state push mid-gesture drops the gesture. The
lanes never resize themselves: heights flow up through a callback and the track viewport lays the
component out, so the cursor overlay and content height stay authoritative.
*/

#pragma once

#include "timeline/cursor_overlay.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <rock_hero/common/audio/automation/i_tone_automation.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_automation.h>
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
    \brief Replaces the rendered automation state, dropping any in-flight gesture.
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
    click-to-seek: point handles, editable lane areas inside the window, resize bands, and the "+"
    chip claim the pointer; disabled lanes, out-of-window areas, and empty strip space do not.

    \param local_point Position in this component's coordinates.
    \return True when the pointer should reach the lanes instead of the cursor overlay.
    */
    [[nodiscard]] bool wantsPointerAt(juce::Point<int> local_point) const;

    /*! \brief Paints every lane, its curve and points, and the trailing "+" lane. */
    void paint(juce::Graphics& graphics) override;

    /*! \brief Updates the hover cursor for the zone under the pointer. */
    void mouseMove(const juce::MouseEvent& event) override;

    /*! \brief Classifies and starts a gesture: add point, move point, resize lane, or menu. */
    void mouseDown(const juce::MouseEvent& event) override;

    /*! \brief Advances the active gesture's local preview. */
    void mouseDrag(const juce::MouseEvent& event) override;

    /*! \brief Commits the active gesture as one intent when it changed anything. */
    void mouseUp(const juce::MouseEvent& event) override;

    /*! \brief Resets a double-clicked point to the parameter's default value. */
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    /*! \brief Clears the hovered-point value readout when the pointer leaves without dragging. */
    void mouseExit(const juce::MouseEvent& event) override;

    /*!
    \brief Reports the value-readout text currently shown next to the cursor, for tests.
    \return Readout text while a point is hovered or dragged, or empty when none is shown.
    */
    [[nodiscard]] std::optional<juce::String> valueReadoutTextForTest() const;

private:
    // One lane's vertical extent in component coordinates.
    struct LaneExtent
    {
        int top{0};
        int height{0};
    };

    // Gesture kinds classified once in mouseDown and dispatched on in mouseDrag/mouseUp.
    struct MovePointDrag
    {
        std::size_t lane_index{};
        std::size_t point_index{};
        common::core::GridPosition preview_position{};
        float preview_value{};
        bool moved{false};
        bool is_new_point{false};
    };
    struct ResizeLaneDrag
    {
        std::size_t lane_index{};
        int start_height{};
        int start_y{};
    };
    using DragState = std::variant<MovePointDrag, ResizeLaneDrag>;

    // Transient "position · value" chip drawn next to the cursor while a point is hovered or
    // dragged, so the value the gesture is producing is legible without a separate panel.
    struct ValueReadout
    {
        juce::Point<int> anchor{};
        juce::String text;
        friend bool operator==(const ValueReadout& lhs, const ValueReadout& rhs) = default;
    };

    // Hit zones resolved by hitAt(); the pass-through predicate and mouseDown share this result.
    struct PointHit
    {
        std::size_t lane_index{};
        std::size_t point_index{};
    };
    struct LaneAreaHit
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
    using Hit = std::variant<PointHit, LaneAreaHit, ResizeBandHit, PlusChipHit>;

    // Resolves the interactive zone at a local point, or nullopt for pass-through space.
    [[nodiscard]] std::optional<Hit> hitAt(juce::Point<int> local_point) const;

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

    // Converts a lane-local y to a normalised value, clamped to the value band.
    [[nodiscard]] float valueForY(int y, const LaneExtent& extent) const;

    // Snaps a raw normalised value to the nearest discrete step for a stepped parameter (so a
    // toggle moves only between its states); returns the value unchanged for a continuous lane.
    [[nodiscard]] static float snappedValueForLane(
        float raw_value, const core::ToneAutomationLaneViewState& lane);

    // Builds the committed point list for the active move-point drag.
    [[nodiscard]] std::vector<common::core::ToneAutomationPoint> pointsForCommit(
        const MovePointDrag& drag) const;

    // Opens the "+" parameter picker as an async popup menu, grouped per chain plugin.
    void showParameterPicker();

    // Opens the delete menu for a right-clicked point.
    void showPointMenu(const PointHit& hit);

    // Opens the remove menu for a right-clicked unauthored tracking lane.
    void showLaneMenu(std::size_t lane_index);

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

    // Active gesture, if any; state pushes drop it instead of resizing against stale indices.
    std::optional<DragState> m_drag{};

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
