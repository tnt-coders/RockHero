#include "tab/tab_view.h"

#include "shared/editor_theme.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cstddef>
#include <memory>
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

// Stroke of the actual-ring reveal's outline: a hairline, deliberately outside the ring family
// above. Those mark ONE object each and are sized to straddle its edge; the reveal draws on every
// visible note at once, so its weight is the whole of what keeps a lane full of outlines reading
// as an annotation over the notation rather than as more notation. The marquee's border is the
// same hairline for the same reason.
constexpr float g_actual_ring_reveal_stroke{1.0f};

// How far the reveal's outline sits under the furniture ink it shares. The caret square and the
// insert ghost are the loudest marks on the lane by design and each states one slot; an outline on
// every visible note at that weight would bury what it is annotating.
constexpr float g_actual_ring_reveal_dim{0.5f};

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

// Applies the chart-editing overlay state; skipped repaints keep unrelated pushes cheap.
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

// Flips which mark the reveal makes. The repaint is conditional because the style changes nothing
// on screen while the reveal is not held — it is a latched preference, unlike the held reveal.
void TabView::setActualRingRevealStyle(ActualRingRevealStyle style)
{
    if (style == m_reveal_style)
    {
        return;
    }

    m_reveal_style = style;
    if (m_actual_ring_reveal)
    {
        repaint();
    }
}

// Read by the shell for the tick beside the style's command.
ActualRingRevealStyle TabView::actualRingRevealStyle() const noexcept
{
    return m_reveal_style;
}

// The form paint draws, and the visibility index that belongs to it. The actual form only when
// the reveal is held in the style that swaps the notation — and only when one was published, so a
// host pushing the presented projection alone simply has no reveal rather than a crash.
const TabView::LaneForm& TabView::drawn() const noexcept
{
    const bool draw_rings = m_actual_ring_reveal &&
                            m_reveal_style == ActualRingRevealStyle::Tails &&
                            m_actual.state != nullptr;
    return draw_rings ? m_actual : m_presented;
}

// With a chart displayed the lane claims its whole band — while paused a click arms the caret
// (which IS the play-from-here position), and while playing the controller turns lane clicks
// into plain seeks, so seeking through the lane keeps working. Without a chart the lane is
// pointer-transparent.
//
// The pointer path reads the PRESENTED projection throughout, because that is the one the
// controller resolves clicks against; the reveal's own form is drawn and nothing more.
bool TabView::wantsPointerAt(juce::Point<int> local_point) const
{
    return m_on_pointer_event != nullptr && m_presented.state != nullptr &&
           m_presented.state->string_count > 0 && getLocalBounds().contains(local_point) &&
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
    const int displayed_count = common::core::displayedStringCount(
        m_presented.state->string_count, m_minimum_displayed_strings);
    return core::ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()),
            m_visible_timeline,
            displayed_count,
            m_presented.state->string_count),
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
    if (m_on_pointer_event != nullptr && m_presented.state != nullptr &&
        m_presented.state->string_count > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Drag, makePointerEvent(event));
    }
}

