#include "timeline/song_sections_snapshot.h"

#include <algorithm>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <utility>

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
    session.setSongSections(sections);
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
// reason, which is what the stable code is there for elsewhere. A section rule names no record, so
// the refusal lights nothing of its own: the verb's subject is the section it acted on, and a
// section a move or an insert collides with is the place the verb addressed.
std::optional<PlanRefused> SongSectionsSnapshot::validate(const MarkerJudgeContext& context) const
{
    if (auto valid =
            common::core::validateSongSectionRules(sections, context.session.song().tempo_map);
        !valid.has_value())
    {
        return PlanRefused{.reason = std::move(valid.error().message), .lit = {}};
    }
    return std::nullopt;
}

EditFocus SongSectionsSnapshot::focusReplacing(const SongSectionsSnapshot& replaced) const
{
    const std::optional<ChangedRecord<common::core::SongSection>> changed =
        firstChangedRecord(sections, replaced.sections);
    if (!changed.has_value())
    {
        return {};
    }
    const common::core::GridPosition start = changed->record->position;
    MarkerEditFocus marker{.marker = {}, .start = start};
    if (changed->present)
    {
        marker.marker = SongSectionSelection{.position = start};
    }
    return marker;
}

} // namespace rock_hero::editor::core
