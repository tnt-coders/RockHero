#include "tone_automation_lanes_view.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Lane sizing: a comfortable default with a resize range wide enough for precise edits.
constexpr int g_default_lane_height = 56;
constexpr int g_minimum_lane_height = 40;
constexpr int g_maximum_lane_height = 120;
constexpr int g_plus_lane_height = 26;

// The value band is inset so a point at 0.0 or 1.0 never sits inside the resize band.
constexpr int g_value_band_inset = 5;

// Bottom strip of each lane that drags vertical resize; matches the tone row's edge grab width.
constexpr int g_resize_band_height = 6;

// Point handles draw small but hit-test forgiving (squared-distance test).
constexpr float g_point_draw_radius = 4.0f;
constexpr int g_point_hit_radius = 8;

// Chips pin to the visible left edge so lane names and "+" stay on screen at any zoom.
constexpr int g_chip_inset_x = 6;
constexpr int g_chip_inset_y = 4;
constexpr float g_chip_corner_radius = 3.0f;
constexpr float g_chip_font_height = 12.0f;

// The fine placement grid used when Ctrl bypasses the visible grid: 1/960 of a beat keeps every
// stored position an exact rational at far finer than audible resolution.
constexpr int g_fine_grid_denominator = 960;

const juce::Colour g_lane_separator{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_curve_colour{editorTheme().accent};
const juce::Colour g_point_fill{juce::Colours::white.withAlpha(0.92f)};
const juce::Colour g_chip_fill{juce::Colours::black.withAlpha(0.55f)};
const juce::Colour g_chip_text{juce::Colours::white.withAlpha(0.92f)};
const juce::Colour g_dim_overlay{juce::Colours::black.withAlpha(0.35f)};

// Alpha applied to a lane whose plugin or parameter no longer resolves.
constexpr float g_unresolved_alpha = 0.35f;

// Converts an exact musical position to seconds through the tempo map (UI-local mirror of the
// editor-core conversion, which lives in a target-private header).
[[nodiscard]] double secondsAtPosition(
    const rock_hero::common::core::TempoMap& tempo_map,
    const rock_hero::common::core::GridPosition& position)
{
    const double global_beat =
        static_cast<double>(tempo_map.globalBeatIndex(position.measure, position.beat)) +
        position.offset.toDouble();
    return tempo_map.secondsAtGlobalBeatPosition(global_beat);
}

} // namespace

ToneAutomationLanesView::ToneAutomationLanesView(
    Listener& listener, const common::core::TempoMap& tempo_map,
    const common::audio::IToneAutomation& tone_automation)
    : m_listener(listener)
    , m_tempo_map(tempo_map)
    , m_tone_automation(tone_automation)
    , m_vblank_attachment(this, [this] { repaintMovedTrackingLanes(); })
{
    setOpaque(false);
}

// Repaints only the unauthored lanes whose live value moved since the last frame, so tracking
// costs nothing while every lane is authored or the knob is untouched.
void ToneAutomationLanesView::repaintMovedTrackingLanes()
{
    // Vblank callbacks fire regardless of visibility, so gate the poll itself.
    if (!isShowing() || m_state.tone_document_ref.empty())
    {
        return;
    }
    const std::vector<LaneExtent> extents = laneExtents();
    for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[lane_index];
        if (!lane.points.empty() || !lane.resolved)
        {
            continue;
        }
        const float value = trackingValueFor(lane);
        auto& drawn = m_drawn_tracking_values[{lane.instance_id, lane.param_id}];
        if (std::abs(drawn - value) < 0.0005f)
        {
            continue;
        }
        drawn = value;
        repaint(0, extents[lane_index].top, getWidth(), extents[lane_index].height);
    }
}

float ToneAutomationLanesView::trackingValueFor(const core::ToneAutomationLaneViewState& lane) const
{
    if (const auto live = m_tone_automation.readParameterNormValue(
            m_state.tone_document_ref, lane.instance_id, lane.param_id);
        live.has_value())
    {
        return *live;
    }
    // An unreadable parameter (tone not loaded) keeps the value the projection last reported.
    return lane.live_norm_value;
}

void ToneAutomationLanesView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    m_visible_timeline = visible_timeline;
    repaint();
}

