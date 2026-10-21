/*!
\file marker_judge_context.h
\brief What the marker funnel hands every snapshot it judges.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/session/session.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief The state a marker snapshot is validated against: the session and the displayed chart's
notes layer.

One context for every snapshot kind, so the funnel judges each the same way and names no kind:
a snapshot reads what its rules need and ignores the rest. The notes layer travels here because a
fret-hand position is judged against the notes it must cover, and the controller already holds
their derivation; a position edit leaves the notes untouched, so the layer is current for the model
being judged.
*/
struct MarkerJudgeContext
{
    /*! \brief The session the snapshot would land in. */
    const common::core::Session& session;

    /*! \brief The current arrangement's notes layer; empty when no chart is displayed. */
    const common::core::ChartResolutions& notes_layer;
};

/*!
\brief What normalizing a marker model converted and derived.

The same for every snapshot kind, so the funnel names no kind: only the song timeline's
normalization converts content or derives a notes layer, and every other kind's is empty.
*/
struct MarkerModelNormalization
{
    /*! \brief The content conversions, which the landing reports. */
    std::vector<common::core::ChartConversion> conversions;

    /*!
    \brief Each arrangement's notes layer under the normalized model, index-parallel to the song's
    arrangements and absent where an arrangement has no chart; empty where none were derived. The
    landing seeds the displayed one into the memo, so one edit walks each chart once.
    */
    std::vector<std::optional<common::core::ChartResolutions>> notes_layers;
};

} // namespace rock_hero::editor::core
