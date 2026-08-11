#include "timeline/arrangement_view.h"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/testing/recording_thumbnail.h>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

using RecordingThumbnail = common::audio::testing::RecordingThumbnail;
using RecordingThumbnailFactory = common::audio::testing::RecordingThumbnailFactory;

// Builds arrangement-view state with full-source audio and an optional timeline start offset.
[[nodiscard]] core::ArrangementViewState makeArrangementState(
    std::filesystem::path path,
    common::core::TimeDuration duration = common::core::TimeDuration{4.0},
    common::core::TimeDuration start_offset = {})
{
    return core::ArrangementViewState{
        .audio_asset =
            common::core::AudioAsset{
                .path = std::move(path), .normalization = std::nullopt, .start_offset = start_offset
            },
        .audio_duration = duration,
        .choices = {},
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
        thumbnail->last_source == std::optional{common::core::AudioAsset{
                                      .path = std::filesystem::path{"full_mix.wav"},
                                      .normalization = std::nullopt,
                                      .start_offset = {},
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
        thumbnail->last_source == std::optional{common::core::AudioAsset{
                                      .path = std::filesystem::path{"lead_override.wav"},
                                      .normalization = std::nullopt,
                                      .start_offset = {},
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

// Verifies a positive audio start offset shifts the waveform later on the timeline: the thumbnail
// is asked for a source range shifted back by the offset (source time starts at zero) while the
// drawn bounds stay under the timeline window, so the waveform lands beneath the audio it plays.
TEST_CASE(
    "ArrangementView offsets the waveform by the audio start offset", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    // Audio starts 2s in and runs 10s, occupying timeline [2, 12]. The visible window [2, 6] is
    // fully inside it, so the whole width draws the source's first four seconds.
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{2.0},
            .end = common::core::TimePosition{6.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"},
        common::core::TimeDuration{10.0},
        common::core::TimeDuration{2.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    CHECK(
        thumbnail->last_drawn_visible_range == std::optional{common::core::TimeRange{
                                                   .start = common::core::TimePosition{0.0},
                                                   .end = common::core::TimePosition{4.0},
                                               }});
    CHECK(thumbnail->last_draw_bounds == std::optional{image.getBounds()});
}

// Verifies the timeline start offset pushes the beginning of a short audio clip to the right,
// leaving silent space before the waveform.
TEST_CASE("ArrangementView leaves a gap before offset audio", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    // Audio starts 2s in and runs 4s, occupying timeline [2, 6] of the visible [0, 10] window.
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{10.0},
        });
    view.setState(makeArrangementState(
        std::filesystem::path{"full_mix.wav"},
        common::core::TimeDuration{4.0},
        common::core::TimeDuration{2.0}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);
    juce::Graphics graphics{image};

    view.paint(graphics);

    // Timeline [2, 6] of a 100px [0, 10] window is the 20..60 px band; the source draws its full
    // [0, 4] seconds there.
    CHECK(thumbnail->last_draw_bounds == std::optional{juce::Rectangle<int>{20, 0, 40, 24}});
    CHECK(
        thumbnail->last_drawn_visible_range == std::optional{common::core::TimeRange{
                                                   .start = common::core::TimePosition{0.0},
                                                   .end = common::core::TimePosition{4.0},
                                               }});
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

// Verifies hiding the waveform suppresses thumbnail drawing until it is re-shown.
TEST_CASE("ArrangementView hides the waveform behind the tablature lane", "[ui][arrangement-view]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingThumbnailFactory thumbnail_factory;
    ArrangementView view;
    view.setBounds(0, 0, 100, 24);
    view.setThumbnailFactory(thumbnail_factory);
    view.setVisibleTimeline(
        common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{4.0},
        });
    view.setState(makeArrangementState(std::filesystem::path{"full_mix.wav"}));
    REQUIRE(thumbnail_factory.thumbnails.size() == 1);
    const RecordingThumbnail* const thumbnail = thumbnail_factory.thumbnails.front();
    const juce::Image image(juce::Image::RGB, 100, 24, true);

    view.setWaveformVisible(false);
    {
        juce::Graphics graphics{image};
        view.paint(graphics);
    }
    CHECK_FALSE(thumbnail->last_drawn_visible_range.has_value());

    view.setWaveformVisible(true);
    {
        juce::Graphics graphics{image};
        view.paint(graphics);
    }
    CHECK(thumbnail->last_drawn_visible_range.has_value());
}

} // namespace rock_hero::editor::ui
