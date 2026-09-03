#include "tab/tab_view.h"

#include "shared/editor_theme.h"
#include "timeline/sticky_label.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <rock_hero/common/ui/tab/tab_lane_layout.h>
#include <rock_hero/common/ui/tab/tab_layout_manifest.h>
#include <rock_hero/common/ui/tab/tab_paint_core.h>
#include <rock_hero/editor/core/chart/chart_reveal.h>
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
           m_presented->stringCount() > 0 && getLocalBounds().contains(local_point) &&
           m_visible_timeline.duration().seconds > 0.0;
}

// THE STRING LEGEND IS INERT CHROME (the pointer half of its ruling): the lane keeps CLAIMING its
// column — wantsPointerAt is unchanged, so the overlay still passes the press down here and no
// seek fires under the letters — and this lane simply has nothing to answer with there, so the
// press dies. The alternative, letting the column fall through, would seek to the leftmost visible
// time whenever a reader clicked a letter, which is a stranger answer than none.
//
// It is the tone row's chip rule read from the other side: there the pinned chip claims the pixel
// and the row (never the overlay) answers it, because a mark drawn ON TOP of a target must resolve
// the pointer that lands on it. The legend has no menu to open, so its answer is silence.
//
// The GOVERNING FRET-HAND CHIP standing on the panel is inert on the same terms and for the same
// reason (user ruling 2026-09-03): it is a mark pinned over notation the reader cannot see, and it
// can reach past the panel's own edge, so the question is asked of the whole pinned chrome rather
// than of the letters' column alone.
//
// Stated ONCE, here, rather than at each pointer entry point: hover, press, and the ghost all ask
// this one question, and a chrome column that swallowed presses but still armed a hover ghost
// would be exactly the half-applied rule this replaces.
bool TabView::wantsNotationAt(juce::Point<int> local_point) const
{
    return wantsPointerAt(local_point) && !pinnedChromeBounds().contains(local_point);
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
        common::core::displayedStringCount(m_presented->stringCount(), m_minimum_displayed_strings);
    return core::ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()),
            m_visible_timeline,
            displayed_count,
            m_presented->stringCount()),
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
    if (!wantsNotationAt(event.getPosition()))
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
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->stringCount() > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Drag, makePointerEvent(event));
    }
}

void TabView::mouseUp(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->stringCount() > 0)
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
    const juce::Point<int> position = event.getPosition();
    if (wantsNotationAt(position))
    {
        m_on_pointer_event(core::ChartPointerPhase::Move, makePointerEvent(event));
    }
    else if (wantsPointerAt(position))
    {
        // Over the legend the lane holds the pointer but has no slot under it, so the ghost goes
        // out exactly as it does when the pointer leaves the lane — a preview of an insert this
        // column would refuse must not hang there behind the letters.
        m_on_pointer_event(core::ChartPointerPhase::Exit, makePointerEvent(event));
    }
}

// Leaving the lane clears any hover ghost; the event carries no position the controller needs.
void TabView::mouseExit(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_presented != nullptr && m_presented->stringCount() > 0)
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
    // A timeline with no duration draws no lane at all, so the legend column is one of the things
    // that flips with it.
    refreshLegendColumn();
    repaint();
    // The caret's y-span is timeline-invariant, so this is a fire-on-change no-op on ordinary
    // zoom/scroll; it exists to cover the one timeline-driven change to the mask — the
    // duration<=0 presence flip caretMaskYRange() and paint() both gate on — so the tab lane needs
    // no per-frame safety net to stay decoupled from the viewport's geometry polling.
    publishCaretMask();
}

