#include "tab/tab_view.h"

#include "shared/editor_theme.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

// One stroke width for every edge-straddling overlay outline (selection rings and the caret
// square), so the editing furniture reads as one family at any head size. The caret-mask query
// shares it so the mask spans exactly the square's outer stroke edge.
[[nodiscard]] float overlayRingStroke(float head_size) noexcept
{
    return std::max(1.0f, head_size / 15.0f) * 1.5f;
}

} // namespace

// The notation rasterizer lives in the shared paint core (rock-hero-common/ui tab/), one
// authority for the editor lane and the game tab strips; these free functions stay on the
// editor surface as thin delegates so editor widgets and tests keep their existing seam.

juce::Colour tabStringColor(int displayed_string, int displayed_string_count)
{
    return common::ui::tabStringColor(displayed_string, displayed_string_count);
}

float tabLaneCenterY(
    int displayed_string, int displayed_string_count, juce::Rectangle<int> bounds) noexcept
{
    return common::ui::tabLaneCenterY(displayed_string, displayed_string_count, bounds);
}

void TabView::setPointerEventCallback(PointerEventCallback on_pointer_event)
{
    m_on_pointer_event = std::move(on_pointer_event);
}

void TabView::setCaretMaskCallback(CaretMaskCallback callback)
{
    m_caret_mask_callback = std::move(callback);
    // Seed the sink with the current mask so a callback installed after a caret already exists is
    // not left believing the column is ungapped.
    m_published_caret_mask.reset();
    publishCaretMask();
}

void TabView::setContextMenuCallback(ContextMenuCallback callback)
{
    m_context_menu_callback = std::move(callback);
}

// Applies the chart-editing overlay state; skipped repaints keep unrelated pushes cheap. The
// repaint covers the notation too, not only the overlays: the selection is one of the two inputs
// to the drawn-form pick, so a selection change re-cuts the tails it names.
void TabView::setEditState(core::ChartEditViewState edit)
{
    if (edit == m_edit)
    {
        return;
    }

    m_edit = std::move(edit);
    repaint();
    // The overlay carries the caret; push its fresh mask now so the paused column's cut-out
    // changes in the same synchronous pass as the drawn square, never a frame behind it.
    publishCaretMask();
}

// Holds or releases the actual-ring reveal. A repaint only on a genuine change, because the
// editor re-asserts its foreground-and-Alt predicate every frame for its whole life, so nearly
// every call says what the lane already shows.
void TabView::setActualRingReveal(bool revealed)
{
    if (revealed == m_actual_ring_reveal)
    {
        return;
    }

    m_actual_ring_reveal = revealed;
    repaint();
}

// With a chart displayed the lane claims its whole band — while paused a click arms the caret
// (which IS the play-from-here position), and while playing the controller turns lane clicks
// into plain seeks, so seeking through the lane keeps working. Without a chart the lane is
// pointer-transparent.
//
// The pointer path reads the PRESENTED projection throughout, because that is the one the
// controller resolves clicks against; an actual ring is drawn and nothing more.
bool TabView::wantsPointerAt(juce::Point<int> local_point) const
{
    return m_on_pointer_event != nullptr && m_presented != nullptr &&
           m_presented->string_count > 0 && getLocalBounds().contains(local_point) &&
           m_visible_timeline.duration().seconds > 0.0;
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
        common::core::displayedStringCount(m_presented->string_count, m_minimum_displayed_strings);
    return core::ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()),
            m_visible_timeline,
            displayed_count,
            m_presented->string_count),
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
    if (!wantsPointerAt(event.getPosition()))
    {
        return;
    }

    // The popup gesture raises the discovery menu instead of starting a gesture: a right press is
    // not a select, a seek, or an insert, so it must never reach the controller as a Down.
    //
    // A LEFT press is never the popup gesture, whatever else is held. JUCE expands
    // popupMenuClickModifier to (rightButton | ctrl) on macOS, so isPopupMenu() alone is also true
    // for Ctrl+left-click there — and Ctrl is this lane's precision modifier, so the menu would eat
    // every precision gesture on that one platform. Testing the left press instead keeps a single
    // behavior on all three, with no OS conditional.
    if (event.mods.isPopupMenu() && !event.mods.isLeftButtonDown())
    {
        if (m_context_menu_callback != nullptr)
        {
            m_context_menu_callback(event.getPosition());
        }
        return;
    }

    m_on_pointer_event(core::ChartPointerPhase::Down, makePointerEvent(event));
}

