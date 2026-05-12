#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/i_thumbnail.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/editor/ui/arrangement_view.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

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

// Records thumbnail source refreshes and draw requests from the arrangement view.
class FakeThumbnail final : public common::audio::IThumbnail
{
public:
    // Captures the new source each time the view asks the thumbnail to refresh itself.
    void setSource(const common::core::AudioAsset& audio_asset) override
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
        juce::Graphics& /*g*/, juce::Rectangle<int> bounds, common::core::TimeRange visible_range,
        float vertical_zoom) override
    {
        last_draw_bounds = bounds;
        last_drawn_visible_range = visible_range;
        last_vertical_zoom = vertical_zoom;
        return draw_result;
    }

    std::optional<common::core::AudioAsset> last_source{};
    std::optional<common::core::TimeRange> last_drawn_visible_range{};
    std::optional<juce::Rectangle<int>> last_draw_bounds{};
    std::optional<float> last_vertical_zoom{};
    int set_source_call_count{0};
    bool generating_proxy{false};
    float proxy_progress{0.0f};
    bool has_source{false};
    bool draw_result{true};
};

// Creates fake thumbnails while recording the component that requested one.
class FakeThumbnailFactory final : public common::audio::IThumbnailFactory
{
public:
    [[nodiscard]] std::unique_ptr<common::audio::IThumbnail> createThumbnail(
        juce::Component& owner) override
    {
        last_owner = &owner;
        create_call_count += 1;
        auto thumbnail = std::make_unique<FakeThumbnail>();
        thumbnails.push_back(thumbnail.get());
        return thumbnail;
    }

    juce::Component* last_owner{nullptr};
    std::vector<FakeThumbnail*> thumbnails{};
    int create_call_count{0};
};

// Records normalized click intent emitted by the arrangement view.
class FakeArrangementViewListener final : public ArrangementView::Listener
{
public:
    // Stores the view pointer and normalized position reported by the component under test.
    void arrangementViewClicked(ArrangementView& view, double normalized_x) override
    {
        last_view = &view;
        last_normalized_x = normalized_x;
        click_count += 1;
    }

    ArrangementView* last_view{nullptr};
    std::optional<double> last_normalized_x{};
    int click_count{0};
};

// Builds arrangement-view state with full-source audio.
[[nodiscard]] core::ArrangementViewState makeArrangementState(
    std::filesystem::path path,
    common::core::TimeDuration duration = common::core::TimeDuration{4.0})
{
    return core::ArrangementViewState{
        .audio_asset = common::core::AudioAsset{std::move(path)},
        .audio_duration = duration,
    };
}

} // namespace

// Verifies assigned audio points the arrangement-owned thumbnail at the asset.
TEST_CASE("ArrangementView creates a thumbnail for audio", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(makeArrangementState(std::filesystem::path{"full_mix.wav"}));

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_owner == &view);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 1);
    CHECK(
        thumbnail->last_source ==
        std::optional{common::core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies reapplying the same audio state reuses the existing thumbnail source.
TEST_CASE("ArrangementView reuses thumbnail source", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setThumbnailFactory(thumbnail_factory);

    const core::ArrangementViewState state =
        makeArrangementState(std::filesystem::path{"full_mix.wav"});

    view.setState(state);
    view.setState(state);

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    CHECK(thumbnail_factory.thumbnails.front()->set_source_call_count == 1);
}

// Verifies changing the arrangement asset refreshes the existing thumbnail.
TEST_CASE("ArrangementView refreshes thumbnail when asset changes", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(makeArrangementState(std::filesystem::path{"full_mix.wav"}));
    view.setState(makeArrangementState(std::filesystem::path{"lead_override.wav"}));

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 2);
    CHECK(
        thumbnail->last_source ==
        std::optional{common::core::AudioAsset{std::filesystem::path{"lead_override.wav"}}});
}

// Verifies ArrangementView asks the thumbnail to draw only the visible asset range.
TEST_CASE("ArrangementView draws the visible waveform range", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{2.0},
            .end = common::core::TimePosition{6.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"}, common::core::TimeDuration{10.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(
        thumbnail->last_drawn_visible_range == std::optional{common::core::TimeRange{
                                                   .start = common::core::TimePosition{2.0},
                                                   .end = common::core::TimePosition{6.0},
                                               }});
    CHECK(thumbnail->last_draw_bounds == std::optional{image.getBounds()});
    CHECK(thumbnail->last_vertical_zoom == std::optional{1.0f});
}

// Verifies audio shorter than the visible range is drawn into the matching view subset.
TEST_CASE("ArrangementView maps short audio into visible bounds", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    FakeThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{10.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"}, common::core::TimeDuration{4.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    FakeThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(thumbnail->last_draw_bounds == std::optional{juce::Rectangle<int>{0, 0, 40, 24}});
}

// Verifies local hit testing emits a normalized horizontal click position.
TEST_CASE("ArrangementView reports normalized click position", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    ArrangementView view;
    FakeArrangementViewListener listener;

    view.addListener(listener);
    view.setBounds(0, 0, 253, 40);

    const float click_x = std::floor(static_cast<float>(view.getWidth()) * 0.25f) + 0.5f;
    view.mouseDown(makeMouseDownEvent(view, click_x, 10.0f));

    CHECK(listener.click_count == 1);
    CHECK(listener.last_view == &view);
    REQUIRE(listener.last_normalized_x.has_value());
    if (listener.last_normalized_x.has_value())
    {
        const double expected_normalized_x =
            static_cast<double>(click_x) / static_cast<double>(view.getWidth());
        CHECK(listener.last_normalized_x.value() == Catch::Approx(expected_normalized_x));
    }
}

} // namespace rock_hero::editor::ui
