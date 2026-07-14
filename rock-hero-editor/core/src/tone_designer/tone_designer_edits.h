/*!
\file tone_designer_edits.h
\brief Undo edits for whole-chain tone replacements: designer Open/New and project Import.
*/

#pragma once

#include "controller/editor_undo_history.h"
#include "signal_chain/signal_chain_edits.h"

#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief One side of a whole-chain replacement: the audible chain at that moment.

Chain audio state is an engine memento (never a file path — redo must restore what the operation
produced even if the file changed on disk afterwards); visual states carry the editor-owned
panel layout the engine deliberately does not retain across a restore.
*/
struct [[nodiscard]] ToneChainSnapshot
{
    /*! \brief Whole-chain engine memento with per-plugin instance ids preserved. */
    common::audio::AudibleToneState chain_state;

    /*! \brief Editor-owned visual state per plugin, in chain order. */
    std::vector<PluginVisualEditState> visual_states;
};

/*! \brief One side of a Tone Designer document replacement: the full document at that moment. */
struct [[nodiscard]] ToneDesignerDocumentSnapshot
{
    /*! \brief Audible chain at this moment. */
    ToneChainSnapshot chain;

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

/*!
\brief One automation entry a project import extracted, with its pre-import live-instance key.

The entry itself is the persisted musical truth restored to the model on undo; the instance id
keys the derived playback curve rebuild on the just-restored plugin (same recipe as
PluginRemoveEdit's automation restore).
*/
struct [[nodiscard]] ToneImportRemovedAutomation
{
    /*! \brief Extracted automation entry, restored verbatim on undo. */
    common::core::ToneParameterAutomation entry;

    /*! \brief Pre-import live instance id the rebuilt curve targets after undo. */
    std::string instance_id;
};

/*!
\brief Edit that replaces the active project tone's whole chain from an imported tone file.

The tone's catalog identity and regions never change — the file supplies only the rig — so the
memento is the chain pair plus the automation entries the import dropped (their durable plugin
ids died with the replaced chain). Undo restores chain, layout, and automation verbatim; redo
re-drops them.
*/
struct [[nodiscard]] ToneImportEdit final : IEdit
{
    /*! \brief Audible chain before the import. */
    ToneChainSnapshot before;

    /*! \brief Audible chain after the import. */
    ToneChainSnapshot after;

    /*! \brief Automation entries the import removed, restored verbatim on undo. */
    std::vector<ToneImportRemovedAutomation> removed_automation;

    /*! \brief Tone whose chain was replaced, used to rebuild derived curves on undo. */
    std::string tone_document_ref;

    /*! \brief Name of the tone whose chain was replaced, for the undo label. */
    std::string tone_name;

    /*! \brief Imported tone file's display name (stem), for the undo label. */
    std::string file_name;

    [[nodiscard]] std::expected<void, EditorUndoFailureCode> undo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::expected<void, EditorUndoFailureCode> redo(
        EditorEditContext& context) const override;
    [[nodiscard]] std::string label() const override;
    [[nodiscard]] bool instantiatesPlugin(EditorUndoDirection direction) const override;
};

} // namespace rock_hero::editor::core
