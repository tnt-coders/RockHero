/*!
\file tone_model_edit.h
\brief The one tone-model undo edit applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/core/tone/tone_track.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief An arrangement's whole tone model: the catalog and the track that references it.

The two are captured together because no tone verb changes one without the other being able to
change with it — a retone can mint a catalog entry and prune the one it replaces, a delete prunes,
a rename touches only the catalog and a split only the track.
*/
struct ToneModelSnapshot
{
    /*! \brief The arrangement's named tone catalog. */
    std::vector<common::core::Tone> tones;

    /*! \brief The arrangement's tone track. */
    common::core::ToneTrack tone_track;

    /*!
    \brief Compares two snapshots by their stored values.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both hold equal catalogs and tracks.
    */
    friend bool operator==(const ToneModelSnapshot& lhs, const ToneModelSnapshot& rhs) = default;
};

/*!
\brief Whole-model memento edit behind every tone verb: split, delete, retone, rename, move.

One edit object rather than one inverse command per verb, the shape the song-section verbs
already share. A tone model is a handful of regions and catalog entries, so carrying both sides
whole costs nothing, and the round trip is exact by assignment — which matters because a delete or
a retone can merge regions, and an inverse command would have to know every region a merge took.
The label is supplied by the verb, the only place that knows which one ran.

\note Purely a model edit. A tone document minted by the verb, and the rig branch it loads into,
      stay in place across undo and redo; the model is the source of truth for the next rig
      reload, and an unreferenced branch is unselected and harmless.
*/
struct [[nodiscard]] ToneModelEdit final : IEdit
{
    /*!
    \brief Captures a tone-model change as the whole model before and after it.
    \param before_value Tone model as it stood before the verb ran.
    \param after_value Tone model the verb produced.
    \param label_value User-visible label naming the verb and its tone.
    */
    ToneModelEdit(
        ToneModelSnapshot before_value, ToneModelSnapshot after_value, std::string label_value)
        : before(std::move(before_value))
        , after(std::move(after_value))
        , edit_label(std::move(label_value))
    {}

    /*!
    \brief Restores the tone model as it stood before the verb.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the preflight failure when no arrangement owns a tone model.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the tone model the verb produced.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the preflight failure when no arrangement owns a tone model.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the verb and its tone. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Tone model as it stood before the verb ran. */
    ToneModelSnapshot before;

    /*! \brief Tone model the verb produced. */
    ToneModelSnapshot after;

    /*! \brief User-visible label naming the verb and its tone. */
    std::string edit_label;
};

} // namespace rock_hero::editor::core
