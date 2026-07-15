#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <juce_events/juce_events.h>
#include <optional>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/game/core/audio/game_audio_config.h>
#include <rock_hero/game/core/settings/game_settings.h>
#include <rock_hero/game/core/testing/null_game_settings.h>
#include <string>
#include <system_error>

namespace rock_hero::game::core
{

namespace
{

// Test-local temp directory owning one test case's settings file, mirroring test_game_settings.
class TemporarySettingsDirectory final
{
public:
    TemporarySettingsDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-game-audio-config-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TemporarySettingsDirectory() noexcept
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_path, cleanup_error);
    }

    TemporarySettingsDirectory(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory(TemporarySettingsDirectory&&) = delete;
    TemporarySettingsDirectory& operator=(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory& operator=(TemporarySettingsDirectory&&) = delete;

    [[nodiscard]] std::filesystem::path settingsFile() const
    {
        return m_path / "game.settings";
    }

private:
    std::filesystem::path m_path;
};

// A complete slot-0 route the v1 single-player config binds.
[[nodiscard]] common::audio::InputDeviceIdentity guitarRoute()
{
    return common::audio::InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = "Focusrite USB ASIO",
        .input_channel_index = 0,
        .input_channel_name = "Input 1",
    };
}

} // namespace

// Verifies the config defaults to empty (the game is unconfigured until the player picks a route).
TEST_CASE("Game audio config defaults to empty", "[core][settings][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    const GameSettings settings{directory.settingsFile()};
    CHECK(settings.gameAudioConfig() == GameAudioConfig{});
    CHECK(settings.gameAudioConfig().players.empty());
}

// Verifies the v1 single-slot config persists and reads back byte-for-byte across a reopen, so the
// multi-input schema is genuinely exercised by a real consumer.
TEST_CASE("Game audio config single-slot round-trips across reopen", "[core][settings][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    const GameAudioConfig config{
        .players = {PlayerInputConfig{.player_slot = 0, .route = guitarRoute()}}
    };

    {
        GameSettings settings{directory.settingsFile()};
        REQUIRE(settings.setGameAudioConfig(config).has_value());
        CHECK(settings.gameAudioConfig() == config);
    }

    const GameSettings reopened{directory.settingsFile()};
    CHECK(reopened.gameAudioConfig() == config);
}

// Verifies the schema is N-ready: a two-entry config (slot 0 and slot 1 as two channels on the one
// active device) round-trips even though v1 writes a single entry.
TEST_CASE("Game audio config multi-slot round-trips", "[core][settings][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    common::audio::InputDeviceIdentity second_route = guitarRoute();
    second_route.input_channel_index = 1;
    second_route.input_channel_name = "Input 2";

    const GameAudioConfig config{
        .players = {
            PlayerInputConfig{.player_slot = 0, .route = guitarRoute()},
            PlayerInputConfig{.player_slot = 1, .route = second_route},
        }
    };

    GameSettings settings{directory.settingsFile()};
    REQUIRE(settings.setGameAudioConfig(config).has_value());
    CHECK(settings.gameAudioConfig() == config);
}

// Verifies a later write replaces the previous config wholesale rather than appending.
TEST_CASE("Game audio config setter replaces the previous config", "[core][settings][audio]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    const GameAudioConfig first{
        .players = {PlayerInputConfig{.player_slot = 0, .route = guitarRoute()}}
    };

    common::audio::InputDeviceIdentity other_route = guitarRoute();
    other_route.input_device_name = "Behringer UMC ASIO";
    const GameAudioConfig second{
        .players = {PlayerInputConfig{.player_slot = 0, .route = other_route}}
    };

    GameSettings settings{directory.settingsFile()};
    REQUIRE(settings.setGameAudioConfig(first).has_value());
    REQUIRE(settings.setGameAudioConfig(second).has_value());
    CHECK(settings.gameAudioConfig() == second);

    // An empty config clears the stored value.
    REQUIRE(settings.setGameAudioConfig(GameAudioConfig{}).has_value());
    CHECK(settings.gameAudioConfig().players.empty());
}

// Verifies the pure primary-route mapping the P2 store mirror consumes selects slot 0's route.
TEST_CASE("Primary player route selects slot 0", "[core][settings][audio]")
{
    CHECK(primaryPlayerRoute(GameAudioConfig{}) == std::nullopt);

    common::audio::InputDeviceIdentity slot_one_route = guitarRoute();
    slot_one_route.input_channel_index = 1;

    // Slot 0 is selected regardless of stored order.
    const GameAudioConfig config{
        .players = {
            PlayerInputConfig{.player_slot = 1, .route = slot_one_route},
            PlayerInputConfig{.player_slot = 0, .route = guitarRoute()},
        }
    };
    const auto primary = primaryPlayerRoute(config);
    REQUIRE(primary.has_value());
    if (primary.has_value())
    {
        CHECK(*primary == guitarRoute());
    }
}

// Verifies the null fake satisfies the extended port with an empty config and accepting writes.
TEST_CASE("Null game settings reports an empty audio config", "[core][settings][audio]")
{
    testing::NullGameSettings settings;

    CHECK(settings.gameAudioConfig().players.empty());
    CHECK(settings
              .setGameAudioConfig(
                  GameAudioConfig{
                      .players = {PlayerInputConfig{.player_slot = 0, .route = guitarRoute()}}
                  })
              .has_value());
}

} // namespace rock_hero::game::core
