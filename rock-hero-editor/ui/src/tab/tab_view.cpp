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

void TabView::setFretHandChipCallback(FretHandChipCallback callback)
{
    m_fret_hand_chip_callback = std::move(callback);
}

// Adopts the core's published marker-plane availability.
void TabView::setMarkerEditsEnabled(const bool marker_edits_enabled)
{
    m_marker_edits_enabled = marker_edits_enabled;
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

// Holds or releases the ring reveal. A repaint only on a genuine change, because the
// editor re-asserts its foreground-and-Alt predicate every frame for its whole life, so nearly
// every call says what the lane already shows.
void TabView::setRingReveal(bool revealed)
{
    if (revealed == m_ring_reveal)
    {
        return;
    }

    m_ring_reveal = revealed;
    repaint();
}

// With a chart displayed the lane claims its whole band — while paused a click arms the caret
// (which IS the play-from-here position), and while playing the controller turns lane clicks
// into plain seeks, so seeking through the lane keeps working. Without a chart the lane is
// pointer-transparent.
bool TabView::wantsPointerAt(juce::Point<int> local_point) const
{
    return m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->stringCount() > 0 &&
           getLocalBounds().contains(local_point) && m_visible_timeline.duration().seconds > 0.0;
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
// reason: it is a mark pinned over notation the reader cannot see, and it can reach past the
// panel's own edge, so the question is asked of the whole pinned chrome rather than of the letters'
// column alone.
//
// Stated ONCE, here, rather than at each pointer entry point, so no future entry point can answer
// the pinned-chrome question differently from the press.
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
        common::core::displayedStringCount(m_tab->stringCount(), m_minimum_displayed_strings);
    return core::ChartPointerEvent{
        .geometry = common::ui::makeTabLaneGeometry(
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getWidth()),
            static_cast<float>(bounds.getHeight()),
            m_visible_timeline,
            displayed_count,
            m_tab->stringCount()),
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

    // A fret-hand chip is the hand row's marker, and a press on it selects that placement the way a
    // ruler chip click selects its marker — instead of reaching the chart as a press on the top
    // string the chip is drawn over. While the marker plane is closed there is no selection to
    // make, so the press goes to the chart like any other, as a ruler chip column seeks then.
    if (m_marker_edits_enabled && m_fret_hand_chip_callback != nullptr)
    {
        if (const std::optional<std::size_t> chip = fretHandChipAt(event.position);
            chip.has_value())
        {
            m_fret_hand_chip_callback(*chip);
            return;
        }
    }

    m_on_pointer_event(core::ChartPointerPhase::Down, makePointerEvent(event));
}

// The column a placement's chip is drawn at: the pin's for the placement the panel pins (its own
// column lies under the panel there), else its own. The one answer the chip, its selection outline
// and its pending box all read.
float TabView::fretHandChipX(
    const common::ui::TabLaneMetrics& metrics, const common::core::ChartViewState& tab,
    const std::size_t index) const
{
    return m_pinned_fhp == index ? static_cast<float>(legendBounds().getX())
                                 : metrics.x(tab.fret_hand_positions[index].seconds);
}

// Chips sit at their placements' own columns, which ascend with the placements, so only a chip
// starting at or left of the point can contain it. The scan runs back from the last of those — the
// chip drawn on top where two overlap — and stops at the viewport's left edge: a chip starting
// further left reaches into view only under the pinned chrome, where no press is taken, so the scan
// stays bounded by what is on screen however long the song.
std::optional<std::size_t> TabView::fretHandChipAt(const juce::Point<float> local_point) const
{
    // Bound to a local so the presence test and every read are provably one object.
    const std::optional<DrawableLane> lane = laneMetrics();
    if (!lane.has_value())
    {
        return std::nullopt;
    }
    const common::ui::TabLaneMetrics& metrics = lane->metrics;
    const std::vector<common::core::FhpViewState>& placements = lane->tab.fret_hand_positions;
    const auto column = [&metrics](const common::core::FhpViewState& fhp) {
        return metrics.x(fhp.seconds);
    };
    const auto first = std::ranges::lower_bound(
        placements, static_cast<float>(m_visible_content_left), std::ranges::less{}, column);
    auto candidate =
        std::ranges::upper_bound(placements, local_point.x, std::ranges::less{}, column);
    while (candidate > first)
    {
        --candidate;
        if (common::ui::tabFhpChipBounds(metrics, *candidate, column(*candidate))
                .contains(local_point))
        {
            return static_cast<std::size_t>(std::distance(placements.begin(), candidate));
        }
    }
    return std::nullopt;
}

void TabView::mouseDrag(const juce::MouseEvent& event)
{
    // No wantsPointerAt gate: a drag that started inside the lane keeps reporting while the
    // pointer travels outside it, exactly like any JUCE drag capture.
    if (m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->stringCount() > 0)
    {
        m_on_pointer_event(core::ChartPointerPhase::Drag, makePointerEvent(event));
    }
}

void TabView::mouseUp(const juce::MouseEvent& event)
{
    if (m_on_pointer_event != nullptr && m_tab != nullptr && m_tab->stringCount() > 0)
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

// Applies the current tab projection and lane-count preference; the projection pointer only
// changes when the displayed arrangement or the chart revision does, so pointer identity is the
// change test.
void TabView::setState(
    std::shared_ptr<const common::core::ChartViewState> tab, int minimum_displayed_strings)
{
    if (tab == m_tab && minimum_displayed_strings == m_minimum_displayed_strings)
    {
        return;
    }

    m_tab = std::move(tab);
    m_minimum_displayed_strings = minimum_displayed_strings;

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
void TabView::paint(juce::Graphics& g)
{
    // Bound to a local so the presence test and every read are provably one object. The answer
    // carries the chart it was derived from, so nothing here re-dereferences m_tab on the
    // strength of this call having succeeded.
    const std::optional<DrawableLane> lane = laneMetrics();
    if (!lane.has_value())
    {
        return;
    }
    const common::ui::TabLaneMetrics& metrics = lane->metrics;
    const common::core::ChartViewState& tab = lane->tab;
    const juce::Rectangle<int> bounds = metrics.bounds;

    // Whether a note is revealed: the rule lives in the editor core beside the hit test that must
    // agree with it (core::chartNoteRevealed), and this lane only routes its overlay through it.
    // ONE ANSWER, and everything the reveal decides reads it: how far a note is drawn — to its
    // ring's end, every keyframe at its true instant — and whether its reveal-only held-stop
    // satellite is there at all (THE SATELLITE REVEAL). Revealing a note shows the whole truth
    // about it at once, so the two cannot be separate questions.
    const auto revealed = [this, &tab](std::size_t index) {
        return core::chartNoteRevealed(tab.notes, index, m_ring_reveal, m_edit);
    };

    // THE SPAN'S OWN REVEAL (core::chartSpanRevealed), the same grounds answering for a different
    // subject: a revealed span's furniture runs to its MUSICAL CLOSE instead of to the extent rule
    // 12a trimmed for display. What the lane hands the paint core is the answer rather than a
    // span, and the core reads whichever of the span's two ends that answer names.
    const auto revealed_shape = [this, &tab](std::size_t index) {
        return core::chartSpanRevealed(tab.shapes[index], m_ring_reveal, tab.notes, m_edit);
    };

    // THE STRING LEGEND'S PANEL IS AN EXCLUSION PLUS A TINT, and this is where the whole of that
    // composition is stated, because the panel is chrome over a lane whose ink comes from three
    // places: the shared paint core, this view's editing overlays, and the canvas beneath.
    //
    // The TINT goes down first, over the canvas's own ink (the waveform) and under everything this
    // lane draws. It is the panel's ground: at full strength the column reads as an opaque band,
    // and lower settings let the waveform through — the one thing the knob moves, since notation
    // is gone from the column at every setting rather than quieted.
    //
    // The EXCLUSION is that "gone": one clip statement covering every lane-content mark below —
    // string lines, tails, brackets, heads, chips, and this view's own selection rings, caret and
    // marquee alike. One statement rather than a per-mark exclusion inside the paint core, which
    // would be the same rule stated on one mark: a mark drawn under the letters says nothing a
    // reader can use, whether its content is its position or not.
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

    // The rows sit on the theme's row background, which is what the lane's one knockout restores.
    common::ui::paintTabLane(g, metrics, tab, revealed, editorTheme().waveform_row_background);

    // Chart-editing overlays draw above the shared notation and never enter the paint core:
    // they are editor-shell furniture, not part of what the game's tab strips render.
    const juce::Colour accent = editorTheme().accent;

    // Selection highlight: an accent ring straddling the head's outer edge — the stroke is
    // centered on the edge, at one and a half border-widths thick, so it sits between the
    // head's own border ring and the accent glow while leaving the glow annulus readable on
    // accented notes (a fully-outward cut buried the glow, and a double-width stroke still
    // covered too much of it). The silhouette comes from the paint core, so the ring always
    // traces the head that is actually under it — re-deriving the shape here would draw a circle
    // around a head whose silhouette the overlay does not know about, such as the plectrum.
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

    // The head a published keyframe ref names, resolved once for every keyframe overlay — the
    // selection ring here and the pending box below — and only where the lane DRAWS it: a
    // keyframe past its note's ink end is drawn while the note is revealed and not otherwise, so
    // an overlay on it follows the same rule and a mark can never appear where no head is. A ref
    // the projection has since outrun simply yields nothing.
    const auto for_each_drawn_keyframe = [&](const std::vector<core::ChartKeyframeRef>& refs,
                                             const auto& draw) {
        for (const core::ChartKeyframeRef& ref : refs)
        {
            if (ref.note_index >= tab.notes.size())
            {
                continue;
            }
            const common::core::NoteViewState& note = tab.notes[ref.note_index];
            if (ref.keyframe_index >= note.keyframes.size())
            {
                continue;
            }
            const common::core::KeyframeViewState& keyframe = note.keyframes[ref.keyframe_index];
            const double drawn_end = common::core::drawnEndSeconds(note, revealed(ref.note_index));
            if (!common::core::instantDrawn(keyframe.seconds, drawn_end))
            {
                continue;
            }
            draw(note, keyframe, common::ui::tabKeyframeLayout(metrics, note, keyframe, drawn_end));
        }
    };

    // Selected keyframes wear the SAME accent ring, traced on the mark the paint core drew for
    // them — the linked head at a junction or a resting point, the slide-out chip's box at a
    // slide-out, a disc around a bend-only point's dot — one selection idiom for every selectable,
    // so a selected junction reads exactly as a selected head does.
    //
    // THE SELECTED OBJECT DRAWS LAST: a linked head is redrawn here before its ring, because the
    // lane paints notes in chart order and an ARRIVAL sits at the next head's own instant, which
    // would otherwise leave the ring around a mark the charter cannot read. A chip needs nothing —
    // chips already draw above every head — and a selected HEAD keeps drawing over the arrival, as
    // the instant's owner should.
    for_each_drawn_keyframe(
        m_edit.selected_keyframes,
        [&](const common::core::NoteViewState& note,
            const common::core::KeyframeViewState& keyframe,
            const common::ui::TabKeyframeLayout& layout) {
            const common::ui::TabLayoutRect& box = layout.box;
            const juce::Rectangle<float> bounds{box.x, box.y, box.width, box.height};
            switch (layout.shape)
            {
                case common::ui::TabKeyframeShape::Head:
                    common::ui::paintTabKeyframeHead(g, metrics, note, keyframe);
                    g.setColour(accent);
                    common::ui::strokeTabNoteHeadOutline(
                        g,
                        note,
                        layout.center_x,
                        layout.center_y,
                        layout.head_size,
                        overlayRingStroke(layout.head_size));
                    break;
                case common::ui::TabKeyframeShape::Chip:
                    g.setColour(accent);
                    g.drawRect(bounds, overlayRingStroke(layout.head_size));
                    break;
                case common::ui::TabKeyframeShape::Dot:
                    g.setColour(accent);
                    g.drawEllipse(bounds, overlayRingStroke(box.width));
                    break;
            }
        });

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
    // the marker is armed — on an empty slot it marks where a typed digit inserts, on a note or a
    // keyframe it rides the selection highlight so the caret stays visible through a single
    // selection.
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

    // The pending fret entry: the provisional value in its accent-bordered box over each
    // affected head (or at the empty insert slot), red when it cannot apply — every affected
    // head marks together, because a relational refusal has no per-note attribution. Editor
    // chrome like the caret, but drawn through the paint core's one exported primitive so the
    // digit's typography and plate cannot drift from the committed head's. The entry's text and
    // its ink are read ONCE for every mark a box rides — a head, a satellite, a slot, a
    // placement's chip: valid rides the dark plate in the digit's own white; invalid FLIPS the
    // plate to the white ground with the theme's red — the polarity flip is itself the glance
    // signal.
    //
    // Bound to a local so the presence test and every read are provably one object.
    const std::optional<core::ChartPendingFretViewState>& pending = m_edit.pending_fret;
    const bool pending_refused = pending.has_value() && !pending->valid;
    const juce::String pending_text =
        pending.has_value() ? juce::String{pending->text} : juce::String{};
    const juce::Colour pending_ink =
        pending_refused ? editorTheme().invalid : editorTheme().primary_text;
    const auto paint_pending_box = [&](const common::core::NoteViewState* const head,
                                       const float center_x,
                                       const float center_y) {
        common::ui::paintTabPendingEntryBox(
            g,
            metrics,
            head,
            center_x,
            center_y,
            pending_text,
            pending_refused,
            pending_ink,
            accent);
    };
    if (pending.has_value())
    {
        if (const auto* const targets = std::get_if<core::ChartPendingFretTargets>(&pending->at))
        {
            for (const std::size_t index : targets->notes)
            {
                if (index >= tab.notes.size())
                {
                    continue;
                }
                const common::core::NoteViewState& note = tab.notes[index];
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
                        paint_pending_box(nullptr, satellite->center_x, satellite->center_y);
                    }
                    continue;
                }
                const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(metrics, note);
                paint_pending_box(&note, layout.onset_x, layout.center_y);
            }
            // A selected keyframe's box rides the mark the paint core drew for it — the linked
            // head at a junction, which the note places exactly as it does its onset's digit, or
            // the slide-out chip at a slide-out, whose digit sits on the chip's own line with no
            // head shape to raise it — so the digit lands where the value will print. A bend-only
            // point prints no fret yet: the typed one lands as a linked head on the string line
            // at its instant, so that is where its box rides.
            for_each_drawn_keyframe(
                targets->keyframes,
                [&](const common::core::NoteViewState& note,
                    const common::core::KeyframeViewState&,
                    const common::ui::TabKeyframeLayout& layout) {
                    switch (layout.shape)
                    {
                        case common::ui::TabKeyframeShape::Head:
                            paint_pending_box(&note, layout.center_x, layout.center_y);
                            break;
                        case common::ui::TabKeyframeShape::Chip:
                            paint_pending_box(nullptr, layout.center_x, layout.center_y);
                            break;
                        case common::ui::TabKeyframeShape::Dot:
                            paint_pending_box(&note, layout.center_x, metrics.laneY(note.string));
                            break;
                    }
                });
        }
        else if (
            const auto* const slot = std::get_if<core::ChartSlotViewState>(&pending->at);
            slot != nullptr && slot->string >= 1 && slot->string <= tab.stringCount()
        )
        {
            paint_pending_box(nullptr, metrics.x(slot->seconds), metrics.laneY(slot->string));
        }
        // A placement's box rides its chip, which the furniture pass draws later and above the
        // lane content; that box is drawn there, over the chip.
    }

    // The lane's content is finished, so the panel's column is settled: everything below draws
    // OVER it.
    lane_content_clip.reset();

    // THE FURNITURE — the hand-shape rails, the capo chip, the fret-hand chips — draws above the
    // panel rather than under it. A span running under the panel is still in force there and a
    // rail cut out of the column would say it had ended; the same goes for a placement whose chip
    // lands in the column. It is ONE pass called once per paint; only where it sits in the
    // composition distinguishes it from the lane content.
    common::ui::paintTabLaneFurniture(g, metrics, tab, revealed_shape);

    // THE GOVERNING FRET-HAND PLACEMENT, pinned on the panel exactly as the ruler pins the tempo
    // and time signature governing its own left edge: an FHP is a region-scoped value, so the panel
    // is not only a name column but a current-state column — which string is which, and where the
    // hand is. The ORDINARY chip, drawn through the same one authority every scrolling placement
    // draws through, and simply given the pin's column instead of its own (fretHandChipX). Which
    // placement (and whether it has yielded to the next one) is refreshPinnedFhp's answer,
    // re-derived when the window moves rather than per paint.
    //
    // In the furniture's own layer, because it IS one of these chips: above the tint and the
    // canvas showing through it, above whatever else the furniture drew, and under the letters
    // like everything else on this lane.
    //
    // Bound to a local so the presence test and the read are provably one object.
    if (const std::optional<std::size_t>& pinned = m_pinned_fhp;
        pinned.has_value() && *pinned < tab.fret_hand_positions.size())
    {
        common::ui::drawTabFhpChip(
            g, metrics, tab.fret_hand_positions[*pinned], fretHandChipX(metrics, tab, *pinned));
    }

    // A placement's chip WHERE IT IS DRAWN — pinned on the panel or scrolling at its own column
    // (fretHandChipX) — the placement and the box, from the one geometry authority, and nothing for
    // an index the projection has since outrun or a lane that prints no text. The selection
    // outline and the pending box both ride it, so a selected or pending placement is marked
    // wherever it shows, the pinned chip included.
    struct DrawnChip
    {
        const common::core::FhpViewState* fhp{nullptr};
        juce::Rectangle<float> box{};
    };
    const auto drawn_chip = [this, &tab, &metrics](const std::size_t index) {
        if (index >= tab.fret_hand_positions.size())
        {
            return std::optional<DrawnChip>{};
        }
        const common::core::FhpViewState& fhp = tab.fret_hand_positions[index];
        const juce::Rectangle<float> box =
            common::ui::tabFhpChipBounds(metrics, fhp, fretHandChipX(metrics, tab, index));
        return box.isEmpty() ? std::optional<DrawnChip>{}
                             : std::optional{DrawnChip{.fhp = &fhp, .box = box}};
    };

    // THE SELECTED FRET-HAND CHIP wears the accent outline a selected keyframe chip wears — one
    // selection idiom for every boxed mark — traced on the chip where it is drawn. Editor
    // furniture, so it stays out of the game-shared paint core.
    if (const std::optional<std::size_t>& selected = m_edit.selected_fret_hand_position;
        selected.has_value())
    {
        if (const std::optional<DrawnChip> chip = drawn_chip(*selected); chip.has_value())
        {
            g.setColour(accent);
            g.drawRect(chip->box, overlayRingStroke(chip->box.getHeight()));
        }
    }

    // A placement's pending fret entry fills the placement's own chip box, where the value will
    // print, above the chip in the furniture's layer. A value that will apply is already the chip's
    // own — the controller previews it into the projection, derived window included — so the plate
    // carries the chip's text; a refused one previews nothing and carries what was typed.
    if (pending.has_value())
    {
        if (const auto* const hand = std::get_if<core::ChartPendingFretHandPosition>(&pending->at);
            hand != nullptr)
        {
            if (const std::optional<DrawnChip> chip = drawn_chip(hand->index); chip.has_value())
            {
                common::ui::paintTabPendingEntryPlate(
                    g,
                    metrics,
                    metrics.fret_font,
                    chip->box,
                    pending_refused ? pending_text : common::ui::tabFhpChipText(*chip->fhp),
                    pending_refused,
                    pending_ink,
                    accent);
            }
        }
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
// chart travelling with it is what makes "laneMetrics answered, so m_tab is live" a fact the
// type carries instead of a guard every caller must remember.
std::optional<TabView::DrawableLane> TabView::laneMetrics() const
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const common::core::ChartViewState* const tab = m_tab.get();
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

    m_pinned_fhp = static_cast<std::size_t>(std::distance(placements.begin(), incoming) - 1);
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

// The earliest selected note's head, the glyph a verb on the selection keeps on screen.
std::optional<juce::Rectangle<float>> TabView::selectedNoteHeadBounds() const
{
    if (m_edit.selected_notes.empty())
    {
        return std::nullopt;
    }
    // The selection publishes in chart order, so its first index is the earliest member.
    return noteHeadBounds(m_edit.selected_notes.front());
}

// One note's head by projection index; the one place the head-rectangle arithmetic lives.
std::optional<juce::Rectangle<float>> TabView::noteHeadBounds(const std::size_t index) const
{
    const std::optional<DrawableLane> lane = laneMetrics();
    if (!lane.has_value() || index >= lane->tab.notes.size())
    {
        return std::nullopt;
    }
    const common::core::NoteViewState& note = lane->tab.notes[index];
    const common::ui::TabNoteLayout layout = common::ui::tabNoteLayout(lane->metrics, note);
    const float half = layout.head_size / 2.0f;
    return juce::Rectangle<float>{
        layout.onset_x - half, layout.center_y - half, layout.head_size, layout.head_size
    };
}

// The caret square: centered on the caret's slot, one pixel larger than a note head so it
// reads around a head it rides. It is a SLOT, not a note, so the reveal has nothing to say about
// it; it needs only the lane's string bound.
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

} // namespace rock_hero::editor::ui
