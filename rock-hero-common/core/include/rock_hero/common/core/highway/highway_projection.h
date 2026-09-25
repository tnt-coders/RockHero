/*!
\file highway_projection.h
\brief Projection from the chart domain model to the seconds-resolved highway view state.
*/

#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Resolves an arrangement to highway view content: the shared chart scene plus the board's
own structure.

The chart scene comes from \ref makeChartViewState, the one projection both surfaces share, so the
board and the tab lane read identical seconds for identical inputs by construction. On top of it
this derives what only the board draws: section labels, the picking-hand light onsets, the onset
groups and their repeat classification, the beat grid, and the camera framing zones. Build once
per chart load and share the result immutably: the camera and every drawer are pure functions of
the returned state plus per-frame time.

\param arrangement Arrangement whose loaded chart is projected; an absent chart yields an empty
       scene.
\param tempo_map Tempo map resolving musical positions to seconds.
\param sections Song-structure section markers, resolved into the state's section list even when
       the arrangement has no chart.
\param options Display mapping carried to the renderer: the lefty mirror, the string-order invert,
       and the minimum string count. None of them changes the projected scene — padding and
       reflection are resolved per frame.
\return Seconds-resolved highway content for rendering.
*/
/*!
\brief Derives the tapping-hand onsets from the resolved notes: which right-hand notes strike
together, the light path each group's window follows, and the light's rise and release.

Right-hand presentation is derived, never authored: each entry carries the fret extent and
count of the right-hand notes struck together at that onset — feeding, for two or more
simultaneous taps, the tapped chord box — plus the PATH the light follows in the board's one
motion element (\ref HighwayHandArrival): the onset; every stop of the hand's travel a tap's
pitched glides or a scrape's whole keyframe path make, up to and including the first stop past
the ink end, whose leg the light follows on the rail's own curve to its arrival;
and the release where it extends the path (sustained contact keeps the light on through the
sustain). A tap's unpitched slide-out contributes nothing — pressure is already releasing, so the
light decays from the last pitched arrival. The extent at each arrival spans every member's
position on its own rail at that instant. Fretting-hand notes sharing the onset contribute
nothing. Notes are judged on where they SOUND, not on `fret`: an open-string tap harmonic strikes
its node, and reading `fret` instead dropped the light from a note the rules explicitly allow.
A sounding place at or below the nut is skipped, and one past the last fret is held at the board's
edge by \ref highwayDrawnStop, so a malformed chart cannot place a light off the board at either
end.

Each onset also carries a light-rise duration, derived with the fret-hand placements' own arrival
rule: the caller supplies each note's margin-based rise (the minimum-sustain-distance margin
before the note, in seconds — zero for non-tap notes), the onset takes the widest member's, and
crowding clamps the rise so it never reaches backward past the previous tap onset's release.

\param notes Seconds-resolved notes sorted by start time.
\param note_rise_seconds Per-note margin rise duration in seconds, sized and ordered like notes.
\return Tap onsets in ascending time order, each with at least one path arrival.
*/
[[nodiscard]] std::vector<HighwayTapOnsetViewState> makeHighwayTapOnsets(
    const std::vector<NoteViewState>& notes, const std::vector<double>& note_rise_seconds);

[[nodiscard]] HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map,
    const std::vector<SongSection>& sections, HighwayDisplayOptions options);

} // namespace rock_hero::common::core
