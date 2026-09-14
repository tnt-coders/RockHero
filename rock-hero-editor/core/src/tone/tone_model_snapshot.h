/*!
\file tone_model_snapshot.h
\brief Whole tone-model snapshot committed and undone by the shared marker commit funnel.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/common/core/tone/tone_track.h>
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

    \param session Session supplying the tempo map the region starts address.
    \return The violation to report, or empty when the model satisfies every rule.
    */
    [[nodiscard]] std::optional<std::string> validate(const common::core::Session& session) const;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal catalogs and tracks.
    */
    friend bool operator==(const ToneModelSnapshot& lhs, const ToneModelSnapshot& rhs) = default;
};

} // namespace rock_hero::editor::core
