#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/core/highway/highway_player_settings.h>
#include <rock_hero/common/core/highway/highway_settings_error.h>
#include <rock_hero/common/core/highway/highway_settings_store.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <rock_hero/common/core/testing/scoped_settings_file.h>
#include <string>

namespace rock_hero::common::core
{

namespace
{

using testing::ScopedSettingsFile;

// Writes one raw property through the shared file, as another product or a damaged file would.
void writeRawSetting(const SharedSettingsFile& settings_file, const char* key, const char* value)
{
    juce::PropertiesFile properties = settings_file.open();
    properties.setValue(key, value);
    REQUIRE(properties.save());
}

} // namespace

// With nothing stored, every setting reads as the player's default.
TEST_CASE("HighwaySettingsStore reads the defaults from an empty file", "[core][highway-settings]")
{
    const ScopedSettingsFile settings_file{"highway_settings_defaults.settings"};
    SharedSettingsFile shared{settings_file.path()};
    const HighwaySettingsStore store{shared};

    CHECK(store.read() == HighwayPlayerSettings{});
}

// A change is stored and returned, and reads back the same through another product's file. Every
// setting differs from its default, so one the store forgot to write would read back as default.
TEST_CASE("HighwaySettingsStore round-trips an update", "[core][highway-settings]")
{
    const ScopedSettingsFile settings_file{"highway_settings_round_trip.settings"};
    SharedSettingsFile shared{settings_file.path()};
    HighwaySettingsStore store{shared};
    const HighwayPlayerSettings picked{
        .theme_id = std::string{"ice"},
        .pinned_palette_id = std::string{"mine"},
        .view = HighwayViewPreferences{
            .mirrored = true,
            .invert_strings = true,
            .show_note_targets = true,
            .show_note_rotation = false,
            .show_arpeggio_brackets = false,
        },
    };

    const std::expected<HighwayPlayerSettings, HighwaySettingsError> updated =
        store.update([&picked](HighwayPlayerSettings& settings) { settings = picked; });

    REQUIRE(updated.has_value());
    CHECK(*updated == picked);
    SharedSettingsFile reopened{settings_file.path()};
    CHECK(HighwaySettingsStore{reopened}.read() == picked);
}

// Unpicking the theme or unpinning the palette removes its key, so it reads as never picked.
TEST_CASE("HighwaySettingsStore clears the theme and the palette pin", "[core][highway-settings]")
{
    const ScopedSettingsFile settings_file{"highway_settings_clear_theme.settings"};
    SharedSettingsFile shared{settings_file.path()};
    HighwaySettingsStore store{shared};
    REQUIRE(store
                .update([](HighwayPlayerSettings& settings) {
                    settings.theme_id = "ice";
                    settings.pinned_palette_id = "ice";
                })
                .has_value());

    REQUIRE(store
                .update([](HighwayPlayerSettings& settings) {
                    settings.theme_id.reset();
                    settings.pinned_palette_id.reset();
                })
                .has_value());

    CHECK(store.read() == HighwayPlayerSettings{});
    CHECK_FALSE(shared.open().containsKey("highway.theme"));
    CHECK_FALSE(shared.open().containsKey("highway.palette"));
}

// Each setting is its own key: an unreadable one reads as its default and the rest keep theirs.
TEST_CASE("HighwaySettingsStore resets only an unreadable setting", "[core][highway-settings]")
{
    const ScopedSettingsFile settings_file{"highway_settings_corrupt.settings"};
    SharedSettingsFile shared{settings_file.path()};
    writeRawSetting(shared, "highway.theme", "dusk");
    writeRawSetting(shared, "highway.mirrored", "sideways");
    writeRawSetting(shared, "highway.invertStrings", "1");
    writeRawSetting(shared, "highway.showNoteTargets", "1");
    writeRawSetting(shared, "highway.showNoteRotation", "yes");
    writeRawSetting(shared, "highway.showArpeggioBrackets", "0");
    const HighwaySettingsStore store{shared};

    const HighwayPlayerSettings settings = store.read();

    const HighwayViewPreferences defaults{};
    CHECK(settings.theme_id == std::optional<std::string>{"dusk"});
    CHECK(settings.view.mirrored == defaults.mirrored);
    CHECK(settings.view.invert_strings);
    CHECK(settings.view.show_note_targets);
    CHECK(settings.view.show_note_rotation == defaults.show_note_rotation);
    CHECK_FALSE(settings.view.show_arpeggio_brackets);
}

// An update rereads the file under the lock, so it keeps what the other product changed since this
// product last read, and it leaves another area's keys alone.
TEST_CASE(
    "HighwaySettingsStore update keeps the other product's change", "[core][highway-settings]")
{
    const ScopedSettingsFile settings_file{"highway_settings_no_clobber.settings"};
    SharedSettingsFile editor_file{settings_file.path()};
    SharedSettingsFile game_file{settings_file.path()};
    HighwaySettingsStore editor{editor_file};
    HighwaySettingsStore game{game_file};
    writeRawSetting(game_file, "otherArea.key", "kept");
    const HighwayPlayerSettings editor_before = editor.read();

    REQUIRE(game.update([](HighwayPlayerSettings& settings) { settings.theme_id = "ice"; })
                .has_value());
    const std::expected<HighwayPlayerSettings, HighwaySettingsError> updated =
        editor.update([](HighwayPlayerSettings& settings) { settings.view.mirrored = true; });

    CHECK_FALSE(editor_before.theme_id.has_value());
    REQUIRE(updated.has_value());
    const HighwayPlayerSettings& stored = *updated;
    CHECK(stored.theme_id == std::optional<std::string>{"ice"});
    CHECK(stored.view.mirrored);
    CHECK(game.read() == stored);
    CHECK(game_file.open().getValue("otherArea.key") == "kept");
}

} // namespace rock_hero::common::core
