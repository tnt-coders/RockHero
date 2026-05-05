#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/ui/track_view.h>
#include <utility>
#include <vector>

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

// Records thumbnail source refreshes and draw requests from the track view.
class FakeThumbnail final : public audio::IThumbnail
{
public:
    // Captures the new source each time the view asks the thumbnail to refresh itself.
    void setSource(const core::AudioAsset& audio_asset) override
    {
        last_source = audio_asset;
        has_source = true;
        set_source_call_count += 1;
    }

    // Reports whether this fake has drawable source data.
    [[nodiscard]] bool hasSource() const override
    {
        return has_source;
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

    // Records the requested visible range so paint behavior can be observed.
    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> bounds, core::TimeRange visible_range,
        float vertical_zoom) override
    {
        last_draw_bounds = bounds;
        last_drawn_visible_range = visible_range;
        last_vertical_zoom = vertical_zoom;
        return draw_result;
    }

    // Last source installed into this thumbnail, if any.
    std::optional<core::AudioAsset> last_source{};

    // Last visible range requested for drawing, if any.
    std::optional<core::TimeRange> last_drawn_visible_range{};

    // Last bounds requested for drawing, if any.
    std::optional<juce::Rectangle<int>> last_draw_bounds{};

    // Last vertical zoom requested for drawing, if any.
    std::optional<float> last_vertical_zoom{};

    // Number of times the view has refreshed this thumbnail's source.
    int set_source_call_count{0};

    // Fake proxy-generation flag.
    bool generating_proxy{false};

    // Fake proxy progress.
    float proxy_progress{0.0f};

    // Fake source-readiness flag.
    bool has_source{false};

    // Result returned by drawChannels().
    bool draw_result{true};
};

// Creates fake thumbnails while recording the component that requested one.
class FakeThumbnailFactory final : public audio::IThumbnailFactory
{
public:
    [[nodiscard]] std::unique_ptr<audio::IThumbnail> createThumbnail(
        juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        thumbnails.push_back(thumbnail.get());
        return thumbnail;
    }

    // Last component that requested a thumbnail, if any.
    juce::Component* last_owner{nullptr};

    // Thumbnail fakes currently owned by track views.
    std::vector<FakeThumbnail*> thumbnails{};

    // Number of thumbnail creation requests observed.
    int create_call_count{0};
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

// Builds full-source track audio for track-view tests.
[[nodiscard]] core::TrackAudio makeTrackAudio(
    std::filesystem::path path, core::TimeDuration duration = core::TimeDuration{4.0})
{
    return core::TrackAudio{
        .asset = core::AudioAsset{std::move(path)},
        .duration = duration,
    };
}

} // namespace

// Verifies assigned audio points the track-owned thumbnail at the asset.
TEST_CASE("TrackView creates a thumbnail for track audio", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio = makeTrackAudio(std::filesystem::path{"full_mix.wav"}),
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_owner == &view);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 1);
    CHECK(
        thumbnail->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies reapplying the same audio state reuses the existing thumbnail source.
TEST_CASE("TrackView reuses thumbnail source for the same asset", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    const TrackViewState state{
        .track_id = core::TrackId{1},
        .display_name = "Full Mix",
        .audio = makeTrackAudio(std::filesystem::path{"full_mix.wav"}),
    };

    view.setState(state);
    view.setState(state);

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    CHECK(thumbnail_factory.thumbnails.front()->set_source_call_count == 1);
}

// Verifies changing the track asset refreshes the existing track-owned thumbnail.
TEST_CASE("TrackView refreshes thumbnail when the asset changes", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio = makeTrackAudio(std::filesystem::path{"full_mix.wav"}),
        });
    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio = makeTrackAudio(std::filesystem::path{"guitar_stem.wav"}),
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 2);
    CHECK(
        thumbnail->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"guitar_stem.wav"}}});
}

// Verifies TrackView asks the thumbnail to draw only the visible asset range.
TEST_CASE("TrackView draws the visible waveform range", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        core::TimeRange{
            .start = core::TimePosition{2.0},
            .end = core::TimePosition{6.0},
        });
    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio =
                makeTrackAudio(std::filesystem::path{"full_mix.wav"}, core::TimeDuration{10.0}),
        });
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(
        thumbnail->last_drawn_visible_range == std::optional{core::TimeRange{
                                                   .start = core::TimePosition{2.0},
                                                   .end = core::TimePosition{6.0},
                                               }});
    CHECK(thumbnail->last_draw_bounds == std::optional{image.getBounds()});
    CHECK(thumbnail->last_vertical_zoom == std::optional{1.0f});
}

// Verifies audio shorter than the visible range is drawn into the matching row subset.
TEST_CASE("TrackView maps short audio into visible row bounds", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        core::TimeRange{
            .start = core::TimePosition{},
            .end = core::TimePosition{10.0},
        });
    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio = makeTrackAudio(std::filesystem::path{"full_mix.wav"}, core::TimeDuration{4.0}),
        });
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(thumbnail->last_draw_bounds == std::optional{juce::Rectangle<int>{0, 0, 40, 24}});
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
    if (listener.last_normalized_x.has_value())
    {
        CHECK(listener.last_normalized_x.value() == Catch::Approx(0.25));
    }
}

} // namespace rock_hero::ui
