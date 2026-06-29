#include "arrangement_view.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using RecordingThumbnail = common::audio::testing::RecordingThumbnail;
using RecordingThumbnailFactory = common::audio::testing::RecordingThumbnailFactory;
using testing::makeMouseDownEvent;

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

    // Last view instance that emitted a click.
    ArrangementView* last_view{nullptr};

    // Last normalized horizontal click position emitted by the view.
    std::optional<double> last_normalized_x{};

    // Number of click notifications received.
    int click_count{0};
};

// Builds arrangement-view state with full-source audio.
[[nodiscard]] core::ArrangementViewState makeArrangementState(
    std::filesystem::path path,
    common::core::TimeDuration duration = common::core::TimeDuration{4.0})
{
    return core::ArrangementViewState{
        .audio_asset =
            common::core::AudioAsset{.path = std::move(path), .normalization = std::nullopt},
        .audio_duration = duration,
    };
}

// Builds a simple one-measure 4/4 grid with linearly spaced beats.
[[nodiscard]] common::core::TempoMap makeOneMeasureTempoMap(double measure_seconds)
{
    return common::core::TempoMap{
        std::vector{
            common::core::TimeSignatureChange{
                .measure = 1,
                .numerator = 4,
                .denominator = 4,
            },
        },
        std::vector{
            common::core::BeatAnchor{
                .measure = 1,
                .beat = 1,
                .seconds = 0.0,
            },
            common::core::BeatAnchor{
                .measure = 2,
                .beat = 1,
                .seconds = measure_seconds,
            },
        },
    };
}

} // namespace

// Verifies assigned audio points the arrangement-owned thumbnail at the asset.
TEST_CASE("ArrangementView creates a thumbnail for audio", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(makeArrangementState(std::filesystem::path{"full_mix.wav"}));

    CHECK(thumbnail_factory.create_call_count == 1);
    CHECK(thumbnail_factory.last_owner == &view);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 1);
    CHECK(
        thumbnail->last_source ==
        std::optional{common::core::AudioAsset{
            .path = std::filesystem::path{"full_mix.wav"}, .normalization = std::nullopt
        }});
}

// Verifies reapplying the same audio state reuses the existing thumbnail source.
TEST_CASE("ArrangementView reuses thumbnail source", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
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
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setThumbnailFactory(thumbnail_factory);

    view.setState(makeArrangementState(std::filesystem::path{"full_mix.wav"}));
    view.setState(makeArrangementState(std::filesystem::path{"lead_override.wav"}));

    CHECK(thumbnail_factory.create_call_count == 1);
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    CHECK(thumbnail->set_source_call_count == 2);
    CHECK(
        thumbnail->last_source ==
        std::optional{common::core::AudioAsset{
            .path = std::filesystem::path{"lead_override.wav"}, .normalization = std::nullopt
        }});
}

// Verifies ArrangementView asks the thumbnail to draw only the visible asset range.
TEST_CASE("ArrangementView draws the visible waveform range", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
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
    RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
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

// Verifies tempo-map beats are visible over the arrangement row.
TEST_CASE("ArrangementView draws the tempo-map beat grid", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 101, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{4.0},
        });
    view.setTempoMap(makeOneMeasureTempoMap(4.0));
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"}, common::core::TimeDuration{4.0}));
    const juce::Image image = view.createComponentSnapshot(view.getLocalBounds());

    const float background = image.getPixelAt(12, 12).getBrightness();
    const float beat_line = image.getPixelAt(25, 12).getBrightness();
    const float measure_line = image.getPixelAt(0, 12).getBrightness();
    CHECK(beat_line > background);
    CHECK(measure_line > beat_line);
}

// Verifies audio shorter than the visible range is drawn into the matching view subset.
TEST_CASE("ArrangementView maps short audio into visible bounds", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
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
    RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(thumbnail->last_draw_bounds == std::optional{juce::Rectangle<int>{0, 0, 40, 24}});
}

// Verifies zoomed viewport repaints ask the thumbnail to draw only the clipped time slice.
TEST_CASE("ArrangementView clips waveform drawing to paint bounds", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 1024, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{8.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"}, common::core::TimeDuration{8.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 1024, 24, true);
    juce::Graphics graphics{image};
    REQUIRE(graphics.reduceClipRegion(juce::Rectangle<int>{256, 6, 128, 10}));

    view.paint(graphics);

    CHECK(
        thumbnail->last_drawn_visible_range == std::optional{common::core::TimeRange{
                                                   .start = common::core::TimePosition{2.0},
                                                   .end = common::core::TimePosition{3.0},
                                               }});
    CHECK(thumbnail->last_draw_bounds == std::optional{juce::Rectangle<int>{256, 0, 128, 24}});
}

// Verifies clipped repaints outside shorter audio do not stretch the waveform into empty space.
TEST_CASE("ArrangementView skips off-audio paint clips", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 1024, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{8.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"}, common::core::TimeDuration{4.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 1024, 24, true);
    juce::Graphics graphics{image};
    REQUIRE(graphics.reduceClipRegion(juce::Rectangle<int>{640, 0, 128, 24}));

    view.paint(graphics);

    CHECK_FALSE(thumbnail->last_drawn_visible_range.has_value());
    CHECK_FALSE(thumbnail->last_draw_bounds.has_value());
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
