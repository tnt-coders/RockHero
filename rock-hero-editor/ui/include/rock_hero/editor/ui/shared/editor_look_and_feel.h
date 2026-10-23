/*!
\file editor_look_and_feel.h
\brief The editor-wide look-and-feel: JUCE's V4 drawing plus a visible keyboard-focus ring.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Draws every editor control as JUCE's V4 does, and rings the one holding keyboard focus.

Stock V4 marks a focused button only by a saturation change, which on the editor's dark buttons
is close to invisible, and marks a focused combo box not at all. This look-and-feel draws one
2 px ring in the theme's focus-ring color around the focused button, combo box or text field, and
makes a button's fill independent of focus, so the ring is the only focus signal.

Focus is a keyboard signal: every editor button and combo box is built with
`setMouseClickGrabsKeyboardFocus(false)`, as JUCE's own AlertWindow builds its buttons, so a ring
only ever appears where Tab or a dialog's initial focus put it.

The application installs one instance as the desktop default before it builds any window and
clears it after the last window is gone. A component that sets a look-and-feel of its own (the
menu bar's MenuLookAndFeel) derives from this one, so the focus rules exist once.
*/
class EditorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    /*! \brief Sets the colors the V4 drawing reads that the editor theme owns. */
    EditorLookAndFeel();

    /*!
    \brief Fills a button as V4 does, without its focus saturation, then rings a focused one.

    \param g Graphics context used for drawing.
    \param button Button being painted.
    \param background_colour Fill color the button asks for.
    \param should_draw_button_as_highlighted True while the mouse is over the button.
    \param should_draw_button_as_down True while the button is held down.
    */
    void drawButtonBackground(
        juce::Graphics& g, juce::Button& button, const juce::Colour& background_colour,
        bool should_draw_button_as_highlighted, bool should_draw_button_as_down) override;

    /*!
    \brief Draws a combo box as V4 does, then rings it while it holds keyboard focus.

    \param g Graphics context used for drawing.
    \param width Box width in pixels.
    \param height Box height in pixels.
    \param is_button_down True while the box is held down.
    \param button_x Left of the box's arrow zone.
    \param button_y Top of the box's arrow zone.
    \param button_w Width of the box's arrow zone.
    \param button_h Height of the box's arrow zone.
    \param box Combo box being painted.
    */
    void drawComboBox(
        juce::Graphics& g, int width, int height, bool is_button_down, int button_x, int button_y,
        int button_w, int button_h, juce::ComboBox& box) override;

protected:
    /*!
    \brief Decides whether a control is drawn with the focus ring: exactly when it holds keyboard
    focus itself.

    The one place the rule is stated. Virtual so a pixel test can paint the focused state, which a
    headless test cannot reach for real: JUCE hands keyboard focus only to a component whose native
    window the OS has made active.

    \param control Button or combo box being painted.
    \return True when the ring is drawn.
    */
    [[nodiscard]] virtual bool showsFocusRing(const juce::Component& control) const;
};

} // namespace rock_hero::editor::ui
