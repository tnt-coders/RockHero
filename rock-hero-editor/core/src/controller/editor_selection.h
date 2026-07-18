/*!
\file editor_selection.h
\brief The editor-wide selection: one sum type whose alternatives are the per-surface selections.
*/

#pragma once

#include "chart/chart_selection.h"

#include <compare>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <variant>

namespace rock_hero::editor::core
{

// A formally selected tone region (the Delete target with the white outline).
struct ToneRegionSelection
{
    // Stable region id within the current arrangement's tone track.
    std::string region_id;

    friend bool operator==(const ToneRegionSelection& lhs, const ToneRegionSelection& rhs) =
        default;
};

// A selected automation point, identified durably by lane keys plus exact musical position so it
// survives state rebuilds that reorder lanes or reindex points.
struct AutomationPointSelection
{
    // Plugin instance owning the automated parameter.
    std::string instance_id;

    // Parameter id within the plugin.
    std::string param_id;

    // Exact musical position of the point.
    common::core::GridPosition position{};

    friend bool operator==(
        const AutomationPointSelection& lhs, const AutomationPointSelection& rhs) = default;
};

// Exactly one selection exists editor-wide (interaction model, 2026-07-18): chart notes, a tone
// region, and an automation point are alternatives of one sum type, so selecting on any surface
// structurally replaces the selection on every other — two live selections are unrepresentable
// and the old Delete precedence ladder has nothing to disambiguate. std::monostate is "nothing
// selected"; a held-but-empty ChartSelection means the same thing. Selection kinds keep their
// shipped lifecycles: chart selection survives seeks and clears on play, while the tone-region
// and automation-point kinds also clear whenever the cursor moves (the transport-move rule).
using EditorSelection =
    std::variant<std::monostate, ChartSelection, ToneRegionSelection, AutomationPointSelection>;

} // namespace rock_hero::editor::core
