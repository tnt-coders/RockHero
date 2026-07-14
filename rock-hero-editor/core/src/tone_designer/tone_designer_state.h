/*!
\file tone_designer_state.h
\brief Headless Tone Designer mode and document state owned by the editor controller.
*/

#pragma once

#include <filesystem>
#include <optional>

namespace rock_hero::editor::core
{

/*!
\brief Tone Designer document state: the editor's resting mode when no project is open.

The designer chain is a file-backed document: `document_path` is the file Save overwrites (empty
for an untitled document), and dirtiness is the undo history's clean marker versus its cursor.
Prompts follow the document; undo follows the session — the designer session reuses the
controller's history, and tone-file opens are entries within it, never history resets.
*/
struct ToneDesignerState
{
    /*! \brief True while the Tone Designer owns the live rig (no project open). */
    bool active{false};

    /*! \brief Associated tone file that Save overwrites; empty for an untitled document. */
    std::optional<std::filesystem::path> document_path;

    /*!
    \brief Clean-marker reconciliation requested by a committed document-replace transition.

    Set by ToneDocumentReplaceEdit::undo/redo to whether the landed-on document state matches its
    file. The controller consumes it after committing the transition: true re-marks the history
    clean at the landed position ("save A, open B, undo" must read clean on A), false leaves the
    marker where it is (the landed position was dirty relative to its file). Empty when the
    committed transition was not a document replace.
    */
    std::optional<bool> pending_clean_reconcile;
};

} // namespace rock_hero::editor::core
