/*!
\file tone_automation_view_state.h
\brief Framework-free render state for the editor's tone parameter automation lanes.
*/

#pragma once

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

    /*! \brief True when the parameter is stepped rather than continuous. */
    bool is_discrete{false};

    /*! \brief False when the parameter no longer resolves; the lane renders disabled. */
    bool resolved{true};

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

/*! \brief View-facing state for the selected tone's automation lanes. */
struct ToneAutomationViewState
{
    /*! \brief Selected tone whose lanes are shown; empty when no region is selected. */
    std::string tone_document_ref;

    /*! \brief Shown automation lanes for the selected tone, in display order. */
    std::vector<ToneAutomationLaneViewState> lanes;

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