void TabView::mouseDrag(const juce::MouseEvent& event)
{
    // No wantsPointerAt gate: a drag that started inside the lane keeps reporting while the
    // pointer travels outside it, exactly like any JUCE drag capture.
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Drag, makePointerEvent(event));
    }
}

void TabView::mouseUp(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Up, makePointerEvent(event));
    }
}

// A button-less hover drives the Alt insert ghost: the controller resolves whether Alt is held
// over an insertable empty slot and publishes the ring. Like the automation lane's ghost the
// preview follows the pointer, so it materializes on the first Alt+move rather than the instant
// Alt is pressed.
void TabView::mouseMove(const juce::MouseEvent& event)
{
    if (wantsPointerAt(event.getPosition()))
    {
        m_on_pointer_event(core::ChartPointerPhase::Move, makePointerEvent(event));
    }
}

// Leaving the lane clears any hover ghost; the event carries no position the controller needs.
void TabView::mouseExit(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Exit, makePointerEvent(event));
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
    // The caret's y-span is timeline-invariant, so this is a fire-on-change no-op on ordinary
    // zoom/scroll; it exists to cover the one timeline-driven change to the mask — the
    // duration<=0 presence flip caretMaskYRange() and paint() both gate on — so the tab lane needs
    // no per-frame safety net to stay decoupled from the viewport's geometry polling.
    publishCaretMask();
}

// Applies the current tab projections and lane-count preference; the projection pointers only
// change when the displayed arrangement or the chart revision does, so pointer identity gates the
// index rebuild. The two forms are published together and tested together — the controller derives
// them in one step, so one changing without the other would be a defect upstream, not a case to
// handle here.
void TabView::setState(
    std::shared_ptr<const common::core::ChartViewState> tab,
    std::shared_ptr<const common::core::ChartViewState> tab_actual, int minimum_displayed_strings)
{
    const bool tab_changed = tab != m_presented || tab_actual != m_actual;
    const bool lanes_changed = minimum_displayed_strings != m_minimum_displayed_strings;
    if (!tab_changed && !lanes_changed)
    {
        return;
    }

    m_presented = std::move(tab);
    m_actual = std::move(tab_actual);
    // The pick in paint reads the actual form AT A PRESENTED NOTE'S INDEX, which the one
    // derivation behind both forms guarantees: presentedChartNotes returns one note per input
    // note in the same order. A mismatch could only be an upstream defect, and this is the seam
    // it would enter through, so it is caught here rather than as an out-of-range read per frame.
    assert(
        m_presented == nullptr || m_actual == nullptr ||
        m_presented->notes.size() == m_actual->notes.size());
    m_minimum_displayed_strings = minimum_displayed_strings;
    if (tab_changed)
    {
        rebuildVisibilityIndex();
    }

    repaint();
    // The displayed string count sets the row layout the caret square rides, so a projection or
    // lane-count change can move the mask even with the caret slot unchanged.
    publishCaretMask();
}

