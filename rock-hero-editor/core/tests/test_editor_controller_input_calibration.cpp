#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <optional>
#include <ostream>
#include <rock_hero/common/audio/testing/fake_live_input.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <variant>

namespace rock_hero::editor::core
{

namespace
{

// Reads calibration through the app's audio-config store and returns the optional payload.
[[nodiscard]] std::optional<common::audio::InputCalibrationState> inputCalibrationFor(
    const common::audio::testing::InMemoryAudioConfigStore& store,
    const common::audio::InputDeviceIdentity& identity)
{
    auto result = store.inputCalibrationFor(identity);
    REQUIRE(result.has_value());
    return std::move(*result);
}

// Stores calibration through the app's audio-config store.
void requireSaveInputCalibration(
    common::audio::testing::InMemoryAudioConfigStore& store,
    common::audio::InputCalibrationState calibration)
{
    // Move outside the assertion macro: REQUIRE re-mentions its expression textually, which
    // bugprone-use-after-move reads as a use of the moved-from calibration.
    const auto saved = store.saveInputCalibration(std::move(calibration));
    REQUIRE(saved.has_value());
}

// Samples the calibration prompt at a steady raw level until its measurement ends, as the prompt's
// timer would. A steady level L calibrates to the target peak less L.
template <typename LiveInput>
[[nodiscard]] common::audio::InputCalibrationProgress runCalibrationMeasurement(
    EditorController& controller, LiveInput& live_input, double peak_db)
{
    live_input.raw_input_meter_level = common::audio::AudioMeterLevel{.peak_db = peak_db};
    constexpr std::size_t longest_measurement = common::audio::inputCalibrationSettleSampleCount() +
                                                common::audio::inputCalibrationListenSampleCount();
    for (std::size_t sample = 0; sample < longest_measurement; ++sample)
    {
        const common::audio::LiveInputSample reading = controller.onInputCalibrationSampled();
        if (!reading.measurement.has_value())
        {
            return common::audio::InputCalibrationFailed{"The measurement was not running."};
        }
        if (!std::holds_alternative<common::audio::InputCalibrationRunning>(*reading.measurement))
        {
            return *reading.measurement;
        }
    }
    return common::audio::InputCalibrationFailed{"The measurement did not finish."};
}

// Runs a measurement to its end and, when it measured a gain, applies that gain as the prompt's
// Apply would, since a measurement only reports its gain.
template <typename LiveInput>
[[nodiscard]] common::audio::InputCalibrationProgress measureAndApply(
    EditorController& controller, LiveInput& live_input, double peak_db)
{
    common::audio::InputCalibrationProgress progress =
        runCalibrationMeasurement(controller, live_input, peak_db);
    if (const auto* const measured =
            std::get_if<common::audio::InputCalibrationMeasured>(&progress))
    {
        const auto applied = controller.onInputCalibrationApplied(measured->gain.db);
        REQUIRE(applied.has_value());
    }
    return progress;
}

} // namespace

// Verifies that the no-device disabled message takes priority over missing calibration.
TEST_CASE(
    "Signal chain reports no input device before missing calibration", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);
    CHECK_FALSE(final_state->signal_chain.input_calibrate_enabled);
    CHECK(final_state->signal_chain.disabled_message == "No audio input device.");
}

// Calibration is device-level, not project-level: with an input route up it is available with no
// project loaded, and the prompt opens on request, so a fresh editor can calibrate before ever
// opening a song.
TEST_CASE("Input calibration is available without a loaded project", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction()
    };
    controller.attachView(view);

    const auto* const initial_state = stateOrNull(view.last_state);
    REQUIRE(initial_state != nullptr);
    CHECK(initial_state->signal_chain.input_calibrate_enabled);

    controller.onInputCalibrationRequested();

    const auto* const prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(prompt_state->input_calibration_prompt.has_value());
}

// An uncalibrated route is offered calibration by itself, once: the prompt opens at startup and
// pauses playback, "Later" keeps it closed for that route, and the user can still ask for it.
TEST_CASE("Missing input calibration offers the prompt once per route", "[core][editor-controller]")
{
    FakeTransport transport;
    transport.current_state.playing = true;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    const auto* const offered_state = stateOrNull(view.last_state);
    REQUIRE(offered_state != nullptr);
    CHECK(offered_state->input_calibration_prompt.has_value());
    CHECK_FALSE(offered_state->audio_device_settings_enabled);
    CHECK(transport.pause_call_count == 1);

    controller.onInputCalibrationClosed();
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    // Later holds across the project load: the same route is not offered again.
    const auto* const gated_state = stateOrNull(view.last_state);
    REQUIRE(gated_state != nullptr);
    CHECK_FALSE(gated_state->input_calibration_prompt.has_value());
    CHECK(gated_state->transport.unavailable_reason == std::nullopt);
    CHECK(gated_state->audio_device_settings_enabled);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(gated_state->signal_chain.disabled_message == "Input calibration required.");

    controller.onInputCalibrationRequested();
    CHECK(transport.pause_call_count == 2);
    const auto* const prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(prompt_state->input_calibration_prompt.has_value());
}

