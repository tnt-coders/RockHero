#include "timeline_ruler.h"

#include "keybinds/editor_command_id.h"
#include "keybinds/editor_command_registry.h"
#include "shared/editor_theme.h"
#include "shared/mark_outline.h"
#include "shared/text_metrics.h"
#include "timeline/sticky_label.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/common/core/timeline/time_signature_text.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <rock_hero/editor/core/timeline/time_signature_target.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <utility>
#include <variant>
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
// body's full height so the numbers stay attached to their downbeats. The tab lane's span chips
// stand on the ruler's bottom edge over the tick band, every chip's height but never taller than
// the room below the measure numbers' ink (spanChipMaxHeight). The heights fold into
// g_timeline_ruler_height; change them together.
constexpr int g_section_row_y{2};
constexpr int g_tempo_row_y{16};
constexpr int g_signature_row_y{30};
constexpr int g_ruler_body_top{43};
constexpr int g_measure_row_y{45};
constexpr int g_label_row_height{12};
constexpr int g_beat_tick_height{10};
constexpr int g_subdivision_tick_height{5};
// Corner radius of every chip's rounded fill, and of the outlines that trace it.
constexpr float g_chip_corner_radius{2.0f};
// The play-from-here flag: a filled triangle at the ruler body's top pointing down the cursor
// line, sized to read at a glance — the 1px alignment line alone is too easy to miss.
constexpr float g_cursor_flag_half_width{5.0f};
constexpr float g_cursor_flag_height{7.0f};

// The renumber flash: the measure numbers a time-signature edit renumbered light in the accent once
// and fade back, a single pulse held a little longer than a refusal's two so a whole run of
// numbers reads as one change.
constexpr FlashTrain g_renumber_flash_train{.pulses = 1.0, .seconds = 0.8};

// Shared ruler text face. Cached label widths are measured with the same face they are drawn
// with, so measurement and drawing must both go through these helpers.
[[nodiscard]] juce::Font rulerFont()
{
    return juce::Font{juce::FontOptions{12.0f}};
}

// The tallest a span chip may stand on the ruler's bottom edge: up to a clear row below the
// measure numbers' baseline, the lowest edge of their ink since digits carry no descenders. Read
// from the face's own metrics, placed as drawText centres a line in the label row, so the chip
// keeps the clearance on every platform's face.
[[nodiscard]] float spanChipMaxHeight()
{
    const juce::Font font = rulerFont();
    const float baseline = static_cast<float>(g_measure_row_y) +
                           ((static_cast<float>(g_label_row_height) - font.getHeight()) / 2.0f) +
                           font.getAscent();
    return static_cast<float>(g_timeline_ruler_height) - (std::ceil(baseline) + 1.0f);
}

// Shared label layout policy for every ruler row: labels sit g_label_inset right of the column
// they annotate, and measured widths carry g_label_width_pad so drawText keeps breathing room.
// The separation consecutive labels keep is g_pinned_label_gap, which lives beside the pin law
// (sticky_label.h) because that law spends the same clearance deciding when a pinned value yields
// — one value, so the two can never drift apart.
constexpr int g_label_inset{4};
constexpr int g_label_width_pad{8};

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
    // it fits between the previous label and the right edge and keeps clear of the claimed label.
    [[nodiscard]] std::optional<int> reserve(int anchor_x, int width) noexcept
    {
        const int label_x = anchor_x + m_label_inset;
        if (label_x < m_next_x || label_x + width > m_right_edge ||
            (m_claimed.has_value() && label_x < m_claimed->end + g_pinned_label_gap &&
             m_claimed->start < label_x + width + g_pinned_label_gap))
        {
            return std::nullopt;
        }

        m_next_x = label_x + width + g_pinned_label_gap;
        return label_x;
    }

    // Sets one label's room aside before the greedy pass, returning its draw x when it fits the
    // row: every label reserved afterwards keeps the usual gap clear of it on either side, so its
    // neighbours are the ones suppressed. The claimed label itself is never reserved again.
    [[nodiscard]] std::optional<int> claim(int anchor_x, int width) noexcept
    {
        const int label_x = anchor_x + m_label_inset;
        if (label_x + width > m_right_edge)
        {
            return std::nullopt;
        }

        m_claimed = Interval{.start = label_x, .end = label_x + width};
        return label_x;
    }

private:
    // A claimed label's horizontal extent.
    struct Interval
    {
        int start{0};
        int end{0};
    };

    // Right edge of the row in local coordinates.
    int m_right_edge;

    // Horizontal offset between a label and the column it annotates.
    int m_label_inset;

    // Leftmost x the next label may occupy; starts at the inset so a column at x 0 can label.
    int m_next_x;

    // The label set aside ahead of the greedy pass, if any.
    std::optional<Interval> m_claimed{};
};

// How far the quarter note's outline is thickened, in pixels at the chip's size. The symbol font
// that draws U+2669 has no bold cut (Segoe UI Symbol on Windows), so the outline is taken at the
// regular weight everywhere, whose stem measures 0.64 px against the bold chip digits' 1.8 px; a
// stroke this wide brings the note's stem to the digits'.
constexpr float g_quarter_note_stroke_width{1.25f};

// The quarter-note glyph that opens every tempo marking, as an outline whose ink stands
// `ink_height` tall with its top-left corner at the origin; empty where no font draws the glyph.
// U+2669 is supplied as escaped UTF-8 so source-file encoding cannot corrupt it, and text shaping
// falls back to a symbol font when the UI font lacks it. Measured at the face's default size and
// scaled, because an outline scales exactly as a larger font size would, then thickened once here
// rather than stroked on every paint; the scale leaves room for the stroke so the ink still stands
// `ink_height` tall. Drawn as a path because a text draw snaps its baseline to a whole row, which
// would cost the glyph a row of size.
[[nodiscard]] juce::Path quarterNoteOutline(const float ink_height)
{
    juce::GlyphArrangement glyph;
    glyph.addLineOfText(
        juce::Font{juce::FontOptions{}}, juce::String::fromUTF8("\xE2\x99\xA9"), 0.0f, 0.0f);
    juce::Path outline;
    glyph.createPath(outline);
    const juce::Rectangle<float> regular_ink = outline.getBounds();
    if (regular_ink.isEmpty())
    {
        return outline;
    }
    outline.applyTransform(
        juce::AffineTransform::translation(-regular_ink.getX(), -regular_ink.getY())
            .scaled((ink_height - g_quarter_note_stroke_width) / regular_ink.getHeight()));
    juce::Path stroke;
    juce::PathStrokeType{
        g_quarter_note_stroke_width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded
    }
        .createStrokedPath(stroke, outline);
    outline.addPath(stroke);
    const juce::Rectangle<float> ink = outline.getBounds();
    outline.applyTransform(juce::AffineTransform::translation(-ink.getX(), -ink.getY()));
    return outline;
}

