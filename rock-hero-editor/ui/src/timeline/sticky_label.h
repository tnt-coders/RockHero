/*!
\file sticky_label.h
\brief Where a label pinned to a scrolling timeline row sits: the one statement of the sticky rule.
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

} // namespace rock_hero::editor::ui
