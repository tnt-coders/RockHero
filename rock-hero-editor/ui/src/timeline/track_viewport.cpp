#include "track_viewport.h"

#include "shared/editor_theme.h"
#include "tab/tab_view.h"
#include "timeline/arrangement_view.h"
#include "timeline/cursor_overlay.h"
#include "timeline/timeline_cursor.h"
#include "tone/tone_automation_lanes_view.h"
#include "tone/tone_track_view.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <limits>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_track_canvas_default_height{720};
constexpr int g_tracks_visible_at_default_size{3};
// Fixed label-height tone row: 13px region names plus the region insets, nothing more.
constexpr int g_tone_track_height{30};
constexpr double g_mouse_wheel_zoom_factor{1.2};
constexpr float g_min_mouse_wheel_delta{std::numeric_limits<float>::epsilon()};
constexpr int g_tempo_grid_dot_size{1};
constexpr int g_tempo_grid_dot_gap{1};

// Playback-follow tuning: the cursor travels across a stationary window and may reach the
// trigger fraction of the view width before the window glides forward far enough to drop it
// back at the pin fraction; the glide runs for the shift duration with a cubic ease-out. The
// cursor keeps playing during the glide, so it lands slightly right of the pin fraction —
// intended, since the shift target is captured when the glide starts.
constexpr double g_follow_shift_trigger_fraction{0.8};
constexpr double g_follow_shift_pin_fraction{0.05};
constexpr double g_follow_shift_duration_seconds{0.3};

// Fixed-cursor smooth scrolling pins the cursor a little inside the left edge: most of the
// screen is road ahead, with enough recent context behind the cursor to stay oriented (user
// tuned 1/3 -> 0.05 -> 0.2 -> 0.15 -> 0.1).
constexpr double g_smooth_follow_pin_fraction{0.1};

// Treats tiny wheel deltas as absent so zoom input stays stable across platforms.
[[nodiscard]] bool hasMouseWheelDelta(float delta) noexcept
{
    return std::abs(delta) > g_min_mouse_wheel_delta;
}

// Returns the first dotted-grid row in the current paint clip, preserving the pattern's content
// origin so partial cursor repaints do not make dots shimmer vertically.
[[nodiscard]] int firstTempoGridDotYInClip(
    juce::Rectangle<int> bounds, juce::Rectangle<int> visible_clip) noexcept
{
    constexpr int dot_stride = g_tempo_grid_dot_size + g_tempo_grid_dot_gap;
    const int first_visible_y = std::max(bounds.getY(), visible_clip.getY());
    const int offset = (first_visible_y - bounds.getY()) % dot_stride;
    return offset == 0 ? first_visible_y : first_visible_y + dot_stride - offset;
}

// Appends the visible 1px dots of one vertical tempo-grid line to a shared list, clipped to the
// visible repaint span. The caller batches the dots into a single fillRectList per color so a
// wide, line-dense repaint (zooming, or clicking the cursor while zoomed out) costs one edge-table
// fill instead of one Graphics::fillRect per dot.
void appendDottedTempoGridLine(
    juce::RectangleList<float>& dots, int x, juce::Rectangle<int> bounds,
    juce::Rectangle<int> visible_clip)
{
    constexpr int dot_stride = g_tempo_grid_dot_size + g_tempo_grid_dot_gap;
    const int bottom = std::min(bounds.getBottom(), visible_clip.getBottom());

    for (int y = firstTempoGridDotYInClip(bounds, visible_clip); y < bottom; y += dot_stride)
    {
        dots.addWithoutMerging(
            juce::Rectangle<int>{x, y, g_tempo_grid_dot_size, g_tempo_grid_dot_size}.toFloat());
    }
}

