#include "editor_view.h"

#include "audio_device_settings_window.h"
#include "input_calibration_window.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/core/audio_asset.h>
#include <rock_hero/common/core/juce_path.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_open_command{1};
constexpr int g_import_command{2};
constexpr int g_save_command{3};
constexpr int g_save_as_command{4};
constexpr int g_close_command{5};
constexpr int g_exit_command{6};
constexpr int g_publish_command{7};
constexpr int g_menu_bar_height{24};
constexpr int g_content_inset{8};
constexpr int g_control_gap{8};
constexpr int g_transport_height{32};
constexpr int g_transport_bar_height{g_content_inset + g_transport_height};
constexpr int g_transport_controls_width{96};
constexpr int g_master_meter_width{384};
constexpr int g_master_meter_min_width{196};
// Floor wide enough to fit the closed-device sentinel without truncation; ceiling chosen so the
// File/Edit/... menu titles still have room on the smallest supported window width.
constexpr int g_audio_device_menu_button_min_width{180};
constexpr int g_audio_device_menu_button_max_width{520};
constexpr int g_track_canvas_width{1264};
constexpr int g_track_canvas_default_height{720};
constexpr int g_tracks_visible_at_default_size{3};
constexpr int g_signal_chain_panel_min_height{160};
constexpr int g_signal_chain_panel_max_height{260};
constexpr int g_track_viewport_min_height{80};
constexpr double g_default_pixels_per_second{static_cast<double>(g_track_canvas_width) / 10.0};
constexpr double g_max_pixels_per_second{static_cast<double>(g_track_canvas_width)};
constexpr double g_mouse_wheel_zoom_factor{1.2};
constexpr float g_min_mouse_wheel_delta{std::numeric_limits<float>::epsilon()};

const juce::Colour g_editor_background_colour{juce::Colours::darkgrey};
const juce::Colour g_transport_bar_colour{juce::Colours::darkgrey.darker(0.16f)};
const juce::Colour g_track_viewport_colour{juce::Colours::darkgrey.darker(0.34f)};

// Reserves enough right-side menu space for the current audio status without overlapping menus.
[[nodiscard]] int audioDeviceButtonWidth(
    const MenuBarButton& button, int menu_bar_height, int available_width)
{
    const int preferred_width = std::clamp(
        button.preferredWidthForHeight(menu_bar_height),
        g_audio_device_menu_button_min_width,
        g_audio_device_menu_button_max_width);
    return std::min(preferred_width, std::max(0, available_width));
}

// Treats tiny wheel deltas as absent so zoom input stays stable across platforms.
[[nodiscard]] bool hasMouseWheelDelta(float delta) noexcept
{
    return std::abs(delta) > g_min_mouse_wheel_delta;
}

// Ensures saved project packages use the Rock Hero project extension when needed.
[[nodiscard]] std::filesystem::path pathWithRhpExtension(const juce::File& file)
{
    std::filesystem::path path = common::core::pathFromJuceFile(file);
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".rhp");
    }
    return path;
}

// Ensures published song packages use the native Rock Hero song extension when needed.
[[nodiscard]] std::filesystem::path pathWithRockExtension(const juce::File& file)
{
    std::filesystem::path path = common::core::pathFromJuceFile(file);
    if (!path.empty() && path.extension().empty())
    {
        path.replace_extension(".rock");
    }
    return path;
}

// Converts a project-suggested publish path into JUCE's save-dialog starting file.
[[nodiscard]] juce::File publishChooserInitialFile(const std::filesystem::path& suggested_file)
{
    if (suggested_file.empty())
    {
        return juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    }

    return common::core::juceFileFromPath(suggested_file);
}

// Gives the unsaved-changes prompt enough context for the action that triggered it. Only the
// deferrable subset reaches this switch; the post-switch return is a defensive fallback for ids the
// controller cannot legitimately stash in a deferred slot.
[[nodiscard]] juce::String unsavedChangesPromptMessage(core::EditorActionId action)
{
    switch (action)
    {
        case core::EditorActionId::CloseProject:
        {
            return "Save changes before closing the current project?";
        }
        case core::EditorActionId::OpenProject:
        {
            return "Save changes before opening another project?";
        }
        case core::EditorActionId::RestoreProject:
        {
            return "Save changes before restoring the previous project?";
        }
        case core::EditorActionId::ImportSong:
        {
            return "Save changes before importing another project?";
        }
        case core::EditorActionId::ExitApplication:
        {
            return "Save changes before exiting Rock Hero Editor?";
        }
        case core::EditorActionId::SaveProject:
        case core::EditorActionId::SaveProjectAs:
        case core::EditorActionId::PublishProject:
        case core::EditorActionId::ResolveUnsavedChangesPrompt:
        case core::EditorActionId::CancelSaveAsPrompt:
        case core::EditorActionId::PlayPause:
        case core::EditorActionId::Stop:
        case core::EditorActionId::SeekWaveform:
        case core::EditorActionId::ShowPluginBrowser:
        case core::EditorActionId::BeginPluginInsert:
        case core::EditorActionId::ScanPluginCatalog:
        case core::EditorActionId::InsertSelectedPlugin:
        case core::EditorActionId::RemovePlugin:
        case core::EditorActionId::MovePlugin:
        case core::EditorActionId::SetSignalChainPlacement:
        case core::EditorActionId::OpenPlugin:
        {
            return "Save changes before continuing?";
        }
    }

    return "Save changes before continuing?";
}

} // namespace

// Converts a timeline position to a bounded subpixel coordinate for the cursor overlay.
std::optional<float> cursorXForTimelinePosition(
    common::core::TimePosition position, common::core::TimeRange visible_timeline,
    int width) noexcept
{
    const common::core::TimeDuration visible_duration = visible_timeline.duration();
    if (width <= 0 || visible_duration.seconds <= 0.0)
    {
        return std::nullopt;
    }

    const double relative_position =
        (position.seconds - visible_timeline.start.seconds) / visible_duration.seconds;
    const double clamped_position = std::clamp(relative_position, 0.0, 1.0);
    const auto max_x = static_cast<double>(width - 1);
    return static_cast<float>(clamped_position * max_x);
}

