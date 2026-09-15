#include "song/song_section_rules.h"

#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>

namespace rock_hero::common::core
{

// Spaces and tabs are the whole of "surrounding whitespace" here: a section name comes from a text
// prompt or an imported bar label, neither of which can carry a newline.
std::string trimmedSongSectionName(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return std::string{name.substr(first, name.find_last_not_of(" \t") - first + 1)};
}

// Keeps the measure and discards everything finer, which is what makes a mid-measure marker a legal
// place to aim a section verb at: the verb snaps, rather than refusing a position the charter could
// not have placed more precisely on the row.
GridPosition songSectionDownbeat(const GridPosition& position) noexcept
{
    return GridPosition{.measure = position.measure, .beat = 1, .offset = {}};
}

// Valid on the tempo map's grid and strictly before its terminal anchor. The downbeat snap is the
// caller's (songSectionDownbeat); this answers only whether the snapped position may hold a
// section.
bool songSectionCanStartAt(const GridPosition& downbeat, const TempoMap& tempo_map)
{
    return isValidGridPosition(downbeat, tempo_map) && downbeat < terminalGridPosition(tempo_map);
}

// Walks the list once in stored order, because two of the four rules are about a section's relation
// to the one before it. The first violation is the whole answer: a caller that has one rule to fix
// has no use for the rest, and every caller here refuses the whole list anyway.
std::expected<void, SongSectionError> validateSongSectionRules(
    const std::vector<SongSection>& sections, const TempoMap& tempo_map)
{
    const SongSection* previous = nullptr;

    for (const SongSection& section : sections)
    {
        if (trimmedSongSectionName(section.name).empty())
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::EmptyName,
                .message =
                    "section names must not be blank: " + formatGridPositionToken(section.position),
            }};
        }

        // Exactly its own measure's downbeat; asking songSectionDownbeat rather than restating
        // beat-1-no-offset keeps the snap the verbs apply and the position this accepts the same
        // rule. The grid and terminal checks are the shared place rule, asked the same way the
        // section chord's projection asks it.
        if (!isValidGridPosition(section.position, tempo_map) ||
            !(section.position == songSectionDownbeat(section.position)))
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::NotAMeasureDownbeat,
                .message = "section position is not a measure downbeat: " +
                           formatGridPositionToken(section.position),
            }};
        }

        // The grid half already held above, so only the terminal half of the shared place rule can
        // fire here.
        if (!songSectionCanStartAt(section.position, tempo_map))
        {
            return std::unexpected{SongSectionError{
                .code = SongSectionErrorCode::SectionPastTerminalAnchor,
                .message = "section starts at or past the tempo-map terminal anchor: " +
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