void ToneAutomationLanesView::setVisibleContentLeft(int content_left_x)
{
    if (m_visible_content_left == content_left_x)
    {
        return;
    }
    m_visible_content_left = content_left_x;
    repaint();
}

void ToneAutomationLanesView::setGridNoteValue(common::core::Fraction note_value)
{
    m_grid_note_value = note_value;
}

void ToneAutomationLanesView::setState(const core::ToneAutomationViewState& state)
{
    // Total height, not lane count, is what the viewport lays rows out from: selecting a tone
    // with zero lanes still grows the view from nothing to the "+" lane, and switching tones can
    // keep the count while the per-lane stored heights differ.
    const int previous_total_height = totalHeight();
    m_state = state;
    // Controller pushes replace the model under any in-flight gesture, so drop the gesture
    // instead of editing against stale indices.
    m_drag.reset();
    publishSnapGuide(std::nullopt);
    if (totalHeight() != previous_total_height && m_heights_changed_callback)
    {
        m_heights_changed_callback();
    }
    repaint();
}

void ToneAutomationLanesView::setEditableWindow(common::core::TimeRange window)
{
    m_editable_window = window;
    repaint();
}

void ToneAutomationLanesView::setSnapGuideCallback(SnapGuideCallback callback)
{
    m_snap_guide_callback = std::move(callback);
}

void ToneAutomationLanesView::setHeightsChangedCallback(HeightsChangedCallback callback)
{
    m_heights_changed_callback = std::move(callback);
}

int ToneAutomationLanesView::totalHeight() const
{
    if (m_state.tone_document_ref.empty())
    {
        return 0;
    }
    int height = 0;
    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        height += laneHeight(lane.instance_id, lane.param_id);
    }
    return height + g_plus_lane_height;
}

bool ToneAutomationLanesView::wantsPointerAt(juce::Point<int> local_point) const
{
    return hitAt(local_point).has_value();
}

std::vector<ToneAutomationLanesView::LaneExtent> ToneAutomationLanesView::laneExtents() const
{
    std::vector<LaneExtent> extents;
    extents.reserve(m_state.lanes.size() + 1);
    int top = 0;
    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        const int height = laneHeight(lane.instance_id, lane.param_id);
        extents.push_back(LaneExtent{.top = top, .height = height});
        top += height;
    }
    extents.push_back(LaneExtent{.top = top, .height = g_plus_lane_height});
    return extents;
}

int ToneAutomationLanesView::laneHeight(
    const std::string& instance_id, const std::string& param_id) const
{
    const auto height = m_lane_heights.find({instance_id, param_id});
    return height != m_lane_heights.end() ? height->second : g_default_lane_height;
}

// Raw linear map, deliberately unclamped: curve segments must span points outside the visible
// range, and the editable-window dim rects need real (possibly off-canvas) edge coordinates.
std::optional<float> ToneAutomationLanesView::xForSeconds(double seconds) const
{
    const double duration = m_visible_timeline.end.seconds - m_visible_timeline.start.seconds;
    if (getWidth() <= 0 || duration <= 0.0)
    {
        return std::nullopt;
    }
    return static_cast<float>(
        (seconds - m_visible_timeline.start.seconds) / duration * static_cast<double>(getWidth()));
}

float ToneAutomationLanesView::valueForY(int y, const LaneExtent& extent) const
{
    const int band_top = extent.top + g_value_band_inset;
    const int band_height =
        std::max(1, extent.height - (2 * g_value_band_inset) - g_resize_band_height);
    const float relative = static_cast<float>(y - band_top) / static_cast<float>(band_height);
    return std::clamp(1.0f - relative, 0.0f, 1.0f);
}

