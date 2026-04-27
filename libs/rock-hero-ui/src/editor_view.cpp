#include "editor_view.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <rock_hero/audio/thumbnail.h>
#include <rock_hero/core/audio_asset.h>
#include <utility>

namespace rock_hero::ui
{

// Converts a timeline position to a bounded subpixel coordinate for the cursor overlay.
std::optional<float> cursorXForTimelinePosition(
    core::TimePosition position, core::TimePosition visible_timeline_start,
    core::TimeDuration visible_timeline_duration, int width) noexcept
{
    if (width <= 0 || visible_timeline_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    const double relative_position =
        (position.seconds - visible_timeline_start.seconds) / visible_timeline_duration.seconds;
    const double clamped_position = std::clamp(relative_position, 0.0, 1.0);
    const auto max_x = static_cast<double>(width - 1);
    return static_cast<float>(clamped_position * max_x);
}

// Handles editor-wide timeline interaction and draws the cursor from live transport position.
class EditorView::CursorOverlay final : public juce::Component
{
public:
    // Starts vblank-driven cursor refresh against the injected read-only transport.
    CursorOverlay(IEditorController& controller, const audio::ITransport& transport)
        : m_controller(controller)
        , m_transport(transport)
        , m_vblank_attachment(this, [this] { advanceCursor(); })
    {
        setComponentID("cursor_overlay");
        setInterceptsMouseClicks(true, false);
    }

    // Stores discrete timeline mapping data pushed by EditorView::setState().
    void setVisibleTimelineRange(
        core::TimePosition visible_timeline_start,
        core::TimeDuration visible_timeline_duration) noexcept
    {
        m_visible_timeline_start = visible_timeline_start;
        m_visible_timeline_duration = visible_timeline_duration;
    }

    // Draws only the cursor; static waveform content remains in TrackView below this overlay.
    void paint(juce::Graphics& g) override
    {
        if (!m_cursor_x.has_value())
        {
            return;
        }

        g.setColour(juce::Colours::white);
        g.drawLine(*m_cursor_x, 0.0f, *m_cursor_x, static_cast<float>(getHeight()), 2.0f);
    }

    // Converts editor-wide timeline clicks into normalized seek intent.
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (getWidth() <= 0)
        {
            return;
        }

        const double ratio = static_cast<double>(event.x) / static_cast<double>(getWidth());
        m_controller.onWaveformClicked(std::clamp(ratio, 0.0, 1.0));
    }

private:
    // Samples the current position at render cadence and invalidates only changed cursor strips.
    void advanceCursor()
    {
        const auto next_cursor_x = cursorXForTimelinePosition(
            m_transport.position(),
            m_visible_timeline_start,
            m_visible_timeline_duration,
            getWidth());

        if (next_cursor_x == m_cursor_x)
        {
            return;
        }

        repaintCursorMovement(m_cursor_x, next_cursor_x);
        m_cursor_x = next_cursor_x;
    }

    // Invalidates the union of old/new subpixel cursor strips, including antialias padding.
    void repaintCursorMovement(
        std::optional<float> previous_cursor_x, std::optional<float> next_cursor_x)
    {
        if ((!previous_cursor_x.has_value() && !next_cursor_x.has_value()) || getWidth() <= 0 ||
            getHeight() <= 0)
        {
            return;
        }

        float left_x = 0.0f;
        float right_x = 0.0f;
        if (previous_cursor_x.has_value() && next_cursor_x.has_value())
        {
            left_x = std::min(*previous_cursor_x, *next_cursor_x);
            right_x = std::max(*previous_cursor_x, *next_cursor_x);
        }
        else
        {
            const float cursor_x =
                previous_cursor_x.has_value() ? *previous_cursor_x : *next_cursor_x;
            left_x = cursor_x;
            right_x = cursor_x;
        }
        constexpr int padding = 3;
        const int left = std::max(0, static_cast<int>(std::floor(left_x)) - padding);
        const int right = std::min(getWidth(), static_cast<int>(std::ceil(right_x)) + padding + 1);
        repaint(left, 0, right - left, getHeight());
    }

    // Controller receives editor-level timeline seek intent.
    IEditorController& m_controller;

    // Read-only transport source sampled at vblank cadence for its live position method.
    const audio::ITransport& m_transport;

    // Vblank-driven callback used to keep cursor motion smooth without transport listeners.
    juce::VBlankAttachment m_vblank_attachment;

    // Start of the visible timeline range last pushed by EditorView::setState().
    core::TimePosition m_visible_timeline_start{};

    // Duration of the visible timeline range last pushed by EditorView::setState().
    core::TimeDuration m_visible_timeline_duration{};

    // Last subpixel cursor x coordinate drawn by the overlay, if a cursor is currently mappable.
    std::optional<float> m_cursor_x{};
};

// Creates child widgets, installs the row thumbnail, and keeps ThumbnailCreator construction-only.
EditorView::EditorView(
    IEditorController& controller, const audio::ITransport& transport,
    const ThumbnailCreator& create_thumbnail)
    : m_controller(controller)
    , m_transport_controls(*this)
    , m_cursor_overlay(std::make_unique<CursorOverlay>(controller, transport))
{
    m_load_button.setComponentID("load_button");
    m_load_button.setButtonText("Load File...");
    m_load_button.setEnabled(false);
    m_load_button.onClick = [this] { onLoadClicked(); };
    setWantsKeyboardFocus(true);

    m_transport_controls.setComponentID("transport_controls");
    m_track_view.setComponentID("track_view");

    if (create_thumbnail)
    {
        m_track_view.setThumbnail(create_thumbnail(m_track_view));
    }

    addAndMakeVisible(m_load_button);
    addAndMakeVisible(m_transport_controls);
    addAndMakeVisible(m_track_view);
    addAndMakeVisible(*m_cursor_overlay);

    setSize(800, 300);
}

// Uses default destruction because child ownership is represented by members.
EditorView::~EditorView() = default;

// Projects controller-derived state into child widgets and cursor mapping state.
void EditorView::setState(const EditorViewState& state)
{
    m_state = state;

    m_load_button.setEnabled(m_state.load_button_enabled && !m_state.tracks.empty());
    m_transport_controls.setState(
        TransportControlsState{
            .play_pause_enabled = m_state.play_pause_enabled,
            .stop_enabled = m_state.stop_enabled,
            .play_pause_shows_pause_icon = m_state.play_pause_shows_pause_icon,
        });

    if (m_state.tracks.empty())
    {
        m_track_view.setState(TrackViewState{});
    }
    else
    {
        m_track_view.setState(m_state.tracks.front());
    }

    m_cursor_overlay->setVisibleTimelineRange(
        m_state.visible_timeline_start, m_state.visible_timeline_duration);
    presentLoadErrorIfNeeded(m_state.last_load_error);
}

// Paints a neutral background while child widgets render their own content.
void EditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
}

