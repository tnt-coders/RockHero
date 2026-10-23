#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "song_information/song_metadata_snapshot.h"

#include <utility>

namespace rock_hero::editor::core
{

// Replaces the song's information with the dialog's submission as one undoable edit. The funnel
// trims the text fields, refuses a negative year, and records nothing for a submission equal to
// what is stored. The information names no chip, so a refusal lights nothing; and it stands at no
// place on the timeline, so the landing selects nothing.
void EditorController::Impl::performActionImpl(const EditorAction::SetSongInformation& action)
{
    SongMetadataSnapshot before = SongMetadataSnapshot::capture(session());
    SongMetadataSnapshot after = before;
    after.metadata = action.metadata;
    commitMarkerModel(std::move(before), std::move(after), "Edit Song Information", RefusalLight{});
}

} // namespace rock_hero::editor::core