// Draws subdivision, beat, and measure grid dots from cached column positions, restricted to the
// current paint's repaint clip. The clip only trims which cached columns and dot rows get drawn;
// it does not change the geometry itself, so results stay stable regardless of how much of the
// canvas repaints.
void drawTempoGridDots(
    juce::Graphics& g, const std::vector<int>& subdivision_grid_x,
    const std::vector<int>& beat_grid_x, const std::vector<int>& measure_grid_x,
    juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
    {
        return;
    }

    const juce::Rectangle<int> visible_clip = g.getClipBounds();

    // Collect dots per color, then issue one batched fill each. Separating the colors keeps the
    // fills homogeneous; the alternative of one fill per line scaled the draw-call count by the
    // dot count per line, which is what made zoomed-out repaints lag.
    juce::RectangleList<float> subdivision_dots;
    juce::RectangleList<float> beat_dots;
    juce::RectangleList<float> measure_dots;
    const auto append_columns = [&](const std::vector<int>& columns,
                                    juce::RectangleList<float>& dots) {
        for (const int x : columns)
        {
            const int absolute_x = bounds.getX() + x;
            if (absolute_x < visible_clip.getX() || absolute_x >= visible_clip.getRight())
            {
                continue;
            }

            appendDottedTempoGridLine(dots, absolute_x, bounds, visible_clip);
        }
    };

    append_columns(subdivision_grid_x, subdivision_dots);
    append_columns(beat_grid_x, beat_dots);
    append_columns(measure_grid_x, measure_dots);

    if (!subdivision_dots.isEmpty())
    {
        g.setColour(editorTheme().grid_subdivision);
        g.fillRectList(subdivision_dots);
    }
    if (!beat_dots.isEmpty())
    {
        g.setColour(editorTheme().grid_beat);
        g.fillRectList(beat_dots);
    }
    if (!measure_dots.isEmpty())
    {
        g.setColour(editorTheme().grid_measure);
        g.fillRectList(measure_dots);
    }
}

} // namespace

// Stores the owning viewport shell so wheel input can update shared zoom state.
TrackViewport::Content::Content(TrackViewport& owner)
    : m_owner(owner)
{
    setInterceptsMouseClicks(false, true);
}

// Stores whether child track content should replace the empty-project message.
void TrackViewport::Content::setProjectLoaded(bool project_loaded)
{
    m_project_loaded = project_loaded;
    repaint();
}

// Stores the shared visible-span grid lines as per-rank column caches and repaints.
void TrackViewport::Content::setGridLines(const std::vector<core::TempoGridLine>& grid_lines)
{
    m_subdivision_grid_x.clear();
    m_beat_grid_x.clear();
    m_measure_grid_x.clear();

    for (const core::TempoGridLine& line : grid_lines)
    {
        switch (line.rank)
        {
            case core::TempoGridLineRank::Subdivision:
            {
                m_subdivision_grid_x.push_back(line.x);
                break;
            }
            case core::TempoGridLineRank::Beat:
            {
                m_beat_grid_x.push_back(line.x);
                break;
            }
            case core::TempoGridLineRank::Measure:
            {
                m_measure_grid_x.push_back(line.x);
                break;
            }
        }
    }

    repaint();
}

// Draws the timeline canvas, tempo grid, waveform row background, and empty-project text.
void TrackViewport::Content::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(editorTheme().timeline_backdrop);

    if (m_project_loaded)
    {
        // Track-row layering contract: the canvas paints one background band per track row,
        // then the tempo grid over every band; row content components draw above the grid
        // (sparse visualizations let it show through, discrete regions cover it), and the
        // cursor overlay owns everything transient on top.
        g.setColour(editorTheme().waveform_row_background);
        g.fillRect(bounds.withHeight(m_owner.primaryTrackHeight()));
        g.setColour(editorTheme().tone_row_background);
        g.fillRect(
            juce::Rectangle<int>{
                0, m_owner.primaryTrackHeight(), bounds.getWidth(), m_owner.toneTrackHeight()
            });
        // Everything below the tone row is the automation lanes band; painting it here (with the
        // grid on top) lets the lanes view stay background-free so the grid shows through it.
        // The lanes share the waveform row's band so the grid reads identically on both.
        const int lanes_top = m_owner.primaryTrackHeight() + m_owner.toneTrackHeight();
        g.setColour(editorTheme().waveform_row_background);
        g.fillRect(
            juce::Rectangle<int>{
                0, lanes_top, bounds.getWidth(), std::max(0, bounds.getHeight() - lanes_top)
            });
        drawTempoGridDots(g, m_subdivision_grid_x, m_beat_grid_x, m_measure_grid_x, bounds);
        return;
    }

    g.setColour(editorTheme().muted_text);
    g.drawText("No Project Loaded", bounds, juce::Justification::centred);
}

