#include "tone_automation_lanes_view.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <iterator>
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

// The value-readout chip drawn next to the cursor: padded for legibility and offset off the
// pointer so it never sits directly under the dragged point.
constexpr int g_readout_pad_x = 6;
constexpr int g_readout_height = 18;
constexpr int g_readout_cursor_gap = 12;
constexpr float g_readout_font_height = 12.0f;

const juce::Colour g_lane_separator{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_curve_colour{editorTheme().accent};
// Point handles fill in the curve colour so they read as part of the line; the selected point (the
// keyboard-Delete target) keeps the same size and colour and adds a white ring so it is picked out
// without jumping in size.
const juce::Colour g_point_fill{g_curve_colour};
const juce::Colour g_point_selected_ring{juce::Colours::white};
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

// One-field text editor hosted in a callout for typed exact-value entry on a point. Return
// commits through the supplied callback and dismisses; Escape dismisses without committing. The
// callout owns and deletes this content, so the commit callback must not touch it afterwards.
class PointValueEditorContent final : public juce::Component
{
public:
    // Creates the editor prefilled with the point's current display text, selected for overtyping.
    PointValueEditorContent(
        const juce::String& initial_text, std::function<void(const juce::String&)> on_commit)
        : m_on_commit(std::move(on_commit))
    {
        m_editor.setText(initial_text, juce::dontSendNotification);
        m_editor.selectAll();
        m_editor.onReturnKey = [this] {
            if (m_on_commit)
            {
                m_on_commit(m_editor.getText());
            }
            dismiss();
        };
        m_editor.onEscapeKey = [this] { dismiss(); };
        addAndMakeVisible(m_editor);
        setSize(140, 26);
    }

    // Fills the callout content with the single text field.
    void resized() override
    {
        m_editor.setBounds(getLocalBounds());
    }

    // Focuses the field as soon as the callout shows it, so typing can start immediately.
    void parentHierarchyChanged() override
    {
        if (isShowing())
        {
            m_editor.grabKeyboardFocus();
        }
    }

private:
    // Closes the owning callout; `this` is deleted by the callout during this call's unwind.
    void dismiss()
    {
        if (auto* const box = findParentComponentOfClass<juce::CallOutBox>())
        {
            box->dismiss();
        }
    }

    // Single-line value field; commit and cancel ride its return/escape callbacks.
    juce::TextEditor m_editor;

    // Receives the entered text on Return.
    std::function<void(const juce::String&)> m_on_commit;
};

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
    // A pointer gesture edits against the model it started with, and the engine pushes fresh state
    // frequently (plugin-state-change notifications fire in bursts, even mid-drag). Applying a push
    // now would reset the gesture and, for a just-pressed point, discard it before mouseUp can
    // commit. JUCE keeps delivering drag/up events to the captured component, so stash the latest
    // state and adopt it when the gesture ends instead of clobbering the edit in progress.
    if (m_drag.has_value())
    {
        m_pending_state = state;
        return;
    }
    applyState(state);
}

