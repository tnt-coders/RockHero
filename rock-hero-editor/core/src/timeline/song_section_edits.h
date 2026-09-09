/*!
\file song_section_edits.h
\brief The song-level section undo edit applied through the editor undo history.
*/

#pragma once

#include "controller/editor_undo_history.h"

#include <expected>
#include <rock_hero/common/core/song/song.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Whole-list memento edit behind every song-section verb: add, rename, move and delete.

One edit object rather than four inverse commands. A section is two fields and a song carries tens
of them, so the whole list is cheaper to carry than a diff would be to compute, and the four verbs
become one code path whose round trip is exact by assignment. The label is supplied by the verb,
which is the only place that knows which one ran.

\note A section edit never re-runs the fret-hand phrase-boundary generator. That generator reads
      section starts as phrase boundaries at IMPORT only, producing stored FretHandPosition
      records; re-running it on an authored edit would silently overwrite hand positions the
      charter placed. Sections are navigation structure — moving one moves no hand.
*/
struct [[nodiscard]] SongSectionsEdit final : IEdit
{
    /*!
    \brief Captures a song-section change as the whole list before and after it.
    \param before_value Section list as it stood before the verb ran.
    \param after_value Section list the verb produced.
    \param label_value User-visible label naming the verb and its section.
    */
    SongSectionsEdit(
        std::vector<common::core::SongSection> before_value,
        std::vector<common::core::SongSection> after_value, std::string label_value)
        : before(std::move(before_value))
        , after(std::move(after_value))
        , edit_label(std::move(label_value))
    {}

    /*!
    \brief Restores the section list as it stood before the verb.
    \param context Apply-time editor/audio dependencies.
    \return Empty success; a whole-list assignment cannot fail.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;

    /*!
    \brief Re-applies the section list the verb produced.
    \param context Apply-time editor/audio dependencies.
    \return Empty success; a whole-list assignment cannot fail.
    */
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;

    /*! \brief Returns the user-visible command label for menus and diagnostics.
    \return Human-readable label naming the verb and its section. */
    [[nodiscard]] std::string label() const override;

    /*! \brief Section list as it stood before the verb ran. */
    std::vector<common::core::SongSection> before;

    /*! \brief Section list the verb produced. */
    std::vector<common::core::SongSection> after;

    /*! \brief User-visible label naming the verb and its section. */
    std::string edit_label;
};

} // namespace rock_hero::editor::core