// Converts normal wheel movement over timeline content into horizontal zoom.
void TrackViewport::Content::mouseWheelMove(
    const juce::MouseEvent& /*event*/, const juce::MouseWheelDetails& wheel)
{
    m_owner.handleMouseWheelZoom(wheel);
}

// Repaints ruler state after scrollbars, wheel scrolling, or programmatic movement.
void TrackViewport::TimelineViewport::visibleAreaChanged(
    const juce::Rectangle<int>& /*new_visible_area*/)
{
    if (on_visible_area_changed)
    {
        on_visible_area_changed();
    }
}

// Installs the existing waveform track and cursor overlay into viewport-owned content.
TrackViewport::TrackViewport(
    core::IEditorController& controller, ArrangementView& arrangement_view, TabView& tab_view,
    ToneTrackView& tone_track_view, ToneAutomationLanesView& tone_automation_lanes_view,
    CursorOverlay& cursor_overlay, const common::audio::ITransport& transport)
    : m_controller(controller)
    , m_arrangement_view(arrangement_view)
    , m_tab_view(tab_view)
    , m_tone_track_view(tone_track_view)
    , m_tone_automation_lanes_view(tone_automation_lanes_view)
    , m_cursor_overlay(cursor_overlay)
    , m_transport(transport)
    , m_content(*this)
    , m_vblank_attachment(this, [this] {
        updatePlaybackFollow();
        updateRulerCursor();
    })
{
    setComponentID("track_viewport");
    m_content.setComponentID("track_viewport_content");
    m_viewport.setComponentID("track_viewport_scroll");
    m_viewport.on_visible_area_changed = [this] {
        updateRulerView();
        refreshTimelineGridForViewChange();
    };

    m_viewport.setScrollBarsShown(true, true);
    m_viewport.setViewedComponent(&m_content, false);
    addAndMakeVisible(m_timeline_ruler);
    addAndMakeVisible(m_viewport);

    // Track content starts hidden to match the project-not-loaded member defaults;
    // setProjectLoaded early-outs on an unchanged flag, so no constructor push runs here.
    // Add order layers the rows: tablature draws over the waveform, cursor overlay over all.
    m_content.addChildComponent(m_arrangement_view);
    m_content.addChildComponent(m_tab_view);
    m_content.addChildComponent(m_tone_track_view);
    m_content.addChildComponent(m_tone_automation_lanes_view);
    m_content.addChildComponent(m_cursor_overlay);
    m_content.setSize(g_track_canvas_width, g_track_canvas_default_height);
    m_timeline_ruler.setCursorPlacementCallback([this](common::core::TimePosition position) {
        m_controller.onTimelineSeekRequested(position);
    });
}

// Stores project-loaded state so the canvas can paint its empty-project message. Every full
// controller state push repeats the flag, so an unchanged flag returns before the relayout
// and grid rescan in layoutScaledCanvas.
void TrackViewport::setProjectLoaded(bool project_loaded)
{
    if (m_project_loaded == project_loaded)
    {
        return;
    }

    m_project_loaded = project_loaded;
    if (!m_project_loaded)
    {
        m_pixels_per_second = g_default_pixels_per_second;
        m_playback_active = false;
        m_stop_enabled = false;
        m_cursor_focus_pending = false;
    }

    m_content.setProjectLoaded(project_loaded);
    m_timeline_ruler.setProjectLoaded(project_loaded);
    m_arrangement_view.setVisible(project_loaded);
    m_tab_view.setVisible(project_loaded);
    m_tone_track_view.setVisible(project_loaded);
    m_tone_automation_lanes_view.setVisible(project_loaded);
    m_cursor_overlay.setVisible(project_loaded);
    layoutScaledCanvas();
    repaint();
}

// Stores the full timeline used to size zoomable content and resets zoom on range changes.
// An unchanged range returns early: every controller state push repeats the range, and the
// relayout would otherwise pay a redundant grid rescan per push.
void TrackViewport::setTimelineRange(common::core::TimeRange timeline_range)
{
    if (m_timeline_range == timeline_range)
    {
        return;
    }

    m_timeline_range = timeline_range;
    m_pixels_per_second = g_default_pixels_per_second;
    layoutScaledCanvas();
}

