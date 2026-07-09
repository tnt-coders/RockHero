#include "tone_region_edits.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <string>
#include <vector>

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

std::expected<void, EditorUndoFailureCode> ToneRegionCreateEdit::undo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    // Undoing a create removes the region that began at the marker; deleteToneRegion merges its
    // span back into the region it was split from, restoring the original single span.
    if (!deleteToneRegion(*tone_track, new_region_id).has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

std::expected<void, EditorUndoFailureCode> ToneRegionCreateEdit::redo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    if (!createToneRegion(*tone_track, position, new_region_id, tone_document_ref).has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

std::string ToneRegionCreateEdit::label() const
{
    return "Insert " + (tone_name.empty() ? std::string{"Tone Change"} : tone_name);
}

std::expected<void, EditorUndoFailureCode> ToneRegionDeleteEdit::undo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    std::vector<common::core::ToneRegion>& regions = tone_track->regions;
    if (removed_index > regions.size())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    // Shrink the neighbor that had absorbed the removed span back to its original endpoint, then
    // reinsert the removed region at its original index so coverage matches the pre-delete state.
    if (absorbed_by_prev)
    {
        if (removed_index == 0 || removed_index - 1 >= regions.size())
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        regions[removed_index - 1].end = removed_region.start;
    }
    else
    {
        if (removed_index >= regions.size())
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        regions[removed_index].start = removed_region.end;
    }

    regions.insert(regions.begin() + static_cast<std::ptrdiff_t>(removed_index), removed_region);

    // Restore the catalog tone the delete pruned so the restored region's reference resolves to
    // a named tone again.
    if (removed_catalog_tone.has_value())
    {
        std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
        if (catalog == nullptr)
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        const bool present =
            std::ranges::any_of(*catalog, [this](const common::core::Tone& candidate) {
                return candidate.tone_document_ref == removed_catalog_tone->tone_document_ref;
            });
        if (!present)
        {
            catalog->push_back(*removed_catalog_tone);
        }
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

std::expected<void, EditorUndoFailureCode> ToneRegionDeleteEdit::redo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    if (!deleteToneRegion(*tone_track, removed_region.id).has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    // Re-prune the catalog tone whose last reference this delete removes.
    if (removed_catalog_tone.has_value())
    {
        if (std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
            catalog != nullptr)
        {
            std::erase_if(*catalog, [this](const common::core::Tone& candidate) {
                return candidate.tone_document_ref == removed_catalog_tone->tone_document_ref;
            });
        }
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

std::string ToneRegionDeleteEdit::label() const
{
    return "Delete " + (region_name.empty() ? std::string{"Tone Region"} : region_name);
}

std::expected<void, EditorUndoFailureCode> ToneRenameEdit::undo(EditorEditContext& context) const
{
    return applyName(context, before_name);
}

std::expected<void, EditorUndoFailureCode> ToneRenameEdit::redo(EditorEditContext& context) const
{
    return applyName(context, after_name);
}

std::string ToneRenameEdit::label() const
{
    const std::string from = before_name.empty() ? std::string{"<unknown>"} : before_name;
    const std::string to = after_name.empty() ? std::string{"<unknown>"} : after_name;
    return "Rename Tone " + from + " to " + to;
}

// Writes the supplied name onto the catalog tone identified by tone_document_ref.
std::expected<void, EditorUndoFailureCode> ToneRenameEdit::applyName(
    EditorEditContext& context, const std::string& name) const
{
    std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
    if (catalog == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const auto tone = std::ranges::find_if(*catalog, [this](const common::core::Tone& candidate) {
        return candidate.tone_document_ref == tone_document_ref;
    });
    if (tone == catalog->end())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    tone->name = name;
    return std::expected<void, EditorUndoFailureCode>{};
}

std::expected<void, EditorUndoFailureCode> ToneBoundaryMoveEdit::undo(
    EditorEditContext& context) const
{
    return applyBoundary(context, before_position);
}

std::expected<void, EditorUndoFailureCode> ToneBoundaryMoveEdit::redo(
    EditorEditContext& context) const
{
    return applyBoundary(context, after_position);
}

std::string ToneBoundaryMoveEdit::label() const
{
    return "Move Tone Boundary";
}

// Snaps both sides of the shared boundary (the right region's start and its predecessor's end) to
// the given position, so coverage stays gap-free.
std::expected<void, EditorUndoFailureCode> ToneBoundaryMoveEdit::applyBoundary(
    EditorEditContext& context, common::core::ToneGridPosition position) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const auto right = std::ranges::find_if(
        tone_track->regions, [this](const common::core::ToneRegion& candidate) {
            return candidate.id == right_region_id;
        });
    if (right == tone_track->regions.end() || right == tone_track->regions.begin())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    std::prev(right)->end = position;
    right->start = position;
    return std::expected<void, EditorUndoFailureCode>{};
}

std::expected<void, EditorUndoFailureCode> ToneCreateWithNewToneEdit::undo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    // Remove the created region first (merging its span back into its predecessor), then drop the
    // catalog tone this edit added. The minted document file is intentionally left in place.
    if (!deleteToneRegion(*tone_track, new_region_id).has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    std::erase_if(*catalog, [this](const common::core::Tone& tone) {
        return tone.tone_document_ref == tone_document_ref;
    });
    return std::expected<void, EditorUndoFailureCode>{};
}

std::expected<void, EditorUndoFailureCode> ToneCreateWithNewToneEdit::redo(
    EditorEditContext& context) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    // Recreate the split region first, then re-add the catalog tone it references.
    if (!createToneRegion(*tone_track, position, new_region_id, tone_document_ref).has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    const bool has_tone = std::ranges::any_of(*catalog, [this](const common::core::Tone& tone) {
        return tone.tone_document_ref == tone_document_ref;
    });
    if (!has_tone)
    {
        catalog->push_back(
            common::core::Tone{.tone_document_ref = tone_document_ref, .name = name});
    }
    return std::expected<void, EditorUndoFailureCode>{};
}

std::string ToneCreateWithNewToneEdit::label() const
{
    return "Add " + (name.empty() ? std::string{"Tone"} : name);
}

std::expected<void, EditorUndoFailureCode> ToneResetEdit::undo(EditorEditContext& context) const
{
    return applyReset(context, before_ref, after_ref, before_ref, before_name);
}

std::expected<void, EditorUndoFailureCode> ToneResetEdit::redo(EditorEditContext& context) const
{
    return applyReset(context, after_ref, before_ref, after_ref, "Default");
}

std::string ToneResetEdit::label() const
{
    return "Reset Tone";
}

// Repoints the sole region to region_ref and rewrites the catalog entry currently keyed by
// catalog_from so it becomes {catalog_to, catalog_name}, keeping region and catalog in sync.
std::expected<void, EditorUndoFailureCode> ToneResetEdit::applyReset(
    EditorEditContext& context, const std::string& region_ref, const std::string& catalog_from,
    const std::string& catalog_to, const std::string& catalog_name) const
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    const auto region = std::ranges::find_if(
        tone_track->regions,
        [this](const common::core::ToneRegion& candidate) { return candidate.id == region_id; });
    const auto tone =
        std::ranges::find_if(*catalog, [&catalog_from](const common::core::Tone& candidate) {
            return candidate.tone_document_ref == catalog_from;
        });
    if (region == tone_track->regions.end() || tone == catalog->end())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }

    region->tone_document_ref = region_ref;
    tone->tone_document_ref = catalog_to;
    tone->name = catalog_name;
    return std::expected<void, EditorUndoFailureCode>{};
}

} // namespace rock_hero::editor::core
