/*!
\file editor_theme.h
\brief The editor's color theme: one value object holding every themeable UI color.

Components read colors through editorTheme() at paint time instead of spelling their own
constants, so a future user-selectable theme only has to swap the returned object and repaint.
Colors that are part of a settled rendering style rather than app chrome — the Charter-derived
string-color palette — live as shared data in rock-hero-common/ui (string_color_palette.h),
consumed by the tab renderer; the theme itself stays editor-private.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Semantic color roles for the editor UI.

The default palette is a single cool dark ramp informed by Charter's modern theme, from the
near-black timeline surfaces up through the panel and window chrome, so every editor surface
reads as one family.
*/
struct EditorTheme
{
    /*! \brief Editor window backdrop behind menus, controls, and panels. */
    juce::Colour window_background{0xff26292e};

    /*! \brief Horizontal chrome strips: the transport bar and tool-window title bars. */
    juce::Colour bar_background{0xff1f2227};

    /*! \brief Docked panel and tool-window body surfaces. */
    juce::Colour panel_background{0xff1d2127};

    /*! \brief Header strips of docked panels and list column headers. */
    juce::Colour panel_header{0xff171a1f};

    /*! \brief Area around and beyond the timeline canvas. */
    juce::Colour timeline_backdrop{0xff121418};

    /*! \brief Timeline ruler body behind measure numbers and ticks. */
    juce::Colour timeline_ruler_background{0xff171a1f};

    /*! \brief Waveform track row band; Charter's lane background, so tablature reads on it. */
    juce::Colour waveform_row_background{0xff141517};

    /*! \brief Tone track row band, one step lighter than the waveform row. */
    juce::Colour tone_row_background{0xff1a1e25};

    /*! \brief Sub-beat subdivision tempo grid dots, dimmest of the grid ranks. */
    juce::Colour grid_subdivision{0xff282a30};

    /*! \brief Off-beat tempo grid dots. */
    juce::Colour grid_beat{0xff34373f};

    /*! \brief Downbeat tempo grid dots and the ruler's measure ticks. */
    juce::Colour grid_measure{0xff6d7283};

    /*! \brief Song-section chip fill in the ruler's grid header. */
    juce::Colour section_chip{0xff2e7d52};

    /*! \brief Tempo-marking chip fill in the ruler's grid header, muted to read as metadata. */
    juce::Colour tempo_chip{0xff3a5a78};

    /*! \brief Time-signature chip fill in the ruler's grid header, muted to read as metadata. */
    juce::Colour signature_chip{0xff77602f};

    /*! \brief Waveform fill; Charter's muted teal sits behind notes without fighting them. */
    juce::Colour waveform{0xff408080};

    /*! \brief Playback cursor line in the timeline and the ruler. */
    juce::Colour playback_cursor{0xffffffff};

    /*!
    \brief Paused play-from-here cursor: the behind-content column and, while paused, the
    ruler's line-and-flag mark above it (the marker model).

    A cool muted step down from the playback white so paused-vs-playing reads instantly and
    the behind-content column cannot be mistaken for a grid line. The ruler mark shares the
    color while paused so column and mark read as one continuous indicator.
    */
    juce::Colour paused_cursor{0xff8fa0a8};

    /*! \brief Interaction accent: snap guides, drop indicators, selection borders. */
    juce::Colour accent{0xff87cefa};

    /*!
    \brief Editor furniture drawn OVER the tab lane's notation: the armed caret's square and the
    Alt-hover insert ghost's ring.

    Translucent white so the notation reads through it and it stays visible over every string
    color; the two overlays share one ink because they are the same kind of thing — what the next
    edit lands on or acts on — told apart by shape alone (square for the caret, round for the
    note-to-be).
    */
    juce::Colour lane_overlay{0xb3ffffff};

    /*!
    \brief Invalid-state ink: a provisional value that cannot apply (the pending fret entry's
    red text).

    Pure red on purpose: its relative luminance (~0.21) against the digit white (1.0) keeps the
    valid/invalid signal legible on luminance alone, so it survives protan and deutan vision
    without a second shape (the pending-entry ruling).
    */
    juce::Colour invalid{0xffff0000};

    /*! \brief Emphasized foreground text over dark surfaces. */
    juce::Colour primary_text{0xffffffff};

    /*! \brief De-emphasized status and placeholder text over dark surfaces. */
    juce::Colour muted_text{0xff9aa1ab};
};

/*!
\brief How far a quieted editor mark falls toward its ground, and the near-black a veil uses.

Quieting means one thing: HALVE the mark's contrast against the ground it sits on, so a viewer
reads "still there, no longer binding" rather than "gone" or "disabled". The ground is whatever the
mark happens to be drawn over, which is why the DISTANCE is the only number the two forms below
share. A fixed ground constant cannot be that shared number: it is right only on a surface that
happens to be exactly that color, and the timeline paints three different row bands under one grid
(the tone row's 0xff1a1e25 is lighter and bluer than the near-black, so a mark pre-mixed toward the
near-black lands on top of it and vanishes there).

g_quiet_ground_argb is therefore not "the ground". It is only the near-black \ref quietingVeil
composites toward, chosen so a veiled region darkens rather than washes out; \ref quieted needs no
ground at all, because the compositor supplies the real one.

The distance is a sighting value: it is set where the grid still reads as a reference lattice while
plainly no longer claiming to bind placement.
*/
inline constexpr juce::uint32 g_quiet_ground_argb{0xff101010};
inline constexpr float g_quiet_lean{0.5F};

/*!
\brief Quiets a color by halving its contrast against whatever it is painted onto.

For a mark whose color is chosen before painting and that lands directly on its own opaque ground.
The returned color is translucent, so the compositor puts it exactly halfway between the mark and
that ground on every band, with no ground constant to state or to get wrong.

The precondition is what makes it exact: nothing but the ground may lie underneath, and the mark
must not overlap itself (a second pass over the same pixel would composite twice). A mark drawn
over OTHER marks leans opaquely toward its own ground instead — the 2D tab lane's rule, where a
tail, a lane line, or a chord fill sits beneath a note and translucency would reveal them. A mark
already on the canvas is quieted with \ref quietingVeil.

\param color Color the mark would draw at full strength.
\return That color, translucent enough to land halfway to whatever it is drawn on.
*/
[[nodiscard]] inline juce::Colour quieted(juce::Colour color) noexcept
{
    return color.withMultipliedAlpha(1.0F - g_quiet_lean);
}

/*!
\brief The translucent ground that quiets whatever is already painted beneath it.

The same halving expressed as a composite, for quieting a region whose marks were drawn by someone
else — a child component's chrome and text, which the parent cannot re-color. Compositing the
near-black over the whole region halves every mark's contrast against its background, but it takes
that background down with it, which is why this is the form for chrome rather than for a mark whose
color the caller still owns (\ref quieted leaves the ground alone).

\return The veil color to fill the region with.
*/
[[nodiscard]] inline juce::Colour quietingVeil() noexcept
{
    return juce::Colour{g_quiet_ground_argb}.withAlpha(g_quiet_lean);
}

/*!
\brief Returns the active editor theme.

The theme lives behind a function-local static built purely from hex literals, so it is safe to
read from any initialization context: unlike the named juce::Colours constants (which are
per-translation-unit dynamically initialized globals with no cross-TU ordering guarantee under
MSVC incremental linking), nothing here depends on another global's initialization.

\return The theme every editor component should draw with.
*/
[[nodiscard]] const EditorTheme& editorTheme() noexcept;

} // namespace rock_hero::editor::ui
