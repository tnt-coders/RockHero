/*!
\file tone_track_normalize.h
\brief Normalizes an arrangement's tone model so every tone track owns an explicit region.
*/

#pragma once

namespace rock_hero::common::core
{

struct Song;

/*!
\brief Materializes the implicit whole-song tone region and its catalog tone for every arrangement.

Rock Hero's tone model treats a tone track as always covered by explicit regions: the editor splits,
selects, and deletes regions uniformly, with no synthesized "default" special case. A freshly
imported or legacy song may still carry only an arrangement-level default tone reference and no
regions, so this normalizer, run once as a song enters the editor, gives each tone-bearing
arrangement a single region spanning `[1.1, terminal)` that references the default tone, and ensures
that tone appears in the arrangement's catalog so the region has a name. Arrangements without a
default tone reference are left untouched, since there is no tone to anchor a region to.

\param song Song whose arrangements are normalized in place.
*/
void ensureExplicitToneRegions(Song& song);

} // namespace rock_hero::common::core
