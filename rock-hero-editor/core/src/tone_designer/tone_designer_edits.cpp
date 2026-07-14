#include "tone_designer_edits.h"

#include "signal_chain/signal_chain_workflow.h"
#include "tone/tone_automation_edits.h"
#include "tone_designer/tone_designer_state.h"

#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Applies one side of a whole-chain replacement: engine chain restore plus the editor-owned
// layout the engine deliberately resets to defaults. The engine restore is transactional (a
// failure leaves the current chain untouched), so a port error is a repaired failure: the
// transition aborts and editor state stays coherent.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyToneChainSnapshot(
    const ToneChainSnapshot& target, EditorEditContext& context)
{
    auto restored = context.live_rig.restoreAudibleToneState(target.chain_state);
    if (!restored.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::RepairedFailure};
    }

    context.signal_chain.replaceSnapshot(
        common::audio::PluginChainSnapshot{.plugins = restored->plugins});
    std::vector<PluginBlockAssignment> placement;
    placement.reserve(target.visual_states.size());
    for (const PluginVisualEditState& visual_state : target.visual_states)
    {
        placement.push_back(
            PluginBlockAssignment{
                .instance_id = visual_state.instance_id,
                .block_index = visual_state.block_index,
            });
    }
    (void)context.signal_chain.setBlockPlacement(placement);
    for (const PluginVisualEditState& visual_state : target.visual_states)
    {
        (void)context.signal_chain.setPluginDisplayTypeOverride(
            visual_state.instance_id, visual_state.display_type_override);
    }

    // The engine applied the gain; refresh the controller-owned mirror the view reads.
    context.output_gain_db = target.chain_state.output_gain.db;
    return {};
}

// Applies one side of the designer document replacement: the chain plus association and the
// clean-marker reconciliation request the controller consumes post-commit.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyDocumentSnapshot(
    const ToneDesignerDocumentSnapshot& target, EditorEditContext& context)
{
    auto applied = applyToneChainSnapshot(target.chain, context);
    if (!applied.has_value())
    {
        return applied;
    }

    context.tone_designer.document_path = target.document_path;
    context.tone_designer.pending_clean_reconcile = target.matches_file;
    return {};
}

} // namespace

// Restores the pre-replacement document (chain, layout, association, cleanliness).
std::expected<void, EditorUndoFailureCode> ToneDocumentReplaceEdit::undo(
    EditorEditContext& context) const
{
    return applyDocumentSnapshot(before, context);
}

// Re-applies the replacement document.
std::expected<void, EditorUndoFailureCode> ToneDocumentReplaceEdit::redo(
    EditorEditContext& context) const
{
    return applyDocumentSnapshot(after, context);
}

// Names the document operation for the Edit menu and the history inspector.
std::string ToneDocumentReplaceEdit::label() const
{
    return operation_label;
}

// A direction instantiates plugins whenever the document it lands on has any.
bool ToneDocumentReplaceEdit::instantiatesPlugin(EditorUndoDirection direction) const
{
    const ToneDesignerDocumentSnapshot& target =
        direction == EditorUndoDirection::Undo ? before : after;
    return !target.chain.chain_state.plugin_states.empty();
}

// Restores the pre-import chain, then puts the dropped automation back: model truth first, then
// the derived playback curve on each just-restored instance (the PluginRemoveEdit recipe).
std::expected<void, EditorUndoFailureCode> ToneImportEdit::undo(EditorEditContext& context) const
{
    auto applied = applyToneChainSnapshot(before, context);
    if (!applied.has_value())
    {
        return applied;
    }

    for (const ToneImportRemovedAutomation& removed : removed_automation)
    {
        (void)applyToneAutomationModel(
            context.session, removed.entry.plugin_id, removed.entry.param_id, removed.entry.points);
        rewriteDerivedToneCurve(
            context,
            tone_document_ref,
            removed.instance_id,
            removed.entry.param_id,
            removed.entry.points);
    }
    return {};
}

// Re-applies the imported chain and re-drops the automation the import removed. The derived
// curves need no clearing: they die with the replaced plugin instances.
std::expected<void, EditorUndoFailureCode> ToneImportEdit::redo(EditorEditContext& context) const
{
    auto applied = applyToneChainSnapshot(after, context);
    if (!applied.has_value())
    {
        return applied;
    }

    for (const ToneImportRemovedAutomation& removed : removed_automation)
    {
        (void)applyToneAutomationModel(
            context.session, removed.entry.plugin_id, removed.entry.param_id, {});
    }
    return {};
}

// Names the import for the Edit menu and the history inspector.
std::string ToneImportEdit::label() const
{
    std::string text = "Import Tone \"" + file_name + "\"";
    if (!tone_name.empty())
    {
        text += " into " + tone_name;
    }
    return text;
}

// A direction instantiates plugins whenever the chain it lands on has any.
bool ToneImportEdit::instantiatesPlugin(EditorUndoDirection direction) const
{
    const ToneChainSnapshot& target = direction == EditorUndoDirection::Undo ? before : after;
    return !target.chain_state.plugin_states.empty();
}

} // namespace rock_hero::editor::core
