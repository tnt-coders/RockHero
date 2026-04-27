#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/audio/thumbnail.h>
#include <rock_hero/ui/editor_view.h>
#include <stdexcept>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Records editor intents emitted by EditorView child widgets.
class FakeEditorController final : public IEditorController
{
public:
    void onLoadAudioAssetRequested(core::TrackId track_id, core::AudioAsset audio_asset) override
    {
        last_track_id = track_id;
        last_audio_asset = std::move(audio_asset);
        load_request_count += 1;
    }

    void onPlayPausePressed() override
    {
        play_pause_press_count += 1;
    }

    void onStopPressed() override
    {
        stop_press_count += 1;
    }

    void onWaveformClicked(double normalized_x) override
    {
        last_normalized_x = normalized_x;
        waveform_click_count += 1;
    }

    std::optional<core::TrackId> last_track_id{};
    std::optional<core::AudioAsset> last_audio_asset{};
    std::optional<double> last_normalized_x{};
    int load_request_count{0};
    int play_pause_press_count{0};
    int stop_press_count{0};
    int waveform_click_count{0};
};

// Fake transport gives the cursor path a position source without exposing Engine.
class FakeTransport final : public audio::ITransport
{
public:
    void play() override
    {}
    void pause() override
    {}
    void stop() override
    {}
    void seek(core::TimePosition position_value) override
    {
        current_state.position = position_value;
    }

    [[nodiscard]] audio::TransportState state() const override
    {
        return current_state;
    }

    [[nodiscard]] core::TimePosition position() const noexcept override
    {
        position_read_count += 1;
        return current_state.position;
    }

    void addListener(Listener& /*listener*/) override
    {}
    void removeListener(Listener& /*listener*/) override
    {}

    audio::TransportState current_state{};
    mutable int position_read_count{0};
};

// Records thumbnail source updates installed through the track view owned by EditorView.
class FakeThumbnail final : public audio::Thumbnail
{
public:
    void setSource(const core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        set_source_call_count += 1;
    }

    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    [[nodiscard]] double getLength() const override
    {
        return 1.0;
    }

    void drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, float /*vertical_zoom*/) override
    {}

    std::optional<core::AudioAsset> last_source{};
    int set_source_call_count{0};
};

// Returns a required child component by id and type, failing the current test if missing.
template <class ComponentType>
[[nodiscard]] ComponentType& findRequiredChild(juce::Component& parent, const juce::String& id)
{
    auto* child = parent.findChildWithID(id);
    if (child == nullptr)
    {
        throw std::runtime_error{"Missing child component: " + id.toStdString()};
    }

    auto* typed_child = dynamic_cast<ComponentType*>(child);
    if (typed_child == nullptr)
    {
        throw std::runtime_error{"Child component has unexpected type: " + id.toStdString()};
    }

    return *typed_child;
}

// Returns the play/pause button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getPlayPauseButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(0));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls play/pause button missing"};
    }
    return *button;
}

// Returns the stop button from the transport-controls child.
[[nodiscard]] juce::DrawableButton& getStopButton(TransportControls& controls)
{
    auto* button = dynamic_cast<juce::DrawableButton*>(controls.getChildComponent(1));
    if (button == nullptr)
    {
        throw std::runtime_error{"TransportControls stop button missing"};
    }
    return *button;
}

// Synthesizes a simple left-button mouse-down event relative to one component.
[[nodiscard]] juce::MouseEvent makeMouseDownEvent(juce::Component& component, float x, float y)
{
    const auto position = juce::Point<float>{x, y};
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
        &component,
        &component,
        event_time,
        position,
        event_time,
        1,
        false
    };
}

} // namespace

// Verifies construction consumes the thumbnail creator exactly once for the initial row.
TEST_CASE("EditorView creates the initial track thumbnail", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    FakeThumbnail* thumbnail_ptr = nullptr;
    juce::Component* thumbnail_owner = nullptr;
    int create_thumbnail_call_count = 0;

    EditorView view{controller, transport, [&](juce::Component& owner) {
                        thumbnail_owner = &owner;
                        create_thumbnail_call_count += 1;
                        auto thumbnail = std::make_unique<FakeThumbnail>();
                        thumbnail_ptr = thumbnail.get();
                        return thumbnail;
                    }};

    CHECK(create_thumbnail_call_count == 1);
    REQUIRE(thumbnail_owner != nullptr);
    CHECK(thumbnail_owner->getComponentID() == "track_view");
    REQUIRE(thumbnail_ptr != nullptr);

    view.setState(
        EditorViewState{
            .load_button_enabled = true,
            .play_pause_enabled = true,
            .stop_enabled = false,
            .play_pause_shows_pause_icon = false,
            .visible_timeline_start = core::TimePosition{},
            .visible_timeline_duration = core::TimeDuration{4.0},
            .tracks = {TrackViewState{
                .track_id = core::TrackId{1},
                .display_name = "Full Mix",
                .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
            }},
            .last_load_error = std::nullopt,
        });

    CHECK(thumbnail_ptr->set_source_call_count == 1);
}

