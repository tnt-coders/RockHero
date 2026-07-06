/*!
\file tone_track_edits.h
\brief Pure tone-track edit operations (slice, delete) that preserve gap-free coverage.
*/

#pragma once

#include <expected>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Splits a region into two adjacent regions at a grid position.

The region named by \p region_id is shrunk to `[start, cut)`; a new region `[cut, end)` is inserted
immediately after it, carrying \p new_region_id, the source region's name, and \p
new_tone_document_ref. Because the split is adjacent and the outer bounds are untouched, coverage is
preserved: no gap can appear. Full structural validation (canonical ids, grid endpoints, terminal
bound) remains the caller's responsibility via validateToneTrackRules.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region to split.
\param cut Grid position to split at; must fall strictly inside the region.
\param new_region_id Canonical id for the new right-hand region.
\param new_tone_document_ref Package-relative tone document for the new right-hand region.
\return Empty success, or the reason the slice was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> sliceToneRegion(
    ToneTrack& tone_track, const std::string& region_id, ToneGridPosition cut,
    std::string new_region_id, std::string new_tone_document_ref);

/*!
\brief Removes a region, extending a neighbor to fill its span so coverage is preserved.

The previous region's end is extended to the removed region's end. When the removed region is the
first one (no previous), the next region's start is instead extended back to the removed region's
start. When it is the only region, the track becomes empty, which means the arrangement falls back
to its whole-song default tone.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region to remove.
\return Empty success, or the reason the delete was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> deleteToneRegion(
    ToneTrack& tone_track, const std::string& region_id);

} // namespace rock_hero::common::core
