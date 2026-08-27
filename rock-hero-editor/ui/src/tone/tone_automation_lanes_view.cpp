#include "tone_automation_lanes_view.h"

#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "timeline/timeline_cursor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <type_traits>
#include <utility>
#include <variant>

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

// Point handles draw small but hit-test forgiving; the forgiving radius is the shared
// g_tone_lane_handle_grab_radius, since the controller re-resolves a press against it.
constexpr float g_point_draw_radius = 4.0f;

// Horizontal slack around the repaint clip when building the curve path: a mark whose center sits
// just outside the clip still inks pixels inside it, so segments and heads are built from a little
// further out than the clip itself. Covers the widest mark a lane draws (a point's radius plus its
// stroke); deliberately not the grab radius, which is an input-policy number that happens to match.
constexpr int g_curve_clip_margin = 8;

// The lane's derived anchor: a HOLLOW point on the authored points' own circle, so it stands among
// them at their size while reading as a different kind of mark — derived rather than authored, and
// so neither selectable nor deletable. Its ring is stroked like the other rings drawn on that
// circle (the selected point's, the insert ghost's); what tells it from the ghost's is the filled
// center at the paint site, which the curve stops at instead of running through. Quieter than a
// point because it is derived, but still visible enough to invite the drag that authors a real
// point in its place.
constexpr float g_anchor_ring_stroke_width = 1.5f;
constexpr float g_anchor_alpha = 0.7f;

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
// The curve, and the point handles that fill in the same colour so they read as part of the line,
// take the theme accent read at paint time. A file-scope const would freeze the accent at static
// initialization and survive a theme change, defeating the theme seam. The selected point (the
// keyboard-Delete target) keeps the same size and colour and adds a white ring so it is picked out
// without jumping in size.
const juce::Colour g_point_selected_ring{juce::Colours::white};
const juce::Colour g_chip_fill{juce::Colours::black.withAlpha(0.55f)};
const juce::Colour g_chip_text{juce::Colours::white.withAlpha(0.92f)};
const juce::Colour g_dim_overlay{juce::Colours::black.withAlpha(0.35f)};

// Alpha applied to a lane whose plugin or parameter no longer resolves.
constexpr float g_unresolved_alpha = 0.35f;

// The forgiving grab circle every lane handle shares — authored points and the derived anchor
// alike — so one mark can never be easier to hit than another. The controller re-resolves a
// forwarded press against the same radius and the same ÷width geometry.
[[nodiscard]] bool withinGrabRadius(juce::Point<int> local_point, float handle_x, float handle_y)
{
    const float dx = static_cast<float>(local_point.x) - handle_x;
    const float dy = static_cast<float>(local_point.y) - handle_y;
    return ((dx * dx) + (dy * dy)) <=
           (core::g_tone_lane_handle_grab_radius * core::g_tone_lane_handle_grab_radius);
}

// Copies a lane's published points into model form, echoing every point bit-identically and
// skipping at most one position — the shared base of the view's points-edit intents, so a
// field added to ToneAutomationPoint needs exactly one copy site.
[[nodiscard]] std::vector<rock_hero::common::core::ToneAutomationPoint> copyLanePoints(
    const rock_hero::editor::core::ToneAutomationLaneViewState& lane,
    const rock_hero::common::core::GridPosition* skip_position)
{
    std::vector<rock_hero::common::core::ToneAutomationPoint> points;
    points.reserve(lane.points.size() + 1);
    for (const rock_hero::editor::core::ToneAutomationPointViewState& point : lane.points)
    {
        if (skip_position != nullptr && point.position == *skip_position)
        {
            continue;
        }
        points.push_back(
            rock_hero::common::core::ToneAutomationPoint{
                .position = point.position,
                .norm_value = point.norm_value,
            });
    }
    return points;
}

// Inserts a point at its ascending-position slot (the upper-bound insert every builder uses).
void insertPointSorted(
    std::vector<rock_hero::common::core::ToneAutomationPoint>& points,
    rock_hero::common::core::ToneAutomationPoint inserted)
{
    const auto insert_at = std::ranges::find_if(
        points, [&inserted](const rock_hero::common::core::ToneAutomationPoint& candidate) {
            return inserted.position < candidate.position;
        });
    points.insert(insert_at, inserted);
}

// One-field text editor hosted in a callout for typed exact-value entry on a point. Return
// commits through the supplied callback and dismisses; Escape dismisses without committing. The
// callout owns and deletes this content, so the commit callback must not touch it afterwards.
class PointValueEditorContent final : public juce::Component
{
public:
    // Where the caret starts, so each entry path overtypes correctly.
    enum class InitialCaret : std::uint8_t
    {
        // Prefilled with a complete value: highlight it so the first keystroke replaces it.
        SelectAll,
        // Seeded with the first typed digit: park the caret after it so more digits append.
        AtEnd,
    };

    // Creates the editor prefilled with the seed text, with the caret placed for the entry path.
    PointValueEditorContent(
        const juce::String& initial_text, InitialCaret initial_caret,
        std::function<void(const juce::String&)> on_commit)
        : m_on_commit(std::move(on_commit))
    {
        m_editor.setText(initial_text, juce::dontSendNotification);
        if (initial_caret == InitialCaret::SelectAll)
        {
            m_editor.selectAll();
        }
        else
        {
            // setText already parks the caret at the end, but say so explicitly rather than lean
            // on that behavior: the seed digit must survive the next keystroke, not be replaced.
            m_editor.moveCaretToEnd();
        }
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
    , m_vblank_attachment(this, [this] {
        repaintMovedAnchorLanes();
        // A lane caret rides the drawn curve, which begins at the anchor, so the square (and its
        // mask) can move with no state push; republish each frame (a no-op while it is stationary).
        publishCaretMask();
    })
{
    setOpaque(false);
}

// Repaints only the lanes whose anchor moved since the last frame, so tracking the knob costs
// nothing while it is untouched. Every resolved lane is polled, not just unauthored ones: the
// anchor is drawn on all of them, and on an unauthored lane it is also the flat line's value.
void ToneAutomationLanesView::repaintMovedAnchorLanes()
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
        if (!lane.resolved)
        {
            continue;
        }
        const float value = anchorValueFor(lane);
        auto& drawn = m_drawn_anchor_values[{lane.instance_id, lane.param_id}];
        if (std::abs(drawn - value) < 0.0005f)
        {
            continue;
        }
        drawn = value;
        repaint(0, extents[lane_index].top, getWidth(), extents[lane_index].height);
    }
}

