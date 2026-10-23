#include "shared/editor_look_and_feel.h"

#include "shared/editor_theme.h"

namespace rock_hero::editor::ui
{

namespace
{

// V4's button corner; the ring sits one pixel inside it, so its own corner is one smaller.
constexpr float g_button_corner{6.0f};
constexpr float g_button_ring_corner{5.0f};

// V4's combo-box corner is 3; the ring one pixel inside it keeps the same curve at 2.
constexpr float g_combo_ring_corner{2.0f};

// The focus ring's stroke: two pixels, centred one pixel inside the control's edge so it covers
// the outermost two rows of the control exactly.
constexpr float g_ring_thickness{2.0f};
constexpr float g_ring_inset{1.0f};

// A button's outline with V4's flat sides: an edge connected to a neighbour keeps square corners.
[[nodiscard]] juce::Path buttonOutline(
    const juce::Button& button, const juce::Rectangle<float> bounds, const float corner)
{
    const bool flat_on_left = button.isConnectedOnLeft();
    const bool flat_on_right = button.isConnectedOnRight();
    const bool flat_on_top = button.isConnectedOnTop();
    const bool flat_on_bottom = button.isConnectedOnBottom();

    juce::Path outline;
    outline.addRoundedRectangle(
        bounds.getX(),
        bounds.getY(),
        bounds.getWidth(),
        bounds.getHeight(),
        corner,
        corner,
        !(flat_on_left || flat_on_top),
        !(flat_on_right || flat_on_top),
        !(flat_on_left || flat_on_bottom),
        !(flat_on_right || flat_on_bottom));
    return outline;
}

} // namespace

// V4 already strokes a focused text field's outline two pixels wide; only its color is ours.
EditorLookAndFeel::EditorLookAndFeel()
{
    setColour(juce::TextEditor::focusedOutlineColourId, editorTheme().focus_ring);
}

// V4's fill, hover and press shading and its connected-edge outline, minus the saturation it
// multiplies in on focus, so the fill reads the same focused or not and the ring alone says so.
void EditorLookAndFeel::drawButtonBackground(
    juce::Graphics& g, juce::Button& button, const juce::Colour& background_colour,
    const bool should_draw_button_as_highlighted, const bool should_draw_button_as_down)
{
    const juce::Rectangle<float> bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);

    juce::Colour fill = background_colour.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);
    if (should_draw_button_as_down || should_draw_button_as_highlighted)
    {
        fill = fill.contrasting(should_draw_button_as_down ? 0.2f : 0.05f);
    }

    const juce::Path outline = buttonOutline(button, bounds, g_button_corner);
    g.setColour(fill);
    g.fillPath(outline);
    g.setColour(button.findColour(juce::ComboBox::outlineColourId));
    g.strokePath(outline, juce::PathStrokeType{1.0f});

    if (showsFocusRing(button))
    {
        g.setColour(editorTheme().focus_ring);
        g.strokePath(
            buttonOutline(
                button,
                button.getLocalBounds().toFloat().reduced(g_ring_inset),
                g_button_ring_corner),
            juce::PathStrokeType{g_ring_thickness});
    }
}

// V4 marks a focused combo box not at all; the ring is drawn over its finished box.
void EditorLookAndFeel::drawComboBox(
    juce::Graphics& g, const int width, const int height, const bool is_button_down,
    const int button_x, const int button_y, const int button_w, const int button_h,
    juce::ComboBox& box)
{
    juce::LookAndFeel_V4::drawComboBox(
        g, width, height, is_button_down, button_x, button_y, button_w, button_h, box);

    if (showsFocusRing(box))
    {
        g.setColour(editorTheme().focus_ring);
        g.drawRoundedRectangle(
            juce::Rectangle<float>{
                0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)
            }
                .reduced(g_ring_inset),
            g_combo_ring_corner,
            g_ring_thickness);
    }
}

// The control itself, not a child: a combo box whose editable text field holds focus leaves the
// ring to that field's own outline.
bool EditorLookAndFeel::showsFocusRing(const juce::Component& control) const
{
    return control.hasKeyboardFocus(false);
}

} // namespace rock_hero::editor::ui
