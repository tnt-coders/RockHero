/*!
\file tone_automation_pointer.h
\brief Pointer-intent types the automation lanes forward to the editor controller.

The lanes view converts raw mouse events into these framework-free values; the controller owns the
snap/placement policy — the same tempo-grid seam an Alt+click uses — so the insert ghost lands on
the exact slot a click would, and the policy is testable without JUCE. This is the automation-lane
sibling of the tablature lane's \ref chart_pointer.h contract: the event carries lane-local pixel
coordinates plus the geometry the pixels were mapped against, so the controller re-derives the
click time itself (through timelinePositionForX) rather than trusting a view-computed time.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/timeline/timeline.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief Which phase of a pointer gesture the automation lanes are forwarding. */
enum class ToneAutomationPointerPhase : std::uint8_t
{
    /*! \brief Primary button pressed inside a lane: grabs a point or starts an Alt-insert/caret. */
    Down,

    /*! \brief Pointer moved while the button is held, advancing the move/insert drag preview. */
    Drag,

    /*! \brief Button released, ending the gesture (commit a move/insert, or select on a click). */
    Up,

    /*! \brief Pointer moved with no button held (a hover), driving the Alt insert ghost. */
    Move,

    /*! \brief Pointer left the lanes, clearing any hover affordance. */
    Exit,
};

/*! \brief Modifier keys held during an automation-lane pointer event. */
struct ToneAutomationPointerModifiers
{
    /*! \brief Precision: placement snap bypasses the visible grid to the 1/960-beat fine tier. */
    bool ctrl{false};

    /*!
    \brief Authoring: over empty lane area, the neutral-create gate — a hover shows the on-curve
    insert ring and a press plants an on-curve point (§9b's Insert verb in its mouse form). Without
    it a lane-area hover publishes no ghost.
    */
    bool alt{false};

    /*! \brief Extend: constrains a point drag to its dominant axis, anchored at the gesture start. */
    bool shift{false};
};

/*!
\brief One lane's value-band pixel span, the y↔value mapping the controller's drag needs.

Computed view-side (the value-band inset and resize-band height are pure-view layout constants, so
they never leak into editor-core): the band is the drawable region a point rides, already inset
from the lane's outer bounds. The controller maps a lane-local pixel y to a normalised value with
just these two numbers — value = clamp(1 - (y - top) / height, 0, 1) — exactly as the lanes view
does, so the ported drag pulls the value bit-for-bit as the view did. The lane sibling of the
tablature lane's \ref common::ui::TabLaneGeometry laneY, which likewise carries the view-computed
pixel layout so the controller hit-tests against the pixels on screen.
*/
struct ToneAutomationLaneExtent
{
    /*! \brief Top pixel of the lane's value band (already inset from the lane's outer bounds). */
    float value_band_top{0.0F};

    /*! \brief Height in pixels of the lane's value band (at least one pixel). */
    float value_band_height{0.0F};
};

/*!
\brief Lane geometry one pointer event was mapped against.

The full content width maps the visible timeline across the lanes, so the controller can invert a
lane-local pixel x back to a click time with exactly the mapping the placement path uses.
*/
struct ToneAutomationLaneGeometry
{
    /*! \brief Timeline range represented by the full content width. */
    common::core::TimeRange visible_timeline{};

    /*! \brief Full content width in pixels. */
    int content_width{0};
};

/*!
\brief One pointer event over an automation lane, in lane-local pixel coordinates.

Modeled on \ref ChartPointerEvent (framework-free pixel coordinates + geometry + modifiers), with
the hovered/pressed lane's identity carried alongside: unlike the chart, the lanes view resolves
which lane the pointer is over (its per-lane heights are a pure-view concern) and names it here, so
the controller re-resolves only the point-vs-empty-area hit and owns the snap, value, and gesture
policy.

The lane identity and vertical geometry (\ref lane_index plus \ref lane_extents) are populated for
the \ref ToneAutomationPointerPhase::Down and \ref ToneAutomationPointerPhase::Move phases, where a
fresh hit-test resolves them. A Drag/Up rides the gesture the controller froze on Down, so those
phases carry only the live pointer (\ref x / \ref y) and \ref modifiers and leave the lane geometry
default.
*/
struct ToneAutomationPointerEvent
{
    /*! \brief Plugin instance owning the hovered/pressed lane's parameter (resolved by hitAt). */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief Geometry the pointer was mapped against, so the controller snaps as a click would. */
    ToneAutomationLaneGeometry geometry{};

    /*!
    \brief Value-band pixel span of every displayed lane, in published-lane display order.

    Matches \ref ToneAutomationViewState::lanes so the controller indexes it by \ref lane_index to
    map the pressed lane's pixel y onto a normalised value. The trailing "+" lane is not included.
    */
    std::vector<ToneAutomationLaneExtent> lane_extents;

    /*! \brief Published index of the hovered/pressed lane (indexes \ref lane_extents). */
    std::size_t lane_index{0};

    /*!
    \brief True when the pressed lane's parameter is stepped rather than continuous.

    Carried from the published lane so a point drag on a discrete lane snaps its pull to the real
    states without the controller re-scanning the chain's parameters on every press.
    */
    bool lane_is_discrete{false};

    /*! \brief Number of discrete steps for a stepped parameter; 0 for a continuous one. */
    int lane_discrete_value_count{0};

    /*! \brief Pointer x in lane-local pixels. */
    float x{0.0F};

    /*! \brief Pointer y in lane-local pixels (the drag's delta-value axis). */
    float y{0.0F};

    /*!
    \brief Consecutive-click count of the gesture (1 = single click, 2 = double click).

    A press whose count is two or more is the value editor's double-click, owned view-side; the
    controller ignores it so the second press never arms a stray drag.
    */
    int clicks{1};

    /*!
    \brief True once the gesture has crossed the framework's click→drag threshold (a Drag phase).

    Carried straight from JUCE's `MouseEvent::mouseWasDraggedSinceMouseDown` so the controller's
    click-vs-move decision matches the shipped view bit-for-bit — both the ~4-pixel travel and the
    long-press-past-the-double-click-timeout it folds in — rather than re-deriving a pixel threshold
    that would drop the timing component. An existing-point grab stays a selecting click until this
    turns true; a new-point insert moves from the press and ignores it.
    */
    bool dragged_since_down{false};

    /*! \brief Modifier keys held during the event. */
    ToneAutomationPointerModifiers modifiers{};
};

} // namespace rock_hero::editor::core
