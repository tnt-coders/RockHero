#include <catch2/catch_test_macros.hpp>
#include <rock_hero/core/song.h>

namespace rock_hero::core
{

// Verifies that default song state is empty so loaders can detect unset metadata and assets.
TEST_CASE("Song default construction is empty", "[core][song]")
{
    const Song song;
    CHECK(song.metadata.title.empty());
    CHECK(song.metadata.artist.empty());
    CHECK(song.audio_asset_ref.empty());
    CHECK(song.tone_timeline_ref.empty());
    CHECK(song.chart.arrangements.empty());
}

// Verifies metadata fields remain plain value storage until validation rules are introduced.
TEST_CASE("Song metadata round-trip", "[core][song]")
{
    Song song;
    song.metadata.title = "Test Song";
    song.metadata.artist = "Test Artist";
    song.metadata.year = 2026;
    CHECK(song.metadata.title == "Test Song");
    CHECK(song.metadata.year == 2026);
}

} // namespace rock_hero::core