std::optional<common::core::GridPosition> ToneAutomationLanesView::musicalPositionForX(
    float content_x, const juce::ModifierKeys& mods) const
{
    const std::optional<common::core::TimePosition> clicked = core::timelineCursorPlacementTime(
        m_tempo_map,
        m_grid_note_value,
        m_visible_timeline,
        getWidth(),
        content_x,
        core::TimelineCursorPlacementMode::Free);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }

    // Snapped placements store the grid line's own exact musical address, so any grid value —
    // including odd fractions like 1/13 that no fixed fine grid divides — round-trips exactly.
    if (placementModeFor(mods) == core::TimelineCursorPlacementMode::SnapToGrid)
    {
        return core::nearestTempoGridPosition(m_tempo_map, m_grid_note_value, *clicked);
    }

    // Ctrl-free placements quantize the fractional beat to the 1/960 fine grid so stored
    // positions stay exact rationals instead of raw doubles; 960 covers every practical straight,
    // triplet, and quintuplet subdivision at sub-millisecond resolution.
    const double global_beat = m_tempo_map.beatPositionAtSeconds(clicked->seconds);
    double whole_beats = 0.0;
    const double beat_fraction = std::modf(std::max(0.0, global_beat), &whole_beats);
    auto beat_index = static_cast<std::int64_t>(whole_beats);
    int fine_steps =
        static_cast<int>(std::lround(beat_fraction * static_cast<double>(g_fine_grid_denominator)));
    if (fine_steps == g_fine_grid_denominator)
    {
        beat_index += 1;
        fine_steps = 0;
    }
    const auto [measure, beat] = m_tempo_map.beatAtGlobalIndex(beat_index);
    return common::core::GridPosition{
        .measure = measure,
        .beat = beat,
        .offset = common::core::Fraction{fine_steps, g_fine_grid_denominator},
    };
}

std::optional<ToneAutomationLanesView::Hit> ToneAutomationLanesView::hitAt(
    juce::Point<int> local_point) const
{
    if (m_state.tone_document_ref.empty())
    {
        return std::nullopt;
    }

    const std::vector<LaneExtent> extents = laneExtents();
    for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
    {
        const LaneExtent& extent = extents[lane_index];
        if (local_point.y < extent.top || local_point.y >= extent.top + extent.height)
        {
            continue;
        }
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[lane_index];
        if (!lane.resolved)
        {
            // Disabled lanes are inert; the overlay keeps click-to-seek over them.
            return std::nullopt;
        }

        // Point handles win over the resize band so a point at value 0 stays grabbable.
        for (std::size_t point_index = 0; point_index < lane.points.size(); ++point_index)
        {
            const std::optional<float> x = xForSeconds(lane.points[point_index].seconds);
            if (!x.has_value())
            {
                continue;
            }
            const float value_y =
                static_cast<float>(extent.top + g_value_band_inset) +
                (1.0f - lane.points[point_index].norm_value) *
                    static_cast<float>(
                        extent.height - (2 * g_value_band_inset) - g_resize_band_height);
            const float dx = static_cast<float>(local_point.x) - *x;
            const float dy = static_cast<float>(local_point.y) - value_y;
            if ((dx * dx) + (dy * dy) <=
                static_cast<float>(g_point_hit_radius * g_point_hit_radius))
            {
                return Hit{PointHit{.lane_index = lane_index, .point_index = point_index}};
            }
        }

        if (local_point.y >= extent.top + extent.height - g_resize_band_height)
        {
            return Hit{ResizeBandHit{.lane_index = lane_index}};
        }

        // Empty lane area adds points, but only inside the selected region's editable window.
        const std::optional<float> window_start = xForSeconds(m_editable_window.start.seconds);
        const std::optional<float> window_end = xForSeconds(m_editable_window.end.seconds);
        if (window_start.has_value() && window_end.has_value() &&
            static_cast<float>(local_point.x) >= *window_start &&
            static_cast<float>(local_point.x) < *window_end)
        {
            return Hit{LaneAreaHit{.lane_index = lane_index}};
        }
        return std::nullopt;
    }

    // The trailing empty lane: only its pinned "+" chip claims the pointer. The chip stays
    // hittable with nothing to offer so the picker can explain the empty state instead of the
    // affordance silently vanishing.
    const LaneExtent& plus_extent = extents.back();
    if (local_point.y >= plus_extent.top && local_point.y < plus_extent.top + plus_extent.height)
    {
        const int chip_left = m_visible_content_left + g_chip_inset_x;
        const juce::Rectangle<int> chip{
            chip_left,
            plus_extent.top + g_chip_inset_y,
            22,
            plus_extent.height - (2 * g_chip_inset_y)
        };
        if (chip.contains(local_point))
        {
            return Hit{PlusChipHit{}};
        }
    }
    return std::nullopt;
}

