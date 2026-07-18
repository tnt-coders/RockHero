#include "track_viewport.h"

#include "shared/editor_theme.h"
#include "tab/tab_view.h"
#include "timeline/arrangement_view.h"
#include "timeline/cursor_overlay.h"
#include "timeline/tempo_grid_dots.h"
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

// Playback-follow tuning: the cursor travels across a stationary window and may reach the
// trigger fraction of the view width before the window glides forward far enough to drop it
// back at the pin fraction; the glide runs for the shift duration with a cubic ease-out. The
// cursor keeps playing during the glide, so it lands slightly right of the pin fraction —
// intended, since the shift target is captured when the glide starts.
constexpr double g_follow_shift_trigger_fraction{0.8};
constexpr double g_follow_shift_pin_fraction{0.05};
constexpr double g_follow_shift_duration_seconds{0.3};

// The caret glide reveals this fraction of the view past the aligned measure edge, so a note
// seated exactly ON the revealed boundary — legal here, unlike Guitar Pro — shows its whole
// head (heads are fixed-pixel, ~25px; 4% clears a full head width at any view past ~650px)
// plus a sliver of the neighboring measure's interior. A view fraction rather than pixels or
// musical time keeps the perceived peek constant across window sizes and zoom, in the same
// unit vocabulary as the trigger/pin fractions above.
constexpr double g_measure_glide_reveal_fraction{0.04};

// Treats tiny wheel deltas as absent so zoom input stays stable across platforms.
[[nodiscard]] bool hasMouseWheelDelta(float delta) noexcept
{
    return std::abs(delta) > g_min_mouse_wheel_delta;
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

// Stores the paused cursor column and repaints only the strips it leaves and enters — the
// same narrow-invalidation pattern the overlay and ruler cursors use.
void TrackViewport::Content::setPausedCursorX(std::optional<float> x)
{
    if (x == m_paused_cursor_x)
    {
        return;
    }

    const auto repaint_strip = [this](std::optional<float> column) {
        if (!column.has_value())
        {
            return;
        }
        constexpr int pad = 3;
        repaint(static_cast<int>(std::floor(*column)) - pad, 0, 2 * pad + 3, getHeight());
    };
    repaint_strip(m_paused_cursor_x);
    repaint_strip(x);
    m_paused_cursor_x = x;
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
        // The paused play-from-here column (the marker model, behind-content ruling
        // 2026-07-18): drawn over the grid but BEHIND every track-row component, so while
        // editing it shows in every gap without ever covering a note or its fret number.
        // During playback the overlay's moving line takes over in front and this hides. The
        // 1px width and the rounding match drawTimelineCursor exactly, so the column and the
        // ruler's mark above it land in the same pixel and read as one line.
        if (m_paused_cursor_x.has_value())
        {
            const int column = std::clamp(
                static_cast<int>(std::round(*m_paused_cursor_x)), 0, bounds.getWidth() - 1);
            g.setColour(editorTheme().paused_cursor);
            g.fillRect(column, 0, 1, bounds.getHeight());
        }
        return;
    }

    g.setColour(editorTheme().muted_text);
    g.drawText("No Project Loaded", bounds, juce::Justification::centred);
}

// Converts normal wheel movement over timeline content into horizontal zoom. Alt marks a
// selection verb, never zoom: Alt-modified wheels route to the shell's selection dispatch and
// are swallowed either way so the hosting viewport cannot scroll on them.
void TrackViewport::Content::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isAltDown())
    {
        if (m_owner.m_on_selection_wheel != nullptr)
        {
            static_cast<void>(m_owner.m_on_selection_wheel(event, wheel));
        }
        return;
    }
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
        advanceWindowGlide();
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

// Stores the armed caret's position and refreshes the ruler's aligned mark (the marker
// model): the mark hides while armed — the caret is the position display — and returns while
// passive. The stored seconds also become the wheel-zoom center while armed. The overlay's
// content-spanning line is untouched here: with a chart it renders only during playback, so
// the lane stays clear of position furniture while editing.
void TrackViewport::setArmedChartCaret(std::optional<double> seconds)
{
    if (m_armed_caret_seconds == seconds)
    {
        return;
    }

    m_armed_caret_seconds = seconds;
    updateRulerCursor();
}