// A route change offers the new route calibration even after "Later" on the old one.
TEST_CASE("Missing input calibration offers a new route again", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
    };
    controller.attachView(view);
    controller.onInputCalibrationClosed();

    audio_devices.current_input_identity = makeInputDeviceIdentity("ASIO", "Interface B");
    audio_devices.notifyChanged();

    const auto* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->input_calibration_prompt.has_value());
    if (state->input_calibration_prompt.has_value())
    {
        CHECK(
            state->input_calibration_prompt->route ==
            makeInputDeviceIdentity("ASIO", "Interface B"));
    }
}

// A calibrated route is never offered.
TEST_CASE("Calibrated input route is not offered calibration", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{3.1},
            .input_device_identity = makeInputDeviceIdentity(),
        });
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
    };
    controller.attachView(view);

    const auto* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    CHECK_FALSE(state->input_calibration_prompt.has_value());
}

// Verifies that a successful calibration is stored in app-local settings and enables live input.
TEST_CASE(
    "Input calibration success stores app-local gain and enables monitoring",
    "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());
    CHECK(transport.set_live_input_monitoring_call_count >= 1);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(0.0, 0));

    REQUIRE(
        std::holds_alternative<common::audio::InputCalibrationMeasured>(
            measureAndApply(controller, transport, -19.5)));

    // A stored gain is what the prompt was for, so Apply ends it.
    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(final_state->signal_chain.disabled_message.empty());
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(12.7, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    REQUIRE(audio_devices.current_input_identity.has_value());
    const auto stored_calibration =
        inputCalibrationFor(store, *audio_devices.current_input_identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK_THAT(stored_calibration->calibration_gain.db, Catch::Matchers::WithinULP(12.7, 0));
        CHECK(stored_calibration->input_device_identity == *audio_devices.current_input_identity);
    }
}

// Verifies a retry, from calibration reopened after an Apply, starts from a neutral measurement
// gain.
TEST_CASE(
    "Input calibration retry resets committed gain before measuring", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    const auto first_measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(first_measurement_started.has_value());
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(0.0, 0));

    REQUIRE(
        std::holds_alternative<common::audio::InputCalibrationMeasured>(
            measureAndApply(controller, transport, -19.5)));
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(12.7, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto* const applied_state = stateOrNull(view.last_state);
    REQUIRE(applied_state != nullptr);
    CHECK_FALSE(applied_state->input_calibration_prompt.has_value());

    controller.onInputCalibrationRequested();
    const auto retry_measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(retry_measurement_started.has_value());
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(0.0, 0));
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);

    controller.onInputCalibrationClosed();

    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(12.7, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// Stopping a measurement hands the route back to the gate and keeps the prompt open for a retry,
// with nothing stored and nothing reported as interrupted at the next sample.
TEST_CASE("Input calibration stop keeps the prompt open", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker)
                .has_value());
    REQUIRE(transport.calibration_input_monitoring_enabled);

    controller.onInputCalibrationMeasurementStopped();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->input_calibration_prompt.has_value());
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    REQUIRE(audio_devices.current_input_identity.has_value());
    CHECK_FALSE(inputCalibrationFor(store, *audio_devices.current_input_identity).has_value());
    CHECK_FALSE(controller.onInputCalibrationSampled().measurement.has_value());
}

// A refused gain reset hands the route back to the gate, which re-arms the stored calibration.
TEST_CASE("Input calibration start restores route on gain failure", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = makeInputDeviceIdentity(),
        });
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    REQUIRE(transport.live_input_monitoring_enabled);
    transport.next_set_input_gain_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::CouldNotSetInputGain,
        "gain reset failed",
    };

    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);

    REQUIRE_FALSE(measurement_started.has_value());
    CHECK(
        measurement_started.error().code ==
        common::audio::LiveInputMonitorErrorCode::BackendRejected);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// A refused calibration path hands the route back to the gate, which re-arms the stored
