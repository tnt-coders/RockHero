#include "timeline/song_section_edits.h"

#include <rock_hero/common/core/session/session.h>

namespace rock_hero::editor::core
{

// Restores the pre-verb section list. Assignment is the whole inverse: sections are song-level, so
// no arrangement has to be resolved first and nothing outside the list can be left inconsistent.
std::expected<void, EditorUndoFailureCode> SongSectionsEdit::undo(EditorEditContext& context) const
{
    context.session.songSections() = before;
    return {};
}

// Re-applies the post-verb section list, the mirror of undo().
std::expected<void, EditorUndoFailureCode> SongSectionsEdit::redo(EditorEditContext& context) const
{
    context.session.songSections() = after;
    return {};
}

// Returns the label the verb supplied ("Add Chorus", "Move Section", and so on).
std::string SongSectionsEdit::label() const
{
    return edit_label;
}

} // namespace rock_hero::editor::core
