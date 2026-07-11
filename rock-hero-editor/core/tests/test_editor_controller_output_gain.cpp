#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <compare>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Verifies that authored output gain controls remain available after loading a live rig.
TEST_CASE("Output gain controls enabled with live rig and arrangement", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
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
    CHECK(final_state->signal_chain.output_gain_controls_enabled);
    CHECK_THAT(final_state->signal_chain.output_gain_db, Catch::Matchers::WithinULP(0.0, 0));
    CHECK(
        final_state->signal_chain.input_calibration_status ==
        InputCalibrationStatus::NoActiveInputDevice);
    CHECK_FALSE(final_state->signal_chain.input_calibrate_enabled);
}

// Verifies that authored output gain controls remain available with the required live-rig port.
TEST_CASE("Output gain controls enabled with required live rig", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host),
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
    CHECK(final_state->signal_chain.output_gain_controls_enabled);
}

// Verifies that an output gain change calls the live rig and marks dirty.
TEST_CASE("Output gain change calls live rig and marks dirty", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onOutputGainChanged(-12.0);

    CHECK(live_rig.set_output_gain_call_count == 1);
    CHECK(std::is_eq(live_rig.current_output_gain.db <=> -12.0));

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(std::is_eq(final_state->signal_chain.output_gain_db <=> -12.0));
    CHECK(final_state->undo_label == std::optional<std::string>{"Set Output Gain to -12 dB"});
}

// Verifies that output gain undo and redo replay through the live-rig port.
TEST_CASE("Output gain undo redo restores live rig", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onOutputGainChanged(-9.0);
    controller.onUndoRequested();

    CHECK(live_rig.set_output_gain_call_count == 2);
    CHECK(live_rig.current_output_gain == common::audio::Gain{0.0});
    const auto* const undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    CHECK_THAT(undone_state->signal_chain.output_gain_db, Catch::Matchers::WithinULP(0.0, 0));
    CHECK(undone_state->redo_label == std::optional<std::string>{"Set Output Gain to -9 dB"});

    controller.onRedoRequested();

    CHECK(live_rig.set_output_gain_call_count == 3);
    CHECK(live_rig.current_output_gain == common::audio::Gain{-9.0});
    const auto* const redone_state = stateOrNull(view.last_state);
    REQUIRE(redone_state != nullptr);
    CHECK(std::is_eq(redone_state->signal_chain.output_gain_db <=> -9.0));
}

// Verifies that live output gain previews coalesce into one committed undo entry.
TEST_CASE("Output gain preview commits one undo entry", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));

    controller.onOutputGainPreviewChanged(-3.0);
    controller.onOutputGainPreviewChanged(-12.0);

    const auto* const preview_state = stateOrNull(view.last_state);
    REQUIRE(preview_state != nullptr);
    CHECK(std::is_eq(preview_state->signal_chain.output_gain_db <=> -12.0));
    CHECK_FALSE(preview_state->undo_label.has_value());
    CHECK(live_rig.set_output_gain_call_count == 2);

    controller.onOutputGainChanged(-12.0);

    const auto* const committed_state = stateOrNull(view.last_state);
    REQUIRE(committed_state != nullptr);
    CHECK(committed_state->undo_label == std::optional<std::string>{"Set Output Gain to -12 dB"});
    CHECK(live_rig.set_output_gain_call_count == 2);

    controller.onUndoRequested();

    CHECK(live_rig.set_output_gain_call_count == 3);
    CHECK(live_rig.current_output_gain == common::audio::Gain{0.0});
    const auto* const undone_state = stateOrNull(view.last_state);
    REQUIRE(undone_state != nullptr);
    CHECK_THAT(undone_state->signal_chain.output_gain_db, Catch::Matchers::WithinULP(0.0, 0));
}

// Verifies that output gain values are clamped through the project-owned gain value type.
TEST_CASE("Output gain changes clamp to valid range", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onOutputGainChanged(-999.0);

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK(std::is_eq(final_state->signal_chain.output_gain_db <=> common::audio::minimumGainDb()));
}

// Verifies that output gain is restored from the live rig load result.
TEST_CASE("Output gain restored from load result", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    live_rig.next_load_result.output_gain = common::audio::Gain{-6.0};
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
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
    CHECK(std::is_eq(final_state->signal_chain.output_gain_db <=> -6.0));
}

// Verifies that output gain resets to default on project close.
TEST_CASE("Output gain resets on project close", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeLiveRig live_rig;
    FakeProjectServices project_services;
    FakeEditorView view;
    EditorController controller{
        audioPorts(transport, audio, plugin_host, live_rig),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    controller.attachView(view);

    REQUIRE(
        loadArrangement(controller, project_services, audio, std::filesystem::path{"song.wav"}));
    controller.onOutputGainChanged(-3.0);

    // Discard unsaved changes and close.
    controller.onCloseRequested();
    controller.onUnsavedChangesDecision(UnsavedChangesDecision::Discard);

    const auto* const final_state = stateOrNull(view.last_state);
    REQUIRE(final_state != nullptr);
    CHECK_THAT(final_state->signal_chain.output_gain_db, Catch::Matchers::WithinULP(0.0, 0));
    CHECK_FALSE(final_state->signal_chain.output_gain_controls_enabled);
}

} // namespace rock_hero::editor::core
