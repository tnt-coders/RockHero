/*!
\file tone_track_edits.h
\brief Pure tone-track edit operations (create, delete, retone, move) that keep the track tiled.
*/

#pragma once

#include <expected>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Finds the region a grid position falls in: the last region starting at or before it.

\param tone_track Tone track to search.
\param position Grid position to resolve.
\return The containing region, or null when \p position precedes the first region.
*/
[[nodiscard]] const ToneRegion* toneRegionAt(const ToneTrack& tone_track, GridPosition position);

/*!
\brief Applies the law that a region boundary IS a tone change.

Removes every region whose tone equals its predecessor's, so consecutive regions never reference
the same tone. The earlier region survives (its id and start stand) and simply runs on to where
the removed one ended. Every edit below ends here, and the package reader runs it once on load, so
a boundary that changes nothing can never be held.

\param tone_track Tone track to normalize in place.
*/
void coalesceToneRegions(ToneTrack& tone_track);

/*!
\brief Creates a new tone region by splitting the region that contains a grid position.

A new region beginning at \p position is inserted after the region containing it, carrying
\p new_region_id and \p new_tone_document_ref, so the tone changes at \p position while the earlier
tone runs up to it. The caller supplies the tone document (a fresh empty tone, or an existing one
to reuse) and validates canonical ids and grid positions separately. Splitting with the containing
region's own tone changes nothing, so the track comes back unchanged.

The new id must be non-empty, which is a precondition rather than a rejection because every caller
mints it. A region is addressed by its id everywhere outside this track — the editor's selection
included — so an empty one would build a region nothing can name or select.

\param tone_track Tone track to modify in place; unchanged on failure.
\param position Grid position at which the tone changes; must fall strictly inside a region.
\param new_region_id Canonical id for the new region beginning at \p position; must be non-empty.
\param new_tone_document_ref Package-relative tone document the new region references.
\return Empty success, or the reason the create was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> createToneRegion(
    ToneTrack& tone_track, GridPosition position, std::string new_region_id,
    std::string new_tone_document_ref);

/*!
\brief Removes a region; the previous region runs on over its span.

When the removed region is the first one, the next region starts where it did instead. Removing
the only region is rejected: the song must always stay covered, so the editor resets the sole
region's tone rather than deleting it. Should the regions either side of the removed one share a
tone, they merge into one.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region to remove.
\return Empty success, or the reason the delete was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> deleteToneRegion(
    ToneTrack& tone_track, const std::string& region_id);

/*!
\brief Points a region at another tone.

A region that comes to share its neighbor's tone merges with it: onto the previous region's tone,
the region itself vanishes into that predecessor; onto the next region's tone, the next region
vanishes into it. Either way the tone the charter asked for now sounds across the whole span.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region to repoint.
\param tone_document_ref Package-relative tone document the region references from now on.
\return Empty success, or the reason the retone was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> retoneToneRegion(
    ToneTrack& tone_track, const std::string& region_id, std::string tone_document_ref);

/*!
\brief Moves the boundary a region opens, keeping it strictly between its neighbors' starts.

The first region's start is the song's own and cannot move. The terminal anchor is not known
here, so a move past it is left to \ref validateToneTrackRules.

\param tone_track Tone track to modify in place; unchanged on failure.
\param region_id Id of the region whose start is the boundary.
\param position New start for the region.
\return Empty success, or the reason the move was rejected.
*/
[[nodiscard]] std::expected<void, ToneTrackError> moveToneBoundary(
    ToneTrack& tone_track, const std::string& region_id, GridPosition position);

} // namespace rock_hero::common::core
