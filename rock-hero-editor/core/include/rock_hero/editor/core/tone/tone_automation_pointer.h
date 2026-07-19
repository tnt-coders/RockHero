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

namespace rock_hero::editor::core
{

/*! \brief Which phase of a pointer gesture the automation lanes are forwarding. */
enum class ToneAutomationPointerPhase : std::uint8_t
{
    /*! \brief Primary button pressed inside a lane (reserved for the drag pipeline, Phase 3). */
    Down,

    /*! \brief Pointer moved while the button is held (reserved for the drag pipeline, Phase 3). */
    Drag,

    /*! \brief Button released, ending the gesture (reserved for the drag pipeline, Phase 3). */
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
the hovered lane's identity carried alongside: unlike the chart, the lanes view resolves which lane
the pointer is over (its per-lane heights are a pure-view concern) and names it here, so the
controller need only own the horizontal snap.
*/
struct ToneAutomationPointerEvent
{
    /*! \brief Plugin instance owning the hovered lane's parameter (resolved view-side by hitAt). */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief Geometry the pointer was mapped against, so the controller snaps as a click would. */
    ToneAutomationLaneGeometry geometry{};

    /*! \brief Pointer x in lane-local pixels. */
    float x{0.0F};

    /*! \brief Pointer y in lane-local pixels (reserved for the drag pipeline's value, Phase 3). */
    float y{0.0F};

    /*! \brief Modifier keys held during the event. */
    ToneAutomationPointerModifiers modifiers{};
};

} // namespace rock_hero::editor::core