void ToneAutomationLanesView::applyState(const core::ToneAutomationViewState& state)
{
    // Total height, not lane count, is what the viewport lays rows out from: selecting a tone
    // with zero lanes still grows the view from nothing to the "+" lane, and switching tones can
    // keep the count while the per-lane stored heights differ.
    const int previous_total_height = totalHeight();
    m_state = state;
    m_value_readout.reset();
    m_insert_ghost.reset();
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
    // The predicate has no event, so it reads the live modifier state; JUCE re-sends a synthetic
    // mouse move on every modifier change, so pressing Alt re-runs this hit test immediately.
    return hitAt(local_point, juce::ModifierKeys::currentModifiers).has_value();
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

// The pinned name chip names the owning plugin too, so multi-plugin chains stay unambiguous;
// unresolved lanes announce the missing plugin inline.
juce::String ToneAutomationLanesView::laneChipText(const core::ToneAutomationLaneViewState& lane)
{
    const juce::String label =
        lane.plugin_name.empty() ? juce::String{lane.name}
                                 : juce::String{lane.plugin_name} + " · " + juce::String{lane.name};
    return lane.resolved ? label : label + " (plugin missing)";
}

// One geometry for painting and hit-testing the chip: pinned to the visible left edge, sized to
// the text plus insets.
juce::Rectangle<int> ToneAutomationLanesView::laneChipBounds(
    const core::ToneAutomationLaneViewState& lane, const LaneExtent& extent) const
{
    const juce::Font chip_font{juce::FontOptions{g_chip_font_height}};
    const int chip_width = textWidth(chip_font, laneChipText(lane)) + (2 * g_chip_inset_x);
    return juce::Rectangle<int>{
        m_visible_content_left + g_chip_inset_x, extent.top + g_chip_inset_y, chip_width, 17
    };
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

float ToneAutomationLanesView::snappedValueForLane(
    float raw_value, const core::ToneAutomationLaneViewState& lane)
{
    if (!lane.is_discrete || lane.discrete_value_count < 2)
    {
        return raw_value;
    }
    // States sit at k/(count-1) for k in [0, count-1]; snap to the nearest so the point can only
    // land on a real state (a two-state toggle moves strictly between 0 and 1).
    const int steps = lane.discrete_value_count - 1;
    const int nearest =
        std::clamp(static_cast<int>(std::lround(raw_value * static_cast<float>(steps))), 0, steps);
    return static_cast<float>(nearest) / static_cast<float>(steps);
}

std::optional<common::core::GridPosition> ToneAutomationLanesView::musicalPositionForX(
    float content_x, const juce::ModifierKeys& mods) const
{
    return musicalGridPositionForX(
        m_tempo_map, m_grid_note_value, m_visible_timeline, getWidth(), content_x, mods);
}

std::optional<ToneAutomationLanesView::Hit> ToneAutomationLanesView::hitAt(
    juce::Point<int> local_point, const juce::ModifierKeys& mods) const
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

        // The pinned name chip is the lane's handle: it claims the pointer on every lane —
        // including unresolved ones — so the lane menu (Remove Lane) stays reachable now that
        // plain clicks on empty lane area pass through to the seek overlay.
        if (laneChipBounds(lane, extent).contains(local_point))
        {
            return Hit{LaneChipHit{.lane_index = lane_index}};
        }

        if (!lane.resolved)
        {
            // Disabled lanes are otherwise inert; the overlay keeps click-to-seek over them.
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

        // Empty lane area belongs to the seek overlay unless Alt (the insert quasimode) is held;
        // with Alt down it becomes the insert target, only inside the region's editable window.
        if (!mods.isAltDown())
        {
            return std::nullopt;
        }
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
            bool selected{};
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
                    .selected = isPointSelected(lane, lane.points[point_index].position),
                });
        }
        if (active_move != nullptr)
        {
            const DrawnPoint preview{
                .seconds = secondsAtPosition(m_tempo_map, active_move->preview_position),
                .norm_value = active_move->preview_value,
                .selected = false,
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
            bool selected{};
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
                CurvePoint{
                    .x = *x,
                    .y = value_to_y(point.norm_value),
                    .authored = true,
                    .selected = point.selected,
                });
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
                // Every point draws in the curve colour at the same size; the selected point (the
                // Delete target) adds a white ring so it reads as picked without moving or resizing.
                const float radius = g_point_draw_radius;
                graphics.setColour(g_point_fill.withMultipliedAlpha(lane_alpha));
                graphics.fillEllipse(
                    point.x - radius, point.y - radius, 2.0f * radius, 2.0f * radius);
                if (point.selected)
                {
                    graphics.setColour(g_point_selected_ring.withMultipliedAlpha(lane_alpha));
                    graphics.drawEllipse(
                        point.x - radius, point.y - radius, 2.0f * radius, 2.0f * radius, 1.5f);
                }
            }
        }
        graphics.setColour(g_curve_colour.withMultipliedAlpha(lane_alpha));
        graphics.strokePath(curve, juce::PathStrokeType{1.6f});

        // The Alt-held insert ghost: a hollow ring where a click would place a point, so the
        // insertion is visible before anything mutates.
        if (m_insert_ghost.has_value() && m_insert_ghost->lane_index == lane_index)
        {
            if (const std::optional<float> ghost_x = xForSeconds(m_insert_ghost->seconds);
                ghost_x.has_value())
            {
                const float ghost_y = value_to_y(m_insert_ghost->norm_value);
                graphics.setColour(g_curve_colour.withAlpha(0.7f));
                graphics.drawEllipse(
                    *ghost_x - g_point_draw_radius,
                    ghost_y - g_point_draw_radius,
                    2.0f * g_point_draw_radius,
                    2.0f * g_point_draw_radius,
                    1.5f);
            }
        }

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

        // The pinned name chip doubles as the lane handle (clicking it opens the lane menu), so
        // its bounds come from the shared helper the hit test uses.
        graphics.setFont(juce::Font{juce::FontOptions{g_chip_font_height}});
        const juce::Rectangle<int> chip = laneChipBounds(lane, extent);
        graphics.setColour(g_chip_fill);
        graphics.fillRoundedRectangle(chip.toFloat(), g_chip_corner_radius);
        graphics.setColour(g_chip_text.withMultipliedAlpha(lane.resolved ? 1.0f : 0.75f));
        graphics.drawText(laneChipText(lane), chip, juce::Justification::centred);
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

    // The cursor readout draws last so it floats above every lane's curve and chips.
    paintValueReadout(graphics);
}

