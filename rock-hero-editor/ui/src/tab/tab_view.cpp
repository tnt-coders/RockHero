#include "tab/tab_view.h"

#include "shared/editor_theme.h"

#include <algorithm>
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
    repaint();
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
        .modifiers =
            core::ChartPointerModifiers{
                .ctrl = event.mods.isCtrlDown(),
                .shift = event.mods.isShiftDown(),
                .alt = event.mods.isAltDown(),
            },
        .clicks = event.getNumberOfClicks(),
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

    // The editing caret on an empty slot (the caret model): a white ring the size of a note
    // head marking exactly where a typed digit will insert. On a note the selection highlight
    // is the caret display, so the controller publishes nothing here.
    if (m_edit.caret.has_value() && m_edit.caret->string >= 1 &&
        m_edit.caret->string <= m_tab->string_count && m_visible_timeline.duration().seconds > 0.0)
    {
        const float x = static_cast<float>(
            (m_edit.caret->seconds - m_visible_timeline.start.seconds) /
            m_visible_timeline.duration().seconds * static_cast<double>(getWidth()));
        const float size = metrics.note_height + 1.0f;
        const float center_y = metrics.laneY(m_edit.caret->string);
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawEllipse(x - size / 2.0f, center_y - size / 2.0f, size, size, ring_stroke(size));
    }
}

// Rebuilds the prefix-maximum sustain-end table after the projection changes.
void TabView::rebuildVisibilityIndex()
{
    m_prefix_max_end_seconds =
        m_tab == nullptr ? std::vector<double>{} : common::ui::tabPrefixMaxEndSeconds(m_tab->notes);
}

} // namespace rock_hero::editor::ui