// Stores the tempo map and grid note value used by the content background grid and the ruler.
void TrackViewport::setGrid(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value)
{
    if (m_tempo_map == tempo_map && m_grid_note_value == grid_note_value)
    {
        return;
    }

    m_tempo_map = tempo_map;
    m_grid_note_value = grid_note_value;
    m_timeline_ruler.setGrid(m_tempo_map, m_grid_note_value);
    refreshTimelineGrid();
}

// Stores coarse transport state pushed by the controller and handles Stop-button reset.
void TrackViewport::setTransportDisplayState(bool playback_active, bool stop_enabled)
{
    const bool stopped_now = m_stop_enabled && !stop_enabled && !playback_active;
    m_playback_active = playback_active;
    m_stop_enabled = stop_enabled;

    if (stopped_now && m_project_loaded && timelineDurationSeconds() > 0.0 &&
        m_transport.position() == m_timeline_range.start)
    {
        setViewportLeft(0);
    }
}

// Stores the tablature lane count and relays out when the waveform row height changes: counts
// past the six-string reference grow the row so lane spacing stays at the reference density.
void TrackViewport::setTabDisplayedStrings(int displayed_strings)
{
    if (m_tab_displayed_strings == displayed_strings)
    {
        return;
    }

    m_tab_displayed_strings = displayed_strings;
    layoutScaledCanvas();
}

// Requests one viewport recenter once a restored project cursor is available.
void TrackViewport::requestCursorFocus()
{
    m_cursor_focus_pending = true;
    focusCursorIfPending();
}

// Installs the callback that reports user-driven zoom changes for persistence.
void TrackViewport::setZoomChangedCallback(std::function<void(double)> on_zoom_changed)
{
    m_on_zoom_changed = std::move(on_zoom_changed);
}

// Applies a restored per-project zoom on a fresh load; clamped by layoutScaledCanvas and never
// re-reported through the zoom-changed callback because it did not come from the user.
void TrackViewport::setRestoredZoomPixelsPerSecond(double pixels_per_second)
{
    if (!std::isfinite(pixels_per_second) || pixels_per_second <= 0.0)
    {
        return;
    }

    m_pixels_per_second = pixels_per_second;
    layoutScaledCanvas();
}

// Paints the area around zoomed content when the viewport is larger than the canvas.
void TrackViewport::paint(juce::Graphics& g)
{
    g.fillAll(editorTheme().timeline_backdrop);
}

// Keeps the viewport responsive while preserving zoom-derived content bounds.
void TrackViewport::resized()
{
    auto bounds = getLocalBounds();
    m_timeline_ruler.setBounds(bounds.removeFromTop(g_timeline_ruler_height));
    m_viewport.setBounds(bounds);
    layoutScaledCanvas();
    focusCursorIfPending();
}

// Returns the duration of the currently displayed full timeline.
double TrackViewport::timelineDurationSeconds() const noexcept
{
    return m_timeline_range.duration().seconds;
}

// Returns the default content height after reserving space for the horizontal scrollbar.
int TrackViewport::defaultVisibleCanvasHeight() const noexcept
{
    return std::max(1, g_track_canvas_default_height - m_viewport.getScrollBarThickness());
}

// Keeps the tone row at a fixed label-height strip: regions only need to show their name until
// clicking one expands per-automation sub-lanes (planned), so taller rows are wasted space.
int TrackViewport::toneTrackHeight() const noexcept
{
    return g_tone_track_height;
}

// Sizes the waveform row so tablature lanes keep the six-string reference density at any count.
// Without a chart the row stays at one third of the usable viewport for a plain waveform; with a
// chart the row scales to the string count, so a four-string bass shrinks the row to fit its
// lanes and an eight-string display grows it (the vertical scrollbar absorbs the overflow).
int TrackViewport::primaryTrackHeight() const noexcept
{
    const int reference_height =
        std::max(1, defaultVisibleCanvasHeight() / g_tracks_visible_at_default_size);
    if (m_tab_displayed_strings <= 0)
    {
        return reference_height;
    }

    return std::max(1, reference_height * m_tab_displayed_strings / g_tab_reference_string_count);
}

