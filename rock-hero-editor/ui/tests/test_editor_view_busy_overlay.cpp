#include <cstdlib>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

// EditorView reveals the busy overlay when state.busy is present and hides it again when busy
// clears. The overlay is identified by its componentID so the test does not depend on the
// concrete BusyOverlay type.
TEST_CASE("EditorView shows the busy overlay while state.busy is set", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    const juce::Component* const overlay = view.findChildWithID("busy_overlay");
    REQUIRE(overlay != nullptr);
    CHECK_FALSE(overlay->isVisible());
    // The bar is built on demand, so none exists until a busy state requests one.
    CHECK(testing::findDescendant(view, "busy_progress_bar") == nullptr);

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
        .indicator = core::BusyIndicator::IndeterminateProgress,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    CHECK(findRequiredDescendant<juce::Component>(view, "busy_progress_bar").isVisible());

    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Loading plugin 1 of 4: Amp Sim",
        .indicator = core::BusyIndicator::DeterminateProgress,
        .progress = std::optional<double>{0.25},
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    CHECK(findRequiredDescendant<juce::Component>(view, "busy_progress_bar").isVisible());

    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::LoadingPlugin,
        .message = "Loading plugin: Amp Sim",
        .indicator = core::BusyIndicator::MessageOnly,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    CHECK(overlay->isVisible());
    // Message-only operations show no bar at all, so none is built.
    CHECK(testing::findDescendant(view, "busy_progress_bar") == nullptr);

    core::EditorViewState idle_state;
    idle_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    idle_state.arrangement = makeArrangementState(std::filesystem::path{});
    idle_state.signal_chain = core::SignalChainViewState{};
    view.setState(idle_state);

    CHECK_FALSE(overlay->isVisible());
}

// Determinate progress paints the exact fraction immediately. juce::ProgressBar would ramp its
// displayed value toward the target; this guards that the overlay shows the true fraction instead.
TEST_CASE("BusyOverlay paints determinate progress at its exact fraction", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    BusyOverlay overlay;
    overlay.setBounds(0, 0, 200, 100);

    overlay.setBusyState(
        core::BusyViewState{
            .operation = core::BusyOperation::LoadingLiveRig,
            .message = "Loading plugin 1 of 2...",
            .indicator = core::BusyIndicator::DeterminateProgress,
            .progress = std::optional<double>{0.5},
            .cancel_enabled = false,
        });

    auto& progress_bar = findRequiredDescendant<juce::Component>(overlay, "busy_progress_bar");
    REQUIRE(progress_bar.getWidth() > 0);

    const juce::Image snapshot =
        progress_bar.createComponentSnapshot(progress_bar.getLocalBounds());
    const juce::Colour foreground = progress_bar.findColour(juce::ProgressBar::foregroundColourId);
    const juce::Colour background = progress_bar.findColour(juce::ProgressBar::backgroundColourId);

    const auto looks_filled = [&](int x, int y) {
        const juce::Colour pixel = snapshot.getPixelAt(x, y);
        const auto channel_distance = [](juce::Colour a, juce::Colour b) {
            return std::abs(static_cast<int>(a.getRed()) - static_cast<int>(b.getRed())) +
                   std::abs(static_cast<int>(a.getGreen()) - static_cast<int>(b.getGreen())) +
                   std::abs(static_cast<int>(a.getBlue()) - static_cast<int>(b.getBlue()));
        };
        return channel_distance(pixel, foreground) < channel_distance(pixel, background);
    };

    const int mid_y = progress_bar.getHeight() / 2;
    // A 0.5 fraction fills the left half exactly: filled left of centre, empty right of centre.
    CHECK(looks_filled(progress_bar.getWidth() / 4, mid_y));
    CHECK_FALSE(looks_filled((progress_bar.getWidth() * 3) / 4, mid_y));
}

// The busy-overlay fence waits for an actual overlay paint and then posts the callback once.
TEST_CASE("EditorView runs busy callback after overlay paint", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    juce::Component* const overlay = view.findChildWithID("busy_overlay");
    REQUIRE(overlay != nullptr);

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::LoadingPlugin,
        .message = "Loading plugin...",
        .indicator = core::BusyIndicator::MessageOnly,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    int callback_count = 0;
    view.runAfterBusyOverlayPainted([&callback_count] { callback_count += 1; });

    const juce::Image image{juce::Image::RGB, 320, 200, true};
    juce::Graphics graphics{image};
    overlay->paint(graphics);

    CHECK(callback_count == 1);

    overlay->paint(graphics);
    CHECK(callback_count == 1);
}

// A hidden view cannot satisfy a paint fence; startup restore must continue instead of waiting.
TEST_CASE("EditorView runs busy callback when hidden", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};

    core::EditorViewState busy_state;
    busy_state.visible_timeline = common::core::TimeRange{
        .start = common::core::TimePosition{},
        .end = common::core::TimePosition{},
    };
    busy_state.arrangement = makeArrangementState(std::filesystem::path{});
    busy_state.signal_chain = core::SignalChainViewState{};
    busy_state.busy = core::BusyViewState{
        .operation = core::BusyOperation::OpeningProject,
        .message = "Opening project...",
        .indicator = core::BusyIndicator::MessageOnly,
        .cancel_enabled = false,
    };
    view.setState(busy_state);

    int callback_count = 0;
    view.runAfterBusyOverlayPainted([&callback_count] { callback_count += 1; });

    CHECK(callback_count == 1);
}

} // namespace rock_hero::editor::ui
