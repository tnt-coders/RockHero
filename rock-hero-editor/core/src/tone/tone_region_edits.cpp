#include "tone_region_edits.h"

#include <algorithm>
#include <rock_hero/common/core/session/session.h>

namespace rock_hero::editor::core
{

std::expected<void, EditorUndoFailureCode> ToneRegionResizeEdit::undo(
    EditorEditContext& context) const
{
    return applyEndpoints(context, before_start, before_end);
}

std::expected<void, EditorUndoFailureCode> ToneRegionResizeEdit::redo(
    EditorEditContext& context) const
{
    return applyEndpoints(context, after_start, after_end);
}

std::string ToneRegionResizeEdit::label() const
{
    return "Resize " + (region_name.empty() ? std::string{"Tone Region"} : region_name);
}

// Writes the supplied endpoints back onto the tracked region without touching audio state.
std::expected<void, EditorUndoFailureCode> ToneRegionResizeEdit::applyEndpoints(
    EditorEditContext& context, common::core::ToneGridPosition start,
    common::core::ToneGridPosition end) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const auto region = std::ranges::find_if(
        tone_track->regions,
        [this](const common::core::ToneRegion& candidate) { return candidate.id == region_id; });
    if (region == tone_track->regions.end())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    region->start = start;
    region->end = end;
    return std::expected<void, EditorUndoFailureCode>{};
}

} // namespace rock_hero::editor::core
