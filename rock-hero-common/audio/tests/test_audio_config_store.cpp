#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <rock_hero/common/audio/settings/active_device_route.h>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_identity.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/core/shared/application_identity.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef TEST_SETTINGS_DIR
#define TEST_SETTINGS_DIR "."
#endif

namespace rock_hero::common::audio
{

namespace
{

constexpr const char* g_input_calibration_states_key{"inputCalibrationStates"};

// Owns one build-local settings file so each test starts with clean persisted state.
class ScopedSettingsFile final
{
public:
    // Creates a settings-file path and removes any stale file from a prior test run.
    explicit ScopedSettingsFile(std::string_view file_name)
        : m_path(std::filesystem::path{TEST_SETTINGS_DIR} / file_name)
    {
        removeFile();
    }

    // Removes the settings file so persistence tests cannot leak state into later tests.
    ~ScopedSettingsFile()
    {
        removeFile();
    }

    ScopedSettingsFile(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
    ScopedSettingsFile(ScopedSettingsFile&&) = delete;
    ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

    // Returns the test-owned settings-file path.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    // Removes the settings file on a best-effort basis.
    void removeFile() const
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    // Build-local settings path owned by this fixture.
    std::filesystem::path m_path;
};

// Builds explicit properties-file options matching the store's explicit-path constructor so tests
// can seed raw and malformed properties through the same JUCE storage the store reads.
[[nodiscard]] juce::PropertiesFile::Options testStoreOptions()
{
    juce::PropertiesFile::Options options;
    const std::string_view folder_name = common::core::applicationDataFolderName();
    options.filenameSuffix = ".settings";
    options.folderName = juce::String{folder_name.data(), folder_name.size()};
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.ignoreCaseOfKeyNames = false;
    options.doNotSave = false;
    options.millisecondsBeforeSaving = 0;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.processLock = nullptr;
    return options;
}

// Writes one raw property through JUCE so malformed settings use production storage.
void writeRawSetting(
    const std::filesystem::path& settings_file, const char* key, const juce::var& value)
{
    juce::PropertiesFile properties{
        common::core::juceFileFromPath(settings_file), testStoreOptions()
    };
    properties.setValue(key, value);
    REQUIRE(properties.save());
}

// Reads a settings file's bytes so read-only tests can prove the file is left unchanged.
[[nodiscard]] std::string readFileBytes(const std::filesystem::path& settings_file)
{
    std::ifstream stream{settings_file, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

// Builds a stable route identity for store tests.
[[nodiscard]] InputDeviceIdentity makeIdentity(
    std::string input_device_name = "Interface A", int channel_index = 0,
    std::string backend_name = "ASIO", std::string channel_name = {})
{
    if (channel_name.empty())
    {
        channel_name = "Input " + std::to_string(channel_index + 1);
    }

    return InputDeviceIdentity{
        .backend_name = std::move(backend_name),
        .input_device_name = std::move(input_device_name),
        .input_channel_index = channel_index,
        .input_channel_name = std::move(channel_name),
    };
}

// Builds a calibration record for one physical route.
[[nodiscard]] InputCalibrationState calibrationFor(
    const InputDeviceIdentity& identity, double gain_db)
{
    return InputCalibrationState{
        .calibration_gain = Gain{gain_db},
        .input_device_identity = identity,
    };
}

// Reads calibration through the typed store contract and returns the optional payload.
[[nodiscard]] std::optional<InputCalibrationState> inputCalibrationFor(
    const AudioConfigStore& store, const InputDeviceIdentity& identity)
{
    auto result = store.inputCalibrationFor(identity);
    REQUIRE(result.has_value());
    return std::move(*result);
}

} // namespace

// A new store invents no route and no calibration until the app writes one.
TEST_CASE("AudioConfigStore starts empty", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_starts_empty.settings"};
    const AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};

    CHECK_FALSE(store.activeDeviceRoute().has_value());
    CHECK_FALSE(inputCalibrationFor(store, makeIdentity()).has_value());
}

// The store persists the device blob paired with its resolved input route identity.
TEST_CASE(
    "AudioConfigStore persists the active device route with identity", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_active_route.settings"};
    const ActiveDeviceRoute route{
        .serialized_state = R"(<DEVICESETUP deviceType="ASIO" audioOutputDeviceName="ASIO"/>)",
        .identity = makeIdentity(),
    };

    {
        AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
        REQUIRE(store.setActiveDeviceRoute(route).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    CHECK(reloaded.activeDeviceRoute() == std::optional{route});
}

// A route with no resolved identity round-trips as a bare blob with absent identity.
TEST_CASE("AudioConfigStore persists an active route without identity", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_active_route_no_identity.settings"};
    const ActiveDeviceRoute route{
        .serialized_state = "<DEVICESETUP deviceType=\"ASIO\"/>",
        .identity = std::nullopt,
    };

    {
        AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
        REQUIRE(store.setActiveDeviceRoute(route).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    const auto stored = reloaded.activeDeviceRoute();
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK(stored->serialized_state == route.serialized_state);
        CHECK_FALSE(stored->identity.has_value());
    }
}

// Clearing the active route removes it from the store without disturbing calibration.
TEST_CASE("AudioConfigStore clears the active device route", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_clear_active_route.settings"};
    const InputDeviceIdentity identity = makeIdentity();

    AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    REQUIRE(store
                .setActiveDeviceRoute(
                    ActiveDeviceRoute{.serialized_state = "<DEVICESETUP/>", .identity = identity})
                .has_value());
    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 5.0)).has_value());

    REQUIRE(store.setActiveDeviceRoute(std::nullopt).has_value());

    CHECK_FALSE(store.activeDeviceRoute().has_value());
    // Calibration is a separate record family and must survive clearing the route.
    CHECK(inputCalibrationFor(store, identity).has_value());
}

// Calibration history persists one physical route and ignores unrelated routes.
TEST_CASE("AudioConfigStore persists physical input calibration", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_calibration.settings"};
    const InputDeviceIdentity identity = makeIdentity();
    const InputDeviceIdentity other_identity = makeIdentity("Interface B");

    {
        AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 6.5)).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    const auto stored = inputCalibrationFor(reloaded, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(6.5, 0));
        CHECK(stored->input_device_identity == identity);
    }
    CHECK_FALSE(inputCalibrationFor(reloaded, other_identity).has_value());
}

// Duplicate XML records for one physical route collapse to the last valid record on load.
TEST_CASE("AudioConfigStore collapses duplicate calibration history", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_duplicate_calibration.settings"};
    const InputDeviceIdentity identity = makeIdentity();
    writeRawSetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{
            R"(<INPUT_CALIBRATIONS formatVersion="1">)"
            R"(<CALIBRATION gainDb="2.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0" inputChannelName="Input 1"/>)"
            R"(<CALIBRATION gainDb="7.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0" inputChannelName="Mic/Inst 1"/>)"
            R"(</INPUT_CALIBRATIONS>)"
        });