// calibration.
TEST_CASE("Input calibration start restores route on monitor failure", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = makeInputDeviceIdentity(),
        });
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    REQUIRE(transport.live_input_monitoring_enabled);
    transport.next_set_calibration_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::CouldNotSetMonitoring,
        "calibration monitoring failed",
    };

    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);

    REQUIRE_FALSE(measurement_started.has_value());
    CHECK(
        measurement_started.error().code ==
        common::audio::LiveInputMonitorErrorCode::BackendRejected);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// Verifies that knowledgeable users can save a calibrated input gain without measurement.
TEST_CASE(
    "Manual input calibration stores gain and enables monitoring", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onInputCalibrationRequested();
    const auto calibration_set = controller.onInputCalibrationApplied(3.25);
    REQUIRE(calibration_set.has_value());

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(final_state->signal_chain.disabled_message.empty());
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(3.25, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    REQUIRE(audio_devices.current_input_identity.has_value());
    const auto stored_calibration =
        inputCalibrationFor(store, *audio_devices.current_input_identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK_THAT(stored_calibration->calibration_gain.db, Catch::Matchers::WithinULP(3.25, 0));
        CHECK(stored_calibration->input_device_identity == *audio_devices.current_input_identity);
    }
}

// Verifies settings editing releases the calibrated route before JUCE closes the active device.
TEST_CASE("Audio settings open releases calibrated input route", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = common::audio::InputDeviceIdentity{
        .backend_name = "Windows Audio",
        .input_device_name = "WASAPI Interface",
        .input_channel_index = 0,
    };
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationApplied(3.25).has_value());
    controller.onInputCalibrationClosed();
    REQUIRE(transport.live_input_monitoring_enabled);
    REQUIRE_FALSE(transport.calibration_input_monitoring_enabled);
    const auto* const enabled_state = stateOrNull(view.last_state);
    REQUIRE(enabled_state != nullptr);
    REQUIRE_FALSE(enabled_state->input_calibration_prompt.has_value());
    REQUIRE(enabled_state->audio_device_settings_enabled);

    controller.onAudioDeviceSettingsOpenRequested();

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    CHECK(inputCalibrationFor(store, *audio_devices.current_input_identity).has_value());
    const auto* const settings_open_state = stateOrNull(view.last_state);
    REQUIRE(settings_open_state != nullptr);
    CHECK(
        settings_open_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
    CHECK_FALSE(settings_open_state->audio_device_settings_enabled);

    audio_devices.notifyChanged();

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    controller.onAudioDeviceSettingsClosed();

    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto* const settings_closed_state = stateOrNull(view.last_state);
    REQUIRE(settings_closed_state != nullptr);
    CHECK(settings_closed_state->audio_device_settings_enabled);
}

// Verifies settings close does not treat JUCE's temporary closed route as a device change.
TEST_CASE("Audio settings close waits for settled input route", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(transport.live_input_monitoring_enabled);

    controller.onAudioDeviceSettingsOpenRequested();
    audio_devices.current_input_identity = std::nullopt;
    controller.onAudioDeviceSettingsClosed();

    CHECK(inputCalibrationFor(store, identity).has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    const auto* const no_device_state = stateOrNull(view.last_state);
    REQUIRE(no_device_state != nullptr);
    CHECK(
        no_device_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);

    audio_devices.current_input_identity = identity;
    audio_devices.notifyChanged();

    CHECK(inputCalibrationFor(store, identity).has_value());
    CHECK(transport.live_input_monitoring_enabled);
    const auto* const reconnected_state = stateOrNull(view.last_state);
    REQUIRE(reconnected_state != nullptr);
    CHECK(
        reconnected_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
}

// Verifies startup with a disconnected calibrated device keeps calibration for reconnect.
TEST_CASE("Stored input calibration waits for disconnected device", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(inputCalibrationFor(store, identity).has_value());
    const auto* const disconnected_state = stateOrNull(view.last_state);
    REQUIRE(disconnected_state != nullptr);
    CHECK(
        disconnected_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);

    audio_devices.current_input_identity = identity;
    audio_devices.notifyChanged();

    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    REQUIRE(inputCalibrationFor(store, identity).has_value());
    const auto* const restored_state = stateOrNull(view.last_state);
    REQUIRE(restored_state != nullptr);
    CHECK(
        restored_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
}

// Verifies saved calibration does not route live input before the project load fully commits.
TEST_CASE(
    "Stored input calibration stays disabled until live rig load completes",
    "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.defer_load_completion = true;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    CHECK_FALSE(transport.live_input_monitoring_enabled);

    audio.next_prepared_audio_duration = loadedTimelineRange().duration();
    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});

    REQUIRE(controller.session().currentArrangement() != nullptr);
    CHECK_FALSE(transport.live_input_monitoring_enabled);

    REQUIRE(live_rig.completePendingLoad());

    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
}

// Verifies backend arming failure does not erase calibration for the unchanged input route.
TEST_CASE("Input calibration reports backend unavailable", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(transport.live_input_monitoring_enabled);

    transport.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(inputCalibrationFor(store, identity).has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(final_state->signal_chain.disabled_message == "Live input backend unavailable.");
}

// Verifies temporary input route loss keeps calibration for a matching reconnect.
TEST_CASE("Input disconnect preserves calibration for same reconnect", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(transport.live_input_monitoring_enabled);

    audio_devices.current_input_identity = std::nullopt;
    audio_devices.notifyChanged();

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(inputCalibrationFor(store, identity).has_value());
    const auto* const disconnected_state = stateOrNull(view.last_state);
    REQUIRE(disconnected_state != nullptr);
    CHECK(
        disconnected_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);

    audio_devices.current_input_identity = identity;
    audio_devices.notifyChanged();

    CHECK(transport.live_input_monitoring_enabled);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    const auto preserved_calibration = inputCalibrationFor(store, identity);
    REQUIRE(preserved_calibration.has_value());
    if (preserved_calibration.has_value())
    {
        CHECK_THAT(preserved_calibration->calibration_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    }
    const auto* const restored_state = stateOrNull(view.last_state);
    REQUIRE(restored_state != nullptr);
    CHECK(
        restored_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
}

// Verifies route changes preserve prior calibration history while gating an unsaved route.
TEST_CASE("Input route change preserves previous calibration history", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity initial_identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = initial_identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = initial_identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(transport.live_input_monitoring_enabled);

    audio_devices.current_input_identity = makeInputDeviceIdentity("ASIO", "Interface B");
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(inputCalibrationFor(store, initial_identity).has_value());
    REQUIRE(audio_devices.current_input_identity.has_value());
    if (audio_devices.current_input_identity.has_value())
    {
        CHECK_FALSE(inputCalibrationFor(store, *audio_devices.current_input_identity).has_value());
    }
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    // The new, uncalibrated route is offered calibration.
    CHECK(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
    CHECK(final_state->signal_chain.disabled_message == "Input calibration required.");
}

// Verifies a saved calibration for the new physical route is applied after a route switch.
TEST_CASE("Input route change applies saved route calibration", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity initial_identity = makeInputDeviceIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeInputDeviceIdentity("ASIO", "Interface B");
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = initial_identity,
        });
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{8.0},
            .input_device_identity = next_identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = initial_identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    audio_devices.current_input_identity = next_identity;
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(8.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK(inputCalibrationFor(store, initial_identity).has_value());
    CHECK(inputCalibrationFor(store, next_identity).has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
}

// Verifies switching away and back restores the original physical-route calibration.
TEST_CASE("Input route change restores saved calibration on return", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity initial_identity = makeInputDeviceIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeInputDeviceIdentity("ASIO", "Interface B");
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = initial_identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = initial_identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    audio_devices.current_input_identity = next_identity;
    audio_devices.notifyChanged();
    REQUIRE_FALSE(transport.live_input_monitoring_enabled);

    audio_devices.current_input_identity = initial_identity;
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    REQUIRE(inputCalibrationFor(store, initial_identity).has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
}

// Verifies a different physical input channel is treated as a different route.
TEST_CASE("Input route channel change requires its own calibration", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity channel_one =
        makeInputDeviceIdentity("ASIO", "Interface A", 0);
    const common::audio::InputDeviceIdentity channel_three =
        makeInputDeviceIdentity("ASIO", "Interface A", 2);
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = channel_one,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = channel_one;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    audio_devices.current_input_identity = channel_three;
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(inputCalibrationFor(store, channel_one).has_value());
    CHECK_FALSE(inputCalibrationFor(store, channel_three).has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
}

// Verifies settings close selects a newly chosen concrete route even while the window was open.
TEST_CASE("Audio settings close applies saved replacement route", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity initial_identity = makeInputDeviceIdentity();
    const common::audio::InputDeviceIdentity next_identity =
        makeInputDeviceIdentity("ASIO", "Interface B");
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = initial_identity,
        });
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{8.0},
            .input_device_identity = next_identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = initial_identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onAudioDeviceSettingsOpenRequested();
    audio_devices.current_input_identity = next_identity;
    controller.onAudioDeviceSettingsClosed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(8.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
}

// Verifies a route change during measurement closes the prompt and leaves monitoring disabled.
TEST_CASE("Input route change during calibration closes prompt", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto* const prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    REQUIRE(prompt_state->input_calibration_prompt.has_value());

    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());
    CHECK(transport.calibration_input_monitoring_enabled);

    audio_devices.current_input_identity = makeInputDeviceIdentity("ASIO", "Interface B");
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    // The old route's prompt closed; the new, uncalibrated route is offered its own.
    REQUIRE(final_state->input_calibration_prompt.has_value());
    if (final_state->input_calibration_prompt.has_value())
    {
        CHECK(final_state->input_calibration_prompt->route == audio_devices.current_input_identity);
    }
    REQUIRE(audio_devices.current_input_identity.has_value());
    if (audio_devices.current_input_identity.has_value())
    {
        CHECK_FALSE(inputCalibrationFor(store, *audio_devices.current_input_identity).has_value());
    }
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
    CHECK(final_state->signal_chain.disabled_message == "Input calibration required.");
}

// Verifies that dismissing manual recalibration restores the previous matching calibration.
TEST_CASE(
    "Manual input recalibration dismissal restores previous calibration",
    "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(transport.live_input_monitoring_enabled);

    controller.onInputCalibrationRequested();
    const auto* const prompt_state = stateOrNull(view.last_state);
    REQUIRE(prompt_state != nullptr);
    CHECK(prompt_state->input_calibration_prompt.has_value());

    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(0.0, 0));

    controller.onInputCalibrationClosed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto restored_calibration = inputCalibrationFor(store, identity);
    REQUIRE(restored_calibration.has_value());
    if (restored_calibration.has_value())
    {
        CHECK_THAT(restored_calibration->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
}

// Dismissing a recalibration in progress restores the stored calibration and its monitoring.
TEST_CASE(
    "Input recalibration dismissal restores previous calibration", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());

    controller.onInputCalibrationClosed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto restored_calibration = inputCalibrationFor(store, identity);
    REQUIRE(restored_calibration.has_value());
    if (restored_calibration.has_value())
    {
        CHECK_THAT(restored_calibration->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
}

// A dismissal whose re-arm the backend refuses keeps the stored calibration and reports the route
// as unavailable.
TEST_CASE(
    "Input recalibration dismissal preserves calibration on backend failure",
    "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());

    transport.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    controller.onInputCalibrationClosed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(final_state->signal_chain.disabled_message == "Live input backend unavailable.");
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto preserved_calibration = inputCalibrationFor(store, identity);
    REQUIRE(preserved_calibration.has_value());
    if (preserved_calibration.has_value())
    {
        CHECK_THAT(preserved_calibration->calibration_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    }
}

// A recalibration is a fact about the route: it is stored even when the backend then refuses to
// arm the route, and the refusal is reported.
TEST_CASE(
    "Input recalibration commit stores the new gain on backend failure",
    "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);
    REQUIRE(measurement_started.has_value());

    const common::audio::InputCalibrationProgress progress =
        runCalibrationMeasurement(controller, transport, -18.0);
    const auto* const measured = std::get_if<common::audio::InputCalibrationMeasured>(&progress);
    REQUIRE(measured != nullptr);
    const double measured_gain_db = measured->gain.db;
    transport.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };

    // Saved is committed: refusing to arm the route is gate state, not a failed store.
    const auto applied = controller.onInputCalibrationApplied(measured_gain_db);
    CHECK(applied.has_value());

    controller.onInputCalibrationClosed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(final_state->signal_chain.disabled_message == "Live input backend unavailable.");
    CHECK_THAT(transport.current_input_gain.db, Catch::Matchers::WithinULP(11.2, 0));
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto stored_calibration = inputCalibrationFor(store, identity);
    REQUIRE(stored_calibration.has_value());
    if (stored_calibration.has_value())
    {
        CHECK_THAT(stored_calibration->calibration_gain.db, Catch::Matchers::WithinULP(11.2, 0));
    }
}

namespace
{

using common::audio::testing::FakeLiveInput;
using common::audio::testing::LiveInputSetterCall;
using common::audio::testing::setCalibrationInputMonitoringCall;
using common::audio::testing::setInputGainCall;
using common::audio::testing::setLiveInputMonitoringCall;

// Builds a controller audio-port bundle with a specific live rig (the stock five-arg overload also
// needs a plugin host, which these live-input trace tests do not). The live-input port the gate
// drives is not an audio port: the tests inject an ordered-recording FakeLiveInput through
// their own LiveInputMonitor so the exact ILiveInput setter trace stays observable.
[[nodiscard]] EditorController::AudioPorts audioPortsForLiveInputTrace(
    FakeTransport& transport, ConfigurableSongAudio& song_audio,
    ConfigurableAudioDeviceConfiguration& audio_devices, FakeLiveRig& live_rig)
{
    return EditorController::AudioPorts{
        .transport = transport,
        .song_audio = song_audio,
        .audio_devices = audio_devices,
        .plugin_host = defaultPluginHost(),
        .live_rig = live_rig,
        .tone_timeline = defaultToneTimeline(),
        .tone_automation = defaultToneAutomation(),
    };
}

// Settled calibration view-state slice pinned after each operation. Store-agnostic and independent
// of which live-input port implementation the controller drives, so it survives later relocations.
struct SettledCalibrationState
{
    InputCalibrationStatus status{};
    std::string disabled_message{};
    bool prompt_present{false};

    friend bool operator==(const SettledCalibrationState&, const SettledCalibrationState&) =
        default;
};

// Renders a settled slice so Catch2 prints legible mismatches.
std::ostream& operator<<(std::ostream& stream, const SettledCalibrationState& state)
{
    return stream << "{status=" << static_cast<int>(state.status) << ", message=\""
                  << state.disabled_message
                  << "\", prompt=" << (state.prompt_present ? "yes" : "no") << "}";
}

// Extracts the settled calibration slice from the last pushed view-state.
[[nodiscard]] SettledCalibrationState settledCalibrationState(const FakeEditorView& view)
{
    const EditorViewState* const state = stateOrNull(view.last_state);
    REQUIRE(state != nullptr);
    if (state == nullptr)
    {
        return {};
    }

    return SettledCalibrationState{
        .status = state->signal_chain.input_calibration_status,
        .disabled_message = state->signal_chain.disabled_message,
        .prompt_present = state->input_calibration_prompt.has_value(),
    };
}

} // namespace

// Pins the exact ILiveInput setter sequence for the canonical open-calibrate-commit-close arc. This
// trace touches only ILiveInput, so it is store- and error-type-agnostic: moving the calibration
// state or its error type anywhere must leave it unchanged.
TEST_CASE("Live input golden trace spans calibration arc", "[core][editor-controller]")
{
    FakeTransport transport;
    transport.current_state.playing = true;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    controller.onInputCalibrationClosed();
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::MissingCalibration,
                                             .disabled_message = "Input calibration required.",
                                             .prompt_present = false,
                                         });

    // Begin the arc; the trace is captured from the prompt-open request onward.
    live_input.calls.clear();
    const int pauses_before = transport.pause_call_count;

    controller.onInputCalibrationRequested();
    CHECK(transport.pause_call_count == pauses_before + 1);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::MissingCalibration,
                                             .disabled_message = "Input calibration required.",
                                             .prompt_present = true,
                                         });

    REQUIRE(controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker)
                .has_value());
    REQUIRE(
        std::holds_alternative<common::audio::InputCalibrationMeasured>(
            measureAndApply(controller, live_input, -19.5)));
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });

    controller.onInputCalibrationClosed();
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });

    const std::vector<LiveInputSetterCall> golden_trace{
        // onInputCalibrationRequested: the gate re-reads the store; the route is uncalibrated.
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
        // onInputCalibrationMeasurementStarted: disable live, reset
        // gain, enable calibration audition.
        setLiveInputMonitoringCall(false),
        setInputGainCall(0.0),
        setCalibrationInputMonitoringCall(true),
        // The measurement finishing hands the still-uncalibrated route back to the gate.
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
        // Apply: the stored gain arms the route.
        setCalibrationInputMonitoringCall(false),
        setInputGainCall(12.7),
        setLiveInputMonitoringCall(true),
        // onInputCalibrationClosed: no setters (the measurement already ended).
    };
    CHECK(live_input.calls == golden_trace);
}

