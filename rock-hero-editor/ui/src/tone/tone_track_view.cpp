#include "tone_track_view.h"

#include "shared/editor_theme.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_region_vertical_inset{4};
constexpr int g_region_corner_radius{4};
constexpr int g_region_label_inset{8};
constexpr int g_edge_grab_width{6};
const juce::Colour g_tone_region_fill{juce::Colour{0xff2b4a66}};
const juce::Colour g_tone_region_border{editorTheme().accent.withAlpha(0.65f)};
// The active tone (the one the rig plays, following the cursor) uses the brighter accent highlight.
const juce::Colour g_tone_region_active_fill{juce::Colour{0xff35597a}};
const juce::Colour g_tone_region_active_border{editorTheme().accent};
// The formally selected region (a deliberate click, the Delete target) adds a distinct white
// outline on top of the active highlight so it is unmistakable.
const juce::Colour g_tone_region_selection_outline{juce::Colours::white};
const juce::Colour g_tone_region_label{juce::Colours::white.withAlpha(0.92f)};

// Region names may be empty in the data model; the row still labels every region.
[[nodiscard]] juce::String regionLabel(const core::ToneRegionViewState& region)
{
    return region.name.empty() ? juce::String{"Tone"} : juce::String{region.name};
}

} // namespace

ToneTrackView::ToneTrackView(
    Listener& listener, const common::core::TempoMap& tempo_map,
    const common::audio::ITransport& transport)
    : m_listener(listener)
    , m_tempo_map(tempo_map)
    , m_transport(transport)
    , m_vblank_attachment(this, [this] { advanceActiveRegion(); })
{}

void ToneTrackView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }

    m_visible_timeline = visible_timeline;
    repaint();
}

void ToneTrackView::setState(const core::ToneTrackViewState& state)
{
    if (m_state == state)
    {
        return;
    }

    m_state = state;
    // Controller pushes replace the model under any in-flight gesture, so drop the gesture
    // instead of resizing against stale indices.
    m_drag.reset();
    m_insert_drag.reset();
    m_insert_ghost_x.reset();
    m_pending_select.reset();
    emitSnapGuide(std::nullopt);
    repaint();
}

void ToneTrackView::setVisibleContentLeft(int content_left_x)
{
    if (m_visible_content_left == content_left_x)
    {
        return;
    }

    // The viewport scrolls this row's pixels without repainting, so a repaint is required to
    // redraw region labels at their new pinned position.
    m_visible_content_left = content_left_x;
    repaint();
}

void ToneTrackView::setGridNoteValue(common::core::Fraction note_value)
{
    m_grid_note_value = note_value;
}

void ToneTrackView::setSnapGuideCallback(SnapGuideCallback on_snap_guide)
{
    m_on_snap_guide = std::move(on_snap_guide);
}

bool ToneTrackView::wantsPointerAt(juce::Point<int> local_point) const
{
    return hitAt(local_point).has_value();
}

