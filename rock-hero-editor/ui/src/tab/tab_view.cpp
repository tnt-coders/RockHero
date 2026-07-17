#include "tab/tab_view.h"

#include "shared/editor_theme.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

// The notation rasterizer lives in the shared paint core (rock-hero-common/ui tab/), one
// authority for the editor lane and the game tab strips; these free functions stay on the
// editor surface as thin delegates so editor widgets and tests keep their existing seam.

// The chart's string count floors the lane count so a user minimum can only add empty lanes.
int tabDisplayedStringCount(int chart_string_count, int minimum_displayed_strings) noexcept
{
    return common::ui::tabDisplayedStringCount(chart_string_count, minimum_displayed_strings);
}

juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return common::ui::tabStringColor(displayed_string, displayed_string_count);
}

juce::Colour tabShapeMarkColor(bool arpeggio)
{
    return common::ui::tabShapeMarkColor(arpeggio);
}

float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    return common::ui::tabLaneCenterY(displayed_string, displayed_string_count, bounds);
}

std::pair<std::size_t, std::size_t> tabVisibleNoteRange(
    const std::vector<common::core::TabNoteView>& notes,
    const std::vector<double>& prefix_max_end_seconds, double span_start_seconds,
    double span_end_seconds) noexcept
{
    return common::ui::tabVisibleNoteRange(
        notes, prefix_max_end_seconds, span_start_seconds, span_end_seconds);
}

// Interception stays enabled: hitTest() claims the lane only while a chart is displayed, so a
// chart-less lane stays transparent to the cursor overlay's click-to-seek handling.
TabView::TabView(const common::core::TempoMap& tempo_map)
    : m_tempo_map(tempo_map)
{}

void TabView::setGridNoteValue(common::core::Fraction grid_note_value) noexcept
{
    m_grid_note_value = grid_note_value;
}

// Recomputes the Alt ghost from a hover/drag event: the snapped x comes from the same
// musicalGridPositionForX seam every placement gesture uses (Ctrl bypasses to the fine grid),
// so the ghost sits exactly where the committed insert will land. Also keeps the copy cursor
// in sync — JUCE synthesizes a mouse-move whenever modifiers change, so pressing or releasing
// Alt updates both without pointer motion.
void TabView::updateGhost(const juce::MouseEvent& event)
{
    const bool insertable = m_tab != nullptr && m_tab->string_count > 0 &&
                            m_visible_timeline.duration().seconds > 0.0 &&
                            !getLocalBounds().isEmpty();
    const bool wants_ghost = insertable && event.mods.isAltDown();
    setMouseCursor(
        wants_ghost ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor);

    std::optional<GhostNote> next_ghost;
    if (wants_ghost)
    {
        const std::optional<common::core::GridPosition> position = musicalGridPositionForX(
            m_tempo_map,
            m_grid_note_value,
            m_visible_timeline,
            getWidth(),
            event.position.x,
            event.mods);
        if (position.has_value())
        {
            const double seconds =
                m_tempo_map.secondsAtNote(position->measure, position->beat, position->offset);
            const double duration = m_visible_timeline.duration().seconds;
            const int displayed_count =
                tabDisplayedStringCount(m_tab->string_count, m_minimum_displayed_strings);
            const float lane_height =
                static_cast<float>(getHeight()) / static_cast<float>(displayed_count);
            const int lane_index = std::clamp(
                static_cast<int>(event.position.y / lane_height), 0, displayed_count - 1);
            const int extra_lanes = displayed_count - m_tab->string_count;
            next_ghost = GhostNote{
                .x = static_cast<float>(
                    (seconds - m_visible_timeline.start.seconds) / duration *
                    static_cast<double>(getWidth())),
                .string =
                    std::clamp(displayed_count - lane_index - extra_lanes, 1, m_tab->string_count),
            };
        }
    }

    const bool unchanged =
        (!next_ghost.has_value() && !m_ghost.has_value()) ||
        (next_ghost.has_value() && m_ghost.has_value() &&
         std::is_eq(next_ghost->x <=> m_ghost->x) && next_ghost->string == m_ghost->string);
    if (unchanged)
    {
        return;
    }

    // Repainting only the strips the ghost moved between keeps hover cheap on wide zoomed
    // content.
    const std::optional<float> previous_x =
        m_ghost.has_value() ? std::optional{m_ghost->x} : std::nullopt;
    const std::optional<float> next_x =
        next_ghost.has_value() ? std::optional{next_ghost->x} : std::nullopt;
    m_ghost = next_ghost;
    repaintGhostStrip(previous_x, next_x);
}