// Handles editor-wide timeline interaction and draws the cursor from current transport position.
class EditorView::CursorOverlay final : public juce::Component
{
public:
    // Starts vblank-driven cursor refresh against the injected read-only transport.
    CursorOverlay(core::IEditorController& controller, const common::audio::ITransport& transport)
        : m_controller(controller)
        , m_transport(transport)
        , m_vblank_attachment(this, [this] { advanceCursor(); })
    {
        setComponentID("cursor_overlay");
        setInterceptsMouseClicks(true, false);
    }

    // Stores discrete timeline mapping data pushed by EditorView::setState().
    void setVisibleTimelineRange(common::core::TimeRange visible_timeline) noexcept
    {
        m_visible_timeline = visible_timeline;
    }

    // Draws only the cursor; static waveform content remains in ArrangementView below it.
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

        const double ratio =
            static_cast<double>(event.position.x) / static_cast<double>(getWidth());
        m_controller.onWaveformClicked(std::clamp(ratio, 0.0, 1.0));
    }

private:
    // Samples the current position at render cadence and invalidates only changed cursor strips.
    void advanceCursor()
    {
        const auto next_cursor_x =
            cursorXForTimelinePosition(m_transport.position(), m_visible_timeline, getWidth());

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
    core::IEditorController& m_controller;

    // Read-only transport source sampled at vblank cadence for its current position method.
    const common::audio::ITransport& m_transport;

    // Vblank-driven callback used to keep cursor motion smooth without transport listeners.
    juce::VBlankAttachment m_vblank_attachment;

    // Visible timeline range last pushed by EditorView::setState().
    common::core::TimeRange m_visible_timeline{};

    // Last subpixel cursor x coordinate drawn by the overlay, if a cursor is currently mappable.
    std::optional<float> m_cursor_x{};
};

// Hosts zoomable track content inside a JUCE viewport for future multi-track scrolling.
class EditorView::TrackViewport final : public juce::Component
{
private:
    // Paints the timeline content area and delegates wheel zoom back to the viewport shell.
    class Content final : public juce::Component
    {
    public:
        // Stores the owning viewport shell so wheel input can update shared zoom state.
        explicit Content(TrackViewport& owner)
            : m_owner(owner)
        {
            setInterceptsMouseClicks(false, true);
        }

        // Stores whether child track content should replace the empty-project message.
        void setProjectLoaded(bool project_loaded)
        {
            m_project_loaded = project_loaded;
            repaint();
        }

        // Draws the darker viewport canvas and centered empty-project status text.
        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds();
            g.fillAll(g_track_viewport_colour);

            if (m_project_loaded)
            {
                return;
            }

            g.setColour(juce::Colours::lightgrey);
            g.drawText("No Project Loaded", bounds, juce::Justification::centred);
        }

        // Converts normal wheel movement over timeline content into horizontal zoom.
        void mouseWheelMove(
            const juce::MouseEvent& /*event*/, const juce::MouseWheelDetails& wheel) override
        {
            m_owner.handleMouseWheelZoom(wheel);
        }

    private:
        // Owner receives input so zoom state stays centralized in TrackViewport.
        TrackViewport& m_owner;

        // False while no project is loaded so the viewport itself owns empty-state drawing.
        bool m_project_loaded{false};
    };

public:
    // Installs the existing waveform track and cursor overlay into viewport-owned content.
    TrackViewport(
        ArrangementView& arrangement_view, CursorOverlay& cursor_overlay,
        const common::audio::ITransport& transport)
        : m_arrangement_view(arrangement_view)
        , m_cursor_overlay(cursor_overlay)
        , m_transport(transport)
        , m_content(*this)
        , m_vblank_attachment(this, [this] { updatePlaybackFollow(); })
    {
        setComponentID("track_viewport");
        m_content.setComponentID("track_viewport_content");
        m_viewport.setComponentID("track_viewport_scroll");

        m_viewport.setScrollBarsShown(true, true);
        m_viewport.setViewedComponent(&m_content, false);
        addAndMakeVisible(m_viewport);

        m_content.addAndMakeVisible(m_arrangement_view);
        m_content.addAndMakeVisible(m_cursor_overlay);
        m_content.setSize(g_track_canvas_width, g_track_canvas_default_height);
        setProjectLoaded(false);
    }

    // Uses default destruction because the viewed component is owned by this shell.
    ~TrackViewport() override = default;

    // Copying is disabled because JUCE component trees and references are not copyable.
    TrackViewport(const TrackViewport&) = delete;

    // Copy assignment is disabled because JUCE component trees and references are not copyable.
    TrackViewport& operator=(const TrackViewport&) = delete;

    // Moving is disabled because hosted component references must remain stable.
    TrackViewport(TrackViewport&&) = delete;

    // Move assignment is disabled because hosted component references must remain stable.
    TrackViewport& operator=(TrackViewport&&) = delete;

    // Stores project-loaded state so the canvas can paint its empty-project message.
    void setProjectLoaded(bool project_loaded)
    {
        m_project_loaded = project_loaded;
        if (!m_project_loaded)
        {
            m_pixels_per_second = g_default_pixels_per_second;
            m_playback_active = false;
            m_playback_start_pending = false;
            m_stop_enabled = false;
        }

        m_content.setProjectLoaded(project_loaded);
        m_arrangement_view.setVisible(project_loaded);
        m_cursor_overlay.setVisible(project_loaded);
        layoutScaledCanvas();
        repaint();
    }

    // Stores the full timeline used to size zoomable content and resets zoom on range changes.
    void setTimelineRange(common::core::TimeRange timeline_range)
    {
        if (m_timeline_range != timeline_range)
        {
            m_timeline_range = timeline_range;
            m_pixels_per_second = g_default_pixels_per_second;
        }

        layoutScaledCanvas();
    }

    // Stores coarse transport state pushed by the controller and handles Stop-button reset.
    void setTransportDisplayState(bool playback_active, bool stop_enabled)
    {
        if (playback_active && !m_playback_active)
        {
            m_playback_start_pending = true;
        }

        const bool stopped_now = m_stop_enabled && !stop_enabled && !playback_active;
        m_playback_active = playback_active;
        m_stop_enabled = stop_enabled;

        if (stopped_now && m_project_loaded && timelineDurationSeconds() > 0.0 &&
            m_transport.position() == m_timeline_range.start)
        {
            setViewportLeft(0);
        }
    }

    // Paints the area around zoomed content when the viewport is larger than the canvas.
    void paint(juce::Graphics& g) override
    {
        g.fillAll(g_track_viewport_colour);
    }

    // Keeps the viewport responsive while preserving zoom-derived content bounds.
    void resized() override
    {
        m_viewport.setBounds(getLocalBounds());
        layoutScaledCanvas();
    }

