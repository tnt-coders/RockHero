#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/audio/i_thumbnail.h>
#include <rock_hero/ui/audio_clip_view.h>
#include <utility>

namespace rock_hero::ui
{

namespace
{

// Records source refreshes so tests can verify clip-local thumbnail asset behavior.
class FakeThumbnail final : public audio::IThumbnail
{
public:
    // Captures the new source each time the clip view asks the thumbnail to refresh itself.
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

    // Reports that this fake never has background proxy work in progress.
    [[nodiscard]] bool isGeneratingProxy() const override
    {
        return false;
    }

    // Reports inert proxy progress because generation is never active in these tests.
    [[nodiscard]] float getProxyProgress() const override
    {
        return 0.0f;
    }

    // Records the requested source range so paint behavior can be observed.
    [[nodiscard]] bool drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, core::TimeRange source_range,
        float /*vertical_zoom*/) override
    {
        last_drawn_source_range = source_range;
        draw_call_count += 1;
        return draw_result;
    }

    // Last source installed into this thumbnail, if any.
    std::optional<core::AudioAsset> last_source{};

    // Last source range requested for drawing, if any.
    std::optional<core::TimeRange> last_drawn_source_range{};

    // Number of times the view has refreshed this thumbnail's source.
    int set_source_call_count{0};

    // Number of draw requests observed.
    int draw_call_count{0};

    // Fake source-readiness flag.
    bool has_source{false};

    // Result returned by drawChannels().
    bool draw_result{true};
};

// Builds one clip state with matching source and timeline ranges for source-refresh tests.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(
    std::filesystem::path path, core::TimeRange source_range)
{
    return AudioClipViewState{
        .audio_clip_id = core::AudioClipId{1},
        .asset = core::AudioAsset{std::move(path)},
        .source_range = source_range,
        .timeline_range = core::TimeRange{
            .start = core::TimePosition{},
            .end = core::TimePosition{source_range.duration().seconds},
        },
    };
}

// Builds one default clip state for tests that do not care about trimming.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(std::filesystem::path path)
{
    return makeAudioClipViewState(
        std::move(path),
        core::TimeRange{
            .start = core::TimePosition{},
            .end = core::TimePosition{4.0},
        });
}

} // namespace

// Verifies state can arrive before the thumbnail without losing the clip asset.
TEST_CASE("AudioClipView applies current state when thumbnail arrives", "[ui][audio-clip-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    AudioClipView view;
    view.setState(makeAudioClipViewState(std::filesystem::path{"full_mix.wav"}));

    auto thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));

    CHECK(thumbnail_ptr->set_source_call_count == 1);
    CHECK(
        thumbnail_ptr->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies repeated state snapshots for the same asset do not churn the thumbnail backend.
TEST_CASE("AudioClipView avoids refreshing thumbnail for same asset", "[ui][audio-clip-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    AudioClipView view;
    const auto state = makeAudioClipViewState(std::filesystem::path{"full_mix.wav"});
    view.setState(state);

    auto thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));
    view.setState(state);

    CHECK(thumbnail_ptr->set_source_call_count == 1);
}

// Verifies a clip asset change refreshes the existing thumbnail instead of replacing the view.
TEST_CASE("AudioClipView refreshes thumbnail when asset changes", "[ui][audio-clip-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    AudioClipView view;
    view.setState(makeAudioClipViewState(std::filesystem::path{"full_mix.wav"}));

    auto thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));
    view.setState(makeAudioClipViewState(std::filesystem::path{"guitar_stem.wav"}));

    CHECK(thumbnail_ptr->set_source_call_count == 2);
    CHECK(
        thumbnail_ptr->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"guitar_stem.wav"}}});
}

// Verifies replacing the thumbnail still points the new backend at the current clip asset.
TEST_CASE("AudioClipView applies current state to replacement thumbnail", "[ui][audio-clip-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    AudioClipView view;
    view.setState(makeAudioClipViewState(std::filesystem::path{"full_mix.wav"}));

    auto first_thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const first_thumbnail_ptr = first_thumbnail.get();
    view.setThumbnail(std::move(first_thumbnail));
    CHECK(first_thumbnail_ptr->set_source_call_count == 1);

    auto replacement_thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const replacement_thumbnail_ptr = replacement_thumbnail.get();
    view.setThumbnail(std::move(replacement_thumbnail));

    CHECK(replacement_thumbnail_ptr->set_source_call_count == 1);
    CHECK(
        replacement_thumbnail_ptr->last_source ==
        std::optional{core::AudioAsset{std::filesystem::path{"full_mix.wav"}}});
}

// Verifies painting uses the clip source range rather than the full source asset duration.
TEST_CASE("AudioClipView draws the clip source range", "[ui][audio-clip-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    AudioClipView view;
    view.setBounds(0, 0, 128, 48);
    const core::TimeRange source_range{
        .start = core::TimePosition{2.0},
        .end = core::TimePosition{6.0},
    };
    view.setState(makeAudioClipViewState(std::filesystem::path{"full_mix.wav"}, source_range));

    auto thumbnail = std::make_unique<FakeThumbnail>();
    const FakeThumbnail* const thumbnail_ptr = thumbnail.get();
    view.setThumbnail(std::move(thumbnail));

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    view.paint(graphics);

    CHECK(thumbnail_ptr->draw_call_count == 1);
    CHECK(thumbnail_ptr->last_drawn_source_range == std::optional{source_range});
}

} // namespace rock_hero::ui