// Converts the current pixel density into the width of the full timeline content.
int TrackViewport::scaledContentWidth() const noexcept
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
double TrackViewport::minPixelsPerSecond() const noexcept
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
void TrackViewport::clampZoomToTimeline()
{
    m_pixels_per_second =
        std::clamp(m_pixels_per_second, minPixelsPerSecond(), g_max_pixels_per_second);
}

// Predicts horizontal scrollbar presence so content height can leave room for it.
bool TrackViewport::needsHorizontalScrollbar(int content_width) const noexcept
{
    return content_width > getWidth();
}

// Extends content when the viewport grows or the track rows outgrow the default canvas (a
// grown waveform row for many-string tablature); the vertical scrollbar absorbs any overflow.
int TrackViewport::scaledContentHeight(int content_width) const noexcept
{
    const int horizontal_scrollbar_height =
        needsHorizontalScrollbar(content_width) ? m_viewport.getScrollBarThickness() : 0;
    const int visible_height = std::max(0, m_viewport.getHeight() - horizontal_scrollbar_height);
    const int track_rows_height =
        primaryTrackHeight() + toneTrackHeight() + m_tone_automation_lanes_view.totalHeight();
    return std::max({defaultVisibleCanvasHeight(), visible_height, track_rows_height});
}

// Keeps vertical content responsive while horizontal content follows zoom state.
void TrackViewport::layoutScaledCanvas()
{
    clampZoomToTimeline();
    const int content_width = scaledContentWidth();
    m_content.setSize(content_width, scaledContentHeight(content_width));
    m_arrangement_view.setBounds(0, 0, m_content.getWidth(), primaryTrackHeight());
    m_tab_view.setBounds(m_arrangement_view.getBounds());
    m_tone_track_view.setBounds(0, primaryTrackHeight(), m_content.getWidth(), toneTrackHeight());
    m_tone_automation_lanes_view.setBounds(
        0,
        primaryTrackHeight() + toneTrackHeight(),
        m_content.getWidth(),
        m_tone_automation_lanes_view.totalHeight());
    m_cursor_overlay.setBounds(m_content.getLocalBounds());
    m_cursor_overlay.toFront(false);
    updateRulerView();
    refreshTimelineGrid();
}

// Relays out the canvas after a vertical-only change. Mirrors layoutScaledCanvas but ends in the
// span-checked grid refresh, so per-frame lane resize drags never rescan the tempo map.
void TrackViewport::relayoutForContentHeightChange()
{
    const int content_width = m_content.getWidth();
    m_content.setSize(content_width, scaledContentHeight(content_width));
    m_tone_automation_lanes_view.setBounds(
        0,
        primaryTrackHeight() + toneTrackHeight(),
        content_width,
        m_tone_automation_lanes_view.totalHeight());
    m_cursor_overlay.setBounds(m_content.getLocalBounds());
    m_cursor_overlay.toFront(false);
    updateRulerView();
    refreshTimelineGridForViewChange();
}

// Changes the horizontal timeline scale around the current transport cursor.
void TrackViewport::handleMouseWheelZoom(const juce::MouseWheelDetails& wheel)
{
    const float wheel_delta = hasMouseWheelDelta(wheel.deltaY) ? wheel.deltaY : wheel.deltaX;
    if (!m_project_loaded || timelineDurationSeconds() <= 0.0 || !hasMouseWheelDelta(wheel_delta) ||
        wheel.isInertial)
    {
        return;
    }

    const common::core::TimePosition cursor_position = m_transport.position();
    const double wheel_steps = std::max(1.0, static_cast<double>(std::abs(wheel_delta)) * 4.0);
    const double zoom_factor = std::pow(g_mouse_wheel_zoom_factor, wheel_steps);
    const double next_pixels_per_second =
        wheel_delta > 0.0f ? m_pixels_per_second * zoom_factor : m_pixels_per_second / zoom_factor;

    const double previous_pixels_per_second = m_pixels_per_second;
    m_pixels_per_second =
        std::clamp(next_pixels_per_second, minPixelsPerSecond(), g_max_pixels_per_second);
    layoutScaledCanvas();
    centerViewportOnTime(cursor_position.seconds);
    // Exact inequality via is_neq keeps -Wfloat-equal builds clean; clamp-unchanged detection
    // is deliberately exact.
    if (m_on_zoom_changed && std::is_neq(m_pixels_per_second <=> previous_pixels_per_second))
    {
        m_on_zoom_changed(m_pixels_per_second);
    }
}

