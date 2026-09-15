#include "timeline_ruler.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "timeline/sticky_label.h"
#include "timeline/timeline_cursor.h"

#include <cmath>
#include <cstddef>
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
// Chip height shared by every ruler chip row so they read as one family.
constexpr int g_chip_height{11};
// The play-from-here flag: a filled triangle at the ruler body's top pointing down the cursor
// line, sized to read at a glance — the 1px alignment line alone was too easy to miss.
constexpr float g_cursor_flag_half_width{5.0f};
constexpr float g_cursor_flag_height{7.0f};

// Shared ruler text face. Cached label widths are measured with the same face they are drawn
// with, so measurement and drawing must both go through these helpers.
[[nodiscard]] juce::Font rulerFont()
{
    return juce::Font{juce::FontOptions{12.0f}};
}

// Chip text face, sized so a chip row reads as a label band rather than as content.
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

// The quarter-note glyph that opens every tempo marking: U+2669, supplied as escaped UTF-8 so
// source-file encoding cannot corrupt it; text shaping falls back to a symbol font when the UI
// font lacks the glyph.
[[nodiscard]] juce::String quarterNoteGlyph()
{
    return juce::String::fromUTF8("\xE2\x99\xA9");
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
            const std::optional<float> x =
                position.has_value() ? localXForSeconds(position->seconds) : std::nullopt;
            if (!x.has_value())
            {
                return;
            }
            const int pad = static_cast<int>(g_cursor_flag_half_width) + 1;
            repaint(static_cast<int>(std::floor(*x)) - pad, 0, 2 * pad + 2, getHeight());
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

    g.setColour(g_timeline_ruler_text_color.withAlpha(0.82f));
    drawLabelRow(g, m_measure_labels, rulerFont(), g_measure_row_y, g_label_row_height);

    drawChipRow(g, m_section_row.chips, editorTheme().section_chip, g_section_row_y);
    drawTempoChips(g);
    drawChipRow(g, m_signature_row.chips, editorTheme().signature_chip, g_signature_row_y);

    drawCursor(g);
}

