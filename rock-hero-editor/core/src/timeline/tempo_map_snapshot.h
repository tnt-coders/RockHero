/*!
\file tempo_map_snapshot.h
\brief Whole tempo-map snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include "controller/edit_focus.h"
#include "controller/marker_judge_context.h"
#include "controller/refusal_light.h"

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief The song's whole tempo map.

Song-level, like the sections: every arrangement is placed in time by the one map. A map is a
handful of anchors and meters, so the whole map is cheaper to carry than a diff would be to
compute, and a ripple, which rewrites every later anchor, needs no special undo.
*/
struct TempoMapSnapshot
{
    /*! \brief The song's tempo map. */
    common::core::TempoMap tempo_map;

    /*! \brief The grid is truth: content and the keyboard keep their addresses across a landing. */
    static constexpr GridCarry g_grid_carry = GridCarry::KeepAddress;

    /*!
    \brief Reads the song's current tempo map.
    \param session Session owning the song.
    \return The map as it now stands.
    */
    [[nodiscard]] static TempoMapSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this map onto the song.
    \param session Session owning the song.
    \return True always; the song always exists, so a tempo map has nowhere else to land.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*! \brief Nothing to restore: every tempo verb builds its map in order. */
    void normalize();

    /*!
    \brief Reports the first structural rule this map breaks.
    \param context The funnel's context; its session's map is the one on screen, which the
           culprits the rule names are lit on (\ref tempoMapCulprits).
    \return The refusal to report, or empty when the map satisfies every rule.
    */
    [[nodiscard]] std::optional<PlanRefused> validate(const MarkerJudgeContext& context) const;

    /*!
    \brief The anchor an undo or redo landing on this map brings into focus.

    The first anchor the transition added, moved or removed. The terminal anchor draws no chip, so
    when it is the one that moved — a typed tempo on the last chip — the focus is the anchor that
    opens the span it ends.

    \param replaced The map the transition replaces.
    \return The changed anchor, or nothing when the maps hold the same anchors.
    */
    [[nodiscard]] EditFocus focusReplacing(const TempoMapSnapshot& replaced) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal tempo maps.
    */
    friend bool operator==(const TempoMapSnapshot& lhs, const TempoMapSnapshot& rhs) = default;
};

/*!
\brief The culprits a tempo-map rule names, as the chips they are drawn by on the map standing on
screen: each anchor the error names by its position, and each time signature by the first measure
its place falls in under `standing`.

The one reading of a tempo-map error's records for every refusal that carries one — a tempo edit's,
the tempo map's commit and the song timeline's — so no verb restates a rule to find what stood in
its way. A record the screen draws no chip for (the terminal anchor, a place in a measure no reign
starts at) lights nothing, as an unresolved target does.

\param error The rule's error.
\param standing The map on screen, which the edit would replace.
\return The light naming every record the error carries.
*/
[[nodiscard]] RefusalLight tempoMapCulprits(
    const common::core::TempoMapError& error, const common::core::TempoMap& standing);

} // namespace rock_hero::editor::core
