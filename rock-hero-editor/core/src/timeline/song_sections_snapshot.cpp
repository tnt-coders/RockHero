#include "timeline/song_sections_snapshot.h"

#include <algorithm>
#include <rock_hero/common/core/song/song_section_rules.h>

namespace rock_hero::editor::core
{

// Sections are song-level, so there is no arrangement to resolve and no absent case to report: an
// unopened session simply carries an empty list.
SongSectionsSnapshot SongSectionsSnapshot::capture(const common::core::Session& session)
{
    return SongSectionsSnapshot{.sections = session.song().sections};
}

// Always succeeds, which is why the marker edit's preflight can never reject a section round trip.
bool SongSectionsSnapshot::applyTo(common::core::Session& session) const
{
    // Assignment is the whole apply: sections are song-level, so no arrangement has to be resolved
    // first and nothing outside the list can be left inconsistent.
    session.songSections() = sections;
    return true;
}

// A sort rather than a sorted insert, so an insert and a move can each just place their section and
// let the one normalization put the list back in the order every consumer reads it in.
void SongSectionsSnapshot::normalize()
{
    std::ranges::sort(sections, std::ranges::less{}, &common::core::SongSection::position);
}

// Every section rule lives in common core, where the package reader asks the same question, so this
// only carries the diagnostic across: the commit funnel logs a refusal rather than branching on the
// reason, which is what the stable code is there for elsewhere.
std::optional<std::string> SongSectionsSnapshot::validate(
    const common::core::Session& session) const
{
    if (const auto valid =
            common::core::validateSongSectionRules(sections, session.song().tempo_map);
        !valid.has_value())
    {
        return valid.error().message;
    }
    return std::nullopt;
}

} // namespace rock_hero::editor::core
