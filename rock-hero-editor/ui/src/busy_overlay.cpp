#include "busy_overlay.h"

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Dim layer alpha behind the centered progress surface. Light enough to keep editor content
// recognizable underneath, dark enough to make "this is not interactive right now" obvious.
constexpr float g_dim_alpha = 0.55F;

// Centered surface dimensions in pixels. The overlay scales the surface within these bounds at
// the smaller dimension when EditorView is narrow.
constexpr int g_surface_width = 280;
constexpr int g_surface_height = 120;
constexpr int g_surface_padding = 16;
constexpr int g_surface_corner_radius = 8;

// Distance between the progress bar and message text inside the surface.
constexpr int g_progress_bar_height = 22;
constexpr int g_progress_message_gap = 12;

} // namespace

// Configures the overlay as a hidden, non-intercepting front-most child. Visibility flips when
// setBusyState() receives a value.
BusyOverlay::BusyOverlay()
{
    setVisible(false);
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);

    m_progress_bar.setComponentID("busy_progress_bar");
    addAndMakeVisible(m_progress_bar);

    m_message_label.setJustificationType(juce::Justification::centred);
    m_message_label.setColour(juce::Label::textColourId, juce::Colours::white);
    m_message_label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_message_label);
}

// Drives visibility, message text, and keyboard focus from the controller-supplied busy state.
// When the overlay becomes newly visible it grabs keyboard focus so EditorView shortcut handlers
// stop receiving keypresses; when it hides it releases interception so the editor returns to its
// normal input state.
void BusyOverlay::setBusyState(const std::optional<core::BusyViewState>& busy)
{
    const bool should_be_visible = busy.has_value();
    const bool was_visible = isVisible();

    if (should_be_visible)
    {
        const bool has_determinate_progress =
            busy->indicator == core::BusyIndicator::DeterminateProgress;
        const bool has_progress_bar = busy->indicator != core::BusyIndicator::MessageOnly;
        m_progress =
            has_determinate_progress ? std::clamp(busy->progress.value_or(0.0), 0.0, 1.0) : -1.0;
        m_message_label.setText(busy->message, juce::dontSendNotification);
        m_progress_bar.setPercentageDisplay(has_determinate_progress);
        m_progress_bar.setVisible(has_progress_bar);
        m_progress_bar.repaint();
        resized();
    }

    if (should_be_visible == was_visible)
    {
        return;
    }

    setVisible(should_be_visible);
    if (should_be_visible)
    {
        toFront(true);
        grabKeyboardFocus();
    }
}

// Stores the owner callback used to build a paint fence around message-thread-only operations.
void BusyOverlay::setPaintCallback(std::function<void()> callback)
{
    m_paint_callback = std::move(callback);
}

// Paints the dim layer behind the editor content and the rounded progress surface centered on
// top. Child widgets paint themselves above this background.
void BusyOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(g_dim_alpha));

    const juce::Rectangle<int> bounds = getLocalBounds();
    const int surface_width = juce::jmin(g_surface_width, bounds.getWidth() - g_surface_padding);
    const int surface_height = juce::jmin(g_surface_height, bounds.getHeight() - g_surface_padding);
    const juce::Rectangle<int> surface =
        bounds.withSizeKeepingCentre(surface_width, surface_height);

    g.setColour(juce::Colour::fromRGB(40, 40, 40));
    g.fillRoundedRectangle(surface.toFloat(), static_cast<float>(g_surface_corner_radius));

    if (m_paint_callback)
    {
        m_paint_callback();
    }
}

// Lays out the centered surface contents: progress bar on top, message label below.
void BusyOverlay::resized()
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const int surface_width = juce::jmin(g_surface_width, bounds.getWidth() - g_surface_padding);
    const int surface_height = juce::jmin(g_surface_height, bounds.getHeight() - g_surface_padding);
    juce::Rectangle<int> surface = bounds.withSizeKeepingCentre(surface_width, surface_height);
    surface = surface.reduced(g_surface_padding);

    if (m_progress_bar.isVisible())
    {
        const juce::Rectangle<int> progress_bounds = surface.removeFromTop(g_progress_bar_height);
        surface.removeFromTop(g_progress_message_gap);
        m_progress_bar.setBounds(progress_bounds);
    }
    else
    {
        m_progress_bar.setBounds({});
    }

    m_message_label.setBounds(surface);
}

// Swallows every key so editor shortcuts cannot reach EditorView while the overlay is visible.
bool BusyOverlay::keyPressed(const juce::KeyPress& /*key*/)
{
    return true;
}

} // namespace rock_hero::editor::ui
