#include <catch2/catch_test_macros.hpp>
#include <expected>
#include <filesystem>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/editor/core/audio/editor_audio_config_store.h>
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
// following read through the store sees the game's persisted state on disk.
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
    "EditorAudioConfigStore reports availability from a calibrated game route",
    "[core][audio][editor-config-store]")
{
    const ScopedGameFile game_file{"editor_config_available.settings"};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    EditorAudioConfigStore store{own_store, game_file.path()};

    SECTION("no game file is unavailable")
    {
        CHECK_FALSE(store.gameSourceAvailable());
    }

    SECTION("a route without a matching calibration is unavailable")
    {
        writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), false);
        CHECK_FALSE(store.gameSourceAvailable());
    }

    SECTION("a calibrated route is available")
    {
        writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), true);
        CHECK(store.gameSourceAvailable());
    }

    // Availability checks read through a throwaway view, so they must not flip the active source.
    CHECK_FALSE(store.usingGameSource());
}

TEST_CASE(
    "EditorAudioConfigStore delegates reads and writes to the active source",
    "[core][audio][editor-config-store]")
{
    const ScopedGameFile game_file{"editor_config_source_select.settings"};
    const common::audio::ActiveDeviceRoute editor_route = routeFor("editor-blob", guitarIdentity());
    const common::audio::ActiveDeviceRoute game_route = routeFor("game-blob", guitarIdentity());

    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());
    writeGameConfig(game_file.path(), game_route, true);

    EditorAudioConfigStore store{own_store, game_file.path()};

    // Own source: reads and writes both delegate to the editor's own store.
    CHECK_FALSE(store.usingGameSource());
    CHECK(store.activeDeviceRoute() == editor_route);

    const common::audio::InputCalibrationState editor_calibration =
        calibrationFor(guitarIdentity());
    REQUIRE(store.saveInputCalibration(editor_calibration).has_value());
    const auto own_stored = own_store.inputCalibrationFor(guitarIdentity());
    REQUIRE(own_stored.has_value());
    REQUIRE(own_stored->has_value());
    CHECK((*own_stored)->calibration_gain.db == editor_calibration.calibration_gain.db);

    // Game source: reads delegate to the game's route.
    store.useGameSource(true);
    CHECK(store.usingGameSource());
    CHECK(store.activeDeviceRoute() == game_route);

    // Writes while sourcing the game hit the read-only game view and fail loudly rather than being
    // redirected into the editor's own store; the own store keeps its route and calibration.
    const std::expected<void, common::audio::AudioConfigError> route_write =
        store.setActiveDeviceRoute(game_route);
    REQUIRE_FALSE(route_write.has_value());
    CHECK(route_write.error().code == common::audio::AudioConfigErrorCode::CouldNotSave);
    CHECK(own_store.activeDeviceRoute() == editor_route);

    const std::expected<void, common::audio::AudioConfigError> calibration_write =
        store.saveInputCalibration(calibrationFor(guitarIdentity()));
    REQUIRE_FALSE(calibration_write.has_value());
    CHECK(calibration_write.error().code == common::audio::AudioConfigErrorCode::CouldNotSave);
}

TEST_CASE(
    "EditorAudioConfigStore round-trips back to the editor route on source off",
    "[core][audio][editor-config-store]")
{
    const ScopedGameFile game_file{"editor_config_round_trip.settings"};
    const common::audio::ActiveDeviceRoute editor_route = routeFor("editor-blob", guitarIdentity());

    common::audio::testing::InMemoryAudioConfigStore own_store;
    REQUIRE(own_store.setActiveDeviceRoute(editor_route).has_value());
    writeGameConfig(game_file.path(), routeFor("game-blob", guitarIdentity()), true);

    EditorAudioConfigStore store{own_store, game_file.path()};
    store.useGameSource(true);
    // A device-config persist while sourcing the game fails at the read-only game view; the own
    // route is left untouched.
    CHECK_FALSE(store.setActiveDeviceRoute(routeFor("game-blob", guitarIdentity())).has_value());

    store.useGameSource(false);
    CHECK_FALSE(store.usingGameSource());
    CHECK(store.activeDeviceRoute() == editor_route);
}

TEST_CASE(
    "EditorAudioConfigStore reads the game file fresh on each source switch",
    "[core][audio][editor-config-store]")
{
    const ScopedGameFile game_file{"editor_config_fresh_read.settings"};
    common::audio::testing::InMemoryAudioConfigStore own_store;
    EditorAudioConfigStore store{own_store, game_file.path()};

    const common::audio::ActiveDeviceRoute first_route = routeFor("game-blob-1", guitarIdentity());
    writeGameConfig(game_file.path(), first_route, true);
    store.useGameSource(true);
    CHECK(store.activeDeviceRoute() == first_route);

    // The game writes a new route after the first read; reconstructing the view must pick it up
    // rather than serving the stale in-memory snapshot from the first construction.
    const common::audio::ActiveDeviceRoute second_route = routeFor("game-blob-2", guitarIdentity());
    writeGameConfig(game_file.path(), second_route, true);
    store.useGameSource(true);
    CHECK(store.activeDeviceRoute() == second_route);
}

} // namespace rock_hero::editor::core
