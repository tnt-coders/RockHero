#include "song_information/song_file_name.h"

#include <catch2/catch_test_macros.hpp>
#include <rock_hero/common/core/song/song.h>

namespace rock_hero::editor::core
{

using common::core::SongMetadata;

// Artist first, then the title, each as stored.
TEST_CASE("Suggested project name joins artist and title", "[core][song-file-name]")
{
    CHECK(
        suggestedProjectFileName(
            SongMetadata{.title = "Hold On", .artist = "Band", .album = "Record", .year = 1999}) ==
        "Band - Hold On.rhp");
}

// A blank artist reads as unknown rather than leaving a bare separator.
TEST_CASE("Suggested project name names a blank artist", "[core][song-file-name]")
{
    CHECK(
        suggestedProjectFileName(
            SongMetadata{.title = "Hold On", .artist = "", .album = "", .year = 0}) ==
        "Unknown Artist - Hold On.rhp");
}

// A blank title reads as unknown the same way.
TEST_CASE("Suggested project name names a blank title", "[core][song-file-name]")
{
    CHECK(
        suggestedProjectFileName(
            SongMetadata{.title = "", .artist = "Band", .album = "", .year = 0}) ==
        "Band - Unknown Song.rhp");
}

// Whitespace alone is blank: each half is measured trimmed.
TEST_CASE("Suggested project name treats whitespace as blank", "[core][song-file-name]")
{
    CHECK(suggestedProjectFileName(SongMetadata{}) == "Unknown Artist - Unknown Song.rhp");
    CHECK(
        suggestedProjectFileName(
            SongMetadata{.title = " \t ", .artist = "  ", .album = "", .year = 0}) ==
        "Unknown Artist - Unknown Song.rhp");
}

// The composed stem goes through the legal-name rule, so a slash in a field cannot make a folder.
TEST_CASE("Suggested project name is legal on every OS", "[core][song-file-name]")
{
    CHECK(
        suggestedProjectFileName(
            SongMetadata{
                .title = "Back in Black", .artist = " AC/DC ", .album = "", .year = 1980
            }) == "AC_DC - Back in Black.rhp");
}

} // namespace rock_hero::editor::core
