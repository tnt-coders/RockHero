/*!
\file marker_judge_context.h
\brief What the marker funnel hands every snapshot it judges.
*/

#pragma once

#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/session/session.h>

namespace rock_hero::editor::core
{

/*!
\brief The state a marker snapshot is validated against: the session, and the displayed chart's
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
    const common::core::ChartResolutions& chart;
};

} // namespace rock_hero::editor::core