private:
    // Returns the duration of the currently displayed full timeline.
    [[nodiscard]] double timelineDurationSeconds() const noexcept
    {
        return m_timeline_range.duration().seconds;
    }

    // Returns the default content height after reserving space for the horizontal scrollbar.
    [[nodiscard]] int defaultVisibleCanvasHeight() const noexcept
    {
        return std::max(1, g_track_canvas_default_height - m_viewport.getScrollBarThickness());
    }

    // Keeps each track at one third of the default usable viewport height.
    [[nodiscard]] int primaryTrackHeight() const noexcept
    {
        return std::max(1, defaultVisibleCanvasHeight() / g_tracks_visible_at_default_size);
    }

    // Converts the current pixel density into the width of the full timeline content.
    [[nodiscard]] int scaledContentWidth() const noexcept
    {
        const double duration = timelineDurationSeconds();
        if (!m_project_loaded || duration <= 0.0)
        {
            return std::max(g_track_canvas_width, getWidth());
        }

        const double scaled_width = std::ceil(duration * m_pixels_per_second);
        return std::max(1, static_cast<int>(scaled_width));
    }

    // Calculates the lowest pixel density needed to fit the whole timeline in view.
    [[nodiscard]] double minPixelsPerSecond() const noexcept
    {
        const double duration = timelineDurationSeconds();
        if (!m_project_loaded || duration <= 0.0)
        {
            return g_default_pixels_per_second;
        }

        const double view_width = static_cast<double>(std::max(1, m_viewport.getViewWidth()));
        const double fit_pixels_per_second = view_width / duration;
        return std::min(g_default_pixels_per_second, fit_pixels_per_second);
    }

    // Keeps the stored zoom inside the current timeline and viewport constraints.
    void clampZoomToTimeline()
    {
        m_pixels_per_second =
            std::clamp(m_pixels_per_second, minPixelsPerSecond(), g_max_pixels_per_second);
    }

    // Predicts horizontal scrollbar presence so content height can leave room for it.
    [[nodiscard]] bool needsHorizontalScrollbar(int content_width) const noexcept
    {
        return content_width > getWidth();
    }

    // Extends content only when the viewport grows; the default canvas already fits three tracks.
    [[nodiscard]] int scaledContentHeight(int content_width) const noexcept
    {
        const int horizontal_scrollbar_height =
            needsHorizontalScrollbar(content_width) ? m_viewport.getScrollBarThickness() : 0;
        const int visible_height = std::max(0, getHeight() - horizontal_scrollbar_height);
        return std::max(defaultVisibleCanvasHeight(), visible_height);
    }

    // Keeps vertical content responsive while horizontal content follows zoom state.
    void layoutScaledCanvas()
    {
        clampZoomToTimeline();
        const int content_width = scaledContentWidth();
        m_content.setSize(content_width, scaledContentHeight(content_width));
        m_arrangement_view.setBounds(0, 0, m_content.getWidth(), primaryTrackHeight());
        m_cursor_overlay.setBounds(m_content.getLocalBounds());
        m_cursor_overlay.toFront(false);
    }

    // Changes the horizontal timeline scale around the current transport cursor.
    void handleMouseWheelZoom(const juce::MouseWheelDetails& wheel)
    {
        const float wheel_delta = hasMouseWheelDelta(wheel.deltaY) ? wheel.deltaY : wheel.deltaX;
        if (!m_project_loaded || timelineDurationSeconds() <= 0.0 ||
            !hasMouseWheelDelta(wheel_delta) || wheel.isInertial)
        {
            return;
        }

        const common::core::TimePosition cursor_position = m_transport.position();
        const double wheel_steps = std::max(1.0, static_cast<double>(std::abs(wheel_delta)) * 4.0);
        const double zoom_factor = std::pow(g_mouse_wheel_zoom_factor, wheel_steps);
        const double next_pixels_per_second = wheel_delta > 0.0f
                                                  ? m_pixels_per_second * zoom_factor
                                                  : m_pixels_per_second / zoom_factor;

        m_pixels_per_second =
            std::clamp(next_pixels_per_second, minPixelsPerSecond(), g_max_pixels_per_second);
        layoutScaledCanvas();
        centerViewportOnTime(cursor_position.seconds);
    }

    // Finds the timeline time at the center of the currently visible viewport.
    [[nodiscard]] double viewportCenterTimeSeconds() const noexcept
    {
        const double duration = timelineDurationSeconds();
        if (duration <= 0.0 || m_content.getWidth() <= 0)
        {
            return m_timeline_range.start.seconds;
        }

        const double center_x = static_cast<double>(m_viewport.getViewPositionX()) +
                                static_cast<double>(m_viewport.getViewWidth()) / 2.0;
        const double normalized_x =
            std::clamp(center_x / static_cast<double>(m_content.getWidth()), 0.0, 1.0);
        return m_timeline_range.start.seconds + normalized_x * duration;
    }

    // Repositions the viewport so the supplied timeline time remains near the center.
    void centerViewportOnTime(double time_seconds)
    {
        const double duration = timelineDurationSeconds();
        if (duration <= 0.0 || m_content.getWidth() <= 0)
        {
            return;
        }

        const double normalized_time =
            std::clamp((time_seconds - m_timeline_range.start.seconds) / duration, 0.0, 1.0);
        const double center_x = normalized_time * static_cast<double>(m_content.getWidth());
        const int next_x = static_cast<int>(
            std::round(center_x - static_cast<double>(m_viewport.getViewWidth()) / 2.0));
        setViewportLeft(next_x);
    }

    // Keeps playback visible using controller-pushed state plus current position reads.
    void updatePlaybackFollow()
    {
        if (!m_project_loaded || !m_playback_active || timelineDurationSeconds() <= 0.0)
        {
            return;
        }

        const auto cursor_x = cursorXForTimelinePosition(
            m_transport.position(), m_timeline_range, m_content.getWidth());
        if (!cursor_x.has_value())
        {
            return;
        }

        if (m_playback_start_pending)
        {
            m_playback_start_pending = false;
            scrollToCursorIfOutOfView(*cursor_x);
            return;
        }

        scrollToCursorIfAtRightEdge(*cursor_x);
    }

    // Snaps playback start to the cursor only when the cursor is outside the viewport.
    void scrollToCursorIfOutOfView(float cursor_x)
    {
        const auto view_area = m_viewport.getViewArea();
        if (cursor_x < static_cast<float>(view_area.getX()) ||
            cursor_x >= static_cast<float>(view_area.getRight()))
        {
            setViewportLeft(static_cast<int>(std::floor(cursor_x)));
        }
    }

    // Starts the next visible page at the cursor once playback reaches the right edge.
    void scrollToCursorIfAtRightEdge(float cursor_x)
    {
        const auto view_area = m_viewport.getViewArea();
        if (cursor_x >= static_cast<float>(view_area.getRight()))
        {
            setViewportLeft(static_cast<int>(std::floor(cursor_x)));
        }
    }

    // Moves the horizontal viewport position while preserving the current vertical scroll.
    void setViewportLeft(int requested_x)
    {
        const int max_x = std::max(0, m_content.getWidth() - m_viewport.getViewWidth());
        const int next_x = std::clamp(requested_x, 0, max_x);
        m_viewport.setViewPosition(next_x, m_viewport.getViewPositionY());
    }

    // Existing waveform view hosted as the first track row.
    ArrangementView& m_arrangement_view;

    // Full-canvas cursor and click overlay.
    CursorOverlay& m_cursor_overlay;

    // Read-only transport sampled to keep the viewport near the current cursor during playback.
    const common::audio::ITransport& m_transport;

    // Zoomed canvas that holds the current waveform track and future track rows.
    Content m_content;

    // JUCE scrolling container around the zoomed timeline canvas.
    juce::Viewport m_viewport;

    // Vblank callback used for viewport-follow behavior without continuous controller pushes.
    juce::VBlankAttachment m_vblank_attachment;

    // Full timeline range represented by the current zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Horizontal timeline scale used to size the zoomed content canvas.
    double m_pixels_per_second{g_default_pixels_per_second};

    // Tracks empty vs loaded display mode for layout and zoom gating.
    bool m_project_loaded{false};

    // Coarse playing flag from core::EditorViewState, used to avoid vblank state polling.
    bool m_playback_active{false};

    // True for one follow tick after playback starts so the viewport can reveal the cursor.
    bool m_playback_start_pending{false};

    // Previous stop-button enabled state, used to identify a Stop action reset.
    bool m_stop_enabled{false};
};