void ToneTrackView::paint(juce::Graphics& g)
{
    // The row draws no background of its own: the canvas paints its band and the shared
    // tempo grid beneath this component (see the track-row layering contract in
    // TrackViewport::Content::paint), so the grid stays visible between regions as the
    // alignment cue the beat snapping targets.
    const auto bounds = getLocalBounds();

    // Fills and borders draw clip-clamped: this row spans the whole zoomed content (hundreds of
    // thousands of pixels at high zoom), and Windows' Direct2D peer renderer — single-precision
    // on the GPU — drops parts of rounded-rectangle strokes at those coordinate magnitudes,
    // leaving an active/selected region with only fragments of its border. A clamped synthetic
    // edge sits at least a corner radius plus stroke-and-antialiasing slack outside the clip, so
    // its corner arcs and edge stroke never reach rendered pixels: the visible result is
    // identical to the unclamped path while the coordinates stay small. (The lanes view builds
    // its curve path clip-locally for the same reason.)
    const juce::Rectangle<int> clip = g.getClipBounds();
    const float clip_pad = static_cast<float>(g_region_corner_radius) + 4.0f;
    const float clip_left = static_cast<float>(clip.getX()) - clip_pad;
    const float clip_right = static_cast<float>(clip.getRight()) + clip_pad;

    for (std::size_t index = 0; index < m_state.regions.size(); ++index)
    {
        const core::ToneRegionViewState& region = m_state.regions[index];
        common::core::TimeRange span = region.time_range;
        if (m_drag.has_value())
        {
            // Preview the live boundary on BOTH sides so the two regions move in sync under the
            // cursor: the dragged region's edge follows the pointer, and its neighbor across the
            // shared boundary follows the same position.
            const auto seconds_at = [this](const common::core::GridPosition& grid) {
                return common::core::TimePosition{m_tempo_map.secondsAtNote(
                    grid.measure, grid.beat, grid.offset)};
            };
            if (m_drag->region_index == index)
            {
                span.start = seconds_at(m_drag->preview_start);
                span.end = seconds_at(m_drag->preview_end);
            }
            else if (m_drag->edge == EdgeKind::End && index == m_drag->region_index + 1)
            {
                span.start = seconds_at(m_drag->preview_end);
            }
            else if (
                m_drag->edge == EdgeKind::Start && m_drag->region_index > 0 &&
                index == m_drag->region_index - 1
            )
            {
                span.end = seconds_at(m_drag->preview_start);
            }
        }

        // Round both edges to the grid's pixel column (gridAlignedX) so a boundary sits exactly on
        // the grid line it was snapped to — and on the same pixel the insert ghost previewed —
        // rather than the fraction beside it. gridAlignedX uses getWidth(), which is bounds' width
        // here (bounds is getLocalBounds()).
        const std::optional<float> start_x = gridAlignedX(span.start);
        const std::optional<float> end_x = gridAlignedX(span.end);
        if (!start_x.has_value() || !end_x.has_value() || *end_x <= *start_x)
        {
            continue;
        }

        // The true span drives label pinning below so text layout stays clip-independent; only
        // the fill/border geometry clamps to the clip neighborhood. A region entirely outside
        // the clip contributes no pixels (its label lives inside its span), so it skips whole.
        const float draw_left = std::max(*start_x, clip_left);
        const float draw_right = std::min(*end_x, clip_right);
        if (draw_right <= draw_left)
        {
            continue;
        }

        const juce::Rectangle<float> region_bounds{
            *start_x,
            static_cast<float>(g_region_vertical_inset),
            *end_x - *start_x,
            static_cast<float>(std::max(1, bounds.getHeight() - (g_region_vertical_inset * 2))),
        };
        const juce::Rectangle<float> draw_bounds{
            draw_left,
            region_bounds.getY(),
            draw_right - draw_left,
            region_bounds.getHeight(),
        };

        // The active tone gets the brighter highlight; a formal selection adds a white outline.
        g.setColour(region.active ? g_tone_region_active_fill : g_tone_region_fill);
        g.fillRoundedRectangle(draw_bounds, static_cast<float>(g_region_corner_radius));

        g.setColour(region.active ? g_tone_region_active_border : g_tone_region_border);
        g.drawRoundedRectangle(
            draw_bounds, static_cast<float>(g_region_corner_radius), region.active ? 2.0f : 1.2f);

        // A formal selection is always also active, so its white outline overpaints the accent
        // border on the exact same edge (same bounds and stroke) rather than sitting inside it.
        if (region.selected)
        {
            g.setColour(g_tone_region_selection_outline);
            g.drawRoundedRectangle(draw_bounds, static_cast<float>(g_region_corner_radius), 2.0f);
        }

        // Pin the label to the visible left edge while the region still covers it (like the pinned
        // tempo/time-signature ruler), clipped to the region so it slides off only as the region
        // itself leaves. m_visible_content_left is this row's content x of the viewport left edge.
        const float pinned_left =
            std::max(region_bounds.getX(), static_cast<float>(m_visible_content_left));
        const juce::Rectangle<float> label_area{
            pinned_left,
            region_bounds.getY(),
            region_bounds.getRight() - pinned_left,
            region_bounds.getHeight(),
        };
        const auto label_bounds =
            label_area.reduced(static_cast<float>(g_region_label_inset), 0.0f).toNearestInt();
        if (label_bounds.getWidth() > 0)
        {
            g.setColour(g_tone_region_label);
            g.setFont(juce::FontOptions{13.0f});
            g.drawText(regionLabel(region), label_bounds, juce::Justification::centredLeft, true);
        }
    }

    // The Alt-held insert ghost: the boundary a click (or the in-flight placement's release) would
    // create, visible before anything mutates. Drawn as a 1px column fill at the grid's own integer
    // x — matching the tempo grid's 1px dots — rather than a 1.5px centered drawLine, which smears
    // across the column boundary (antialiased over ~2px) and reads as a fat line sitting a pixel off
    // the crisp grid dots even when its center is on the column. The fill occupies [x, x + 1), the
    // exact column the grid dots occupy, so the preview lands on the grid line.
    if (m_insert_ghost_x.has_value())
    {
        g.setColour(editorTheme().accent.withAlpha(0.7f));
        const auto top = static_cast<float>(g_region_vertical_inset);
        const auto bottom = static_cast<float>(bounds.getHeight() - g_region_vertical_inset);
        g.fillRect(*m_insert_ghost_x, top, 1.0f, bottom - top);
    }
}

