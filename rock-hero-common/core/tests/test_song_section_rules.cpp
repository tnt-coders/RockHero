#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <optional>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

using testing::anchor;
using testing::downbeat;

// Eight bars of 4/4 at 120 BPM, the terminal on 9:1.
[[nodiscard]] TempoMap makeSectionMap()
{
    return testing::commonTimeMap({anchor("1:1", 0), anchor("9:1", 16000)});
}

// The code a list of sections is refused with, or empty where the rules accept it.
[[nodiscard]] std::optional<SongSectionErrorCode> refusalOf(
    const std::vector<SongSection>& sections, const TempoMap& tempo_map)
{
    const std::expected<void, SongSectionError> valid =
        validateSongSectionRules(sections, tempo_map);
    return valid.has_value() ? std::nullopt : std::optional{valid.error().code};
}

} // namespace

// A section is a named instant anywhere on the grid, a measure downbeat or not: a section half a
// beat past beat 2 of bar 6 is as legal as one on the barline.
TEST_CASE("Section rules accept a section off the downbeat", "[core][sections]")
{
    const TempoMap tempo_map = makeSectionMap();
    const std::vector<SongSection> sections{
        SongSection{.position = downbeat(1), .name = "Intro"},
        SongSection{.position = anchor("6:2+1/2", 0).position, .name = "Pickup"},
    };
    CHECK_FALSE(refusalOf(sections, tempo_map).has_value());
}

// The place rule still holds: a section on the terminal anchor would name a passage of no length,
// and one off the grid names no moment.
TEST_CASE("Section rules refuse the terminal and an off-grid position", "[core][sections]")
{
    const TempoMap tempo_map = makeSectionMap();
    CHECK(
        refusalOf({SongSection{.position = downbeat(9), .name = "Outro"}}, tempo_map) ==
        SongSectionErrorCode::OffGridOrPastTerminal);
    CHECK(
        refusalOf(
            {SongSection{.position = anchor("6:5", 0).position, .name = "Late"}}, tempo_map) ==
        SongSectionErrorCode::OffGridOrPastTerminal);
}

// Two sections at one instant, or one before the section listed ahead of it, would leave "which
// section governs this moment" answering arbitrarily.
TEST_CASE("Section rules refuse a repeated or reversed position", "[core][sections]")
{
    const TempoMap tempo_map = makeSectionMap();
    const GridPosition pickup = anchor("6:2+1/2", 0).position;
    CHECK(
        refusalOf(
            {SongSection{.position = pickup, .name = "Verse"},
             SongSection{.position = pickup, .name = "Chorus"}},
            tempo_map) == SongSectionErrorCode::UnsortedOrRepeatedPositions);
    CHECK(
        refusalOf(
            {SongSection{.position = pickup, .name = "Verse"},
             SongSection{.position = downbeat(6), .name = "Chorus"}},
            tempo_map) == SongSectionErrorCode::UnsortedOrRepeatedPositions);
}

// A name of nothing but whitespace is as blank as an empty one.
TEST_CASE("Section rules refuse a blank name", "[core][sections]")
{
    CHECK(
        refusalOf({SongSection{.position = downbeat(2), .name = "  \t"}}, makeSectionMap()) ==
        SongSectionErrorCode::EmptyName);
}

} // namespace rock_hero::common::core