void ToneAutomationLanesView::mouseMove(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods);

    // Hovering a point shows its position and value next to the cursor; the Alt-held insert zone
    // shows the prospective point the same way; any other zone clears the readout and ghost.
    std::optional<GhostPoint> ghost;
    if (hit.has_value())
    {
        if (const auto* const point_hit = std::get_if<PointHit>(&*hit))
        {
            const core::ToneAutomationLaneViewState& lane = m_state.lanes[point_hit->lane_index];
            const core::ToneAutomationPointViewState& point = lane.points[point_hit->point_index];
            setValueReadout(
                ValueReadout{
                    .anchor = event.getPosition(),
                    .text = readoutTextFor(point.position, lane, point.norm_value),
                });
        }
        else if (const auto* const area = std::get_if<LaneAreaHit>(&*hit))
        {
            // LaneAreaHit only exists while Alt is held: preview exactly what a click would
            // insert, snapped like the click itself (Ctrl composes for fine placement).
            const core::ToneAutomationLaneViewState& lane = m_state.lanes[area->lane_index];
            const std::vector<LaneExtent> extents = laneExtents();
            if (const std::optional<common::core::GridPosition> position =
                    musicalPositionForX(static_cast<float>(event.getPosition().x), event.mods);
                position.has_value())
            {
                const float value = snappedValueForLane(
                    valueForY(event.getPosition().y, extents[area->lane_index]), lane);
                ghost = GhostPoint{
                    .lane_index = area->lane_index,
                    .position = *position,
                    .seconds = secondsAtPosition(m_tempo_map, *position),
                    .norm_value = value,
                };
                setValueReadout(
                    ValueReadout{
                        .anchor = event.getPosition(),
                        .text = readoutTextFor(*position, lane, value),
                    });
            }
            else
            {
                setValueReadout(std::nullopt);
            }
        }
        else
        {
            setValueReadout(std::nullopt);
        }
    }
    else
    {
        setValueReadout(std::nullopt);
    }
    setInsertGhost(ghost);

    if (!hit.has_value())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (std::holds_alternative<PointHit>(*hit) || std::holds_alternative<PlusChipHit>(*hit) ||
        std::holds_alternative<LaneChipHit>(*hit))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else if (std::holds_alternative<ResizeBandHit>(*hit))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    else
    {
        // The insert quasimode shows the arrow-with-plus copy cursor: "a click adds here".
        setMouseCursor(juce::MouseCursor::CopyingCursor);
    }
}

