#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <optional>
#include <rock_hero/common/core/highway/highway_player_settings.h>
#include <rock_hero/common/core/highway/highway_settings_store.h>
#include <rock_hero/common/core/highway/highway_view_preferences.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <rock_hero/common/core/testing/scoped_settings_file.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <string>

namespace rock_hero::editor::core
{

namespace
{

using common::core::HighwayPlayerSettings;
using common::core::HighwaySettingsStore;
using common::core::HighwayViewPreferences;
using common::core::SharedSettingsFile;
using common::core::testing::ScopedSettingsFile;

// The controller's services over the given highway settings store, the rest the harness defaults.
[[nodiscard]] EditorController::Services servicesOver(HighwaySettingsStore& store)
{
    return controllerServices(
        nullEditorSettings(),
        immediateTaskRunner(),
        immediateMessageThreadScheduler(),
        defaultAudioConfigStore(),
        defaultLiveInputMonitor(),
        store);
}

// The highway settings the view was last shown.
[[nodiscard]] HighwayPlayerSettings shownSettings(const FakeEditorView& view)
{
    REQUIRE(view.last_state.has_value());
    return view.last_state.has_value() ? view.last_state->highway_settings
                                       : HighwayPlayerSettings{};
}

} // namespace

// The player's stored theme, pin and reading choices reach the view from the first state on.
TEST_CASE("EditorController publishes the stored highway settings", "[core][editor-controller]")
{
    const ScopedSettingsFile settings_file{"controller_highway_restore.settings"};
    SharedSettingsFile shared_file{settings_file.path()};
    HighwaySettingsStore store{shared_file};
    const HighwayPlayerSettings stored{
        .theme_id = std::string{"night"},
        .pinned_palette_id = std::string{"ice"},
        .view = HighwayViewPreferences{.mirrored = true, .invert_string_order = false},
    };
    REQUIRE(store.update([&stored](HighwayPlayerSettings& settings) { settings = stored; })
                .has_value());
    FakeTransport transport;
    ConfigurableSongAudio audio;

    EditorController controller{
        audioPorts(transport, audio), servicesOver(store), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    CHECK(shownSettings(view) == stored);
}

// A theme pick is stored and shown, and leaves the palette pin as it was: a pinned palette stays
// pinned, and an unpinned one stays the theme's choice, so it follows the new theme.
TEST_CASE("A highway theme pick leaves the palette pin alone", "[core][editor-controller]")
{
    const ScopedSettingsFile settings_file{"controller_highway_theme.settings"};
    SharedSettingsFile shared_file{settings_file.path()};
    HighwaySettingsStore store{shared_file};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), servicesOver(store), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onHighwayThemeChangeRequested("night");
    CHECK(shownSettings(view).theme_id == std::optional<std::string>{"night"});
    CHECK_FALSE(shownSettings(view).pinned_palette_id.has_value());
    CHECK(store.read() == shownSettings(view));

    controller.onStringPaletteChangeRequested("ice");
    controller.onHighwayThemeChangeRequested("dusk");
    CHECK(shownSettings(view).theme_id == std::optional<std::string>{"dusk"});
    CHECK(shownSettings(view).pinned_palette_id == std::optional<std::string>{"ice"});
    CHECK(store.read() == shownSettings(view));
}

// A concrete palette pins, "Theme's choice" unpins, and the theme stays as picked throughout.
TEST_CASE("A palette pick pins and Theme's choice unpins", "[core][editor-controller]")
{
    const ScopedSettingsFile settings_file{"controller_highway_palette.settings"};
    SharedSettingsFile shared_file{settings_file.path()};
    HighwaySettingsStore store{shared_file};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), servicesOver(store), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);
    controller.onHighwayThemeChangeRequested("night");

    controller.onStringPaletteChangeRequested("ice");
    CHECK(shownSettings(view).pinned_palette_id == std::optional<std::string>{"ice"});
    CHECK(store.read().pinned_palette_id == std::optional<std::string>{"ice"});

    controller.onStringPaletteChangeRequested(std::nullopt);
    CHECK_FALSE(shownSettings(view).pinned_palette_id.has_value());
    CHECK_FALSE(store.read().pinned_palette_id.has_value());
    CHECK(shownSettings(view).theme_id == std::optional<std::string>{"night"});
}

// A pick writes through the shared file's lock, so it keeps what the game changed since the editor
// last read, and the view shows the record as stored.
TEST_CASE("A highway pick keeps the game's change", "[core][editor-controller]")
{
    const ScopedSettingsFile settings_file{"controller_highway_shared.settings"};
    SharedSettingsFile editor_file{settings_file.path()};
    SharedSettingsFile game_file{settings_file.path()};
    HighwaySettingsStore editor_store{editor_file};
    HighwaySettingsStore game_store{game_file};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), servicesOver(editor_store), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    REQUIRE(
        game_store.update([](HighwayPlayerSettings& settings) { settings.view.mirrored = true; })
            .has_value());
    controller.onHighwayThemeChangeRequested("night");

    CHECK(shownSettings(view).view.mirrored);
    CHECK(shownSettings(view).theme_id == std::optional<std::string>{"night"});
    CHECK(game_store.read() == shownSettings(view));
}

// A pick the shared file cannot store still applies for the session, as the editor's other display
// preferences do; the failure is logged, and nothing reaches the file.
TEST_CASE("A highway pick that cannot be saved still applies", "[core][editor-controller]")
{
    // A file where the settings file's folder should be, so no save can create the settings file.
    const ScopedSettingsFile blocker{"controller_highway_blocker"};
    {
        const std::ofstream blocking_file{blocker.path()};
        REQUIRE(blocking_file.good());
    }
    SharedSettingsFile shared_file{blocker.path() / "shared.settings"};
    HighwaySettingsStore store{shared_file};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), servicesOver(store), noopExitFunction()
    };
    FakeEditorView view;
    controller.attachView(view);

    controller.onHighwayThemeChangeRequested("night");
    controller.onStringPaletteChangeRequested("ice");

    CHECK(shownSettings(view).theme_id == std::optional<std::string>{"night"});
    CHECK(shownSettings(view).pinned_palette_id == std::optional<std::string>{"ice"});
    CHECK(store.read() == HighwayPlayerSettings{});
}

} // namespace rock_hero::editor::core
