#include "song_information/song_metadata_snapshot.h"

#include <rock_hero/common/core/shared/whitespace_trim.h>
#include <string>
#include <utility>

namespace rock_hero::editor::core
{

// The information is song-level, so there is no arrangement to resolve and no absent case to
// report: an unopened session simply carries empty information.
SongMetadataSnapshot SongMetadataSnapshot::capture(const common::core::Session& session)
{
    return SongMetadataSnapshot{.metadata = session.song().metadata};
}

// Always succeeds, which is why the marker edit's preflight can never reject an information round
// trip.
bool SongMetadataSnapshot::applyTo(common::core::Session& session) const
{
    session.setSongMetadata(metadata);
    return true;
}

// The one trim every stored name goes through, so a title of nothing but spaces is stored empty
// and resubmitting a value with stray spaces around it changes nothing.
void SongMetadataSnapshot::normalize()
{
    metadata.title = std::string{common::core::trimmedWhitespace(metadata.title)};
    metadata.artist = std::string{common::core::trimmedWhitespace(metadata.artist)};
    metadata.album = std::string{common::core::trimmedWhitespace(metadata.album)};
}

// The year rule is the only one: every text field may be empty, so nothing is required to save. A
// negative year cannot be repaired without inventing a value, so it is refused, lighting nothing.
std::optional<PlanRefused> SongMetadataSnapshot::validate(
    const MarkerJudgeContext& /*context*/) const
{
    if (metadata.year < 0)
    {
        return PlanRefused{.reason = "a year is a positive number, or 0 for unknown", .lit = {}};
    }
    return std::nullopt;
}

// The information stands at no place on the timeline, so no landing has anything to bring into
// view.
EditFocus SongMetadataSnapshot::focusReplacing(const SongMetadataSnapshot& /*replaced*/) const
{
    return {};
}

} // namespace rock_hero::editor::core
