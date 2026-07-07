/*!
\file tone_track_normalize.h
\brief Normalizes an arrangement's tone model so every tone track owns an explicit region.
*/

#pragma once

namespace rock_hero::common::core
{

struct Song;

/*!
\brief Materializes the implicit whole-song tone region for every tone-bearing arrangement.

Rock Hero's tone model treats a tone track as always covered by explicit regions: the editor splits,
selects, and deletes regions uniformly, with no synthesized "default" special case. A freshly
imported song may carry a tone catalog and no regions, so this normalizer, run once as a song
enters the editor, gives each such arrangement a single region spanning `[1.1, terminal)` that
references the first catalog tone. Arrangements with an empty catalog are left untouched (the load
baseline mints a default tone before this runs), as are arrangements that already own regions.

\param song Song whose arrangements are normalized in place.
*/
void ensureExplicitToneRegions(Song& song);

} // namespace rock_hero::common::core