void ToneAutomationLanesView::paint(juce::Graphics& graphics)
{
    if (m_state.tone_document_ref.empty())
    {
        return;
    }

    const juce::Rectangle<int> clip = graphics.getClipBounds();
    const std::vector<LaneExtent> extents = laneExtents();
    const std::optional<float> window_start = xForSeconds(m_editable_window.start.seconds);
    const std::optional<float> window_end = xForSeconds(m_editable_window.end.seconds);

    for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
    {
        const LaneExtent& extent = extents[lane_index];
        const juce::Rectangle<int> lane_bounds{0, extent.top, getWidth(), extent.height};
        if (!lane_bounds.intersects(clip))
        {
            continue;
        }
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[lane_index];
        const float lane_alpha = lane.resolved ? 1.0f : g_unresolved_alpha;

        // The canvas paints the shared waveform-band and the tempo grid beneath this component,
        // and the grid must read exactly as it does on the waveform row — so lanes paint no
        // background at all; separators and name chips carry the lane boundaries.

        const int band_top = extent.top + g_value_band_inset;
        const int band_height =
            std::max(1, extent.height - (2 * g_value_band_inset) - g_resize_band_height);
        const auto value_to_y = [band_top, band_height](float norm_value) {
            return static_cast<float>(band_top) +
                   (1.0f - norm_value) * static_cast<float>(band_height);
        };

        // The drawn list substitutes (or inserts) the active drag's preview point so the gesture
        // paints live without touching the authoritative state.
        struct DrawnPoint
        {
            double seconds{};
            float norm_value{};
        };
        std::vector<DrawnPoint> drawn;
        drawn.reserve(lane.points.size() + 1);
        const MovePointDrag* active_move = nullptr;
        if (m_drag.has_value())
        {
            if (const auto* const move = std::get_if<MovePointDrag>(&*m_drag);
                move != nullptr && move->lane_index == lane_index)
            {
                active_move = move;
            }
        }
        for (std::size_t point_index = 0; point_index < lane.points.size(); ++point_index)
        {
            if (active_move != nullptr && !active_move->is_new_point &&
                active_move->point_index == point_index)
            {
                continue;
            }
            drawn.push_back(
                DrawnPoint{
                    .seconds = lane.points[point_index].seconds,
                    .norm_value = lane.points[point_index].norm_value,
                });
        }
        if (active_move != nullptr)
        {
            const DrawnPoint preview{
                .seconds = secondsAtPosition(m_tempo_map, active_move->preview_position),
                .norm_value = active_move->preview_value,
            };
            const auto insert_at =
                std::ranges::find_if(drawn, [&preview](const DrawnPoint& candidate) {
                    return preview.seconds < candidate.seconds;
                });
            drawn.insert(insert_at, preview);
        }

        // Project the drawn points to lane pixels once, then extend the curve flat to both canvas
        // edges: the parameter holds its first/last value outside the authored points, so a
        // single seeded point reads as a full-length line rather than an isolated dot.
        struct CurvePoint
        {
            float x{};
            float y{};
            bool authored{};
        };
        std::vector<CurvePoint> curve_points;
        curve_points.reserve(drawn.size() + 2);
        for (const DrawnPoint& point : drawn)
        {
            const std::optional<float> x = xForSeconds(point.seconds);
            if (!x.has_value())
            {
                continue;
            }
            curve_points.push_back(
                CurvePoint{.x = *x, .y = value_to_y(point.norm_value), .authored = true});
        }
        if (!curve_points.empty())
        {
            curve_points.insert(
                curve_points.begin(),
                CurvePoint{
                    .x = std::min(-1.0f, curve_points.front().x),
                    .y = curve_points.front().y,
                    .authored = false,
                });
            curve_points.push_back(
                CurvePoint{
                    .x = std::max(static_cast<float>(getWidth()) + 1.0f, curve_points.back().x),
                    .y = curve_points.back().y,
                    .authored = false,
                });
        }
        else if (drawn.empty() && lane.resolved)
        {
            // An open lane with no authored points tracks the parameter's live value as a flat
            // full-width line, so the lane always shows what the plugin is actually doing.
            const float tracking_value = trackingValueFor(lane);
            m_drawn_tracking_values[{lane.instance_id, lane.param_id}] = tracking_value;
            const float y = value_to_y(tracking_value);
            curve_points.push_back(CurvePoint{.x = -1.0f, .y = y, .authored = false});
            curve_points.push_back(
                CurvePoint{.x = static_cast<float>(getWidth()) + 1.0f, .y = y, .authored = false});
        }

        // The playhead strip repaints every lane each frame with a narrow clip, so only the
        // points whose segments can intersect the clip build the stroked path.
        juce::Path curve;
        bool path_started = false;
        const auto clip_left = static_cast<float>(clip.getX() - g_point_hit_radius);
        const auto clip_right = static_cast<float>(clip.getRight() + g_point_hit_radius);
        std::optional<float> previous_x;
        float previous_y = 0.0f;
        for (const CurvePoint& point : curve_points)
        {
            if (previous_x.has_value() && std::max(*previous_x, point.x) >= clip_left &&
                std::min(*previous_x, point.x) <= clip_right)
            {
                if (!path_started)
                {
                    curve.startNewSubPath(*previous_x, previous_y);
                    path_started = true;
                }
                if (lane.is_discrete)
                {
                    curve.lineTo(point.x, previous_y);
                    curve.lineTo(point.x, point.y);
                }
                else
                {
                    curve.lineTo(point.x, point.y);
                }
            }
            else
            {
                path_started = false;
            }
            previous_x = point.x;
            previous_y = point.y;

            if (point.authored && point.x >= clip_left && point.x <= clip_right)
            {
                graphics.setColour(g_point_fill.withMultipliedAlpha(lane_alpha));
                graphics.fillEllipse(
                    point.x - g_point_draw_radius,
                    point.y - g_point_draw_radius,
                    2.0f * g_point_draw_radius,
                    2.0f * g_point_draw_radius);
            }
        }
        graphics.setColour(g_curve_colour.withMultipliedAlpha(lane_alpha));
        graphics.strokePath(curve, juce::PathStrokeType{1.6f});

        // Dim everything outside the selected region's window: the lane is authored per tone but
        // edited per region instance.
        graphics.setColour(g_dim_overlay);
        if (window_start.has_value() && *window_start > static_cast<float>(lane_bounds.getX()))
        {
            graphics.fillRect(
                juce::Rectangle<float>{
                    0.0f,
                    static_cast<float>(extent.top),
                    *window_start,
                    static_cast<float>(extent.height)
                });
        }
        if (window_end.has_value() && *window_end < static_cast<float>(lane_bounds.getRight()))
        {
            graphics.fillRect(
                juce::Rectangle<float>{
                    *window_end,
                    static_cast<float>(extent.top),
                    static_cast<float>(lane_bounds.getRight()) - *window_end,
                    static_cast<float>(extent.height)
                });
        }

        graphics.setColour(g_lane_separator);
        graphics.fillRect(0, extent.top + extent.height - 1, getWidth(), 1);

        // Pinned name chip names the owning plugin too, so multi-plugin chains stay unambiguous;
        // unresolved lanes announce the missing plugin inline.
        const juce::String label = lane.plugin_name.empty() ? juce::String{lane.name}
                                                            : juce::String{lane.plugin_name} +
                                                                  " · " + juce::String{lane.name};
        const juce::String chip_text = lane.resolved ? label : label + " (plugin missing)";
        const juce::Font chip_font{juce::FontOptions{g_chip_font_height}};
        graphics.setFont(chip_font);
        const int chip_width = textWidth(chip_font, chip_text) + (2 * g_chip_inset_x);
        const juce::Rectangle<int> chip{
            m_visible_content_left + g_chip_inset_x, extent.top + g_chip_inset_y, chip_width, 17
        };
        graphics.setColour(g_chip_fill);
        graphics.fillRoundedRectangle(chip.toFloat(), g_chip_corner_radius);
        graphics.setColour(g_chip_text.withMultipliedAlpha(lane.resolved ? 1.0f : 0.75f));
        graphics.drawText(chip_text, chip, juce::Justification::centred);
    }

    // The trailing empty lane carries only the pinned "+" chip; no fill, so the shared band and
    // grid read at full strength there too.
    const LaneExtent& plus_extent = extents.back();
    const juce::Rectangle<int> plus_bounds{0, plus_extent.top, getWidth(), plus_extent.height};
    if (plus_bounds.intersects(clip))
    {
        // The chip is always drawn (dimmed when there is nothing to offer): hiding it made
        // "empty tone" and "listing failed" indistinguishable from a missing feature.
        const bool has_offer = !m_state.available_parameters.empty();
        const juce::Rectangle<int> chip{
            m_visible_content_left + g_chip_inset_x,
            plus_extent.top + g_chip_inset_y,
            22,
            plus_extent.height - (2 * g_chip_inset_y)
        };
        graphics.setColour(g_chip_fill.withMultipliedAlpha(has_offer ? 1.0f : 0.7f));
        graphics.fillRoundedRectangle(chip.toFloat(), g_chip_corner_radius);
        graphics.setColour(g_chip_text.withMultipliedAlpha(has_offer ? 1.0f : 0.55f));
        graphics.setFont(juce::Font{juce::FontOptions{g_chip_font_height + 2.0f}});
        graphics.drawText("+", chip, juce::Justification::centred);
    }
}

