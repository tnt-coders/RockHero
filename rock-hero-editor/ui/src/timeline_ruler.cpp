#include "timeline_ruler.h"

#include "editor_colors.h"
#include "timeline_cursor.h"

#include <algorithm>
#include <cmath>
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

// Measures text without using JUCE's deprecated Font string-width helpers.
[[nodiscard]] int textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(arrangement.getBoundingBox(0, -1, true).getWidth()));
}

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
// shared with the track grid and snapping.
void TimelineRuler::setGrid(
    common::core::TempoMap tempo_map, common::core::Fraction grid_spacing_beats)
{
    if (m_tempo_map == tempo_map && m_grid_spacing_beats == grid_spacing_beats)
    {
        return;
    }

    m_tempo_map = std::move(tempo_map);
    m_grid_spacing_beats = grid_spacing_beats;
    refreshRulerGeometry();
    repaint();
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
    const std::optional<common::core::TimePosition> position = timelineCursorPlacementTime(
        m_tempo_map,
        m_grid_spacing_beats,
        m_timeline_range,
        m_content_width,
        timeline_x,
        event.mods.isCtrlDown() ? TimelineCursorPlacementMode::Free
                                : TimelineCursorPlacementMode::SnapToGrid);
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
    int next_measure_x = 4;
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

        // The stored x carries the small inset off the measure tick so drawing needs no
        // per-label offset math.
        const juce::String measure_text{line.measure};
        const int measure_width = textWidth(font, measure_text) + 8;
        if (x >= next_measure_x && x + measure_width <= getWidth())
        {
            m_measure_labels.push_back(
                RulerLabel{.x = x + 4, .text = measure_text, .width = measure_width});
            next_measure_x = x + measure_width + 10;
        }
    }
}

// Rebuilds the tempo and signature bands. The tempo band gets a metronome marking ("♩=120.00")
// for the span each non-terminal anchor starts and the pinned active tempo at the left edge;
// the signature band gets a label at each signature-change downbeat plus the pinned active
// signature. Anchors draw no marker of their own: tempo is not editable yet, so the markings
// alone say everything the display needs. The pinned values need a musical frame of reference,
// so they stay hidden until the first downbeat (the first anchor) reaches or passes the visible
// left edge. Tempo markings split into an enlarged quarter-note glyph and text-size digits,
// cached as adjacent labels because one text draw cannot mix fonts; only the glyph is enlarged,
// so the equals sign rides with the digits.
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

    int next_tempo_x = 4;
    if (pinned_visible)
    {
        const juce::String number =
            "=" + juce::String{m_tempo_map.quarterNoteBpmAtSeconds(view_left_time->seconds), 2};
        const int number_width = textWidth(font, number) + 8;
        m_tempo_prefix_labels.push_back(RulerLabel{.x = 4, .text = prefix, .width = prefix_width});
        m_tempo_labels.push_back(
            RulerLabel{.x = 4 + prefix_width, .text = number, .width = number_width});
        next_tempo_x = 4 + prefix_width + number_width + 10;
    }

    // The terminal anchor only ends the last span, so it gets no marking of its own.
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
    {
        const auto local_x = localXForSeconds(anchors[index].seconds);
        if (!local_x.has_value())
        {
            continue;
        }

        const float x = *local_x;
        const juce::String number =
            "=" + juce::String{m_tempo_map.quarterNoteBpmAtSeconds(anchors[index].seconds), 2};
        const int number_width = textWidth(font, number) + 8;
        const int label_x = static_cast<int>(std::round(x)) + 4;
        const int marking_width = prefix_width + number_width;
        if (label_x >= next_tempo_x && label_x + marking_width <= getWidth())
        {
            m_tempo_prefix_labels.push_back(
                RulerLabel{.x = label_x, .text = prefix, .width = prefix_width});
            m_tempo_labels.push_back(
                RulerLabel{.x = label_x + prefix_width, .text = number, .width = number_width});
            next_tempo_x = label_x + marking_width + 10;
        }
    }

    // Formats one signature as "numerator/denominator".
    const auto signature_text = [](const common::core::TimeSignatureChange& change) {
        return juce::String{change.numerator} + "/" + juce::String{change.denominator};
    };

    int next_signature_x = 4;
    if (pinned_visible)
    {
        const juce::String text =
            signature_text(m_tempo_map.timeSignatureAtSeconds(view_left_time->seconds));
        const int width = textWidth(font, text) + 8;
        m_signature_labels.push_back(RulerLabel{.x = 4, .text = text, .width = width});
        next_signature_x = 4 + width + 10;
    }

    for (const common::core::TimeSignatureChange& change : m_tempo_map.timeSignatures())
    {
        const auto local_x = localXForSeconds(m_tempo_map.secondsAtBeat(change.measure, 1));
        if (!local_x.has_value())
        {
            continue;
        }

        const juce::String text = signature_text(change);
        const int width = textWidth(font, text) + 8;
        const int label_x = static_cast<int>(std::round(*local_x)) + 4;
        if (label_x >= next_signature_x && label_x + width <= getWidth())
        {
            m_signature_labels.push_back(RulerLabel{.x = label_x, .text = text, .width = width});
            next_signature_x = label_x + width + 10;
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

// Draws one cached row of overlap-suppressed labels in the current colour at a fixed vertical
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
// starts at the body's top edge instead of y 0 because the event band above blends into the
// editor chrome, so a full-height line would read as poking out above the ruler.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    if (!m_cursor_x.has_value() || getWidth() <= 0)
    {
        return;
    }

    const int cursor_x = std::clamp(static_cast<int>(std::round(*m_cursor_x)), 0, getWidth() - 1);
    g.setColour(juce::Colours::white);
    g.fillRect(cursor_x, g_ruler_body_top, 1, getHeight() - g_ruler_body_top);
}

} // namespace rock_hero::editor::ui
