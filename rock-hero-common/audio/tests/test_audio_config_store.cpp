#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/shared/settings_file_options.h>
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

using testing::makeInputDeviceIdentity;

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

// Matches the store's storage format, so seeded raw and malformed properties go through the same
// JUCE storage the store reads. The application name only places the default file, and the tests
// supply the file directly.
[[nodiscard]] juce::PropertiesFile::Options testStoreOptions()
{
    return common::core::settingsFileOptions({});
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
    const AudioConfigStore store{settings_file.path()};

    CHECK_FALSE(store.activeDeviceRoute().has_value());
    CHECK_FALSE(inputCalibrationFor(store, makeInputDeviceIdentity()).has_value());
}

// JUCE stores an XML-shaped value as an element and re-emits it single-line without a header, so
// the blob round-trips as XML, not as bytes; every reader parses it.
TEST_CASE("AudioConfigStore persists the active device route", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_active_route.settings"};
    juce::XmlElement expected{"DEVICESETUP"};
    expected.setAttribute("deviceType", "ASIO");
    expected.setAttribute("audioOutputDeviceName", "ASIO");

    {
        AudioConfigStore store{settings_file.path()};
        REQUIRE(store.setActiveDeviceRoute(expected.toString().toStdString()).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path()};
    const std::optional<std::string> stored = reloaded.activeDeviceRoute();
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        const std::unique_ptr<juce::XmlElement> parsed = juce::parseXML(juce::String{*stored});
        REQUIRE(parsed != nullptr);
        CHECK(parsed->isEquivalentTo(&expected, false));
    }
}

// Clearing the active route removes it from the store without disturbing calibration.
TEST_CASE("AudioConfigStore clears the active device route", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_clear_active_route.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();

    AudioConfigStore store{settings_file.path()};
    REQUIRE(store.setActiveDeviceRoute("<DEVICESETUP/>").has_value());
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
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    const InputDeviceIdentity other_identity = makeInputDeviceIdentity("ASIO", "Interface B");

    {
        AudioConfigStore store{settings_file.path()};
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 6.5)).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path()};
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
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    writeRawSetting(
        settings_file.path(),
        g_input_calibration_states_key,
        juce::String{
            R"(<INPUT_CALIBRATIONS formatVersion="1">)"
            R"(<CALIBRATION gainDb="2.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0"/>)"
            R"(<CALIBRATION gainDb="7.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0"/>)"
            R"(</INPUT_CALIBRATIONS>)"
        });

    const AudioConfigStore store{settings_file.path()};
    const auto stored = inputCalibrationFor(store, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(7.0, 0));
    }
}

// Out-of-range gains are clamped to the supported calibration range on the way into the store.
TEST_CASE("AudioConfigStore clamps calibration gain", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_clamp_gain.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore store{settings_file.path()};

    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 100.0)).has_value());

    const auto stored = inputCalibrationFor(store, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(maximumGainDb(), 0));
    }
}

// Malformed calibration XML surfaces as a typed error rather than silent absence, and a save does
// not overwrite the unreadable state.
TEST_CASE("AudioConfigStore preserves malformed calibration history", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_malformed_calibration.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    writeRawSetting(settings_file.path(), g_input_calibration_states_key, juce::String{"[not-xml"});

    AudioConfigStore store{settings_file.path()};

    const auto loaded = store.inputCalibrationFor(identity);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == AudioConfigErrorCode::InvalidInputCalibrationHistory);
    CHECK_FALSE(loaded.error().message.empty());

    const auto saved = store.saveInputCalibration(
        calibrationFor(makeInputDeviceIdentity("ASIO", "Interface B"), 4.0));
    REQUIRE_FALSE(saved.has_value());
    CHECK(saved.error().code == AudioConfigErrorCode::InvalidInputCalibrationHistory);
}

// The active route and calibration are independent record families that persist side by side.
TEST_CASE("AudioConfigStore keeps route and calibration independent", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_independence.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    const std::string route{"<DEVICESETUP deviceType=\"ASIO\"/>"};

    {
        AudioConfigStore store{settings_file.path()};
        REQUIRE(store.setActiveDeviceRoute(route).has_value());
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 9.0)).has_value());
    }

    const AudioConfigStore reloaded{settings_file.path()};
    CHECK(reloaded.activeDeviceRoute() == std::optional{route});
    const auto stored = inputCalibrationFor(reloaded, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(9.0, 0));
    }
}

// Both products hold their own store over the one shared file, so a store never serves a stale
// copy: a write through one is visible to a store opened before it.
TEST_CASE("AudioConfigStore reads another store's write", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_fresh_read.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore editor{settings_file.path()};
    const AudioConfigStore game{settings_file.path()};

    REQUIRE(editor.saveInputCalibration(calibrationFor(identity, 4.5)).has_value());

    const auto stored = inputCalibrationFor(game, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(4.5, 0));
    }
}

// A write rereads the file before it saves, so it keeps what another store wrote to the other
// record family rather than overwriting it with an older copy.
TEST_CASE("AudioConfigStore write keeps another store's write", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_no_clobber.settings"};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore editor{settings_file.path()};
    AudioConfigStore game{settings_file.path()};

    REQUIRE(editor.saveInputCalibration(calibrationFor(identity, 3.0)).has_value());
    REQUIRE(game.setActiveDeviceRoute("<DEVICESETUP/>").has_value());

    CHECK(editor.activeDeviceRoute().has_value());
    CHECK(inputCalibrationFor(editor, identity).has_value());
}

} // namespace rock_hero::common::audio