// Keeps the control strip above one waveform row and overlays the cursor across that row.
void EditorView::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto button_row = area.removeFromTop(32);
    m_load_button.setBounds(button_row.removeFromLeft(120));
    button_row.removeFromLeft(8);
    m_transport_controls.setBounds(button_row.removeFromLeft(176));
    area.removeFromTop(8);
    m_track_view.setBounds(area);
    m_cursor_overlay->setBounds(area);
    m_cursor_overlay->toFront(false);
}

// Routes editor-level keyboard shortcuts through the same controller intents as child widgets.
bool EditorView::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress{juce::KeyPress::spaceKey})
    {
        m_controller.onPlayPausePressed();
        return true;
    }

    return false;
}

// Opens an asynchronous file chooser and sends accepted assets to the controller.
void EditorView::onLoadClicked()
{
    if (m_state.tracks.empty())
    {
        return;
    }

    const core::TrackId track_id = m_state.tracks.front().track_id;
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Select an audio file",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.aiff;*.ogg;*.flac");

    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, track_id](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            const core::AudioAsset audio_asset{
                std::filesystem::path{file.getFullPathName().toWideCharPointer()}
            };
            m_controller.onLoadAudioAssetRequested(track_id, audio_asset);
        });
}

// Shows each distinct error once and resets the edge when the controller clears the error.
void EditorView::presentLoadErrorIfNeeded(const std::optional<std::string>& error)
{
    if (!error.has_value())
    {
        m_last_presented_error.reset();
        return;
    }

    if (m_last_presented_error == error)
    {
        return;
    }

    m_last_presented_error = error;
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Could not load file")
            .withMessage(juce::String{error->c_str()})
            .withButton("OK"),
        nullptr);
}

// Forwards the transport-control intent to the workflow controller.
void EditorView::onPlayPausePressed()
{
    m_controller.onPlayPausePressed();
}

// Forwards the stop-control intent to the workflow controller.
void EditorView::onStopPressed()
{
    m_controller.onStopPressed();
}

} // namespace rock_hero::ui