// The room a tempo chip spends on its quarter note ahead of the digits: the glyph's ink and a clear
// column before the equals sign's advance box.
[[nodiscard]] int tempoGlyphLead(const juce::Path& glyph)
{
    return static_cast<int>(std::ceil(glyph.getBounds().getWidth())) + 1;
}

// The measure-number row's band across a ruler of the given width: what the renumber flash
// repaints.
[[nodiscard]] juce::Rectangle<int> measureRowBounds(const int width) noexcept
{
    return juce::Rectangle<int>{0, g_measure_row_y, width, g_label_row_height};
}

} // namespace

// Names the component for tests, enables direct mouse placement and takes the span chips' inks.
// The quarter note's ink fills the chip but for the clear rows every chip keeps.
TimelineRuler::TimelineRuler(const common::ui::TabShapeInks shape_inks)
    : m_tempo_glyph{quarterNoteOutline(
          static_cast<float>(m_chip_height) - (2.0f * common::ui::g_chip_ink_clearance))}
    , m_shape_inks{shape_inks}
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

// Positions the ruler's aligned play-from-here mark (the marker model): the moving playhead
// while playing, else the marker — the armed caret's slot or the passive transport rest; absent
// only without a loaded project. The paused flag picks the mark's color so it always matches the
// cursor drawn below it in the content.
//
// The mark is stored in seconds and mapped to a column in drawCursor, exactly as the shape chips
// and the cursor overlay's time selection are. Pre-mapping here would freeze the column against the
// scroll offset that was current at the push: the caller short-circuits on a memo key of musical
// inputs, so a pure horizontal scroll moves the content without a push and the mark would stay
// pinned to the same screen pixel, pointing at the wrong time.
void TimelineRuler::setCursorPosition(
    std::optional<common::core::TimePosition> cursor_position, bool paused)
{
    if (cursor_position == m_cursor_position && paused == m_cursor_paused)
    {
        return;
    }

    // The flag is wider than the 1px line the shared strip helper pads for, so the ruler
    // invalidates its own flag-wide strips around the old and new positions.
    const auto repaint_flag_strip =
        [this](const std::optional<common::core::TimePosition>& position) {
            const std::optional<int> column =
                position.has_value() ? localCursorColumn(*position) : std::nullopt;
            if (!column.has_value())
            {
                return;
            }
            const int pad = static_cast<int>(g_cursor_flag_half_width) + 1;
            repaint(*column - pad, 0, (2 * pad) + 2, getHeight());
        };
    repaint_flag_strip(m_cursor_position);
    m_cursor_position = cursor_position;
    m_cursor_paused = paused;
    repaint_flag_strip(m_cursor_position);
}

// Stores the tempo map that supplies anchors and click snapping, plus the note value clicks
// quantize onto. Like setTimelineView, the rebuild and repaint are deferred to the setGridLines
// push owning-view callers issue after every grid change; rebuilding here would run against grid
// lines scanned for the previous grid and be discarded unpainted.
void TimelineRuler::setGrid(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum)
{
    if (m_tempo_map == tempo_map && m_placement_quantum == placement_quantum)
    {
        return;
    }

    m_tempo_map = tempo_map;
    m_reigns = tempo_map.reigns();
    m_placement_quantum = placement_quantum;
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

// Paints the chip rows with their dotted leaders and the ruler body's measure-number row and tick
// band.
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
    drawChipLeaders(g, m_section_row.leader_xs, g_section_row_y, editorTheme().section_chip);
    drawChipLeaders(g, m_tempo_row.leader_xs, g_tempo_row_y, editorTheme().tempo_chip);
    drawChipLeaders(g, m_signature_row.leader_xs, g_signature_row_y, editorTheme().signature_chip);

    drawBeatTicks(g);
    drawMeasureLabels(g);
    // A span chip is drawn on the lane's own ground, the rails' ground, so a derived chip ghosts
    // exactly as its rails do. Its selection ring and refusal glow are not drawn here: the chip and
    // its top rail read as one shape, so the track viewport traces the two together.
    for (const PlacedSpanChip& placed : m_span_chips)
    {
        common::ui::drawTabSpanChip(
            g, m_chip_font, m_shape_inks, placed.chip, editorTheme().waveform_row_background);
    }

    drawSectionChips(g);
    drawTempoChips(g);
    drawSignatureChips(g);

    drawCursor(g);
}

// Refreshes cached ruler geometry after a resize changes the visible ruler width.
void TimelineRuler::resized()
{
    refreshRulerGeometry();
}

// The selected chip's bounds where a row placed it at its marker; a pinned chip proves nothing and
// has none.
std::optional<juce::Rectangle<int>> TimelineRuler::selectedChipBounds() const
{
    // One selection editor-wide, so at most one row holds a selected chip.
    const auto in_row =
        [this](const ChipRow& row, const int row_y) -> std::optional<juce::Rectangle<int>> {
        const auto selected =
            std::ranges::find_if(row.chips, [](const RulerChip& chip) { return chip.selected; });
        if (selected == row.chips.end() || selected->pinned)
        {
            return std::nullopt;
        }
        return juce::Rectangle<int>{selected->label.x, row_y, selected->label.width, m_chip_height};
    };
    if (const auto chip = in_row(m_section_row, g_section_row_y); chip.has_value())
    {
        return chip;
    }
    if (const auto chip = in_row(m_tempo_row, g_tempo_row_y); chip.has_value())
    {
        return chip;
    }
    if (const auto chip = in_row(m_signature_row, g_signature_row_y); chip.has_value())
    {
        return chip;
    }
    const auto span = std::ranges::find_if(m_span_chips, [this](const PlacedSpanChip& placed) {
        return placed.index == m_selected_span;
    });
    if (span == m_span_chips.end() || span->pinned)
    {
        return std::nullopt;
    }
    return spanChipBox(span->index).transform([](const juce::Rectangle<float>& box) {
        return box.getSmallestIntegerContainer();
    });
}

// Rationale lives on the declaration in timeline_ruler.h.
std::optional<juce::Rectangle<float>> TimelineRuler::spanChipBox(const std::size_t span) const
{
    const auto placed = std::ranges::find_if(
        m_span_chips, [span](const PlacedSpanChip& chip) { return chip.index == span; });
    if (placed == m_span_chips.end())
    {
        return std::nullopt;
    }
    const juce::Rectangle<float> box = common::ui::tabSpanChipBounds(m_chip_font, placed->chip);
    if (box.isEmpty())
    {
        return std::nullopt;
    }
    return box;
}

