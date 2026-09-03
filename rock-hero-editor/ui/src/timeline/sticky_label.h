/*!
\file sticky_label.h
\brief The two rules a label pinned to a scrolling timeline row obeys: where it sits, when it goes.

Two DIFFERENT laws, each stated once, kept together because a reader reaching for one has to be
able to see that the other is not it. \ref rock_hero::editor::ui::stickyLabelLeft is GEOMETRIC — a
label rides its own anchor and sticks at the window's edge while any of that anchor is on screen.
\ref rock_hero::editor::ui::pinYieldsToIncomingLabel is about SUCCESSION — a row that pins the
value governing its left edge drops that pin when the next value's own label scrolls close enough
to take the edge over. A label can want both (the tone chips want the first, the ruler's value rows
and the tab lane's fret-hand pin want the second) and neither implies the other.
*/

#pragma once

#include <algorithm>
#include <optional>

namespace rock_hero::editor::ui
{

/*!
\brief Left edge of a label that rides its anchor and sticks at the window's left edge.

The rule every pinned label on the scrolling canvas obeys, in one place: the label sits at its
ANCHOR's left edge while that edge is on screen; once the anchor has scrolled left of the window it
sticks to the window's edge, so the thing it names stays named while any of it is visible; and once
the anchor's RIGHT edge passes the window too, the label slides off with it rather than staying
glued to the window over somebody else's territory. The tone regions' names, the automation row's
lane and "+" chips, and any label added later all read this — the right bound was dropped once
already when a caller restated the rule by hand, which left a whole chip column pinned over the
dimmed area past its tone.

All three coordinates live in the same space (the row's own local x, with the window edge pushed in
as a content coordinate); mixing spaces is the caller's error to avoid.

\param anchor_left Left edge of the thing being labelled.
\param anchor_right Right edge of the thing being labelled.
\param window_left Content x of the visible area's left edge.
\return Left edge the label draws at, or no value once the anchor has left the window entirely.
*/
[[nodiscard]] constexpr std::optional<float> stickyLabelLeft(
    float anchor_left, float anchor_right, float window_left) noexcept
{
    if (window_left >= anchor_right)
    {
        return std::nullopt;
    }

    return std::max(anchor_left, window_left);
}

/*!
\brief Clearance a pinned timeline value keeps from the label scrolling in behind it.

One value for every pinned row, because it is one rule: a pinned label reserves its own width plus
this, and a label placed after another keeps this much clear of it. The ruler's rows spend it both
ways (\ref pinYieldsToIncomingLabel and its row placement), and the tab lane's pinned fret-hand
chip spends it the first way, so two chips can never end up abutting on one surface and separated
on another.
*/
inline constexpr int g_pinned_label_gap{10};

/*!
\brief Whether a row's pinned value must yield to the value scrolling in behind it.

The succession half of a pinned row. The pin wins only while the incoming label still fits to its
right; once the incoming label's anchor crosses that boundary the PIN is dropped rather than the
incoming label suppressed, so the new value keeps scrolling to the left edge and takes over as the
pin. Dropping the pin is what makes the handover read as one value replacing another instead of the
old one hanging on over the new one's own column.

The boundary is \ref g_pinned_label_gap past the pinned label's own width: a pin reserved at offset
zero accepts the next anchor only from there on, which is the same reservation any two consecutive
labels in a row make.

Both coordinates are offsets from the PIN's own left edge, so a row whose pin sits at its left edge
passes anchors in its own local x while a row pinning inside a wider component (the tab lane, whose
pin rides the scrolling canvas) subtracts the pin's column first.

\param pinned_width Width of the label the row currently pins.
\param first_incoming_anchor_x Offset of the first upcoming label's anchor from the pin's left
       edge, or no value when nothing is scrolling in.
\return True when the pin must be dropped in favour of the incoming label.
*/
[[nodiscard]] constexpr bool pinYieldsToIncomingLabel(
    int pinned_width, std::optional<int> first_incoming_anchor_x) noexcept
{
    return first_incoming_anchor_x.has_value() &&
           *first_incoming_anchor_x < pinned_width + g_pinned_label_gap;
}

} // namespace rock_hero::editor::ui
