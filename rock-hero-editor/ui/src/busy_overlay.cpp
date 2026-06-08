#include "busy_overlay.h"

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <memory>
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
constexpr int g_surface_height_with_cancel = 164;
constexpr int g_surface_padding = 16;
constexpr int g_surface_corner_radius = 8;

// Distance between the progress bar and message text inside the surface.
constexpr int g_progress_bar_height = 22;
constexpr int g_progress_message_gap = 12;
constexpr int g_cancel_button_height = 28;
constexpr int g_cancel_button_width = 96;
constexpr int g_cancel_message_gap = 14;

// Chooses the centered surface height needed by the current child set.
[[nodiscard]] int busySurfaceHeight(bool shows_cancel) noexcept
{
    return shows_cancel ? g_surface_height_with_cancel : g_surface_height;
}

} // namespace

// Registers the owned indeterminate animation as a hidden child; determinate progress is painted
// directly, so the bar is shown only when there is no known fraction.
BusyOverlay::BusyProgressBar::BusyProgressBar()
{
    addChildComponent(m_indeterminate_bar);
}

// Selects determinate (exact fraction) or indeterminate rendering. The owned juce::ProgressBar is
// shown only for the indeterminate case; determinate progress is painted directly.
void BusyOverlay::BusyProgressBar::setProgress(std::optional<double> progress)
{
    m_value = progress.has_value() ? std::clamp(*progress, 0.0, 1.0) : -1.0;
    m_indeterminate_bar.setVisible(!progress.has_value());
    repaint();
}

// Paints determinate progress as the exact fraction. The indeterminate animation is drawn by the
// owned juce::ProgressBar child, so nothing is painted here in that case.
void BusyOverlay::BusyProgressBar::paint(juce::Graphics& g)
{
    if (m_value < 0.0)
    {
        return;
    }

    const juce::Rectangle<float> bar_bounds = getLocalBounds().toFloat();
    if (bar_bounds.isEmpty())
    {
        return;
    }

    const juce::Colour background = findColour(juce::ProgressBar::backgroundColourId);
    const juce::Colour foreground = findColour(juce::ProgressBar::foregroundColourId);
    const float corner_radius = static_cast<float>(getHeight()) * 0.5F;

    g.setColour(background);
    g.fillRoundedRectangle(bar_bounds, corner_radius);

    juce::Rectangle<float> progress_bounds = bar_bounds;
    progress_bounds.setWidth(progress_bounds.getWidth() * static_cast<float>(m_value));
    // Shrink the fill's corner radius for small fractions so a narrow bar renders as a thin rounded
    // rect instead of a lens-shaped blob wider than its own width.
    const float fill_radius = juce::jmin(corner_radius, progress_bounds.getWidth() * 0.5F);
    g.setColour(foreground);
    g.fillRoundedRectangle(progress_bounds, fill_radius);

    juce::String text;
    text << juce::roundToInt(m_value * 100.0) << '%';
    g.setColour(juce::Colour::contrasting(background, foreground));
    g.setFont(static_cast<float>(getHeight()) * 0.6F);
    g.drawText(text, getLocalBounds(), juce::Justification::centred, false);
}

// Keeps the owned indeterminate bar filling the whole component.
void BusyOverlay::BusyProgressBar::resized()
{
    m_indeterminate_bar.setBounds(getLocalBounds());
}

// Configures the overlay as a hidden, non-intercepting front-most child. Visibility flips when
// setBusyState() receives a value.
BusyOverlay::BusyOverlay()
{
    setVisible(false);
    setInterceptsMouseClicks(true, true);
    setWantsKeyboardFocus(true);

    m_message_label.setJustificationType(juce::Justification::centred);
    m_message_label.setColour(juce::Label::textColourId, juce::Colours::white);
    m_message_label.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(m_message_label);

    m_cancel_button.setComponentID("busy_cancel_button");
    m_cancel_button.setButtonText("Cancel");
    m_cancel_button.onClick = [this] {
        if (m_cancel_button.isEnabled() && m_cancel_callback)
        {
            m_cancel_callback();
        }
    };
    addChildComponent(m_cancel_button);
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

        // Build the progress bar only while one is shown. Destroying it when no bar is needed keeps
        // the owned juce::ProgressBar's animation timer from running during idle editing.
        if (has_progress_bar && m_progress_bar == nullptr)
        {
            m_progress_bar = std::make_unique<BusyProgressBar>();
            m_progress_bar->setComponentID("busy_progress_bar");
            addAndMakeVisible(*m_progress_bar);
        }
        else if (!has_progress_bar)
        {
            m_progress_bar.reset();
        }

        if (m_progress_bar != nullptr)
        {
            m_progress_bar->setProgress(
                has_determinate_progress ? std::optional<double>{busy->progress.value_or(0.0)}
                                         : std::nullopt);
        }
        // Only show Cancel on the overlay whose owner actually wired a cancel handler. This keeps a
        // cancellable operation from rendering a duplicate, inert Cancel button on the editor-wide
        // overlay when a window-owned overlay (such as the plugin browser) already offers cancel.
        const bool show_cancel = busy->cancel_enabled && static_cast<bool>(m_cancel_callback);
        m_cancel_button.setVisible(show_cancel);
        m_cancel_button.setEnabled(show_cancel);
        m_message_label.setText(busy->message, juce::dontSendNotification);
        resized();
    }
    else
    {
        m_progress_bar.reset();
        m_cancel_button.setVisible(false);
        m_cancel_button.setEnabled(false);
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

// Stores the owner callback used to emit cancellation from the optional button.
void BusyOverlay::setCancelCallback(std::function<void()> callback)
{
    m_cancel_callback = std::move(callback);
}

// Paints the dim layer behind the editor content and the rounded progress surface centered on
// top. Child widgets paint themselves above this background.
void BusyOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(g_dim_alpha));

    const juce::Rectangle<int> bounds = getLocalBounds();
    const int surface_width = juce::jmin(g_surface_width, bounds.getWidth() - g_surface_padding);
    const int surface_height = juce::jmin(
        busySurfaceHeight(m_cancel_button.isVisible()), bounds.getHeight() - g_surface_padding);
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
    const int surface_height = juce::jmin(
        busySurfaceHeight(m_cancel_button.isVisible()), bounds.getHeight() - g_surface_padding);
    juce::Rectangle<int> surface = bounds.withSizeKeepingCentre(surface_width, surface_height);
    surface = surface.reduced(g_surface_padding);

    if (m_cancel_button.isVisible())
    {
        const juce::Rectangle<int> cancel_bounds =
            surface.removeFromBottom(g_cancel_button_height)
                .withSizeKeepingCentre(
                    juce::jmin(g_cancel_button_width, surface.getWidth()), g_cancel_button_height);
        surface.removeFromBottom(g_cancel_message_gap);
        m_cancel_button.setBounds(cancel_bounds);
    }

    if (m_progress_bar != nullptr)
    {
        const juce::Rectangle<int> progress_bounds = surface.removeFromTop(g_progress_bar_height);
        surface.removeFromTop(g_progress_message_gap);
        m_progress_bar->setBounds(progress_bounds);
    }

    m_message_label.setBounds(surface);
}

// Swallows every key so editor shortcuts cannot reach EditorView while the overlay is visible.
bool BusyOverlay::keyPressed(const juce::KeyPress& /*key*/)
{
    return true;
}

} // namespace rock_hero::editor::ui
