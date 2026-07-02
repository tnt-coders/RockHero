#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// Play intent issues play() when stopped and pause() when playing, once audio is loaded.
TEST_CASE("EditorController play intent toggles loaded transport", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));

    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 0);

    transport.current_state.playing = true;
    controller.onPlayPausePressed();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 1);
}

// Plugin-window Play/Pause shortcuts use the same transport intent as the main editor view.
TEST_CASE(
    "EditorController plugin window play intent toggles transport", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    RecordingPluginHost plugin_host;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, plugin_host),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));

    plugin_host.notifyPluginWindowPlayPauseRequested();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 0);

    transport.current_state.playing = true;
    plugin_host.notifyPluginWindowPlayPauseRequested();
    CHECK(transport.play_call_count == 1);
    CHECK(transport.pause_call_count == 1);
}

// Without arrangement audio there is nothing to play, so the intent is a no-op.
TEST_CASE("EditorController ignores play intent without audio", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    EditorController controller{
        audioPorts(transport, audio), defaultControllerServices(), noopExitFunction()
    };

    controller.onPlayPausePressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.pause_call_count == 0);
}

// The stop intent respects the same gate the view publishes.
TEST_CASE("EditorController stop intent follows reset gate", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));

    controller.onStopPressed();
    CHECK(transport.stop_call_count == 0);

    transport.current_position = common::core::TimePosition{1.5};
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 1);
    CHECK(transport.current_position == common::core::TimePosition{});

    transport.current_state.playing = true;
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 2);
}

// Stopping from a paused non-start cursor refreshes the view directly after stop().
TEST_CASE("EditorController stop intent refreshes paused reset state", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));
    FakeEditorView view;
    controller.attachView(view);

    transport.current_position = common::core::TimePosition{1.5};
    transport.setStateAndNotify(
        common::audio::TransportState{
            .playing = false,
        });
    CHECK(lastStopEnabled(view) == std::optional{true});
    const int pushes_before_stop = view.set_state_call_count;

    controller.onStopPressed();

    CHECK(transport.stop_call_count == 1);
    CHECK(view.set_state_call_count == pushes_before_stop + 1);
    CHECK(lastStopEnabled(view) == std::optional{false});
}

// Timeline seek intents clamp out-of-range positions into the loaded session timeline.
TEST_CASE("EditorController timeline seek clamps into the timeline", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0)));

    controller.onTimelineSeekRequested(common::core::TimePosition{2.0});
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});

    controller.onTimelineSeekRequested(common::core::TimePosition{-1.0});
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{0.0}});

    controller.onTimelineSeekRequested(common::core::TimePosition{9.0});
    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{4.0}});
}

// A seek issued by the controller refreshes whether Stop can reset the cursor.
TEST_CASE("EditorController timeline seek refreshes stop state", "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    REQUIRE(loadArrangement(
        controller,
        project_services,
        audio,
        std::filesystem::path{"a.wav"},
        loadedTimelineRange(4.0)));
    FakeEditorView view;
    controller.attachView(view);

    CHECK(lastStopEnabled(view) == std::optional{false});

    controller.onTimelineSeekRequested(common::core::TimePosition{2.0});

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});
    CHECK(lastStopEnabled(view) == std::optional{true});

    controller.onTimelineSeekRequested(common::core::TimePosition{0.0});

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{}});
    CHECK(lastStopEnabled(view) == std::optional{false});
}

} // namespace rock_hero::editor::core
