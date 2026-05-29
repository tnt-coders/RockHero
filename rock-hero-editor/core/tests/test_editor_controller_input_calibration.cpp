#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
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
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: no audio input device selected.");
}

// Verifies that missing calibration disables the signal chain until calibration is requested.
TEST_CASE(
    "Missing input calibration disables live input until manually requested",
    "[core][editor-controller]")
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(view.last_state.has_value());
    CHECK(transport.pause_call_count == 0);
    CHECK_FALSE(view.last_state->input_calibration_prompt.has_value());
    CHECK(view.last_state->audio_device_settings_enabled);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    const auto* const gated_state = stateOrNull(view.last_state);
    REQUIRE(gated_state != nullptr);
    CHECK(gated_state->transport.play_pause_enabled);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(
        gated_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
    CHECK(
        gated_state->signal_chain.disabled_message ==
        "Live input disabled: input calibration required.");

    controller.onInputCalibrationRequested();
    CHECK(transport.pause_call_count == 1);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->input_calibration_prompt.has_value());
    CHECK_FALSE(view.last_state->audio_device_settings_enabled);

    controller.onInputCalibrationDismissed();

    const auto* const dismissed_state = stateOrNull(view.last_state);
    REQUIRE(dismissed_state != nullptr);
    CHECK_FALSE(dismissed_state->input_calibration_prompt.has_value());
    CHECK(dismissed_state->transport.play_pause_enabled);
    CHECK(dismissed_state->audio_device_settings_enabled);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
}

// Verifies that a successful calibration is stored in app-local settings and enables live input.
TEST_CASE(
    "Input calibration success stores app-local gain and enables monitoring",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_success"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());
    CHECK(transport.set_live_input_monitoring_call_count >= 1);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);
    CHECK(transport.current_input_gain.db == 0.0);

    const auto calibration_succeeded = controller.onInputCalibrationSucceeded(7.5);
    REQUIRE(calibration_succeeded.has_value());

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(final_state->signal_chain.disabled_message.empty());
    CHECK(transport.current_input_gain.db == 7.5);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    const auto stored_calibration = settings.inputCalibrationState();
    REQUIRE(stored_calibration.has_value());
    CHECK(stored_calibration->calibration_gain.db == 7.5);
    REQUIRE(audio_devices.current_input_identity.has_value());
    CHECK(stored_calibration->input_device_identity == *audio_devices.current_input_identity);
}

// Verifies retry starts from a neutral measurement gain after a completed prompt calibration.
TEST_CASE(
    "Input calibration retry resets committed gain before measuring", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_retry_reset"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    const auto first_measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(first_measurement_started.has_value());
    CHECK(transport.current_input_gain.db == 0.0);

    const auto first_calibration_succeeded = controller.onInputCalibrationSucceeded(7.5);
    REQUIRE(first_calibration_succeeded.has_value());
    CHECK(transport.current_input_gain.db == 7.5);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->input_calibration_prompt.has_value());

    const auto retry_measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(retry_measurement_started.has_value());
    CHECK(transport.current_input_gain.db == 0.0);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);

    controller.onInputCalibrationMeasurementCancelled();

    CHECK(transport.current_input_gain.db == 7.5);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// Verifies calibration setup failure while resetting gain restores the previous input route.
TEST_CASE("Input calibration start restores route on gain failure", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_gain_failure"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    transport.current_input_gain = common::audio::Gain{4.0};
    transport.live_input_monitoring_enabled = true;
    transport.calibration_input_monitoring_enabled = false;
    transport.next_set_input_gain_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::CouldNotSetInputGain,
        "gain reset failed",
    };

    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();

    REQUIRE_FALSE(measurement_started.has_value());
    CHECK(
        measurement_started.error().code ==
        common::audio::LiveInputErrorCode::CouldNotSetInputGain);
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// Verifies calibration setup failure while enabling monitor restores the previous input route.
TEST_CASE("Input calibration start restores route on monitor failure", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_monitor_failure"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();

    transport.current_input_gain = common::audio::Gain{4.0};
    transport.live_input_monitoring_enabled = true;
    transport.calibration_input_monitoring_enabled = false;
    transport.next_set_calibration_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::CouldNotSetMonitoring,
        "calibration monitoring failed",
    };

    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();

    REQUIRE_FALSE(measurement_started.has_value());
    CHECK(
        measurement_started.error().code ==
        common::audio::LiveInputErrorCode::CouldNotSetMonitoring);
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
}