void ToneAutomationLanesView::mouseMove(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition());
    if (!hit.has_value())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (std::holds_alternative<PointHit>(*hit) || std::holds_alternative<PlusChipHit>(*hit))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else if (std::holds_alternative<ResizeBandHit>(*hit))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
}

void ToneAutomationLanesView::mouseDown(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition());
    if (!hit.has_value())
    {
        return;
    }

    if (event.mods.isPopupMenu())
    {
        if (const auto* const point_hit = std::get_if<PointHit>(&*hit))
        {
            showPointMenu(*point_hit);
        }
        else if (
            const auto* const area_hit = std::get_if<LaneAreaHit>(&*hit);
            area_hit != nullptr && m_state.lanes[area_hit->lane_index].points.empty()
        )
        {
            // Only unauthored tracking lanes offer removal here; authored lanes are removed by
            // deleting their points.
            showLaneMenu(area_hit->lane_index);
        }
        return;
    }

    if (std::holds_alternative<PlusChipHit>(*hit))
    {
        showParameterPicker();
        return;
    }

    if (const auto* const band = std::get_if<ResizeBandHit>(&*hit))
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[band->lane_index];
        m_drag = ResizeLaneDrag{
            .lane_index = band->lane_index,
            .start_height = laneHeight(lane.instance_id, lane.param_id),
            .start_y = event.getPosition().y,
        };
        return;
    }

    const std::vector<LaneExtent> extents = laneExtents();
    if (const auto* const point_hit = std::get_if<PointHit>(&*hit))
    {
        const core::ToneAutomationPointViewState& point =
            m_state.lanes[point_hit->lane_index].points[point_hit->point_index];
        m_drag = MovePointDrag{
            .lane_index = point_hit->lane_index,
            .point_index = point_hit->point_index,
            .preview_position = point.position,
            .preview_value = point.norm_value,
            .moved = false,
            .is_new_point = false,
        };
        return;
    }

    // Empty editable area: create a preview point and enter the move drag immediately, so
    // press-drag-release adds and places in one gesture; the commit happens on mouseUp.
    const auto& area = std::get<LaneAreaHit>(*hit);
    const std::optional<common::core::GridPosition> position =
        musicalPositionForX(static_cast<float>(event.getPosition().x), event.mods);
    if (!position.has_value())
    {
        return;
    }
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[area.lane_index];
    std::size_t insert_index = 0;
    while (insert_index < lane.points.size() && lane.points[insert_index].position < *position)
    {
        ++insert_index;
    }
    m_drag = MovePointDrag{
        .lane_index = area.lane_index,
        .point_index = insert_index,
        .preview_position = *position,
        .preview_value = valueForY(event.getPosition().y, extents[area.lane_index]),
        .moved = true,
        .is_new_point = true,
    };
    repaint();
}