// Paints the editor menu strip as flat application chrome instead of a framed control.
class MenuLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Matches the editor background without the default JUCE top/bottom border lines.
    void drawMenuBarBackground(
        juce::Graphics& g, int /*width*/, int /*height*/, bool /*isMouseOverBar*/,
        juce::MenuBarComponent& /*menu_bar*/) override
    {
        g.fillAll(g_editor_background_colour);
    }

    // Keeps the menu item readable on the flat strip and uses a simple hover fill.
    void drawMenuBarItem(
        juce::Graphics& g, int width, int height, int item_index, const juce::String& item_text,
        bool is_mouse_over_item, bool is_menu_open, bool /*isMouseOverBar*/,
        juce::MenuBarComponent& menu_bar) override
    {
        const juce::Rectangle<int> bounds{0, 0, width, height};
        if (is_menu_open || is_mouse_over_item)
        {
            g.setColour(juce::Colours::grey);
            g.fillRect(bounds.reduced(2, 2));
        }

        g.setColour(
            menu_bar.isEnabled() ? juce::Colours::white : juce::Colours::white.withAlpha(0.5f));
        g.setFont(getMenuBarFont(menu_bar, item_index, item_text));
        g.drawFittedText(item_text, bounds.reduced(4, 0), juce::Justification::centred, 1);
    }
};

// Creates child widgets and gives the arrangement view its waveform-thumbnail factory.
EditorView::EditorView(core::IEditorController& controller, AudioPorts audio_ports)
    : m_controller(controller)
    , m_audio_devices(audio_ports.audio_devices)
    , m_audio_meters(audio_ports.meter_source)
    , m_live_input(audio_ports.live_input)
    , m_menu_look_and_feel(std::make_unique<MenuLookAndFeel>())
    , m_menu_bar(this)
    , m_transport_controls(*this)
    , m_master_output_meter(AudioLevelMeterOrientation::Horizontal, "Master")
    , m_signal_chain_panel(*this)
    , m_cursor_overlay(std::make_unique<CursorOverlay>(controller, audio_ports.transport))
    , m_track_viewport(
          std::make_unique<TrackViewport>(
              m_arrangement_view, *m_cursor_overlay, audio_ports.transport))
    , m_meter_vblank_attachment(this, [this] { refreshAudioMeters(); })
{
    setWantsKeyboardFocus(true);

    m_menu_bar.setComponentID("file_menu_bar");
    m_menu_bar.setLookAndFeel(m_menu_look_and_feel.get());
    m_transport_controls.setComponentID("transport_controls");
    m_master_output_meter.setComponentID("master_output_meter");
    m_audio_device_button.setComponentID("audio_device_button");
    m_audio_device_button.setText("Audio Device");
    m_audio_device_button.onClick = [this] { showAudioDeviceSettingsWindow(); };
    m_arrangement_view.setComponentID("arrangement_view");
    m_busy_overlay.setComponentID("busy_overlay");
    m_busy_overlay.setPaintCallback([this] { handleBusyOverlayPainted(); });

    m_arrangement_view.setThumbnailFactory(audio_ports.thumbnail_factory);

    addAndMakeVisible(m_menu_bar);
    addAndMakeVisible(m_transport_controls);
    addAndMakeVisible(m_master_output_meter);
    addAndMakeVisible(m_audio_device_button);
    addAndMakeVisible(m_signal_chain_panel);
    addAndMakeVisible(*m_track_viewport);
    // BusyOverlay is added last so it lands on top of the editor child stack. It also calls
    // toFront() on activation, but adding it here as the final child means the initial Z-order
    // is already correct before the first push.
    addChildComponent(m_busy_overlay);
    m_track_viewport->setProjectLoaded(m_state.project_loaded);

    setSize(1280, 800);
}