// Verifies setState projects transition-shaped state into child widgets without reading position.
TEST_CASE("EditorView setState projects controls without polling position", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    EditorView view{controller, transport, [](juce::Component&) {
                        return std::make_unique<FakeThumbnail>();
                    }};

    auto& load_button = findRequiredChild<juce::TextButton>(view, "load_button");
    auto& controls = findRequiredChild<TransportControls>(view, "transport_controls");

    view.setState(EditorViewState{});

    CHECK_FALSE(load_button.isEnabled());
    CHECK_FALSE(getPlayPauseButton(controls).isEnabled());
    CHECK_FALSE(getStopButton(controls).isEnabled());
    CHECK(transport.position_read_count == 0);

    view.setState(
        EditorViewState{
            .load_button_enabled = true,
            .play_pause_enabled = true,
            .stop_enabled = true,
            .play_pause_shows_pause_icon = true,
            .visible_timeline_start = core::TimePosition{},
            .visible_timeline_duration = core::TimeDuration{8.0},
            .tracks = {TrackViewState{
                .track_id = core::TrackId{2},
                .display_name = "Full Mix",
                .audio_asset = core::AudioAsset{std::filesystem::path{"mix.wav"}},
            }},
            .last_load_error = std::nullopt,
        });

    CHECK(load_button.isEnabled());
    CHECK(getPlayPauseButton(controls).isEnabled());
    CHECK(getStopButton(controls).isEnabled());
    CHECK(getPlayPauseButton(controls).getToggleState());
    CHECK(transport.position_read_count == 0);
}

// Verifies editor-wide timeline clicks are forwarded without depending on a specific track row.
TEST_CASE("EditorView forwards timeline clicks to the controller", "[ui][editor-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeEditorController controller;
    FakeTransport transport;
    EditorView view{controller, transport, [](juce::Component&) {
                        return std::make_unique<FakeThumbnail>();
                    }};
    view.setBounds(0, 0, 500, 200);

    auto& cursor_overlay = findRequiredChild<juce::Component>(view, "cursor_overlay");
    REQUIRE(cursor_overlay.getWidth() > 0);
    const float click_x = static_cast<float>(cursor_overlay.getWidth()) * 0.25f;
    cursor_overlay.mouseDown(makeMouseDownEvent(cursor_overlay, click_x, 20.0f));

    CHECK(controller.waveform_click_count == 1);
    REQUIRE(controller.last_normalized_x.has_value());
    CHECK(controller.last_normalized_x.value() == Catch::Approx(0.25));
}

// Verifies cursor geometry uses a pushed visible range plus a separately read position.
TEST_CASE("EditorView cursor geometry maps position through visible range", "[ui][editor-view]")
{
    CHECK(
        cursorXForTimelinePosition(
            core::TimePosition{5.0}, core::TimePosition{}, core::TimeDuration{10.0}, 201) ==
        std::optional<int>{100});
    CHECK(
        cursorXForTimelinePosition(
            core::TimePosition{12.0}, core::TimePosition{10.0}, core::TimeDuration{4.0}, 101) ==
        std::optional<int>{50});
    CHECK(
        cursorXForTimelinePosition(
            core::TimePosition{-1.0}, core::TimePosition{}, core::TimeDuration{4.0}, 101) ==
        std::optional<int>{0});
    CHECK(
        cursorXForTimelinePosition(
            core::TimePosition{9.0}, core::TimePosition{}, core::TimeDuration{4.0}, 101) ==
        std::optional<int>{100});
    CHECK_FALSE(cursorXForTimelinePosition(
                    core::TimePosition{1.0}, core::TimePosition{}, core::TimeDuration{}, 101)
                    .has_value());
}

} // namespace rock_hero::ui