    const AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    const auto stored = inputCalibrationFor(store, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(7.0, 0));
    }
}

// Removing one physical route leaves other saved route calibrations intact.
TEST_CASE("AudioConfigStore removes one physical calibration", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_remove_calibration.settings"};
    const InputDeviceIdentity first_identity = makeIdentity("Interface A");
    const InputDeviceIdentity second_identity = makeIdentity("Interface B");
    AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    REQUIRE(store.saveInputCalibration(calibrationFor(first_identity, 3.0)).has_value());
    REQUIRE(store.saveInputCalibration(calibrationFor(second_identity, 6.0)).has_value());

    REQUIRE(store.removeInputCalibration(first_identity).has_value());

    CHECK_FALSE(inputCalibrationFor(store, first_identity).has_value());
    const auto preserved = inputCalibrationFor(store, second_identity);
    REQUIRE(preserved.has_value());
    if (preserved.has_value())
    {
        CHECK_THAT(preserved->calibration_gain.db, Catch::Matchers::WithinULP(6.0, 0));
    }
}

// Out-of-range gains are clamped to the supported calibration range on the way into the store.
TEST_CASE("AudioConfigStore clamps calibration gain", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_clamp_gain.settings"};
    const InputDeviceIdentity identity = makeIdentity();
    AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};

    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 100.0)).has_value());

    const auto stored = inputCalibrationFor(store, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(maximumGainDb(), 0));
    }
}