// Disconnects the menu bar from this model before base and member teardown begins.
EditorView::~EditorView()
{
    if (m_audio_device_settings_window != nullptr && !m_audio_device_settings_window_reset_pending)
    {
        m_controller.onAudioDeviceSettingsClosed();
    }

    m_audio_device_settings_window.reset();
    m_audio_device_settings_window_reset_pending = false;
    m_busy_overlay.setPaintCallback({});
    m_menu_bar.setLookAndFeel(nullptr);
    m_menu_bar.setModel(nullptr);
}

// Projects controller-derived state into child widgets and cursor mapping state.
void EditorView::setState(const core::EditorViewState& state)
{
    m_state = state;
    if (!m_state.busy.has_value())
    {
        m_after_busy_overlay_paint = {};
    }

    menuItemsChanged();
    m_track_viewport->setProjectLoaded(m_state.project_loaded);
    m_track_viewport->setTimelineRange(m_state.visible_timeline);
    m_track_viewport->setTransportDisplayState(
        m_state.transport.play_pause_shows_pause_icon, m_state.transport.stop_enabled);
    m_transport_controls.setState(m_state.transport);
    updateAudioDeviceButton();
    m_signal_chain_panel.setState(m_state.signal_chain);
    refreshAudioMeters();

    m_arrangement_view.setVisibleTimeline(m_state.visible_timeline);
    m_arrangement_view.setState(m_state.arrangement);

    m_cursor_overlay->setVisibleTimelineRange(m_state.visible_timeline);
    presentUnsavedChangesPromptIfNeeded(m_state.unsaved_changes_prompt);
    presentSaveAsPromptIfNeeded(m_state.save_as_prompt);
    presentRestoreInterruptedPromptIfNeeded(m_state.restore_interrupted_prompt);
    presentInputCalibrationPromptIfNeeded(m_state.input_calibration_prompt);
    presentPluginBrowserIfNeeded(m_state.plugin_browser);
    m_busy_overlay.setBusyState(m_state.busy);
    repaint();
}

// Presents controller-reported workflow failures as one-shot dialogs.
void EditorView::showError(const std::string& message)
{
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Could not complete request")
            .withMessage(juce::String{message.c_str()})
            .withButton("OK"),
        nullptr);
}

// Defers message-thread-only work until BusyOverlay has actually rendered the busy state.
void EditorView::runAfterBusyOverlayPainted(std::function<void()> callback)
{
    if (!m_state.busy.has_value())
    {
        m_after_busy_overlay_paint = {};
        return;
    }

    m_after_busy_overlay_paint = std::move(callback);
    if (m_after_busy_overlay_paint)
    {
        // Startup restore can request the fence before the window has a paintable peer.
        // Waiting for an impossible paint would leave the project-open continuation stuck.
        if (!isShowing())
        {
            // Clear the member before invoking so reentrant fence requests see no stale callback.
            const std::function<void()> pending_callback = std::move(m_after_busy_overlay_paint);
            m_after_busy_overlay_paint = {};
            pending_callback();
            return;
        }

        m_busy_overlay.repaint();
    }
}

// Defers follow-up work until the editor has presented one frame without the busy overlay.
void EditorView::runAfterBusyOverlayRemoved(std::function<void()> callback)
{
    if (!callback)
    {
        return;
    }

    m_after_busy_overlay_removed_paint = std::move(callback);
    if (!isShowing())
    {
        // Headless tests and startup teardown cannot produce a native repaint. Run now so the
        // workflow never waits indefinitely for a presentation that cannot happen.
        const std::function<void()> pending_callback =
            std::move(m_after_busy_overlay_removed_paint);
        m_after_busy_overlay_removed_paint = {};
        pending_callback();
        return;
    }

    repaint();
}

// Paints the background and transport strip behind child widgets.
void EditorView::paint(juce::Graphics& g)
{
    g.fillAll(g_editor_background_colour);

    g.setColour(g_transport_bar_colour);
    g.fillRect(0, g_menu_bar_height, getWidth(), g_transport_bar_height);
    handleBusyOverlayRemovedPainted();
}

// Keeps the control strip above the timeline viewport and signal-chain panel.
void EditorView::resized()
{
    layoutMenuStrip();
    auto top_area = getLocalBounds();
    top_area.removeFromTop(g_menu_bar_height);
    auto transport_row = top_area.removeFromTop(g_transport_bar_height);
    auto control_row =
        transport_row.withTrimmedLeft(g_content_inset).withTrimmedRight(g_content_inset);
    control_row = control_row.withSizeKeepingCentre(
        control_row.getWidth(), std::min(g_transport_height, control_row.getHeight()));

    m_transport_controls.setBounds(
        control_row.removeFromLeft(std::min(g_transport_controls_width, control_row.getWidth())));

    const int master_meter_width = std::min(g_master_meter_width, control_row.getWidth());
    if (master_meter_width >= g_master_meter_min_width)
    {
        m_master_output_meter.setVisible(true);
        m_master_output_meter.setBounds(
            control_row.removeFromRight(master_meter_width).reduced(0, 4));
    }
    else
    {
        m_master_output_meter.setVisible(false);
        m_master_output_meter.setBounds({});
    }

    auto bottom_area = trackViewportBounds();
    const int target_signal_chain_panel_height = std::clamp(
        bottom_area.getHeight() / 3,
        g_signal_chain_panel_min_height,
        g_signal_chain_panel_max_height);
    const int max_signal_chain_panel_height =
        std::max(0, bottom_area.getHeight() - g_control_gap - g_track_viewport_min_height);
    const int signal_chain_panel_height =
        std::min(target_signal_chain_panel_height, max_signal_chain_panel_height);
    auto signal_chain_panel_bounds = bottom_area.removeFromBottom(signal_chain_panel_height);
    if (signal_chain_panel_height > 0)
    {
        bottom_area.removeFromBottom(std::min(g_control_gap, bottom_area.getHeight()));
    }
    m_track_viewport->setBounds(bottom_area);
    m_signal_chain_panel.setBounds(signal_chain_panel_bounds);
    m_busy_overlay.setBounds(getLocalBounds());
}

