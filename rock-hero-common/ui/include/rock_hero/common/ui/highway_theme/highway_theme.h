/*!
\file highway_theme.h
\brief The color roles one look of the 3D highway is painted with.
*/

#pragma once

#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief Every color the 3D highway paints that is not a string color, one member per role.

A role is named for what the color does, never for where it is drawn. Each is read as stored,
alpha included; the renderer then scales that alpha for fades, tiers and frames, and replaces it
outright only where a member says so. String colors are not roles; they come from the
StringColorPalette passed beside the theme. Nor is any color derived from a role (the picking hand's
chord and arpeggio, a box fill's middle, the dim fret number, the tapping light's lean), so a
derived color always follows the role it comes from.

A box or a rail tells two things apart by color alone: its kind of span and its hand. The kind is
authored, as chord and arpeggio, which are the fretting hand's colors; the picking hand's are
derived from them.

No member has a default, so a theme spelled out field by field must name every role.
*/
struct HighwayTheme
{
    /*! \brief The flat color the whole scene is cleared to behind the board. */
    ArgbColor backdrop;

    /*!
    \brief Beat and measure bars, and the fade trailing each attack line into its measure or past
    its note.
    */
    ArgbColor beat_bar;

    /*!
    \brief The sharp line at each measure's downbeat, under a single note's fret slots and at a
    pitched slide keyframe's slot.
    */
    ArgbColor attack_line;

    /*! \brief A plain lane where a hand's light reveals the board, at the light's full strength. */
    ArgbColor lit_lane;

    /*! \brief An inlay-dotted lane where a hand's light reveals the board. */
    ArgbColor lit_lane_dotted;

    /*!
    \brief The per-fret runway ribbons, which scale its alpha by their light tiers.

    A dotted fret's number outside the hand window wears it at half alpha.
    */
    ArgbColor lane_line;

    /*! \brief Fret lines outside every hand's light. */
    ArgbColor fret_line;

    /*! \brief Fret lines at a hand's full light; a line crossfades toward it from fret_line. */
    ArgbColor fret_line_lit;

    /*!
    \brief A dotted fret's number on a measure downbeat inside the hand window; a number the window
    covers partly blends toward it from lane_line at half alpha.
    */
    ArgbColor fret_number_lit;

    /*!
    \brief Every hand-position target: arrival numbers, tapped positions, harmonic node numbers and
    the current hand's numbers pinned at the hit line.

    The tapping hand's light leans its lanes toward this color, so the two hands' lights read apart.
    */
    ArgbColor fret_number_target;

    /*! \brief The fretting hand's strike pops, additive light on the fret lines and box sides. */
    ArgbColor strike_fretting;

    /*! \brief The picking hand's strike pops. */
    ArgbColor strike_picking;

    /*!
    \brief The fretting hand's chord: its box's frame, the ends of its fill (whose middle is the
    same color darkened), its accent light, the palm-mute mark on a repeat box, and its rails.

    A tapped chord's box and rails wear this color's picking-hand counterpart.
    */
    ArgbColor chord;

    /*!
    \brief The fretting hand's arpeggio, everywhere chord paints a chord: its box, under the posture
    brackets, and its rails.
    */
    ArgbColor arpeggio;

    /*! \brief The full-mute X on a repeat box. */
    ArgbColor full_mute_mark;

    /*! \brief The dead zone over the board face between the nut and the capo. */
    ArgbColor capo_shade;

    /*! \brief The capo's bar across the strings. */
    ArgbColor capo_bar;

    /*! \brief The rim around the capo's bar. */
    ArgbColor capo_bar_rim;

    /*! \brief The tint the inlay art is multiplied by; its alpha is the inlays' opacity. */
    ArgbColor inlay_tint;

    /*! \brief The section labels above the board. */
    ArgbColor label_text;
};

} // namespace rock_hero::common::ui
