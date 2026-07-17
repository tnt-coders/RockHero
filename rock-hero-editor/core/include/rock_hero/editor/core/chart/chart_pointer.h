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
};

/*! \brief Modifier keys held during a chart pointer event, per the editing interaction model. */
struct ChartPointerModifiers
{
    /*! \brief Precision: bypass grid snap onto the fine grid. */
    bool ctrl{false};

    /*! \brief Extend: add to the selection instead of replacing it. */
    bool shift{false};

    /*! \brief Create quasimode; reserved for note insertion and ignored by selection. */
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
};

/*! \brief Direction of a keyboard caret move in the tablature lane. */
enum class ChartCaretDirection : std::uint8_t
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