// Adopts the core's published marker-plane availability.
void TimelineRuler::setMarkerEditsEnabled(const bool marker_edits_enabled)
{
    m_marker_edits_enabled = marker_edits_enabled;
}

// Converts ruler clicks into timeline seek positions using scrollable timeline coordinates, except
// on the chips, which are objects rather than positions: a chip click selects the marker it stands
// for and seeks nothing, which is exactly what lets the selection survive the cursor-coupled clear
// that a seek would otherwise trigger. While the marker plane is CLOSED there is no selection to
// make, so a chip column behaves like every other ruler column and seeks — swallowing the press
// would make the chips dead zones on a surface whose whole job is placing the cursor. A right-click
// on the tempo row opens the tempo menu, on the signature row the signature menu, and anywhere else
// the section menu, since the ruler is the sections' only surface. Each listener call returns at
// once: it republishes the rows the chip lives in, so nothing may be read through it afterwards. A
// press on a tempo chip also arms the chip's drag (mouseDrag), with everything it needs read off
// the map BEFORE the select republishes it.
void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    m_tempo_chip_drag.reset();
    if (!m_project_loaded || m_content_width <= 0)
    {
        return;
    }

    const juce::Point<int> point = event.getPosition();
    const RulerChip* const section_chip = chipAt(m_section_row.chips, g_section_row_y, point);
    // A LEFT press is never the popup gesture, whatever else is held, for the reason the chart lane
    // and the automation lanes both state: JUCE expands popupMenuClickModifier to
    // (rightButton | ctrl) on macOS, so isPopupMenu() alone is also true for Ctrl+left-click there,
    // and the section menu would swallow every Ctrl-held seek on that one platform. The menu
    // belongs to an actual right press on every platform.
    if (event.mods.isPopupMenu() && !event.mods.isLeftButtonDown())
    {
        if (chipRowContains(g_tempo_row_y, point))
        {
            showTempoContextMenu(chipAt(m_tempo_row.chips, g_tempo_row_y, point), event.position);
            return;
        }
        if (chipRowContains(g_signature_row_y, point))
        {
            showSignatureContextMenu(
                chipAt(m_signature_row.chips, g_signature_row_y, point), event.position);
            return;
        }
        showSectionContextMenu(section_chip, event.position);
        return;
    }
    if (!event.mods.isLeftButtonDown())
    {
        return;
    }
    if (m_listener != nullptr && m_marker_edits_enabled)
    {
        if (section_chip != nullptr)
        {
            m_listener->onSongSectionSelected(
                m_section_source[section_chip->source_index].position);
            return;
        }
        if (const RulerChip* const chip = chipAt(m_tempo_row.chips, g_tempo_row_y, point))
        {
            const common::core::BeatAnchor& anchor = m_tempo_map.anchors()[chip->source_index];
            const common::core::GridPosition position = anchor.position;
            if (const std::optional<common::core::TimePosition> pressed =
                    timeAtLocalX(event.position.x);
                pressed.has_value())
            {
                m_tempo_chip_drag = TempoChipDrag{
                    .position = position,
                    .origin_milliseconds = anchor.milliseconds,
                    .press_seconds = pressed->seconds,
                };
            }
            m_listener->onTempoAnchorSelected(position);
            return;
        }
        if (const RulerChip* const chip = chipAt(m_signature_row.chips, g_signature_row_y, point))
        {
            m_listener->onTimeSignatureSelected(m_reigns[chip->source_index].first_measure);
            return;
        }
        if (const PlacedSpanChip* const span = spanChipAt(event.position); span != nullptr)
        {
            m_listener->onSpanSelected(span->index);
            return;
        }
    }

    if (!m_cursor_placement_callback)
    {
        return;
    }
    const float timeline_x = static_cast<float>(m_view_x) + event.position.x;
    const std::optional<common::core::TimePosition> position = core::timelineCursorPlacementTime(
        m_tempo_map, m_placement_quantum, m_timeline_range, m_content_width, timeline_x);
    if (position.has_value())
    {
        m_cursor_placement_callback(*position);
    }
}

// A tempo-chip drag is the pointer form of Alt+arrows: every event past JUCE's drag threshold asks
// the listener to move the anchor to its press time plus the pointer's travel in content time,
// rounded once, never snapped — the anchor defines the grid, so no grid line can be its step. The
// core moves the model itself, so the chip, the grid and every chart surface follow live, and it
// alone decides what to refuse: a refused event leaves the anchor at its last legal time while the
// drag goes on. With the Note Lock on, the core holds each event instead and the chip and the
// grid follow its published preview, until the release commits.
void TimelineRuler::mouseDrag(const juce::MouseEvent& event)
{
    if (m_listener == nullptr || !event.mouseWasDraggedSinceMouseDown() ||
        !m_tempo_chip_drag.has_value())
    {
        return;
    }
    const TempoChipDrag drag = *m_tempo_chip_drag;
    const std::optional<common::core::TimePosition> pointer = timeAtLocalX(event.position.x);
    if (!pointer.has_value())
    {
        return;
    }
    m_listener->onTempoAnchorMoveRequested(
        drag.position,
        drag.origin_milliseconds +
            common::core::nearestAnchorMilliseconds(pointer->seconds - drag.press_seconds));
}

// The release disarms an armed chip press and reports the gesture's end, moved or not: whether a
// drag held anything to commit is the core's to know.
void TimelineRuler::mouseUp(const juce::MouseEvent& /*event*/)
{
    const bool armed = m_tempo_chip_drag.has_value();
    m_tempo_chip_drag.reset();
    if (armed && m_listener != nullptr)
    {
        m_listener->onTempoAnchorDragReleased();
    }
}

// Opens the prompt for a double-clicked section, tempo or signature chip — the selection's restate
// verb, the same shortcut the tone strip's regions carry. The first click of the double already
// selected the chip, so the core publishes the prompt for what is outlined (a section's rename, a
// tempo span's tempo, a time signature's meter) and the ruler decides nothing about it. Nothing
// opens while the marker plane is closed: a prompt is a stronger promise than the menu row it
// mirrors, and that row is disabled there.
void TimelineRuler::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!m_project_loaded || m_listener == nullptr || !m_marker_edits_enabled)
    {
        return;
    }
    const juce::Point<int> point = event.getPosition();
    if (chipAt(m_section_row.chips, g_section_row_y, point) != nullptr ||
        chipAt(m_tempo_row.chips, g_tempo_row_y, point) != nullptr ||
        chipAt(m_signature_row.chips, g_signature_row_y, point) != nullptr)
    {
        m_listener->onSelectionRestateRequested();
    }
}

// Stores the listener that receives the chips' intents.
void TimelineRuler::setListener(Listener& listener)
{
    m_listener = &listener;
}