// Malformed calibration XML surfaces as a typed error rather than silent absence, and neither
// lookup nor removal overwrites the unreadable state.
TEST_CASE("AudioConfigStore preserves malformed calibration history", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_malformed_calibration.settings"};
    const InputDeviceIdentity identity = makeIdentity();
    writeRawSetting(settings_file.path(), g_input_calibration_states_key, juce::String{"[not-xml"});

    AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};

    const auto loaded = store.inputCalibrationFor(identity);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == AudioConfigErrorCode::InvalidInputCalibrationHistory);
    CHECK_FALSE(loaded.error().message.empty());

    const auto removed = store.removeInputCalibration(identity);
    REQUIRE_FALSE(removed.has_value());
    CHECK(removed.error().code == AudioConfigErrorCode::InvalidInputCalibrationHistory);

    const auto saved = store.saveInputCalibration(calibrationFor(makeIdentity("Interface B"), 4.0));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().code == AudioConfigErrorCode::InvalidInputCalibrationHistory);
}

// The active route and calibration are independent record families that persist side by side.
TEST_CASE("AudioConfigStore keeps route and calibration independent", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_independence.settings"};
    const InputDeviceIdentity identity = makeIdentity();
    const ActiveDeviceRoute route{
        .serialized_state = "<DEVICESETUP deviceType=\"ASIO\"/>",
        .identity = identity,
    };

    {
        AudioConfigStore store{settings_file.path(), AudioConfigStore::Access::ReadWrite};
        REQUIRE(store.setActiveDeviceRoute(route).has_value());
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 9.0)).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path(), AudioConfigStore::Access::ReadWrite};
    CHECK(reloaded.activeDeviceRoute() == std::optional{route});
    const auto stored = inputCalibrationFor(reloaded, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(9.0, 0));
    }
}

// A read-only store rejects every setter with CouldNotSave and leaves the file byte-for-byte intact.
TEST_CASE("AudioConfigStore read-only rejects every setter", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_read_only.settings"};
    const InputDeviceIdentity identity = makeIdentity();

    {
        AudioConfigStore writer{settings_file.path(), AudioConfigStore::Access::ReadWrite};
        REQUIRE(
            writer
                .setActiveDeviceRoute(
                    ActiveDeviceRoute{.serialized_state = "<DEVICESETUP/>", .identity = identity})
                .has_value());
        REQUIRE(writer.saveInputCalibration(calibrationFor(identity, 5.0)).has_value());
    }

    const std::string before = readFileBytes(settings_file.path());

    AudioConfigStore reader{settings_file.path(), AudioConfigStore::Access::ReadOnly};

    // Getters still work on a read-only store.
    CHECK(reader.activeDeviceRoute().has_value());
    CHECK(inputCalibrationFor(reader, identity).has_value());

    const auto route_result = reader.setActiveDeviceRoute(
        ActiveDeviceRoute{.serialized_state = "<OTHER/>", .identity = {}});
    REQUIRE_FALSE(route_result.has_value());
    CHECK(route_result.error().code == AudioConfigErrorCode::CouldNotSave);

    const auto save_result =
        reader.saveInputCalibration(calibrationFor(makeIdentity("Interface B"), 8.0));
    REQUIRE_FALSE(save_result.has_value());
    CHECK(save_result.error().code == AudioConfigErrorCode::CouldNotSave);

    const auto remove_result = reader.removeInputCalibration(identity);
    REQUIRE_FALSE(remove_result.has_value());
    CHECK(remove_result.error().code == AudioConfigErrorCode::CouldNotSave);

    CHECK(readFileBytes(settings_file.path()) == before);
}

// The two identity constants name distinct audio-config file partitions.
TEST_CASE("AudioConfigStore identity constants name distinct files", "[audio][config-store]")
{
    CHECK(editorAudioConfigApplicationName() == "Rock Hero Editor Audio");
    CHECK(gameAudioConfigApplicationName() == "Rock Hero Game Audio");
    CHECK(editorAudioConfigApplicationName() != gameAudioConfigApplicationName());
}

} // namespace rock_hero::common::audio