// Refreshes cached ruler geometry after a resize changes the visible ruler width.
void TimelineRuler::resized()
{
    refreshRulerGeometry();
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
// anywhere opens the section menu, since the ruler is the sections' only surface and carries no
// competing menu. Each listener call returns at once: it republishes the rows the chip lives in, so
// nothing may be read through it afterwards.
void TimelineRuler::mouseDown(const juce::MouseEvent& event)
{
    if (!m_project_loaded || m_content_width <= 0)
    {
        return;
    }

    const juce::Point<int> point = event.getPosition();
    const RulerChip* const section_chip = chipAt(m_section_row.chips, g_section_row_y, point);
    if (event.mods.isPopupMenu())
    {
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
            m_listener->onTempoAnchorSelected(
                common::core::GridPosition{
                    .measure = anchor.measure, .beat = anchor.beat, .offset = {}
                });
            return;
        }
        if (const RulerChip* const chip = chipAt(m_signature_row.chips, g_signature_row_y, point))
        {
            m_listener->onTimeSignatureSelected(
                m_tempo_map.timeSignatures()[chip->source_index].measure);
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

// Opens the rename prompt for a double-clicked chip, the same shortcut the tone strip's regions
// carry. The first click of the double already selected it, so the prompt names what is outlined.
// Nothing opens while the marker plane is closed: a prompt is a stronger promise than the menu row
// it mirrors, and that row is disabled there.
void TimelineRuler::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!m_project_loaded || m_listener == nullptr || !m_marker_edits_enabled)
    {
        return;
    }
    if (const RulerChip* const chip =
            chipAt(m_section_row.chips, g_section_row_y, event.getPosition());
        chip != nullptr)
    {
        // Copied out first, for the reason showSectionContextMenu states: the intent republishes
        // the section list and rebuilds the row this chip lives in.
        const common::core::GridPosition position = m_section_source[chip->source_index].position;
        const juce::String name = m_section_source[chip->source_index].name;
        m_listener->onSongSectionRenamePromptRequested(position, name);
    }
}

// Stores the listener that receives the chips' intents.
void TimelineRuler::setListener(Listener& listener)
{
    m_listener = &listener;
}

// Resolves a point to the chip under it in one row. Chips never overlap within a row (the row-wide
// placement guarantees it), so the first containing chip is the only one.
const TimelineRuler::RulerChip* TimelineRuler::chipAt(
    const std::vector<RulerChip>& chips, const int row_y, const juce::Point<int> point)
{
    if (point.y < row_y || point.y >= row_y + g_chip_height)
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

// Opens the ruler's section menu. The add verb is offered wherever the click resolves to a grid
// position, because it is the one that needs discovering, and it inserts at the CLICK's measure:
// a pointer menu inserts where you pointed, and the marker-rule form is the keyboard chord's. The
// rest act on the chip the click landed on, which the menu selects first so the verbs and the
// outline agree about their subject.
void TimelineRuler::showSectionContextMenu(const RulerChip* chip, juce::Point<float> click)
{
    if (m_listener == nullptr)
    {
        return;
    }

    // The click's grid position, through the same placement seam a left click seeks by.
    const float timeline_x = static_cast<float>(m_view_x) + click.x;
    const std::optional<common::core::TimePosition> clicked = core::timelineCursorPlacementTime(
        m_tempo_map, m_placement_quantum, m_timeline_range, m_content_width, timeline_x);
    std::optional<common::core::GridPosition> insert_position;
    if (clicked.has_value())
    {
        insert_position =
            core::nearestTempoGridPosition(m_tempo_map, m_placement_quantum, *clicked);
    }

    // Copy the chip's section out BEFORE selecting it. The selection runs a full controller
    // dispatch that republishes the section list, and setSectionLabels rebuilds the chip row the
    // pointer points into — so reading through `chip` after this call is a use-after-free that
    // usually looks like it works, because the rebuilt vector reuses the same buffer.
    const bool over_chip = chip != nullptr;
    common::core::GridPosition position{};
    juce::String name;
    if (over_chip)
    {
        position = m_section_source[chip->source_index].position;
        name = m_section_source[chip->source_index].name;
        m_listener->onSongSectionSelected(position);
    }

    // Every row here is a marker verb — a section's rename included, unlike a tone DOCUMENT's — so
    // all five are disabled while the marker plane is closed rather than left to click and do
    // nothing. The flag is the core's own availability answer as published; the ruler adds no
    // second gate of its own.
    juce::PopupMenu menu;
    if (insert_position.has_value())
    {
        menu.addItem(1, "Insert Section Here", m_marker_edits_enabled, false);
    }
    if (over_chip)
    {
        menu.addItem(2, "Rename", m_marker_edits_enabled, false);
        menu.addItem(3, "Move a Measure Earlier", m_marker_edits_enabled, false);
        menu.addItem(4, "Move a Measure Later", m_marker_edits_enabled, false);
        menu.addItem(5, "Delete", m_marker_edits_enabled, false);
    }
    menu.showMenuAsync(
        // Force a cancel result if the ruler is deleted while the menu is open, so the callback
        // never reaches a dangling listener (JUCE reports result 0 for a deleted watch target).
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this, position, owned_name = std::move(name), insert_position](int result) {
            if (result == 1 && insert_position.has_value())
            {
                m_listener->onSongSectionInsertPromptRequested(*insert_position);
            }
            else if (result == 2)
            {
                m_listener->onSongSectionRenamePromptRequested(position, owned_name);
            }
            else if (result == 3 || result == 4)
            {
                m_listener->onSongSectionMoveRequested(result == 4);
            }
            else if (result == 5)
            {
                m_listener->onSongSectionDeleteRequested();
            }
        });
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

// Places one chip row over a row's markers. Anchors draw no marker of their own: each chip's left
// edge sits on its grid column, which marks the position. The pinned value seeds the row at column
// zero so the shared placement policy positions and suppresses everything uniformly, but the pin
// yields to the row's first scrolling chip once that chip would collide, so the incoming value
// scrolls all the way to the left edge instead of vanishing behind the pin — unless the pin stands
// for the selected marker, which is never the chip that gives way.
template <typename SecondsAt, typename TextAt>
TimelineRuler::ChipRow TimelineRuler::placeChipRow(
    const std::size_t count, const SecondsAt& seconds_at, const TextAt& text_at,
    const int extra_width, const std::optional<double> pinned_left_seconds,
    const std::optional<std::size_t> selected) const
{
    ChipRow row;
    const juce::Font font = chipFont();
    const auto anchor_x_at = [this, &seconds_at](const std::size_t index) -> std::optional<int> {
        const std::optional<float> local_x = localXForSeconds(seconds_at(index));
        if (!local_x.has_value())
        {
            return std::nullopt;
        }
        return static_cast<int>(std::round(*local_x));
    };
    const auto width_of = [&font, extra_width](const juce::String& text) {
        return extra_width + textWidth(font, text) + g_label_width_pad;
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

    const auto place = [&](const int anchor_x, const std::size_t index) {
        if (!placement.accepts(anchor_x))
        {
            return;
        }
        juce::String text = text_at(index);
        if (text.isEmpty())
        {
            return;
        }
        const int width = width_of(text);
        if (const std::optional<int> label_x = placement.reserve(anchor_x, width))
        {
            row.chips.push_back(chip_of(*label_x, std::move(text), width, index));
        }
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
                place(0, *active);
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

// Rebuilds the three chip rows from the section list and the tempo map. Tempo markings
// ("♩=120.00") split into an enlarged quarter-note glyph and chip-size digits, drawn in their own
// fonts inside one chip because one text draw cannot mix fonts; only the glyph is enlarged, so
// the equals sign rides with the digits. The caller owns the pin gate, keeping the chip rows and
// the measure row pinning in lockstep.
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

    // The terminal anchor only ends the last span, so it gets no marking of its own.
    const std::vector<common::core::BeatAnchor>& anchors = m_tempo_map.anchors();
    const std::size_t marking_count = anchors.empty() ? 0 : anchors.size() - 1;
    m_tempo_glyph_width = textWidth(noteGlyphFont(), quarterNoteGlyph()) + 1;
    m_tempo_row = placeChipRow(
        marking_count,
        [&anchors](const std::size_t index) { return anchors[index].seconds; },
        [this, &anchors](const std::size_t index) {
            return "=" +
                   juce::String{m_tempo_map.quarterNoteBpmAtSeconds(anchors[index].seconds), 2};
        },
        m_tempo_glyph_width,
        pinned_left_seconds,
        first_index_where(marking_count, [this, &anchors](const std::size_t index) {
            return m_selected_tempo_anchor == common::core::GridPosition{
                                                  .measure = anchors[index].measure,
                                                  .beat = anchors[index].beat,
                                                  .offset = {},
                                              };
        }));

    const std::vector<common::core::TimeSignatureChange>& changes = m_tempo_map.timeSignatures();
    m_signature_row = placeChipRow(
        changes.size(),
        [this, &changes](const std::size_t index) {
            return m_tempo_map.secondsAtBeat(changes[index].measure, 1);
        },
        [&changes](const std::size_t index) {
            return juce::String{changes[index].numerator} + "/" +
                   juce::String{changes[index].denominator};
        },
        0,
        pinned_left_seconds,
        first_index_where(changes.size(), [this, &changes](const std::size_t index) {
            return m_selected_signature_measure == changes[index].measure;
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

// Draws one chip's rounded fill, then outlines it in the theme accent when it stands for the
// selected marker — the same token the tone strip's selected region uses, so a selection reads the
// same on every surface. A 1px stroke on the chip's own bounds, because the chip is 11px tall and
// a heavier ring would swallow the fill it sits on.
juce::Rectangle<float> TimelineRuler::drawChipFrame(
    juce::Graphics& g, const RulerChip& chip, const int row_y, const juce::Colour fill)
{
    const juce::Rectangle<float> bounds{
        static_cast<float>(chip.label.x),
        static_cast<float>(row_y),
        static_cast<float>(chip.label.width),
        static_cast<float>(g_chip_height)
    };
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 2.0f);
    if (chip.selected)
    {
        g.setColour(editorTheme().accent);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }
    g.setColour(juce::Colours::white);
    return bounds;
}

// Draws one cached chip row — rounded fill, white centered text. The chips must have been measured
// with the chip font.
void TimelineRuler::drawChipRow(
    juce::Graphics& g, const std::vector<RulerChip>& chips, const juce::Colour fill,
    const int row_y)
{
    g.setFont(chipFont());
    for (const RulerChip& chip : chips)
    {
        g.drawText(
            chip.label.text, drawChipFrame(g, chip, row_y, fill), juce::Justification::centred);
    }
}

// Draws the tempo chip row: the glyph and digits in their own fonts because one text draw cannot
// mix fonts. The enlarged glyph centers on the chip so its extra size hangs evenly.
void TimelineRuler::drawTempoChips(juce::Graphics& g)
{
    const juce::Font digits_font = chipFont();
    const juce::Font glyph_font = noteGlyphFont();
    const juce::String glyph = quarterNoteGlyph();
    const auto glyph_width = static_cast<float>(m_tempo_glyph_width);
    for (const RulerChip& chip : m_tempo_row.chips)
    {
        const juce::Rectangle<float> bounds =
            drawChipFrame(g, chip, g_tempo_row_y, editorTheme().tempo_chip);
        g.setFont(glyph_font);
        g.drawText(
            glyph,
            bounds.withWidth(glyph_width).translated(3.0f, 0.0f),
            juce::Justification::centredLeft);
        g.setFont(digits_font);
        g.drawText(
            chip.label.text,
            bounds.withTrimmedLeft(glyph_width + 3.0f),
            juce::Justification::centredLeft);
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
    const std::optional<float> cursor_x =
        m_cursor_position.has_value() ? localXForSeconds(m_cursor_position->seconds) : std::nullopt;
    const juce::Colour line_color =
        m_cursor_paused ? editorTheme().paused_cursor : editorTheme().playback_cursor;
    const std::optional<int> column =
        drawTimelineCursor(g, *this, cursor_x, g_ruler_body_top, line_color);
    if (!column.has_value())
    {
        return;
    }

    // The flag's tip centers on the exact pixel column the line occupies, so the two can never
    // land a pixel apart from independent rounding.
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