// Rationale lives on the declaration in timeline_ruler.h.
bool TimelineRuler::chipRowContains(const int row_y, const juce::Point<int> point) const noexcept
{
    return point.y >= row_y && point.y < row_y + m_chip_height;
}

// Resolves a point to the chip under it in one row. Chips never overlap within a row (the row-wide
// placement guarantees it), so the first containing chip is the only one.
const TimelineRuler::RulerChip* TimelineRuler::chipAt(
    const std::vector<RulerChip>& chips, const int row_y, const juce::Point<int> point) const
{
    if (!chipRowContains(row_y, point))
    {
        return nullptr;
    }
    for (const RulerChip& chip : chips)
    {
        if (point.x >= chip.label.x && point.x < chip.label.x + chip.label.width)
        {
            return &chip;
        }
    }
    return nullptr;
}

// The click's own grid position at a quantum: the placement quantum where the insert lands on the
// slot itself, the tick where its verb reads the beat the click is IN.
std::optional<common::core::GridPosition> TimelineRuler::clickedGridPosition(
    const juce::Point<float> click, const common::core::Fraction quantum) const
{
    const std::optional<common::core::TimePosition> clicked = timeAtLocalX(click.x);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }
    return core::nearestTempoGridPosition(m_tempo_map, quantum, *clicked);
}

// The tempo and section inserts place where their chords would, at the published placement
// quantum, since an anchor or a section stands wherever a grid slot does.
std::optional<common::core::GridPosition> TimelineRuler::insertSlotAt(
    const juce::Point<float> click) const
{
    return clickedGridPosition(click, m_placement_quantum);
}

// Every row of a ruler menu is a marker verb — a section's rename included, unlike a tone
// DOCUMENT's — so every row is disabled while the marker plane is closed rather than left to click
// and do nothing, and enabled otherwise, the core refusing what it must. The flag is the core's own
// availability answer as published; the ruler adds no second gate of its own. Rows are numbered by
// position, so no row's id is ever stated beside its action.
juce::PopupMenu TimelineRuler::markerMenu(const std::vector<MarkerMenuRow>& rows) const
{
    juce::PopupMenu menu;
    int item_id = 0;
    for (const MarkerMenuRow& row : rows)
    {
        menu.addItem(++item_id, row.text, m_marker_edits_enabled, false);
    }
    return menu;
}

// The rows are built first, reading everything they need off the chip, and only then is the chip
// selected: the selection runs a full controller dispatch that republishes the row the chip lives
// in, so reading through the chip afterwards is a use-after-free that usually looks like it works,
// because the rebuilt vector reuses the same buffer. Selecting before the menu shows keeps the
// verbs and the outline agreeing about their subject.
void TimelineRuler::showMarkerMenu(
    std::vector<MarkerMenuRow> rows, const std::function<void()>& select_chip)
{
    if (m_listener == nullptr)
    {
        return;
    }
    juce::PopupMenu menu = markerMenu(rows);
    if (select_chip)
    {
        select_chip();
    }
    menu.showMenuAsync(
        // Force a cancel result if the ruler is deleted while the menu is open, so the callback
        // never reaches a dangling listener (JUCE reports result 0 for a deleted watch target).
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [owned_rows = std::move(rows)](const int result) {
            if (result > 0 && static_cast<std::size_t>(result) <= owned_rows.size())
            {
                owned_rows[static_cast<std::size_t>(result) - 1].action();
            }
        });
}

// Opens the ruler's section menu. The add verb is offered wherever the click resolves to a grid
// slot, because it is the one that needs discovering, and it inserts at the CLICK's slot: a pointer
// menu inserts where you pointed, and the marker-rule form is the keyboard chord's. The rest act on
// the chip the click landed on, which the menu selects; Rename is the selection's restate verb, as
// a double-click is, and the moves are Alt+arrows' one grid step.
void TimelineRuler::showSectionContextMenu(const RulerChip* chip, const juce::Point<float> click)
{
    std::vector<MarkerMenuRow> rows;
    if (const std::optional<common::core::GridPosition> insert_position = insertSlotAt(click);
        insert_position.has_value())
    {
        rows.push_back(
            {.text = "Insert Section Here", .action = [this, inserted = *insert_position] {
                 m_listener->onSongSectionInsertPromptRequested(inserted);
             }});
    }
    std::function<void()> select_chip;
    if (chip != nullptr)
    {
        rows.push_back(
            {.text = "Rename", .action = [this] { m_listener->onSelectionRestateRequested(); }});
        rows.push_back({.text = "Move a Grid Step Earlier", .action = [this] {
                            m_listener->onSelectionMoveRequested(false);
                        }});
        rows.push_back({.text = "Move a Grid Step Later", .action = [this] {
                            m_listener->onSelectionMoveRequested(true);
                        }});
        rows.push_back(
            {.text = "Delete", .action = [this] { m_listener->onSelectionDeleteRequested(); }});
        select_chip = [this, selected = m_section_source[chip->source_index].position] {
            m_listener->onSongSectionSelected(selected);
        };
    }
    showMarkerMenu(std::move(rows), select_chip);
}

// The tempo row's rows, the section menu's shape and policy: the insert at the clicked slot, plus
// Set Tempo and Delete on the chip the click landed on. Set Tempo is the selection's restate verb,
// Delete the Delete key's. Scale Tempo acts on the whole song, so it is offered wherever the row is
// clicked; the row runs the ScaleTempo command, so its text is that command's registered name.
std::vector<TimelineRuler::MarkerMenuRow> TimelineRuler::tempoRows(
    const juce::Point<float> click, const bool over_chip) const
{
    std::vector<MarkerMenuRow> rows;
    if (const std::optional<common::core::GridPosition> insert_position = insertSlotAt(click);
        insert_position.has_value())
    {
        rows.push_back(
            {.text = core::markerVerbLabel(core::MarkerVerb::AddTempoAnchor),
             .action = [this, inserted = *insert_position] {
                 m_listener->onTempoAnchorInsertRequested(inserted);
             }});
    }
    if (over_chip)
    {
        rows.push_back(
            {.text = core::markerVerbPromptRow(core::MarkerVerb::SetTempo),
             .action = [this] { m_listener->onSelectionRestateRequested(); }});
        rows.push_back(
            {.text = core::markerVerbLabel(core::MarkerVerb::DeleteTempoAnchor),
             .action = [this] { m_listener->onSelectionDeleteRequested(); }});
    }
    if (const EditorCommandSpec* const scale =
            findEditorCommandSpec(toJuceCommandId(EditorCommandId::ScaleTempo));
        scale != nullptr)
    {
        rows.push_back(
            {.text = scale->name, .action = [this] { m_listener->onScaleTempoPromptRequested(); }});
    }
    return rows;
}