// Retries the startup focus request if this component is explicitly shown later.
void EditorView::visibilityChanged()
{
    requestInitialKeyboardFocusIfReady();
}

// Retries the startup focus request when JUCE attaches the editor under a window peer.
void EditorView::parentHierarchyChanged()
{
    requestInitialKeyboardFocusIfReady();
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

// Returns the single File menu displayed by the editor.
juce::StringArray EditorView::getMenuBarNames()
{
    return {"File"};
}

// Builds the File menu using only controller-derived state.
juce::PopupMenu EditorView::getMenuForIndex(int top_level_menu_index, const juce::String& menu_name)
{
    if (top_level_menu_index != 0 || menu_name != "File")
    {
        return {};
    }

    juce::PopupMenu menu;
    menu.addItem(g_open_command, "Open...", m_state.open_enabled);
    menu.addItem(g_import_command, "Import...", m_state.import_enabled);
    menu.addSeparator();
    menu.addItem(g_save_command, "Save", m_state.save_enabled);
    menu.addItem(g_save_as_command, "Save As...", m_state.save_as_enabled);
    menu.addItem(g_publish_command, "Publish...", m_state.publish_enabled);
    menu.addSeparator();
    menu.addItem(g_close_command, "Close", m_state.close_enabled);
    menu.addItem(g_exit_command, "Exit");
    return menu;
}

// Routes File menu selections to either a chooser or a direct controller intent.
void EditorView::menuItemSelected(int menu_item_id, int /*top_level_menu_index*/)
{
    switch (menu_item_id)
    {
        case g_open_command:
        {
            if (m_state.open_enabled)
            {
                showOpenChooser();
            }
            break;
        }
        case g_import_command:
        {
            if (m_state.import_enabled)
            {
                showImportChooser();
            }
            break;
        }
        case g_save_command:
        {
            if (!m_state.save_enabled)
            {
                break;
            }
            if (m_state.save_requires_destination)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            else
            {
                m_controller.onSaveRequested();
            }
            break;
        }
        case g_save_as_command:
        {
            if (m_state.save_as_enabled)
            {
                showSaveAsChooser(SaveAsChooserPurpose::UserSaveAs);
            }
            break;
        }
        case g_publish_command:
        {
            if (m_state.publish_enabled)
            {
                showPublishChooser();
            }
            break;
        }
        case g_close_command:
        {
            if (m_state.close_enabled)
            {
                m_controller.onCloseRequested();
            }
            break;
        }
        case g_exit_command:
        {
            m_controller.onExitRequested();
            break;
        }
        default:
        {
            break;
        }
    }
}

// Opens an asynchronous file chooser and sends accepted project package paths to the controller.
void EditorView::showOpenChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Open Rock Hero Project",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            safe_this->m_controller.onOpenRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous file chooser and sends accepted import paths to the controller.
void EditorView::showImportChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Import Rock Hero Song or Rocksmith PSARC",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rock;*.psarc");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (!file.existsAsFile())
            {
                return;
            }

            safe_this->m_controller.onImportRequested(common::core::pathFromJuceFile(file));
        });
}

// Opens an asynchronous file chooser and sends accepted save paths to the controller.
void EditorView::showSaveAsChooser(SaveAsChooserPurpose purpose)
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Save Rock Hero Project",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.rhp");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safe_this, purpose](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                if (purpose == SaveAsChooserPurpose::DeferredAction)
                {
                    safe_this->m_controller.onSaveAsCancelled();
                }
                return;
            }

            safe_this->m_controller.onSaveAsRequested(pathWithRhpExtension(file));
        });
}

// Opens an asynchronous file chooser and sends accepted native song package paths to the
// controller.
void EditorView::showPublishChooser()
{
    m_file_chooser = std::make_unique<juce::FileChooser>(
        "Publish Rock Hero Song (.rock)",
        publishChooserInitialFile(m_state.suggested_publish_file),
        "*.rock");

    const juce::Component::SafePointer<EditorView> safe_this{this};
    m_file_chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safe_this](const juce::FileChooser& chooser) {
            if (safe_this == nullptr)
            {
                return;
            }

            const auto file = chooser.getResult();
            if (file.getFullPathName().isEmpty())
            {
                return;
            }

            safe_this->m_controller.onPublishRequested(pathWithRockExtension(file));
        });
}

// Shows each distinct unsaved-changes prompt once and reports the selected decision.
void EditorView::presentUnsavedChangesPromptIfNeeded(
    const std::optional<core::UnsavedChangesPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_unsaved_changes_prompt.reset();
        return;
    }

    if (m_last_presented_unsaved_changes_prompt == prompt)
    {
        return;
    }

    m_last_presented_unsaved_changes_prompt = prompt;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Unsaved changes")
            .withMessage(unsavedChangesPromptMessage(prompt->prompted_action))
            .withButton("Save")
            .withButton("Discard")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe_this](int button_index) {
            if (safe_this == nullptr)
            {
                return;
            }

            switch (button_index)
            {
                case 0:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Save);
                    break;
                }
                case 1:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Discard);
                    break;
                }
                default:
                {
                    safe_this->m_controller.onUnsavedChangesDecision(
                        core::UnsavedChangesDecision::Cancel);
                    break;
                }
            }
        });
}