// Finds the timeline time at the center of the currently visible viewport.
double TrackViewport::viewportCenterTimeSeconds() const noexcept
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
void TrackViewport::centerViewportOnTime(double time_seconds)
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

// Runs a deferred project-load focus pass only after the viewport has paintable dimensions.
void TrackViewport::focusCursorIfPending()
{
    if (!m_cursor_focus_pending || !m_project_loaded || timelineDurationSeconds() <= 0.0 ||
        m_viewport.getViewWidth() <= 0 || m_content.getWidth() <= 0)
    {
        return;
    }

    m_cursor_focus_pending = false;
    centerViewportOnTime(m_transport.position().seconds);
}

// Keeps playback visible using controller-pushed state plus current position reads.
void TrackViewport::updatePlaybackFollow()
{
    if (!m_project_loaded || !m_playback_active || timelineDurationSeconds() <= 0.0)
    {
        m_window_shift.reset();
        return;
    }

    const auto cursor_x =
        cursorXForTimelinePosition(m_transport.position(), m_timeline_range, m_content.getWidth());
    if (!cursor_x.has_value())
    {
        m_window_shift.reset();
        return;
    }

    if (m_follow_style == PlaybackFollowStyle::SmoothScroll)
    {
        followCursorSmoothly(*cursor_x);
        return;
    }

    followCursorWithWindowShifts(*cursor_x);
}

// Stores the follow mode and abandons any in-flight shifted-window glide so the new mode takes
// over from the current view instead of finishing the old animation.
void TrackViewport::setPlaybackFollowStyle(PlaybackFollowStyle style)
{
    m_follow_style = style;
    m_window_shift.reset();
}

// Reports the active follow mode so the menu can tick the spike toggle.
PlaybackFollowStyle TrackViewport::playbackFollowStyle() const noexcept
{
    return m_follow_style;
}

// Fixed-cursor smooth scroll (adopted mode, spike-grade implementation): every vblank repins
// the window so the cursor stays at the pin fraction while the content flows past it,
// rhythm-game style. Known spike limits: integer viewport positions make low zoom tick instead
// of glide, and Tracktion's block-quantized audible time shimmers the scroll velocity — both
// removed by the time-space camera in docs/roadmap/51-smooth-scroll-camera.md.
void TrackViewport::followCursorSmoothly(float cursor_x)
{
    setViewportLeft(
        static_cast<int>(std::round(
            static_cast<double>(cursor_x) -
            static_cast<double>(m_viewport.getViewWidth()) * g_smooth_follow_pin_fraction)));
}

// Returns the viewport left edge that places the cursor at the shifted-window pin fraction.
double TrackViewport::pinnedWindowLeftFor(float cursor_x) const noexcept
{
    return static_cast<double>(cursor_x) -
           static_cast<double>(m_viewport.getViewWidth()) * g_follow_shift_pin_fraction;
}

// Guitar Pro-style playback follow: lets the cursor travel across a stationary window and
// glides the window forward once the cursor sits at or past the trigger fraction, so it
// resumes its travel from the pin fraction. An on-screen cursor below the trigger — mid-
// window travel, entering from the left, or playback just started — leaves the window
// alone; a cursor past the trigger (even beyond the right edge after a seek or a play start
// elsewhere) glides back to the pin, the fixed duration keeping distant glides one quick
// sweep instead of a long chase; a cursor off-screen left snaps straight to the pin so
// playback is never running invisibly behind the view. The glide eases toward a target
// captured when it starts: chasing the live cursor instead would never converge and the
// follow would degenerate into smooth scrolling.
void TrackViewport::followCursorWithWindowShifts(float cursor_x)
{
    const auto view_width = static_cast<double>(m_viewport.getViewWidth());
    if (view_width <= 0.0)
    {
        return;
    }

    const double local_x =
        static_cast<double>(cursor_x) - static_cast<double>(m_viewport.getViewPositionX());
    if (local_x < 0.0)
    {
        m_window_shift.reset();
        setViewportLeft(static_cast<int>(std::round(pinnedWindowLeftFor(cursor_x))));
        return;
    }

    if (!m_window_shift.has_value())
    {
        if (local_x < view_width * g_follow_shift_trigger_fraction)
        {
            return;
        }

        m_window_shift = WindowShift{
            .start_left = static_cast<double>(m_viewport.getViewPositionX()),
            .target_left = pinnedWindowLeftFor(cursor_x),
            .start_seconds = juce::Time::getMillisecondCounterHiRes() * 0.001,
        };
    }

    const double progress =
        (juce::Time::getMillisecondCounterHiRes() * 0.001 - m_window_shift->start_seconds) /
        g_follow_shift_duration_seconds;
    if (progress >= 1.0)
    {
        setViewportLeft(static_cast<int>(std::round(m_window_shift->target_left)));
        m_window_shift.reset();
        return;
    }

    const double eased = 1.0 - std::pow(1.0 - progress, 3.0);
    setViewportLeft(
        static_cast<int>(std::round(
            m_window_shift->start_left +
            (m_window_shift->target_left - m_window_shift->start_left) * eased)));
}

