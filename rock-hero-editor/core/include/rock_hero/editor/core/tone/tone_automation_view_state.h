/*!
\file tone_automation_view_state.h
\brief Framework-free render state for the editor's tone parameter automation lanes.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*! \brief One automation curve point rendered on a lane. */
struct ToneAutomationPointViewState
{
    /*!
    \brief Exact musical position of the point (the stored truth).

    The view echoes this back bit-identically for points a gesture did not move, so value-only
    edits never re-derive musical positions from display seconds.
    */
    common::core::GridPosition position;

    /*! \brief Display position in absolute song seconds, derived through the tempo map. */
    double seconds{0.0};

    /*! \brief Parameter value normalised to `[0, 1]`. */
    float norm_value{0.0F};

    /*! \brief Segment shape toward the next point, in `[-1, 1]`; 0 is linear. */
    float curve_shape{0.0F};

    /*!
    \brief Compares two point view states by their stored values.
    \param lhs Left-hand point view state.
    \param rhs Right-hand point view state.
    \return True when both point view states store equal values.
    */
    friend bool operator==(
        const ToneAutomationPointViewState& lhs, const ToneAutomationPointViewState& rhs) = default;
};

/*! \brief One automation lane (a plugin parameter) shown beneath the tone strip. */
struct ToneAutomationLaneViewState
{
    /*! \brief Owning plugin instance id. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief User-facing parameter name. */
    std::string name;

    /*! \brief User-facing owning-plugin name, so multi-plugin chains stay unambiguous. */
    std::string plugin_name;

    /*! \brief True when the parameter is stepped rather than continuous. */
    bool is_discrete{false};

    /*!
    \brief Number of discrete steps for a stepped parameter; 0 for a continuous one.

    Point drags on a discrete lane snap to the nearest of these evenly-spaced steps, so a toggle
    moves only between its two states instead of any value in between.
    */
    int discrete_value_count{0};

    /*! \brief False when the parameter no longer resolves; the lane renders disabled. */
    bool resolved{true};

    /*!
    \brief Parameter's live value at projection time, normalised to `[0, 1]`.

    Lanes without authored points render this as a flat tracking line (the view refreshes it at
    render cadence through the live-value provider); authored lanes ignore it.
    */
    float live_norm_value{0.0F};

    /*! \brief Parameter's default value, normalised to `[0, 1]`; a double-click resets a point here. */
    float default_norm_value{0.0F};

    /*! \brief Curve points in ascending time (the whole curve, absolute seconds). */
    std::vector<ToneAutomationPointViewState> points;

    /*!
    \brief Compares two lane view states by their stored values.
    \param lhs Left-hand lane view state.
    \param rhs Right-hand lane view state.
    \return True when both lane view states store equal values.
    */
    friend bool operator==(
        const ToneAutomationLaneViewState& lhs, const ToneAutomationLaneViewState& rhs) = default;
};

/*! \brief One parameter the "+" picker can open a new lane for. */
struct ToneAutomationParamChoice
{
    /*! \brief Owning plugin instance id. */
    std::string instance_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief User-facing parameter name. */
    std::string name;

    /*! \brief Parameter group for submenu nesting; empty when ungrouped. */
    std::string group;

    /*! \brief User-facing owning-plugin name for the picker's per-plugin hierarchy. */
    std::string plugin_name;

    /*!
    \brief Compares two parameter choices by their stored values.
    \param lhs Left-hand choice.
    \param rhs Right-hand choice.
    \return True when both choices store equal values.
    */
    friend bool operator==(
        const ToneAutomationParamChoice& lhs, const ToneAutomationParamChoice& rhs) = default;
};

/*! \brief Locates the selected automation point inside a lane list. */
struct ToneAutomationSelectedPointRef
{
    /*! \brief Index of the owning lane in \ref ToneAutomationViewState::lanes. */
    std::size_t lane_index{0};

    /*! \brief Index of the point in that lane's \ref ToneAutomationLaneViewState::points. */
    std::size_t point_index{0};

    /*!
    \brief Compares two selected-point references by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both references store equal values.
    */
    friend bool operator==(
        const ToneAutomationSelectedPointRef& lhs,
        const ToneAutomationSelectedPointRef& rhs) = default;
};

/*! \brief The armed marker caret riding an automation lane row (the row axis, §9b). */
struct ToneAutomationLaneCaretRef
{
    /*! \brief Index of the caret's lane in \ref ToneAutomationViewState::lanes. */
    std::size_t lane_index{0};

    /*! \brief Caret slot in absolute song seconds, derived through the tempo map. */
    double seconds{0.0};

    /*! \brief Exact musical position of the caret slot. */
    common::core::GridPosition position{};

    /*!
    \brief Start of the caret's measure in seconds, for the keep-in-view window glide.

    Lane caret navigation glides the window exactly as chart caret navigation does (the same
    measure-reveal rule); published with the caret so the view never re-derives measure bounds
    from the tempo map.
    */
    double measure_start_seconds{0.0};

    /*! \brief End of the caret's measure (the next measure's start) in seconds. */
    double measure_end_seconds{0.0};

    /*!
    \brief Compares two lane caret references by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both references store equal values.
    */
    friend bool operator==(
        const ToneAutomationLaneCaretRef& lhs, const ToneAutomationLaneCaretRef& rhs) = default;
};

