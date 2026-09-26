/*!
\file song_section_rules.h
\brief Structural validation rules for song sections, shared by editing and persistence.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <string_view>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Stable failure reasons reported by song-section structural validation. */
enum class SongSectionErrorCode : std::uint8_t
{
    /*! \brief A section name is empty, or holds nothing but surrounding whitespace. */
    EmptyName,

    /*! \brief A section position is not a measure downbeat on the tempo map's grid. */
    NotAMeasureDownbeat,

    /*! \brief A section starts at or past the tempo map's terminal anchor, naming no passage. */
    SectionPastTerminalAnchor,

    /*! \brief Section positions are not strictly ascending, so one repeats or reverses another. */
    UnsortedOrRepeatedPositions,
};

/*! \brief Recoverable failure produced by song-section structural validation. */
struct [[nodiscard]] SongSectionError
{
    /*! \brief Stable error code used by callers for branching. */
    SongSectionErrorCode code{};

    /*! \brief Human-readable diagnostic suitable for UI display or logs. */
    std::string message;
};

/*!
\brief Strips surrounding whitespace from a section name.

The one trim behind the name rule: a name of nothing but spaces is as blank as an empty one, and
would draw as a chip with no label to click accurately. Every producer — authoring, import, package
read — stores what this returns, and \ref validateSongSectionRules measures emptiness through it,
so the stored name and the accepted name can never disagree.

\param name Raw name as typed or as read from a package.
\return The name without surrounding spaces or tabs.
*/
[[nodiscard]] std::string trimmedSongSectionName(std::string_view name);

/*!
\brief The measure downbeat a section covering a position starts on.

The one snap behind the position rule, so the verbs that place a section and the validator that
accepts one cannot disagree about where a section may start: a section names a passage that begins
at a barline, so it starts on beat 1 of its measure with no sub-beat offset. The board promotes that
downbeat's own bar rather than drawing a mark of its own, which an off-downbeat start would have
nothing to promote.

\param position Any grid position inside the measure.
\return That measure's downbeat.
*/
[[nodiscard]] GridPosition songSectionDownbeat(const GridPosition& position) noexcept;

/*!
\brief Validates the structural song-section rules shared by editing and persistence.

Checks non-empty names (after \ref trimmedSongSectionName), positions that are real measure
downbeats (\ref songSectionDownbeat) on the tempo map's grid, every position lying strictly before
the terminal anchor (a section starting on the closing barline would name a passage of no length),
and strictly ascending positions — strictly, because two sections at one position would make "which
section governs this moment" answer arbitrarily.

\param sections Section markers to validate, in the order they are stored.
\param tempo_map Tempo map the section positions must address.
\return Empty success, or the rule violation to report.
*/
[[nodiscard]] std::expected<void, SongSectionError> validateSongSectionRules(
    const std::vector<SongSection>& sections, const TempoMap& tempo_map);

} // namespace rock_hero::common::core
