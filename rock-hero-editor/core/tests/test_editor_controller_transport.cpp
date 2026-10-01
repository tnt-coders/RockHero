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

// Without an open audio device nothing could move the playhead, so Play and Stop are unavailable —
// the view is told why and both intents are no-ops — while the loaded song stays editable.
TEST_CASE(
    "EditorController refuses transport intents while the audio device is closed",
    "[core][editor-controller]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    ConfigurableAudioDeviceConfiguration audio_devices;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio, audio_devices),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadArrangement(controller, project_services, audio, std::filesystem::path{"a.wav"}));

    controller.onPlayPausePressed();
    controller.onStopPressed();

    CHECK(transport.play_call_count == 0);
    CHECK(transport.stop_call_count == 0);
    CHECK(view.timeline_start_reveal_count == 0);
    REQUIRE(view.last_state.has_value());
    if (view.last_state.has_value())
    {
        CHECK(
            view.last_state->transport.unavailable_reason ==
            "Playback disabled: audio device closed.");
    }
}

// Stop runs wherever the transport is, the start included: it shares Play's gate, not a cursor
// rule, so a paused cursor already at the start still resets.
TEST_CASE("EditorController stop intent runs at any position", "[core][editor-controller]")
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
    CHECK(transport.stop_call_count == 1);

    transport.current_position = common::core::TimePosition{1.5};
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 2);
    CHECK(transport.current_position == common::core::TimePosition{});

    transport.current_state.playing = true;
    controller.onStopPressed();
    CHECK(transport.stop_call_count == 3);
}

// Stop refreshes the view and then asks it to bring the timeline start into sight.
TEST_CASE("EditorController stop intent reveals the timeline start", "[core][editor-controller]")
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
    const int pushes_before_stop = view.set_state_call_count;

    controller.onStopPressed();

    CHECK(transport.stop_call_count == 1);
    CHECK(view.set_state_call_count == pushes_before_stop + 1);
    CHECK(view.timeline_start_reveal_count == 1);
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

// A seek never changes whether the transport is available: Stop does not dim at the start.
TEST_CASE(
    "EditorController timeline seek leaves the transport available", "[core][editor-controller]")
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

    const auto transport_available = [&view] {
        return view.last_state.has_value() &&
               !view.last_state->transport.unavailable_reason.has_value();
    };
    CHECK(transport_available());

    controller.onTimelineSeekRequested(common::core::TimePosition{2.0});

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{2.0}});
    CHECK(transport_available());

    controller.onTimelineSeekRequested(common::core::TimePosition{0.0});

    CHECK(transport.last_seek_position == std::optional{common::core::TimePosition{}});
    CHECK(transport_available());
}

} // namespace rock_hero::editor::core
