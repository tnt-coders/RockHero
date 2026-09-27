/*!
\file fret_hand_positions_snapshot.h
\brief Whole fret-hand placement stream snapshot committed and undone by the shared marker commit
funnel.
*/

#pragma once

#include "controller/edit_focus.h"

#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/session/session.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief The current arrangement's whole fret-hand placement stream.

Arrangement-scoped, like the tone model: the placements are part of the chart the arrangement
owns. A placement is two small fields and a chart carries at most a few hundred of them, so the
whole stream is cheaper to carry than a diff would be to compute, and the round trip is exact by
assignment.
*/
struct FretHandPositionsSnapshot
{
    /*! \brief The chart's fret-hand placements. */
    std::vector<common::core::FretHandPosition> placements;

    /*!
    \brief Reads the current arrangement's placement stream.
    \param session Session whose current arrangement owns the chart.
    \return The stream as it now stands, or an empty snapshot when no chart is loaded.
    */
    [[nodiscard]] static FretHandPositionsSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this stream onto the current arrangement's chart.

    \param session Session whose current arrangement owns the chart.
    \return False when no chart is loaded to carry the stream.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*! \brief Restores the ascending position order every consumer reads the stream in. */
    void normalize();

    /*!
    \brief Reports the first structural rule this stream breaks.

    \param session Session supplying the chart's tuning and the tempo map the positions address.
    \return The violation to report, or empty when the stream satisfies every rule.
    */
    [[nodiscard]] std::optional<std::string> validate(const common::core::Session& session) const;

    /*!
    \brief The placement an undo or redo landing on this stream brings into focus.
    \param replaced The stream the transition replaces.
    \return The first placement it added, moved or retyped — else the first it removed, shown by
            its former position — or nothing when the streams hold the same placements.
    */
    [[nodiscard]] EditFocus focusReplacing(const FretHandPositionsSnapshot& replaced) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal placement streams.
    */
    friend bool operator==(
        const FretHandPositionsSnapshot& lhs, const FretHandPositionsSnapshot& rhs) = default;
};

} // namespace rock_hero::editor::core