// The shared repaintCursorStrip pads for a one-pixel cursor line; the ghost ring reaches half a
// head-size on either side of its onset x, so its strip pads by the style ceiling on that reach
// instead. Anything narrower clips the incoming ghost's draw and leaves stale fragments of the
// outgoing one behind.
void TabView::repaintGhostStrip(std::optional<float> previous_x, std::optional<float> next_x)
{
    if ((!previous_x.has_value() && !next_x.has_value()) || getWidth() <= 0 || getHeight() <= 0)
    {
        return;
    }

    float left_x = 0.0f;
    float right_x = 0.0f;
    if (previous_x.has_value() && next_x.has_value())
    {
        left_x = std::min(*previous_x, *next_x);
        right_x = std::max(*previous_x, *next_x);
    }
    else
    {
        const float ghost_x = previous_x.has_value() ? *previous_x : *next_x;
        left_x = ghost_x;
        right_x = ghost_x;
    }
    constexpr float antialias_padding = 3.0f;
    const float half_extent =
        (common::ui::TabLaneStyle{}.max_note_height + 1.0f) / 2.0f + antialias_padding;
    const int left = std::max(0, static_cast<int>(std::floor(left_x - half_extent)));
    const int right = std::min(getWidth(), static_cast<int>(std::ceil(right_x + half_extent)) + 1);
    repaint(left, 0, right - left, getHeight());
}

void TabView::mouseMove(const juce::MouseEvent& event)
{
    updateGhost(event);
}

void TabView::mouseExit(const juce::MouseEvent& /*event*/)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
    if (m_ghost.has_value())
    {
        const std::optional<float> previous_x{m_ghost->x};
        m_ghost.reset();
        repaintGhostStrip(previous_x, std::nullopt);
    }
}

void TabView::setPointerEventCallback(PointerEventCallback on_pointer_event)
{
    m_on_pointer_event = std::move(on_pointer_event);
}

// Applies the chart-editing overlay state; skipped repaints keep unrelated pushes cheap.
void TabView::setEditState(core::ChartEditViewState edit)
{
    if (edit == m_edit)
    {
        return;
    }

    m_edit = std::move(edit);
    refreshFocusBlink();
    repaint();
}

// The underline blinks like a text caret — the focused numeral is where typed digits land, so
// the affordance borrows the "enterable" idiom (user choice 2026-07-17). The timer runs only
// while an underline is showing; everything else in the lane stays repaint-on-push.
void TabView::refreshFocusBlink()
{
    const bool wants_blink =
        m_tab != nullptr && m_edit.focused_note.has_value() && m_edit.selected_notes.size() > 1;
    m_focus_underline_visible = true;
    if (wants_blink)
    {
        startTimer(530);
    }
    else
    {
        stopTimer();
        m_focus_underline_bounds = {};
    }
}

void TabView::timerCallback()
{
    m_focus_underline_visible = !m_focus_underline_visible;
    if (!m_focus_underline_bounds.isEmpty())
    {
        repaint(m_focus_underline_bounds.expanded(2));
    }
}

// With a chart displayed the lane claims its whole band — the controller still turns empty
// clicks into seeks, so seeking through the lane keeps working. Without a chart the lane is
// pointer-transparent.
bool TabView::wantsPointerAt(juce::Point<int> local_point) const
{
    return m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->string_count > 0 &&
           getLocalBounds().contains(local_point) && m_visible_timeline.duration().seconds > 0.0 &&
           !getLocalBounds().isEmpty();
}

bool TabView::hitTest(int x, int y)
{
    return wantsPointerAt({x, y});
}

// Builds the chart pointer event carrying the exact geometry the notation painted with, so the
// controller's hit resolution and the pixels on screen can never disagree.
core::ChartPointerEvent TabView::makePointerEvent(const juce::MouseEvent& event) const
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const int displayed_count =
        tabDisplayedStringCount(m_tab->string_count, m_minimum_displayed_strings);
    return core::ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()),
            m_visible_timeline,
            displayed_count,
            m_tab->string_count),
        .x = event.position.x,
        .y = event.position.y,
        .modifiers = core::ChartPointerModifiers{
            .ctrl = event.mods.isCtrlDown(),
            .shift = event.mods.isShiftDown(),
            .alt = event.mods.isAltDown(),
        },
    };
}

