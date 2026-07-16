/*!
\file timeline_ruler.h
\brief Pinned bars-and-beats ruler with tempo and signature header bands.
*/

#pragma once

#include <compare>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief Fixed ruler component height in pixels.

The vertical band layout this height accommodates — song-section lane, tempo band, signature
band, measure-number row, and tick band — is private to timeline_ruler.cpp; change this constant
only together with those band constants.
*/
inline constexpr int g_timeline_ruler_height{68};

/*!
\brief One chord or arpeggio hand-shape name shown at the shape's span start.

Tab-projection data, not ruler data: the tablature lane's shape spans own the names, and the
ruler merely renders them in its bottom band — exactly as the tempo and signature bands render
tempo-map data — because that band is the only pinned surface directly above the lane's rails.
*/
struct RulerShapeLabel
{
    /*! \brief Absolute timeline second of the span start the chip anchors to. */
    double seconds{0.0};

    /*! \brief Template display name; never empty (unnamed shapes get no chip). */
    juce::String name;

    /*! \brief True for arpeggio spans (purple chip); false for chord spans (blue chip). */
    bool arpeggio{false};

    /*!
    \brief Compares two shape labels by their stored values.
    \param lhs Left-hand label.
    \param rhs Right-hand label.
    \return True when both labels store equal values.
    */
    friend bool operator==(const RulerShapeLabel& lhs, const RulerShapeLabel& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on
        // the floating member. Exact equality is intended; the ordering query expresses it
        // warning-free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.name == rhs.name &&
               lhs.arpeggio == rhs.arpeggio;
    }
};

/*!
\brief One song-section name shown at the section's start in the ruler's top band.

Song-level view data, not ruler data: the controller's section projection owns the names and
start times, and the ruler renders them in its top band as pinned, bar-line-anchored navigation
labels — exactly as the tempo and signature bands render tempo-map data.
*/
struct RulerSectionLabel
{
    /*! \brief Absolute timeline second of the section start the label anchors to. */
    double seconds{0.0};

    /*! \brief Section display name; never empty (unnamed sections get no label). */
    juce::String name;

    /*!
    \brief Compares two section labels by their stored values.
    \param lhs Left-hand label.
    \param rhs Right-hand label.
    \return True when both labels store equal values.
    */
    friend bool operator==(const RulerSectionLabel& lhs, const RulerSectionLabel& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. The ordering query expresses exact equality warning-free.
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.name == rhs.name;
    }
};

/*!
\brief Draws the pinned bars-and-beats ruler above the scrollable timeline content.

The ruler stays fixed while the timeline content scrolls under it: callers push the current view
geometry and the shared visible-span grid lines, and the ruler renders measure numbers, beat and
subdivision ticks, the tempo/signature header bands, and the tab-fed chord/arpeggio name chips
from that one scan result. Clicks convert into the same snapped cursor-placement intent as
timeline-content clicks.
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
    \brief Stores the tab-derived chord/arpeggio name chips drawn in the bottom tick band.

    The chips sit flush with the ruler's bottom edge, directly above the tablature lane's top
    rail, overlapping only ticks — the measure-number row above stays clear. An unchanged list
    returns early because every controller state push repeats it.

    \param labels Named shape spans in ascending start order; empty clears the chips.
    */
    void setShapeLabels(std::vector<RulerShapeLabel> labels);

    /*!
    \brief Stores the chart-derived song-section names drawn in the ruler's top band.

    The names sit in a pinned lane above the tempo band; the active section pins to the left edge
    as the song scrolls. An unchanged list returns early because every controller state push
    repeats it.

    \param labels Section names in ascending start order; empty clears the lane.
    */
    void setSectionLabels(std::vector<RulerSectionLabel> labels);

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
    // signature at the left edge; same font-sharing contract. The caller passes the view-left
    // time when pinning is active (empty otherwise) so all pinned rows share one gate.
    void refreshHeaderBands(const juce::Font& font, std::optional<double> pinned_left_seconds);

    // Rebuilds the top section band: one label per visible section start plus the pinned active
    // section at the left edge, sharing the header bands' pin gate.
    void refreshSectionBand(const juce::Font& font, std::optional<double> pinned_left_seconds);

    // Draws visible grid ticks: body-height measures, short beats, and shorter subdivision ticks.
    void drawBeatTicks(juce::Graphics& g);

    // Draws one cached row of overlap-suppressed labels in the current color at a fixed vertical
    // band. The font must match the one the row's widths were measured with.
    void drawLabelRow(
        juce::Graphics& g, const std::vector<RulerLabel>& labels, const juce::Font& font, int y,
        int height);

    // Draws the same transport cursor through the ruler for vertical alignment.
    void drawCursor(juce::Graphics& g);

    // Draws the tab-derived chord/arpeggio name chips along the ruler's bottom edge.
    void drawShapeChips(juce::Graphics& g);

    // Draws the chart-derived song-section names in the ruler's top band.
    void drawSectionLane(juce::Graphics& g);

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

    // Measure-number row of the ruler body: the pinned active measure at the left edge, then
    // the numbers that survived overlap suppression, with widths already measured. Kept out of
    // paint() because text-width measurement
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

    // Tab-derived chord/arpeggio name chips for the bottom tick band, pushed by the owning view
    // whenever the displayed chart changes; chip x positions map per paint via localXForSeconds.
    std::vector<RulerShapeLabel> m_shape_labels{};

    // Chart-derived song-section names for the top band, pushed by the owning view whenever the
    // displayed chart changes; positions map via localXForSeconds like the shape chips.
    std::vector<RulerSectionLabel> m_section_source{};

    // Section band: the pinned active section at the left edge, then one label per visible section
    // start with band-wide overlap suppression; cached for the same text-measurement reason as
    // m_measure_labels.
    std::vector<RulerLabel> m_section_labels{};
};

} // namespace rock_hero::editor::ui