void ToneAutomationLanesView::mouseDown(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods);

    if (event.mods.isPopupMenu())
    {
        // A right-click on a point offers its own menu; a right-click on a claimed lane zone (the
        // name chip or the resize band) offers lane removal. Empty lane area belongs to the seek
        // overlay now, so the chip is the lane's always-reachable handle.
        if (const auto* const point_hit = hit.has_value() ? std::get_if<PointHit>(&*hit) : nullptr)
        {
            showPointMenu(*point_hit);
        }
        else if (
            hit.has_value() && (std::holds_alternative<LaneChipHit>(*hit) ||
                                std::holds_alternative<ResizeBandHit>(*hit) ||
                                std::holds_alternative<LaneAreaHit>(*hit))
        )
        {
            if (const std::optional<std::size_t> lane_index = laneIndexAtY(event.getPosition().y);
                lane_index.has_value())
            {
                showLaneMenu(*lane_index);
            }
        }
        return;
    }

    if (!hit.has_value())
    {
        return;
    }

    if (std::holds_alternative<PlusChipHit>(*hit))
    {
        showParameterPicker();
        return;
    }

    // The name chip is the lane handle: a plain click opens the lane menu.
    if (const auto* const chip = std::get_if<LaneChipHit>(&*hit))
    {
        showLaneMenu(chip->lane_index);
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
            .start_position = point.position,
            .start_value = point.norm_value,
            .moved = false,
            .is_new_point = false,
        };
        return;
    }

    // Alt-held empty editable area: create a preview point and enter the move drag immediately, so
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
    const float creation_value =
        snappedValueForLane(valueForY(event.getPosition().y, extents[area.lane_index]), lane);
    m_drag = MovePointDrag{
        .lane_index = area.lane_index,
        .point_index = insert_index,
        .preview_position = *position,
        .preview_value = creation_value,
        .start_position = *position,
        .start_value = creation_value,
        .moved = true,
        .is_new_point = true,
    };
    // The drag preview replaces the hover ghost from here on.
    setInsertGhost(std::nullopt);
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

    // A grabbed existing point stays a click until the pointer travels past JUCE's drag
    // threshold, so the micro-jiggle inside a click can never commit an accidental move.
    if (!move.moved && !move.is_new_point && !event.mouseWasDraggedSinceMouseDown())
    {
        return;
    }

    // Shift constrains the drag to its dominant axis, anchored at the gesture start: a horizontal
    // move keeps the starting value and a vertical move keeps the starting position.
    const bool horizontal_dominant =
        std::abs(event.getDistanceFromDragStartX()) >= std::abs(event.getDistanceFromDragStartY());
    const bool lock_value = event.mods.isShiftDown() && horizontal_dominant;
    const bool lock_position = event.mods.isShiftDown() && !horizontal_dominant;

    // Clamp X inside the editable window before converting, then clamp musically between the
    // temporal neighbors so the committed list stays strictly ascending by construction.
    const std::optional<float> window_start = xForSeconds(m_editable_window.start.seconds);
    const std::optional<float> window_end = xForSeconds(m_editable_window.end.seconds);
    float clamped_x = static_cast<float>(event.getPosition().x);
    if (window_start.has_value() && window_end.has_value())
    {
        clamped_x = std::clamp(clamped_x, *window_start, std::max(*window_start, *window_end - 1));
    }

    if (lock_position)
    {
        move.preview_position = move.start_position;
    }
    else if (
        const std::optional<common::core::GridPosition> position =
            musicalPositionForX(clamped_x, event.mods);
        position.has_value()
    )
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
    move.preview_value =
        lock_value
            ? move.start_value
            : snappedValueForLane(valueForY(event.getPosition().y, extents[move.lane_index]), lane);
    move.moved = true;

    const double preview_seconds = secondsAtPosition(m_tempo_map, move.preview_position);
    if (const std::optional<float> guide_x = xForSeconds(preview_seconds); guide_x.has_value())
    {
        // Keep the full-height alignment line, but carry no label of its own: the position now
        // rides in the cursor readout below, since the line's top-of-canvas label read as awkward.
        publishSnapGuide(TimelineSnapGuide{.x = *guide_x, .label = juce::String{}});
    }
    setValueReadout(
        ValueReadout{
            .anchor = event.getPosition(),
            .text = readoutTextFor(move.preview_position, lane, move.preview_value),
        });
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
    setValueReadout(std::nullopt);

    bool committed = false;
    // A move-point gesture selects the point it landed on: a committed create/move selects the new
    // point, and a plain click (no movement) selects the point clicked. Identity is captured before
    // the commit swaps m_state so the lane reference below stays valid.
    std::optional<SelectedPoint> new_selection;
    if (const auto* const move = std::get_if<MovePointDrag>(&finished))
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[move->lane_index];
        if (move->moved || move->is_new_point)
        {
            new_selection = SelectedPoint{
                .instance_id = lane.instance_id,
                .param_id = lane.param_id,
                .position = move->preview_position,
            };
            // The commit runs synchronously back through the controller and pushes fresh state; with
            // m_drag already cleared that push applies immediately rather than deferring.
            m_listener.onToneAutomationPointsEditRequested(
                lane.instance_id, lane.param_id, pointsForCommit(*move));
            committed = true;
        }
        else
        {
            new_selection = SelectedPoint{
                .instance_id = lane.instance_id,
                .param_id = lane.param_id,
                .position = lane.points[move->point_index].position,
            };
        }
    }
    repaint();

    // Adopt state deferred during the gesture now that indices are safe to replace. A commit already
    // published its own fresher state above, so any snapshot that predates it is stale and dropped;
    // a gesture that committed nothing (a plain click or a lane resize) instead adopts the snapshot.
    if (committed)
    {
        m_pending_state.reset();
    }
    else if (m_pending_state.has_value())
    {
        const core::ToneAutomationViewState pending = std::move(*m_pending_state);
        m_pending_state.reset();
        applyState(pending);
    }

    // Announce the selection last: the controller stores the durable identity and publishes it
    // resolved in the next state push, which arrives synchronously through the same call. A
    // stale identity (a rare pending snapshot dropped the point) publishes as no selection.
    if (new_selection.has_value())
    {
        m_listener.onToneAutomationPointSelectRequested(
            new_selection->instance_id, new_selection->param_id, new_selection->position);
    }
}

