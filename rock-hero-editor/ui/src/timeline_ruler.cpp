#include "timeline_ruler.h"

#include "editor_colors.h"
#include "text_metrics.h"
#include "timeline_cursor.h"

#include <cmath>
#include <optional>
#include <rock_hero/editor/core/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline_geometry.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

const juce::Colour g_timeline_ruler_color{juce::Colours::darkgrey.darker(0.45f)};
const juce::Colour g_timeline_ruler_text_color{210, 210, 210};
const juce::Colour g_timeline_tempo_color{180, 218, 255};
const juce::Colour g_timeline_signature_color{255, 214, 140};

// Vertical layout: the tempo band sits on top, holding every tempo change at its timeline
// position like a DAW tempo lane; the signature band directly below it holds every signature
// change at its downbeat. Both bands pin the active value to the left edge while the song
// scrolls. The ruler body below them holds the measure-number row above the tick band; measure
// ticks run the body's full height so the numbers stay attached to their downbeats.
constexpr int g_tempo_row_y{1};
constexpr int g_tempo_band_bottom{15};
constexpr int g_signature_row_y{15};
constexpr int g_ruler_body_top{28};
constexpr int g_measure_row_y{30};
constexpr int g_label_row_height{12};
constexpr int g_beat_tick_height{10};
constexpr int g_subdivision_tick_height{5};

// Shared ruler text face. Cached label widths are measured with the same face they are drawn
// with, so measurement and drawing must both go through these helpers.
[[nodiscard]] juce::Font rulerFont()
{
    return juce::Font{juce::FontOptions{12.0f}};
}

// Enlarged bold face used only for the quarter-note glyph of tempo markings: the symbol needs
// significantly more size and weight than the digits to stay legible, while bolding or enlarging
// the whole marking only made it muddier.
[[nodiscard]] juce::Font noteGlyphFont()
{
    return juce::Font{juce::FontOptions{16.0f, juce::Font::bold}};
}

// Shared label layout policy for every ruler row: labels sit g_label_inset right of the column
// they annotate, measured widths carry g_label_width_pad so drawText keeps breathing room, and
// consecutive labels keep g_label_gap of separation.
constexpr int g_label_inset{4};
constexpr int g_label_width_pad{8};
constexpr int g_label_gap{10};

// Greedy left-to-right overlap suppression for one ruler label row. Every row routes its
// candidates through one of these so the inset, padding, and gap policy cannot drift between
// rows, and the position-only accepts() test lets callers skip text formatting and
// GlyphArrangement measurement for columns the previous label already covers — on dense maps far
// more candidates arrive per rebuild than survive suppression.
class RulerRowPlacement
{
public:
    // Binds the row to the ruler's right edge, past which no label may extend.
    explicit RulerRowPlacement(int right_edge) noexcept
        : m_right_edge(right_edge)
    {}

    // Reports whether a label anchored at this column could still be placed, before its width is
    // known; reserve() makes the definitive fit test.
    [[nodiscard]] bool accepts(int anchor_x) const noexcept
    {
        return anchor_x + g_label_inset >= m_next_x;
    }

    // Claims room for a measured label anchored at the column, returning the label's draw x when
    // it fits between the previous label and the right edge.
    [[nodiscard]] std::optional<int> reserve(int anchor_x, int width) noexcept
    {
        const int label_x = anchor_x + g_label_inset;
        if (label_x < m_next_x || label_x + width > m_right_edge)
        {
            return std::nullopt;
        }

        m_next_x = label_x + width + g_label_gap;
        return label_x;
    }

private:
    // Right edge of the row in local coordinates.
    int m_right_edge;

    // Leftmost x the next label may occupy; starts at the inset so a column at x 0 can label.
    int m_next_x{g_label_inset};
};

} // namespace

// Names the component for tests and enables direct mouse placement.
TimelineRuler::TimelineRuler()
{
    setComponentID("timeline_ruler");
    setInterceptsMouseClicks(true, false);
}

// Stores whether the ruler should draw musical position data.
void TimelineRuler::setProjectLoaded(bool project_loaded)
{
    if (m_project_loaded == project_loaded)
    {
        return;
    }

    m_project_loaded = project_loaded;
    repaint();
}