void TabView::mouseUp(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_presented.state != nullptr &&
        m_presented.state->string_count > 0)
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
    if (m_on_pointer_event != nullptr && m_presented.state != nullptr &&
        m_presented.state->string_count > 0)
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
    const bool tab_changed = tab != m_presented.state || tab_actual != m_actual.state;
    const bool lanes_changed = minimum_displayed_strings != m_minimum_displayed_strings;
    if (!tab_changed && !lanes_changed)
    {
        return;
    }

    m_presented.state = std::move(tab);
    m_actual.state = std::move(tab_actual);
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
// Everything below reads the DRAWN form: while the reveal is held in its tail style that is the
// chart at its actual rings, and every overlay must trace the heads that were painted rather than
// the other form's. The heads are identical in both forms (presentation touches only the tail), so
// today this is a rule about which authority the overlays ask, not about pixels moving.
void TabView::paint(juce::Graphics& g)
{
    const LaneForm& lane = drawn();
    if (lane.state == nullptr || lane.state->string_count <= 0)
    {
        return;
    }

    const juce::Rectangle<int> bounds = getLocalBounds();
    if (bounds.isEmpty() || m_visible_timeline.duration().seconds <= 0.0)
    {
        return;
    }

    const common::core::ChartViewState& tab = *lane.state;
    const int displayed_count =
        common::core::displayedStringCount(tab.string_count, m_minimum_displayed_strings);
    const common::ui::TabLaneMetrics metrics = common::ui::makeTabLaneMetrics(
        bounds, m_visible_timeline, displayed_count, tab.string_count);
    common::ui::paintTabLane(g, metrics, tab, lane.prefix_max_end_seconds);

    // Chart-editing overlays draw above the shared notation and never enter the paint core:
    // they are editor-shell furniture, not part of what the game's tab strips render.
    const juce::Colour accent = editorTheme().accent;

    // The actual-ring reveal in its OUTLINE style: the notation above is the presented picture,
    // and every visible note additionally gets the ring the string really sounds for outlined —
    // which that tail may have trimmed, floored, or dropped to nothing. Drawn for EVERY visible
    // note, not only where the two ends differ — an outline landing exactly on a drawn tail is the
    // statement "this is the whole ring", and a mark that appeared only on disagreement would
    // leave the reader unable to tell agreement from a reveal that is simply off.
    //
    // Its ink is the theme's lane_overlay, halved. Editor furniture reads through EditorTheme,
    // the editor's one color seam; the lane's own quieting authority (the paint core's Ink set
    // leaned toward the lane ground) belongs to the NOTATION, is private to that core, and is not
    // a palette chrome may borrow. Within the theme this joins the caret square and the insert
    // ghost rather than the accent, for two reasons: the accent means "selected", and a mark in
    // it on every visible note would read as a lane-wide selection; and this outline belongs to
    // the same Alt family the insert ghost does — what the next edit acts on.
    //
    // The TAIL style needs nothing here: it swapped the whole lane to the actual form above, so
    // the ring IS the notation and no ink question arises.
    //
    // Drawn first of the overlays so the selection ring and the caret stay above it: it is the
    // quietest mark here and by far the most numerous.
    if (m_actual_ring_reveal && m_reveal_style == ActualRingRevealStyle::Outline &&
        m_actual.state != nullptr)
    {
        // The paint core's own visible window, asked rather than restated, so an outline cannot
        // survive a repaint the note under it did not. Both the cull and the rectangle come from
        // the ACTUAL form — one authority for a ring's length, whichever style is showing it —
        // and that form's index is the running maximum over its own (ring-length) ends, which is
        // exactly what an outline reaching past its presented tail needs to stay in range.
        const common::core::TimeRange span = common::ui::tabVisibleSpan(metrics, g.getClipBounds());
        const std::vector<common::core::NoteViewState>& rings = m_actual.state->notes;
        const auto [first, last] = common::core::visibleEventRange(
            rings, m_actual.prefix_max_end_seconds, span.start.seconds, span.end.seconds);
        g.setColour(editorTheme().lane_overlay.withMultipliedAlpha(g_actual_ring_reveal_dim));
        for (std::size_t index = first; index < last; ++index)
        {
            // The prefix maximum is a RUNNING one, so the range can open on a long ring and carry
            // shorter neighbours that ended before the window with it.
            if (rings[index].end_seconds < span.start.seconds)
            {
                continue;
            }

            // The actual form's own tail rectangle, from the layout the paint core draws with, so
            // the outline traces exactly where that form's tail would sit rather than restating
            // the geometry. Every stored ring is positive, so this rectangle is never empty.
            const common::ui::TabLayoutRect ring =
                common::ui::tabNoteLayout(metrics, rings[index]).tail;
            g.drawRect(
                juce::Rectangle<float>{ring.x, ring.y, ring.width, ring.height},
                g_actual_ring_reveal_stroke);
        }
    }

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
        const common::core::NoteViewState& note = tab.notes[index];
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
        if (const auto* const notes =
                std::get_if<std::vector<std::size_t>>(&m_edit.pending_fret->at))
        {
            for (const std::size_t index : *notes)
            {
                if (index >= tab.notes.size())
                {
                    continue;
                }
                const common::core::NoteViewState& note = tab.notes[index];
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
    const common::core::ChartViewState* const tab = drawn().state.get();
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
// reads around a head it rides. The string bound comes from the drawn form like every other
// overlay, which costs nothing to honor: the string count is identical in both forms.
std::optional<juce::Rectangle<float>> TabView::caretSquare(
    const common::ui::TabLaneMetrics& metrics) const
{
    const common::core::ChartViewState* const tab = drawn().state.get();
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

// Rebuilds each form's prefix-maximum end table after the projections change.
void TabView::rebuildVisibilityIndex()
{
    // Each table is the running maximum of ITS OWN form's note ends, which is exactly what that
    // form draws: the presented one stops at the tails (the span-implied hold that outlasts them
    // belongs to the 3D board, and indexing it here would keep notes in range this surface
    // stopped drawing), and the actual one runs to the rings, which is what a ring outlasting its
    // tail needs to stay in range for as long as it is drawn. Two tables because the two culls
    // answer different questions, never because one is a widened copy of the other.
    const auto prefix_max = [](const std::shared_ptr<const common::core::ChartViewState>& state) {
        return state == nullptr ? std::vector<double>{}
                                : common::core::makeSustainPrefixMax(state->notes);
    };
    m_presented.prefix_max_end_seconds = prefix_max(m_presented.state);
    m_actual.prefix_max_end_seconds = prefix_max(m_actual.state);
}

} // namespace rock_hero::editor::ui