// Pins the gate's arm-on-match order for a matching calibrated route: calibration audition off,
// then gain, then live monitoring on. Captured over the project-lifecycle gate (no store re-read).
TEST_CASE("Live input gate arms matching route in order", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    live_rig.defer_load_completion = true;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    audio.next_prepared_audio_duration = loadedTimelineRange().duration();
    project_services.next_song = makeSong(std::filesystem::path{"song.wav"});
    controller.onOpenRequested(std::filesystem::path{"loaded.rhp"});

    // Project audio is not ready until the deferred live-rig load completes: monitoring stays off,
    // yet the derived status already reports Calibrated with an empty message.
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });

    live_input.calls.clear();
    REQUIRE(live_rig.completePendingLoad());

    // The completion runs the gate twice: a not-ready disable pass, then the arm pass once project
    // audio is ready. The arm-on-match order is the trailing three calls: calibration audition off,
    // then gain, then live monitoring on.
    const std::vector<LiveInputSetterCall> arm_order{
        setCalibrationInputMonitoringCall(false),
        setInputGainCall(5.0),
        setLiveInputMonitoringCall(true),
    };
    REQUIRE(live_input.calls.size() >= arm_order.size());
    const std::vector<LiveInputSetterCall> arm_tail(
        live_input.calls.end() - static_cast<std::ptrdiff_t>(arm_order.size()),
        live_input.calls.end());
    CHECK(arm_tail == arm_order);
    CHECK(live_input.live_input_monitoring_enabled);
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(5.0, 0));
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });
}

