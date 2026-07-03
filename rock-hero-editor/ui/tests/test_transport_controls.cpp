#include "transport_controls.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <stdexcept>

namespace rock_hero::editor::ui
{

using core::TransportViewState;

namespace
{

using testing::clickButton;

// Records transport button intents so widget tests can verify local listener dispatch.
class FakeTransportControlsListener final : public TransportControls::Listener
{
public:
    // Captures the primary transport-button intent.
    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    // Captures the stop-button intent.
    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    // Number of play/pause intents observed.
    int play_pause_press_count{0};

    // Number of stop intents observed.
    int stop_press_count{0};
};

// Returns the concrete play/pause button owned by the widget under test.
[[nodiscard]] juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    auto* button =
        dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("play_pause_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the concrete stop button owned by the widget under test.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.findChildWithID("stop_button"));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

} // namespace

// Verifies setState projects enabledness directly onto the concrete JUCE buttons.
TEST_CASE("TransportControls setState updates enabledness", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);

    controls.setState(
        TransportViewState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
        });

    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());

    controls.setState(
        TransportViewState{
            .play_pause_enabled = false,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = true,
        });

    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
}

// Verifies fixed transport buttons are centered and ordered Play/Pause, then Stop.
TEST_CASE("TransportControls centers play pause before stop", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 40);

    CHECK(getPlayPauseButton(controls).getBounds() == juce::Rectangle<int>{22, 4, 32, 32});
    CHECK(getStopButton(controls).getBounds() == juce::Rectangle<int>{66, 4, 32, 32});
}

// Verifies pause-icon state does not use JUCE toggle state, which paints a button background.
TEST_CASE("TransportControls pause icon does not toggle the button", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);

    controls.setState(
        TransportViewState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
        });
    auto& play_pause_button = getPlayPauseButton(controls);
    REQUIRE(play_pause_button.getCurrentImage() != nullptr);
    CHECK_FALSE(play_pause_button.getToggleState());

    controls.setState(
        TransportViewState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = true,
        });

    REQUIRE(play_pause_button.getCurrentImage() != nullptr);
    CHECK_FALSE(play_pause_button.getToggleState());
}

// Verifies transport button clicks cannot steal editor-level keyboard shortcuts.
TEST_CASE("TransportControls buttons do not take keyboard focus", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setState(
        TransportViewState{
            .play_pause_enabled = true,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = false,
        });

    auto& play_pause_button = getPlayPauseButton(controls);
    auto& stop_button = getStopButton(controls);
    CHECK_FALSE(play_pause_button.getWantsKeyboardFocus());
    CHECK_FALSE(play_pause_button.getMouseClickGrabsKeyboardFocus());
    CHECK_FALSE(stop_button.getWantsKeyboardFocus());
    CHECK_FALSE(stop_button.getMouseClickGrabsKeyboardFocus());
}

// Verifies the primary button emits the listener's play/pause intent when clicked.
TEST_CASE("TransportControls play pause click calls listener", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);
    controls.setState(
        TransportViewState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
        });

    clickButton(getPlayPauseButton(controls));

    CHECK(listener.play_pause_press_count == 1);
    CHECK(listener.stop_press_count == 0);
}

// Verifies the stop button emits the listener's stop intent when clicked.
TEST_CASE("TransportControls stop click calls listener", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);
    controls.setState(
        TransportViewState{
            .play_pause_enabled = false,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = false,
        });

    clickButton(getStopButton(controls));

    CHECK(listener.play_pause_press_count == 0);
    CHECK(listener.stop_press_count == 1);
}

} // namespace rock_hero::editor::ui