void ToneTrackView::mouseMove(const juce::MouseEvent& event)
{
    const std::optional<RegionHit> hit = hitAt(event.getPosition());

    // Alt over a region body previews the tone change a click would insert: a ghost boundary
    // line at the snapped position, under the arrow-with-plus copy cursor.
    std::optional<float> ghost_x;
    if (hit.has_value() && !hit->edge.has_value() && event.mods.isAltDown())
    {
        if (const std::optional<common::core::GridPosition> position = insertPositionForX(
                static_cast<float>(event.getPosition().x), hit->region_index, event.mods);
            position.has_value())
        {
            ghost_x = xForGridPosition(*position);
        }
    }
    setInsertGhostX(ghost_x);

    if (ghost_x.has_value())
    {
        setMouseCursor(juce::MouseCursor::CopyingCursor);
    }
    else if (hit.has_value() && hit->edge.has_value())
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (hit.has_value())
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void ToneTrackView::mouseExit(const juce::MouseEvent& /*event*/)
{
    // A drag keeps its preview (JUCE keeps delivering drag events); a plain hover that leaves
    // the row clears the ghost.
    if (!m_insert_drag.has_value())
    {
        setInsertGhostX(std::nullopt);
    }
}

void ToneTrackView::mouseDown(const juce::MouseEvent& event)
{
    m_drag.reset();
    m_insert_drag.reset();
    m_pending_select.reset();

    const std::optional<RegionHit> hit = hitAt(event.getPosition());
    if (!hit.has_value())
    {
        return;
    }

    const core::ToneRegionViewState& region = m_state.regions[hit->region_index];

    // A right-click (or track-pad equivalent) on a region opens its context menu; the menu's
    // insert entry uses the click's own snapped position so it mirrors the Alt-click gesture.
    if (event.mods.isPopupMenu())
    {
        const std::optional<common::core::GridPosition> insert_position =
            hit->edge.has_value() ? std::nullopt
                                  : insertPositionForX(
                                        static_cast<float>(event.getPosition().x),
                                        hit->region_index,
                                        juce::ModifierKeys{});
        showRegionContextMenu(region, insert_position);
        return;
    }

    // Alt on a region body starts the insert placement: press-drag-release places the ghost
    // boundary and commits the tone-change intent on release (a plain click commits in place).
    if (!hit->edge.has_value() && event.mods.isAltDown())
    {
        if (const std::optional<common::core::GridPosition> position = insertPositionForX(
                static_cast<float>(event.getPosition().x), hit->region_index, event.mods);
            position.has_value())
        {
            m_insert_drag =
                InsertDragState{.region_index = hit->region_index, .preview = *position};
            setInsertGhostX(xForGridPosition(*position));
        }
        return;
    }

    if (hit->edge.has_value())
    {
        m_drag = DragState{
            .region_index = hit->region_index,
            .edge = *hit->edge,
            .preview_start = region.grid_start,
            .preview_end = region.grid_end,
        };
        return;
    }

    m_pending_select = hit->region_index;
}

// Opens the region's right-click context menu, mirroring every pointer gesture: insert-here (the
// Alt-click equivalent, when the click position can split the region), rename (the double-click
// shortcut), and delete (the Delete-key equivalent).
void ToneTrackView::showRegionContextMenu(
    const core::ToneRegionViewState& region,
    std::optional<common::core::GridPosition> insert_position)
{
    juce::PopupMenu menu;
    if (insert_position.has_value())
    {
        menu.addItem(3, "Insert Tone Change Here");
    }
    // The synthesized default region has no catalog tone to rename or delete.
    if (!region.tone_document_ref.empty())
    {
        menu.addItem(1, "Rename");
        if (!region.id.empty())
        {
            menu.addItem(2, "Delete");
        }
    }
    if (menu.getNumItems() == 0)
    {
        return;
    }
    menu.showMenuAsync(
        // Force a cancel result if this view is deleted while the menu is open, so the callback
        // never dereferences a dangling listener (JUCE reports result 0 for a deleted watch target).
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this, ref = region.tone_document_ref, name = region.name, id = region.id, insert_position](
            int result) {
            if (result == 1)
            {
                m_listener.onToneRenamePromptRequested(ref, name);
            }
            else if (result == 2)
            {
                m_listener.onToneRegionDeleteRequested(id);
            }
            else if (result == 3 && insert_position.has_value())
            {
                m_listener.onToneChangeInsertRequested(*insert_position);
            }
        });
}

