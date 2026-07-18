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
    \brief Paused play-from-here column drawn behind the track content (the marker model).

    A cool muted step down from the playback white so paused-vs-playing reads instantly and
    the behind-content column cannot be mistaken for a grid line.
    */
    juce::Colour paused_cursor{0xff8fa0a8};

    /*! \brief Interaction accent: snap guides, drop indicators, selection borders. */
    juce::Colour accent{0xff87cefa};

    /*! \brief Emphasized foreground text over dark surfaces. */
    juce::Colour primary_text{0xffffffff};

    /*! \brief De-emphasized status and placeholder text over dark surfaces. */
    juce::Colour muted_text{0xff9aa1ab};
};

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
