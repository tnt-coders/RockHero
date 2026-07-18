#include "timeline_ruler.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "tab/tab_view.h"
#include "timeline/timeline_cursor.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

const juce::Colour g_timeline_ruler_text_color{210, 210, 210};

// Vertical layout: three chip rows sit on top — sections, tempo markings, time signatures —
// each pinning the active value to the left edge while the song scrolls, and each chip dropping
// a dotted leader line down to the ruler body so its exact position stays readable. The ruler
// body below them holds the measure-number row above the tick band; measure ticks run the
// body's full height so the numbers stay attached to their downbeats. The heights fold into
// g_timeline_ruler_height; change them together.
constexpr int g_section_row_y{2};
constexpr int g_tempo_row_y{16};
constexpr int g_signature_row_y{30};
constexpr int g_ruler_body_top{43};
constexpr int g_measure_row_y{45};
constexpr int g_label_row_height{12};
constexpr int g_beat_tick_height{10};
constexpr int g_subdivision_tick_height{5};
// Chip height shared by every ruler chip row and the chord/arpeggio name chips so they read as
// one family.
constexpr int g_chip_height{11};
// Tab-derived chord/arpeggio name chips fill the tick band below the measure-number row, flush
// with the bottom edge so each chip reads as sitting directly on the tablature lane's top rail
// beneath it.
constexpr int g_shape_chip_height{11};

// Shared ruler text face. Cached label widths are measured with the same face they are drawn
// with, so measurement and drawing must both go through these helpers.
[[nodiscard]] juce::Font rulerFont()
{
    return juce::Font{juce::FontOptions{12.0f}};
}

// Chip text face shared with the tab lane's chord/arpeggio name chips.
[[nodiscard]] juce::Font chipFont()
{
    return juce::Font{juce::FontOptions{10.0f}.withStyle("Bold")};
}

