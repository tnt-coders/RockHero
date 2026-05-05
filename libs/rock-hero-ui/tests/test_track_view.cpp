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

// Records thumbnail source refreshes so tests can verify row-local asset diff behavior.
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

    // Ignores drawing because these tests only need observable state transitions.
    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, core::TimeRange /*source_range*/,
        float /*vertical_zoom*/) override
    {
        return true;
    }

    // Last source installed into this thumbnail, if any.
    std::optional<core::AudioAsset> last_source{};

    // Number of times the view has refreshed this thumbnail's source.
    int set_source_call_count{0};

    // Fake proxy-generation flag.
    bool generating_proxy{false};

    // Fake proxy progress.
    float proxy_progress{0.0f};

    // Fake source-readiness flag.
    bool has_source{false};
};

// Creates fake thumbnails while recording each clip component that requested one.
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

    // Last clip component that requested a thumbnail, if any.
    juce::Component* last_owner{nullptr};

    // Thumbnail fakes currently owned by clip views.
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

// Provides the default four-second clip range used by most track-view tests.
[[nodiscard]] core::TimeRange defaultClipRange() noexcept
{
    return core::TimeRange{
        .start = core::TimePosition{},
        .end = core::TimePosition{4.0},
    };
}

// Builds one clip state for track-view tests that only care about asset propagation.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(
    std::filesystem::path path, core::TimeRange timeline_range)
{
    const core::TimeRange source_range{
        .start = core::TimePosition{},
        .end = core::TimePosition{timeline_range.duration().seconds},
    };

    return AudioClipViewState{
        .audio_clip_id = core::AudioClipId{1},
        .asset = core::AudioAsset{std::move(path)},
        .source_range = source_range,
        .timeline_range = timeline_range,
    };
}

// Builds one default-range clip state for tests that do not care about timeline placement.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(std::filesystem::path path)
{
    return makeAudioClipViewState(std::move(path), defaultClipRange());
}

} // namespace

// Verifies a newly present clip creates one clip-owned thumbnail and points it at the asset.
TEST_CASE("TrackView creates a clip thumbnail when clip state arrives", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_clips = {makeAudioClipViewState(std::filesystem::path{"full_mix.wav"})},
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.last_owner != nullptr);
    CHECK(thumbnail_factory.last_owner->getComponentID() == "audio_clip_view");
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 1);
    CHECK(
        thumbnail->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies reapplying the same clip state reuses the existing clip view and thumbnail source.
TEST_CASE("TrackView reuses clip thumbnail for the same asset", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    const TrackViewState state{
        .track_id = core::TrackId{1},
        .display_name = "Full Mix",
        .audio_clips = {makeAudioClipViewState(std::filesystem::path{"full_mix.wav"})},
    };

    view.setState(state);
    view.setState(state);

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    CHECK(thumbnail_factory.thumbnails.front()->set_source_call_count == 1);
}

// Verifies changing the clip asset refreshes the existing clip-owned thumbnail.
TEST_CASE("TrackView refreshes clip thumbnail when the asset changes", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_clips = {makeAudioClipViewState(std::filesystem::path{"full_mix.wav"})},
        });
    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_clips = {makeAudioClipViewState(std::filesystem::path{"guitar_stem.wav"})},
        });

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 2);
    CHECK(
        thumbnail->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"guitar_stem.wav"}}});
}

// Verifies TrackView maps clip timeline placement into child component bounds.
TEST_CASE("TrackView maps clip timeline range into row bounds", "[ui][track-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    TrackView view;
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        core::TimeRange{
            .start = core::TimePosition{},
            .end = core::TimePosition{10.0},
        });
    view.setBounds(0, 0, 100, 24);

    view.setState(
        TrackViewState{
            .track_id = core::TrackId{1},
            .display_name = "Full Mix",
            .audio_clips = {makeAudioClipViewState(
                std::filesystem::path{"full_mix.wav"},
                core::TimeRange{
                    .start = core::TimePosition{2.0},
                    .end = core::TimePosition{6.0},
                })},
        });

    juce::Component* const clip_view = view.findChildWithID("audio_clip_view");
    REQUIRE(clip_view != nullptr);
    CHECK(clip_view->getX() == 20);
    CHECK(clip_view->getY() == 0);
    CHECK(clip_view->getWidth() == 40);
    CHECK(clip_view->getHeight() == 24);
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
