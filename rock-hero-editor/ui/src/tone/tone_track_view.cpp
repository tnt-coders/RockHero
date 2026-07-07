#include "tone_track_view.h"

#include "shared/editor_theme.h"

#include <algorithm>
#include <cmath>
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
const juce::Colour g_tone_region_selected_fill{juce::Colour{0xff35597a}};
const juce::Colour g_tone_region_selected_border{editorTheme().accent};
const juce::Colour g_tone_region_label{juce::Colours::white.withAlpha(0.92f)};
// The synthesized legacy-default region reads as a passive continuation, not authored content.
const juce::Colour g_default_region_fill{juce::Colours::white.withAlpha(0.05f)};
const juce::Colour g_default_region_border{juce::Colours::white.withAlpha(0.25f)};
const juce::Colour g_default_region_label{juce::Colours::white.withAlpha(0.55f)};

// Region names may be empty in the data model; the row still labels every region.
[[nodiscard]] juce::String regionLabel(const core::ToneRegionViewState& region)
{
    return region.name.empty() ? juce::String{"Tone"} : juce::String{region.name};
}

// Formats the "<measure>:<beat>" readout shown beside the snap guide.
[[nodiscard]] juce::String beatReadout(int measure, int beat)
{
    return juce::String{measure} + ":" + juce::String{beat};
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

    for (std::size_t index = 0; index < m_state.regions.size(); ++index)
    {
        const core::ToneRegionViewState& region = m_state.regions[index];
        common::core::TimeRange span = region.time_range;
        if (m_drag.has_value())
        {
            // Preview the live boundary on BOTH sides so the two regions move in sync under the
            // cursor: the dragged region's edge follows the pointer, and its neighbor across the
            // shared boundary follows the same position.
            const auto seconds_at = [this](const common::core::ToneGridPosition& grid) {
                return common::core::TimePosition{m_tempo_map.secondsAtBeat(
                    grid.measure, grid.beat)};
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

        const std::optional<float> start_x = core::timelineXForPosition(
            span.start,
            m_visible_timeline,
            bounds.getWidth(),
            core::TimelinePositionClamping::ClampToVisibleRange);
        const std::optional<float> end_x = core::timelineXForPosition(
            span.end,
            m_visible_timeline,
            bounds.getWidth(),
            core::TimelinePositionClamping::ClampToVisibleRange);
        if (!start_x.has_value() || !end_x.has_value() || *end_x <= *start_x)
        {
            continue;
        }

        const juce::Rectangle<float> region_bounds{
            *start_x,
            static_cast<float>(g_region_vertical_inset),
            *end_x - *start_x,
            static_cast<float>(std::max(1, bounds.getHeight() - (g_region_vertical_inset * 2))),
        };

        const bool is_default = region.synthesized_default;
        const bool highlighted = region.selected;
        if (is_default)
        {
            g.setColour(g_default_region_fill);
        }
        else
        {
            g.setColour(highlighted ? g_tone_region_selected_fill : g_tone_region_fill);
        }
        g.fillRoundedRectangle(region_bounds, static_cast<float>(g_region_corner_radius));

        if (is_default)
        {
            g.setColour(g_default_region_border);
        }
        else
        {
            g.setColour(highlighted ? g_tone_region_selected_border : g_tone_region_border);
        }
        g.drawRoundedRectangle(
            region_bounds, static_cast<float>(g_region_corner_radius), highlighted ? 2.0f : 1.2f);

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
            g.setColour(is_default ? g_default_region_label : g_tone_region_label);
            g.setFont(juce::FontOptions{13.0f});
            g.drawText(regionLabel(region), label_bounds, juce::Justification::centredLeft, true);
        }
    }
}

void ToneTrackView::mouseMove(const juce::MouseEvent& event)
{
    const std::optional<RegionHit> hit = hitAt(event.getPosition());
    if (hit.has_value() && hit->edge.has_value())
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

void ToneTrackView::mouseDown(const juce::MouseEvent& event)
{
    m_drag.reset();
    m_pending_select.reset();

    const std::optional<RegionHit> hit = hitAt(event.getPosition());
    if (!hit.has_value())
    {
        return;
    }

    const core::ToneRegionViewState& region = m_state.regions[hit->region_index];

    // A right-click (or track-pad equivalent) on a region body opens its context menu.
    if (event.mods.isPopupMenu())
    {
        showRegionContextMenu(region);
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

// Opens the region's right-click context menu. Rename mirrors the double-click quick shortcut.
void ToneTrackView::showRegionContextMenu(const core::ToneRegionViewState& region)
{
    if (region.tone_document_ref.empty())
    {
        return; // The synthesized default region has no catalog tone to act on.
    }

    juce::PopupMenu menu;
    menu.addItem(1, "Rename");
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(this),
        [this, ref = region.tone_document_ref, name = region.name](int result) {
            if (result == 1)
            {
                m_listener.onToneRenamePromptRequested(ref, name);
            }
        });
}

void ToneTrackView::mouseDrag(const juce::MouseEvent& event)
{
    if (!m_drag.has_value())
    {
        return;
    }

    const std::optional<std::int64_t> snapped_beat =
        snappedBeatForDrag(static_cast<float>(event.getPosition().x));
    if (!snapped_beat.has_value())
    {
        return;
    }

    const auto [measure, beat] = m_tempo_map.beatAtGlobalIndex(*snapped_beat);
    const common::core::ToneGridPosition snapped{.measure = measure, .beat = beat};
    if (m_drag->edge == EdgeKind::Start)
    {
        m_drag->preview_start = snapped;
    }
    else
    {
        m_drag->preview_end = snapped;
    }

    const double guide_seconds =
        m_tempo_map.secondsAtGlobalBeatPosition(static_cast<double>(*snapped_beat));
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
                .label = beatReadout(measure, beat),
            });
    }

    repaint();
}

