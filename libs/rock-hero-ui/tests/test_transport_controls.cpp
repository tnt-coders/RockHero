#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/ui/transport_controls.h>
#include <stdexcept>

namespace rock_hero::ui
{

namespace
{

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
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(0));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the concrete stop button owned by the widget under test.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(1));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

// Synthesizes a left-button mouse event positioned inside the supplied button.
[[nodiscard]] juce::MouseEvent makeButtonMouseEvent(juce::Button& button)
{
    const auto position = juce::Point<float>{5.0f, 5.0f};
    const auto event_time = juce::Time::getCurrentTime();

    return {
        juce::Desktop::getInstance().getMainMouseSource(),
        position,
        juce::ModifierKeys::leftButtonModifier,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        &button,
        &button,
        event_time,
        position,
        event_time,
        1,
        false
    };
}

// Presses and releases the supplied button synchronously through JUCE's normal mouse path.
void clickButton(juce::Button& button)
{
    const auto event = makeButtonMouseEvent(button);
    auto& component = static_cast<juce::Component&>(button);
    component.mouseDown(event);
    component.mouseUp(event);
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
        TransportControlsState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
        });

    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());

    controls.setState(
        TransportControlsState{
            .play_pause_enabled = false,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = true,
        });

    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
}

// Verifies setState flips the primary button between its play and pause icon variants.
TEST_CASE("TransportControls setState switches play and pause icon", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);

    controls.setState(
        TransportControlsState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
        });
    auto& play_pause_button = getPlayPauseButton(controls);
    auto* play_image = play_pause_button.getCurrentImage();
    CHECK_FALSE(play_pause_button.getToggleState());

    controls.setState(
        TransportControlsState{
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = true,
        });
    auto* pause_image = play_pause_button.getCurrentImage();

    CHECK(play_pause_button.getToggleState());
    REQUIRE(play_image != nullptr);
    REQUIRE(pause_image != nullptr);
    CHECK(play_image != pause_image);
}

// Verifies the primary button emits the listener's play/pause intent when clicked.
TEST_CASE("TransportControls play pause click calls listener", "[ui][transport-controls]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeTransportControlsListener listener;
    TransportControls controls{listener};
    controls.setBounds(0, 0, 120, 24);
    controls.setState(
        TransportControlsState{
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
        TransportControlsState{
            .play_pause_enabled = false,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = false,
        });

    clickButton(getStopButton(controls));

    CHECK(listener.play_pause_press_count == 0);
    CHECK(listener.stop_press_count == 1);
}

} // namespace rock_hero::ui