// Verifies that knowledgeable users can save a calibrated input gain without measurement.
TEST_CASE(
    "Manual input calibration stores gain and enables monitoring", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_manual_set"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onInputCalibrationRequested();
    const auto calibration_set = controller.onInputCalibrationManuallySet(3.25);
    REQUIRE(calibration_set.has_value());

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(final_state->signal_chain.disabled_message.empty());
    CHECK(transport.current_input_gain.db == 3.25);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    const auto stored_calibration = settings.inputCalibrationState();
    REQUIRE(stored_calibration.has_value());
    CHECK(stored_calibration->calibration_gain.db == 3.25);
    REQUIRE(audio_devices.current_input_identity.has_value());
    CHECK(stored_calibration->input_device_identity == *audio_devices.current_input_identity);
}

// Verifies settings editing releases the calibrated route before JUCE closes the active device.
TEST_CASE("Audio settings open releases calibrated input route", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_settings_route_release"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = common::audio::InputDeviceIdentity{
        .backend_name = "Windows Audio",
        .input_device_name = "WASAPI Interface",
        .input_channel_index = 0,
        .input_channel_name = "Input 1",
    };
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onInputCalibrationRequested();
    REQUIRE(controller.onInputCalibrationManuallySet(3.25).has_value());
    controller.onInputCalibrationDismissed();
    REQUIRE(transport.live_input_monitoring_enabled);
    REQUIRE_FALSE(transport.calibration_input_monitoring_enabled);
    REQUIRE(view.last_state.has_value());
    REQUIRE_FALSE(view.last_state->input_calibration_prompt.has_value());
    REQUIRE(view.last_state->audio_device_settings_enabled);

    REQUIRE(controller.onAudioDeviceSettingsOpenRequested());

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    CHECK(settings.inputCalibrationState().has_value());
    REQUIRE(view.last_state.has_value());
    CHECK(
        view.last_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
    CHECK_FALSE(view.last_state->audio_device_settings_enabled);

    audio_devices.notifyChanged();

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);

    controller.onAudioDeviceSettingsClosed();

    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->audio_device_settings_enabled);
}

// Verifies settings close does not treat JUCE's temporary closed route as a device change.
TEST_CASE("Audio settings close waits for settled input route", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_settings_close_settle"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    REQUIRE(transport.live_input_monitoring_enabled);

    REQUIRE(controller.onAudioDeviceSettingsOpenRequested());
    audio_devices.current_input_identity = std::nullopt;
    controller.onAudioDeviceSettingsClosed();

    CHECK(settings.inputCalibrationState().has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    REQUIRE(view.last_state.has_value());
    CHECK(
        view.last_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);

    audio_devices.current_input_identity = identity;
    audio_devices.notifyChanged();

    CHECK(settings.inputCalibrationState().has_value());
    CHECK(transport.live_input_monitoring_enabled);
    REQUIRE(view.last_state.has_value());
    CHECK(
        view.last_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::Calibrated);
}

// Verifies saved calibration does not route live input before the project load fully commits.
TEST_CASE(
    "Stored input calibration stays disabled until live rig load completes",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_startup_gate"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
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

    CHECK(transport.current_input_gain.db == 5.0);
    CHECK(transport.live_input_monitoring_enabled);
}

// Verifies backend arming failure does not erase calibration for the unchanged input route.
TEST_CASE("Input calibration reports backend unavailable", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_backend_unavailable"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
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
    CHECK(settings.inputCalibrationState().has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: live input backend unavailable.");
}

// Verifies that committed input route changes clear app-local calibration and disable monitoring.
TEST_CASE(
    "Input route change clears calibration and disables monitoring", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_route_change"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity initial_identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    CHECK_FALSE(transport.live_input_monitoring_enabled);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    CHECK(transport.current_input_gain.db == 5.0);
    CHECK(transport.live_input_monitoring_enabled);

    audio_devices.current_input_identity = makeInputDeviceIdentity("Interface B");
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(settings.inputCalibrationState().has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: input calibration required.");
}

// Verifies a route change during measurement closes the prompt and leaves monitoring disabled.
TEST_CASE("Input route change during calibration closes prompt", "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_active_route_change"};
    EditorSettings settings{files.settingsFile()};
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    audio_devices.current_input_identity = makeInputDeviceIdentity();
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);
    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    REQUIRE(view.last_state.has_value());
    REQUIRE(view.last_state->input_calibration_prompt.has_value());

    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());
    CHECK(transport.calibration_input_monitoring_enabled);

    audio_devices.current_input_identity = makeInputDeviceIdentity("Interface B");
    audio_devices.notifyChanged();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK_FALSE(settings.inputCalibrationState().has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::MissingCalibration);
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: input calibration required.");
}