void ToneAutomationLanesView::mouseDrag(const juce::MouseEvent& event)
{
    if (!m_drag.has_value())
    {
        return;
    }

    if (auto* const resize = std::get_if<ResizeLaneDrag>(&*m_drag))
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[resize->lane_index];
        const int new_height = std::clamp(
            resize->start_height + (event.getPosition().y - resize->start_y),
            g_minimum_lane_height,
            g_maximum_lane_height);
        m_lane_heights[{lane.instance_id, lane.param_id}] = new_height;
        if (m_heights_changed_callback)
        {
            m_heights_changed_callback();
        }
        repaint();
        return;
    }

    auto& move = std::get<MovePointDrag>(*m_drag);
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[move.lane_index];
    const std::vector<LaneExtent> extents = laneExtents();

    // Clamp X inside the editable window before converting, then clamp musically between the
    // temporal neighbors so the committed list stays strictly ascending by construction.
    const std::optional<float> window_start = xForSeconds(m_editable_window.start.seconds);
    const std::optional<float> window_end = xForSeconds(m_editable_window.end.seconds);
    float clamped_x = static_cast<float>(event.getPosition().x);
    if (window_start.has_value() && window_end.has_value())
    {
        clamped_x = std::clamp(clamped_x, *window_start, std::max(*window_start, *window_end - 1));
    }

    if (const std::optional<common::core::GridPosition> position =
            musicalPositionForX(clamped_x, event.mods);
        position.has_value())
    {
        bool blocked = false;
        // Neighbor indices skip the dragged point itself for existing points; for a new point the
        // insertion index already partitions the neighbors.
        if (move.point_index > 0)
        {
            const std::size_t previous_index = move.point_index - 1;
            if (previous_index < lane.points.size() &&
                !(lane.points[previous_index].position < *position))
            {
                blocked = true;
            }
        }
        const std::size_t next_index = move.is_new_point ? move.point_index : move.point_index + 1;
        if (next_index < lane.points.size() && !(*position < lane.points[next_index].position))
        {
            blocked = true;
        }
        if (!blocked)
        {
            move.preview_position = *position;
        }
    }
    move.preview_value = valueForY(event.getPosition().y, extents[move.lane_index]);
    move.moved = true;

    const double preview_seconds = secondsAtPosition(m_tempo_map, move.preview_position);
    if (const std::optional<float> guide_x = xForSeconds(preview_seconds); guide_x.has_value())
    {
        publishSnapGuide(
            TimelineSnapGuide{
                .x = *guide_x,
                .label = juce::String{common::core::formatGridPositionToken(move.preview_position)},
            });
    }
    repaint(0, extents[move.lane_index].top, getWidth(), extents[move.lane_index].height);
}