// Builds the tempo row's menu with the rows that apply at the click, selecting nothing.
juce::PopupMenu TimelineRuler::tempoMenuAt(const juce::Point<float> click) const
{
    return markerMenu(
        tempoRows(click, chipAt(m_tempo_row.chips, g_tempo_row_y, click.roundToInt()) != nullptr));
}

// The tempo row's menu, selecting the chip the click landed on.
void TimelineRuler::showTempoContextMenu(const RulerChip* chip, const juce::Point<float> click)
{
    std::function<void()> select_chip;
    if (chip != nullptr)
    {
        select_chip = [this, selected = m_tempo_map.anchors()[chip->source_index].position] {
            m_listener->onTempoAnchorSelected(selected);
        };
    }
    showMarkerMenu(tempoRows(click, chip != nullptr), select_chip);
}

// A time signature starts a bar, and a bar is whole beats, so the click is read at the tick, never
// rounded into the next beat, and names the beat it is IN, as the meter chord reads the cursor;
// what a meter verb does there is the core's one rule, timeSignatureTargetAt. Only its insert is
// Add's.
std::optional<common::core::GridPosition> TimelineRuler::signatureInsertPositionAt(
    const juce::Point<float> click) const
{
    const std::optional<common::core::GridPosition> clicked =
        clickedGridPosition(click, common::core::g_tick_quantum_note_value);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }
    const core::TimeSignatureTarget target =
        core::timeSignatureTargetAt(m_tempo_map, common::core::beatStart(*clicked));
    if (const auto* const insert = std::get_if<core::InsertTimeSignatureTarget>(&target))
    {
        return insert->position;
    }
    return std::nullopt;
}

// The tempo menu's shape and policy over the signature row: Add where a time signature may start
// at the click, Set and Delete over a chip. Set is the selection's restate verb, Delete the Delete
// key's.
std::vector<TimelineRuler::MarkerMenuRow> TimelineRuler::signatureRows(
    const juce::Point<float> click, const bool over_chip) const
{
    std::vector<MarkerMenuRow> rows;
    if (const std::optional<common::core::GridPosition> insert_position =
            signatureInsertPositionAt(click);
        insert_position.has_value())
    {
        rows.push_back(
            {.text = core::markerVerbPromptRow(core::MarkerVerb::AddTimeSignature),
             .action = [this, inserted = *insert_position] {
                 m_listener->onTimeSignatureInsertPromptRequested(inserted);
             }});
    }
    if (over_chip)
    {
        rows.push_back(
            {.text = core::markerVerbPromptRow(core::MarkerVerb::SetTimeSignature),
             .action = [this] { m_listener->onSelectionRestateRequested(); }});
        rows.push_back(
            {.text = core::markerVerbLabel(core::MarkerVerb::DeleteTimeSignature),
             .action = [this] { m_listener->onSelectionDeleteRequested(); }});
    }
    return rows;
}

// Builds the signature row's menu with the rows that apply at the click, selecting nothing.
juce::PopupMenu TimelineRuler::signatureMenuAt(const juce::Point<float> click) const
{
    return markerMenu(signatureRows(
        click, chipAt(m_signature_row.chips, g_signature_row_y, click.roundToInt()) != nullptr));
}