// Moves the horizontal viewport position while preserving the current vertical scroll. Ruler
// and grid updates happen through the viewport's visible-area callback when the position
// actually changes.
void TrackViewport::setViewportLeft(int requested_x)
{
    const int max_x = std::max(0, m_content.getWidth() - m_viewport.getViewWidth());
    const int next_x = std::clamp(requested_x, 0, max_x);
    m_viewport.setViewPosition(next_x, m_viewport.getViewPositionY());
}

// Pushes the current scroll and content geometry into the pinned ruler. Callers must follow
// with a grid refresh so the ruler receives lines matching the new view.
void TrackViewport::updateRulerView()
{
    m_timeline_ruler.setTimelineView(
        m_timeline_range, m_content.getWidth(), m_viewport.getViewPositionX());
    // The tone row and automation lanes scroll with the content, so they need the viewport left
    // edge to pin their labels there (the ruler is a separate pinned overlay and does not).
    m_tone_track_view.setVisibleContentLeft(m_viewport.getViewPositionX());
    m_tone_automation_lanes_view.setVisibleContentLeft(m_viewport.getViewPositionX());
}

// Returns the content-coordinate span the shared grid scan must cover: the viewport's view
// offset across the pinned ruler width, which always covers the viewport's own view width.
std::pair<int, int> TrackViewport::gridVisibleSpan() const noexcept
{
    const int visible_x_begin = std::max(0, m_viewport.getViewPositionX());
    const int visible_x_end =
        std::min(m_content.getWidth(), visible_x_begin + m_timeline_ruler.getWidth());
    return {visible_x_begin, visible_x_end};
}

// Computes the visible-span tempo-grid lines once and pushes the one scan result to both the
// ruler and the content canvas, so zoom, scroll, and grid changes pay a single tempo-map scan.
void TrackViewport::refreshTimelineGrid()
{
    const auto [visible_x_begin, visible_x_end] = gridVisibleSpan();
    m_grid_span_begin = visible_x_begin;
    m_grid_span_end = visible_x_end;

    std::vector<core::TempoGridLine> lines = core::visibleTempoGridLines(
        m_tempo_map,
        m_grid_note_value,
        m_timeline_range,
        m_content.getWidth(),
        visible_x_begin,
        visible_x_end);
    m_content.setGridLines(lines);
    m_timeline_ruler.setGridLines(std::move(lines));
}

// Scroll-driven grid refresh that skips the scan when the horizontal visible span is
// unchanged, so vertical scrolls and no-op view updates stay free.
void TrackViewport::refreshTimelineGridForViewChange()
{
    const auto [visible_x_begin, visible_x_end] = gridVisibleSpan();
    if (visible_x_begin == m_grid_span_begin && visible_x_end == m_grid_span_end)
    {
        return;
    }

    refreshTimelineGrid();
}

// Keeps the ruler cursor aligned with the main cursor overlay.
void TrackViewport::updateRulerCursor()
{
    if (!m_project_loaded)
    {
        return;
    }

    m_timeline_ruler.setCursorPosition(m_transport.position());
}

} // namespace rock_hero::editor::ui