// A measurement start refused at its first step hands the uncalibrated route back to the gate,
// which keeps it silent; the prompt stays open to show the error.
TEST_CASE("Live input start rollback on disable failure", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    live_input.current_input_gain = common::audio::Gain{4.0};
    live_input.live_input_monitoring_enabled = false;
    live_input.calibration_input_monitoring_enabled = false;
    live_input.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    live_input.calls.clear();

    const auto started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);

    REQUIRE_FALSE(started.has_value());
    const std::vector<LiveInputSetterCall> trace{
        setLiveInputMonitoringCall(false),
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::MissingCalibration,
                                             .disabled_message = "Input calibration required.",
                                             .prompt_present = true,
                                         });
}

// A measurement start refused at the gain reset hands the uncalibrated route back to the gate.
TEST_CASE("Live input start rollback on gain failure", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    live_input.current_input_gain = common::audio::Gain{4.0};
    live_input.live_input_monitoring_enabled = false;
    live_input.calibration_input_monitoring_enabled = false;
    live_input.next_set_input_gain_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "gain reset failed",
    };
    live_input.calls.clear();

    const auto started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);

    REQUIRE_FALSE(started.has_value());
    const std::vector<LiveInputSetterCall> trace{
        setLiveInputMonitoringCall(false),
        setInputGainCall(0.0),
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
}

