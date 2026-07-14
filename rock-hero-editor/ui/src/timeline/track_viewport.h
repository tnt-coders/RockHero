/*!
\file track_viewport.h
\brief Viewport shell that hosts the zoomable track canvas for the editor timeline.
*/

#pragma once

#include "timeline/timeline_ruler.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/controller/i_editor_controller.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

class ArrangementView;
class CursorOverlay;
class TabView;
class ToneAutomationLanesView;
class ToneTrackView;

/*!
\brief Hosts zoomable track content inside a JUCE viewport for future multi-track scrolling.

The shell owns the pinned timeline ruler, the scrolling viewport, and the zoomed content canvas
that hosts the arrangement waveform row and the editor-wide cursor overlay. It computes the
shared visible-span tempo-grid scan once per geometry change and feeds the one result to both the
ruler and the canvas, and it keeps playback visible with Guitar Pro-style shifted-window follow.
*/
class TrackViewport final : public juce::Component
{
private:
    // Paints the timeline content area and delegates wheel zoom back to the viewport shell.
    class Content final : public juce::Component
    {
    public:
        // Stores the owning viewport shell so wheel input can update shared zoom state.
        explicit Content(TrackViewport& owner);

        // Stores whether child track content should replace the empty-project message.
        void setProjectLoaded(bool project_loaded);

        // Stores the shared visible-span grid lines as per-rank column caches and repaints. The
        // owner computes the lines once per geometry change and hands the same scan result to
        // this canvas and the ruler, so repaints that do not change geometry never rescan the
        // tempo map.
        void setGridLines(const std::vector<core::TempoGridLine>& grid_lines);

        // Draws the timeline canvas, tempo grid, waveform row background, and empty-project text.
        void paint(juce::Graphics& g) override;

        // Converts normal wheel movement over timeline content into horizontal zoom.
        void mouseWheelMove(
            const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    private:
        // Owner receives input so zoom state stays centralized in TrackViewport.
        TrackViewport& m_owner;

        // False while no project is loaded so the viewport itself owns empty-state drawing.
        bool m_project_loaded{false};

        // Cached grid column positions per rank, rebuilt only when the owner pushes a fresh
        // visible-span scan.
        std::vector<int> m_subdivision_grid_x{};
        std::vector<int> m_beat_grid_x{};
        std::vector<int> m_measure_grid_x{};
    };

    // Viewport subclass that lets the pinned ruler track user-driven scrollbar movement.
    class TimelineViewport final : public juce::Viewport
    {
    public:
        // Notifies the owner whenever JUCE changes the visible content rectangle.
        std::function<void()> on_visible_area_changed;

    private:
        // Repaints ruler state after scrollbars, wheel scrolling, or programmatic movement.
        void visibleAreaChanged(const juce::Rectangle<int>& new_visible_area) override;
    };

public:
    /*!
    \brief Installs the existing waveform track and cursor overlay into viewport-owned content.

    \param controller Controller that receives ruler-level and canvas-level timeline seek intents.
    \param arrangement_view Waveform view hosted as the first track row; must outlive this shell.
    \param tab_view Tablature lane drawn over the waveform row; must outlive this shell.
    \param tone_track_view Tone track row hosted below the waveform; must outlive this shell.
    \param tone_automation_lanes_view Automation lanes row hosted below the tone track; must
    outlive this shell.
    \param cursor_overlay Editor-wide cursor overlay hosted above the canvas; must outlive this
    shell.
    \param transport Read-only transport sampled to keep playback visible during follow.
    */
    TrackViewport(
        core::IEditorController& controller, ArrangementView& arrangement_view, TabView& tab_view,
        ToneTrackView& tone_track_view, ToneAutomationLanesView& tone_automation_lanes_view,
        CursorOverlay& cursor_overlay, const common::audio::ITransport& transport);

    /*! \brief Uses default destruction because the viewed component is owned by this shell. */
    ~TrackViewport() override = default;

    /*! \brief Copying is disabled because JUCE component trees and references are not copyable. */
    TrackViewport(const TrackViewport&) = delete;

    /*! \brief Copy assignment is disabled because component trees are not copyable. */
    TrackViewport& operator=(const TrackViewport&) = delete;

    /*! \brief Moving is disabled because hosted component references must remain stable. */
    TrackViewport(TrackViewport&&) = delete;

    /*! \brief Move assignment is disabled because hosted component references must remain
    stable. */
    TrackViewport& operator=(TrackViewport&&) = delete;

    /*!
    \brief Stores project-loaded state so the canvas can paint its empty-project message.

    Every full controller state push repeats the flag, so an unchanged flag returns before the
    relayout and grid rescan in layoutScaledCanvas.

    \param project_loaded True when a project arrangement is loaded for display.
    */
    void setProjectLoaded(bool project_loaded);

    /*!
    \brief Stores the full timeline used to size zoomable content and resets zoom on changes.

    An unchanged range returns early: every controller state push repeats the range, and the
    relayout would otherwise pay a redundant grid rescan per push.

    \param timeline_range Full timeline range represented by the zoomed content width.
    */
    void setTimelineRange(common::core::TimeRange timeline_range);

