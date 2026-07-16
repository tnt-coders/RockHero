#include "timeline/tempo_grid_dots.h"

#include "shared/editor_theme.h"

#include <algorithm>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_tempo_grid_dot_size{1};
constexpr int g_tempo_grid_dot_gap{1};

// Returns the first dotted-grid row in the current paint clip, preserving the pattern's bounds
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

} // namespace

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

} // namespace rock_hero::editor::ui
