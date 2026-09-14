/*!
\file marker_model_edit.h
\brief The one whole-snapshot undo edit behind every timeline-marker verb.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/core/session/session.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

/*!
\brief Whole-snapshot memento edit behind every marker verb of one marker kind.

One edit object rather than one inverse command per verb. A marker model is a handful of small
records, so carrying both sides whole costs nothing, and the round trip is exact by assignment —
which matters because a tone delete or retone can MERGE regions and a section move re-sorts the
list, so an inverse command would have to know every record the edit took with it. The label is
supplied by the verb, the only place that knows which one ran.

\tparam Snapshot Marker-model snapshot type; marker_model_commit.h states what it must supply.
*/
template <typename Snapshot> struct [[nodiscard]] MarkerModelEdit final : IEdit
{
    /*!
    \brief Captures a marker-model change as the whole model before and after it.
    \param before_value Marker model as it stood before the verb ran.
    \param after_value Marker model the verb produced.
    \param label_value User-visible label naming the verb and its marker.
    */
    MarkerModelEdit(Snapshot before_value, Snapshot after_value, std::string label_value)
        : before(std::move(before_value))
        , after(std::move(after_value))
        , edit_label(std::move(label_value))
    {}

    /*!
    \brief Restores the marker model as it stood before the verb.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the preflight failure when the session cannot carry the snapshot.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override
    {
        return apply(context, before);
    }

    /*!
    \brief Re-applies the marker model the verb produced.
    \param context Apply-time editor/audio dependencies.
    \return Empty success, or the preflight failure when the session cannot carry the snapshot.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override
    {
        return apply(context, after);
    }

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the verb and its marker. */
    [[nodiscard]] std::string label() const override
    {
        return edit_label;
    }

    /*! \brief Marker model as it stood before the verb ran. */
    Snapshot before;

    /*! \brief Marker model the verb produced. */
    Snapshot after;

    /*! \brief User-visible label naming the verb and its marker. */
    std::string edit_label;

private:
    // Assignment is the whole apply, in either direction; a snapshot that has nowhere to land (no
    // arrangement owns the model any more) reports the preflight failure rather than doing nothing.
    [[nodiscard]] static std::expected<void, EditorUndoFailureCode> apply(
        EditorEditContext& context, const Snapshot& snapshot)
    {
        if (!snapshot.applyTo(context.session))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        return {};
    }
};

} // namespace rock_hero::editor::core