// Verifies that dismissing manual recalibration restores the previous matching calibration.
TEST_CASE(
    "Manual input recalibration dismissal restores previous calibration",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_manual_restore"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK(transport.live_input_monitoring_enabled);

    controller.onInputCalibrationRequested();
    REQUIRE(view.last_state.has_value());
    CHECK(view.last_state->input_calibration_prompt.has_value());

    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK(transport.calibration_input_monitoring_enabled);
    CHECK(transport.current_input_gain.db == 0.0);

    controller.onInputCalibrationDismissed();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto restored_calibration = settings.inputCalibrationState();
    REQUIRE(restored_calibration.has_value());
    CHECK(restored_calibration->calibration_gain.db == 4.0);
}

// Verifies that a failed manual recalibration attempt restores monitoring without closing retry UI.
TEST_CASE(
    "Manual input recalibration cancellation restores previous calibration",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_manual_cancel"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());

    controller.onInputCalibrationMeasurementCancelled();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(final_state->input_calibration_prompt.has_value());
    CHECK(final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Calibrated);
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto restored_calibration = settings.inputCalibrationState();
    REQUIRE(restored_calibration.has_value());
    CHECK(restored_calibration->calibration_gain.db == 4.0);
}

// Verifies backend restore failure preserves calibration and reports the route as unavailable.
TEST_CASE(
    "Input recalibration cancel preserves calibration on backend failure",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_manual_cancel_backend_unavailable"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());

    transport.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    controller.onInputCalibrationMeasurementCancelled();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: live input backend unavailable.");
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto preserved_calibration = settings.inputCalibrationState();
    REQUIRE(preserved_calibration.has_value());
    CHECK(preserved_calibration->calibration_gain.db == 4.0);
}

// Verifies final arming failure after a completed recalibration does not delete the old value.
TEST_CASE(
    "Input recalibration commit preserves calibration on backend failure",
    "[core][editor-controller]")
{
    const ScopedControllerFiles files{"input_calibration_auto_commit_backend_unavailable"};
    EditorSettings settings{files.settingsFile()};
    const common::audio::InputDeviceIdentity identity = makeInputDeviceIdentity();
    settings.setInputCalibrationState(
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
    EditorController controller{
        audioPorts(transport, audio, audio_devices, plugin_host, live_rig),
        controllerServices(settings),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onInputCalibrationRequested();
    const auto measurement_started = controller.onInputCalibrationMeasurementStarted();
    REQUIRE(measurement_started.has_value());

    transport.next_set_live_input_monitoring_error = common::audio::LiveInputError{
        common::audio::LiveInputErrorCode::InputRouteUnavailable,
        "live input route could not be armed",
    };
    const auto calibration_applied = controller.onInputCalibrationSucceeded(6.0);
    REQUIRE_FALSE(calibration_applied.has_value());

    controller.onInputCalibrationMeasurementCancelled();

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_FALSE(final_state->input_calibration_prompt.has_value());
    CHECK(
        final_state->signal_chain.input_calibration_status == InputCalibrationStatus::Unavailable);
    CHECK(
        final_state->signal_chain.disabled_message ==
        "Live input disabled: live input backend unavailable.");
    CHECK(transport.current_input_gain.db == 4.0);
    CHECK_FALSE(transport.live_input_monitoring_enabled);
    CHECK_FALSE(transport.calibration_input_monitoring_enabled);
    const auto preserved_calibration = settings.inputCalibrationState();
    REQUIRE(preserved_calibration.has_value());
    CHECK(preserved_calibration->calibration_gain.db == 4.0);
}

} // namespace rock_hero::editor::core
