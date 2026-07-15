#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <juce_events/juce_events.h>
#include <rock_hero/game/core/settings/game_settings.h>
#include <rock_hero/game/core/testing/null_game_settings.h>
#include <string>
#include <system_error>
#include <vector>

namespace rock_hero::game::core
{

namespace
{

// Test-local temp directory owning one test case's settings file.
class TemporarySettingsDirectory final
{
public:
    // Creates a test-local directory under the platform temp root.
    TemporarySettingsDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-game-settings-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    // Removes the test directory on a best-effort basis.
    ~TemporarySettingsDirectory() noexcept
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(m_path, cleanup_error);
    }

    TemporarySettingsDirectory(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory(TemporarySettingsDirectory&&) = delete;
    TemporarySettingsDirectory& operator=(const TemporarySettingsDirectory&) = delete;
    TemporarySettingsDirectory& operator=(TemporarySettingsDirectory&&) = delete;

    // The isolated settings file path for the test's GameSettings instance.
    [[nodiscard]] std::filesystem::path settingsFile() const
    {
        return m_path / "game.settings";
    }

private:
    std::filesystem::path m_path;
};

} // namespace

// Verifies the profile id mints once, persists, and stays stable across reopen — every score
// record stamps it, so regeneration would orphan a player's history.
TEST_CASE("Game settings profile id is stable across reopen", "[core][settings]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    std::string first_id;
    {
        GameSettings settings{directory.settingsFile()};
        const auto minted = settings.getOrCreateProfileId();
        REQUIRE(minted.has_value());
        CHECK_FALSE(minted->empty());
        first_id = *minted;

        // A second call in the same session returns the identical id.
        const auto again = settings.getOrCreateProfileId();
        REQUIRE(again.has_value());
        CHECK(*again == first_id);
    }

    // A fresh instance over the same file reads the persisted id back.
    GameSettings reopened{directory.settingsFile()};
    const auto restored = reopened.getOrCreateProfileId();
    REQUIRE(restored.has_value());
    CHECK(*restored == first_id);
}

// Verifies the display name defaults to "Player", round-trips, and rejects empty names.
TEST_CASE("Game settings display name defaults and round-trips", "[core][settings]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;
    GameSettings settings{directory.settingsFile()};

    CHECK(settings.profileDisplayName() == "Player");

    REQUIRE(settings.setProfileDisplayName("Shredder").has_value());
    CHECK(settings.profileDisplayName() == "Shredder");

    const auto rejected = settings.setProfileDisplayName("");
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == GameSettingsErrorCode::InvalidSettingValue);
    CHECK(settings.profileDisplayName() == "Shredder");
}

// Verifies the first-run flag distinguishes never-set from explicitly set, and persists.
TEST_CASE("Game settings first-run flag persists", "[core][settings]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    {
        GameSettings settings{directory.settingsFile()};
        CHECK_FALSE(settings.firstRunCompleted().has_value());
        REQUIRE(settings.setFirstRunCompleted(true).has_value());
        CHECK(settings.firstRunCompleted() == std::optional{true});
    }

    const GameSettings reopened{directory.settingsFile()};
    CHECK(reopened.firstRunCompleted() == std::optional{true});
}

// Verifies custom scan roots default to empty, round-trip (including a non-ASCII path), and a
// new set replaces the previous one wholesale.
TEST_CASE("Game settings custom scan roots round-trip", "[core][settings]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    const std::vector<std::filesystem::path> roots = {
        std::filesystem::path{"D:/Extra Songs"},
        std::filesystem::path{std::u8string{u8"E:/Café Songs"}},
    };

    {
        GameSettings settings{directory.settingsFile()};
        CHECK(settings.customScanRoots().empty());
        REQUIRE(settings.setCustomScanRoots(roots).has_value());
        CHECK(settings.customScanRoots() == roots);
    }

    GameSettings reopened{directory.settingsFile()};
    CHECK(reopened.customScanRoots() == roots); // persisted across reopen, order and Unicode intact

    // A new set replaces the old one; an empty set clears it.
    REQUIRE(reopened.setCustomScanRoots(std::vector<std::filesystem::path>{}).has_value());
    CHECK(reopened.customScanRoots().empty());
}

// Verifies a corrupt stored custom-roots value degrades to none instead of crashing startup.
TEST_CASE("Game settings custom scan roots tolerate a corrupt value", "[core][settings]")
{
    const juce::ScopedJuceInitialiser_GUI juce_runtime;
    const TemporarySettingsDirectory directory;

    // Hand-write a settings file whose customScanRoots value is not a JSON array.
    {
        std::ofstream stream{directory.settingsFile(), std::ios::binary};
        stream << R"(<?xml version="1.0" encoding="UTF-8"?>)"
               << "\n<PROPERTIES>\n"
               << R"(  <VALUE name="customScanRoots" val="not valid json {"/>)"
               << "\n</PROPERTIES>\n";
    }

    const GameSettings settings{directory.settingsFile()};
    CHECK(settings.customScanRoots().empty());
}

// Verifies the null fake satisfies the port with defaults and accepting writes.
TEST_CASE("Null game settings reports defaults", "[core][settings]")
{
    testing::NullGameSettings settings;

    const auto profile_id = settings.getOrCreateProfileId();
    REQUIRE(profile_id.has_value());
    CHECK_FALSE(profile_id->empty());
    CHECK(settings.profileDisplayName() == "Player");
    CHECK_FALSE(settings.firstRunCompleted().has_value());
    CHECK(settings.customScanRoots().empty());
    CHECK(settings.setProfileDisplayName("Anyone").has_value());
    CHECK(settings.setFirstRunCompleted(true).has_value());
    CHECK(settings.setCustomScanRoots(std::vector<std::filesystem::path>{}).has_value());
}

} // namespace rock_hero::game::core
