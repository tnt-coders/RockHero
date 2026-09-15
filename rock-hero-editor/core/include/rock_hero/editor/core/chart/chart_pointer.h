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
    /*!
    \brief Toggle individual selection membership on a note press.

    Its only meaning on this surface. Placement composes nothing from it: every position lands on
    the one placement quantum, which grid snap alone moves.
    */
    bool ctrl{false};

    /*! \brief Extend: a marquee release adds its box to the selection instead of replacing. */
    bool shift{false};

    /*!
    \brief Reveal: Alt shows the satellites a press may then address, and creates nothing.

    A CLICK NEVER CREATES on this lane, whatever modifiers it carries — every object here is
    TYPED, so the pointer's whole job is to say where the next digit lands. Alt's meaning is the
    reveal alone, which is what makes a satellite it brought in clickable while it is drawn.
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

    Selection granularity follows the containment hierarchy: a single click selects the individual
    note, a double click its whole onset group. Selection's alone: no click count authors anything.
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

/*!
\brief A caret leap to a derived musical position (the Home/End and PageUp/Down family).

Unlike ChartStepDirection's one-grid-step moves, each value names an absolute or section-relative
destination the controller resolves from the tempo map and song sections. All four are horizontal
reach: they move the caret in time and keep its row (string or lane).
*/
enum class ChartCaretJump : std::uint8_t
{
    /*! \brief The chart's first position (measure 1, beat 1) — Home. */
    ChartStart,

    /*! \brief The chart's terminal downbeat — End. */
    ChartEnd,

    /*! \brief The nearest section starting before the caret — PageUp. */
    PreviousSection,

    /*! \brief The nearest section starting after the caret — PageDown. */
    NextSection,
};

/*!
\brief A row the keyboard jumps onto by selection (the `Ctrl+Shift`+letter family).

Each value names one of the rows the focus-row walk reaches by selecting rather than by arming a
caret: the ruler's three marker rows, the tone row, and the "+" row beneath the lanes. A jump lands
exactly as the walk does — it selects the marker holding the cursor and demotes the caret in place
— and where the row has nothing to hold the cursor (a song with no sections) it selects nothing,
silently. The string and lane rows need no jump: the arrows return to the row the caret was last
armed on.
*/
enum class FocusRowJump : std::uint8_t
{
    /*! \brief The ruler's section row — `Ctrl+Shift+M`. */
    Section,

    /*! \brief The ruler's tempo row — `Ctrl+Shift+B`. */
    Tempo,

    /*! \brief The ruler's time-signature row — `Ctrl+Shift+/`. */
    TimeSignature,

    /*! \brief The tone row — `Ctrl+Shift+T`. */
    Tone,

    /*! \brief The "+" row that adds an automation lane — `Ctrl+Shift+A`. */
    AddAutomationLane,
};

/*!
\brief The unit a Shift+arrow time-selection extend grows the range by.

Each value pairs with a ChartStepDirection (Left = earlier, Right = later) to name the moving
edge's destination — the same motions the caret jumps use, applied to the range's focus edge
instead of the caret. ChartBound resolves to the chart start (Left) or end (Right).
*/
enum class TimeSelectionExtent : std::uint8_t
{
    /*! \brief One display-grid step — Shift+Left/Right. */
    Grid,

    /*! \brief One measure — Shift+Ctrl+Left/Right. */
    Measure,

    /*! \brief The previous/next section — Shift+PageUp/PageDown. */
    Section,

    /*! \brief The chart start/end — Shift+Home/End. */
    ChartBound,
};

} // namespace rock_hero::editor::core
