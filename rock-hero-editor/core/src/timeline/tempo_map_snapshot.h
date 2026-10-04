/*!
\file tempo_map_snapshot.h
\brief Whole tempo-map snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include "controller/edit_focus.h"

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
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
    \param session Unused; the tempo-map rules need nothing beyond the map.
    \return The violation to report, or empty when the map satisfies every rule.
    */
    [[nodiscard]] std::optional<std::string> validate(const common::core::Session& session) const;

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
    \brief Carries the cursor's place across a tempo commit: unchanged, because the grid is truth
    here, so the cursor keeps its musical place and moves in time with the map.
    \param from Map before the commit; unused.
    \param to Map after the commit; unused.
    \param place The cursor's place under \p from.
    \return \p place itself.
    */
    [[nodiscard]] static common::core::GridPosition carryCursor(
        const common::core::TempoMap& from, const common::core::TempoMap& to,
        common::core::GridPosition place) noexcept;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal tempo maps.
    */
    friend bool operator==(const TempoMapSnapshot& lhs, const TempoMapSnapshot& rhs) = default;
};

} // namespace rock_hero::editor::core