// Forwards the tab-derived chord/arpeggio name chips to the pinned ruler, which renders them
// in its bottom tick band directly above the tablature lane.
void TrackViewport::setShapeLabels(std::vector<RulerShapeLabel> labels)
{
    m_timeline_ruler.setShapeLabels(std::move(labels));
}

// Forwards the song's section names to the pinned ruler's section chip row.
void TrackViewport::setSectionLabels(std::vector<RulerSectionLabel> labels)
{
    m_timeline_ruler.setSectionLabels(std::move(labels));
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

void TrackViewport::setSelectionWheelCallback(
    std::function<bool(const juce::MouseEvent&, const juce::MouseWheelDetails&)> on_selection_wheel)
{
    m_on_selection_wheel = std::move(on_selection_wheel);
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
    // The marker model's click and visibility rules: seek clicks stay inside the highway band
    // (tone/lane clicks never move the position), and with a chart displayed the
    // content-spanning cursor line renders only during playback — while paused the position
    // shows as the caret (armed) or the ruler's mark (passive), keeping the lane clear of
    // furniture over note numbers. Chartless arrangements keep their paused line as the only
    // indicator.
    m_cursor_overlay.setSeekBandHeight(primaryTrackHeight());
    m_cursor_overlay.setPausedCursorHidden(m_tab_displayed_strings > 0);
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
    m_cursor_overlay.setSeekBandHeight(primaryTrackHeight());
    m_cursor_overlay.setPausedCursorHidden(m_tab_displayed_strings > 0);
    updateRulerView();
    refreshTimelineGridForViewChange();
}

// Changes the horizontal timeline scale around the current position: the armed caret when one
// exists (the marker model — the caret is the position, so zoom keeps it centered), else the
// transport cursor (the playing playhead or the passive paused cursor).
void TrackViewport::handleMouseWheelZoom(const juce::MouseWheelDetails& wheel)
{
    const float wheel_delta = hasMouseWheelDelta(wheel.deltaY) ? wheel.deltaY : wheel.deltaX;
    if (!m_project_loaded || timelineDurationSeconds() <= 0.0 || !hasMouseWheelDelta(wheel_delta) ||
        wheel.isInertial)
    {
        return;
    }

    const common::core::TimePosition cursor_position{m_armed_caret_seconds.value_or(
        m_transport.position().seconds)};
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

// Keeps playback visible using controller-pushed state plus current position reads. While
// paused this deliberately leaves any in-flight glide alone — the caret's keep-measure-visible
// rule drives paused glides through the same shared machinery.
void TrackViewport::updatePlaybackFollow()
{
    if (!m_project_loaded || !m_playback_active || timelineDurationSeconds() <= 0.0)
    {
        return;
    }

    const auto cursor_x =
        cursorXForTimelinePosition(m_transport.position(), m_timeline_range, m_content.getWidth());
    if (!cursor_x.has_value())
    {
        m_window_shift.reset();
        return;
    }

    followCursorWithWindowShifts(*cursor_x);
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

    if (!m_window_shift.has_value() && local_x >= view_width * g_follow_shift_trigger_fraction)
    {
        beginWindowGlide(pinnedWindowLeftFor(cursor_x));
    }
}

// Starts (or retargets) the eased window glide toward a viewport left edge. The target is
// captured here rather than chased so the glide always converges; a newer request simply
// supersedes an in-flight one from the current position.
void TrackViewport::beginWindowGlide(double target_left)
{
    m_window_shift = WindowShift{
        .start_left = static_cast<double>(m_viewport.getViewPositionX()),
        .target_left = target_left,
        .start_seconds = juce::Time::getMillisecondCounterHiRes() * 0.001,
    };
}

// Advances the in-flight window glide with the shared cubic ease-out; shared by playback
// follow and the caret's keep-measure-visible rule, so both motions feel identical.
void TrackViewport::advanceWindowGlide()
{
    if (!m_window_shift.has_value())
    {
        return;
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

// Glides the window until the caret's measure sits fully in view (the marker model's
// keep-in-view rule): the minimal shift that fits the whole measure — aligning a measure
// starting before the view at the left, one ending past it at the right, each overshooting
// by the reveal fraction so boundary notes of the neighboring measure show whole — through
// the same eased glide playback follow uses. A measure wider than the view falls back to the
// minimal shift that brings the caret itself into view with a tenth-of-view pad; a fully
// visible measure moves nothing.
void TrackViewport::ensureMeasureVisible(
    double measure_start_seconds, double measure_end_seconds, double caret_seconds)
{
    if (!m_project_loaded || timelineDurationSeconds() <= 0.0 || m_viewport.getViewWidth() <= 0 ||
        m_content.getWidth() <= 0)
    {
        return;
    }

    const auto x_of = [this](double seconds) {
        const double clamped =
            std::clamp(seconds, m_timeline_range.start.seconds, m_timeline_range.end.seconds);
        return cursorXForTimelinePosition(
            common::core::TimePosition{clamped}, m_timeline_range, m_content.getWidth());
    };
    const auto start_x = x_of(measure_start_seconds);
    const auto end_x = x_of(measure_end_seconds);
    const auto caret_x = x_of(caret_seconds);
    if (!start_x.has_value() || !end_x.has_value() || !caret_x.has_value())
    {
        return;
    }

    const auto view_left = static_cast<double>(m_viewport.getViewPositionX());
    const auto view_width = static_cast<double>(m_viewport.getViewWidth());
    const double view_right = view_left + view_width;
    double target_left = view_left;
    if (static_cast<double>(*end_x - *start_x) <= view_width)
    {
        // The aligned edge overshoots by the reveal fraction so the neighboring measure's
        // boundary notes show whole; when measure plus reveal cannot both fit, the full
        // measure wins and the reveal compresses.
        const double reveal = view_width * g_measure_glide_reveal_fraction;
        if (static_cast<double>(*start_x) < view_left)
        {
            target_left = std::max(
                static_cast<double>(*start_x) - reveal, static_cast<double>(*end_x) - view_width);
        }
        else if (static_cast<double>(*end_x) > view_right)
        {
            target_left = std::min(
                static_cast<double>(*end_x) + reveal - view_width, static_cast<double>(*start_x));
        }
        else
        {
            return;
        }
    }
    else
    {
        const double pad = view_width * 0.1;
        if (static_cast<double>(*caret_x) < view_left)
        {
            target_left = static_cast<double>(*caret_x) - pad;
        }
        else if (static_cast<double>(*caret_x) > view_right)
        {
            target_left = static_cast<double>(*caret_x) - view_width + pad;
        }
        else
        {
            return;
        }
    }

    beginWindowGlide(target_left);
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

// Keeps the ruler cursor aligned with where playback would start.
void TrackViewport::updateRulerCursor()
{
    if (!m_project_loaded)
    {
        return;
    }

    // The ruler mark is the play-from-here indicator and is ALWAYS shown (user ruling
    // 2026-07-18): the moving playhead while playing, else the marker — the armed caret's
    // slot (Space seeks there before playing) or the passive transport rest. While armed the
    // mark must sit at the caret, not the stale transport position, or it would lie about
    // where Space starts.
    const bool playing = m_transport.state().playing;
    const double mark_seconds =
        playing ? m_transport.position().seconds
                : m_armed_caret_seconds.value_or(m_transport.position().seconds);
    m_timeline_ruler.setCursorPosition(common::core::TimePosition{mark_seconds}, !playing);

    // The same marker position feeds the behind-content paused column: hidden during playback
    // (the overlay's moving line takes over in front) and without a chart (the overlay keeps
    // the chartless paused line as the only indicator). While a caret is armed the column
    // rides the caret's slot; the caret square masks its own interior so the cursor never
    // shows through the caret itself.
    const bool paused_column_visible = !playing && m_tab_displayed_strings > 0;
    m_content.setPausedCursorX(
        paused_column_visible
            ? cursorXForTimelinePosition(
                  common::core::TimePosition{mark_seconds}, m_timeline_range, m_content.getWidth())
            : std::optional<float>{});
}

} // namespace rock_hero::editor::ui
