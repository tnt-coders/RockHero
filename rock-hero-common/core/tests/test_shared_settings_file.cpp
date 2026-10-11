#include <catch2/catch_test_macros.hpp>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <rock_hero/common/core/testing/scoped_settings_file.h>

namespace rock_hero::common::core
{

using testing::ScopedSettingsFile;

// A file that does not exist yet opens as an empty set rather than failing.
TEST_CASE("SharedSettingsFile opens a missing file as empty", "[core][shared-settings]")
{
    const ScopedSettingsFile settings_file{"shared_settings_missing.settings"};
    const SharedSettingsFile shared{settings_file.path()};

    // Non-const: JUCE offers the property list through a non-const accessor only.
    juce::PropertiesFile properties = shared.open();

    CHECK(properties.getAllProperties().size() == 0);
}

// A locked write hands back what the callback returned, and what it saved is on disk.
TEST_CASE(
    "SharedSettingsFile runs a locked write and returns its result", "[core][shared-settings]")
{
    const ScopedSettingsFile settings_file{"shared_settings_locked_write.settings"};
    SharedSettingsFile shared{settings_file.path()};

    const std::optional<bool> saved = shared.whileLocked([](juce::PropertiesFile& properties) {
        properties.setValue("area.key", "value");
        return properties.save();
    });

    CHECK(saved == std::optional{true});
    CHECK(shared.open().getValue("area.key") == "value");
}

// Each product holds its own object over the one file, and neither caches it: a write through one
// is visible to the other's next open, and the other's next write keeps it.
TEST_CASE("SharedSettingsFile never serves a stale copy", "[core][shared-settings]")
{
    const ScopedSettingsFile settings_file{"shared_settings_fresh.settings"};
    SharedSettingsFile editor{settings_file.path()};
    SharedSettingsFile game{settings_file.path()};
    const juce::PropertiesFile opened_before = game.open();

    const std::optional<bool> editor_saved =
        editor.whileLocked([](juce::PropertiesFile& properties) {
            properties.setValue("editor.key", "1");
            return properties.save();
        });
    REQUIRE(editor_saved == std::optional{true});
    const std::optional<bool> game_saved = game.whileLocked([](juce::PropertiesFile& properties) {
        properties.setValue("game.key", "2");
        return properties.save();
    });
    REQUIRE(game_saved == std::optional{true});

    CHECK_FALSE(opened_before.containsKey("editor.key"));
    const juce::PropertiesFile reopened = editor.open();
    CHECK(reopened.getValue("editor.key") == "1");
    CHECK(reopened.getValue("game.key") == "2");
}

} // namespace rock_hero::common::core