float ToneAutomationLanesView::anchorValueFor(const core::ToneAutomationLaneViewState& lane) const
{
    if (const auto baseline = m_tone_automation.readParameterBaselineNormValue(
            m_state.tone_document_ref, lane.instance_id, lane.param_id);
        baseline.has_value())
    {
        return *baseline;
    }
    // An unreadable parameter (tone not loaded) keeps the value the projection last reported.
    return lane.anchor_norm_value;
}

void ToneAutomationLanesView::setVisibleTimeline(common::core::TimeRange visible_timeline)
{
    // Equality-gated like the sibling views' setters: view assembly pushes this every state
    // application, and an unchanged window repaints nothing.
    if (m_visible_timeline == visible_timeline)
    {
        return;
    }
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

void ToneAutomationLanesView::setPlacementQuantum(common::core::Fraction placement_quantum)
{
    m_placement_quantum = placement_quantum;
}

void ToneAutomationLanesView::setState(const core::ToneAutomationViewState& state)
{
    // A lane-resize drag reads m_state for its lane keys, so a state push mid-resize (the engine
    // pushes fresh state frequently — plugin-state notifications fire in bursts) is stashed and
    // adopted when the resize ends, keeping its index stable. The controller-owned point
    // move/insert drag needs no such deferral: its preview is baked into every state build (through
    // ToneAutomationViewState::drag_preview), so applying a push mid-drag repaints the lane WITH
    // the current preview instead of resetting the edit.
    if (m_drag.has_value())
    {
        m_pending_state = state;
        return;
    }
    applyState(state);
}

void ToneAutomationLanesView::applyState(const core::ToneAutomationViewState& state)
{
    // Unchanged pushes repaint nothing (the TabView equality gate): plugin-state notifications
    // fire state rebuilds in bursts, and most change nothing the lanes render.
    if (m_state == state)
    {
        return;
    }
    // Total height, not lane count, is what the viewport lays rows out from: selecting a tone
    // with zero lanes still grows the view from nothing to the "+" lane, and switching tones can
    // keep the count while the per-lane stored heights differ.
    const int previous_total_height = totalHeight();
    m_state = state;
    // The chip labels and their measured widths are a pure function of the lane list, so they are
    // re-laid out here rather than in paint() or in every pointer hit-test.
    refreshLaneChips();
    // A standing drag preview owns the transient overlays: mouseDrag keeps the readout fresh at the
    // cursor and the guide rides the preview, so a mid-drag rebuild must not wipe them. Only a
    // preview-less push (the common case, and the drag's own committing push) clears them.
    if (m_state.drag_preview.has_value())
    {
        if (const std::optional<float> guide_x = xForSeconds(
                core::secondsAtGridPosition(m_tempo_map, m_state.drag_preview->position));
            guide_x.has_value())
        {
            publishSnapGuide(TimelineSnapGuide{.x = *guide_x, .label = juce::String{}});
        }
    }
    else
    {
        m_value_readout.reset();
        publishSnapGuide(std::nullopt);
    }
    if (totalHeight() != previous_total_height && m_heights_changed_callback)
    {
        m_heights_changed_callback();
    }
    repaint();
    // The new state can arm, clear, move, or reshape the lane caret (a point edit at its slot
    // shifts its on-curve y); push the fresh mask now so the paused column's cut-out changes in the
    // same synchronous pass as the drawn square, never a frame behind it.
    publishCaretMask();
}

void ToneAutomationLanesView::setEditableWindow(common::core::TimeRange window)
{
    // Equality-gated like every sibling setter here: the editor re-pushes the whole state on every
    // caret step, and an unchanged window must repaint nothing.
    if (m_editable_window == window)
    {
        return;
    }

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

void ToneAutomationLanesView::setCaretMaskCallback(CaretMaskCallback callback)
{
    m_caret_mask_callback = std::move(callback);
    // Seed the sink with the current mask so a callback installed after a caret already exists is
    // not left believing the column is ungapped.
    m_published_caret_mask.reset();
    publishCaretMask();
}

// Translates the local caret mask into content coordinates and pushes it only when it changed, so
// the per-frame vblank publish costs nothing while the square is stationary.
void ToneAutomationLanesView::publishCaretMask()
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

void ToneAutomationLanesView::moved()
{
    publishCaretMask();
}

void ToneAutomationLanesView::resized()
{
    publishCaretMask();
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
    // Alt only reorders the pinned chip against the lane area beneath it, and both orderings agree
    // on the question asked here — whether the lanes claim the pixel at all — so this resolves
    // without it rather than reaching for the live modifier state.
    return hitAt(local_point, false).has_value();
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

// Rebuilds the per-lane chip labels and their measured widths. Kept out of paint() and hitAt():
// laying out the label text is a GlyphArrangement pass plus two string joins, and it used to run
// for every lane on every paint AND on every pointer hover hit-test.
void ToneAutomationLanesView::refreshLaneChips()
{
    const juce::Font chip_font{juce::FontOptions{g_chip_font_height}};
    m_lane_chips.clear();
    m_lane_chips.reserve(m_state.lanes.size());
    for (const core::ToneAutomationLaneViewState& lane : m_state.lanes)
    {
        juce::String text = laneChipText(lane);
        const int width = textWidth(chip_font, text) + (2 * g_chip_inset_x);
        m_lane_chips.push_back(LaneChip{.text = std::move(text), .width = width});
    }
}

// One geometry for painting and hit-testing the chip: pinned to the visible left edge, sized to
// the cached text width plus insets. Only the pinned x depends on the scroll offset, so
// setVisibleContentLeft slides the chip without invalidating the cache.
juce::Rectangle<int> ToneAutomationLanesView::laneChipBounds(
    std::size_t lane_index, const LaneExtent& extent) const
{
    return juce::Rectangle<int>{
        m_visible_content_left + g_chip_inset_x,
        extent.top + g_chip_inset_y,
        m_lane_chips[lane_index].width,
        17
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

ToneAutomationLanesView::ValueBand ToneAutomationLanesView::valueBandFor(const LaneExtent& extent)
{
    // Inset from the lane's outer bounds by the value-band inset above and the resize band
    // below, so a point at value 0 or 1 never sits inside the resize grab.
    return ValueBand{
        .top = extent.top + g_value_band_inset,
        .height = std::max(1, extent.height - (2 * g_value_band_inset) - g_resize_band_height),
    };
}

float ToneAutomationLanesView::valueBandY(const ValueBand& band, float norm_value)
{
    return static_cast<float>(band.top) + (1.0f - norm_value) * static_cast<float>(band.height);
}

// The lane caret square: centered on the curve at the published caret slot — exactly where
// Insert and typed values land — shared by paint and the paused-column mask.
std::optional<juce::Rectangle<float>> ToneAutomationLanesView::laneCaretSquare() const
{
    if (!m_state.lane_caret.has_value() || m_state.lane_caret->lane_index >= m_state.lanes.size())
    {
        return std::nullopt;
    }
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[m_state.lane_caret->lane_index];
    const std::optional<float> x = xForSeconds(m_state.lane_caret->seconds);
    if (!x.has_value())
    {
        return std::nullopt;
    }
    const std::vector<LaneExtent> extents = laneExtents();
    const LaneExtent& extent = extents[m_state.lane_caret->lane_index];
    const float y =
        valueBandY(valueBandFor(extent), curveValueAt(lane, m_state.lane_caret->seconds));
    const float half_side = g_point_draw_radius + 2.0f;
    return juce::Rectangle<float>{
        *x - half_side, y - half_side, 2.0f * half_side, 2.0f * half_side
    };
}

std::optional<juce::Range<float>> ToneAutomationLanesView::caretMaskYRange() const
{
    const std::optional<juce::Rectangle<float>> square = laneCaretSquare();
    if (!square.has_value())
    {
        return std::nullopt;
    }
    // Half the stroke sits outside the rectangle bounds; include it so the paused column never
    // touches the drawn stroke.
    return juce::Range<float>{square->getY() - 1.0f, square->getBottom() + 1.0f};
}

core::ToneAutomationCurveSample ToneAutomationLanesView::laneAnchorSample(
    const core::ToneAutomationLaneViewState& lane) const
{
    return core::ToneAutomationCurveSample{
        .seconds = core::toneAutomationAnchorSeconds(),
        .norm_value = anchorValueFor(lane),
    };
}

std::vector<core::ToneAutomationCurveSample> ToneAutomationLanesView::laneCurveSamples(
    const core::ToneAutomationLaneViewState& lane)
{
    std::vector<core::ToneAutomationCurveSample> samples;
    samples.reserve(lane.points.size());
    for (const core::ToneAutomationPointViewState& point : lane.points)
    {
        samples.push_back(
            core::ToneAutomationCurveSample{
                .seconds = point.seconds,
                .norm_value = point.norm_value,
            });
    }
    return samples;
}

float ToneAutomationLanesView::curveValueAt(
    const core::ToneAutomationLaneViewState& lane, double seconds) const
{
    // Raw, never snapped: this reports what the lane DRAWS, and the drawn curve carries the raw
    // anchor and the raw authored values (the backend curve is written from the same numbers). A
    // discrete lane's anchor can sit off-state — the tone state's value is the plugin's, not the
    // chart's — and snapping here would put the caret square and the readout at a different height
    // from the anchor mark they are supposed to sit on. Creation snaps instead, at the one seam
    // that makes a point: the controller's landing value.
    return core::toneAutomationCurveValueAtSeconds(
        laneAnchorSample(lane), laneCurveSamples(lane), seconds, lane.is_discrete);
}

std::optional<common::core::GridPosition> ToneAutomationLanesView::musicalPositionForX(
    float content_x) const
{
    return musicalGridPositionForX(
        m_tempo_map, m_placement_quantum, m_visible_timeline, getWidth(), content_x);
}

bool ToneAutomationLanesView::withinAnchorGrab(
    const core::ToneAutomationLaneViewState& lane, const LaneExtent& extent,
    juce::Point<int> local_point) const
{
    // No mark, nothing to grab: an authored point on (or before) the anchor's slot already states
    // the lane's value there, so paint draws that point instead of the anchor's mark.
    if (!lane.points.empty() && lane.points.front().seconds <= core::toneAutomationAnchorSeconds())
    {
        return false;
    }
    const std::optional<float> anchor_x = xForSeconds(core::toneAutomationAnchorSeconds());
    if (!anchor_x.has_value())
    {
        return false;
    }
    // Reading the anchor's value is a port call, so the cheap column test prunes it first — the
    // same order the controller's matching test uses.
    if (std::abs(static_cast<float>(local_point.x) - *anchor_x) >
        core::g_tone_lane_handle_grab_radius)
    {
        return false;
    }
    return withinGrabRadius(
        local_point, *anchor_x, valueBandY(valueBandFor(extent), anchorValueFor(lane)));
}

std::optional<std::size_t> ToneAutomationLanesView::laneIndexOf(const Hit& hit)
{
    return std::visit(
        []<typename Zone>(const Zone& zone) -> std::optional<std::size_t> {
            if constexpr (std::is_same_v<Zone, PlusChipHit>)
            {
                return std::nullopt;
            }
            else
            {
                return zone.lane_index;
            }
        },
        hit);
}

std::optional<ToneAutomationLanesView::Hit> ToneAutomationLanesView::hitAt(
    juce::Point<int> local_point, bool alt_down) const
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

        // The pinned name chip is the lane menu's only home, so it has to stay hittable — but it
        // is pinned OVER the lane's own content, and a mark drawn on top of a target must never
        // shadow it. That is the same principle that resolves point handles ahead of the resize
        // band, generalised: the chip yields to every target beneath it, so it is resolved last
        // and claims only what would otherwise do nothing but arm the caret.
        const bool over_chip = laneChipBounds(lane_index, extent).contains(local_point);

        if (!lane.resolved)
        {
            // A disabled lane has no points, no anchor, and no editable area — the chip is its
            // only target, so its lane menu (Remove Lane) stays reachable and the rest of the row
            // keeps the overlay's click-to-seek.
            if (over_chip)
            {
                return Hit{LaneChipHit{.lane_index = lane_index}};
            }
            return std::nullopt;
        }

        // Point handles win over everything drawn around them: a point at value 0 stays grabbable
        // under the resize band, and a point near the lane start stays grabbable under the chip.
        for (std::size_t point_index = 0; point_index < lane.points.size(); ++point_index)
        {
            const std::optional<float> x = xForSeconds(lane.points[point_index].seconds);
            if (!x.has_value())
            {
                continue;
            }
            if (withinGrabRadius(
                    local_point,
                    *x,
                    valueBandY(valueBandFor(extent), lane.points[point_index].norm_value)))
            {
                return Hit{PointHit{.lane_index = lane_index, .point_index = point_index}};
            }
        }

        // Editable lane area is a hit with or without Alt (§9b): with Alt down it is the insert
        // quasimode's target, and a plain click seeks and arms the caret on the lane — the
        // row-axis form of the chart lane's empty click. Outside the editable window the area
        // stays with the seek overlay, and so does whatever part of the anchor's grab falls out
        // there: a press could not author past the window edge anyway, since creation refuses it.
        const std::optional<float> window_start = xForSeconds(m_editable_window.start.seconds);
        const std::optional<float> window_end = xForSeconds(m_editable_window.end.seconds);
        const bool in_window = window_start.has_value() && window_end.has_value() &&
                               static_cast<float>(local_point.x) >= *window_start &&
                               static_cast<float>(local_point.x) < *window_end;

        // The derived anchor's grab is a handle too — its drag authors the lane's start point — so
        // like a point handle it outruns both marks drawn across it: the resize band, which an
        // anchor at value 0 reaches into, and the chip pinned over the lane start where the anchor
        // always sits. It also outruns Alt, because the anchor is the one place the Alt-authors
        // law does not reach; resolving it as its own zone (rather than as lane area) is what
        // keeps the hover from offering an insert the press would refuse. The controller
        // re-resolves the press against the same circle.
        if (in_window && withinAnchorGrab(lane, extent, local_point))
        {
            return Hit{LaneAnchorHit{.lane_index = lane_index}};
        }

        if (local_point.y >= extent.top + extent.height - g_resize_band_height)
        {
            return Hit{ResizeBandHit{.lane_index = lane_index}};
        }

        // Bare lane area takes its pixel back from the chip only under Alt, where a press inserts
        // instead of merely arming the caret. Everything else beneath the chip has claimed its
        // pixel above, so what is left for the chip is exactly the area that offers nothing else.
        if (in_window && (!over_chip || alt_down))
        {
            return Hit{LaneAreaHit{.lane_index = lane_index}};
        }
        if (over_chip)
        {
            return Hit{LaneChipHit{.lane_index = lane_index}};
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

        const ValueBand band = valueBandFor(extent);
        const auto value_to_y = [band](float norm_value) { return valueBandY(band, norm_value); };

        // The drawn list substitutes (or inserts) the controller's published drag preview so the
        // gesture paints live without touching the authoritative state: a moved point hides its
        // stored self and draws the preview in its place, an insert adds a preview point.
        struct DrawnPoint
        {
            double seconds{};
            float norm_value{};
            bool selected{};
        };
        std::vector<DrawnPoint> drawn;
        drawn.reserve(lane.points.size() + 1);
        const core::ToneAutomationDragPreviewRef* active_preview = nullptr;
        if (m_state.drag_preview.has_value() && m_state.drag_preview->lane_index == lane_index)
        {
            active_preview = &*m_state.drag_preview;
        }
        for (std::size_t point_index = 0; point_index < lane.points.size(); ++point_index)
        {
            if (active_preview != nullptr && !active_preview->is_new_point &&
                active_preview->source_point_index == point_index)
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
        if (active_preview != nullptr)
        {
            const DrawnPoint preview{
                .seconds = core::secondsAtGridPosition(m_tempo_map, active_preview->position),
                .norm_value = active_preview->value,
                .selected = false,
            };
            const auto insert_at =
                std::ranges::find_if(drawn, [&preview](const DrawnPoint& candidate) {
                    return preview.seconds < candidate.seconds;
                });
            drawn.insert(insert_at, preview);
        }

        // Project the drawn points to lane pixels once, then extend the curve flat to both canvas
        // edges: the parameter holds its first/last value outside them, so a single seeded point
        // reads as a full-length line rather than an isolated dot.
        //
        // The lane's derived anchor leads the list. It is where the curve genuinely begins — the
        // parameter's pre-automation value, which the write seam prepends to the backend curve —
        // so the segment into the first authored point is drawn, not flattened, and a lane with no
        // authored points is simply the anchor's own flat line. An authored point on (or before)
        // the anchor's slot already states the lane's value there, so no anchor mark is drawn for
        // it. An unresolved lane has no readable parameter, so it shows no anchor either.
        struct CurvePoint
        {
            float x{};
            float y{};
            bool authored{};
            bool selected{};
        };
        std::vector<CurvePoint> curve_points;
        curve_points.reserve(drawn.size() + 3);
        // Where the anchor's own mark goes, kept aside because it is painted after the curve is
        // stroked rather than with the points; empty exactly where no anchor mark is drawn.
        std::optional<juce::Point<float>> anchor_mark;
        if (lane.resolved)
        {
            const core::ToneAutomationCurveSample anchor = laneAnchorSample(lane);
            m_drawn_anchor_values[{lane.instance_id, lane.param_id}] = anchor.norm_value;
            if (const std::optional<float> anchor_x = xForSeconds(anchor.seconds);
                anchor_x.has_value() && (drawn.empty() || drawn.front().seconds > anchor.seconds))
            {
                const juce::Point<float> mark{*anchor_x, value_to_y(anchor.norm_value)};
                anchor_mark = mark;
                curve_points.push_back(
                    CurvePoint{
                        .x = mark.x,
                        .y = mark.y,
                        .authored = false,
                        .selected = false,
                    });
            }
        }
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
                    .selected = false,
                });
            curve_points.push_back(
                CurvePoint{
                    .x = std::max(static_cast<float>(getWidth()) + 1.0f, curve_points.back().x),
                    .y = curve_points.back().y,
                    .authored = false,
                    .selected = false,
                });
        }

        // The playhead strip repaints every lane each frame with a narrow clip, so only the
        // points whose segments can intersect the clip build the stroked path.
        juce::Path curve;
        bool path_started = false;
        const auto clip_left = static_cast<float>(clip.getX() - g_curve_clip_margin);
        const auto clip_right = static_cast<float>(clip.getRight() + g_curve_clip_margin);
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
                // Delete target) adds a white ring so it reads as
                // picked without moving or resizing.
                const float radius = g_point_draw_radius;
                graphics.setColour(editorTheme().accent.withMultipliedAlpha(lane_alpha));
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
        graphics.setColour(editorTheme().accent.withMultipliedAlpha(lane_alpha));
        graphics.strokePath(curve, juce::PathStrokeType{1.6f});

        // The derived anchor, drawn AFTER the curve because its center has to cover the line: a
        // HOLLOW point where an authored point is a solid one, same circle and same place in the
        // handle family, so it reads as the other KIND of mark rather than as a smaller or dimmer
        // point. It cannot be selected or deleted. What it answers is a DRAG, which authors a real
        // point in its place at the anchor's own value and pulls it off the anchor from there — a
        // bare click authors nothing and leaves the ring exactly as it is — so it stays quieter
        // than a point while still reading as a handle.
        //
        // The center is filled with the band the canvas paints beneath the lanes: the lanes view
        // draws no background of its own (TrackViewport's track-row layering contract paints the
        // lanes band in the waveform row's colour), so restating that one role here is what makes
        // the fill invisible as a fill and visible only as an emptied center. It is opaque rather
        // than lane-alpha'd because an occluder that lets the line through is not an occluder —
        // and the two can never disagree, since an unresolved lane draws no anchor at all. The
        // filled center is also what tells this ring from the Alt-insert ghost's, which is the
        // same circle drawn hollow-through, with the curve running visibly across it.
        if (anchor_mark.has_value() && anchor_mark->x >= clip_left && anchor_mark->x <= clip_right)
        {
            const juce::Point<float> mark = *anchor_mark;
            const juce::Rectangle<float> ring{
                mark.x - g_point_draw_radius,
                mark.y - g_point_draw_radius,
                2.0f * g_point_draw_radius,
                2.0f * g_point_draw_radius
            };
            graphics.setColour(editorTheme().waveform_row_background);
            graphics.fillEllipse(ring);
            graphics.setColour(
                editorTheme().accent.withMultipliedAlpha(lane_alpha * g_anchor_alpha));
            graphics.drawEllipse(ring, g_anchor_ring_stroke_width);
        }

        // The Alt-held insert ghost: a hollow ring on the curve where an Alt+click would place a
        // point, published by the controller through m_state.insert_ghost (its slot snapped
        // controller-side, and hidden over an occupied slot). Its on-curve y is derived here,
        // mirroring the point/caret paint. Suppressed while any gesture runs: a lane-resize drag
        // (m_drag) or a controller-owned point drag (drag_preview) owns the lane then.
        if (!m_drag.has_value() && !m_state.drag_preview.has_value() &&
            m_state.insert_ghost.has_value() && m_state.insert_ghost->lane_index == lane_index)
        {
            if (const std::optional<float> ghost_x = xForSeconds(m_state.insert_ghost->seconds);
                ghost_x.has_value())
            {
                const float ghost_y = value_to_y(curveValueAt(lane, m_state.insert_ghost->seconds));
                graphics.setColour(editorTheme().accent.withAlpha(0.7f));
                graphics.drawEllipse(
                    *ghost_x - g_point_draw_radius,
                    ghost_y - g_point_draw_radius,
                    2.0f * g_point_draw_radius,
                    2.0f * g_point_draw_radius,
                    1.5f);
            }
        }

        // The armed marker caret riding this lane (§9b): a white rounded square centered on
        // the curve at the caret slot — where Insert and typed values land. Its geometry comes
        // from the shared helper so the paused-column cut-out can never diverge from the drawn
        // square.
        if (m_state.lane_caret.has_value() && m_state.lane_caret->lane_index == lane_index)
        {
            if (const std::optional<juce::Rectangle<float>> square = laneCaretSquare())
            {
                graphics.setColour(juce::Colours::white);
                graphics.drawRoundedRectangle(*square, 2.0f, 1.5f);
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
        const juce::Rectangle<int> chip = laneChipBounds(lane_index, extent);
        graphics.setColour(g_chip_fill);
        graphics.fillRoundedRectangle(chip.toFloat(), g_chip_corner_radius);
        graphics.setColour(g_chip_text.withMultipliedAlpha(lane.resolved ? 1.0f : 0.75f));
        graphics.drawText(m_lane_chips[lane_index].text, chip, juce::Justification::centred);
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
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods.isAltDown());

    // Resolve the hover readout and the hover intent. The insert ghost is controller-owned now
    // (published through m_state.insert_ghost); the view forwards every lane-area hover and lets
    // the controller resolve snap + occupancy, exactly like the tab lane's chart ghost. Hovering a
    // point shows its position and value; the Alt-held insert zone shows the prospective on-curve
    // point the same way; any other zone shows none. The anchor is one of those other zones: it
    // answers a drag rather than a click, so a hover over it forwards no lane-area hover and the
    // ghost ring stays off the one pixel neighbourhood where Alt would not author.
    std::optional<ValueReadout> readout;
    std::optional<std::size_t> hovered_lane_index;
    if (hit.has_value())
    {
        if (const auto* const point_hit = std::get_if<PointHit>(&*hit))
        {
            const core::ToneAutomationLaneViewState& lane = m_state.lanes[point_hit->lane_index];
            const core::ToneAutomationPointViewState& point = lane.points[point_hit->point_index];
            readout = ValueReadout{
                .anchor = event.getPosition(),
                .text = readoutTextFor(point.position, lane, point.norm_value),
            };
        }
        else if (const auto* const area = std::get_if<LaneAreaHit>(&*hit))
        {
            const core::ToneAutomationLaneViewState& lane = m_state.lanes[area->lane_index];
            hovered_lane_index = area->lane_index;
            // Under Alt the prospective on-curve insert previews its position and value next to the
            // cursor, snapped exactly as the controller's ghost is. Without Alt the lane area is
            // the caret-arming click zone and shows none.
            if (event.mods.isAltDown())
            {
                if (const std::optional<common::core::GridPosition> position =
                        musicalPositionForX(static_cast<float>(event.getPosition().x));
                    position.has_value())
                {
                    const double seconds = core::secondsAtGridPosition(m_tempo_map, *position);
                    readout = ValueReadout{
                        .anchor = event.getPosition(),
                        .text = readoutTextFor(*position, lane, curveValueAt(lane, seconds)),
                    };
                }
            }
        }
    }

    // Forward the hover before applying the readout: a lane-area hover can push fresh state (the
    // controller resolves the insert ghost), and that push resets the readout in applyState — so
    // the readout is applied last, surviving the rebuild. The event carries the raw pixels plus the
    // geometry rather than a view-computed time, so the controller inverts the pixel through the
    // placement seam itself and the ghost lands on the exact slot an Alt+click would.
    if (hovered_lane_index.has_value())
    {
        m_listener.onToneAutomationPointerMove(makePointerEvent(event, hovered_lane_index));
    }
    else
    {
        m_listener.onToneAutomationPointerExit();
    }
    setValueReadout(readout);

    if (!hit.has_value())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (std::holds_alternative<PointHit>(*hit) || std::holds_alternative<LaneAnchorHit>(*hit) ||
        std::holds_alternative<PlusChipHit>(*hit) || std::holds_alternative<LaneChipHit>(*hit))
    {
        // The anchor takes the point handle's cursor, with or without Alt: it is a grabbable mark,
        // and the copy cursor would promise the insert its press does not perform.
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else if (std::holds_alternative<ResizeBandHit>(*hit))
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    else if (std::holds_alternative<LaneAreaHit>(*hit) && !event.mods.isAltDown())
    {
        // Plain lane area is the caret-arming click zone: a normal cursor, exactly like the
        // chart lane's own empty space.
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    else
    {
        // The insert quasimode shows the arrow-with-plus copy cursor: "a click adds here".
        setMouseCursor(juce::MouseCursor::CopyingCursor);
    }
}

void ToneAutomationLanesView::mouseDown(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods.isAltDown());

    // A left press is excluded for the same reason the chart lane excludes it: JUCE expands
    // popupMenuClickModifier to (rightButton | ctrl) on macOS, so isPopupMenu() alone would raise
    // this menu on a Ctrl+left-click there and nowhere else. The menu belongs to an actual right
    // press on every platform.
    if (event.mods.isPopupMenu() && !event.mods.isLeftButtonDown())
    {
        // A right-click on a point offers its own menu; a right-click on any other claimed lane
        // zone (the name chip, the resize band, the lane area) offers lane removal. The chip is
        // the one zone every lane has, resolved or not, so the menu is always reachable.
        if (const auto* const point_hit = hit.has_value() ? std::get_if<PointHit>(&*hit) : nullptr)
        {
            showPointMenu(*point_hit);
        }
        else if (hit.has_value())
        {
            // Every lane-zone hit already carries its row: use the stored index rather than
            // re-deriving the lane from y a second time. The "+" chip names none, and offers no
            // lane to remove.
            if (const std::optional<std::size_t> lane_index = laneIndexOf(*hit);
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

    // The name chip is the lane menu's only home: a plain click over it opens the menu, wherever
    // hitAt did not hand the pixel to a target beneath it.
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

    // A press on a point handle, the derived anchor, or editable lane area forwards to the
    // controller, which owns the gesture: it re-resolves point-vs-anchor-vs-area from the same
    // geometry and arms a point move, an anchor drag, an Alt-insert placement (refused on an
    // occupied slot), or the lane caret, and publishes any preview back. The resize band, name
    // chip, "+" picker, and right-clicks are handled above, so only those editing zones reach
    // here. The hover readout already carries the on-curve value an Alt-insert lands at, so the
    // press leaves it in place; a drag advance refreshes it.
    if (const std::optional<std::size_t> lane_index = laneIndexOf(*hit); lane_index.has_value())
    {
        m_listener.onToneAutomationPointerDown(makePointerEvent(event, *lane_index));
    }
}

void ToneAutomationLanesView::mouseDrag(const juce::MouseEvent& event)
{
    // A lane-resize drag is view-owned; advance it here. Everything else is a controller-owned
    // point move/insert (or nothing): forward the live pointer and let the controller run the
    // delta-value / neighbor-clamp / axis-lock state machine, then seed the cursor readout from
    // whatever preview it publishes back.
    if (m_drag.has_value())
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[m_drag->lane_index];
        const int new_height = std::clamp(
            m_drag->start_height + (event.getPosition().y - m_drag->start_y),
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

    m_listener.onToneAutomationPointerDrag(makePointerEvent(event, std::nullopt));
    applyDragPreviewReadout(event.getPosition());
}

void ToneAutomationLanesView::mouseUp(const juce::MouseEvent& event)
{
    // A lane-resize drag ends view-side: drop it and adopt any state deferred during it. Everything
    // else is a controller-owned point drag — forward the release so the controller commits its
    // edit (or selects a click) and clears its own preview.
    if (m_drag.has_value())
    {
        m_drag.reset();
        if (m_pending_state.has_value())
        {
            const core::ToneAutomationViewState pending = std::move(*m_pending_state);
            m_pending_state.reset();
            applyState(pending);
        }
        return;
    }

    m_listener.onToneAutomationPointerUp(makePointerEvent(event, std::nullopt));
}

bool ToneAutomationLanesView::cancelActiveGesture()
{
    // Only the view-owned lane resize cancels here — restoring its starting height. The
    // controller-owned point move/insert drag cancels through the controller's Esc handler, routed
    // (through the published drag preview) by the editor's shared Escape ladder.
    if (!m_drag.has_value())
    {
        return false;
    }

    const core::ToneAutomationLaneViewState& lane = m_state.lanes[m_drag->lane_index];
    m_lane_heights[{lane.instance_id, lane.param_id}] = m_drag->start_height;
    if (m_heights_changed_callback)
    {
        m_heights_changed_callback();
    }
    m_drag.reset();

    // Adopt a state deferred during the resize, exactly as its uncommitted release would.
    if (m_pending_state.has_value())
    {
        const core::ToneAutomationViewState pending = std::move(*m_pending_state);
        m_pending_state.reset();
        applyState(pending);
    }
    repaint();
    return true;
}

// Emits the points-edit intent that inserts a new point into a lane (echoing every existing
// point bit-identically) and selects it — the creation primitive behind the caret's typed
// values.
void ToneAutomationLanesView::requestPointInsert(
    const core::ToneAutomationLaneViewState& lane, const common::core::GridPosition& position,
    float value)
{
    const bool occupied = std::ranges::any_of(
        lane.points, [&position](const core::ToneAutomationPointViewState& point) {
            return point.position == position;
        });
    if (occupied)
    {
        // The slot is occupied after all (a state race); creating nothing is the answer.
        return;
    }
    std::vector<common::core::ToneAutomationPoint> points = copyLanePoints(lane, nullptr);
    insertPointSorted(
        points, common::core::ToneAutomationPoint{.position = position, .norm_value = value});
    m_listener.onToneAutomationPointsEditRequested(
        lane.instance_id, lane.param_id, std::move(points));
    m_listener.onToneAutomationPointSelectRequested(lane.instance_id, lane.param_id, position);
}

// Opens the typed-value editor at the armed lane caret, seeded with the typed digit: the
// keyboard mirror of double-click value entry (the typing rule on lane rows, §9b). Committing
// creates an on-caret point with the typed value, or retypes the point already at the slot.
bool ToneAutomationLanesView::beginCaretValueEntry(int digit)
{
    if (!m_state.lane_caret.has_value() || m_state.lane_caret->lane_index >= m_state.lanes.size() ||
        !isShowing())
    {
        return false;
    }
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[m_state.lane_caret->lane_index];
    if (!lane.resolved)
    {
        return false;
    }
    const common::core::GridPosition caret_position = m_state.lane_caret->position;

    auto content = std::make_unique<PointValueEditorContent>(
        juce::String{digit},
        PointValueEditorContent::InitialCaret::AtEnd,
        [safe_this = juce::Component::SafePointer<ToneAutomationLanesView>{this},
         tone_ref = m_state.tone_document_ref,
         instance_id = lane.instance_id,
         param_id = lane.param_id,
         caret_position](const juce::String& text) {
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
            const auto lane_now = std::ranges::find_if(
                safe_this->m_state.lanes, [&](const core::ToneAutomationLaneViewState& candidate) {
                    return candidate.instance_id == instance_id && candidate.param_id == param_id;
                });
            if (lane_now == safe_this->m_state.lanes.end())
            {
                return;
            }
            const float value = snappedValueForLane(*parsed, *lane_now);
            const bool occupied = std::ranges::any_of(
                lane_now->points, [&](const core::ToneAutomationPointViewState& point) {
                    return point.position == caret_position;
                });
            if (occupied)
            {
                safe_this->requestPointReplace(
                    instance_id, param_id, caret_position, caret_position, value);
            }
            else
            {
                safe_this->requestPointInsert(*lane_now, caret_position, value);
            }
        });

    // Anchor the callout to the caret square's on-screen location.
    const std::optional<juce::Rectangle<float>> square = laneCaretSquare();
    const juce::Rectangle<int> local_anchor =
        square.has_value() ? square->getSmallestIntegerContainer()
                           : juce::Rectangle<int>{getWidth() / 2, getHeight() / 2, 8, 8};
    juce::CallOutBox::launchAsynchronously(
        std::move(content), localAreaToGlobal(local_anchor), nullptr);
    return true;
}

void ToneAutomationLanesView::mouseDoubleClick(const juce::MouseEvent& event)
{
    const std::optional<Hit> hit = hitAt(event.getPosition(), event.mods.isAltDown());
    if (!hit.has_value())
    {
        return;
    }
    const auto* const point_hit = std::get_if<PointHit>(&*hit);
    if (point_hit == nullptr)
    {
        return;
    }
    // The controller ignores a double-click's second press (its click count is two), so no drag is
    // in flight to drop: the editor simply opens over the point the first click already selected.
    showPointValueEditor(*point_hit);
}

void ToneAutomationLanesView::mouseExit(const juce::MouseEvent& /*event*/)
{
    // A resize or a controller-owned point drag keeps its overlays even when the pointer leaves the
    // component (JUCE keeps delivering drag events to the captured component); a plain hover that
    // leaves the lanes clears the readout and ends the hover so the controller clears the ghost.
    if (!m_drag.has_value() && !m_state.drag_preview.has_value())
    {
        setValueReadout(std::nullopt);
        m_listener.onToneAutomationPointerExit();
    }
}

std::vector<core::ToneAutomationLaneExtent> ToneAutomationLanesView::laneValueBandExtents() const
{
    // The band is the drawable region a point rides — inset from the lane's outer bounds by the
    // value-band inset above and the resize band below — so a point at value 0 or 1 never sits in
    // the resize grab. Computing it here keeps those pixel constants view-side.
    const std::vector<LaneExtent> extents = laneExtents();
    std::vector<core::ToneAutomationLaneExtent> bands;
    bands.reserve(m_state.lanes.size());
    for (std::size_t lane_index = 0; lane_index < m_state.lanes.size(); ++lane_index)
    {
        const ValueBand band = valueBandFor(extents[lane_index]);
        bands.push_back(
            core::ToneAutomationLaneExtent{
                .value_band_top = static_cast<float>(band.top),
                .value_band_height = static_cast<float>(band.height),
            });
    }
    return bands;
}

core::ToneAutomationPointerEvent ToneAutomationLanesView::makePointerEvent(
    const juce::MouseEvent& event, std::optional<std::size_t> lane_index) const
{
    core::ToneAutomationPointerEvent pointer_event;
    pointer_event.geometry.visible_timeline = m_visible_timeline;
    pointer_event.geometry.content_width = getWidth();
    pointer_event.x = static_cast<float>(event.getPosition().x);
    pointer_event.y = static_cast<float>(event.getPosition().y);
    pointer_event.clicks = event.getNumberOfClicks();
    pointer_event.dragged_since_down = event.mouseWasDraggedSinceMouseDown();
    pointer_event.modifiers.alt = event.mods.isAltDown();
    pointer_event.modifiers.shift = event.mods.isShiftDown();
    // A resolved lane (Down/Move) names its identity, index, value shape, and the band-extent
    // vector the controller maps y to a value with. A Drag/Up rides the gesture the controller
    // froze on Down, so it leaves the lane geometry default and never
    // rebuilds the extents mid-drag.
    if (lane_index.has_value() && *lane_index < m_state.lanes.size())
    {
        const core::ToneAutomationLaneViewState& lane = m_state.lanes[*lane_index];
        pointer_event.instance_id = lane.instance_id;
        pointer_event.param_id = lane.param_id;
        pointer_event.lane_index = *lane_index;
        pointer_event.lane_is_discrete = lane.is_discrete;
        pointer_event.lane_discrete_value_count = lane.discrete_value_count;
        pointer_event.lane_extents = laneValueBandExtents();
    }
    return pointer_event;
}

void ToneAutomationLanesView::applyDragPreviewReadout(juce::Point<int> anchor)
{
    if (!m_state.drag_preview.has_value() ||
        m_state.drag_preview->lane_index >= m_state.lanes.size())
    {
        return;
    }
    const core::ToneAutomationDragPreviewRef& preview = *m_state.drag_preview;
    const core::ToneAutomationLaneViewState& lane = m_state.lanes[preview.lane_index];
    setValueReadout(
        ValueReadout{
            .anchor = anchor,
            .text = readoutTextFor(preview.position, lane, preview.value),
        });
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
            // arrangement entry); the open-lane close then removes any session-scoped open lane so
            // the row disappears instead of falling back to an anchor-only lane. The close is a
            // no-op for a package-loaded authored lane that never had an open entry.
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
        PointValueEditorContent::InitialCaret::SelectAll,
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
    const float point_y = valueBandY(valueBandFor(extents[hit.lane_index]), point.norm_value);
    const int point_x =
        static_cast<int>(xForSeconds(point.seconds).value_or(static_cast<float>(getWidth()) / 2));
    const juce::Rectangle<int> anchor =
        localAreaToGlobal(juce::Rectangle<int>{point_x - 4, static_cast<int>(point_y) - 4, 8, 8});
    juce::CallOutBox::launchAsynchronously(std::move(content), anchor, nullptr);
}

// Emits the points-edit intent that removes the point at the given position from its lane —
// the right-click "Delete Point" menu's path (keyboard Delete routes through the controller's
// unified selection dispatch); a no-op when the lane no longer exists (a state push may have
// removed it while a menu was open).
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
        m_listener.onToneAutomationPointsEditRequested(
            instance_id, param_id, copyLanePoints(lane, &position));
        return;
    }
}

// Emits the points-edit intent that replaces one point's position and value, echoing every other
// point bit-identically. Selects the edited point at its new position before committing, so the
// synchronous state push keeps it selected.
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
        const bool replaces_a_point =
            std::ranges::any_of(lane.points, [&](const core::ToneAutomationPointViewState& p) {
                return p.position == position;
            });
        if (!replaces_a_point)
        {
            return;
        }
        std::vector<common::core::ToneAutomationPoint> points = copyLanePoints(lane, &position);
        insertPointSorted(
            points,
            common::core::ToneAutomationPoint{
                .position = new_position,
                .norm_value = new_value,
            });
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