// Shows a controller-requested Save As chooser once and reports cancellation when needed.
void EditorView::presentSaveAsPromptIfNeeded(const std::optional<core::SaveAsPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_save_as_prompt.reset();
        return;
    }

    if (m_last_presented_save_as_prompt == prompt)
    {
        return;
    }

    m_last_presented_save_as_prompt = prompt;
    showSaveAsChooser(SaveAsChooserPurpose::DeferredAction);
}

// Shows each distinct interrupted-restore prompt once and reports Retry as the standard OK button.
void EditorView::presentRestoreInterruptedPromptIfNeeded(
    const std::optional<core::RestoreInterruptedPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        m_last_presented_restore_interrupted_prompt.reset();
        return;
    }

    if (m_last_presented_restore_interrupted_prompt == prompt)
    {
        return;
    }

    m_last_presented_restore_interrupted_prompt = prompt;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Project did not finish opening")
            .withMessage(
                juce::String{"The previous project did not finish opening:\n\n"} +
                common::core::juceStringFromPath(prompt->project_file) +
                "\n\nTry opening it again?")
            .withButton("OK")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe_this](int button_index) {
            if (safe_this == nullptr)
            {
                return;
            }

            const core::RestoreInterruptedDecision decision =
                button_index == 0 ? core::RestoreInterruptedDecision::Retry
                                  : core::RestoreInterruptedDecision::Cancel;
            safe_this->m_controller.onRestoreInterruptedDecision(decision);
        });
}

// Opens or closes the input calibration prompt from controller-derived state.
void EditorView::presentInputCalibrationPromptIfNeeded(
    const std::optional<core::InputCalibrationPrompt>& prompt)
{
    if (!prompt.has_value())
    {
        if (m_input_calibration_window != nullptr)
        {
            m_input_calibration_window->setVisible(false);

            const juce::Component::SafePointer<EditorView> safe_this{this};
            juce::MessageManager::callAsync([safe_this] {
                EditorView* const view = safe_this.getComponent();
                // The calibration window can request this from its own timer or close callback.
                // Hide now, but defer destruction until that event stack has unwound.
                if (view != nullptr && !view->m_state.input_calibration_prompt.has_value())
                {
                    view->m_input_calibration_window.reset();
                }
            });
        }
        return;
    }

    if (m_input_calibration_window != nullptr)
    {
        m_input_calibration_window->toFront(true);
        return;
    }

    m_input_calibration_window = std::make_unique<InputCalibrationWindow>(
        m_controller, &m_live_input, *prompt, isShowing() ? this : nullptr);
}

// Opens or refreshes the plugin browser top-level window from controller-derived state.
void EditorView::presentPluginBrowserIfNeeded(const core::PluginBrowserViewState& state)
{
    if (!state.visible)
    {
        if (m_plugin_browser_window != nullptr)
        {
            m_plugin_browser_window->setVisible(false);

            const juce::Component::SafePointer<EditorView> safe_this{this};
            juce::MessageManager::callAsync([safe_this] {
                EditorView* const view = safe_this.getComponent();
                // The browser can request this while JUCE is dispatching its own close event.
                // Hide now, but defer destruction until that event stack has unwound.
                // This is a Component lifetime guard: SafePointer is JUCE's built-in way for
                // posted UI callbacks to observe deletion without keeping the Component alive.
                if (view != nullptr && !view->m_state.plugin_browser.visible)
                {
                    view->m_plugin_browser_window.reset();
                }
            });
        }
        return;
    }

    if (m_plugin_browser_window == nullptr)
    {
        auto& listener = static_cast<PluginBrowserWindow::Listener&>(*this);
        m_plugin_browser_window = std::make_unique<PluginBrowserWindow>(listener);
        if (isShowing())
        {
            m_plugin_browser_window->centreAroundComponent(this, 760, 500);
        }
    }

    m_plugin_browser_window->setVisible(true);
    m_plugin_browser_window->setState(state);
    m_plugin_browser_window->toFront(true);
}

// Re-positions only the menu-bar children when the audio-device label changes width, so a
// status-text update does not relayout the transport row, track viewport, or signal-chain panel.
void EditorView::layoutMenuStrip()
{
    juce::Rectangle<int> menu_bar_bounds = getLocalBounds().removeFromTop(g_menu_bar_height);
    const juce::Rectangle<int> audio_device_bounds =
        menu_bar_bounds.removeFromRight(audioDeviceButtonWidth(
            m_audio_device_button, g_menu_bar_height, menu_bar_bounds.getWidth()));
    m_menu_bar.setBounds(menu_bar_bounds);
    m_audio_device_button.setBounds(audio_device_bounds);
}

// Applies controller-derived audio routing state to the menu-bar button.
void EditorView::updateAudioDeviceButton()
{
    const juce::String status_text{m_state.audio_device_status_text.c_str()};
    if (m_audio_device_button.getText() != status_text)
    {
        m_audio_device_button.setText(status_text);
        layoutMenuStrip();
    }

    m_audio_device_button.setEnabled(
        m_state.audio_devices_available && m_state.audio_device_settings_enabled);
}

// Samples meter values at display cadence. This intentionally bypasses EditorController state
// because meters are volatile playback display data, like cursor position.
void EditorView::refreshAudioMeters()
{
    const common::audio::AudioMeterSnapshot snapshot = m_audio_meters.audioMeterSnapshot();

    m_master_output_meter.setLevel(snapshot.master_output);
    m_signal_chain_panel.setMeterLevels(snapshot.live_rig_input, snapshot.live_rig_output);
}