void ToneAutomationLanesView::mouseUp(const juce::MouseEvent& /*event*/)
{
    if (!m_drag.has_value())
    {
        return;
    }
    const DragState finished = *m_drag;
    m_drag.reset();
    publishSnapGuide(std::nullopt);

    if (const auto* const move = std::get_if<MovePointDrag>(&finished))
    {
        if (move->moved || move->is_new_point)
        {
            const core::ToneAutomationLaneViewState& lane = m_state.lanes[move->lane_index];
            m_listener.onToneAutomationPointsEditRequested(
                lane.instance_id, lane.param_id, pointsForCommit(*move));
        }
    }
    repaint();
}

std::vector<common::core::ToneAutomationPoint> ToneAutomationLanesView::pointsForCommit(
    const MovePointDrag& drag) const
{
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[drag.lane_index];
    std::vector<common::core::ToneAutomationPoint> points;
    points.reserve(lane.points.size() + 1);
    // Untouched points echo their stored musical positions bit-identically; only the dragged
    // point carries a new position/value.
    for (std::size_t index = 0; index < lane.points.size(); ++index)
    {
        if (!drag.is_new_point && index == drag.point_index)
        {
            continue;
        }
        points.push_back(
            common::core::ToneAutomationPoint{
                .position = lane.points[index].position,
                .norm_value = lane.points[index].norm_value,
                .curve_shape = lane.points[index].curve_shape,
            });
    }
    const common::core::ToneAutomationPoint edited{
        .position = drag.preview_position,
        .norm_value = drag.preview_value,
        .curve_shape = drag.is_new_point ? 0.0F : lane.points[drag.point_index].curve_shape,
    };
    const auto insert_at =
        std::ranges::find_if(points, [&edited](const common::core::ToneAutomationPoint& candidate) {
            return edited.position < candidate.position;
        });
    points.insert(insert_at, edited);
    return points;
}