void ToneTrackView::mouseUp(const juce::MouseEvent& event)
{
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
        if (region.synthesized_default)
        {
            continue;
        }

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

// Converts a drag x coordinate into the snapped, clamped global beat for the active edge.
std::optional<std::int64_t> ToneTrackView::snappedBeatForDrag(float x) const
{
    if (!m_drag.has_value())
    {
        return std::nullopt;
    }

    const std::optional<common::core::TimePosition> position =
        core::timelinePositionForX(x, m_visible_timeline, getWidth());
    if (!position.has_value())
    {
        return std::nullopt;
    }

    const core::ToneRegionViewState& region = m_state.regions[m_drag->region_index];
    const std::int64_t own_start =
        m_tempo_map.globalBeatIndex(region.grid_start.measure, region.grid_start.beat);
    const std::int64_t own_end =
        m_tempo_map.globalBeatIndex(region.grid_end.measure, region.grid_end.beat);

    std::int64_t min_beat = 0;
    std::int64_t max_beat = m_tempo_map.terminalGlobalBeatIndex();
    if (m_drag->edge == EdgeKind::Start)
    {
        max_beat = own_end - 1;
        if (m_drag->region_index > 0)
        {
            const core::ToneRegionViewState& previous = m_state.regions[m_drag->region_index - 1];
            // Boundary semantics: the shared boundary may move left into the previous region, down
            // to one beat after its start, so neither region ever becomes empty.
            min_beat =
                m_tempo_map.globalBeatIndex(previous.grid_start.measure, previous.grid_start.beat) +
                1;
        }
    }
    else
    {
        min_beat = own_start + 1;
        if (m_drag->region_index + 1 < m_state.regions.size())
        {
            const core::ToneRegionViewState& next = m_state.regions[m_drag->region_index + 1];
            // Boundary semantics: the shared boundary may move right into the next region, up to one
            // beat before its end, so neither region ever becomes empty.
            max_beat = m_tempo_map.globalBeatIndex(next.grid_end.measure, next.grid_end.beat) - 1;
        }
    }

    const auto snapped = static_cast<std::int64_t>(
        std::llround(m_tempo_map.beatPositionAtSeconds(position->seconds)));
    return std::clamp(snapped, min_beat, max_beat);
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
        if (!region.synthesized_default && position >= region.time_range.start.seconds &&
            position < region.time_range.end.seconds)
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
    if (active.has_value() && !m_state.regions[*active].selected)
    {
        m_listener.onToneRegionSelected(m_state.regions[*active].id);
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