// A measurement start refused at the calibration path hands the uncalibrated route back to the
// gate.
TEST_CASE("Live input start rollback on audition failure", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    live_input.current_input_gain = common::audio::Gain{4.0};
    live_input.live_input_monitoring_enabled = false;
    live_input.calibration_input_monitoring_enabled = false;
    live_input.next_set_calibration_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "calibration monitoring failed",
    };
    live_input.calls.clear();

    const auto started =
        controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker);

    REQUIRE_FALSE(started.has_value());
    const std::vector<LiveInputSetterCall> trace{
        setLiveInputMonitoringCall(false),
        setInputGainCall(0.0),
        setCalibrationInputMonitoringCall(true),
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
}

// A commit whose gain the backend refuses is stored, then the gate reports the refused route.
TEST_CASE("Live input commit reports a refused gain", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker)
                .has_value());

    const common::audio::InputCalibrationProgress progress =
        runCalibrationMeasurement(controller, live_input, -18.0);
    const auto* const measured = std::get_if<common::audio::InputCalibrationMeasured>(&progress);
    REQUIRE(measured != nullptr);
    const double measured_gain_db = measured->gain.db;
    live_input.next_set_input_gain_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    live_input.calls.clear();

    // Saved is committed: refusing to arm the route is gate state, not a failed store.
    const auto applied = controller.onInputCalibrationApplied(measured_gain_db);
    CHECK(applied.has_value());
    const std::vector<LiveInputSetterCall> trace{
        setCalibrationInputMonitoringCall(false),
        setInputGainCall(11.2),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Unavailable,
                                             .disabled_message = "Live input backend unavailable.",
                                             .prompt_present = false,
                                         });
}