// The signature row's menu, selecting the chip the click landed on.
void TimelineRuler::showSignatureContextMenu(const RulerChip* chip, const juce::Point<float> click)
{
    std::function<void()> select_chip;
    if (chip != nullptr)
    {
        select_chip = [this, selected = m_reigns[chip->source_index].first_measure] {
            m_listener->onTimeSignatureSelected(selected);
        };
    }
    showMarkerMenu(signatureRows(click, chip != nullptr), select_chip);
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

// Stores the spans. Only the span chips depend on them, so only those rebuild.
void TimelineRuler::setSpans(std::vector<RulerSpan> spans)
{
    if (m_spans == spans)
    {
        return;
    }

    m_spans = std::move(spans);
    refreshSpanChips();
    repaint();
}

// Stores the selected span. A selection places no chip, so a repaint is the whole change: it
// re-traces the ring the track viewport draws over this ruler's chip.
void TimelineRuler::setSelectedSpan(const std::optional<std::size_t> span)
{
    if (m_selected_span == span)
    {
        return;
    }

    m_selected_span = span;
    repaint();
}

// Stores the span chips' inks; the chips' placement does not depend on them, so a repaint is the
// whole change.
void TimelineRuler::setShapeInks(const common::ui::TabShapeInks shape_inks)
{
    m_shape_inks = shape_inks;
    repaint();
}

// Stores the selected tempo and signature chips. A selection claims its chip's room when the rows
// place, so a changed selection rebuilds them.
void TimelineRuler::setSelectedTempoMapChips(
    const std::optional<common::core::GridPosition> tempo_anchor,
    const std::optional<int> signature_measure)
{
    if (m_selected_tempo_anchor == tempo_anchor &&
        m_selected_signature_measure == signature_measure)
    {
        return;
    }

    m_selected_tempo_anchor = tempo_anchor;
    m_selected_signature_measure = signature_measure;
    refreshRulerGeometry();
    repaint();
}

// Rationale lives on the declaration in timeline_ruler.h.
void TimelineRuler::flashRefusal(std::vector<core::RulerChipKey> chips)
{
    if (startRefusalFlash(m_chip_refusal, std::move(chips)))
    {
        repaint();
    }
}

// The running refusal flash's level when it lights this chip, else nothing.
std::optional<float> TimelineRuler::chipRefusalLevel(const core::RulerChipKey& chip) const
{
    if (!m_chip_refusal.has_value() ||
        std::ranges::none_of(m_chip_refusal->subject, [&chip](const core::RulerChipKey& refused) {
            return refused == chip;
        }))
    {
        return std::nullopt;
    }
    return m_chip_refusal->pulse.level();
}

// Rationale lives on the declaration in timeline_ruler.h.
void TimelineRuler::flashRenumberedMeasures(const int from_measure)
{
    m_renumber_flash = PulsedFlash<int>{
        .subject = from_measure,
        .pulse = FlashPulse{g_renumber_flash_train},
    };
    repaint(measureRowBounds(getWidth()));
}

// Rationale lives on the declaration in timeline_ruler.h. The renumber flash lights only the
// measure-number row, so only that band repaints while it runs.
void TimelineRuler::stepFlashes()
{
    if (retireFinishedFlash(m_chip_refusal))
    {
        repaint();
    }
    if (retireFinishedFlash(m_renumber_flash))
    {
        repaint(measureRowBounds(getWidth()));
    }
}

// The ruler is pinned over scrolling content, so its local x reaches the content through the
// scroll offset.
std::optional<common::core::TimePosition> TimelineRuler::timeAtLocalX(
    const float local_x) const noexcept
{
    return core::timelinePositionForX(
        static_cast<float>(m_view_x) + local_x, m_timeline_range, m_content_width);
}

// Maps an absolute timeline second to this pinned ruler's local x coordinate.
std::optional<float> TimelineRuler::localXForSeconds(double seconds) const noexcept
{
    const std::optional<float> local_x = unboundedLocalXForSeconds(seconds);
    if (!local_x.has_value() || *local_x < 0.0f || *local_x >= static_cast<float>(getWidth()))
    {
        return std::nullopt;
    }

    return local_x;
}

// The local x of a time even off the ruler's bounds, for a mark whose anchor scrolled away while
// part of it still shows.
std::optional<float> TimelineRuler::unboundedLocalXForSeconds(double seconds) const noexcept
{
    const auto content_x = core::timelineXForPosition(
        common::core::TimePosition{seconds},
        m_timeline_range,
        m_content_width,
        core::TimelinePositionClamping::ExtendPastVisibleRange);
    if (!content_x.has_value())
    {
        return std::nullopt;
    }

    return *content_x - static_cast<float>(m_view_x);
}

// The cursor's column is chosen on the canvas, where its end clamp lives, and only then offset
// into the ruler, so the ruler's mark stands on the overlay's column even when the ruler runs past
// the canvas's right edge (over the vertical scrollbar).
std::optional<int> TimelineRuler::localCursorColumn(
    const common::core::TimePosition position) const noexcept
{
    // Read through has_value rather than optional::transform, which libc++ routes through the
    // throwing value() and so cannot be called under this function's noexcept.
    const std::optional<int> content_column =
        cursorColumnForTimelinePosition(position, m_timeline_range, m_content_width);
    if (!content_column.has_value())
    {
        return std::nullopt;
    }
    return *content_column - m_view_x;
}

// Each chip rides its span by the pinned-label rule every scrolling label obeys: at the span's
// start, or held at the ruler's left edge once it scrolls that far, never past where the span's
// rails stop. Starts ascend, so the walk stops at the first start past the edge.
void TimelineRuler::refreshSpanChips()
{
    m_span_chips.clear();
    const auto width = static_cast<float>(getWidth());
    const float max_height = spanChipMaxHeight();
    for (std::size_t index = 0; index < m_spans.size(); ++index)
    {
        const RulerSpan& span = m_spans[index];
        // Mapped exactly as the lane maps its rails, a span running past the timeline's end
        // included, so the chip and its rails start and stop on the same columns.
        const std::optional<float> start_x = unboundedLocalXForSeconds(span.start_seconds);
        const std::optional<float> end_x = unboundedLocalXForSeconds(span.end_seconds);
        if (!start_x.has_value() || !end_x.has_value())
        {
            continue;
        }
        if (*start_x >= width)
        {
            break;
        }
        const std::optional<float> next_start_x =
            index + 1 < m_spans.size() ? unboundedLocalXForSeconds(m_spans[index + 1].start_seconds)
                                       : std::nullopt;
        const float stop_x = common::ui::tabSpanStopX(*end_x, next_start_x);
        const std::optional<float> left = stickyLabelLeft(*start_x, stop_x, 0.0f);
        if (!left.has_value())
        {
            continue;
        }
        m_span_chips.push_back(
            PlacedSpanChip{
                .chip =
                    common::ui::TabSpanChip{
                        .base_left = {*left, static_cast<float>(getHeight())},
                        .stop_x = stop_x,
                        .max_height = max_height,
                        .arpeggio = span.arpeggio,
                        .held = span.held,
                        .name = std::nullopt,
                    },
                .index = index,
                .pinned = *start_x < *left,
            });
    }
}

// Chips advance with their spans' starts, and a chip only ever meets its successor at the parting,
// so a reverse walk finds the one drawn on top first.
const TimelineRuler::PlacedSpanChip* TimelineRuler::spanChipAt(const juce::Point<float> point) const
{
    for (const PlacedSpanChip& placed : std::views::reverse(m_span_chips))
    {
        if (common::ui::tabSpanChipBounds(m_chip_font, placed.chip).contains(point))
        {
            return &placed;
        }
    }
    return nullptr;
}

// Rebuilds the cached tick rectangles, the measure-number row, the chip rows and the span chips
// from the stored grid lines, timeline geometry, tempo map and span starts. Kept out of paint() so
// repaints driven only by cursor movement, whether vblank-driven playback or a single click, do
// not rebuild geometry or repeat the per-label GlyphArrangement text-width measurement on every
// frame; all of these only need to rerun when the state they depend on actually changes.
void TimelineRuler::refreshRulerGeometry()
{
    m_tick_rects.clear();
    m_measure_labels.clear();
    refreshSpanChips();

    const juce::Font font = rulerFont();

    // The pinned chip values and the pinned measure number need a musical frame of reference,
    // so they stay hidden until the first downbeat (the first anchor) reaches or passes the
    // visible left edge; the shared gate keeps every pinned row pinning in lockstep.
    const std::optional<common::core::TimePosition> view_left_time = timeAtLocalX(0.0f);
    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();
    const bool pinnable = view_left_time.has_value() && !anchors.empty() &&
                          view_left_time->seconds >= anchors.front().seconds();
    const std::optional<double> pinned_left_seconds =
        pinnable ? std::optional{view_left_time->seconds} : std::nullopt;

    refreshChipRows(pinned_left_seconds);

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
                MeasureLabel{
                    .label =
                        RulerLabel{.x = *label_x, .text = measure_text, .width = measure_width},
                    .measure = measure,
                });
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

// Places one chip row over a row's markers. Anchors draw no marker of their own: each chip's left
// edge sits on its grid column, which marks the position. The pinned value seeds the row at column
// zero so the shared placement policy positions and suppresses everything uniformly, but the pin
// yields to the row's first scrolling chip once that chip would collide, so the incoming value
// scrolls all the way to the left edge instead of vanishing behind the pin — unless the pin stands
// for the selected marker, which is never the chip that gives way.
template <typename SecondsAt, typename TextAt>
TimelineRuler::ChipRow TimelineRuler::placeChipRow(
    const std::size_t count, const SecondsAt& seconds_at, const TextAt& text_at,
    const int lead_width, const std::optional<double> pinned_left_seconds,
    const std::optional<std::size_t> selected) const
{
    ChipRow row;
    const auto anchor_x_at = [this, &seconds_at](const std::size_t index) -> std::optional<int> {
        const std::optional<float> local_x = localXForSeconds(seconds_at(index));
        if (!local_x.has_value())
        {
            return std::nullopt;
        }
        return static_cast<int>(std::round(*local_x));
    };
    const auto width_of = [this, lead_width](const juce::String& text) {
        return lead_width + m_chip_font.width(text) + g_label_width_pad;
    };
    const auto chip_of = [&selected](int label_x, juce::String text, int width, std::size_t index) {
        return RulerChip{
            .label = RulerLabel{.x = label_x, .text = std::move(text), .width = width},
            .source_index = index,
            .selected = selected == index,
        };
    };

    // Chip rows use a zero inset so the chip edge lands on the marker's grid column.
    RulerRowPlacement placement{getWidth(), 0};

    // The selected chip takes its room first, wherever its start is visible.
    std::optional<std::size_t> claimed;
    if (selected.has_value() && *selected < count)
    {
        const std::size_t index = *selected;
        if (const std::optional<int> anchor_x = anchor_x_at(index); anchor_x.has_value())
        {
            // An unnamed marker draws no chip, so it has no room to claim.
            juce::String text = text_at(index);
            const int width = width_of(text);
            const std::optional<int> label_x =
                text.isEmpty() ? std::nullopt : placement.claim(*anchor_x, width);
            if (label_x.has_value())
            {
                row.chips.push_back(chip_of(*label_x, std::move(text), width, index));
                claimed = index;
            }
        }
    }

    // Places one marker's chip at an anchor column when the row has room for it, and returns the
    // placed chip (or nullptr) so the pin below can mark its own.
    const auto place = [&](const int anchor_x, const std::size_t index) -> RulerChip* {
        if (!placement.accepts(anchor_x))
        {
            return nullptr;
        }
        juce::String text = text_at(index);
        if (text.isEmpty())
        {
            return nullptr;
        }
        const int width = width_of(text);
        const std::optional<int> label_x = placement.reserve(anchor_x, width);
        if (!label_x.has_value())
        {
            return nullptr;
        }
        row.chips.push_back(chip_of(*label_x, std::move(text), width, index));
        return &row.chips.back();
    };

    if (pinned_left_seconds.has_value())
    {
        // Starts ascend, so the active marker is the last one starting at or before the edge.
        std::optional<std::size_t> active;
        for (std::size_t index = 0; index < count && seconds_at(index) <= *pinned_left_seconds;
             ++index)
        {
            active = index;
        }
        if (active.has_value())
        {
            // The first marker starting at or right of the edge decides whether the pin yields.
            std::optional<int> first_anchor_x;
            for (std::size_t index = *active; index < count && !first_anchor_x.has_value(); ++index)
            {
                first_anchor_x = anchor_x_at(index);
            }
            if (selected == active ||
                !pinYieldsToIncomingLabel(width_of(text_at(*active)), first_anchor_x))
            {
                if (RulerChip* const chip = place(0, *active); chip != nullptr)
                {
                    // Pinned unless the marker itself stands exactly at the edge.
                    chip->pinned = anchor_x_at(*active) != std::optional<int>{0};
                }
            }
        }
    }

    for (std::size_t index = 0; index < count; ++index)
    {
        if (const std::optional<int> anchor_x = anchor_x_at(index))
        {
            row.leader_xs.push_back(*anchor_x);
            if (claimed != index)
            {
                place(*anchor_x, index);
            }
        }
    }
    return row;
}

// Rebuilds the three chip rows from the section list and the tempo map. A tempo marking ("♩=137")
// stores only its text, "=137": the quarter note is drawn ahead of it at its own size, so the row
// reserves the glyph's room as each chip's lead. The caller owns the pin gate, keeping the chip
// rows and the measure row pinning in lockstep.
void TimelineRuler::refreshChipRows(const std::optional<double> pinned_left_seconds)
{
    // The index of the first of a row's markers the predicate accepts: how each row finds the
    // marker its selection names.
    const auto first_index_where = [](const std::size_t count,
                                      const auto& accepts) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < count; ++index)
        {
            if (accepts(index))
            {
                return index;
            }
        }
        return std::nullopt;
    };

    m_section_row = placeChipRow(
        m_section_source.size(),
        [this](const std::size_t index) { return m_section_source[index].seconds; },
        [this](const std::size_t index) { return m_section_source[index].name; },
        0,
        pinned_left_seconds,
        first_index_where(m_section_source.size(), [this](const std::size_t index) {
            return m_section_source[index].selected;
        }));

    // The terminal anchor only ends the last span, so it gets no marking of its own; marking i is
    // span i, printed as the tempo prompt prefills it.
    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();
    const std::size_t marking_count = anchors.empty() ? 0 : anchors.size() - 1;
    m_tempo_row = placeChipRow(
        marking_count,
        [&anchors](const std::size_t index) { return anchors[index].seconds(); },
        [this](const std::size_t index) {
            return "=" + juce::String{core::tempoSpanBpmText(m_tempo_map, index)};
        },
        tempoGlyphLead(m_tempo_glyph),
        pinned_left_seconds,
        first_index_where(marking_count, [this, &anchors](const std::size_t index) {
            return m_selected_tempo_anchor == anchors[index].position;
        }));

    // Every reign start, the derived short bar's included, printing its meter.
    m_signature_row = placeChipRow(
        m_reigns.size(),
        [this](const std::size_t index) {
            return m_tempo_map.secondsAtWholeNote(m_reigns[index].place);
        },
        [this](const std::size_t index) {
            return juce::String{common::core::timeSignatureText(m_reigns[index].meter)};
        },
        0,
        pinned_left_seconds,
        first_index_where(m_reigns.size(), [this](const std::size_t index) {
            return m_selected_signature_measure == m_reigns[index].first_measure;
        }));
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
        for (int y = row_y + m_chip_height; y < g_ruler_body_top - 1; y += 2)
        {
            dots.addWithoutMerging(juce::Rectangle<int>{x, y, 1, 1}.toFloat());
        }
    }
    g.setColour(color);
    g.fillRectList(dots);
}

