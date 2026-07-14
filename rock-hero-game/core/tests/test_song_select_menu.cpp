#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/game/core/input/menu_action.h>
#include <rock_hero/game/core/library/library_index.h>
#include <rock_hero/game/core/menu/song_select_menu.h>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// One library entry with a title and the given arrangement ids.
[[nodiscard]] LibraryEntry song(
    const std::filesystem::path& path, const std::string& title,
    const std::vector<std::string>& arrangement_ids)
{
    LibraryEntry entry;
    entry.package_path = path;
    entry.metadata.title = title;
    for (const std::string& id : arrangement_ids)
    {
        LibraryArrangementSummary arrangement;
        arrangement.id = id;
        arrangement.part = common::core::Part::Lead;
        entry.arrangements.push_back(arrangement);
    }
    return entry;
}

// A two-song library: A (two arrangements) then B (one).
[[nodiscard]] LibraryIndex twoSongs()
{
    return LibraryIndex{
        .entries = {
            song("C:/Songs/a.rock", "Song A", {"a-lead", "a-bass"}),
            song("C:/Songs/b.rock", "Song B", {"b-rhythm"}),
        }
    };
}

} // namespace

// Verifies song navigation wraps and Accept advances to the arrangement list.
TEST_CASE("Song select navigates songs and enters arrangements", "[core][menu]")
{
    SongSelectMenu menu{twoSongs()};

    CHECK(menu.screen() == SongSelectScreen::SongList);
    CHECK(menu.selectedSongIndex() == 0);

    menu.handle(MenuAction::NavigateDown);
    CHECK(menu.selectedSongIndex() == 1);
    menu.handle(MenuAction::NavigateDown); // wraps back to the first
    CHECK(menu.selectedSongIndex() == 0);
    menu.handle(MenuAction::NavigateUp); // wraps to the last
    CHECK(menu.selectedSongIndex() == 1);

    menu.handle(MenuAction::Accept);
    CHECK(menu.screen() == SongSelectScreen::ArrangementList);
    REQUIRE(menu.currentSong() != nullptr);
    CHECK(menu.currentSong()->metadata.title == "Song B");
    REQUIRE(menu.currentArrangements().size() == 1);
}

// Verifies accepting an arrangement produces a one-shot launch with the right package and id.
TEST_CASE("Song select accepts an arrangement into a launch request", "[core][menu]")
{
    SongSelectMenu menu{twoSongs()};

    menu.handle(MenuAction::Accept); // Song A -> arrangement list
    REQUIRE(menu.screen() == SongSelectScreen::ArrangementList);
    menu.handle(MenuAction::NavigateDown); // second arrangement (a-bass)
    CHECK(menu.selectedArrangementIndex() == 1);

    menu.handle(MenuAction::Accept);
    const std::optional<SongSelectLaunch> launch = menu.takeLaunch();
    REQUIRE(launch.has_value());
    CHECK(launch->package_path == std::filesystem::path{"C:/Songs/a.rock"});
    CHECK(launch->arrangement_id == "a-bass");

    // The launch is one-shot.
    CHECK_FALSE(menu.takeLaunch().has_value());
}

// Verifies Back returns from the arrangement list to the song list.
TEST_CASE("Song select goes back from arrangements to songs", "[core][menu]")
{
    SongSelectMenu menu{twoSongs()};

    menu.handle(MenuAction::Accept);
    REQUIRE(menu.screen() == SongSelectScreen::ArrangementList);

    menu.handle(MenuAction::Back);
    CHECK(menu.screen() == SongSelectScreen::SongList);
    CHECK_FALSE(menu.takeLaunch().has_value());
}

// Verifies an empty library keeps every action a safe no-op.
TEST_CASE("Song select handles an empty library safely", "[core][menu]")
{
    SongSelectMenu menu{LibraryIndex{}};

    CHECK(menu.currentSong() == nullptr);
    CHECK(menu.currentArrangements().empty());

    menu.handle(MenuAction::NavigateDown);
    menu.handle(MenuAction::Accept); // cannot enter arrangements with no songs
    CHECK(menu.screen() == SongSelectScreen::SongList);
    CHECK(menu.selectedSongIndex() == 0);
    CHECK_FALSE(menu.takeLaunch().has_value());
}

} // namespace rock_hero::game::core
