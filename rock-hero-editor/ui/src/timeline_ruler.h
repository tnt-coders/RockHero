#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/core/tempo_map.h>
#include <rock_hero/common/core/timeline.h>
#include <vector>

namespace rock_hero::editor::ui
{

inline constexpr int g_timeline_ruler_height{32};

// Draws the pinned bars-and-beats ruler above the scrollable timeline content.
class TimelineRuler final : public juce::Component
{
public:
    using CursorPlacementCallback = std::function<void(double normalized_x)>;

    // Names the component for tests and enables direct cursor-placement clicks.
    TimelineRuler();

    // Stores whether the ruler should draw musical position data.
    void setProjectLoaded(bool project_loaded);

    // Stores the ruler geometry derived from the viewport and zoomed content.
    void setTimelineView(common::core::TimeRange timeline_range, int content_width, int view_x);

    // Samples the current transport cursor for the ruler's aligned playhead mark.
    void setCursorPosition(common::core::TimePosition cursor_position);

    // Stores the tempo map that supplies measures and anchors.
    void setTempoMap(common::core::TempoMap tempo_map);

    // Stores the callback that receives normalized cursor-placement intent.
    void setCursorPlacementCallback(CursorPlacementCallback callback);

    // Paints quiet measure orientation marks and brighter tempo-map anchors.
    void paint(juce::Graphics& g) override;

    // Converts ruler clicks into the same normalized placement intent as timeline-content clicks.
    void mouseDown(const juce::MouseEvent& event) override;

    // Refreshes cached grid-line geometry after a resize changes the visible ruler width.
    void resized() override;

private:
    // Maps an absolute timeline second to this pinned ruler's local x coordinate.
    [[nodiscard]] std::optional<float> localXForSeconds(double seconds) const noexcept;

    // Recomputes the cached grid lines from the current timeline geometry and tempo map. Kept out
    // of paint() so cursor-only repaints, which happen at vblank cadence, do not rescan the whole
    // visible beat range on every frame.
    void refreshGridLines();

    // Draws visible beat ticks, with measure ticks promoted to the full ruler height.
    void drawBeatTicks(juce::Graphics& g);

    // Draws timing anchors as diamonds and labels precise seconds when horizontal room allows.
    void drawAnchors(juce::Graphics& g);

    // Draws the same transport cursor through the ruler for vertical alignment.
    void drawCursor(juce::Graphics& g);

    // Full timeline range represented by the zoomed content width.
    common::core::TimeRange m_timeline_range{};

    // Tempo map used for ruler measure ticks and anchor positions.
    common::core::TempoMap m_tempo_map{};

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

    // A measure label already resolved to a non-overlapping draw position.
    struct MeasureLabel
    {
        int x{0};
        juce::String text{};
        int width{0};
    };

    // Precomputed tick rectangles in local ruler coordinates, cached so paint() only issues one
    // fillRectList call instead of rebuilding geometry on every repaint.
    juce::RectangleList<float> m_tick_rects{};

    // Measure labels that survived overlap suppression, with widths already measured. Kept out of
    // paint() because text-width measurement (GlyphArrangement layout) is comparatively expensive
    // and previously ran for every visible measure column on every repaint, including narrow
    // cursor-only repaints driven at vblank cadence or triggered by a single click.
    std::vector<MeasureLabel> m_measure_labels{};
};

} // namespace rock_hero::editor::ui
