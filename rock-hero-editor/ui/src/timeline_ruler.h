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

inline constexpr int g_timeline_ruler_height{32};

// Draws the pinned bars-and-beats ruler above the scrollable timeline content.
class TimelineRuler final : public juce::Component
{
public:
    using CursorPlacementCallback = std::function<void(common::core::TimePosition position)>;

    // Names the component for tests and enables direct cursor-placement clicks.
    TimelineRuler();

    // Stores whether the ruler should draw musical position data.
    void setProjectLoaded(bool project_loaded);

    // Stores the ruler geometry derived from the viewport and zoomed content. Does not rebuild
    // cached tick geometry by itself: tick coordinates come from the grid lines, so callers must
    // follow every view change with a setGridLines push for the new span.
    void setTimelineView(common::core::TimeRange timeline_range, int content_width, int view_x);

    // Samples the current transport cursor for the ruler's aligned playhead mark.
    void setCursorPosition(common::core::TimePosition cursor_position);

    // Stores the tempo map that supplies anchors and click snapping, plus the grid step in beats
    // shared with the track grid and snapping.
    void setGrid(common::core::TempoMap tempo_map, common::core::Fraction grid_spacing_beats);

    // Stores the tempo-grid lines computed once by the owning view for the current visible span
    // and rebuilds the cached ruler geometry from them. The lines share the scan with the track
    // content, so the ruler never runs its own tempo-map scan.
    void setGridLines(std::vector<core::TempoGridLine> grid_lines);

    // Stores the callback that receives cursor-placement seek positions.
    void setCursorPlacementCallback(CursorPlacementCallback callback);

    // Paints quiet measure orientation marks and brighter tempo-map anchors.
    void paint(juce::Graphics& g) override;

    // Converts ruler clicks into the same snapped placement intent as timeline-content clicks.
    void mouseDown(const juce::MouseEvent& event) override;

    // Refreshes cached grid-line geometry after a resize changes the visible ruler width.
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

    // Rebuilds the cached tick, label, and anchor geometry from the stored grid lines, timeline
    // geometry, and tempo map. Kept out of paint() so cursor-only repaints, which happen at
    // vblank cadence, do not rebuild geometry or remeasure label text on every frame.
    void refreshRulerGeometry();

    // Rebuilds the top band's signature geometry: the pinned active signature-and-tempo block at
    // the left edge plus a measure-number and signature pair at each visible signature change.
    // Returns the covered rectangles so plain measure numbers can flow around them.
    [[nodiscard]] std::vector<juce::Rectangle<int>> refreshSignatureLabels(const juce::Font& font);

    // Rebuilds the merged anchor-marker path and overlap-suppressed anchor labels; part of
    // refreshRulerGeometry and shares its font so cached label widths match the paint font.
    void refreshAnchorGeometry(const juce::Font& font);

    // Draws visible grid ticks: full-height measures, quarter-height beats, and shorter
    // subdivision ticks.
    void drawBeatTicks(juce::Graphics& g);

    // Draws the cached anchor diamonds and second-precise anchor labels.
    void drawAnchors(juce::Graphics& g);

    // Draws one cached row of overlap-suppressed labels in the current colour at a fixed vertical
    // band.
    void drawLabelRow(juce::Graphics& g, const std::vector<RulerLabel>& labels, int y, int height);

    // Draws the same transport cursor through the ruler for vertical alignment.
    void drawCursor(juce::Graphics& g);

    // Full timeline range represented by the zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Tempo map used for ruler measure ticks and anchor positions.
    common::core::TempoMap m_tempo_map{};

    // Grid step measured in tempo-map beats, initialized to the whole-beat grid because the
    // Fraction default of 0/1 is a degenerate step.
    common::core::Fraction m_grid_spacing_beats{1, 1};

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

    // Measure labels that survived overlap suppression, with widths already measured. Kept out of
    // paint() because text-width measurement (GlyphArrangement layout) is comparatively expensive
    // and previously ran for every visible measure column on every repaint, including narrow
    // cursor-only repaints driven at vblank cadence or triggered by a single click.
    std::vector<RulerLabel> m_measure_labels{};

    // Signature-colored top-band labels: the pinned block showing the signature and quarter-note
    // tempo active at the visible left edge, plus the signature part of each visible
    // signature-change pair. Plain measure numbers flow around these.
    std::vector<RulerLabel> m_signature_labels{};

    // All visible anchor diamonds merged into one path so paint() issues a single fill instead of
    // building and filling a path per anchor on every repaint.
    juce::Path m_anchor_markers{};

    // Anchor tempo labels (quarter-note BPM of the span each anchor starts) that survived overlap
    // suppression, cached for the same text-measurement reason as m_measure_labels.
    std::vector<RulerLabel> m_anchor_labels{};
};

} // namespace rock_hero::editor::ui