// Opens the audio-device settings window when a hardware-configuration backend is available.
void EditorView::showAudioDeviceSettingsWindow()
{
    if (!m_state.audio_device_settings_enabled)
    {
        return;
    }

    if (m_audio_device_settings_window != nullptr)
    {
        m_audio_device_settings_window->toFront(true);
        return;
    }

    // Hand the dispatcher to the settings window so OK and Cancel can dismiss the dialog
    // immediately and run device-manager work behind the editor's blocking busy overlay.
    // juce::AudioDeviceManager occupies the message thread, so the overlay's blocking
    // presentation paints once before the freeze rather than animating through it.
    const juce::Component::SafePointer<EditorView> safe_this{this};
    if (!m_controller.onAudioDeviceSettingsOpenRequested())
    {
        return;
    }

    m_audio_device_settings_window_reset_pending = false;
    m_audio_device_settings_window = AudioDeviceSettingsWindow::show(
        m_audio_devices,
        m_audio_device_button,
        [safe_this](std::function<void()> operation, std::function<void()> after_cleared) {
            if (auto* view = safe_this.getComponent())
            {
                view->m_controller.onAudioDeviceChangeRequested(
                    std::move(operation), std::move(after_cleared));
                return;
            }

            if (operation)
            {
                operation();
            }
            if (after_cleared)
            {
                after_cleared();
            }
        },
        [safe_this] {
            if (auto* view = safe_this.getComponent())
            {
                view->m_audio_device_settings_window_reset_pending = true;
                view->m_controller.onAudioDeviceSettingsClosed();
                view->scheduleAudioDeviceSettingsWindowReset();
            }
        });
}

// Clears the owner-held settings window after JUCE and view callbacks have unwound.
void EditorView::scheduleAudioDeviceSettingsWindowReset()
{
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::MessageManager::callAsync([safe_this] {
        if (auto* view = safe_this.getComponent())
        {
            view->m_audio_device_settings_window.reset();
            view->m_audio_device_settings_window_reset_pending = false;
        }
    });
}

// Runs the single pending fence callback after BusyOverlay has crossed its paint path. Showing
// views post a follow-up message so expensive work stays out of the real paint callback.
void EditorView::handleBusyOverlayPainted()
{
    if (!m_after_busy_overlay_paint)
    {
        return;
    }

    std::function<void()> callback = std::move(m_after_busy_overlay_paint);
    m_after_busy_overlay_paint = {};
    if (!isShowing())
    {
        callback();
        return;
    }

    juce::MessageManager::callAsync(std::move(callback));
}

// Runs the single pending clear-fence callback after the editor has painted with busy cleared.
void EditorView::handleBusyOverlayRemovedPainted()
{
    if (m_state.busy.has_value() || !m_after_busy_overlay_removed_paint)
    {
        return;
    }

    std::function<void()> callback = std::move(m_after_busy_overlay_removed_paint);
    m_after_busy_overlay_removed_paint = {};
    juce::MessageManager::callAsync(std::move(callback));
}

// Returns the area shared by the track viewport and bottom signal-chain panel.
juce::Rectangle<int> EditorView::trackViewportBounds() const
{
    auto area = getLocalBounds();
    area.removeFromTop(g_menu_bar_height);
    area.removeFromTop(g_transport_bar_height);
    area.removeFromTop(g_control_gap);
    area.removeFromLeft(g_content_inset);
    area.removeFromRight(g_content_inset);
    area.removeFromBottom(g_content_inset);
    return area;
}

// Schedules focus after the current attach/show callback so the native peer can activate first.
void EditorView::requestInitialKeyboardFocusIfReady()
{
    if (m_has_requested_initial_keyboard_focus || !isShowing())
    {
        return;
    }

    m_has_requested_initial_keyboard_focus = true;
    const juce::Component::SafePointer<EditorView> safe_this{this};
    juce::MessageManager::callAsync([safe_this] {
        if (safe_this == nullptr)
        {
            return;
        }

        if (!safe_this->isShowing())
        {
            safe_this->m_has_requested_initial_keyboard_focus = false;
            return;
        }

        safe_this->grabKeyboardFocus();
    });
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

// Opens the plugin browser for a specific insertion slot selected in the signal-chain panel.
void EditorView::onInsertPluginPressed(std::size_t chain_index)
{
    if (!m_state.signal_chain.insert_plugin_enabled)
    {
        return;
    }

    m_controller.onPluginInsertSlotSelected(chain_index);
}

// Forwards row-level remove intent to the controller after checking derived availability.
void EditorView::onRemovePluginPressed(std::string instance_id)
{
    if (!m_state.signal_chain.remove_plugins_enabled)
    {
        return;
    }

    m_controller.onRemovePluginRequested(std::move(instance_id));
}

// Forwards row-level move intent to the controller after checking derived availability.
void EditorView::onMovePluginPressed(std::string instance_id, std::size_t destination_index)
{
    if (!m_state.signal_chain.move_plugins_enabled)
    {
        return;
    }

    m_controller.onMovePluginRequested(std::move(instance_id), destination_index);
}

// Forwards the authored block placement so the controller can persist it with the project. This is
// document state, not a gated user action, so it is reported regardless of edit availability.
void EditorView::onSignalChainPlacementChanged(std::vector<std::size_t> block_indices)
{
    m_controller.onSignalChainPlacementChanged(std::move(block_indices));
}

// Forwards row-level open intent to the controller; controller-side routing handles busy gating.
void EditorView::onOpenPluginPressed(std::string instance_id)
{
    m_controller.onOpenPluginRequested(std::move(instance_id));
}

// Opens input calibration through the controller when the command is available.
void EditorView::onInputCalibrationPressed()
{
    if (!m_state.signal_chain.input_calibrate_enabled)
    {
        return;
    }
    m_controller.onInputCalibrationRequested();
}

// Forwards output gain slider changes to the controller when output controls are enabled.
void EditorView::onOutputGainChanged(double gain_db)
{
    if (!m_state.signal_chain.output_gain_controls_enabled)
    {
        return;
    }
    m_controller.onOutputGainChanged(gain_db);
}

// Forwards browser rescan intent to the workflow controller.
void EditorView::onPluginBrowserScanRequested()
{
    m_controller.onPluginCatalogScanRequested();
}

// Forwards the selected browser plugin to the workflow controller.
void EditorView::onPluginBrowserAddRequested(std::string plugin_id)
{
    m_controller.onSelectedPluginInsertRequested(std::move(plugin_id));
}

// Forwards browser close intent to the workflow controller.
void EditorView::onPluginBrowserClosed()
{
    m_controller.onPluginBrowserClosed();
}

} // namespace rock_hero::editor::ui