// Stores the viewport's left edge in this lane's own coordinates, which the pinned chrome — the
// string legend and the governing fret-hand chip standing on it — pins to.
//
// The repaint is held to the chrome's two columns — where it was and where it now is — rather than
// taken over the whole lane. The viewport SCROLLS these pixels without repainting them, so the
// notation is already correct everywhere else, and a playback follow moves this every frame: a
// full-row repaint would re-rasterize the whole visible chart at that rate for the sake of one
// column of chrome. For the same reason the panel's SIZE is not re-derived here — a scroll moves
// the pin and nothing else, so that half of the bounds is arithmetic on the cache.
//
// The pinned PLACEMENT is the one thing a scroll really does change, because which placement
// governs the left edge is a question about where the edge is. It is re-derived between the two
// repaints so the column being vacated is invalidated with the chip that was standing in it and
// the column being taken with the chip that now is.
//
// This lane is transparent, so both repaints reach the canvas beneath as well — which is what
// keeps the tempo grid's own exclusion at this column in step without a second push.
void TabView::setVisibleContentLeft(int content_left_x)
{
    if (m_visible_content_left == content_left_x)
    {
        return;
    }

    repaint(pinnedChromeBounds());
    m_visible_content_left = content_left_x;
    refreshPinnedFhp(laneMetrics());
    repaint(pinnedChromeBounds());
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

    // A lane-count change moves the legend panel — the count sets the font its width is measured
    // in — and a projection change decides whether there is a tuning to name at all. The panel's
    // WIDTH does not depend on which names the tuning states (tabStringLegendBounds).
    refreshLegendColumn();
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
    // Bound to a local so the presence test and every read are provably one object. The answer
    // carries the chart it was derived from, so nothing here re-dereferences m_presented on the
    // strength of this call having succeeded.
    const std::optional<DrawableLane> lane = laneMetrics();
    if (!lane.has_value())
    {
        return;
    }
    const common::ui::TabLaneMetrics& metrics = lane->metrics;
    const common::core::ChartViewState& tab = lane->tab;
    const juce::Rectangle<int> bounds = metrics.bounds;

    // Which form one note draws in, and the only statement of that rule: its ACTUAL ring while
    // the whole-lane reveal is held, while it is selected, or while the CARET stands anywhere
    // inside the ring the note really sounds. Its presented tail otherwise.
    //
    // The selection draws actual because the selection is the thing under scrutiny — and every
    // chart verb settles on a selection change, so deselecting is exactly the moment presentation
    // clips the tail back. The reveal covers what a selection cannot, since placing notes leaves
    // nothing selected (setActualRingReveal carries that argument).
    //
    // THE CARET'S PEEK is the third, and it is what a click on a tail means now that tails are not
    // targets (user ruling 2026-08-30): the click moves the caret to the slot under the pointer,
    // and if that slot lies inside ink the lane is hiding, the ink shows for as long as the caret
    // stays in the ring. Deterministic and keyed on the edit position alone — no timer, no
    // selection touched, nothing latched — so the caret leaving is the whole of what hides it
    // again. It answers "is something here?" honestly while the click goes on doing what clicks in
    // this lane always did.
    //
    // The peek asks ONLY whether the note is sounding here (user ruling 2026-08-30, final): if a
    // note's STORED duration says it rings at the caret at all, it peeks. Onset through actual
    // end, both ends included, and nothing about presentation enters the test — not why ink is
    // missing, not where the drawn ink stopped. The warrant is authoring: a technique typed onto
    // a presentation-hidden tail is legal and forces that tail visible, so authoring must
    // function identically anywhere in the ring, and the reader's question ("is something here?")
    // is the same question at every instant of it.
    //
    // It costs the rule NOTHING to include the drawn stretch, which is why the earlier
    // past-the-ink form was the more complicated one for no gain: the drawn part re-draws
    // identically in either form (presentation touches only where a tail STOPS), so the visible
    // change is exactly the clipped end growing into view — the very thing the caret is asking
    // about. Two boundary problems die with it: a quarter-tail clipped a sixteenth by the next
    // onset now reveals from anywhere along the tail rather than only from the sliver past its
    // ink, and a grid-snapped caret landing exactly on a drawn end is inside the ring like any
    // other position instead of a strictness question.
    //
    // Reads the published selection rather than a copy of it: the indices are the ones the
    // selection ring already draws with, ascending in the tab projection's own note order
    // (ChartEditViewState), so membership is a binary search over the same table.
    //
    // ONE PREDICATE, and everything the reveal decides reads it: which form a note draws in, and
    // whether its reveal-only held-stop satellite is there at all (user ruling 2026-08-31, THE
    // SATELLITE REVEAL). Revealing a note shows the whole truth about it at once, so the two
    // cannot be separate questions — and the rule itself lives in the editor core beside the hit
    // test that must agree with it (core::chartNoteRevealed).
    const auto revealed = [this](std::size_t index) {
        if (m_actual == nullptr)
        {
            return false;
        }
        // Bound once so the presence test and the reads are provably the same object.
        const std::optional<core::ChartCaretViewState>& caret = m_edit.caret;
        std::optional<core::ChartCaretPeek> peek;
        if (caret.has_value())
        {
            peek = core::ChartCaretPeek{.seconds = caret->seconds, .string = caret->string};
        }
        return core::chartNoteRevealed(
            m_actual->notes[index],
            m_actual_ring_reveal,
            std::ranges::binary_search(m_edit.selected_notes, index),
            peek);
    };
    const auto drawn_note =
        [this, &tab, &revealed](std::size_t index) -> const common::core::NoteViewState& {
        // The ACTUAL form is what a revealed note draws in: setState pins the two tables to one
        // length and one order, so the index names the same note in either.
        return m_actual != nullptr && revealed(index) ? m_actual->notes[index] : tab.notes[index];
    };

    // THE SPAN ARM of the same reveal (user ruling 2026-09-04), stated beside the note's because it
    // is the same held modifier and the same selection answering for a different subject: while it
    // is on, or while a span covers a selected note, that span's furniture runs to its MUSICAL
    // CLOSE instead of to the extent rule 12a trimmed for display.
    //
    // A span has no second projected form to swap to — the two forms differ in their notes alone —
    // so what the lane hands the paint core is the answer rather than a note, and the core reads
    // whichever of the span's two ends that answer names. The rule itself lives in the editor core
    // beside the note's (core::chartSpanRevealed), for the same reason: one spelling.
    //
    // Reads the PRESENTED notes for the coverage test, which is exact in either form — presentation
    // moves no onset — and keeps the lambda off m_actual, which may be absent.
    const auto revealed_shape = [this, &tab](std::size_t index) {
        // Bound once so the presence test and the reads are provably the same object.
        const std::optional<core::ChartCaretViewState>& caret = m_edit.caret;
        std::optional<core::ChartCaretPeek> peek;
        if (caret.has_value())
        {
            peek = core::ChartCaretPeek{.seconds = caret->seconds, .string = caret->string};
        }
        return core::chartSpanRevealed(
            tab.shapes[index], m_actual_ring_reveal, tab.notes, m_edit.selected_notes, peek);
    };

    // THE STRING LEGEND'S PANEL IS AN EXCLUSION PLUS A TINT (user ruling 2026-09-03), and this is
    // where the whole of that composition is stated, because the panel is chrome over a lane whose
    // ink comes from three places: the shared paint core, this view's editing overlays, and the
    // canvas beneath.
    //
    // The TINT goes down first, over the canvas's own ink (the waveform) and under everything this
    // lane draws. It is what the panel now IS in place of the opaque ground the legend used to
    // fill: at full strength the column reads exactly as that ground did, and lower settings let
    // the waveform through — the one thing the knob still moves, since notation is gone from the
    // column at every setting rather than quieted.
    //
    // The EXCLUSION is that "gone": one clip statement covering every lane-content mark below —
    // string lines, tails, brackets, heads, chips, and this view's own selection rings, caret and
    // marquee alike. It replaces the string lines' own exclusion inside the paint core, which was
    // the same rule stated on one mark: a mark drawn under the letters says nothing a reader can
    // use, whether its content is its position or not.
    //
    // What draws ABOVE it is the furniture and the letters, below.
    const juce::Rectangle<int> panel = legendBounds();
    common::ui::drawTabStringLegendTint(g, panel, editorTheme().waveform_row_background);

    // Held in an optional rather than a nested block purely so the clip can be released mid-paint
    // without re-indenting every pass under it; ScopedTransparencyLayer is used the same way in
    // the paint core.
    std::optional<juce::Graphics::ScopedSaveState> lane_content_clip;
    if (!panel.isEmpty())
    {
        lane_content_clip.emplace(g);
        g.excludeClipRegion(panel);
    }

    common::ui::paintTabLane(
        g,
        metrics,
        tab,
        m_prefix_max_end_seconds,
        m_prefix_max_shape_end_seconds,
        drawn_note,
        revealed);

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

    // Selected keyframes wear the SAME accent ring, traced on the linked head the paint core drew
    // at that junction — one selection idiom for every selectable, so a selected junction reads
    // exactly as a selected head does. Drawn from the published list, which only holds keyframes
    // that still draw, so a ring can never appear where no head is.
    for (const core::ChartKeyframeRef& selected : m_edit.selected_keyframes)
    {
        if (selected.note_index >= tab.notes.size())
        {
            continue;
        }
        const common::core::NoteViewState& note = drawn_note(selected.note_index);
        if (selected.keyframe_index >= note.slides.size())
        {
            continue;
        }
        const common::core::KeyframeViewState& keyframe = note.slides[selected.keyframe_index];
        const common::ui::TabKeyframeLayout layout =
            common::ui::tabKeyframeLayout(metrics, note, keyframe);
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
    // paint core already draws wherever its span's mark falls, so an authoring dot beside it was a
    // mark for one fact, and the fact was drawn in the wrong place besides. All that is left here
    // is the selection ring, traced on that bracket — the same accent every other selected object
    // wears, on the same silhouette the click resolved.
    //
    // The layout answers with nothing for a hold that resolved into no span, which is precisely
    // the hold the paint core draws no bracket for; ring and mark therefore appear and vanish
    // together with no rule of their own. It answers with nothing for a sounding note too, which
    // is why this pass and the head-ring pass above can share one selection list.
    //
    // The BRACKET wears the edge, traced on its own silhouette (user ruling 2026-08-27, option b):
    // a box around the pair drew accent through the empty centre where no head exists, which read
    // as a ring around nothing. The silhouette comes from the paint core for the head ring's
    // reason — the mark the accent traces is the mark that was drawn.
    for (const std::size_t index : m_edit.selected_notes)
    {
        if (index >= tab.notes.size())
        {
            continue;
        }
        const std::optional<common::ui::TabSilentHoldLayout> layout =
            common::ui::tabSilentHoldLayout(metrics, drawn_note(index));
        if (!layout.has_value())
        {
            continue;
        }
        g.setColour(accent);
        common::ui::strokeTabBracketOutline(
            g, metrics, *layout, overlayRingStroke(layout->box.height));
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
    if (const std::optional<juce::Rectangle<float>> square = caretSquare(*lane))
    {
        const float size = square->getWidth();
        g.setColour(editorTheme().lane_overlay);
        g.drawRoundedRectangle(*square, size / 8.0f, overlayRingStroke(size));
    }

    // The insert ghost: a hollow white ring the size of a note head, at the slot where an insert
    // would land. Round rather than the caret's square so it reads as a note-to-be, not the editing
    // caret; present only while that insert would actually happen (the controller resolves the
    // honesty gate), so it never advertises an insert that no-ops or refuses.
    //
    // A ghost carrying a FRET is a pending typed value rather than the Alt hover's neutral create,
    // so the ring also prints the value: it is the head that value is about to become, and the
    // digit is what makes the provisional entry visibly pending at a slot that has no head yet.
    //
    // Each optional is bound to a local once so its check and every access are provably the same
    // object, which is the shape this file uses wherever a guarantee has to survive a call.
    if (const std::optional<core::ChartInsertGhostViewState>& ghost = m_edit.insert_ghost;
        ghost.has_value() && ghost->slot.string >= 1 && ghost->slot.string <= tab.stringCount())
    {
        const float size = metrics.note_height;
        const float center_x = metrics.x(ghost->slot.seconds);
        const float center_y = metrics.laneY(ghost->slot.string);
        g.setColour(editorTheme().lane_overlay);
        g.drawEllipse(
            center_x - size / 2.0f, center_y - size / 2.0f, size, size, overlayRingStroke(size));
        if (const std::optional<int>& ghost_fret = ghost->fret;
            ghost_fret.has_value() && metrics.draw_text)
        {
            // Through the paint core's own lane font, like every other number on this lane: the
            // provisional digit has to sit on the string line exactly where the committed one
            // will, and that placement is the font's to make and not this file's.
            metrics.fret_font.draw(
                g,
                juce::String{*ghost_fret},
                juce::Rectangle<float>{center_x - size / 2.0f, center_y - size / 2.0f, size, size});
        }
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
                // An entry on the HELD channel wears its box on the satellite, which is where the
                // value it is typing will print — no head sits there, so the box carries none,
                // exactly as the empty-slot insert case does. A held stop whose satellite is not
                // drawn shows nothing, on the same rule that keeps its hit box off the lane.
                if (targets->channel == common::core::ChartStopChannel::Held)
                {
                    if (const std::optional<common::ui::TabHeldStopLayout> satellite =
                            common::ui::tabHeldStopLayout(metrics, note, revealed(index));
                        satellite.has_value())
                    {
                        common::ui::paintTabPendingEntryBox(
                            g,
                            metrics,
                            nullptr,
                            satellite->center_x,
                            satellite->center_y,
                            text,
                            invalid,
                            ink,
                            accent);
                    }
                    continue;
                }
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
            slot != nullptr && slot->string >= 1 && slot->string <= tab.stringCount()
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

    // The lane's content is finished, so the panel's column is settled: everything below draws
    // OVER it.
    lane_content_clip.reset();

    // THE FURNITURE — the hand-shape rails, the capo chip, the fret-hand chips — draws above the
    // panel rather than under it (user ruling 2026-09-03). A span running under the panel is still
    // in force there and a rail cut out of the column would say it had ended; the same goes for a
    // placement whose chip lands in the column. It stays ONE pass called once per paint — only
    // where it sits in the composition moved.
    common::ui::paintTabLaneFurniture(
        g, metrics, tab, m_prefix_max_shape_end_seconds, revealed_shape);

    // THE GOVERNING FRET-HAND PLACEMENT, pinned on the panel exactly as the ruler pins the tempo
    // and time signature governing its own left edge (user ruling 2026-09-03): an FHP is a
    // region-scoped value, so the panel is not only a name column but a current-state column —
    // which string is which, and where the hand is. The ORDINARY chip, drawn through the same one
    // authority every scrolling placement draws through, and simply given the pin's column instead
    // of its own. Which placement (and whether it has yielded to the next one) is
    // refreshPinnedFhp's answer, re-derived when the window moves rather than per paint.
    //
    // In the furniture's own layer, because it IS one of these chips: above the tint and the
    // canvas showing through it, above whatever else the furniture drew, and under the letters
    // like everything else on this lane.
    //
    // Bound to a local so the presence test and the read are provably one object.
    const std::optional<common::core::FhpViewState>& pinned = m_pinned_fhp;
    if (pinned.has_value())
    {
        common::ui::drawTabFhpChip(g, metrics, *pinned, static_cast<float>(panel.getX()));
    }

    // THE STRING LEGEND'S LETTERS, last of everything: each string's own pitch name on its own
    // line. Last because they must stay readable whatever crosses the column — a rail, a chip or
    // the canvas's own ink passing behind them rather than over them is what makes the letters
    // answer "which line is this string?" at any scroll position instead of only where nothing
    // else is drawn.
    common::ui::drawTabStringLegend(g, metrics, tab.open_strings, panel);
}

// The lane's metrics for the state and the bounds as they now stand — WITH the chart they came
// from — or nothing when there is nothing to draw with: no chart, an empty lane, or a timeline with
// no duration to map. THE ONE derivation — paint, the caret mask and the legend column all ask
// this — so a geometry the mask reports can never be one the paint did not draw with, and the
// chart travelling with it is what makes "laneMetrics answered, so m_presented is live" a fact the
// type carries instead of a guard every caller must remember.
std::optional<TabView::DrawableLane> TabView::laneMetrics() const
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const common::core::ChartViewState* const tab = m_presented.get();
    if (tab == nullptr || tab->stringCount() <= 0 || bounds.isEmpty() ||
        m_visible_timeline.duration().seconds <= 0.0)
    {
        return std::nullopt;
    }

    common::ui::TabLaneMetrics metrics = common::ui::makeTabLaneMetrics(
        bounds,
        m_visible_timeline,
        common::core::displayedStringCount(tab->stringCount(), m_minimum_displayed_strings),
        tab->stringCount());
    return DrawableLane{.metrics = std::move(metrics), .tab = *tab};
}

// Re-derives the legend panel at pin zero. Asked of the paint core rather than measured here, so
// the column a scroll repaints, the column the lane's content is excluded from, the column the
// canvas stops its grid at and the column the letters draw in are one rectangle. It measures the
// widest name any tuning could state, which is why it belongs here — on the changes that move it —
// rather than on the paint path.
//
// The pinned chip follows, because every fact that resizes the panel (the bounds, the projection,
// the lane count) either moves the chip or decides whether there is one at all.
void TabView::refreshLegendColumn()
{
    // Bound to a local so the presence test and the reads are provably one object.
    const std::optional<DrawableLane> lane = laneMetrics();
    m_legend_column =
        lane.has_value()
            ? common::ui::tabStringLegendBounds(lane->metrics, lane->tab.open_strings, 0)
            : juce::Rectangle<int>{};
    refreshPinnedFhp(lane);
}

// Re-derives the placement pinned on the panel: the one GOVERNING the window's left edge, unless
// the next placement's own chip has come close enough to take the edge over.
//
// Resolved in COLUMNS rather than in seconds, which is why nothing here inverts the lane's
// time-to-x mapping: placements ascend in time and the mapping is monotonic, so "the last
// placement at or left of the pin" is the same search either way, and the yield law the ruler's
// value rows read is stated in columns to begin with. Its boundary is the pinned chip's own width
// plus the clearance every pinned timeline value keeps, so the pin drops exactly when the incoming
// chip would otherwise crowd it — and that incoming chip, drawn by the furniture pass at its own
// column, scrolls on to the edge and becomes the next pin.
void TabView::refreshPinnedFhp(const std::optional<DrawableLane>& lane)
{
    m_pinned_fhp.reset();
    m_pinned_fhp_column = {};

    if (!lane.has_value() || m_legend_column.isEmpty())
    {
        return;
    }

    const common::ui::TabLaneMetrics& metrics = lane->metrics;
    const std::vector<common::core::FhpViewState>& placements = lane->tab.fret_hand_positions;
    const auto column = [&metrics](const common::core::FhpViewState& fhp) {
        return metrics.x(fhp.seconds);
    };
    const auto pin_x = static_cast<float>(legendBounds().getX());
    // The first placement whose column is strictly right of the pin, so the one before it is the
    // placement in force there — including a placement landing exactly ON the pin, which has
    // arrived and therefore governs.
    const auto incoming = std::ranges::upper_bound(placements, pin_x, std::ranges::less{}, column);
    if (incoming == placements.begin())
    {
        return;
    }

    const common::core::FhpViewState& governing = *std::prev(incoming);
    const juce::Rectangle<int> chip =
        common::ui::tabFhpChipBounds(metrics, governing, 0.0f).getSmallestIntegerContainer();
    std::optional<int> incoming_offset;
    if (incoming != placements.end())
    {
        incoming_offset = juce::roundToInt(column(*incoming) - pin_x);
    }
    if (pinYieldsToIncomingLabel(chip.getWidth(), incoming_offset))
    {
        return;
    }

    m_pinned_fhp = governing;
    m_pinned_fhp_column = chip;
}

// The legend column's local rectangle at the current pin, empty when no legend draws.
juce::Rectangle<int> TabView::legendBounds() const
{
    return m_legend_column.translated(m_visible_content_left, 0);
}

// The pinned chrome's local rectangle at the current pin: the panel plus whatever the pinned chip
// reaches past it, and empty when neither draws.
juce::Rectangle<int> TabView::pinnedChromeBounds() const
{
    return m_legend_column.getUnion(m_pinned_fhp_column).translated(m_visible_content_left, 0);
}

// Resolves the caret square against freshly derived metrics, mirroring paint's derivation so
// the mask always matches the drawn square.
std::optional<juce::Range<float>> TabView::caretMaskYRange() const
{
    // Bound to a local so the presence test and the read are provably one object.
    const std::optional<DrawableLane> lane = laneMetrics();
    if (!lane.has_value())
    {
        return std::nullopt;
    }
    const std::optional<juce::Rectangle<float>> square = caretSquare(*lane);
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
    // The lane's height sets its font, which is what the legend column is measured in.
    refreshLegendColumn();
    publishCaretMask();
}

// The caret square: centered on the caret's slot, one pixel larger than a note head so it
// reads around a head it rides. It is a SLOT, not a note, so the per-note form pick has nothing
// to say about it; the string bound it needs is the presented form's, which is the same count
// the actual form carries.
std::optional<juce::Rectangle<float>> TabView::caretSquare(const DrawableLane& lane) const
{
    if (!m_edit.caret.has_value() || m_edit.caret->string < 1 ||
        m_edit.caret->string > lane.tab.stringCount())
    {
        return std::nullopt;
    }

    const common::ui::TabLaneMetrics& metrics = lane.metrics;
    const float center_y = metrics.laneY(m_edit.caret->string);
    const float x = metrics.x(m_edit.caret->seconds);
    if (m_edit.caret->channel == common::core::ChartStopChannel::Held)
    {
        // The caret is on the note's OTHER stop, so the square marks the mark that states it: the
        // satellite column outboard of the posture bracket's closing bar. Its geometry is the
        // lane's own (TabLaneGeometry::satelliteSlot), the same numbers the digit is drawn in and
        // the pointer is tested against, so the armed square lands exactly on the occupied slot.
        const common::ui::TabBracketGeometry bracket = metrics.bracketGeometry();
        const common::ui::TabSatelliteSlot slot = metrics.satelliteSlot();
        const float bar_right = x + bracket.radius + static_cast<float>(bracket.bar) / 2.0f;
        return juce::Rectangle<float>{
            bar_right,
            center_y - bracket.half_height,
            static_cast<float>(slot.extent()),
            bracket.half_height * 2.0f
        };
    }
    const float size = metrics.headSize();
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
    // The spans' own table, for the paint core's two span passes. Either form serves it — the
    // forms differ in their notes alone — and without it those passes walk the song's whole span
    // prefix on every repaint.
    //
    // Built over the MUSICAL CLOSES for the notes' reason exactly: this lane reveals spans, so the
    // close is the furthest its rails can reach and a table on the drawn extents would cull away a
    // revealed rail still on screen. Named rather than taken off the events, because a span carries
    // two ends and letting the table pick by spelling is how a cull comes to disagree with a paint.
    m_prefix_max_shape_end_seconds =
        furthest_reaching == nullptr
            ? std::vector<double>{}
            : common::core::makeSustainPrefixMax(
                  furthest_reaching->shapes |
                  std::views::transform(&common::core::ShapeViewState::close_seconds));
}

} // namespace rock_hero::editor::ui