// A commit whose route the backend refuses to monitor is stored at the new gain, with monitoring
// left off.
TEST_CASE("Live input commit reports refused monitoring", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker)
                .has_value());

    const common::audio::InputCalibrationProgress progress =
        runCalibrationMeasurement(controller, live_input, -18.0);
    const auto* const measured = std::get_if<common::audio::InputCalibrationMeasured>(&progress);
    REQUIRE(measured != nullptr);
    const double measured_gain_db = measured->gain.db;
    live_input.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    live_input.calls.clear();

    // Saved is committed: refusing to arm the route is gate state, not a failed store.
    const auto applied = controller.onInputCalibrationApplied(measured_gain_db);
    CHECK(applied.has_value());
    const std::vector<LiveInputSetterCall> trace{
        setCalibrationInputMonitoringCall(false),
        setInputGainCall(11.2),
        setLiveInputMonitoringCall(true),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(11.2, 0));
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Unavailable,
                                             .disabled_message = "Live input backend unavailable.",
                                             .prompt_present = false,
                                         });
}

// Pins the dismissal path: an active measurement over a matching calibrated route restores the
// prior calibration (audition off, gain, monitoring on) and closes the prompt.
TEST_CASE("Live input dismissal restores previous calibration", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{4.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationMeasurementStarted(common::audio::PickupClass::Humbucker)
                .has_value());

    live_input.calls.clear();
    controller.onInputCalibrationClosed();

    const std::vector<LiveInputSetterCall> trace{
        setCalibrationInputMonitoringCall(false),
        setInputGainCall(4.0),
        setLiveInputMonitoringCall(true),
    };
    CHECK(live_input.calls == trace);
    CHECK(live_input.live_input_monitoring_enabled);
    CHECK_THAT(live_input.current_input_gain.db, Catch::Matchers::WithinULP(4.0, 0));
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });
}