void TabView::mouseDown(const juce::MouseEvent& event)
{
    if (wantsPointerAt(event.getPosition()))
    {
        m_on_pointer_event(core::ChartPointerPhase::Down, makePointerEvent(event));
    }
}

void TabView::mouseDrag(const juce::MouseEvent& event)
{
    // The Alt insert quasimode places at the release point, so its ghost tracks the drag too.
    updateGhost(event);
    // No wantsPointerAt gate: a drag that started inside the lane keeps reporting while the
    // pointer travels outside it, exactly like any JUCE drag capture.
    if (m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Drag, makePointerEvent(event));
    }
}

void TabView::mouseUp(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Up, makePointerEvent(event));
    }
}

void TabView::setSustainWheelCallback(SustainWheelCallback on_sustain_wheel)
{
    m_on_sustain_wheel = std::move(on_sustain_wheel);
}

// Alt+wheel adjusts the selection's sustain per the interaction model (Ctrl+Alt+wheel steps the
// fine grid); wheel events without Alt fall through to the hosting viewport's scrolling.
void TabView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const bool wants_sustain = m_on_sustain_wheel != nullptr && event.mods.isAltDown() &&
                               m_tab != nullptr && m_tab->string_count > 0 && wheel.deltaY != 0.0f;
    if (!wants_sustain)
    {
        Component::mouseWheelMove(event, wheel);
        return;
    }

    m_on_sustain_wheel(wheel.deltaY > 0.0f ? 1 : -1, event.mods.isCtrlDown());
}

// Stores the visible timeline range used to map note times to pixels.
void TabView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }

    m_visible_timeline = visible_timeline;
    repaint();
}

// Applies the current tab projection and lane-count preference; the projection pointer only
// changes when the displayed arrangement changes, so pointer identity gates the index rebuild.
void TabView::setState(
    std::shared_ptr<const common::core::TabViewState> tab, int minimum_displayed_strings)
{
    const bool tab_changed = tab != m_tab;
    const bool lanes_changed = minimum_displayed_strings != m_minimum_displayed_strings;
    if (!tab_changed && !lanes_changed)
    {
        return;
    }

    m_tab = std::move(tab);
    m_minimum_displayed_strings = minimum_displayed_strings;
    if (tab_changed)
    {
        rebuildVisibilityIndex();
        refreshFocusBlink();
    }

    repaint();
}