bool ToneAutomationLanesView::cancelActiveGesture()
{
    if (!m_drag.has_value())
    {
        return false;
    }

    // A cancelled lane resize restores its starting height; point gestures simply never commit.
    if (const auto* const resize = std::get_if<ResizeLaneDrag>(&*m_drag))
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[resize->lane_index];
        m_lane_heights[{lane.instance_id, lane.param_id}] = resize->start_height;
        if (m_heights_changed_callback)
        {
            m_heights_changed_callback();
        }
    }
    m_drag.reset();
    publishSnapGuide(std::nullopt);
    setValueReadout(std::nullopt);

    // Adopt a state deferred during the gesture, exactly as an uncommitted release would.
    if (m_pending_state.has_value())
    {
        const core::ToneAutomationViewState pending = std::move(*m_pending_state);
        m_pending_state.reset();
        applyState(pending);
    }
    repaint();
    return true;
}

bool ToneAutomationLanesView::nudgeSelectedPoint(NudgeDirection direction, bool fine)
{
    const std::optional<SelectedPoint> selected = selectedPointFromState();
    if (!selected.has_value())
    {
        return false;
    }
    const SelectedPoint target = *selected;

    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        if (lane.instance_id != target.instance_id || lane.param_id != target.param_id)
        {
            continue;
        }
        const auto point =
            std::ranges::find_if(lane.points, [&](const core::ToneAutomationPointViewState& p) {
                return p.position == target.position;
            });
        if (point == lane.points.end())
        {
            return false;
        }

        if (direction == NudgeDirection::Up || direction == NudgeDirection::Down)
        {
            // A discrete lane steps one real state; a continuous one steps 0.01 (0.001 fine).
            const float step = lane.is_discrete && lane.discrete_value_count >= 2
                                   ? 1.0F / static_cast<float>(lane.discrete_value_count - 1)
                                   : (fine ? 0.001F : 0.01F);
            const float raw = point->norm_value + (direction == NudgeDirection::Up ? step : -step);
            const float new_value = snappedValueForLane(std::clamp(raw, 0.0F, 1.0F), lane);
            // Exact inequality via is_neq keeps -Wfloat-equal builds clean; snapped-value
            // no-change detection is deliberately exact.
            if (std::is_neq(new_value <=> point->norm_value))
            {
                requestPointReplace(
                    target.instance_id,
                    target.param_id,
                    target.position,
                    target.position,
                    new_value);
            }
            return true;
        }

        // Time nudge: to the adjacent tempo-grid line, or by one 1/960-beat fine step. Both go
        // through the beat axis so the result stays an exact rational.
        const bool later = direction == NudgeDirection::Later;
        const double global_beat = static_cast<double>(m_tempo_map.globalBeatIndex(
                                       target.position.measure, target.position.beat)) +
                                   target.position.offset.toDouble();
        common::core::GridPosition new_position;
        if (fine)
        {
            new_position = core::fineGridPositionForBeat(
                m_tempo_map, global_beat + (later ? 1.0 : -1.0) / 960.0);
        }
        else
        {
            // One grid step is the grid note value scaled by the local meter's beat unit; landing
            // via nearest-grid-line keeps odd grids exact and respects the measure-anchored walk.
            const common::core::TimeSignatureChange signature =
                m_tempo_map.timeSignatureAt(target.position.measure);
            const double step_beats =
                m_grid_note_value.toDouble() * static_cast<double>(signature.denominator);
            const double target_seconds = m_tempo_map.secondsAtGlobalBeatPosition(
                global_beat + (later ? step_beats : -step_beats));
            new_position = core::nearestTempoGridPosition(
                m_tempo_map, m_grid_note_value, common::core::TimePosition{target_seconds});
        }

        // Refuse nudges that collapse (map edge) or reverse direction (nearest-line bounce-back),
        // and clamp strictly between the neighbors and inside the editable window; a refused
        // nudge still reports handled so the arrow key never leaks to another target.
        if (new_position == target.position || (later && new_position < target.position) ||
            (!later && target.position < new_position))
        {
            return true;
        }
        if (point != lane.points.begin() && !(std::prev(point)->position < new_position))
        {
            return true;
        }
        if (std::next(point) != lane.points.end() && !(new_position < std::next(point)->position))
        {
            return true;
        }
        const double new_seconds = secondsAtPosition(m_tempo_map, new_position);
        if (new_seconds < m_editable_window.start.seconds ||
            new_seconds >= m_editable_window.end.seconds)
        {
            return true;
        }
        requestPointReplace(
            target.instance_id, target.param_id, target.position, new_position, point->norm_value);
        return true;
    }
    return false;
}

