/*!
\file highway_diagnostics_options.h
\brief Draw-time diagnostics switches for the highway renderer's editor-only sighting rigs.
*/

#pragma once

#include <cstdint>

namespace rock_hero::common::ui
{

/*!
\brief Which form the actual-ring diagnostics mark takes on the board floor.

ONE switch for how the ring is shown rather than a light with its own toggle beside a band with
another: every form marks the same notes over the same span and differs only in what it paints
there, so a second switch could only ever produce two marks stacked on one ring.
*/
enum class ActualRingLook : std::uint8_t
{
    /*! \brief Nothing is drawn; the value every shipped surface runs with. */
    Off,

    /*! \brief A soft additive light in the note's own string color, following the note's glide. */
    Light,

    /*! \brief A translucent white body over the whole ring, with a brighter cap at its end. */
    Fill,

    /*!
    \brief Two thin white rails along the mark's edges and the same cap, leaving the floor clear.
    */
    Outline
};

/*!
\brief Draw-time diagnostics switches for the editor's sighting rigs.

Deliberately NOT part of common::core::HighwayDisplayOptions. Those ride the memoized
HighwayViewState, so changing one re-projects the whole chart; these change nothing but what the
next frame emits, and a toggle a user presses to look at something must not rebuild the scene it
is looking at.

**The game cannot switch these on, and that is a composition-root guarantee rather than a
compile-time one.** The only caller of \ref HighwayRenderer::setDiagnosticsOptions is the editor's
preview surface, reached from an editor command id the game does not link — so the game's board is
the default-constructed value and nothing in the game binary can name another. The project ruled
AGAINST compiling diagnostics out of shipped builds (game/core `diagnostics.h`, plan 20 open
question 5, answer A: release-build timing bugs have to stay observable), so there is deliberately
no build define gating this either.

Its own header rather than a corner of the renderer's: the pure rules beside the renderer and the
editor's preview plumbing name this POD without needing the renderer's whole API, so neither has to
pull the shader and texture sets along with it.
*/
struct HighwayDiagnosticsOptions
{
    /*! \brief Form the actual-ring mark takes under each marked note. */
    ActualRingLook actual_ring{ActualRingLook::Off};

    /*!
    \brief Mark notes that already draw a tail of their own.

    Positive so the default reads as "mark everything". Off leaves the rig showing only the notes
    whose ring the presented form does NOT already draw, which is the comparison a reader wants
    once the agreeing cases have been checked. The fact is the chart's — the note's presented end
    is later than its onset — never "a tail is visible this frame", which flickers as a tail
    scrolls off the horizon.
    */
    bool actual_ring_marks_tailed_notes{true};

    /*!
    \brief Mark notes inside a strum that draws a chord box.

    Also positive, and off for the same kind of reason: a chord box already states how long its
    posture is held, so a ring mark under each member repeats it — see
    \ref common::core::highwayChordBoxApplies for the one rule both the box and this filter read.
    */
    bool actual_ring_marks_chord_members{true};
};

} // namespace rock_hero::common::ui
