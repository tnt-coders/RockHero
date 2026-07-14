#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/editor/core/audio/editor_effective_audio_config_store.h>
#include <string_view>
#include <system_error>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

// Owns a build-local game audio-config file so each test starts and ends with no stale game state.
class ScopedGameFile final
{
public:
    explicit ScopedGameFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        remove();
    }

    ~ScopedGameFile()
    {
        remove();
    }

    ScopedGameFile(const ScopedGameFile&) = delete;
    ScopedGameFile& operator=(const ScopedGameFile&) = delete;
    ScopedGameFile(ScopedGameFile&&) = delete;
    ScopedGameFile& operator=(ScopedGameFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    void remove() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    std::filesystem::path m_path;
};

[[nodiscard]] common::audio::InputDeviceIdentity guitarIdentity()
{
    return common::audio::InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = "Focusrite",
        .input_channel_index = 0,
        .input_channel_name = "Input 1",
    };
}

[[nodiscard]] common::audio::ActiveDeviceRoute routeFor(
    std::string blob, common::audio::InputDeviceIdentity identity)
{
    return common::audio::ActiveDeviceRoute{
        .serialized_state = std::move(blob),
        .identity = std::move(identity),
    };
}

[[nodiscard]] common::audio::InputCalibrationState calibrationFor(
    common::audio::InputDeviceIdentity identity)
{
    return common::audio::InputCalibrationState{
        .calibration_gain = common::audio::Gain{6.0},
        .input_device_identity = std::move(identity),
    };
}

// Writes a route (and optionally its matching calibration) into a fresh read-write game store so a
// following read through the facade sees the game's persisted state on disk.
void writeGameConfig(
    const std::filesystem::path& game_file, const common::audio::ActiveDeviceRoute& route,
    bool with_calibration)
{
    common::audio::AudioConfigStore game_store{
        game_file, common::audio::AudioConfigStore::Access::ReadWrite
    };
    REQUIRE(game_store.setActiveDeviceRoute(route).has_value());
    if (with_calibration)
    {
        REQUIRE(game_store.saveInputCalibration(calibrationFor(*route.identity)).has_value());
    }
}

} // namespace

TEST_CASE(
    "EditorEffectiveAudioConfigStore reports availability from a calibrated game route",
    "[core][audio][effective-store]")
{
    const ScopedGameFile game_file{"effective_available.settings"};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    EditorEffectiveAudioConfigStore facade{own_store, game_file.path()};

    SECTION("no game file is unavailable")
    {
        CHECK_FALSE(facade.gameSourceAvailable());
    }

    SECTION("a route without a matching calibration is unavailable")
    {
        writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), false);
        CHECK_FALSE(facade.gameSourceAvailable());
    }

    SECTION("a calibrated route is available")
    {
        writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), true);
        CHECK(facade.gameSourceAvailable());
    }

    // Availability checks read through a throwaway view, so they must not flip the live source.
    CHECK_FALSE(facade.usingGameSource());
}

TEST_CASE(
    "EditorEffectiveAudioConfigStore selects getters by source and always writes the own store",
    "[core][audio][effective-store]")
{
    const ScopedGameFile game_file{"effective_source_select.settings"};
    const common::audio::ActiveDeviceRoute editor_route = routeFor("editor-blob", guitarIdentity());
    const common::audio::ActiveDeviceRoute game_route = routeFor("game-blob", guitarIdentity());

    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());
    writeGameConfig(game_file.path(), game_route, true);

    EditorEffectiveAudioConfigStore facade{own_store, game_file.path()};

    // Own source: getters read the editor's own route.
    CHECK_FALSE(facade.usingGameSource());
    CHECK(facade.activeDeviceRoute() == editor_route);

    // Game source: getters read the game's route, while writes stay routed to the own store.
    facade.useGameSource(true);
    CHECK(facade.usingGameSource());
    CHECK(facade.activeDeviceRoute() == game_route);

    // A calibration write during game source still lands in the editor's own store.
    const common::audio::InputCalibrationState editor_calibration =
        calibrationFor(guitarIdentity());
    REQUIRE(facade.saveInputCalibration(editor_calibration).has_value());
    const auto stored = own_store.inputCalibrationFor(guitarIdentity());
    REQUIRE(stored.has_value());
    REQUIRE(stored->has_value());
    CHECK((*stored)->calibration_gain.db == editor_calibration.calibration_gain.db);

    // The active-route setter is suppressed while sourcing the game so the game-adopted route is
    // never written into the editor's own store.
    REQUIRE(facade.setActiveDeviceRoute(game_route).has_value());
    CHECK(own_store.activeDeviceRoute() == editor_route);
}

TEST_CASE(
    "EditorEffectiveAudioConfigStore round-trips back to the editor route on source off",
    "[core][audio][effective-store]")
{
    const ScopedGameFile game_file{"effective_round_trip.settings"};
    const common::audio::ActiveDeviceRoute editor_route = routeFor("editor-blob", guitarIdentity());

    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());
    writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), true);

    EditorEffectiveAudioConfigStore facade{own_store, game_file.path()};
    facade.useGameSource(true);
    // A device-config persist while sourcing the game is suppressed; the own route is preserved.
    REQUIRE(facade.setActiveDeviceRoute(routeFor("game-blob", guitarIdentity())).has_value());

    facade.useGameSource(false);
    CHECK_FALSE(facade.usingGameSource());
    CHECK(facade.activeDeviceRoute() == editor_route);
}

TEST_CASE(
    "EditorEffectiveAudioConfigStore reads the game file fresh on each source switch",
    "[core][audio][effective-store]")
{
    const ScopedGameFile game_file{"effective_fresh_read.settings"};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    EditorEffectiveAudioConfigStore facade{own_store, game_file.path()};

    const common::audio::ActiveDeviceRoute first_route = routeFor("game-blob-1", guitarIdentity());
    writeGameConfig(game_file.path(), first_route, true);
    facade.useGameSource(true);
    CHECK(facade.activeDeviceRoute() == first_route);

    // The game writes a new route after the first read; reconstructing the view must pick it up
    // rather than serving the stale in-memory snapshot from the first construction.
    const common::audio::ActiveDeviceRoute second_route = routeFor("game-blob-2", guitarIdentity());
    writeGameConfig(game_file.path(), second_route, true);
    facade.useGameSource(true);
    CHECK(facade.activeDeviceRoute() == second_route);
}

} // namespace rock_hero::editor::core
