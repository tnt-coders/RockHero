/*!
\file chart_pointer.h
\brief Pointer-intent types the tablature lane forwards to the editor controller.

The lane view converts raw mouse events into these framework-free values; the controller owns
every hit-testing and gesture-policy decision (glyph select vs. seek vs. marquee) so the policy
is testable without JUCE. The carried lane geometry is the same TabLaneGeometry the shared paint
core drew with, so hits resolve against exactly the pixels on screen.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>

namespace rock_hero::editor::core
{

/*! \brief Which phase of a pointer gesture the lane view is forwarding. */
enum class ChartPointerPhase : std::uint8_t
{
    /*! \brief Primary button pressed inside the lane. */
    Down,

    /*! \brief Pointer moved while the button is held. */
    Drag,

    /*! \brief Button released, ending the gesture. */
    Up,

    /*! \brief Pointer moved with no button held (a hover), driving the Alt insert ghost. */
    Move,

    /*! \brief Pointer left the lane, clearing any hover affordance. */
    Exit,
};

/*! \brief Modifier keys held during a chart pointer event, per the editing interaction model. */
struct ChartPointerModifiers
{
    /*!
    \brief Toggle individual selection membership on a note press; on a playing-lane seek,
    the ruler-family fine bypass. Chart placement itself is grid-native (settlement §11) and
    has no precision tier.
    */
    bool ctrl{false};

    /*! \brief Extend: a marquee release adds its box to the selection instead of replacing. */
    bool shift{false};

    /*!
    \brief Authoring: on an empty slot, Alt is the neutral-create gesture — a hover shows the
    insert ring and a press plants a fret-0 note there (§9b's Insert verb in its mouse form,
    the chart sibling of the automation lane's on-curve Alt+click). Ignored over a note, where
    the slot is occupied and the press keeps its select/(future) move meaning.
    */
    bool alt{false};
};

/*! \brief One pointer event inside the tablature lane, in lane-local pixel coordinates. */
struct ChartPointerEvent
{
    /*! \brief Lane geometry the notation was painted with when the event fired. */
    common::ui::TabLaneGeometry geometry{};

    /*! \brief Pointer x in lane-local pixels. */
    float x{};

    /*! \brief Pointer y in lane-local pixels. */
    float y{};

    /*! \brief Modifier keys held during the event. */
    ChartPointerModifiers modifiers{};

    /*!
    \brief Consecutive-click count of the gesture (1 = single click, 2 = double click).

    Selection granularity follows the containment hierarchy (settled 2026-07-17): a single
    click selects the individual note, a double click its whole onset group.
    */
    int clicks{1};
};

/*! \brief Direction of an arrow-key step in the tablature lane. */
enum class ChartStepDirection : std::uint8_t
{
    /*! \brief One grid step earlier. */
    Left,

    /*! \brief One grid step later. */
    Right,

    /*! \brief One string lane higher in pitch. */
    Up,

    /*! \brief One string lane lower in pitch. */
    Down,
};

} // namespace rock_hero::editor::core