// Draws the cached measure-number row in the font its widths were measured with. The renumber
// flash's blend of the plain ink toward the accent is worked out once per paint, so a running
// flash costs one color switch per number and allocates nothing.
void TimelineRuler::drawMeasureLabels(juce::Graphics& g)
{
    const juce::Colour plain = g_timeline_ruler_text_color.withAlpha(0.82f);
    // Read once ahead of the loop, so the loop never reaches into the optional flash; with none
    // running, no measure is lit.
    int lit_from = std::numeric_limits<int>::max();
    juce::Colour lit = plain;
    if (m_renumber_flash.has_value())
    {
        lit_from = m_renumber_flash->subject;
        lit = plain.interpolatedWith(editorTheme().accent, m_renumber_flash->pulse.level());
    }
    g.setFont(rulerFont());
    for (const MeasureLabel& measure_label : m_measure_labels)
    {
        const RulerLabel& label = measure_label.label;
        g.setColour(measure_label.measure >= lit_from ? lit : plain);
        g.drawText(
            label.text,
            label.x,
            g_measure_row_y,
            label.width,
            g_label_row_height,
            juce::Justification::centredLeft);
    }
}

// A placed chip's box on its row: the label's span at the shared chip height.
juce::Rectangle<float> TimelineRuler::chipBounds(const RulerChip& chip, const int row_y) const
{
    return juce::Rectangle<float>{
        static_cast<float>(chip.label.x),
        static_cast<float>(row_y),
        static_cast<float>(chip.label.width),
        static_cast<float>(m_chip_height)
    };
}

