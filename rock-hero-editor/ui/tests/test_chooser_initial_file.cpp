#include "main_window/chooser_initial_file.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_core/juce_core.h>
#include <rock_hero/common/core/shared/juce_path.h>

namespace rock_hero::editor::ui
{

// No suggestion at all opens the dialog in the user's home.
TEST_CASE("Chooser opens an empty suggestion in the home", "[ui][chooser-initial-file]")
{
    CHECK(
        chooserInitialFile(std::filesystem::path{}) ==
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));
}

// A never-saved project's bare name opens in the home, never against the working directory.
TEST_CASE("Chooser opens a bare name in the home", "[ui][chooser-initial-file]")
{
    CHECK(
        chooserInitialFile(std::filesystem::path{"Band - Song.rhp"}) ==
        juce::File::getSpecialLocation(juce::File::userHomeDirectory)
            .getChildFile("Band - Song.rhp"));
}

// A suggestion with its own directory opens exactly there.
TEST_CASE("Chooser opens a full path as itself", "[ui][chooser-initial-file]")
{
    const juce::File home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    const juce::File saved = home.getChildFile("charts").getChildFile("Band - Song.rhp");

    CHECK(chooserInitialFile(common::core::pathFromJuceFile(saved)) == saved);
}

} // namespace rock_hero::editor::ui
