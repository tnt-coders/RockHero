#include "tone_designer_edits.h"

#include "signal_chain/signal_chain_workflow.h"
#include "tone_designer/tone_designer_state.h"

#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// Applies one side of the document replacement: engine chain restore, editor layout, file
// association, and the clean-marker reconciliation request the controller consumes post-commit.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyDocumentSnapshot(
    const ToneDesignerDocumentSnapshot& target, EditorEditContext& context)
{
    // The engine restore is transactional (a failure leaves the current chain untouched), so a
    // port error is a repaired failure: the transition aborts and editor state stays coherent.
    auto restored = context.live_rig.restoreAudibleToneState(target.chain_state);
    if (!restored.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::RepairedFailure};
    }

    // Rebuild the editor-side chain model from the authoritative engine result, then reapply the
    // editor-owned layout the engine deliberately resets to defaults.
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

    // Repoint the document and ask the controller to reconcile the clean marker after commit.
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
    return !target.chain_state.plugin_states.empty();
}

} // namespace rock_hero::editor::core
