/*!
\file tone_track_edits.h
\brief Pure tone-track edit operations (create, delete) that preserve gap-free coverage.
*/

#pragma once

#include <expected>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Creates a new tone region by splitting the region that contains a grid position.

The region containing \p at is shrunk to `[start, at)`; a new region `[at, end)` is inserted
immediately after it, carrying \p new_region_id and \p new_tone_document_ref, so the tone changes at
\p at while the earlier tone runs up to it. Because the split is adjacent and the outer bounds are
untouched, coverage stays gap-free by construction. The caller supplies the tone document (a fresh
empty tone, or an existing one to reuse) and validates canonical ids and grid endpoints separately.

\param tone_track Tone track to modify in place; unchanged on failure.
\param at Grid position at which the tone changes; must fall strictly inside a region.
\param new_region_id Canonical id for the new region beginning at \p at.
\param new_tone_document_ref Package-relative tone document the new region references.
\return Empty success, or the reason the create was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> createToneRegion(
    ToneTrack& tone_track, ToneGridPosition at, std::string new_region_id,
    std::string new_tone_document_ref);

/*!
\brief Removes a region, extending a neighbor to fill its span so coverage is preserved.

The previous region's end is extended over the removed span. When the removed region is the first
one (no previous), the next region's start is instead extended back. Removing the only region is
rejected: the song must always stay covered, so the editor resets the sole region's tone rather than
deleting it.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region to remove.
\return Empty success, or the reason the delete was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> deleteToneRegion(
    ToneTrack& tone_track, const std::string& region_id);

} // namespace rock_hero::common::core
