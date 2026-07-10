/*!
\file tone_automation.h
\brief Arrangement-owned plugin-parameter automation: musical points keyed by durable plugin ids.
*/

#pragma once

#include <compare>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief One automation curve point at an exact musical position.

The musical position is the source of truth; playback seconds are derived through the song tempo
map, so points follow grid and tempo-map edits.
*/
struct ToneAutomationPoint
{
    /*! \brief Exact musical position of the point. */
    GridPosition position;

    /*! \brief Parameter value normalised to `[0, 1]`. */
    float norm_value{0.0F};

    /*! \brief Segment shape toward the next point, in `[-1, 1]`; 0 is linear. */
    float curve_shape{0.0F};

    /*!
    \brief Compares two automation points for equal value.
    \param lhs Left-hand point.
    \param rhs Right-hand point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(
        const ToneAutomationPoint& lhs, const ToneAutomationPoint& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.position == rhs.position && std::is_eq(lhs.norm_value <=> rhs.norm_value) &&
               std::is_eq(lhs.curve_shape <=> rhs.curve_shape);
    }
};

/*!
\brief One plugin parameter's automation curve within an arrangement.

Keyed by the plugin's durable minted id (carried through the tone document's plugin records) plus
the parameter id, so the association survives chain reorder and remove. An entry whose plugin id no
longer resolves to a live plugin stays persisted and renders as an unresolved lane.
*/
struct ToneParameterAutomation
{
    /*! \brief Durable minted id of the plugin that owns the parameter. */
    std::string plugin_id;

    /*! \brief Parameter id within the plugin. */
    std::string param_id;

    /*! \brief Curve points in strictly ascending musical position; never empty when persisted. */
    std::vector<ToneAutomationPoint> points;

    /*!
    \brief Compares two automation entries for equal value.
    \param lhs Left-hand entry.
    \param rhs Right-hand entry.
    \return True when both entries store equal values.
    */
    friend bool operator==(const ToneParameterAutomation& lhs, const ToneParameterAutomation& rhs) =
        default;
};

/*!
\brief Checks one automation entry against the structural rules.

Rules: non-empty plugin and parameter ids; at least one point; points at valid grid positions for
\p tempo_map (existing measure/beat, offset in `[0, 1)`); strictly ascending positions; normalised
values in `[0, 1]`; curve shapes in `[-1, 1]`.

\param automation Entry to check.
\param tempo_map Tempo map that defines the valid musical grid.
\return True when the entry satisfies every structural rule.
*/
[[nodiscard]] bool isValidToneParameterAutomation(
    const ToneParameterAutomation& automation, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