    /*!
    \brief Relays out the canvas after a vertical-only content change (lane add/remove/resize).

    Identical to the zoom relayout except it ends in the span-checked grid refresh: a height-only
    change leaves the horizontal span untouched, so per-frame lane resize drags never rescan the
    tempo map.
    */
    void relayoutForContentHeightChange();

    /*!
    \brief Stores the tempo map and grid note value used by the content grid and the ruler.

    The map arrives by const& rather than the usual sink-by-value because every state push
    repeats it and the common unchanged case would otherwise copy the anchor vectors and derived
    index tables just to discard them.

    \param tempo_map Song-level tempo map used to render beat and measure grid lines.
    \param grid_note_value Grid step as a fraction of a whole note.
    */
    void setGrid(const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value);

    /*!
    \brief Stores coarse transport state pushed by the controller and handles Stop-button reset.

    \param playback_active True while the transport is playing.
    \param stop_enabled True while the Stop action can reset playback or the playhead.
    */
    void setTransportDisplayState(bool playback_active, bool stop_enabled);

    /*!
    \brief Stores the tablature lane count so the waveform row can grow past six strings.

    The tablature lane spacing is pinned to the six-string reference density, so displays with
    more strings need a proportionally taller waveform row instead of compressed lanes; the row
    grows into the canvas and the viewport's vertical scrollbar absorbs any overflow.

    \param displayed_strings Displayed tablature lane count; zero without a chart.
    */
    void setTabDisplayedStrings(int displayed_strings);

    /*!
    \brief Forwards the tab-derived chord/arpeggio name chips to the pinned timeline ruler.

    The chips are tablature data rendered on the ruler's pinned surface (its bottom tick band
    sits directly above the tab lane's top rail), exactly as the tempo and signature bands
    render tempo-map data.

    \param labels Named shape spans in ascending start order; empty clears the chips.
    */
    void setShapeLabels(std::vector<RulerShapeLabel> labels);

    /*! \brief Requests one viewport recenter once a restored project cursor is available. */
    void requestCursorFocus();

    /*!
    \brief Installs the callback that reports user-driven zoom changes for persistence.
    \param on_zoom_changed Callback receiving the new pixels-per-second scale.
    */
    void setZoomChangedCallback(std::function<void(double)> on_zoom_changed);

    /*!
    \brief Applies a per-project zoom restored from app-local settings.

    Called only on a fresh project load, before the cursor recenter, so restored zoom never
    fights the user's live wheel zooming. The value is clamped to the current timeline bounds
    and does not re-report through the zoom-changed callback.

    \param pixels_per_second Restored horizontal timeline scale.
    */
    void setRestoredZoomPixelsPerSecond(double pixels_per_second);

    /*!
    \brief Paints the area around zoomed content when the viewport is larger than the canvas.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Keeps the viewport responsive while preserving zoom-derived content bounds. */
    void resized() override;

private:
    // Returns the duration of the currently displayed full timeline.
    [[nodiscard]] double timelineDurationSeconds() const noexcept;

    // Returns the default content height after reserving space for the horizontal scrollbar.
    [[nodiscard]] int defaultVisibleCanvasHeight() const noexcept;

    // Keeps each track at one third of the default usable viewport height, growing the waveform
    // row proportionally when the tablature shows more than the six-string reference.
    [[nodiscard]] int primaryTrackHeight() const noexcept;

    // Compact height of the tone track row hosted below the waveform.
    [[nodiscard]] int toneTrackHeight() const noexcept;

    // Converts the current pixel density into the width of the full timeline content.
    [[nodiscard]] int scaledContentWidth() const noexcept;

    // Calculates the lowest pixel density needed to fit the whole timeline in view.
    [[nodiscard]] double minPixelsPerSecond() const noexcept;

    // Keeps the stored zoom inside the current timeline and viewport constraints.
    void clampZoomToTimeline();

    // Predicts horizontal scrollbar presence so content height can leave room for it.
    [[nodiscard]] bool needsHorizontalScrollbar(int content_width) const noexcept;

    // Extends content only when the viewport grows; the default canvas already fits three tracks.
    [[nodiscard]] int scaledContentHeight(int content_width) const noexcept;

    // Keeps vertical content responsive while horizontal content follows zoom state.
    void layoutScaledCanvas();

    // Changes the horizontal timeline scale around the current transport cursor.
    void handleMouseWheelZoom(const juce::MouseWheelDetails& wheel);

    // Finds the timeline time at the center of the currently visible viewport.
    [[nodiscard]] double viewportCenterTimeSeconds() const noexcept;

    // Repositions the viewport so the supplied timeline time remains near the center.
    void centerViewportOnTime(double time_seconds);

    // Runs a deferred project-load focus pass only after the viewport has paintable dimensions.
    void focusCursorIfPending();

