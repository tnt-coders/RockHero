/*!
\file timeline_ruler.h
\brief Pinned bars-and-beats ruler with tempo and signature header bands.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <rock_hero/editor/core/tempo_grid_geometry.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Fixed ruler component height in pixels.

The vertical band layout this height accommodates — tempo band, signature band, measure-number
row, and tick band — is private to timeline_ruler.cpp; change this constant only together with
those band constants.
*/
inline constexpr int g_timeline_ruler_height{53};

/*!
\brief Draws the pinned bars-and-beats ruler above the scrollable timeline content.

The ruler stays fixed while the timeline content scrolls under it: callers push the current view
geometry and the shared visible-span grid lines, and the ruler renders measure numbers, beat and
subdivision ticks, and the tempo/signature header bands from that one scan result. Clicks convert
into the same snapped cursor-placement intent as timeline-content clicks.
*/
class TimelineRuler final : public juce::Component
{
public:
    /*! \brief Receives the timeline position of a cursor-placement click. */
    using CursorPlacementCallback = std::function<void(common::core::TimePosition position)>;

    /*! \brief Names the component for tests and enables direct cursor-placement clicks. */
    TimelineRuler();

    /*!
    \brief Stores whether the ruler should draw musical position data.
    \param project_loaded True while a project is loaded; false leaves the ruler as plain chrome.
    */
    void setProjectLoaded(bool project_loaded);

    /*!
    \brief Stores the ruler geometry derived from the viewport and zoomed content.

    Does not rebuild cached tick geometry by itself: tick coordinates come from the grid lines,
    so callers must follow every view change with a setGridLines push for the new span.

    \param timeline_range Full timeline range represented by the zoomed content width.
    \param content_width Width of the scrollable timeline canvas in pixels.
    \param view_x Horizontal scroll offset of the viewport into the zoomed timeline canvas.
    */
    void setTimelineView(common::core::TimeRange timeline_range, int content_width, int view_x);

    /*!
    \brief Samples the current transport cursor for the ruler's aligned playhead mark.
    \param cursor_position Current transport position on the timeline.
    */
    void setCursorPosition(common::core::TimePosition cursor_position);

    /*!
    \brief Stores the tempo map that supplies anchors and click snapping, plus the grid step in
    beats shared with the track grid and snapping.

    Does not rebuild cached geometry by itself: callers must follow every grid change with a
    setGridLines push, matching setTimelineView.

    \param tempo_map Song tempo map shared with the track grid and snapping.
    \param grid_note_value Grid step as a fraction of a whole note.
    */
    void setGrid(const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value);

    /*!
    \brief Stores the visible-span grid lines and rebuilds the cached ruler geometry from them.

    The lines are computed once by the owning view for the current visible span and shared with
    the track content, so the ruler never runs its own tempo-map scan.

    \param grid_lines Visible tempo-grid lines in content coordinates.
    */
    void setGridLines(std::vector<core::TempoGridLine> grid_lines);

    /*!
    \brief Stores the callback that receives cursor-placement seek positions.
    \param callback Callback invoked with the placement time of each ruler click.
    */
    void setCursorPlacementCallback(CursorPlacementCallback callback);

    /*!
    \brief Paints the tempo and signature bands, the measure-number row, and the tick band.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*!
    \brief Converts ruler clicks into the same snapped placement intent as timeline-content
    clicks.
    \param event JUCE mouse event relative to this ruler.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*! \brief Refreshes cached grid-line geometry after a resize changes the visible width. */
    void resized() override;

private:
    // A ruler text label already resolved to a non-overlapping draw position.
    struct RulerLabel
    {
        int x{0};
        juce::String text{};
        int width{0};
    };

    // Maps an absolute timeline second to this pinned ruler's local x coordinate.
    [[nodiscard]] std::optional<float> localXForSeconds(double seconds) const noexcept;

    // Rebuilds the cached tick and label geometry from the stored grid lines, timeline geometry,
    // and tempo map. Kept out of paint() so cursor-only repaints, which happen at vblank cadence,
    // do not rebuild geometry or remeasure label text on every frame.
    void refreshRulerGeometry();

    // Rebuilds the tempo and signature bands above the ruler body: a metronome marking (enlarged
    // quarter-note glyph plus text-size digits) for the span each non-terminal anchor starts, a
    // signature label at each signature-change downbeat, and the pinned active tempo and
    // signature at the left edge; same font-sharing contract.
    void refreshHeaderBands(const juce::Font& font);

    // Draws visible grid ticks: body-height measures, short beats, and shorter subdivision ticks.
    void drawBeatTicks(juce::Graphics& g);

    // Draws one cached row of overlap-suppressed labels in the current color at a fixed vertical
    // band. The font must match the one the row's widths were measured with.
    void drawLabelRow(
        juce::Graphics& g, const std::vector<RulerLabel>& labels, const juce::Font& font, int y,
        int height);

    // Draws the same transport cursor through the ruler for vertical alignment.
    void drawCursor(juce::Graphics& g);

    // Full timeline range represented by the zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Tempo map used for ruler measure ticks and anchor positions.
    common::core::TempoMap m_tempo_map{};

    // Grid step as a fraction of a whole note, initialized to the quarter-note default because the
    // Fraction default of 0/1 is a degenerate step.
    common::core::Fraction m_grid_note_value{1, 4};

    // Width of the scrollable timeline canvas that shares geometry with the grid.
    int m_content_width{0};

    // Horizontal scroll offset of the viewport into the zoomed timeline canvas.
    int m_view_x{0};

    // False while the editor is empty, so the ruler stays as plain chrome.
    bool m_project_loaded{false};

    // Last subpixel cursor x coordinate drawn by the ruler.
    std::optional<float> m_cursor_x{};

    // Callback invoked when the user clicks the ruler to place the transport cursor.
    CursorPlacementCallback m_cursor_placement_callback{};

    // Tempo-grid lines for the current visible span, pushed by the owning view so the ruler and
    // the track content share one tempo-map scan. Stored in content coordinates; ticks subtract
    // m_view_x, so the lines must be re-pushed after every view change.
    std::vector<core::TempoGridLine> m_grid_lines{};

    // Precomputed tick rectangles in local ruler coordinates, cached so paint() only issues one
    // fillRectList call instead of rebuilding geometry on every repaint.
    juce::RectangleList<float> m_tick_rects{};

    // Measure-number row of the ruler body: the numbers that survived overlap suppression, with
    // widths already measured. Kept out of paint() because text-width measurement
    // (GlyphArrangement layout) is comparatively expensive and previously ran for every visible
    // measure column on every repaint, including narrow cursor-only repaints driven at vblank
    // cadence or triggered by a single click.
    std::vector<RulerLabel> m_measure_labels{};

    // Signature band, drawn in the signature color on its own line between the tempo band and
    // the ruler body: the pinned active signature at the left edge, then one label per visible
    // signature-change downbeat.
    std::vector<RulerLabel> m_signature_labels{};

    // Enlarged quarter-note glyphs of the tempo markings, one per entry in m_tempo_labels and
    // drawn immediately left of it; split from the digits because one text draw cannot mix fonts.
    std::vector<RulerLabel> m_tempo_prefix_labels{};

    // Tempo-marking digits: the pinned active tempo at the left edge, then the quarter-note
    // tempo of the span each visible anchor starts, with band-wide overlap suppression; cached
    // for the same text-measurement reason as m_measure_labels.
    std::vector<RulerLabel> m_tempo_labels{};
};

} // namespace rock_hero::editor::ui
