#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/thumbnail.h>
#include <rock_hero/ui/editor/track_view.h>

namespace rock_hero::ui
{

namespace
{

// Synthesizes a simple left-button mouse-down event relative to one component for track-view tests.
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

// Records thumbnail source refreshes so tests can verify row-local asset diff behavior.
class FakeThumbnail final : public audio::Thumbnail
{
public:
    // Captures the new source each time the view asks the thumbnail to refresh itself.
    void setSource(const core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        set_source_call_count += 1;
    }

    // Reports whether this fake thumbnail is still generating a proxy.
    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return generating_proxy;
    }

    // Reports proxy progress configured by the test.
    [[nodiscard]] float getProxyProgress() const override
    {
        return proxy_progress;
    }

    // Reports the fake audio length configured by the test.
    [[nodiscard]] double getLength() const override
    {
        return length_seconds;
    }

    // Ignores drawing because these tests only need observable state transitions.
    void drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, float /*vertical_zoom*/) override
    {}

    // Last source installed into this thumbnail, if any.
    std::optional<core::AudioAsset> last_source{};

    // Number of times the view has refreshed this thumbnail's source.
    int set_source_call_count{0};

    // Fake proxy-generation flag.
    bool generating_proxy{false};

    // Fake proxy progress.
    float proxy_progress{0.0f};

    // Fake loaded length in seconds.
    double length_seconds{1.0};
};

// Records normalized click intent emitted by one track view.
class FakeTrackViewListener final : public TrackView::Listener
{
public:
    // Stores the view pointer and normalized position reported by the component under test.
    void trackViewClicked(TrackView& view, double normalized_x) override
    {
        last_view = &view;
        last_normalized_x = normalized_x;
        click_count += 1;
    }

    // Last track view that emitted click intent, if any.
    TrackView* last_view{nullptr};

    // Last normalized x position emitted by the track view, if any.
    std::optional<double> last_normalized_x{};

    // Number of click events observed.
    int click_count{0};
};

} // namespace

// Verifies a newly present audio asset refreshes the track-view-owned thumbnail exactly once.
TEST_CASE("TrackView refreshes thumbnail when a present asset arrives", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TrackView view;
    auto thumbnail = std::make_unique<FakeThumbnail>();
    auto* thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
        });

    CHECK(thumbnail_ptr->set_source_call_count == 1);
    CHECK(
        thumbnail_ptr->last_source ==
        std::optional<core::AudioAsset>{core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies reapplying the same present asset does not trigger another thumbnail refresh.
TEST_CASE("TrackView does not refresh thumbnail twice for the same asset", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TrackView view;
    auto thumbnail = std::make_unique<FakeThumbnail>();
    auto* thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));

    const TrackViewState state{
        .track_id = core::TrackId{1},
        .display_name = "Full Mix",
        .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
    };

    view.setState(state);
    view.setState(state);

    CHECK(thumbnail_ptr->set_source_call_count == 1);
}

// Verifies changing to a different present asset triggers another thumbnail refresh.
TEST_CASE("TrackView refreshes thumbnail again when the asset changes", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TrackView view;
    auto thumbnail = std::make_unique<FakeThumbnail>();
    auto* thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_asset = core::AudioAsset{std::filesystem::path{"full_mix.wav"}},
        });
    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_asset = core::AudioAsset{std::filesystem::path{"guitar_stem.wav"}},
        });

    CHECK(thumbnail_ptr->set_source_call_count == 2);
    CHECK(
        thumbnail_ptr->last_source == std::optional<core::AudioAsset>{
                                          core::AudioAsset{std::filesystem::path{"guitar_stem.wav"}}
                                      });
}

// Verifies track-local hit testing emits a normalized horizontal click position.
TEST_CASE("TrackView reports normalized click position", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    TrackView view;
    FakeTrackViewListener listener;

    view.addListener(listener);
    view.setBounds(0, 0, 200, 40);

    view.mouseDown(makeMouseDownEvent(view, 50.0f, 10.0f));

    CHECK(listener.click_count == 1);
    CHECK(listener.last_view == &view);
    REQUIRE(listener.last_normalized_x.has_value());
    CHECK(listener.last_normalized_x.value() == Catch::Approx(0.25));
}

} // namespace rock_hero::ui
