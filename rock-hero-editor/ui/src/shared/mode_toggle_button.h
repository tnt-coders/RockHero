/*!
\file mode_toggle_button.h
\brief Toolbar toggle for a latched editing mode, marked by a ring rather than a fill.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::editor::ui
{

/*!
\brief Toolbar toggle for a latched editing mode.

On, it shows its filled glyph inside a 1 px accent ring, the editor's selection mark, with no
fill; off, its outlined glyph with no ring; disabled, the whole button at 0.4 alpha. The glyph fills
the image area a 32 px transport button gives its icon.

The button renders the owner's published state through setToggleState and never flips itself on a
click: the click is an intent the owner forwards, and the state comes back with the next push. Each
mode supplies its own icon pair, so one class serves every latched mode on the toolbar.
*/
class ModeToggleButton final : public juce::DrawableButton
{
public:
    /*!
    \brief The on-state ring's corner radius, in pixels: the outline a refusal glow around the
    button follows, as a chip's glow follows its chip.
    */
    static constexpr float g_ring_corner_radius{3.0f};

    /*!
    \brief Creates the button over its mode's icon pair.
    \param name Component name.
    \param on_icon The filled glyph shown while the mode is on.
    \param off_icon The outlined glyph shown while the mode is off.
    */
    ModeToggleButton(
        const juce::String& name, std::unique_ptr<juce::Drawable> on_icon,
        std::unique_ptr<juce::Drawable> off_icon);

    /*!
    \brief Paints the accent ring while the mode is on, and nothing behind the glyph otherwise.
    \param g Graphics context used for drawing.
    \param should_draw_button_as_highlighted Unused: the mark says the mode's state only.
    \param should_draw_button_as_down Unused, for the same reason.
    */
    void paintButton(
        juce::Graphics& g, bool should_draw_button_as_highlighted,
        bool should_draw_button_as_down) override;

    /*! \brief Dims the whole button, ring and glyph together, while it is disabled. */
    void enablementChanged() override;
};

} // namespace rock_hero::editor::ui