// Stores the ruler geometry derived from the viewport and zoomed content. The rebuild and repaint
// are deferred to the setGridLines push that owning-view callers issue after every view change,
// because tick coordinates need lines scanned for the new span.
void TimelineRuler::setTimelineView(
    common::core::TimeRange timeline_range, int content_width, int view_x)
{
    m_timeline_range = timeline_range;
    m_content_width = content_width;
    m_view_x = view_x;
}

// Samples the current transport cursor for the ruler's aligned playhead mark.
void TimelineRuler::setCursorPosition(common::core::TimePosition cursor_position)
{
    const auto next_cursor_x = localXForSeconds(cursor_position.seconds);
    if (next_cursor_x == m_cursor_x)
    {
        return;
    }

    repaintCursorStrip(*this, m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

// Stores the tempo map that supplies anchors and click snapping, plus the grid step in beats
// shared with the track grid and snapping. Like setTimelineView, the rebuild and repaint are
// deferred to the setGridLines push owning-view callers issue after every grid change; rebuilding
// here would run against grid lines scanned for the previous grid and be discarded unpainted.
void TimelineRuler::setGrid(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_spacing_beats)
{
    if (m_tempo_map == tempo_map && m_grid_spacing_beats == grid_spacing_beats)
    {
        return;
    }

    m_tempo_map = tempo_map;
    m_grid_spacing_beats = grid_spacing_beats;
}

// Stores the shared visible-span grid lines and rebuilds the cached ruler geometry from them.
// This is the one scan result both the ruler and the track content render from.
void TimelineRuler::setGridLines(std::vector<core::TempoGridLine> grid_lines)
{
    m_grid_lines = std::move(grid_lines);
    refreshRulerGeometry();
    repaint();
}

// Stores the callback that receives cursor-placement seek positions.
void TimelineRuler::setCursorPlacementCallback(CursorPlacementCallback callback)
{
    m_cursor_placement_callback = std::move(callback);
}

// Paints the tempo and signature bands above the ruler body, the body's measure-number row, and
// the tick band.
void TimelineRuler::paint(juce::Graphics& g)
{
    // The tempo and signature bands blend into the editor chrome so they read as sitting above
    // the ruler; only the body below them gets the ruler background.
    g.fillAll(g_editor_background_color);
    g.setColour(g_timeline_ruler_color);
    g.fillRect(0, g_ruler_body_top, getWidth(), getHeight() - g_ruler_body_top);
    g.setColour(g_track_viewport_color);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    if (!m_project_loaded || getWidth() <= 0 || m_content_width <= 0 ||
        m_timeline_range.duration().seconds <= 0.0)
    {
        return;
    }

    drawBeatTicks(g);

    // Measure numbers fill the body row; tempo markings and signature labels each fill their own
    // band. Tempo markings split into an enlarged quarter-note glyph and text-size digits because
    // one text draw cannot mix fonts; the glyph centers in the full tempo-band height so its
    // extra size hangs evenly around the digit row.
    const juce::Font label_font = rulerFont();
    g.setColour(g_timeline_ruler_text_color.withAlpha(0.82f));
    drawLabelRow(g, m_measure_labels, label_font, g_measure_row_y, g_label_row_height);
    g.setColour(g_timeline_signature_color);
    drawLabelRow(g, m_signature_labels, label_font, g_signature_row_y, g_label_row_height);
    g.setColour(g_timeline_tempo_color);
    drawLabelRow(g, m_tempo_prefix_labels, noteGlyphFont(), 0, g_tempo_band_bottom);
    drawLabelRow(g, m_tempo_labels, label_font, g_tempo_row_y, g_label_row_height);

    drawCursor(g);
}

// Refreshes cached ruler geometry after a resize changes the visible ruler width.
void TimelineRuler::resized()
{
    refreshRulerGeometry();
}

// Converts ruler clicks into timeline seek positions using scrollable timeline coordinates.
void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    if (!m_project_loaded || m_content_width <= 0 || !m_cursor_placement_callback ||
        !event.mods.isLeftButtonDown())
    {
        return;
    }

    const float timeline_x = static_cast<float>(m_view_x) + event.position.x;
    const std::optional<common::core::TimePosition> position = core::timelineCursorPlacementTime(
        m_tempo_map,
        m_grid_spacing_beats,
        m_timeline_range,
        m_content_width,
        timeline_x,
        placementModeFor(event.mods));
    if (position.has_value())
    {
        m_cursor_placement_callback(*position);
    }
}

// Maps an absolute timeline second to this pinned ruler's local x coordinate.
std::optional<float> TimelineRuler::localXForSeconds(double seconds) const noexcept
{
    const auto content_x = core::timelineXForPosition(
        common::core::TimePosition{seconds},
        m_timeline_range,
        m_content_width,
        core::TimelinePositionClamping::RejectOutsideVisibleRange);
    if (!content_x.has_value())
    {
        return std::nullopt;
    }

    const float local_x = *content_x - static_cast<float>(m_view_x);
    if (local_x < 0.0f || local_x >= static_cast<float>(getWidth()))
    {
        return std::nullopt;
    }

    return local_x;
}

// Rebuilds cached tick rectangles, the body's measure-number row, and the header bands from the
// stored grid lines, timeline geometry, and tempo map. Kept out of paint() so repaints driven
// only by cursor movement, whether vblank-driven playback or a single click, do not rebuild
// geometry or repeat the per-label GlyphArrangement text-width measurement on every frame; all of
// these only need to rerun when the state they depend on actually changes.
void TimelineRuler::refreshRulerGeometry()
{
    m_tick_rects.clear();
    m_measure_labels.clear();

    const juce::Font font = rulerFont();
    refreshHeaderBands(font);

    // Subdivision ticks stay half the beat height so the ruler reads which short ticks are real
    // beats even when a fine grid fills the space between them; measure ticks span the whole
    // body and carry their number, which scrolls with the tick like a DAW bar ruler.
    RulerRowPlacement measure_row{getWidth()};
    for (const core::TempoGridLine& line : m_grid_lines)
    {
        const int x = line.x - m_view_x;
        if (line.rank != core::TempoGridLineRank::Measure)
        {
            const int tick_height = line.rank == core::TempoGridLineRank::Beat
                                        ? g_beat_tick_height
                                        : g_subdivision_tick_height;
            m_tick_rects.addWithoutMerging(
                juce::Rectangle<int>{x, getHeight() - tick_height, 1, tick_height}.toFloat());
            continue;
        }

        m_tick_rects.addWithoutMerging(
            juce::Rectangle<int>{x, g_ruler_body_top, 1, getHeight() - g_ruler_body_top}.toFloat());

        if (!measure_row.accepts(x))
        {
            continue;
        }

        const juce::String measure_text{line.measure};
        const int measure_width = textWidth(font, measure_text) + g_label_width_pad;
        if (const std::optional<int> label_x = measure_row.reserve(x, measure_width))
        {
            m_measure_labels.push_back(
                RulerLabel{.x = *label_x, .text = measure_text, .width = measure_width});
        }
    }
}

// Rebuilds the tempo and signature bands. The tempo band gets a metronome marking ("♩=120.00")
// for the span each non-terminal anchor starts and the pinned active tempo at the left edge;
// the signature band gets a label at each signature-change downbeat plus the pinned active
// signature. Anchors draw no marker of their own: tempo is not editable yet, so the markings
// alone say everything the display needs. The pinned values need a musical frame of reference,
// so they stay hidden until the first downbeat (the first anchor) reaches or passes the visible
// left edge; they seed their rows at column zero so the shared placement policy positions and
// suppresses everything uniformly. Tempo markings split into an enlarged quarter-note glyph and
// text-size digits, cached as adjacent labels because one text draw cannot mix fonts; only the
// glyph is enlarged, so the equals sign rides with the digits.
void TimelineRuler::refreshHeaderBands(const juce::Font& font)
{
    m_tempo_prefix_labels.clear();
    m_tempo_labels.clear();
    m_signature_labels.clear();

    // The quarter-note glyph is U+2669, supplied as escaped UTF-8 so source-file encoding cannot
    // corrupt it; text shaping falls back to a symbol font when the UI font lacks the glyph.
    const juce::String prefix = juce::String::fromUTF8("\xE2\x99\xA9");
    const juce::Font prefix_font = noteGlyphFont();
    const int prefix_width = textWidth(prefix_font, prefix) + 1;

    const auto view_left_time =
        core::timelinePositionForX(static_cast<float>(m_view_x), m_timeline_range, m_content_width);
    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();
    const bool pinned_visible = view_left_time.has_value() && !anchors.empty() &&
                                view_left_time->seconds >= anchors.front().seconds;

    // Places one metronome marking — glyph plus digits — as a single suppression unit, resolving
    // the tempo and measuring the digits only after the cheap position test.
    RulerRowPlacement tempo_row{getWidth()};
    const auto place_marking = [&](int anchor_x, double marking_seconds) {
        if (!tempo_row.accepts(anchor_x))
        {
            return;
        }

        const juce::String digits =
            "=" + juce::String{m_tempo_map.quarterNoteBpmAtSeconds(marking_seconds), 2};
        const int digits_width = textWidth(font, digits) + g_label_width_pad;
        const std::optional<int> label_x = tempo_row.reserve(anchor_x, prefix_width + digits_width);
        if (!label_x.has_value())
        {
            return;
        }

        m_tempo_prefix_labels.push_back(
            RulerLabel{.x = *label_x, .text = prefix, .width = prefix_width});
        m_tempo_labels.push_back(
            RulerLabel{.x = *label_x + prefix_width, .text = digits, .width = digits_width});
    };

    if (pinned_visible)
    {
        place_marking(0, view_left_time->seconds);
    }

    // The terminal anchor only ends the last span, so it gets no marking of its own.
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
    {
        const auto local_x = localXForSeconds(anchors[index].seconds);
        if (local_x.has_value())
        {
            place_marking(static_cast<int>(std::round(*local_x)), anchors[index].seconds);
        }
    }

    // Places one signature label, formatting and measuring only after the position test.
    RulerRowPlacement signature_row{getWidth()};
    const auto place_signature = [&](int anchor_x,
                                     const common::core::TimeSignatureChange& change) {
        if (!signature_row.accepts(anchor_x))
        {
            return;
        }

        const juce::String text =
            juce::String{change.numerator} + "/" + juce::String{change.denominator};
        const int width = textWidth(font, text) + g_label_width_pad;
        if (const std::optional<int> label_x = signature_row.reserve(anchor_x, width))
        {
            m_signature_labels.push_back(RulerLabel{.x = *label_x, .text = text, .width = width});
        }
    };

    if (pinned_visible)
    {
        place_signature(0, m_tempo_map.timeSignatureAtSeconds(view_left_time->seconds));
    }

    for (const common::core::TimeSignatureChange& change : m_tempo_map.timeSignatures())
    {
        const auto local_x = localXForSeconds(m_tempo_map.secondsAtBeat(change.measure, 1));
        if (local_x.has_value())
        {
            place_signature(static_cast<int>(std::round(*local_x)), change);
        }
    }
}

// Draws visible grid ticks, with measure ticks promoted to the ruler body's full height so the
// measure-number row stays visually attached to its downbeats.
void TimelineRuler::drawBeatTicks(juce::Graphics& g)
{
    if (!m_tick_rects.isEmpty())
    {
        g.setColour(g_measure_grid_color);
        g.fillRectList(m_tick_rects);
    }
}

// Draws one cached row of overlap-suppressed labels in the current color at a fixed vertical
// band, using the same font the row's widths were measured with.
void TimelineRuler::drawLabelRow(
    juce::Graphics& g, const std::vector<RulerLabel>& labels, const juce::Font& font, int y,
    int height)
{
    g.setFont(font);
    for (const RulerLabel& label : labels)
    {
        g.drawText(label.text, label.x, y, label.width, height, juce::Justification::centredLeft);
    }
}

// Draws the same transport cursor through the ruler body for vertical alignment. The cursor
// starts at the body's top edge instead of y 0 because the header bands above blend into the
// editor chrome, so a full-height line would read as poking out above the ruler.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    drawTimelineCursor(g, *this, m_cursor_x, g_ruler_body_top);
}

} // namespace rock_hero::editor::ui
