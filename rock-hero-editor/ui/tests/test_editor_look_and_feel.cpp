#include "shared/editor_theme.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/ui/shared/editor_look_and_feel.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>

namespace rock_hero::editor::ui
{

namespace
{

using testing::nearestColor;

// The editor look-and-feel with every control painted as focused, the state a headless test cannot
// reach for real: JUCE hands focus only to a component in an OS-active native window.
class FocusedLookAndFeel final : public EditorLookAndFeel
{
protected:
    [[nodiscard]] bool showsFocusRing(const juce::Component& /*control*/) const override
    {
        return true;
    }
};

// The control's size, and a probe on its left edge at mid-height: inside the ring's two-pixel
// stroke and well away from any glyph (the controls carry no text).
constexpr int g_width{80};
constexpr int g_height{30};
constexpr int g_ring_x{1};
constexpr int g_mid_y{g_height / 2};

// A probe in the button's middle, on its fill, far from the ring.
constexpr int g_fill_x{g_width / 2};

// Paints a component through a look-and-feel onto an opaque black ground.
[[nodiscard]] juce::Image paintThrough(juce::Component& component, juce::LookAndFeel& look_and_feel)
{
    component.setLookAndFeel(&look_and_feel);
    component.setBounds(0, 0, g_width, g_height);
    juce::Image image{juce::Image::ARGB, g_width, g_height, true};
    {
        juce::Graphics g{image};
        g.fillAll(juce::Colours::black);
        component.paintEntireComponent(g, true);
    }
    component.setLookAndFeel(nullptr);
    return image;
}

} // namespace

// The focused button's edge turns the focus-ring color; its fill does not change with focus.
TEST_CASE("EditorLookAndFeel rings a focused button", "[ui][focus-ring]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    EditorLookAndFeel plain;
    FocusedLookAndFeel focused;
    juce::TextButton button;

    const juce::Image unfocused_image = paintThrough(button, plain);
    const juce::Image focused_image = paintThrough(button, focused);

    const juce::Colour ring = editorTheme().focus_ring;
    const juce::Colour unfocused_edge = unfocused_image.getPixelAt(g_ring_x, g_mid_y);
    CHECK(testing::squaredColorDistance(unfocused_edge, ring) > 3 * 64 * 64);
    CHECK(
        nearestColor(focused_image.getPixelAt(g_ring_x, g_mid_y), {ring, unfocused_edge}) == ring);
    CHECK(
        focused_image.getPixelAt(g_fill_x, g_mid_y) ==
        unfocused_image.getPixelAt(g_fill_x, g_mid_y));
}

// V4 draws no focus on a combo box; the editor's ring marks it the same way.
TEST_CASE("EditorLookAndFeel rings a focused combo box", "[ui][focus-ring]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    EditorLookAndFeel plain;
    FocusedLookAndFeel focused;
    juce::ComboBox box;

    const juce::Image unfocused_image = paintThrough(box, plain);
    const juce::Image focused_image = paintThrough(box, focused);

    const juce::Colour ring = editorTheme().focus_ring;
    const juce::Colour unfocused_edge = unfocused_image.getPixelAt(g_ring_x, g_mid_y);
    CHECK(testing::squaredColorDistance(unfocused_edge, ring) > 3 * 64 * 64);
    CHECK(
        nearestColor(focused_image.getPixelAt(g_ring_x, g_mid_y), {ring, unfocused_edge}) == ring);
}

// A text field's focused outline is V4's own two-pixel stroke, in the focus-ring color.
TEST_CASE("EditorLookAndFeel colors a focused text field's outline", "[ui][focus-ring]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    const EditorLookAndFeel look_and_feel;

    CHECK(
        look_and_feel.findColour(juce::TextEditor::focusedOutlineColourId) ==
        editorTheme().focus_ring);
}

} // namespace rock_hero::editor::ui