/*!
\brief The in-flight point move/insert drag's preview, published while the gesture holds.

The controller owns the move/insert drag state machine (neighbor clamp, delta value, Shift axis
lock, editable-window clamp) and republishes this preview on every advance; the view paints it
exactly as it paints a stored point, so the gesture previews without mutating the model. Present
only once the drag has actually produced a preview — an Alt-insert from the press, an existing-point
grab once it crosses the click threshold — so a plain point click (which selects, never moves)
publishes none. The lane sibling of the tablature lane's \ref ChartMarqueeViewState.
*/
struct ToneAutomationDragPreviewRef
{
    /*! \brief Index of the drag's lane in \ref ToneAutomationViewState::lanes. */
    std::size_t lane_index{0};

    /*! \brief Exact musical position of the preview point (the drawn x derives from it). */
    common::core::GridPosition position{};

    /*! \brief Preview point value normalised to `[0, 1]` (the drawn y). */
    float value{0.0F};

    /*! \brief True when the drag is inserting a new point; false when moving an existing one. */
    bool is_new_point{false};

    /*!
    \brief Index of the moved point in its lane, hidden while the preview stands in for it.

    Valid only when \ref is_new_point is false: the view skips the stored point at this index and
    draws the preview in its place, so a moved point never double-draws. An insert hides nothing.
    */
    std::size_t source_point_index{0};

    /*!
    \brief Compares two drag-preview references by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both references store equal values.
    */
    friend bool operator==(
        const ToneAutomationDragPreviewRef& lhs, const ToneAutomationDragPreviewRef& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // value member; exact equality is intended (a dirty-checked republish of the same preview).
        return lhs.lane_index == rhs.lane_index && lhs.position == rhs.position &&
               std::is_eq(lhs.value <=> rhs.value) && lhs.is_new_point == rhs.is_new_point &&
               lhs.source_point_index == rhs.source_point_index;
    }
};

/*! \brief The Alt-held insert ghost's rendered position over an empty lane slot (§9b). */
struct ToneAutomationInsertGhostRef
{
    /*! \brief Index of the ghost's lane in \ref ToneAutomationViewState::lanes. */
    std::size_t lane_index{0};

    /*! \brief Ghost slot in absolute song seconds, derived through the tempo map like the caret. */
    double seconds{0.0};

    /*!
    \brief Compares two insert-ghost references by their stored values.
    \param lhs Left-hand reference.
    \param rhs Right-hand reference.
    \return True when both references store equal values.
    */
    friend bool operator==(
        const ToneAutomationInsertGhostRef& lhs, const ToneAutomationInsertGhostRef& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // seconds member; exact equality is intended (the ghost snaps to an exact grid slot).
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.lane_index == rhs.lane_index;
    }
};

/*! \brief View-facing state for the selected tone's automation lanes. */
struct ToneAutomationViewState
{
    /*! \brief Selected tone whose lanes are shown; empty when no region is selected. */
    std::string tone_document_ref;

    /*! \brief Shown automation lanes for the selected tone, in display order. */
    std::vector<ToneAutomationLaneViewState> lanes;

    /*!
    \brief The selected point, re-resolved against \ref lanes on every push.

    The editor-wide selection stores the point durably (lane keys plus exact musical position);
    this reference is the per-push resolution of that selection against the lanes actually
    published, so a selection whose point vanished simply publishes as nothing instead of
    pointing at the wrong glyph — the same self-healing rule chart selection indices follow.
    */
    std::optional<ToneAutomationSelectedPointRef> selected_point;

    /*!
    \brief The armed caret when it rides one of these lanes, re-resolved per push.

    The marker stores the lane durably (instance + parameter); this is its per-push resolution
    against the published lanes, so a caret whose lane is not visible publishes as nothing.
    While present, the caret square draws on the lane and the paused cursor line hides — the
    armed marker's one-glyph rule, extended to lane rows (§9b).
    */
    std::optional<ToneAutomationLaneCaretRef> lane_caret;

    /*!
    \brief The Alt-held insert ghost, present while Alt hovers an insertable empty lane slot.

    Rendered as a hollow ring on the curve where an Alt+click would plant an on-curve point —
    the neutral-create verb's mouse form (§9b), the lane sibling of the tab lane's fret-0 note
    ring. The controller resolves snap and occupancy exactly as the click would, and publishes
    the ghost only when the ring would be honest: absent over occupied slots (an insert there
    would no-op), without Alt, or while playing, so the affordance never advertises an action it
    would not perform (§7).
    */
    std::optional<ToneAutomationInsertGhostRef> insert_ghost;

    /*!
    \brief The in-flight point move/insert drag preview, present only while a drag is producing one.

    The controller owns the gesture and republishes this on every advance; the view paints it in
    place of the moved point (or as a new point). While present it also masks the insert ghost and
    routes Esc to the controller's gesture cancel, exactly as the tab lane's marquee does.
    */
    std::optional<ToneAutomationDragPreviewRef> drag_preview;

    /*! \brief Parameters without a lane yet, offered by the empty lane's "+" picker. */
    std::vector<ToneAutomationParamChoice> available_parameters;

    /*!
    \brief True when parameter listing failed outright (tone not loaded in the rig).

    Distinguishes "this tone has nothing to automate" from "the backend could not answer", so the
    picker can explain itself instead of both states rendering as an empty offer.
    */
    bool parameters_unavailable{false};

    /*!
    \brief Compares two tone-automation view states by their stored values.
    \param lhs Left-hand tone-automation view state.
    \param rhs Right-hand tone-automation view state.
    \return True when both tone-automation view states store equal values.
    */
    friend bool operator==(const ToneAutomationViewState& lhs, const ToneAutomationViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