// Pins the device-change re-gate to no device: the select teardown and the gate's no-device branch
// both disable, and the controller pushes the settled no-device view-state.
TEST_CASE("Live input device change to none re-gates view", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(live_input.live_input_monitoring_enabled);

    const int pushes_before = view.set_state_call_count;
    live_input.calls.clear();
    audio_devices.current_input_identity = std::nullopt;
    audio_devices.notifyChanged();

    // One gate run: the calibration path off, then the no-device branch disable.
    const std::vector<LiveInputSetterCall> trace{
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
    CHECK(view.set_state_call_count > pushes_before);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::NoActiveInputDevice,
                                             .disabled_message = "No audio input device.",
                                             .prompt_present = false,
                                         });
}

// Pins the device-change re-gate onto an uncalibrated route: same disable trace as the no-device
// case, but the settled state distinguishes it as missing calibration.
TEST_CASE("Live input device change to uncalibrated re-gates", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(live_input.live_input_monitoring_enabled);

    live_input.calls.clear();
    audio_devices.current_input_identity = makeInputDeviceIdentity("ASIO", "Interface B");
    audio_devices.notifyChanged();

    const std::vector<LiveInputSetterCall> trace{
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::MissingCalibration,
                                             .disabled_message = "Input calibration required.",
                                             .prompt_present = true,
                                         });
}

// Pins the gate's audio-device-settings-open branch: a configuration change while the settings
// window is open disables monitoring but leaves the calibrated status intact with an empty message.
TEST_CASE("Live input gate disables while settings open", "[core][editor-controller]")
{
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    requireSaveInputCalibration(
        store,
        common::audio::InputCalibrationState{
            .calibration_gain = common::audio::Gain{5.0},
            .input_device_identity = identity,
        });

    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = identity;
    FakeLiveRig live_rig;
    FakeLiveInput live_input;
    FakeProjectServices project_services;
    FakeEditorView view;
    common::audio::LiveInputMonitor monitor{live_input, audio_devices, store};
    EditorController controller{
        audioPortsForLiveInputTrace(transport, audio, audio_devices, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onAudioDeviceSettingsOpenRequested();

    live_input.calls.clear();
    audio_devices.notifyChanged();

    const std::vector<LiveInputSetterCall> trace{
        // Same physical route: select emits no effects. Gate: preamble
        // disable, settings-open disable.
        setCalibrationInputMonitoringCall(false),
        setLiveInputMonitoringCall(false),
    };
    CHECK(live_input.calls == trace);
    CHECK_FALSE(live_input.live_input_monitoring_enabled);
    CHECK(
        settledCalibrationState(view) == SettledCalibrationState{
                                             .status = InputCalibrationStatus::Calibrated,
                                             .disabled_message = {},
                                             .prompt_present = false,
                                         });
}

// The settings window holds the route, so a route it stages is not offered calibration until the
// window closes; closing on an uncalibrated route then offers it.
TEST_CASE(
    "Settings window holds the calibration offer until it closes", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeEditorView view;
    common::audio::testing::InMemoryAudioConfigStore store = savedRouteAudioConfigStore();
    common::audio::LiveInputMonitor monitor{transport, audio_devices, store};
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(nullEditorSettings(), store, monitor),
        noopExitFunction(),
    };
    controller.attachView(view);
    controller.onAudioDeviceSettingsOpenRequested();

    audio_devices.current_input_identity = makeInputDeviceIdentity();
    audio_devices.notifyChanged();
    const auto* const staging_state = stateOrNull(view.last_state);
    REQUIRE(staging_state != nullptr);
    CHECK_FALSE(staging_state->input_calibration_prompt.has_value());

    controller.onAudioDeviceSettingsClosed();
    const auto* const closed_state = stateOrNull(view.last_state);
    REQUIRE(closed_state != nullptr);
    CHECK(closed_state->input_calibration_prompt.has_value());
}

} // namespace rock_hero::editor::core
