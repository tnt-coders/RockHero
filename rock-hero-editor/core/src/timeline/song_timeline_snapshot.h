/*!
\file song_timeline_snapshot.h
\brief Whole song-timeline snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include "controller/edit_focus.h"
#include "controller/marker_judge_context.h"

#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief The song's whole timeline: the snapshot every whole-song verb commits, the time-signature
verbs and Scale Tempo under the Note Lock.

Such an edit keeps every object's time and re-addresses it, in every arrangement, so its one undo
entry has to restore all of it field-equal: the map, the sections and every arrangement's chart,
tone track and automation (\ref common::core::SongTimeline, the one definition of that set). Whole
rather than a diff, as every marker snapshot is, because the re-address is not invertible where a
scale rounded, so no replay could restore it.
*/
struct SongTimelineSnapshot
{
    /*! \brief The song's timeline. */
    common::core::SongTimeline timeline;

    /*! \brief Every time is held: content and the keyboard keep their times across a landing. */
    static constexpr GridCarry g_grid_carry = GridCarry::KeepTime;

    /*!
    \brief Reads the song's current timeline.
    \param session Session owning the song.
    \return The timeline as it now stands.
    */
    [[nodiscard]] static SongTimelineSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this timeline onto the song.
    \param session Session owning the song.
    \return False when the song's arrangements are no longer the ones this was read from.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*!
    \brief Brings every chart's shape holds to their normal form under this timeline's map
    (\ref common::core::normalizeShapeHolds): a re-address keeps each place's time, which can round
    a span's close away from a hold's end, or a hold onto no length.

    Nothing else: the verbs state the meter list in order, and a time signature restating its
    reign stays in the live model while its chip is selected. The history's and the file's form
    without it is the funnel's (`writtenModel`, \ref common::core::withoutRedundantTimeSignatures).

    \return One conversion per hold the normal form changed, its place naming the arrangement;
            empty where every hold
            already fit.
    */
    [[nodiscard]] std::vector<common::core::ChartConversion> normalize();

    /*!
    \brief Reports the first structural rule this timeline breaks, in any of its slices
    (\ref common::core::validateSongTimelineRules).
    \param context Unused; every rule reads the timeline's own map.
    \return The violation to report, or empty when the timeline satisfies every rule.
    */
    [[nodiscard]] std::optional<std::string> validate(const MarkerJudgeContext& context) const;

    /*!
    \brief The time signature an undo or redo landing on this timeline brings into focus: the first
    one the transition added, restated, renumbered or removed.
    \param replaced The timeline the transition replaces.
    \return The changed time signature, or nothing when both hold the same meters.
    */
    [[nodiscard]] EditFocus focusReplacing(const SongTimelineSnapshot& replaced) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal timelines.
    */
    friend bool operator==(const SongTimelineSnapshot& lhs, const SongTimelineSnapshot& rhs) =
        default;
};

} // namespace rock_hero::editor::core
