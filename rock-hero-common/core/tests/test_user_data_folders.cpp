#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/user_data_folders.h>

namespace rock_hero::common::core
{

// Every per-user location agrees on one folder named for the family, on every platform.
TEST_CASE("User data locations all sit in the one per-user folder", "[core][user-data]")
{
    const std::filesystem::path folder = settingsFile(Product::Game).parent_path().parent_path();

    CHECK(folder.filename() == pathFromUtf8(applicationDataFolderName()));
    CHECK(settingsFile(Product::Editor).parent_path().parent_path() == folder);
    CHECK(sharedSettingsFile().parent_path().parent_path() == folder);
    CHECK(logFile(Product::Game).parent_path().parent_path() == folder);
    CHECK(logFile(Product::Editor).parent_path().parent_path() == folder);
    CHECK(themesFolder().parent_path() == folder);
    CHECK(songsFolder().parent_path() == folder);
    CHECK(tracktionFolder().parent_path() == folder);
}

// Pins the on-disk names: each is one lowercase word, and each product token names both of that
// product's files.
TEST_CASE("User data locations have their one-word names", "[core][user-data]")
{
    CHECK(settingsFile(Product::Game).parent_path().filename() == "settings");
    CHECK(settingsFile(Product::Game).filename() == "game.settings");
    CHECK(settingsFile(Product::Editor).filename() == "editor.settings");
    CHECK(sharedSettingsFile().filename() == "shared.settings");
    CHECK(logFile(Product::Game).parent_path().filename() == "logs");
    CHECK(logFile(Product::Game).filename() == "game.log");
    CHECK(logFile(Product::Editor).filename() == "editor.log");
    CHECK(themesFolder().filename() == "themes");
    CHECK(songsFolder().filename() == "songs");
    CHECK(tracktionFolder().filename() == "tracktion");
}

// Scratch is not user data: it sits in the temporary directory, under the family's name.
TEST_CASE("The scratch folder sits in the temporary directory", "[core][user-data]")
{
    const std::filesystem::path temp =
        pathFromJuceFile(juce::File::getSpecialLocation(juce::File::tempDirectory));

    CHECK(scratchFolder() == temp / pathFromUtf8(applicationDataFolderName()));
}

} // namespace rock_hero::common::core