    // Keeps playback visible using controller-pushed state plus current position reads.
    void updatePlaybackFollow();

    // Returns the viewport left edge that places the cursor at the shifted-window pin fraction.
    [[nodiscard]] double pinnedWindowLeftFor(float cursor_x) const noexcept;

    // Shifted-window playback follow; see the implementation comment for the full behavior.
    void followCursorWithWindowShifts(float cursor_x);

    // Moves the horizontal viewport position while preserving the current vertical scroll. Ruler
    // and grid updates happen through the viewport's visible-area callback when the position
    // actually changes.
    void setViewportLeft(int requested_x);

    // Pushes the current scroll and content geometry into the pinned ruler. Callers must follow
    // with a grid refresh so the ruler receives lines matching the new view.
    void updateRulerView();

    // Returns the content-coordinate span the shared grid scan must cover: the viewport's view
    // offset across the pinned ruler width, which always covers the viewport's own view width.
    [[nodiscard]] std::pair<int, int> gridVisibleSpan() const noexcept;

    // Computes the visible-span tempo-grid lines once and pushes the one scan result to both the
    // ruler and the content canvas, so zoom, scroll, and grid changes pay a single tempo-map scan.
    void refreshTimelineGrid();

    // Scroll-driven grid refresh that skips the scan when the horizontal visible span is
    // unchanged, so vertical scrolls and no-op view updates stay free.
    void refreshTimelineGridForViewChange();

    // Keeps the ruler cursor aligned with the main cursor overlay.
    void updateRulerCursor();

    // Zoomed-canvas width and zoom bounds; class-scope (unlike the other tuning constants in the
    // implementation file) so the zoom default can be a default member initializer.
    static constexpr int g_track_canvas_width{1264};
    static constexpr double g_default_pixels_per_second{
        static_cast<double>(g_track_canvas_width) / 10.0
    };
    static constexpr double g_max_pixels_per_second{static_cast<double>(g_track_canvas_width)};

    // Controller receives ruler-level timeline seek intent.
    core::IEditorController& m_controller;

    // Existing waveform view hosted as the first track row.
    ArrangementView& m_arrangement_view;

    // Tablature lane drawn over the waveform row, under the cursor overlay.
    TabView& m_tab_view;

    // Tone track row hosted below the waveform on the shared canvas.
    ToneTrackView& m_tone_track_view;

    // Automation lanes row hosted below the tone track; its total height feeds content layout.
    ToneAutomationLanesView& m_tone_automation_lanes_view;

    // Full-canvas cursor and click overlay.
    CursorOverlay& m_cursor_overlay;

    // Read-only transport sampled to keep the viewport near the current cursor during playback.
    const common::audio::ITransport& m_transport;

    // Zoomed canvas that holds the current waveform track and future track rows.
    Content m_content;

    // Pinned ruler that shows measure orientation plus the tempo and signature header bands.
    TimelineRuler m_timeline_ruler;

    // JUCE scrolling container around the zoomed timeline canvas.
    TimelineViewport m_viewport;

    // Vblank callback used for viewport-follow behavior without continuous controller pushes.
    juce::VBlankAttachment m_vblank_attachment;

    // Reports user-driven zoom changes so the controller can persist them per project.
    std::function<void(double)> m_on_zoom_changed;

    // Full timeline range represented by the current zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Song-level tempo map used to render beat and measure grid lines.
    common::core::TempoMap m_tempo_map{};

    // Grid step as a fraction of a whole note, initialized to the quarter-note default because the
    // Fraction default of 0/1 is a degenerate step.
    common::core::Fraction m_grid_note_value{1, 4};

    // Horizontal content span covered by the last shared grid scan, used to skip scroll-driven
    // rescans when the visible span did not move.
    int m_grid_span_begin{0};
    int m_grid_span_end{0};

    // Horizontal timeline scale used to size the zoomed content canvas.
    double m_pixels_per_second{g_default_pixels_per_second};

    // Tracks empty vs loaded display mode for layout and zoom gating.
    bool m_project_loaded{false};

    // Displayed tablature lane count; rows past the six-string reference density grow the
    // waveform row proportionally so lane spacing never compresses.
    int m_tab_displayed_strings{0};

    // Coarse playing flag from core::EditorViewState, used to avoid vblank state polling.
    bool m_playback_active{false};

    // In-flight shifted-window glide, present only while the window is animating forward.
    struct WindowShift
    {
        // Viewport left edge when the glide started.
        double start_left{};

        // Viewport left edge the glide eases toward, captured at the trigger crossing so the
        // glide converges while the cursor keeps moving.
        double target_left{};

        // Wall-clock glide start in seconds, driving the eased progress.
        double start_seconds{};
    };
    std::optional<WindowShift> m_window_shift{};

    // Previous stop-button enabled state, used to identify a Stop action reset.
    bool m_stop_enabled{false};

    // Set while a project-load state is waiting for a sized viewport before centering the cursor.
    bool m_cursor_focus_pending{false};
};

} // namespace rock_hero::editor::ui