// Guards the empty cases, derives the shared metrics, and delegates the drawing to the shared
// notation paint core.
void TabView::paint(juce::Graphics& g)
{
    if (m_tab == nullptr || m_tab->string_count <= 0)
    {
        return;
    }

    const juce::Rectangle<int> bounds = getLocalBounds();
    if (bounds.isEmpty() || m_visible_timeline.duration().seconds <= 0.0)
    {
        return;
    }

    const int displayed_count =
        tabDisplayedStringCount(m_tab->string_count, m_minimum_displayed_strings);
    const common::ui::TabLaneMetrics metrics = common::ui::makeTabLaneMetrics(
        bounds, m_visible_timeline, displayed_count, m_tab->string_count);
    common::ui::paintTabLane(g, metrics, *m_tab, m_prefix_max_end_seconds);

    // Chart-editing overlays draw above the shared notation and never enter the paint core:
    // they are editor-shell furniture, not part of what the game's tab strips render.
    const juce::Colour accent = editorTheme().accent;

    // One stroke width for both edge-straddling rings (selection highlight and Alt ghost), so
    // the ghost previews exactly the ring dimensions a selected head wears.
    const auto ring_stroke = [](float head_size) noexcept {
        return std::max(1.0f, head_size / 15.0f) * 1.5f;
    };

    // Selection highlight: an accent ring straddling the head's outer edge — the stroke is
    // centered on the edge, at one and a half border-widths thick, so it sits between the
    // head's own border ring and the accent glow while leaving the glow annulus readable on
    // accented notes (user feedback 2026-07-17, twice narrowed: the fully-outward cut buried
    // the glow and the double-width stroke still covered too much of it); harmonic heads get
    // the matching diamond.
    for (const std::size_t index : m_edit.selected_notes)
    {
        if (index >= m_tab->notes.size())
        {
            continue;
        }
        const common::core::TabNoteView& note = m_tab->notes[index];
        const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(metrics, note);
        const float stroke = ring_stroke(layout.head_size);
        const float extent = layout.head_size;
        g.setColour(accent);
        if (note.harmonic != common::core::NoteHarmonic::None)
        {
            juce::Path shape;
            shape.startNewSubPath(layout.onset_x, layout.center_y - extent / 2.0f);
            shape.lineTo(layout.onset_x + extent / 2.0f, layout.center_y);
            shape.lineTo(layout.onset_x, layout.center_y + extent / 2.0f);
            shape.lineTo(layout.onset_x - extent / 2.0f, layout.center_y);
            shape.closeSubPath();
            g.strokePath(shape, juce::PathStrokeType{stroke});
        }
        else
        {
            g.drawEllipse(
                layout.onset_x - extent / 2.0f,
                layout.center_y - extent / 2.0f,
                extent,
                extent,
                stroke);
        }
    }

    // The focused member's type-here underline: a caret-style blinking accent bar under the
    // fret numeral, marking where typed digits land. Drawn only when the selection offers a
    // choice (more than one member) and the numerals themselves are drawn — typing frets with
    // invisible numbers is not a workflow worth an indicator.
    m_focus_underline_bounds = {};
    if (m_edit.focused_note.has_value() && m_edit.selected_notes.size() > 1 && metrics.draw_text &&
        *m_edit.focused_note < m_tab->notes.size())
    {
        const common::ui::TabNoteLayout layout =
            common::ui::tabNoteLayout(metrics, m_tab->notes[*m_edit.focused_note]);
        const float bar_width = layout.head_size * 0.6f;
        const float bar_thickness = std::max(1.5f, layout.head_size / 15.0f);
        const juce::Rectangle<float> bar{
            layout.onset_x - bar_width / 2.0f,
            layout.center_y + layout.head_size * 0.28f,
            bar_width,
            bar_thickness
        };
        m_focus_underline_bounds = bar.getSmallestIntegerContainer();
        if (m_focus_underline_visible)
        {
            g.setColour(accent);
            g.fillRect(bar);
        }
    }

    // The in-flight marquee: translucent accent fill with a crisp border.
    if (m_edit.marquee.has_value())
    {
        const float left = metrics.x(m_edit.marquee->start_seconds);
        const float right = metrics.x(m_edit.marquee->end_seconds);
        const float top = static_cast<float>(bounds.getY()) +
                          m_edit.marquee->top_fraction * static_cast<float>(bounds.getHeight());
        const float bottom =
            static_cast<float>(bounds.getY()) +
            m_edit.marquee->bottom_fraction * static_cast<float>(bounds.getHeight());
        const juce::Rectangle<float> box{left, top, right - left, bottom - top};
        g.setColour(accent.withAlpha(0.15f));
        g.fillRect(box);
        g.setColour(accent);
        g.drawRect(box, 1.0f);
    }

    // The Alt-held ghost: a faded white ring with the exact dimensions of the selection ring
    // on the head a click would insert, at the snapped position on the hovered lane. A neutral
    // outline, not a full colored note (user feedback 2026-07-17): the insert always lands at
    // fret 0 and the real fret gets typed right after, so mirroring color and number would
    // preview a note nobody keeps.
    if (m_ghost.has_value() && m_ghost->string >= 1 && m_ghost->string <= m_tab->string_count)
    {
        const float size = metrics.note_height + 1.0f;
        const float center_y = metrics.laneY(m_ghost->string);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(
            m_ghost->x - size / 2.0f, center_y - size / 2.0f, size, size, ring_stroke(size));
    }
}

// Rebuilds the prefix-maximum sustain-end table after the projection changes.
void TabView::rebuildVisibilityIndex()
{
    m_prefix_max_end_seconds =
        m_tab == nullptr ? std::vector<double>{} : common::ui::tabPrefixMaxEndSeconds(m_tab->notes);
}

} // namespace rock_hero::editor::ui