void ToneAutomationLanesView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods);
    if (!hit.has_value())
    {
        return;
    }
    const auto* const point_hit = std::get_if<PointHit>(&*hit);
    if (point_hit == nullptr)
    {
        return;
    }

    // The double-click's second press armed a move drag; drop it so the value editor opens over a
    // quiescent gesture state (the following mouseUp then early-returns without committing).
    m_drag.reset();
    publishSnapGuide(std::nullopt);
    showPointValueEditor(*point_hit);
}

void ToneAutomationLanesView::mouseExit(const juce::MouseEvent& /*event*/)
{
    // A drag keeps its readout even when the pointer leaves the component (JUCE keeps delivering
    // drag events); a plain hover that leaves the lanes clears the readout and the insert ghost.
    if (!m_drag.has_value())
    {
        setValueReadout(std::nullopt);
        setInsertGhost(std::nullopt);
    }
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
    // Distinct capture name (not `menu`): clang's -Wshadow-uncaptured-local flags init-captures
    // that shadow the enclosing function's `menu` local.
    const auto close_group = [&target_menu = plugin_menu, &grouped, &open_group] {
        if (grouped.getNumItems() > 0)
        {
            target_menu.addSubMenu(open_group, grouped);
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
            // An authored lane's points are cleared first (one undoable edit that drops the
            // arrangement entry); the open-lane close then removes any session tracking lane so the
            // row disappears instead of falling back to a live-tracking lane. The close is a no-op
            // for a package-loaded authored lane that never had an open entry.
            const auto lane = std::ranges::find_if(
                m_state.lanes, [&](const core::ToneAutomationLaneViewState& candidate) {
                    return candidate.instance_id == instance_id && candidate.param_id == param_id;
                });
            if (lane != m_state.lanes.end() && !lane->points.empty())
            {
                m_listener.onToneAutomationPointsEditRequested(instance_id, param_id, {});
            }
            m_listener.onToneAutomationLaneRemoveRequested(instance_id, param_id);
        });
}

void ToneAutomationLanesView::showPointMenu(const PointHit& hit)
{
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[hit.lane_index];
    juce::PopupMenu menu;
    menu.addItem(1, "Delete Point");
    menu.addItem(2, "Set Value...");
    menu.addItem(3, "Reset to Default");
    // Identify the point by its musical position captured now, not by index: a state push while
    // the menu is open must not act on a different point.
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withMousePosition().withDeletionCheck(*this),
        [this,
         instance_id = lane.instance_id,
         param_id = lane.param_id,
         default_value = lane.default_norm_value,
         position = lane.points[hit.point_index].position](int result) {
            if (result == 1)
            {
                requestPointDelete(instance_id, param_id, position);
            }
            else if (result == 2)
            {
                // Re-resolve indices at click time; the position identifies the point durably.
                for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
                {
                    const core::ToneAutomationLaneViewState& candidate = m_state.lanes[lane_index];
                    if (candidate.instance_id != instance_id || candidate.param_id != param_id)
                    {
                        continue;
                    }
                    for (std::size_t point_index = 0; point_index < candidate.points.size();
                         ++point_index)
                    {
                        if (candidate.points[point_index].position == position)
                        {
                            showPointValueEditor(
                                PointHit{.lane_index = lane_index, .point_index = point_index});
                            return;
                        }
                    }
                }
            }
            else if (result == 3)
            {
                requestPointReplace(instance_id, param_id, position, position, default_value);
            }
        });
}

// Opens the typed exact-value callout for a point, prefilled with the value formatted in the
// parameter's native units; Return parses the text through the automation port and commits one
// points-edit intent. Skipped when the view is not on screen (headless tests have no desktop).
void ToneAutomationLanesView::showPointValueEditor(const PointHit& hit)
{
    if (!isShowing())
    {
        return;
    }
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[hit.lane_index];
    const core::ToneAutomationPointViewState& point = lane.points[hit.point_index];

    const auto formatted = m_tone_automation.formatParameterValue(
        m_state.tone_document_ref, lane.instance_id, lane.param_id, point.norm_value);
    const juce::String initial_text = formatted.has_value() && !formatted->empty()
                                          ? juce::String{*formatted}
                                          : juce::String{point.norm_value, 3};

    auto content = std::make_unique<PointValueEditorContent>(
        initial_text,
        [safe_this = juce::Component::SafePointer<ToneAutomationLanesView>{this},
         tone_ref = m_state.tone_document_ref,
         instance_id = lane.instance_id,
         param_id = lane.param_id,
         position = point.position](const juce::String& text) {
            if (safe_this == nullptr)
            {
                return;
            }
            const auto parsed = safe_this->m_tone_automation.parseParameterValue(
                tone_ref, instance_id, param_id, text.toStdString());
            if (!parsed.has_value())
            {
                return;
            }
            // Re-snap discrete lanes so typed text can only land on a real state.
            const auto lane_now = std::ranges::find_if(
                safe_this->m_state.lanes, [&](const core::ToneAutomationLaneViewState& candidate) {
                    return candidate.instance_id == instance_id && candidate.param_id == param_id;
                });
            if (lane_now == safe_this->m_state.lanes.end())
            {
                return;
            }
            safe_this->requestPointReplace(
                instance_id, param_id, position, position, snappedValueForLane(*parsed, *lane_now));
        });

    // Anchor the callout to the point's on-screen location.
    const std::vector<LaneExtent> extents = laneExtents();
    const LaneExtent& extent = extents[hit.lane_index];
    const float point_y =
        static_cast<float>(extent.top + g_value_band_inset) +
        (1.0f - point.norm_value) *
            static_cast<float>(extent.height - (2 * g_value_band_inset) - g_resize_band_height);
    const int point_x =
        static_cast<int>(xForSeconds(point.seconds).value_or(static_cast<float>(getWidth()) / 2));
    const juce::Rectangle<int> anchor =
        localAreaToGlobal(juce::Rectangle<int>{point_x - 4, static_cast<int>(point_y) - 4, 8, 8});
    juce::CallOutBox::launchAsynchronously(std::move(content), anchor, nullptr);
}

