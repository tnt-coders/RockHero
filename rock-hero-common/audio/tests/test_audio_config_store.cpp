#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/settings/audio_config_error.h>
#include <rock_hero/common/audio/settings/audio_config_store.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/testing/input_device_identity_fixtures.h>
#include <rock_hero/common/core/shared/shared_settings_file.h>
#include <rock_hero/common/core/testing/scoped_settings_file.h>
#include <string>
#include <utility>

namespace rock_hero::common::audio
{

namespace
{

using common::core::testing::ScopedSettingsFile;
using testing::makeInputDeviceIdentity;

constexpr const char* g_input_calibration_states_key{"audio.inputCalibrationStates"};

// Writes one raw property through the shared file, so malformed settings use production storage.
void writeRawSetting(
    const common::core::SharedSettingsFile& settings_file, const char* key, const juce::var& value)
{
    juce::PropertiesFile properties = settings_file.open();
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
    common::core::SharedSettingsFile shared{settings_file.path()};
    const AudioConfigStore store{shared};

    CHECK_FALSE(store.activeDeviceRoute().has_value());
    CHECK_FALSE(inputCalibrationFor(store, makeInputDeviceIdentity()).has_value());
}

// JUCE stores an XML-shaped value as an element and re-emits it single-line without a header, so
// the blob round-trips as XML, not as bytes; every reader parses it.
TEST_CASE("AudioConfigStore persists the active device route", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_active_route.settings"};
    common::core::SharedSettingsFile shared{settings_file.path()};
    juce::XmlElement expected{"DEVICESETUP"};
    expected.setAttribute("deviceType", "ASIO");
    expected.setAttribute("audioOutputDeviceName", "ASIO");

    {
        AudioConfigStore store{shared};
        REQUIRE(store.setActiveDeviceRoute(expected.toString().toStdString()).has_value());
    }

    const AudioConfigStore reloaded{shared};
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
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();

    AudioConfigStore store{shared};
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
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    const InputDeviceIdentity other_identity = makeInputDeviceIdentity("ASIO", "Interface B");

    {
        AudioConfigStore store{shared};
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 6.5)).has_value());
    }

    const AudioConfigStore reloaded{shared};
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
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    writeRawSetting(
        shared,
        g_input_calibration_states_key,
        juce::String{
            R"(<INPUT_CALIBRATIONS formatVersion="1">)"
            R"(<CALIBRATION gainDb="2.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0"/>)"
            R"(<CALIBRATION gainDb="7.0" backendName="ASIO" inputDeviceName="Interface A" )"
            R"(inputChannelIndex="0"/>)"
            R"(</INPUT_CALIBRATIONS>)"
        });

    const AudioConfigStore store{shared};
    const auto stored = inputCalibrationFor(store, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(7.0, 0));
    }
}

// Every gain takes the calibration normal form on the way into the store: clamped to the
// supported range and rounded to the step.
TEST_CASE("AudioConfigStore keeps calibration gains in normal form", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_clamp_gain.settings"};
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore store{shared};

    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 100.0)).has_value());
    const auto clamped = inputCalibrationFor(store, identity);
    REQUIRE(clamped.has_value());
    if (clamped.has_value())
    {
        CHECK_THAT(clamped->calibration_gain.db, Catch::Matchers::WithinULP(maximumGainDb(), 0));
    }

    REQUIRE(store.saveInputCalibration(calibrationFor(identity, 3.14)).has_value());
    const auto rounded = inputCalibrationFor(store, identity);
    REQUIRE(rounded.has_value());
    if (rounded.has_value())
    {
        CHECK_THAT(rounded->calibration_gain.db, Catch::Matchers::WithinULP(3.1, 0));
    }
}

// Malformed calibration XML surfaces as a typed error rather than silent absence, and a save does
// not overwrite the unreadable state.
TEST_CASE("AudioConfigStore preserves malformed calibration history", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_malformed_calibration.settings"};
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    writeRawSetting(shared, g_input_calibration_states_key, juce::String{"[not-xml"});

    AudioConfigStore store{shared};

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
    common::core::SharedSettingsFile shared{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    const std::string route{"<DEVICESETUP deviceType=\"ASIO\"/>"};

    {
        AudioConfigStore store{shared};
        REQUIRE(store.setActiveDeviceRoute(route).has_value());
        REQUIRE(store.saveInputCalibration(calibrationFor(identity, 9.0)).has_value());
    }

    const AudioConfigStore reloaded{shared};
    CHECK(reloaded.activeDeviceRoute() == std::optional{route});
    const auto stored = inputCalibrationFor(reloaded, identity);
    REQUIRE(stored.has_value());
    if (stored.has_value())
    {
        CHECK_THAT(stored->calibration_gain.db, Catch::Matchers::WithinULP(9.0, 0));
    }
}

// Each product opens the shared file in its own process, so a store never serves a stale copy: a
// write through one is visible to a store opened before it.
TEST_CASE("AudioConfigStore reads another store's write", "[audio][config-store]")
{
    const ScopedSettingsFile settings_file{"config_store_fresh_read.settings"};
    common::core::SharedSettingsFile editor_file{settings_file.path()};
    common::core::SharedSettingsFile game_file{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore editor{editor_file};
    const AudioConfigStore game{game_file};

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
    common::core::SharedSettingsFile editor_file{settings_file.path()};
    common::core::SharedSettingsFile game_file{settings_file.path()};
    const InputDeviceIdentity identity = makeInputDeviceIdentity();
    AudioConfigStore editor{editor_file};
    AudioConfigStore game{game_file};

    REQUIRE(editor.saveInputCalibration(calibrationFor(identity, 3.0)).has_value());
    REQUIRE(game.setActiveDeviceRoute("<DEVICESETUP/>").has_value());

    CHECK(editor.activeDeviceRoute().has_value());
    CHECK(inputCalibrationFor(editor, identity).has_value());
}

} // namespace rock_hero::common::audio