void ToneTrackView::mouseDrag(const juce::MouseEvent& event)
{
    // An insert placement drags its ghost boundary; holding it inside the pressed region keeps
    // the release's split target unambiguous.
    if (m_insert_drag.has_value())
    {
        if (const std::optional<common::core::GridPosition> position = insertPositionForX(
                static_cast<float>(event.getPosition().x), m_insert_drag->region_index, event.mods);
            position.has_value())
        {
            m_insert_drag->preview = *position;
            const std::optional<float> ghost_x = xForGridPosition(*position);
            setInsertGhostX(ghost_x);
            if (ghost_x.has_value())
            {
                // Format the local position (just stored as the preview) rather than re-reading
                // m_insert_drag: the guard at the top of the branch is what proves it engaged.
                emitSnapGuide(
                    TimelineSnapGuide{
                        .x = static_cast<float>(getX()) + *ghost_x,
                        .label = juce::String{common::core::formatGridPositionToken(*position)},
                    });
            }
        }
        return;
    }

    if (!m_drag.has_value())
    {
        return;
    }

    const std::optional<common::core::GridPosition> snapped =
        snappedGridPositionForDrag(static_cast<float>(event.getPosition().x), event.mods);
    if (!snapped.has_value())
    {
        // The snapped grid line fell outside the open interval that keeps both neighbors non-empty;
        // hold the last valid preview so the boundary simply stops at that grid line.
        return;
    }

    if (m_drag->edge == EdgeKind::Start)
    {
        m_drag->preview_start = *snapped;
    }
    else
    {
        m_drag->preview_end = *snapped;
    }

    const double guide_seconds =
        m_tempo_map.secondsAtNote(snapped->measure, snapped->beat, snapped->offset);
    const std::optional<float> guide_x = core::timelineXForPosition(
        common::core::TimePosition{guide_seconds},
        m_visible_timeline,
        getWidth(),
        core::TimelinePositionClamping::ClampToVisibleRange);
    if (guide_x.has_value())
    {
        emitSnapGuide(
            TimelineSnapGuide{
                .x = static_cast<float>(getX()) + *guide_x,
                .label = juce::String{common::core::formatGridPositionToken(*snapped)},
            });
    }

    repaint();
}

void ToneTrackView::mouseUp(const juce::MouseEvent& event)
{
    if (m_insert_drag.has_value())
    {
        const InsertDragState insert = *m_insert_drag;
        m_insert_drag.reset();
        setInsertGhostX(std::nullopt);
        emitSnapGuide(std::nullopt);
        m_listener.onToneChangeInsertRequested(insert.preview);
        return;
    }

    if (m_drag.has_value())
    {
        const DragState drag = *m_drag;
        m_drag.reset();
        emitSnapGuide(std::nullopt);
        repaint();

        const core::ToneRegionViewState& region = m_state.regions[drag.region_index];
        if (drag.preview_start == region.grid_start && drag.preview_end == region.grid_end)
        {
            return;
        }

        // The dragged edge is an interior boundary shared with a neighbor; move both sides together
        // by emitting a boundary intent keyed by the region on the later side of the boundary.
        if (drag.edge == EdgeKind::End && drag.region_index + 1 < m_state.regions.size())
        {
            m_listener.onToneBoundaryMoveRequested(
                m_state.regions[drag.region_index + 1].id, drag.preview_end);
        }
        else if (drag.edge == EdgeKind::Start && drag.region_index > 0)
        {
            m_listener.onToneBoundaryMoveRequested(region.id, drag.preview_start);
        }
        return;
    }

    if (m_pending_select.has_value() && event.mouseWasClicked())
    {
        const std::size_t region_index = *m_pending_select;
        m_pending_select.reset();
        m_listener.onToneRegionSelected(m_state.regions[region_index].id);
        return;
    }

    m_pending_select.reset();
}