// Emits the points-edit intent that removes the point at the given position from its lane. Shared by
// the right-click "Delete Point" menu and the keyboard-Delete path; a no-op when the lane or point
// no longer exists (a state push may have removed it while a menu was open).
void ToneAutomationLanesView::requestPointDelete(
    const std::string& instance_id, const std::string& param_id,
    const common::core::GridPosition& position)
{
    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        if (lane.instance_id != instance_id || lane.param_id != param_id)
        {
            continue;
        }
        std::vector<common::core::ToneAutomationPoint> points;
        points.reserve(lane.points.size());
        for (const core::ToneAutomationPointViewState& point : lane.points)
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
        m_listener.onToneAutomationPointsEditRequested(instance_id, param_id, std::move(points));
        return;
    }
}

// Emits the points-edit intent that replaces one point's position and value, echoing every other
// point bit-identically and keeping the edited point's curve shape. Selects the edited point at
// its new position before committing, so the synchronous state push keeps it selected.
void ToneAutomationLanesView::requestPointReplace(
    const std::string& instance_id, const std::string& param_id,
    const common::core::GridPosition& position, const common::core::GridPosition& new_position,
    float new_value)
{
    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        if (lane.instance_id != instance_id || lane.param_id != param_id)
        {
            continue;
        }
        const auto target =
            std::ranges::find_if(lane.points, [&](const core::ToneAutomationPointViewState& p) {
                return p.position == position;
            });
        if (target == lane.points.end())
        {
            return;
        }
        std::vector<common::core::ToneAutomationPoint> points;
        points.reserve(lane.points.size());
        for (const core::ToneAutomationPointViewState& point : lane.points)
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
        const common::core::ToneAutomationPoint edited{
            .position = new_position,
            .norm_value = new_value,
            .curve_shape = target->curve_shape,
        };
        const auto insert_at = std::ranges::find_if(
            points, [&edited](const common::core::ToneAutomationPoint& candidate) {
                return edited.position < candidate.position;
            });
        points.insert(insert_at, edited);
        m_listener.onToneAutomationPointsEditRequested(instance_id, param_id, std::move(points));
        // Re-announce the selection at the edited point's new identity: the edit's state push
        // resolved the old position to nothing, and the follow-up push from this intent lights
        // the point back up where it landed.
        m_listener.onToneAutomationPointSelectRequested(instance_id, param_id, new_position);
        return;
    }
}

// Reports whether a specific lane point is the published editor-wide selection.
bool ToneAutomationLanesView::isPointSelected(
    const core::ToneAutomationLaneViewState& lane, const common::core::GridPosition& position) const
{
    const std::optional<SelectedPoint> selected = selectedPointFromState();
    return selected.has_value() && selected->instance_id == lane.instance_id &&
           selected->param_id == lane.param_id && selected->position == position;
}

// Resolves the published selection reference back to the durable point identity, or empty when
// the state publishes no (or an out-of-range) selection.
std::optional<ToneAutomationLanesView::SelectedPoint> ToneAutomationLanesView::
    selectedPointFromState() const
{
    if (!m_state.selected_point.has_value())
    {
        return std::nullopt;
    }
    const core::ToneAutomationSelectedPointRef& ref = *m_state.selected_point;
    if (ref.lane_index >= m_state.lanes.size() ||
        ref.point_index >= m_state.lanes[ref.lane_index].points.size())
    {
        return std::nullopt;
    }
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[ref.lane_index];
    return SelectedPoint{
        .instance_id = lane.instance_id,
        .param_id = lane.param_id,
        .position = lane.points[ref.point_index].position,
    };
}

