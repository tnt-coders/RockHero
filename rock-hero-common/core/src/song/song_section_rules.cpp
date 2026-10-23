#include "song/song_section_rules.h"

#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/shared/whitespace_trim.h>
#include <string>

namespace rock_hero::common::core
{

// Walks the list once in stored order, because one of the three rules is about a section's relation
// to the one before it. The first violation is the whole answer: a caller that has one rule to fix
// has no use for the rest, and every caller here refuses the whole list anyway.
std::expected<void, SongSectionError> validateSongSectionRules(
    const std::vector<SongSection>& sections, const TempoMap& tempo_map)
{
    const SongSection* previous = nullptr;

    for (const SongSection& section : sections)
    {
        // A name of nothing but spaces is as blank as an empty one, and would draw as a chip with
        // no label to click accurately.
        if (trimmedWhitespace(section.name).empty())
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::EmptyName,
                .message =
                    "section names must not be blank: " + formatGridPositionToken(section.position),
            }};
        }

        // The shared place rule, asked the same way the section chord's projection asks it.
        if (!objectCanStartAt(section.position, tempo_map))
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::OffGridOrPastTerminal,
                .message = "section stands off the grid or at or past the tempo-map terminal "
                           "anchor: " +
                           formatGridPositionToken(section.position),
            }};
        }

        if (previous != nullptr && section.position <= previous->position)
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::UnsortedOrRepeatedPositions,
                .message = "section positions must be strictly ascending: " +
                           formatGridPositionToken(section.position),
            }};
        }

        previous = &section;
    }

    return std::expected<void, SongSectionError>{};
}

} // namespace rock_hero::common::core