void ToneTrackView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const std::optional<RegionHit> hit = hitAt(event.getPosition());
    if (!hit.has_value())
    {
        return;
    }

    const core::ToneRegionViewState& region = m_state.regions[hit->region_index];
    if (region.tone_document_ref.empty())
    {
        return; // The synthesized default region has no catalog tone to rename.
    }
    m_listener.onToneRenamePromptRequested(region.tone_document_ref, region.name);
}

// Maps one region's span to component x coordinates, or empty when unmappable.
std::optional<std::pair<float, float>> ToneTrackView::regionXSpan(
    const core::ToneRegionViewState& region) const
{
    const std::optional<float> start_x = core::timelineXForPosition(
        region.time_range.start,
        m_visible_timeline,
        getWidth(),
        core::TimelinePositionClamping::ClampToVisibleRange);
    const std::optional<float> end_x = core::timelineXForPosition(
        region.time_range.end,
        m_visible_timeline,
        getWidth(),
        core::TimelinePositionClamping::ClampToVisibleRange);
    if (!start_x.has_value() || !end_x.has_value() || *end_x <= *start_x)
    {
        return std::nullopt;
    }

    return std::pair{*start_x, *end_x};
}

// Resolves the authored region (and optionally its edge) under a local position.
std::optional<ToneTrackView::RegionHit> ToneTrackView::hitAt(juce::Point<int> local_point) const
{
    if (local_point.y < 0 || local_point.y >= getHeight())
    {
        return std::nullopt;
    }

    const auto x = static_cast<float>(local_point.x);
    for (std::size_t index = 0; index < m_state.regions.size(); ++index)
    {
        const core::ToneRegionViewState& region = m_state.regions[index];
        const std::optional<std::pair<float, float>> span = regionXSpan(region);
        if (!span.has_value())
        {
            continue;
        }

        const auto grab = static_cast<float>(g_edge_grab_width);
        // Only interior boundaries are draggable; the first region's start (song start) and the last
        // region's end (terminal) are pinned, so every draggable edge is a boundary shared with a
        // neighbor and moving it keeps coverage gap-free.
        if (index > 0 && std::abs(x - span->first) <= grab)
        {
            return RegionHit{.region_index = index, .edge = EdgeKind::Start};
        }
        if (index + 1 < m_state.regions.size() && std::abs(x - span->second) <= grab)
        {
            return RegionHit{.region_index = index, .edge = EdgeKind::End};
        }
        if (x > span->first && x < span->second)
        {
            return RegionHit{.region_index = index, .edge = std::nullopt};
        }
    }

    return std::nullopt;
}

// Resolves an x to the snapped position a tone change would be inserted at (sub-beat, Ctrl
// bypasses to the fine grid), accepted only strictly inside the given region: a change landing on
// an existing boundary would split nothing, so it resolves to empty.
std::optional<common::core::GridPosition> ToneTrackView::insertPositionForX(
    float x, std::size_t region_index, const juce::ModifierKeys& mods) const
{
    const std::optional<common::core::GridPosition> snapped = musicalGridPositionForX(
        m_tempo_map, m_grid_note_value, m_visible_timeline, getWidth(), x, mods);
    if (!snapped.has_value() || region_index >= m_state.regions.size())
    {
        return std::nullopt;
    }
    const core::ToneRegionViewState& region = m_state.regions[region_index];
    if (!(region.grid_start < *snapped) || !(*snapped < region.grid_end))
    {
        return std::nullopt;
    }
    return snapped;
}

// Rounds timelineXForPosition to the whole pixel the tempo grid draws its lines on
// (visibleTempoGridLines rounds the same mapping to an int column). The row spans the full-width
// canvas at x 0, so a row-local rounded x coincides with the grid column drawn behind it; anything
// meant to sit on a grid line — region boundaries, the insert ghost — goes through here so it lands
// on the grid's pixel instead of the fraction beside it that the raw float mapping produces.
std::optional<float> ToneTrackView::gridAlignedX(common::core::TimePosition position) const
{
    const std::optional<float> x = core::timelineXForPosition(
        position,
        m_visible_timeline,
        getWidth(),
        core::TimelinePositionClamping::ClampToVisibleRange);
    if (!x.has_value())
    {
        return std::nullopt;
    }
    return std::round(*x);
}