void ToneAutomationLanesView::showParameterPicker()
{
    juce::PopupMenu menu;
    if (m_state.available_parameters.empty())
    {
        // Explain the empty offer instead of showing nothing: an unloaded tone cannot be
        // listed, and an empty tone has no parameters until plugins are added.
        menu.addItem(
            -1,
            m_state.parameters_unavailable ? "Tone parameters unavailable (tone not loaded)"
                                           : "No automatable parameters in this tone",
            false);
        menu.showMenuAsync(juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this));
        return;
    }

    // One expandable submenu per chain plugin (in chain order, numbered so duplicate plugins stay
    // distinct), with the plugin's parameter groups nested inside. The listing arrives in
    // plugin-then-tree order, so consecutive runs of one instance id are one plugin.
    juce::PopupMenu plugin_menu;
    juce::PopupMenu grouped;
    juce::String open_group;
    juce::String open_instance;
    juce::String open_plugin_label;
    int plugin_number = 0;
    int item_id = 1;
    const auto close_group = [&menu = plugin_menu, &grouped, &open_group] {
        if (grouped.getNumItems() > 0)
        {
            menu.addSubMenu(open_group, grouped);
            grouped = juce::PopupMenu{};
        }
        open_group.clear();
    };
    const auto close_plugin = [&menu, &plugin_menu, &open_plugin_label, &close_group] {
        close_group();
        if (plugin_menu.getNumItems() > 0)
        {
            menu.addSubMenu(open_plugin_label, plugin_menu);
            plugin_menu = juce::PopupMenu{};
        }
    };
    for (const core::ToneAutomationParamChoice& choice : m_state.available_parameters)
    {
        if (juce::String{choice.instance_id} != open_instance)
        {
            close_plugin();
            open_instance = juce::String{choice.instance_id};
            ++plugin_number;
            open_plugin_label = juce::String{plugin_number} + " · " +
                                (choice.plugin_name.empty() ? juce::String{"Plugin"}
                                                            : juce::String{choice.plugin_name});
        }

        const juce::String group{choice.group};
        if (group != open_group)
        {
            close_group();
            open_group = group;
        }
        if (group.isEmpty())
        {
            plugin_menu.addItem(item_id, juce::String{choice.name});
        }
        else
        {
            grouped.addItem(item_id, juce::String{choice.name});
        }
        ++item_id;
    }
    close_plugin();

    // Value-captured choices keep the callback safe across state pushes; the deletion check
    // forces a cancel result if this view ever dies while the menu is open.
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this, choices = m_state.available_parameters](int result) {
            if (result <= 0 || static_cast<std::size_t>(result) > choices.size())
            {
                return;
            }
            const core::ToneAutomationParamChoice& choice =
                choices[static_cast<std::size_t>(result - 1)];
            m_listener.onToneAutomationLaneAddRequested(choice.instance_id, choice.param_id);
        });
}

void ToneAutomationLanesView::showLaneMenu(std::size_t lane_index)
{
    if (lane_index >= m_state.lanes.size())
    {
        return;
    }
    juce::PopupMenu menu;
    menu.addItem(1, "Remove Lane");
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this,
         instance_id = m_state.lanes[lane_index].instance_id,
         param_id = m_state.lanes[lane_index].param_id](int result) {
            if (result != 1)
            {
                return;
            }
            m_listener.onToneAutomationLaneRemoveRequested(instance_id, param_id);
        });
}

void ToneAutomationLanesView::showPointMenu(const PointHit& hit)
{
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[hit.lane_index];
    juce::PopupMenu menu;
    menu.addItem(1, "Delete Point");
    // Identify the point by its musical position captured now, not by index: a state push while
    // the menu is open must not delete a different point.
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this,
         instance_id = lane.instance_id,
         param_id = lane.param_id,
         position = lane.points[hit.point_index].position](int result) {
            if (result != 1)
            {
                return;
            }
            for (const core::ToneAutomationLaneViewState& current_lane : m_state.lanes)
            {
                if (current_lane.instance_id != instance_id || current_lane.param_id != param_id)
                {
                    continue;
                }
                std::vector<common::core::ToneAutomationPoint> points;
                points.reserve(current_lane.points.size());
                for (const core::ToneAutomationPointViewState& point : current_lane.points)
                {
                    if (point.position == position)
                    {
                        continue;
                    }
                    points.push_back(
                        common::core::ToneAutomationPoint{
                            .position = point.position,
                            .norm_value = point.norm_value,
                            .curve_shape = point.curve_shape,
                        });
                }
                m_listener.onToneAutomationPointsEditRequested(
                    instance_id, param_id, std::move(points));
                return;
            }
        });
}

void ToneAutomationLanesView::publishSnapGuide(std::optional<TimelineSnapGuide> guide)
{
    if (m_snap_guide_callback)
    {
        m_snap_guide_callback(std::move(guide));
    }
}

} // namespace rock_hero::editor::ui
