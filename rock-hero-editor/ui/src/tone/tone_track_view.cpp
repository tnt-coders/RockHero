#include "tone_track_view.h"

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
const juce::Colour g_tone_region_border{juce::Colours::lightskyblue.withAlpha(0.65f)};
const juce::Colour g_tone_region_selected_fill{juce::Colour{0xff35597a}};
const juce::Colour g_tone_region_selected_border{juce::Colours::lightskyblue};
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
        if (m_drag.has_value() && m_drag->region_index == index)
        {
            span = common::core::TimeRange{
                .start = common::core::TimePosition{m_tempo_map.secondsAtBeat(
                    m_drag->preview_start.measure, m_drag->preview_start.beat)},
                .end = common::core::TimePosition{m_tempo_map.secondsAtBeat(
                    m_drag->preview_end.measure, m_drag->preview_end.beat)},
            };
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

        const auto label_bounds =
            region_bounds.reduced(static_cast<float>(g_region_label_inset), 0.0f).toNearestInt();
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

        m_listener.onToneRegionResizeRequested(region.id, drag.preview_start, drag.preview_end);
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
        if (std::abs(x - span->first) <= grab)
        {
            return RegionHit{.region_index = index, .edge = EdgeKind::Start};
        }
        if (std::abs(x - span->second) <= grab)
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
            min_beat =
                m_tempo_map.globalBeatIndex(previous.grid_end.measure, previous.grid_end.beat);
        }
    }
    else
    {
        min_beat = own_start + 1;
        if (m_drag->region_index + 1 < m_state.regions.size())
        {
            const core::ToneRegionViewState& next = m_state.regions[m_drag->region_index + 1];
            max_beat = m_tempo_map.globalBeatIndex(next.grid_start.measure, next.grid_start.beat);
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