// Guards the empty cases, derives the shared metrics, and delegates the drawing to the shared
// notation paint core.
//
// The presented projection is what everything but the notes reads — string count, capo, spans,
// placements are equal in the two forms — and drawn_note below is the ONE place that decides
// which form a note itself is drawn in. Every overlay reads it too, so nothing can trace a head
// the lane did not draw.
void TabView::paint(juce::Graphics& g)
{
    if (m_presented == nullptr || m_presented->string_count <= 0)
    {
        return;
    }

    const juce::Rectangle<int> bounds = getLocalBounds();
    if (bounds.isEmpty() || m_visible_timeline.duration().seconds <= 0.0)
    {
        return;
    }

    const common::core::ChartViewState& tab = *m_presented;
    const int displayed_count =
        common::core::displayedStringCount(tab.string_count, m_minimum_displayed_strings);
    const common::ui::TabLaneMetrics metrics = common::ui::makeTabLaneMetrics(
        bounds, m_visible_timeline, displayed_count, tab.string_count);

    // Which form one note draws in, and the only statement of that rule: its ACTUAL ring while
    // the whole-lane reveal is held OR while it is selected, its presented tail otherwise.
    //
    // The selection draws actual because the selection is the thing under scrutiny — and every
    // chart verb settles on a selection change, so deselecting is exactly the moment presentation
    // clips the tail back. The reveal covers what a selection cannot, since placing notes leaves
    // nothing selected (setActualRingReveal carries that argument).
    //
    // Reads the published selection rather than a copy of it: the indices are the ones the
    // selection ring already draws with, ascending in the tab projection's own note order
    // (ChartEditViewState), so membership is a binary search over the same table.
    const auto drawn_note = [this, &tab](std::size_t index) -> const common::core::NoteViewState& {
        const bool actual =
            m_actual != nullptr &&
            (m_actual_ring_reveal || std::ranges::binary_search(m_edit.selected_notes, index));
        return actual ? m_actual->notes[index] : tab.notes[index];
    };

    common::ui::paintTabLane(g, metrics, tab, m_prefix_max_end_seconds, drawn_note);

    // Chart-editing overlays draw above the shared notation and never enter the paint core:
    // they are editor-shell furniture, not part of what the game's tab strips render.
    const juce::Colour accent = editorTheme().accent;

    // Selection highlight: an accent ring straddling the head's outer edge — the stroke is
    // centered on the edge, at one and a half border-widths thick, so it sits between the
    // head's own border ring and the accent glow while leaving the glow annulus readable on
    // accented notes (a fully-outward cut buried the glow, and a double-width stroke still
    // covered too much of it). The silhouette comes from the paint core, so the ring always
    // traces the head that is actually under it — this used to re-derive the shape here and
    // drew a circle around every plectrum once the scrape head shipped.
    for (const std::size_t index : m_edit.selected_notes)
    {
        if (index >= tab.notes.size())
        {
            continue;
        }
        const common::core::NoteViewState& note = drawn_note(index);
        // A silently-held stop has no head to ring; its face is the posture bracket, ringed by
        // the pass below on the very silhouette the click resolved.
        if (common::core::silentHold(note.attack))
        {
            continue;
        }
        const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(metrics, note);
        g.setColour(accent);
        common::ui::strokeTabNoteHeadOutline(
            g,
            note,
            layout.onset_x,
            layout.center_y,
            layout.head_size,
            overlayRingStroke(layout.head_size));
    }

    // Selected waypoints wear the SAME accent ring, traced on the linked head the paint core drew
    // at that junction — one selection idiom for every selectable, so a selected junction reads
    // exactly as a selected head does. Drawn from the published list, which only holds waypoints
    // that still draw, so a ring can never appear where no head is.
    for (const core::ChartWaypointRef& selected : m_edit.selected_waypoints)
    {
        if (selected.note_index >= tab.notes.size())
        {
            continue;
        }
        const common::core::NoteViewState& note = drawn_note(selected.note_index);
        if (selected.waypoint_index >= note.slides.size())
        {
            continue;
        }
        const common::core::SlideViewState& waypoint = note.slides[selected.waypoint_index];
        const common::ui::TabWaypointLayout layout =
            common::ui::tabWaypointLayout(metrics, note, waypoint);
        g.setColour(accent);
        common::ui::strokeTabNoteHeadOutline(
            g,
            note,
            layout.center_x,
            layout.center_y,
            layout.head_size,
            overlayRingStroke(layout.head_size));
    }

    // Selected silently-held stops. The overlay draws NO mark of its own for one (user ruling
    // 2026-08-27: "There should be no dot visible when we press N ... The bracket marker IS the
    // data point that we can select and modify"): the stop is stated by the arpeggio bracket the
    // paint core already draws at its span's start, so an authoring dot beside it was a second
    // mark for one fact, and the fact was drawn in the wrong place besides. All that is left here
    // is the selection ring, traced on that bracket — the same accent every other selected object
    // wears, on the same silhouette the click resolved.
    //
    // The layout answers with nothing for a hold that resolved into no span, which is precisely
    // the hold the paint core draws no bracket for; ring and mark therefore appear and vanish
    // together with no rule of their own. It answers with nothing for a sounding note too, which
    // is why this pass and the head-ring pass above can share one selection list.
    for (const std::size_t index : m_edit.selected_notes)
    {
        if (index >= tab.notes.size())
        {
            continue;
        }
        const std::optional<common::ui::TabSilentHoldLayout> layout =
            common::ui::tabSilentHoldLayout(metrics, tab.notes[index]);
        if (!layout.has_value())
        {
            continue;
        }
        const common::ui::TabLayoutRect& box = layout->box;
        g.setColour(accent);
        g.drawRect(
            juce::Rectangle<float>{box.x, box.y, box.width, box.height},
            overlayRingStroke(box.height));
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

    // The armed caret (the marker model): a white, slightly rounded square at the caret's
    // slot. Square rather than round so it reads as editor furniture distinct from every
    // circular note shape (heads, accent glows) and stays visible over them; drawn whenever
    // the marker is armed — on an empty slot it marks where a typed digit inserts, on a note
    // it rides the selection highlight so the caret stays visible through a single selection.
    // While the marker is passive the controller publishes nothing here and the ruler's
    // play-from-here mark is the position display. The paused play-from-here column behind
    // the content never shows inside the square: the track viewport cuts caretMaskYRange()
    // out of it.
    if (const std::optional<juce::Rectangle<float>> square = caretSquare(metrics))
    {
        const float size = square->getWidth();
        g.setColour(editorTheme().lane_overlay);
        g.drawRoundedRectangle(*square, size / 8.0f, overlayRingStroke(size));
    }

    // The Alt-hover insert ghost: a hollow white ring the size of a note head, at the slot where
    // an Alt+click would plant a fret-0 note. Round rather than the caret's square so it reads as
    // a note-to-be, not the editing caret; present only while Alt hovers an insertable empty slot
    // (the controller resolves the honesty gate), so it never advertises an insert that no-ops.
    if (m_edit.insert_ghost.has_value() && m_edit.insert_ghost->string >= 1 &&
        m_edit.insert_ghost->string <= tab.string_count)
    {
        const float size = metrics.note_height;
        const float center_x = metrics.x(m_edit.insert_ghost->seconds);
        const float center_y = metrics.laneY(m_edit.insert_ghost->string);
        g.setColour(editorTheme().lane_overlay);
        g.drawEllipse(
            center_x - size / 2.0f, center_y - size / 2.0f, size, size, overlayRingStroke(size));
    }

    // The pending fret entry: the provisional value in its accent-bordered box over each
    // affected head (or at the empty insert slot), red when it cannot apply — every affected
    // head marks together, because a relational refusal has no per-note attribution. Editor
    // chrome like the caret and the ghost, but drawn through the paint core's one exported
    // primitive so the digit's typography and plate cannot drift from the committed head's.
    if (m_edit.pending_fret.has_value())
    {
        // Valid rides the dark plate in the digit's own white; invalid FLIPS the plate to the
        // white ground with the theme's red — the polarity flip is itself the glance signal.
        const bool invalid = !m_edit.pending_fret->valid;
        const juce::Colour ink = invalid ? editorTheme().invalid : editorTheme().primary_text;
        const juce::String text{m_edit.pending_fret->text};
        if (const auto* const targets =
                std::get_if<core::ChartPendingFretTargets>(&m_edit.pending_fret->at))
        {
            for (const std::size_t index : targets->notes)
            {
                if (index >= tab.notes.size())
                {
                    continue;
                }
                const common::core::NoteViewState& note = drawn_note(index);
                // A selected bracket wears the same box AT the bracket, which is where the stop it
                // states prints — no head sits under it, so the box carries none, exactly as the
                // empty-slot insert case does. A hold whose bracket is not drawn shows nothing, on
                // the same rule that keeps its ring and its hit box off the lane.
                if (const std::optional<common::ui::TabSilentHoldLayout> hold =
                        common::ui::tabSilentHoldLayout(metrics, note);
                    common::core::silentHold(note.attack))
                {
                    if (hold.has_value())
                    {
                        common::ui::paintTabPendingEntryBox(
                            g,
                            metrics,
                            nullptr,
                            hold->center_x,
                            hold->center_y,
                            text,
                            invalid,
                            ink,
                            accent);
                    }
                    continue;
                }
                const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(metrics, note);
                common::ui::paintTabPendingEntryBox(
                    g, metrics, &note, layout.onset_x, layout.center_y, text, invalid, ink, accent);
            }
        }
        else if (
            const auto* const slot =
                std::get_if<core::ChartSlotViewState>(&m_edit.pending_fret->at);
            slot != nullptr && slot->string >= 1 && slot->string <= tab.string_count
        )
        {
            common::ui::paintTabPendingEntryBox(
                g,
                metrics,
                nullptr,
                metrics.x(slot->seconds),
                metrics.laneY(slot->string),
                text,
                invalid,
                ink,
                accent);
        }
    }
}

// Resolves the caret square against freshly derived metrics, mirroring paint's derivation so
// the mask always matches the drawn square.
std::optional<juce::Range<float>> TabView::caretMaskYRange() const
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const common::core::ChartViewState* const tab = m_presented.get();
    if (tab == nullptr || tab->string_count <= 0 || bounds.isEmpty() ||
        m_visible_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    const int displayed_count =
        common::core::displayedStringCount(tab->string_count, m_minimum_displayed_strings);
    const std::optional<juce::Rectangle<float>> square = caretSquare(
        common::ui::makeTabLaneMetrics(
            bounds, m_visible_timeline, displayed_count, tab->string_count));
    if (!square.has_value())
    {
        return std::nullopt;
    }

    // The span reaches the square's outer stroke edge, so no cursor pixel survives inside or
    // under the outline.
    const float stroke_reach = overlayRingStroke(square->getWidth()) / 2.0f;
    return juce::Range<float>{square->getY() - stroke_reach, square->getBottom() + stroke_reach};
}

// Translates the local caret mask into content coordinates and pushes it only when it changed.
void TabView::publishCaretMask()
{
    const std::optional<juce::Range<float>> local = caretMaskYRange();
    const std::optional<juce::Range<float>> content =
        local.has_value() ? std::optional<juce::Range<float>>{*local + static_cast<float>(getY())}
                          : std::nullopt;
    if (content == m_published_caret_mask)
    {
        return;
    }
    m_published_caret_mask = content;
    if (m_caret_mask_callback)
    {
        m_caret_mask_callback(content);
    }
}

void TabView::moved()
{
    publishCaretMask();
}

void TabView::resized()
{
    publishCaretMask();
}

// The caret square: centered on the caret's slot, one pixel larger than a note head so it
// reads around a head it rides. It is a SLOT, not a note, so the per-note form pick has nothing
// to say about it; the string bound it needs is the presented form's, which is the same count
// the actual form carries.
std::optional<juce::Rectangle<float>> TabView::caretSquare(
    const common::ui::TabLaneMetrics& metrics) const
{
    const common::core::ChartViewState* const tab = m_presented.get();
    if (tab == nullptr || !m_edit.caret.has_value() || m_edit.caret->string < 1 ||
        m_edit.caret->string > tab->string_count)
    {
        return std::nullopt;
    }

    const float size = metrics.headSize();
    const float center_y = metrics.laneY(m_edit.caret->string);
    const float x = metrics.x(m_edit.caret->seconds);
    return juce::Rectangle<float>{x - size / 2.0f, center_y - size / 2.0f, size, size};
}

// Rebuilds the prefix-maximum end table the paint core culls against, after the projections
// change.
//
// The ACTUAL form's ends, because a lane that draws both forms at once needs a bound that holds
// for either, and presentation only ever trims: every presented end falls at or before its own
// note's ring. Culling against the rings keeps a note in the candidate range for as long as ANY
// form of it could be drawn, and the paint passes drop each one whose drawn end really precedes
// the window, so the picture stays exact. The span-implied hold that outlasts both belongs to the
// 3D board and is still not indexed here — this surface does not draw it.
void TabView::rebuildVisibilityIndex()
{
    const common::core::ChartViewState* const furthest_reaching =
        m_actual != nullptr ? m_actual.get() : m_presented.get();
    m_prefix_max_end_seconds = furthest_reaching == nullptr
                                   ? std::vector<double>{}
                                   : common::core::makeSustainPrefixMax(furthest_reaching->notes);
}

} // namespace rock_hero::editor::ui