// Enlarged bold face used only for the quarter-note glyph of tempo markings: the symbol needs
// more size and weight than the digits to stay legible inside the chip, while bolding or
// enlarging the whole marking only made it muddier.
[[nodiscard]] juce::Font noteGlyphFont()
{
    return juce::Font{juce::FontOptions{13.0f, juce::Font::bold}};
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
    // Binds the row to the ruler's right edge, past which no label may extend. Text rows keep
    // the default inset so a label sits just right of the column it annotates; chip rows pass
    // zero so the chip's left edge lands exactly on its grid column, marking the position.
    explicit RulerRowPlacement(int right_edge, int label_inset = g_label_inset) noexcept
        : m_right_edge(right_edge)
        , m_label_inset(label_inset)
        , m_next_x(label_inset)
    {}

    // Reports whether a label anchored at this column could still be placed, before its width is
    // known; reserve() makes the definitive fit test.
    [[nodiscard]] bool accepts(int anchor_x) const noexcept
    {
        return anchor_x + m_label_inset >= m_next_x;
    }

    // Claims room for a measured label anchored at the column, returning the label's draw x when
    // it fits between the previous label and the right edge.
    [[nodiscard]] std::optional<int> reserve(int anchor_x, int width) noexcept
    {
        const int label_x = anchor_x + m_label_inset;
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

    // Horizontal offset between a label and the column it annotates.
    int m_label_inset;

    // Leftmost x the next label may occupy; starts at the inset so a column at x 0 can label.
    int m_next_x;
};

// Decides whether a row's pinned label must yield to the row's first scrolling label. The pin
// wins only while the incoming label still fits to its right; once the incoming label's anchor
// crosses that boundary, the pin is dropped instead of suppressing the incoming label, so the
// new value keeps scrolling to the left edge and takes over as the pin. The boundary mirrors
// RulerRowPlacement: a pin reserved at column zero accepts the next anchor only from
// pinned_width + g_label_gap onward.
[[nodiscard]] bool pinYieldsToIncomingLabel(
    int pinned_width, std::optional<int> first_scrolling_anchor_x) noexcept
{
    return first_scrolling_anchor_x.has_value() &&
           *first_scrolling_anchor_x < pinned_width + g_label_gap;
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

// Positions the ruler's aligned play-from-here mark (the marker model, 2026-07-18): the
// moving playhead while playing, else the marker — the armed caret's slot or the passive
// transport rest; absent only without a loaded project.
void TimelineRuler::setCursorPosition(std::optional<common::core::TimePosition> cursor_position)
{
    const std::optional<float> next_cursor_x =
        cursor_position.has_value() ? localXForSeconds(cursor_position->seconds) : std::nullopt;
    if (next_cursor_x == m_cursor_x)
    {
        return;
    }

    repaintCursorStrip(*this, m_cursor_x, next_cursor_x);
    m_cursor_x = next_cursor_x;
}

// Stores the tempo map that supplies anchors and click snapping, plus the grid note value
// shared with the track grid and snapping. Like setTimelineView, the rebuild and repaint are
// deferred to the setGridLines push owning-view callers issue after every grid change; rebuilding
// here would run against grid lines scanned for the previous grid and be discarded unpainted.
void TimelineRuler::setGrid(
    const common::core::TempoMap& tempo_map, common::core::Fraction grid_note_value)
{
    if (m_tempo_map == tempo_map && m_grid_note_value == grid_note_value)
    {
        return;
    }

    m_tempo_map = tempo_map;
    m_grid_note_value = grid_note_value;
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

// Paints the chip rows with their dotted leaders, the ruler body's measure-number row and tick
// band, and the chord/arpeggio name chips along the bottom edge.
void TimelineRuler::paint(juce::Graphics& g)
{
    // The chip rows blend into the editor chrome so the chips and their leaders read as part of
    // the header rather than more content; the body below them gets the ruler background, and
    // the color steps alone divide chrome, body, and the content scrolling under it.
    g.fillAll(editorTheme().window_background);
    g.setColour(editorTheme().timeline_ruler_background);
    g.fillRect(0, g_ruler_body_top, getWidth(), getHeight() - g_ruler_body_top);

    if (!m_project_loaded || getWidth() <= 0 || m_content_width <= 0 ||
        m_timeline_range.duration().seconds <= 0.0)
    {
        return;
    }

    // Leaders run down to the top of the ruler body, where the ticks take over as the position
    // marks; every chip row draws after every leader row, so any chip covers a leader crossing
    // it.
    drawChipLeaders(g, m_section_leader_xs, g_section_row_y, editorTheme().section_chip);
    drawChipLeaders(g, m_tempo_leader_xs, g_tempo_row_y, editorTheme().tempo_chip);
    drawChipLeaders(g, m_signature_leader_xs, g_signature_row_y, editorTheme().signature_chip);

    drawBeatTicks(g);

    g.setColour(g_timeline_ruler_text_color.withAlpha(0.82f));
    drawLabelRow(g, m_measure_labels, rulerFont(), g_measure_row_y, g_label_row_height);

    drawChipRow(g, m_section_labels, editorTheme().section_chip, g_section_row_y);
    drawTempoChips(g);
    drawChipRow(g, m_signature_labels, editorTheme().signature_chip, g_signature_row_y);

    drawShapeChips(g);
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
        m_grid_note_value,
        m_timeline_range,
        m_content_width,
        timeline_x,
        placementModeFor(event.mods));
    if (position.has_value())
    {
        m_cursor_placement_callback(*position);
    }
}

// Stores the tab-derived chord/arpeggio name chips for the bottom tick band. An unchanged list
// returns early because every controller state push repeats it.
void TimelineRuler::setShapeLabels(std::vector<RulerShapeLabel> labels)
{
    if (m_shape_labels == labels)
    {
        return;
    }

    m_shape_labels = std::move(labels);
    repaint();
}

// Stores the song's section names for the section chip row. The names cache a pinned,
// overlap-suppressed chip row, so a changed list rebuilds ruler geometry; an unchanged list
// returns early because every controller state push repeats it.
void TimelineRuler::setSectionLabels(std::vector<RulerSectionLabel> labels)
{
    if (m_section_source == labels)
    {
        return;
    }

    m_section_source = std::move(labels);
    refreshRulerGeometry();
    repaint();
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

// Rebuilds the cached tick rectangles, the measure-number row, and the chip rows from the
// stored grid lines, timeline geometry, and tempo map. Kept out of paint() so repaints driven
// only by cursor movement, whether vblank-driven playback or a single click, do not rebuild
// geometry or repeat the per-label GlyphArrangement text-width measurement on every frame; all of
// these only need to rerun when the state they depend on actually changes.
void TimelineRuler::refreshRulerGeometry()
{
    m_tick_rects.clear();
    m_measure_labels.clear();

    const juce::Font font = rulerFont();

    // The pinned chip values and the pinned measure number need a musical frame of reference,
    // so they stay hidden until the first downbeat (the first anchor) reaches or passes the
    // visible left edge; the shared gate keeps every pinned row pinning in lockstep.
    const auto view_left_time =
        core::timelinePositionForX(static_cast<float>(m_view_x), m_timeline_range, m_content_width);
    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();
    const bool pinnable = view_left_time.has_value() && !anchors.empty() &&
                          view_left_time->seconds >= anchors.front().seconds;
    const std::optional<double> pinned_left_seconds =
        pinnable ? std::optional{view_left_time->seconds} : std::nullopt;

    refreshHeaderBands(chipFont(), pinned_left_seconds);
    refreshSectionBand(chipFont(), pinned_left_seconds);

    // Like the chip rows, the active measure pins to the left edge while the song scrolls,
    // seeding the row at column zero so downbeat numbers scrolling underneath suppress
    // uniformly — except when the next downbeat's own number gets close enough to collide,
    // where the pin yields so the incoming number can scroll into its place.
    RulerRowPlacement measure_row{getWidth()};
    const auto place_measure = [&](int anchor_x, int measure) {
        if (!measure_row.accepts(anchor_x))
        {
            return;
        }

        const juce::String measure_text{measure};
        const int measure_width = textWidth(font, measure_text) + g_label_width_pad;
        if (const std::optional<int> label_x = measure_row.reserve(anchor_x, measure_width))
        {
            m_measure_labels.push_back(
                RulerLabel{.x = *label_x, .text = measure_text, .width = measure_width});
        }
    };

    // The first upcoming downbeat number decides whether the pin yields to it; anchors left of
    // the view cannot place a label, so they cannot be the incoming label either.
    std::optional<int> first_measure_anchor_x;
    for (const core::TempoGridLine& line : m_grid_lines)
    {
        if (line.rank == core::TempoGridLineRank::Measure && line.x - m_view_x >= 0)
        {
            first_measure_anchor_x = line.x - m_view_x;
            break;
        }
    }

    if (pinned_left_seconds.has_value())
    {
        // Quantize to hundredths before splitting off the whole beat, like the transport
        // readout: a left edge sitting exactly on a downbeat can otherwise read as the previous
        // measure through anchor-span inverse rounding.
        const auto total_hundredths = static_cast<std::int64_t>(
            std::llround(m_tempo_map.beatPositionAtSeconds(*pinned_left_seconds) * 100.0));
        const int pinned_measure = m_tempo_map.beatAtGlobalIndex(total_hundredths / 100).first;
        const int pinned_width = textWidth(font, juce::String{pinned_measure}) + g_label_width_pad;
        if (!pinYieldsToIncomingLabel(pinned_width, first_measure_anchor_x))
        {
            place_measure(0, pinned_measure);
        }
    }

    // Subdivision ticks stay half the beat height so the ruler reads which short ticks are real
    // beats even when a fine grid fills the space between them; measure ticks span the whole
    // body and carry their number.
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
        place_measure(x, line.measure);
    }
}

// Rebuilds the tempo and signature chip rows. The tempo row gets a metronome marking ("♩=120.00")
// for the span each non-terminal anchor starts and the pinned active tempo at the left edge;
// the signature row gets a chip at each signature-change downbeat plus the pinned active
// signature. Anchors draw no marker of their own: each chip's left edge sits on its grid column,
// which marks the position. The pinned values seed their rows at column zero so the shared
// placement policy positions and suppresses everything uniformly, but each pin yields to its
// row's first scrolling chip once that chip would collide, so the incoming value scrolls all
// the way to the left edge instead of vanishing behind the pin; the caller owns the pin gate,
// keeping the chip rows and the measure row pinning in lockstep. Tempo markings split into an
// enlarged quarter-note glyph and chip-size digits, cached as adjacent labels because one text
// draw cannot mix fonts; only the glyph is enlarged, so the equals sign rides with the digits.
void TimelineRuler::refreshHeaderBands(
    const juce::Font& font, std::optional<double> pinned_left_seconds)
{
    m_tempo_prefix_labels.clear();
    m_tempo_labels.clear();
    m_tempo_leader_xs.clear();
    m_signature_labels.clear();
    m_signature_leader_xs.clear();

    // The quarter-note glyph is U+2669, supplied as escaped UTF-8 so source-file encoding cannot
    // corrupt it; text shaping falls back to a symbol font when the UI font lacks the glyph.
    const juce::String prefix = juce::String::fromUTF8("\xE2\x99\xA9");
    const juce::Font prefix_font = noteGlyphFont();
    const int prefix_width = textWidth(prefix_font, prefix) + 1;

    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();

    // Places one metronome marking — glyph plus digits — as a single suppression unit, resolving
    // the tempo and measuring the digits only after the cheap position test. Chip rows use a
    // zero inset so the chip edge lands on the anchor's grid column.
    RulerRowPlacement tempo_row{getWidth(), 0};
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

    // The first upcoming marking decides whether the pinned marking yields to it. The gate
    // re-measures the pinned digits, so a surviving pin measures them twice per rebuild; that
    // stays cheaper than formatting every suppressed scrolling candidate eagerly.
    std::optional<int> first_marking_anchor_x;
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
    {
        if (const auto local_x = localXForSeconds(anchors[index].seconds))
        {
            first_marking_anchor_x = static_cast<int>(std::round(*local_x));
            break;
        }
    }

    if (pinned_left_seconds.has_value())
    {
        const juce::String pinned_digits =
            "=" + juce::String{m_tempo_map.quarterNoteBpmAtSeconds(*pinned_left_seconds), 2};
        const int pinned_width = prefix_width + textWidth(font, pinned_digits) + g_label_width_pad;
        if (!pinYieldsToIncomingLabel(pinned_width, first_marking_anchor_x))
        {
            place_marking(0, *pinned_left_seconds);
        }
    }

    // The terminal anchor only ends the last span, so it gets no marking of its own.
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
    {
        const auto local_x = localXForSeconds(anchors[index].seconds);
        if (local_x.has_value())
        {
            const int anchor_x = static_cast<int>(std::round(*local_x));
            m_tempo_leader_xs.push_back(anchor_x);
            place_marking(anchor_x, anchors[index].seconds);
        }
    }

    // Places one signature chip, formatting and measuring only after the position test.
    RulerRowPlacement signature_row{getWidth(), 0};
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

    // The first upcoming signature label decides whether the pinned signature yields to it.
    std::optional<int> first_signature_anchor_x;
    for (const common::core::TimeSignatureChange& change : m_tempo_map.timeSignatures())
    {
        if (const auto local_x = localXForSeconds(m_tempo_map.secondsAtBeat(change.measure, 1)))
        {
            first_signature_anchor_x = static_cast<int>(std::round(*local_x));
            break;
        }
    }

    if (pinned_left_seconds.has_value())
    {
        const common::core::TimeSignatureChange pinned_signature =
            m_tempo_map.timeSignatureAtSeconds(*pinned_left_seconds);
        const juce::String pinned_text = juce::String{pinned_signature.numerator} + "/" +
                                         juce::String{pinned_signature.denominator};
        const int pinned_width = textWidth(font, pinned_text) + g_label_width_pad;
        if (!pinYieldsToIncomingLabel(pinned_width, first_signature_anchor_x))
        {
            place_signature(0, pinned_signature);
        }
    }

    for (const common::core::TimeSignatureChange& change : m_tempo_map.timeSignatures())
    {
        const auto local_x = localXForSeconds(m_tempo_map.secondsAtBeat(change.measure, 1));
        if (local_x.has_value())
        {
            const int anchor_x = static_cast<int>(std::round(*local_x));
            m_signature_leader_xs.push_back(anchor_x);
            place_signature(anchor_x, change);
        }
    }
}

// Rebuilds the section chip row: one chip per visible section start plus the pinned active
// section (the last one starting at or before the view edge) at the left edge, sharing the
// header rows' pin gate. Section starts come from the controller's section projection, not the
// tempo map, so positions resolve through localXForSeconds.
void TimelineRuler::refreshSectionBand(
    const juce::Font& font, std::optional<double> pinned_left_seconds)
{
    m_section_labels.clear();
    m_section_leader_xs.clear();

    RulerRowPlacement section_row{getWidth(), 0};
    const auto place_section = [&](int anchor_x, const juce::String& name) {
        if (name.isEmpty() || !section_row.accepts(anchor_x))
        {
            return;
        }

        const int width = textWidth(font, name) + g_label_width_pad;
        if (const std::optional<int> label_x = section_row.reserve(anchor_x, width))
        {
            m_section_labels.push_back(RulerLabel{.x = *label_x, .text = name, .width = width});
        }
    };

    // The first section starting at or right of the view edge decides whether the pin yields.
    std::optional<int> first_section_anchor_x;
    for (const RulerSectionLabel& section : m_section_source)
    {
        if (const auto local_x = localXForSeconds(section.seconds))
        {
            first_section_anchor_x = static_cast<int>(std::round(*local_x));
            break;
        }
    }

    if (pinned_left_seconds.has_value())
    {
        // Sections ascend by start, so the active one is the last starting at or before the edge.
        const RulerSectionLabel* active = nullptr;
        for (const RulerSectionLabel& section : m_section_source)
        {
            if (section.seconds > *pinned_left_seconds)
            {
                break;
            }
            active = &section;
        }
        if (active != nullptr)
        {
            const int pinned_width = textWidth(font, active->name) + g_label_width_pad;
            if (!pinYieldsToIncomingLabel(pinned_width, first_section_anchor_x))
            {
                place_section(0, active->name);
            }
        }
    }

    for (const RulerSectionLabel& section : m_section_source)
    {
        if (const auto local_x = localXForSeconds(section.seconds))
        {
            const int anchor_x = static_cast<int>(std::round(*local_x));
            m_section_leader_xs.push_back(anchor_x);
            place_section(anchor_x, section.name);
        }
    }
}

// Draws visible grid ticks, with measure ticks promoted to the ruler body's full height so the
// measure-number row stays visually attached to its downbeats.
void TimelineRuler::drawBeatTicks(juce::Graphics& g)
{
    if (!m_tick_rects.isEmpty())
    {
        g.setColour(editorTheme().grid_measure);
        g.fillRectList(m_tick_rects);
    }
}

// Draws one chip row's dotted leaders: a 1px dotted column from the row's chip bottom down to
// the top of the ruler body at every visible event position — the leaders stay on the chip
// area's dark surface, where the body's own ticks take over as the position marks. The leaders
// come from the cached event columns rather than the placed chips, so a chip suppressed on a
// dense map still marks its position and a pinned chip (whose anchor is off-screen) never draws
// one.
void TimelineRuler::drawChipLeaders(
    juce::Graphics& g, const std::vector<int>& anchor_xs, int row_y, juce::Colour color)
{
    if (anchor_xs.empty())
    {
        return;
    }

    juce::RectangleList<float> dots;
    for (const int x : anchor_xs)
    {
        for (int y = row_y + g_chip_height; y < g_ruler_body_top - 1; y += 2)
        {
            dots.addWithoutMerging(juce::Rectangle<int>{x, y, 1, 1}.toFloat());
        }
    }
    g.setColour(color);
    g.fillRectList(dots);
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

// Draws one cached row of overlap-suppressed labels as filled chips — rounded fill, white
// centered text — the chord-chip style the tab lane's name chips established. The labels must
// have been measured with the chip font.
void TimelineRuler::drawChipRow(
    juce::Graphics& g, const std::vector<RulerLabel>& labels, juce::Colour fill, int row_y)
{
    const juce::Font font = chipFont();
    for (const RulerLabel& label : labels)
    {
        const juce::Rectangle<float> chip{
            static_cast<float>(label.x),
            static_cast<float>(row_y),
            static_cast<float>(label.width),
            static_cast<float>(g_chip_height)
        };
        g.setColour(fill);
        g.fillRoundedRectangle(chip, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(font);
        g.drawText(label.text, chip, juce::Justification::centred);
    }
}

// Draws the tempo chip row: one fill spanning each cached glyph+digits pair (the two label
// vectors are parallel), then the glyph and digits in their own fonts because one text draw
// cannot mix fonts. The enlarged glyph centers on the chip so its extra size hangs evenly.
void TimelineRuler::drawTempoChips(juce::Graphics& g)
{
    const juce::Font digits_font = chipFont();
    const juce::Font glyph_font = noteGlyphFont();
    for (std::size_t index = 0; index < m_tempo_labels.size(); ++index)
    {
        const RulerLabel& glyph = m_tempo_prefix_labels[index];
        const RulerLabel& digits = m_tempo_labels[index];
        const juce::Rectangle<float> chip{
            static_cast<float>(glyph.x),
            static_cast<float>(g_tempo_row_y),
            static_cast<float>(glyph.width + digits.width),
            static_cast<float>(g_chip_height)
        };
        g.setColour(editorTheme().tempo_chip);
        g.fillRoundedRectangle(chip, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(glyph_font);
        g.drawText(
            glyph.text,
            chip.withWidth(static_cast<float>(glyph.width)).translated(3.0f, 0.0f),
            juce::Justification::centredLeft);
        g.setFont(digits_font);
        g.drawText(
            digits.text,
            chip.withTrimmedLeft(static_cast<float>(glyph.width) + 3.0f),
            juce::Justification::centredLeft);
    }
}

// Draws the chord/arpeggio name chips flush with the ruler's bottom edge, directly above the
// tablature lane's top rail (user-directed placement: the lane has no clean room for names, and
// this band overlaps only ticks — the measure-number row above stays clear). Chips draw left to
// right in span order, so where shape changes crowd tighter than a name's width the later
// span's chip wins locally instead of being suppressed entirely.
void TimelineRuler::drawShapeChips(juce::Graphics& g)
{
    if (m_shape_labels.empty())
    {
        return;
    }

    const juce::Font chip_font = chipFont();
    for (const RulerShapeLabel& label : m_shape_labels)
    {
        const std::optional<float> local_x = localXForSeconds(label.seconds);
        if (!local_x.has_value())
        {
            continue;
        }

        const float chip_width = static_cast<float>(textWidth(chip_font, label.name)) + 6.0f;
        const juce::Rectangle<float> chip{
            *local_x,
            static_cast<float>(getHeight() - g_shape_chip_height),
            chip_width,
            static_cast<float>(g_shape_chip_height)
        };
        g.setColour(tabShapeMarkColor(label.arpeggio));
        g.fillRoundedRectangle(chip, 2.0f);
        g.setColour(juce::Colours::white);
        g.setFont(chip_font);
        g.drawText(label.name, chip, juce::Justification::centred);
    }
}

// Draws the same transport cursor through the ruler body for vertical alignment. The cursor
// starts at the body's top edge instead of y 0 so it does not cut through the chip rows above.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    drawTimelineCursor(g, *this, m_cursor_x, g_ruler_body_top);
}

} // namespace rock_hero::editor::ui