// Maps a musical position to this row's grid-aligned x coordinate, when the geometry allows it.
std::optional<float> ToneTrackView::xForGridPosition(
    const common::core::GridPosition& position) const
{
    return gridAlignedX(
        common::core::TimePosition{m_tempo_map.secondsAtNote(
            position.measure, position.beat, position.offset)});
}

// Sets or clears the ghost boundary line, repainting only the narrow strips it moves between.
void ToneTrackView::setInsertGhostX(std::optional<float> ghost_x)
{
    if (m_insert_ghost_x == ghost_x)
    {
        return;
    }
    const auto repaint_strip = [this](const std::optional<float>& strip_x) {
        if (strip_x.has_value())
        {
            repaint(static_cast<int>(*strip_x) - 2, 0, 5, getHeight());
        }
    };
    repaint_strip(m_insert_ghost_x);
    m_insert_ghost_x = ghost_x;
    repaint_strip(m_insert_ghost_x);
}

bool ToneTrackView::cancelActiveGesture()
{
    if (!m_drag.has_value() && !m_insert_drag.has_value())
    {
        return false;
    }
    m_drag.reset();
    m_insert_drag.reset();
    setInsertGhostX(std::nullopt);
    emitSnapGuide(std::nullopt);
    repaint();
    return true;
}

// Snaps a drag x to the tempo grid (sub-beat, Ctrl bypasses to the fine grid, exactly as the
// automation lanes below), then accepts it only inside the open interval that keeps both regions
// sharing the dragged boundary non-empty. Every draggable edge is an interior boundary (the first
// region's start and the last region's end are pinned, so hit-testing never arms them).
std::optional<common::core::GridPosition> ToneTrackView::snappedGridPositionForDrag(
    float x, const juce::ModifierKeys& mods) const
{
    if (!m_drag.has_value())
    {
        return std::nullopt;
    }

    const std::optional<common::core::GridPosition> snapped = musicalGridPositionForX(
        m_tempo_map, m_grid_note_value, m_visible_timeline, getWidth(), x, mods);
    if (!snapped.has_value())
    {
        return std::nullopt;
    }

    const core::ToneRegionViewState& region = m_state.regions[m_drag->region_index];
    // Start edge: the boundary sits between the previous region's start and this region's end.
    // End edge: between this region's start and the next region's end. The relevant neighbor always
    // exists for a draggable edge, so the bounds below are the fixed endpoints of the two spans that
    // share the boundary.
    common::core::GridPosition lower;
    common::core::GridPosition upper;
    if (m_drag->edge == EdgeKind::Start)
    {
        lower = m_state.regions[m_drag->region_index - 1].grid_start;
        upper = region.grid_end;
    }
    else
    {
        lower = region.grid_start;
        upper = m_state.regions[m_drag->region_index + 1].grid_end;
    }

    if (!(lower < *snapped) || !(*snapped < upper))
    {
        return std::nullopt;
    }
    return snapped;
}

// Recomputes which region contains the sampled playhead. Runs at render cadence like the
// cursor overlay; when the playhead crosses into a different region, the row emits one
// discrete selection intent so the single tone selection follows the cursor. Per-frame
// positions never route through the controller.
void ToneTrackView::advanceActiveRegion()
{
    const double position = m_transport.position().seconds;
    std::optional<std::size_t> active;
    for (std::size_t index = 0; index < m_state.regions.size(); ++index)
    {
        const core::ToneRegionViewState& region = m_state.regions[index];
        if (position >= region.time_range.start.seconds && position < region.time_range.end.seconds)
        {
            active = index;
            break;
        }
    }

    if (active == m_active_region_index)
    {
        return;
    }

    m_active_region_index = active;
    // Route boundary crossings through the "activate" intent so the tone follows the cursor without
    // a formal selection. Skip when the crossed region is already active to avoid redundant work.
    if (active.has_value() && !m_state.regions[*active].active)
    {
        m_listener.onToneRegionActivated();
    }
}

// Reports the current snap guide, or clears it with an empty value.
void ToneTrackView::emitSnapGuide(std::optional<TimelineSnapGuide> guide)
{
    if (m_on_snap_guide)
    {
        m_on_snap_guide(std::move(guide));
    }
}

} // namespace rock_hero::editor::ui
