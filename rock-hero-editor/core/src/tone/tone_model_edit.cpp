#include "tone/tone_model_edit.h"

#include <rock_hero/common/core/session/session.h>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Assigns one side of the memento onto the current arrangement's tone model. Assignment is the
// whole apply: region ids are carried in the snapshot, so a selection naming one still resolves.
std::expected<void, EditorUndoFailureCode> applyToneModel(
    EditorEditContext& context, const ToneModelSnapshot& snapshot)
{
    common::core::ToneTrack* const tone_track = context.session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = context.session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    *catalog = snapshot.tones;
    *tone_track = snapshot.tone_track;
    return {};
}

} // namespace

std::expected<void, EditorUndoFailureCode> ToneModelEdit::undo(EditorEditContext& context) const
{
    return applyToneModel(context, before);
}

std::expected<void, EditorUndoFailureCode> ToneModelEdit::redo(EditorEditContext& context) const
{
    return applyToneModel(context, after);
}

// Returns the label the verb supplied ("Insert Dirty", "Delete Clean", and so on).
std::string ToneModelEdit::label() const
{
    return edit_label;
}

} // namespace rock_hero::editor::core
