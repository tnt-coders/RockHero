/*!
\file tone_model_snapshot.h
\brief Whole tone-model snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include "controller/edit_focus.h"
#include "controller/marker_judge_context.h"
#include "controller/refusal_light.h"

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief An arrangement's whole tone model: the catalog and the track that references it.

The two are captured together because no tone verb changes one without the other being able to
change with it — a retone can mint a catalog entry and prune the one it replaces, a delete prunes,
a rename touches only the catalog and a split only the track.

\note Purely a model snapshot. A tone document minted by a verb, and the rig branch it loads into,
      stay in place across undo and redo; the model is the source of truth for the next rig
      reload, and an unreferenced branch is unselected and harmless.
*/
struct ToneModelSnapshot
{
    /*! \brief The arrangement's named tone catalog. */
    std::vector<common::core::Tone> tones;

    /*! \brief The arrangement's tone track. */
    common::core::ToneTrack tone_track;

    /*!
    \brief Reads the current arrangement's tone model.
    \param session Session whose current arrangement owns the model.
    \return The model as it now stands, or an empty snapshot when no arrangement is loaded.
    */
    [[nodiscard]] static ToneModelSnapshot capture(const common::core::Session& session);

    /*!
    \brief Writes this model onto the current arrangement.

    \param session Session whose current arrangement receives the model.
    \return True when an arrangement carried the model; false when none is loaded.
    */
    [[nodiscard]] bool applyTo(common::core::Session& session) const;

    /*!
    \brief Drops every catalog tone no region references any more.

    A phantom entry would keep owning its name while nothing could reach it: the picker offers only
    tones some region references, so an unreferenced entry is already unofferable. Run on every
    tone commit, so the two verbs that can take a tone's last reference (delete, retone) can never
    disagree about it.
    */
    void normalize();

    /*!
    \brief Reports the first structural rule this model breaks.

    \param context The funnel's context; its session supplies the tempo map the region starts
           address.
    \return The refusal to report, lighting the regions the broken rule names, or empty when the
            model satisfies every rule.
    */
    [[nodiscard]] std::optional<PlanRefused> validate(const MarkerJudgeContext& context) const;

    /*!
    \brief The region an undo or redo landing on this model brings into focus.

    A region the transition added, moved or retoned, else one it removed (shown by its former
    start); where the track is untouched and only the catalog changed — a rename — the first region
    sounding a changed tone, since that region's chip is where the change shows.

    \param replaced The model the transition replaces.
    \return The focus, or nothing when the models hold the same regions and tones.
    */
    [[nodiscard]] EditFocus focusReplacing(const ToneModelSnapshot& replaced) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal catalogs and tracks.
    */
    friend bool operator==(const ToneModelSnapshot& lhs, const ToneModelSnapshot& rhs) = default;
};

/*!
\brief The culprits a tone-track rule names: each region the error names, by its id.

The one reading of a tone-track error's records for every refusal that carries one — a tone verb's
own edit, the tone model's commit and the song timeline's — so no verb restates a rule to find what
stood in its way. A region of another arrangement than the one on screen lights nothing, as an
unresolved target does.

\param error The rule's error.
\return The light naming every region the error carries.
*/
[[nodiscard]] RefusalLight toneTrackCulprits(const common::core::ToneTrackError& error);

} // namespace rock_hero::editor::core