// Fills one chip and leaves the label color set, so the label draws into the same bounds next.
void TimelineRuler::drawChipFill(
    juce::Graphics& g, const juce::Rectangle<float> bounds, const juce::Colour fill)
{
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, g_chip_corner_radius);
    g.setColour(common::ui::g_chip_label_color);
}

// The selected chip's ring and the refused chip's glow trace one outline, the boxed-mark ring every
// overlay wears (boxRingOutline): lying wholly outside the chip, it covers none of the fill or the
// label, however small the chip. The ring takes the theme accent, the token the tab lane's
// selection rings wear, so a selection reads the same on the ruler as on the notation (the tone
// strip outlines its selected region in white instead, its accent already marking the active tone).
void TimelineRuler::drawChipOverlays(
    juce::Graphics& g, const RulerChip& chip, const juce::Rectangle<float> bounds,
    const std::optional<float> refusal_level)
{
    if (!chip.selected && !refusal_level.has_value())
    {
        return;
    }
    const MarkOutline outline = boxRingOutline(bounds, g_chip_corner_radius);
    if (chip.selected)
    {
        strokeMarkRing(g, outline, editorTheme().accent);
    }
    if (refusal_level.has_value())
    {
        paintRefusalGlow(g, outline.path, outline.extent, *refusal_level);
    }
}

// Draws the section chip row — rounded fill, centered label, the selection ring — each chip glowing
// while a refusal of its section runs.
void TimelineRuler::drawSectionChips(juce::Graphics& g) const
{
    for (const RulerChip& chip : m_section_row.chips)
    {
        const juce::Rectangle<float> bounds = chipBounds(chip, g_section_row_y);
        drawChipFill(g, bounds, editorTheme().section_chip);
        m_chip_font.draw(g, chip.label.text, bounds);
        drawChipOverlays(
            g,
            chip,
            bounds,
            chipRefusalLevel(
                core::RulerChipKey{
                    .row = core::RulerChipRow::Section,
                    .start = m_section_source[chip.source_index].position,
                }));
    }
}

// Draws the tempo chip row, each chip glowing while a refusal of its anchor runs. The quarter note
// stands half the label padding in from the chip's left edge and a clear row down from its top;
// the digits centre in the room after its lead, which leaves them the same padding on the right.
void TimelineRuler::drawTempoChips(juce::Graphics& g)
{
    const auto glyph_lead = static_cast<float>(tempoGlyphLead(m_tempo_glyph));
    const float glyph_inset = static_cast<float>(g_label_width_pad) / 2.0f;
    for (const RulerChip& chip : m_tempo_row.chips)
    {
        const juce::Rectangle<float> bounds = chipBounds(chip, g_tempo_row_y);
        drawChipFill(g, bounds, editorTheme().tempo_chip);
        drawChipOverlays(
            g,
            chip,
            bounds,
            chipRefusalLevel(
                core::RulerChipKey{
                    .row = core::RulerChipRow::Tempo,
                    .start = m_tempo_map.anchors()[chip.source_index].position,
                }));
        g.setColour(common::ui::g_chip_label_color);
        g.fillPath(
            m_tempo_glyph,
            juce::AffineTransform::translation(
                bounds.getX() + glyph_inset, bounds.getY() + common::ui::g_chip_ink_clearance));
        m_chip_font.draw(g, chip.label.text, bounds.withTrimmedLeft(glyph_lead));
    }
}

// Draws the signature chip row. A DERIVED reign's chip draws as a ghost, an authored one solid, so
// the charter sees which meters the song stores: the chip's fill and label are flattened and
// composited at the tab lane's ghost weight, exactly as a derived fret-hand chip is. The selection
// ring and the refusal glow draw after the ghost closes, so both read as solid as on any chip.
void TimelineRuler::drawSignatureChips(juce::Graphics& g)
{
    for (const RulerChip& chip : m_signature_row.chips)
    {
        const common::core::MeterReign& reign = m_reigns[chip.source_index];
        const juce::Rectangle<float> bounds = chipBounds(chip, g_signature_row_y);
        {
            std::optional<common::ui::ScopedTransparencyLayer> ghost;
            if (reign.derived)
            {
                ghost.emplace(g, bounds.getSmallestIntegerContainer(), common::ui::g_ghost_opacity);
            }
            drawChipFill(g, bounds, editorTheme().signature_chip);
            m_chip_font.draw(g, chip.label.text, bounds);
        }
        drawChipOverlays(
            g,
            chip,
            bounds,
            chipRefusalLevel(
                core::RulerChipKey{
                    .row = core::RulerChipRow::TimeSignature,
                    .start = common::core::measureDownbeat(reign.first_measure),
                }));
    }
}

// Draws the play-from-here cursor through the ruler body for vertical alignment, topped by the
// flag triangle. The cursor starts at the body's top edge instead of y 0 so it does not cut
// through the chip rows above. The line takes the paused or playback color so it reads as one
// continuous indicator with the cursor in the content below; the flag stays playback white in
// both states so the play-from-here mark never loses visibility.
void TimelineRuler::drawCursor(juce::Graphics& g)
{
    // Mapped here, not at the push: the column has to follow the current scroll offset.
    const std::optional<int> cursor_column =
        m_cursor_position.has_value() ? localCursorColumn(*m_cursor_position) : std::nullopt;
    const juce::Colour line_color =
        m_cursor_paused ? editorTheme().paused_cursor : editorTheme().playback_cursor;
    const std::optional<int> column =
        drawTimelineCursor(g, *this, cursor_column, g_ruler_body_top, line_color);
    if (!column.has_value())
    {
        return;
    }

    // The flag's tip centers on the pixel column the line occupies.
    const float tip_x = static_cast<float>(*column) + 0.5f;
    juce::Path flag;
    flag.startNewSubPath(tip_x - g_cursor_flag_half_width, static_cast<float>(g_ruler_body_top));
    flag.lineTo(tip_x + g_cursor_flag_half_width, static_cast<float>(g_ruler_body_top));
    flag.lineTo(tip_x, static_cast<float>(g_ruler_body_top) + g_cursor_flag_height);
    flag.closeSubPath();
    g.setColour(editorTheme().playback_cursor);
    g.fillPath(flag);
}

} // namespace rock_hero::editor::ui
