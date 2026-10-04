/*!
\file highway_projection.h
\brief Projection from the chart domain model to the seconds-resolved highway view state.
*/

#pragma once

#include <rock_hero/common/core/highway/highway_view_state.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Derives the tapping-hand onsets from the resolved notes: which right-hand notes strike
together, and the strike's fret extent and count.

Right-hand presentation is derived, never authored: each entry carries the fret extent, count and
hold end of the right-hand notes struck together at that onset — feeding, for two or more
simultaneous taps, the tapped chord box — and nothing about the light, which lives on the hand
(\ref makePickHandLight). Fretting-hand notes sharing the onset contribute nothing. Notes are judged
on where they SOUND, not on `fret`, so an open-string tap harmonic strikes its node (the reason is
recorded at the grouping walk in `highway_projection.cpp`). A sounding place at or below the nut is
skipped, and one past the last fret is held at the board's edge by \ref highwayDrawnStop, so a
malformed chart cannot place a strike off the board at either end.

\param notes Seconds-resolved notes sorted by start time. A strike's hold keeps the margin rise of
       the head that closes it (\ref NoteViewState::marginRiseSeconds, \ref drawnHoldExtent).
\return Tap onsets in ascending time order.
*/
[[nodiscard]] std::vector<HighwayTapOnsetViewState> makeHighwayTapOnsets(
    const std::vector<NoteViewState>& notes);

/*!
\brief Derives the picking hand's light from the same onset groups \ref makeHighwayTapOnsets
strikes: one track the hand's window travels, and the stretches it is lit over.

THE TRACK is every group's PATH in the board's one motion element (\ref HighwayHandArrival),
concatenated in time order. A group's path is the onset; every stop of the hand's travel a tap's
pitched glides or a scrape's whole keyframe path make, up to and including the first stop past the
ink end, whose leg the light follows on the rail's own curve and settles over the crop zone; and the
release where it extends the path (sustained contact keeps the light on through the sustain). A
tap's unpitched slide-out contributes nothing — pressure is already releasing, so the light decays
from the last pitched arrival. The extent at each arrival spans every member's position on its own
rail at that instant. An earlier group's arrivals at or after the next group's onset are dropped:
the hand has moved on, the same statement the fretting window makes when it shifts while a note
rings. Each group's first arrival is instant; every later arrival ramps over the leg from the
arrival before it.

THE LIT STRETCHES merge one item per tapped member — the group's onset, the member's release, the
member's margin rise — through the same merge the fretting hand's evidence takes
(\ref mergeLitEvidence), under the picking hand's own tolerance (\ref g_pick_light_rest_seconds):
each strike is its own light, the dip between strikes mirroring the finger lifting, and only
strikes that overlap merge.

\param notes Seconds-resolved notes sorted by start time, each carrying its margin rise
       (\ref NoteViewState::marginRiseSeconds).
\return The picking hand's light.
*/
[[nodiscard]] HighwayHandLight makePickHandLight(const std::vector<NoteViewState>& notes);

/*!
\brief Derives when the fretting hand's light is lit: wherever the chart proves the hand holds
something.

The evidence is every note whose onset is not a right-hand onset (fretted, open, dead, natural
harmonic, legato, left tap) — an open string included, because it is drawn as a bar spanning the
window and a dark window under it would read as a floating bar; every right-hand onset whose held
stop is pressed, lit through its claim; and every hand-posture span over its drawn extent. A bare
tap proves nothing. A note lights from its onset, rising over its margin, to its release (the drawn
end, or the last pitched keyframe before a drawn slide-out); a span lights over its drawn extent
with no rise of its own, because a span never OPENS a light — it starts at a note's onset, which
carries the rise, or tiles onto its predecessor. The evidence merges under the one rest tolerance
(\ref mergeLitEvidence, \ref g_hand_rest_seconds).

\param scene The chart scene whose notes and spans are the evidence; each note carries its margin
       rise (\ref NoteViewState::marginRiseSeconds).
\return The lit stretches: disjoint and ascending.
*/
[[nodiscard]] std::vector<HighwayLitStretch> makeFretHandLight(const ChartViewState& scene);

/*!
\brief Composes highway view content: the shared chart scene plus the board's own structure.

The scene is taken, never built: the caller builds it once with \ref makeChartViewState, the one
projection both surfaces share, and the board carries that very value, so the board and the tab
lane cannot read different seconds for one chart revision. On top of it this derives what only the
board draws: section labels, both hands' lights, the tap onsets, the onset groups and their repeat
classification, the beat grid, and the camera framing zones. Build once per chart revision and
share the result immutably: the camera and every drawer are pure functions of the returned state
plus per-frame time.

\param scene The arrangement's chart scene, `makeChartViewState(arrangement, tempo_map)`; moved
       into the returned state. Each note's margin rise is read off it, and the scene of an absent
       chart yields no board structure beyond the sections.
\param tempo_map Tempo map resolving musical positions to seconds.
\param sections Song-structure section markers, resolved into the state's section list even when
       the arrangement has no chart.
\param options Display mapping carried to the renderer: the lefty mirror, the string-order invert,
       and the minimum string count. None of them changes the projected scene — padding and
       reflection are resolved per frame.
\return Seconds-resolved highway content for rendering.
*/
[[nodiscard]] HighwayViewState makeHighwayViewState(
    ChartViewState scene, const TempoMap& tempo_map, const std::vector<SongSection>& sections,
    HighwayDisplayOptions options);

} // namespace rock_hero::common::core
