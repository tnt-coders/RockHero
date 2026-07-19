/*!
\file tone_automation_view_state.h
\brief Framework-free render state for the editor's tone parameter automation lanes.
*/

#pragma once

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