// Resolves the real lane row (not the trailing "+" lane) containing a local y, or empty.
std::optional<std::size_t> ToneAutomationLanesView::laneIndexAtY(int y) const
{
    const std::vector<LaneExtent> extents = laneExtents();
    for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
    {
        if (y >= extents[lane_index].top &&
            y < extents[lane_index].top + extents[lane_index].height)
        {
            return lane_index;
        }
    }
    return std::nullopt;
}

void ToneAutomationLanesView::publishSnapGuide(std::optional<TimelineSnapGuide> guide)
{
    if (m_snap_guide_callback)
    {
        m_snap_guide_callback(std::move(guide));
    }
}

juce::String ToneAutomationLanesView::readoutTextFor(
    const common::core::GridPosition& position, const core::ToneAutomationLaneViewState& lane,
    float norm_value) const
{
    juce::String text{common::core::formatGridPositionToken(position)};
    // The value is formatted in the parameter's native units; if the tone cannot be resolved the
    // readout still shows the position rather than nothing.
    if (const auto value = m_tone_automation.formatParameterValue(
            m_state.tone_document_ref, lane.instance_id, lane.param_id, norm_value);
        value.has_value() && !value->empty())
    {
        text << "  " << juce::String{*value};
    }
    return text;
}

juce::Rectangle<int> ToneAutomationLanesView::readoutBounds(const ValueReadout& readout) const
{
    const juce::Font font{juce::FontOptions{g_readout_font_height}};
    const int width = textWidth(font, readout.text) + (2 * g_readout_pad_x);
    // Default above-right of the cursor; flip toward the inside whenever an edge would clip it, so
    // the chip stays fully visible within the lanes even at the row's borders.
    int x = readout.anchor.x + g_readout_cursor_gap;
    if (x + width > getWidth())
    {
        x = readout.anchor.x - g_readout_cursor_gap - width;
    }
    x = std::clamp(x, 0, std::max(0, getWidth() - width));
    int y = readout.anchor.y - g_readout_cursor_gap - g_readout_height;
    if (y < 0)
    {
        y = readout.anchor.y + g_readout_cursor_gap;
    }
    y = std::clamp(y, 0, std::max(0, getHeight() - g_readout_height));
    return juce::Rectangle<int>{x, y, width, g_readout_height};
}

// Sets or clears the Alt-held insert ghost, repainting only the lane rows it vacates and covers.
void ToneAutomationLanesView::setInsertGhost(std::optional<GhostPoint> ghost)
{
    if (m_insert_ghost == ghost)
    {
        return;
    }
    const std::vector<LaneExtent> extents = laneExtents();
    const auto repaint_ghost_lane = [this, &extents](const std::optional<GhostPoint>& target) {
        if (target.has_value() && target->lane_index < m_state.lanes.size())
        {
            const LaneExtent& extent = extents[target->lane_index];
            repaint(0, extent.top, getWidth(), extent.height);
        }
    };
    repaint_ghost_lane(m_insert_ghost);
    m_insert_ghost = ghost;
    repaint_ghost_lane(m_insert_ghost);
}

void ToneAutomationLanesView::setValueReadout(std::optional<ValueReadout> readout)
{
    if (m_value_readout == readout)
    {
        return;
    }
    if (m_value_readout.has_value())
    {
        repaint(readoutBounds(*m_value_readout));
    }
    m_value_readout = std::move(readout);
    if (m_value_readout.has_value())
    {
        repaint(readoutBounds(*m_value_readout));
    }
}

void ToneAutomationLanesView::paintValueReadout(juce::Graphics& graphics) const
{
    if (!m_value_readout.has_value())
    {
        return;
    }
    const juce::Rectangle<int> bounds = readoutBounds(*m_value_readout);
    graphics.setColour(g_chip_fill);
    graphics.fillRoundedRectangle(bounds.toFloat(), g_chip_corner_radius);
    graphics.setColour(g_chip_text);
    graphics.setFont(juce::Font{juce::FontOptions{g_readout_font_height}});
    graphics.drawText(m_value_readout->text, bounds, juce::Justification::centred);
}

std::optional<juce::String> ToneAutomationLanesView::valueReadoutTextForTest() const
{
    if (!m_value_readout.has_value())
    {
        return std::nullopt;
    }
    return m_value_readout->text;
}

} // namespace rock_hero::editor::ui
