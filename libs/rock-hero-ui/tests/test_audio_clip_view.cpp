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
        set_source_call_count += 1;
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

    // Reports a loaded thumbnail so paint would draw rather than show the loading branch.
    [[nodiscard]] double getLength() const override
    {
        return 1.0;
    }

    // Ignores drawing because these tests only need observable source refresh behavior.
    void drawChannels(
        juce::Graphics& /*g*/, juce::Rectangle<int> /*bounds*/, float /*vertical_zoom*/) override
    {}

    // Last source installed into this thumbnail, if any.
    std::optional<core::AudioAsset> last_source{};

    // Number of times the view has refreshed this thumbnail's source.
    int set_source_call_count{0};
};

// Builds one clip state with matching source and timeline ranges for source-refresh tests.
[[nodiscard]] AudioClipViewState makeAudioClipViewState(std::filesystem::path path)
{
    const core::TimeRange clip_range{
        .start = core::TimePosition{},
        .end = core::TimePosition{4.0},
    };

    return AudioClipViewState{
        .audio_clip_id = core::AudioClipId{1},
        .asset = core::AudioAsset{std::move(path)},
        .source_range = clip_range,
        .timeline_range = clip_range,
    };
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

} // namespace rock_hero::ui
