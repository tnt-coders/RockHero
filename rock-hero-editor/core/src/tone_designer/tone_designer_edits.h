/*!
\file tone_designer_edits.h
\brief Undo edit for whole-document replacement in the Tone Designer (Open and New).
*/

#pragma once

#include "controller/editor_undo_history.h"
#include "signal_chain/signal_chain_edits.h"

#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief One side of a Tone Designer document replacement: the full document at that moment.

Chain audio state is an engine memento (never a file path — redo must restore what the open
produced even if the file changed on disk afterwards); visual states carry the editor-owned
panel layout the engine deliberately does not retain across a restore.
*/
struct [[nodiscard]] ToneDesignerDocumentSnapshot
{
    /*! \brief Whole-chain engine memento with per-plugin instance ids preserved. */
    common::audio::AudibleToneState chain_state;

    /*! \brief Editor-owned visual state per plugin, in chain order. */
    std::vector<PluginVisualEditState> visual_states;

    /*! \brief File association at this moment; empty for an untitled document. */
    std::optional<std::filesystem::path> document_path;

    /*!
    \brief True when this state matched its file (clean) at capture time.

    Drives the clean-marker reconciliation after the transition commits, so undoing past an open
    back to a just-saved document reads clean again.
    */
    bool matches_file{false};
};

/*!
\brief Edit that swaps the entire Tone Designer document: chain, gain, layout, and association.

One edit models Open (after = the opened file's rig) and New (after = an empty untitled chain),
so a document replacement is always exactly one undo entry. Undo and redo restore the target
side's chain through the synchronous engine restore, reapply the editor-owned layout, repoint
the file association, and request clean-marker reconciliation through the designer state.
*/
struct [[nodiscard]] ToneDocumentReplaceEdit final : IEdit
{
    /*! \brief Document state before the replacement. */
    ToneDesignerDocumentSnapshot before;

    /*! \brief Document state after the replacement. */
    ToneDesignerDocumentSnapshot after;

    /*! \brief User-visible label, for example "Open Tone \"Lead\"" or "New Tone". */
    std::string operation_label;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] bool instantiatesPlugin(EditorUndoDirection direction) const override;
};

} // namespace rock_hero::editor::core
