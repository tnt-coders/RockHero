/*!
\file chart_projection.h
\brief The one projection from an arrangement's chart to its seconds-resolved view state.
*/

#pragma once

#include <cstdint>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/timeline/tempo_map.h>

namespace rock_hero::common::core
{

/*!
\brief Which of a note's two lengths the projected \ref ChartViewState::notes carry.

The two forms differ in \ref ChartViewState::notes and in NOTHING else. Holds, hand-shape spans
and their arrival kinds, fret-hand placements and their approach ramps, the string count and the
capo are all derived from the PRESENTED stream whichever form is asked for, so a surface swapping
forms swaps its note tails and no other mark on it moves.

\ref Actual exists for the editor's Alt reveal alone (\ref makeChartViewState states why no game
surface can reach it) and is not a second opinion about what a note is: positions, strings, frets,
techniques and flags are identical in both, because presentation only ever touches the tail.
*/
enum class ChartNoteForm : std::uint8_t
{
    /*! \brief The presented note (\ref presentedChartNotes): what a surface draws and scores. */
    Presented,

    /*!
    \brief The saved note: the ring the string actually sounds, carrying the payload the
    presentation rules clipped off with the tail.
    */
    Actual,
};

/*!
\brief Projects an arrangement's chart into the seconds-resolved scene both surfaces draw.

Every musical position resolves through the tempo map exactly once here, and every per-note fact
comes from the one resolutions pass (\ref chartResolutions): the PRESENTED stream is what is drawn
(\ref presentedChartNotes — the stored ring is the actual duration the string sounds, and every
tail, payload point and gesture end a surface shows is derived from it), each note's connection
claim arrives resolved, and each note's hold arrives in seconds. The 2D lane renders the result as
is; the 3D highway composes it and adds board-only structure (\ref makeHighwayViewState). An
arrangement without a chart projects an empty state (string_count zero), which renders nothing.

**Scored = presented is a property of these producers, not a rule to remember.**
\ref makeHighwayViewState composes this function with no form argument, so every state a game
surface can obtain is the presented one and \ref ChartNoteForm::Actual is unreachable from the
board, the game, and the scorer (`docs/plans/in-progress/note-sustain-model.md`, ruling 4). The one
caller that asks for the actual form is the editor's 2D reveal, which is not hit-testable and feeds
no scoring path.

\param arrangement Arrangement whose chart should be displayed.
\param tempo_map Tempo map used to resolve musical positions to seconds.
\param form Which length the projected notes carry; everything else is form-invariant.
\return The chart's shared view state.
*/
[[nodiscard]] ChartViewState makeChartViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map,
    ChartNoteForm form = ChartNoteForm::Presented);

} // namespace rock_hero::common::core
