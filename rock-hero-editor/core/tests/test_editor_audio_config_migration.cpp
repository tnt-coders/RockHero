#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/input/input_calibration_state.h>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/in_memory_audio_config_store.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/editor/core/settings/editor_audio_settings_migration.h>
#include <rock_hero/editor/core/settings/editor_settings.h>
#include <string>
#include <string_view>
#include <system_error>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::editor::core
{

namespace
{

constexpr const char* g_audio_device_state_key{"audioDeviceState"};
constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"};

// One legacy calibration record for the identity the tests look up.
constexpr const char* g_legacy_calibration_xml{
    R"(<INPUT_CALIBRATIONS formatVersion="1">)"
    R"(<CALIBRATION gainDb="5.0" backendName="ASIO" inputDeviceName="Interface A" )"
    R"(inputChannelIndex="0" inputChannelName="Input 1"/>)"
    R"(</INPUT_CALIBRATIONS>)"
};

// Owns the legacy settings file plus the sibling audio-config file EditorSettings opens, so each
// test starts clean and leaves nothing behind.
class ScopedSettingsFile final
{
public:
    explicit ScopedSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFiles();
    }

    ~ScopedSettingsFile()
    {
        removeFiles();
    }

    ScopedSettingsFile(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile(ScopedSettingsFile&&) = delete;
    ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    void removeFiles() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
        std::filesystem::remove(EditorSettings::audioConfigFileFor(m_path), error);
    }

    std::filesystem::path m_path;
};

// Builds explicit properties-file options so tests seed obsolete keys through production storage.
[[nodiscard]] juce::PropertiesFile::Options legacySettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

// Seeds one obsolete key into the legacy settings file through the same JUCE storage EditorSettings
// reads on migration.
void writeLegacySetting(
    const std::filesystem::path& settings_file, const char* key, const juce::String& value)
{
    juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), legacySettingsOptions()
    };
    properties.setValue(key, value);
    REQUIRE(properties.save());
}

// Route identity matching the seeded legacy calibration record.
[[nodiscard]] common::audio::InputDeviceIdentity makeIdentity()
{
    return common::audio::InputDeviceIdentity{
        .backend_name = "ASIO",
        .input_device_name = "Interface A",
        .input_channel_index = 0,
        .input_channel_name = "Input 1",
    };
}

} // namespace

// An empty store gets both legacy values copied across, and the legacy keys are then cleared.
TEST_CASE(
    "migrateEditorAudioSettings copies device state and calibration when the store is empty",
    "[core][settings][migration]")
{
    const ScopedSettingsFile settings_file{"migration_copy_when_missing.settings"};
    writeLegacySetting(
        settings_file.path(), g_audio_device_state_key, juce::String{"legacy-device-state"});
    writeLegacySetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{g_legacy_calibration_xml});
    EditorSettings legacy{settings_file.path()};
    common::audio::testing::InMemoryAudioConfigStore store;

    migrateEditorAudioSettings(legacy, store);

    REQUIRE(store.activeDeviceRoute().has_value());
    CHECK(store.activeDeviceRoute()->serialized_state == "legacy-device-state");
    // The identity is recomputed on the next successful apply, so it migrates absent.
    CHECK_FALSE(store.activeDeviceRoute()->identity.has_value());

    const auto calibration = store.inputCalibrationFor(makeIdentity());
    REQUIRE(calibration.has_value());
    REQUIRE(calibration->has_value());
    if (calibration.has_value() && calibration->has_value())
    {
        CHECK_THAT((*calibration)->calibration_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    }

    CHECK_FALSE(legacy.readLegacyAudioDeviceState().has_value());
    CHECK(legacy.readLegacyInputCalibrations().empty());
}

// A store that already has a route keeps it; copy-not-move leaves the legacy device state intact.
TEST_CASE(
    "migrateEditorAudioSettings never overwrites an existing device route",
    "[core][settings][migration]")
{
    const ScopedSettingsFile settings_file{"migration_keep_existing_route.settings"};
    writeLegacySetting(
        settings_file.path(), g_audio_device_state_key, juce::String{"legacy-device-state"});
    EditorSettings legacy{settings_file.path()};
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store
                .setActiveDeviceRoute(
                    common::audio::ActiveDeviceRoute{
                        .serialized_state = "existing-route", .identity = makeIdentity()
                    })
                .has_value());

    migrateEditorAudioSettings(legacy, store);

    REQUIRE(store.activeDeviceRoute().has_value());
    CHECK(store.activeDeviceRoute()->serialized_state == "existing-route");
    CHECK(legacy.readLegacyAudioDeviceState() == std::optional<std::string>{"legacy-device-state"});
}

// A route already present in the store is not overwritten; the redundant legacy record is cleared.
TEST_CASE(
    "migrateEditorAudioSettings never overwrites existing calibration",
    "[core][settings][migration]")
{
    const ScopedSettingsFile settings_file{"migration_keep_existing_calibration.settings"};
    writeLegacySetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{g_legacy_calibration_xml});
    EditorSettings legacy{settings_file.path()};
    common::audio::testing::InMemoryAudioConfigStore store;
    REQUIRE(store
                .saveInputCalibration(
                    common::audio::InputCalibrationState{
                        .calibration_gain = common::audio::Gain{3.0},
                        .input_device_identity = makeIdentity()
                    })
                .has_value());

    migrateEditorAudioSettings(legacy, store);

    const auto calibration = store.inputCalibrationFor(makeIdentity());
    REQUIRE(calibration.has_value());
    REQUIRE(calibration->has_value());
    if (calibration.has_value() && calibration->has_value())
    {
        CHECK_THAT((*calibration)->calibration_gain.db, Catch::Matchers::WithinULP(3.0, 0));
    }
    CHECK(legacy.readLegacyInputCalibrations().empty());
}

// Absent legacy keys migrate as a no-op success without touching the store.
TEST_CASE("migrateEditorAudioSettings tolerates absent legacy keys", "[core][settings][migration]")
{
    const ScopedSettingsFile settings_file{"migration_no_source.settings"};
    EditorSettings legacy{settings_file.path()};
    common::audio::testing::InMemoryAudioConfigStore store;

    migrateEditorAudioSettings(legacy, store);

    CHECK_FALSE(store.activeDeviceRoute().has_value());
    CHECK(store.input_calibrations.empty());
}

// Re-running the migration is a no-op once the legacy keys are cleared.
TEST_CASE("migrateEditorAudioSettings is idempotent across re-runs", "[core][settings][migration]")
{
    const ScopedSettingsFile settings_file{"migration_idempotent.settings"};
    writeLegacySetting(
        settings_file.path(), g_audio_device_state_key, juce::String{"legacy-device-state"});
    writeLegacySetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{g_legacy_calibration_xml});
    EditorSettings legacy{settings_file.path()};
    common::audio::testing::InMemoryAudioConfigStore store;

    migrateEditorAudioSettings(legacy, store);
    migrateEditorAudioSettings(legacy, store);

    REQUIRE(store.activeDeviceRoute().has_value());
    CHECK(store.activeDeviceRoute()->serialized_state == "legacy-device-state");
    CHECK(store.input_calibrations.size() == 1);
    CHECK_FALSE(legacy.readLegacyAudioDeviceState().has_value());
    CHECK(legacy.readLegacyInputCalibrations().empty());
}

} // namespace rock_hero::editor::core
