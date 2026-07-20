/*!
\file editor_selection.h
\brief The editor-wide selection: one sum type whose alternatives are the per-surface selections.
*/

#pragma once

#include "chart/chart_selection.h"

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

// A grid-locked time span across every surface (the Shift+arrow / Shift+click time selection).
// Both endpoints are display-grid positions — a boundary is never off-grid (decision B) — stored
// as an anchor (the fixed end) and a focus (the end an extend moves), so extension knows which
// side grows; the rendered/edited span is the ordered pair [min, max]. A distinct EditorSelection
// kind, so making one structurally evicts any object selection and vice versa (decision D).
struct TimeSelection
{
    // The end that stays put while the span is extended (the last non-Shift marker position,
    // grid-snapped).
    common::core::GridPosition anchor{};

    // The end an extend moves; equals the anchor for a zero-width range before the first extend.
    common::core::GridPosition focus{};

    // The span's earlier endpoint (the two are ordered on demand rather than on every extend).
    [[nodiscard]] const common::core::GridPosition& start() const noexcept
    {
        return focus < anchor ? focus : anchor;
    }

    // The span's later endpoint.
    [[nodiscard]] const common::core::GridPosition& end() const noexcept
    {
        return focus < anchor ? anchor : focus;
    }

    friend bool operator==(const TimeSelection& lhs, const TimeSelection& rhs) = default;
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
// region, an automation point, and a time span are alternatives of one sum type, so selecting on
// any surface structurally replaces the selection on every other — two live selections are
// unrepresentable and the old Delete precedence ladder has nothing to disambiguate. std::monostate
// is "nothing selected"; a held-but-empty ChartSelection means the same thing. Selection kinds keep
// their shipped lifecycles: chart selection and the time span survive seeks and clear on play,
// while the tone-region and automation-point kinds also clear whenever the cursor moves (the
// transport-move rule). The time span is the object-vs-time exclusivity of decision D (2026-07-20):
// making a range dissolves the object selection and demotes the marker to passive, and any object
// gesture evicts the range in turn.
using EditorSelection = std::variant<
    std::monostate, ChartSelection, ToneRegionSelection, AutomationPointSelection, TimeSelection>;

} // namespace rock_hero::editor::core
