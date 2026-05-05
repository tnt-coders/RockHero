#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/audio/engine.h>
#include <rock_hero/audio/i_thumbnail.h>

namespace rock_hero::audio
{

namespace
{

// Verifies at compile time that the concrete adapter is usable through its audio port surfaces.
static_assert(std::derived_from<Engine, ITransport>);
static_assert(std::derived_from<Engine, IEdit>);
static_assert(std::derived_from<Engine, IThumbnailFactory>);

// Returns the build-tree copy of the audio fixture that the real Engine loads in tests.
[[nodiscard]] std::filesystem::path fixtureAudioPath()
{
    return std::filesystem::path{TEST_DATA_DIR} / "drum_loop.wav";
}

// Wraps the real fixture path in the framework-free asset type used by IEdit.
[[nodiscard]] core::AudioAsset fixtureAudioAsset()
{
    return core::AudioAsset{fixtureAudioPath()};
}

// Converts an accepted duration into the timeline range used by rendering and seeks.
[[nodiscard]] core::TimeRange timelineRangeForDuration(core::TimeDuration duration)
{
    return core::TimeRange{
        .start = core::TimePosition{},
        .end = core::TimePosition{duration.seconds},
    };
}

// Loads the fixture through the public edit port and returns the accepted duration.
[[nodiscard]] std::optional<core::TimeDuration> loadFixtureAudio(IEdit& edit)
{
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    return edit.loadAudio(audio_asset);
}

// Loads the fixture and fails the current test if the concrete backend rejects it.
[[nodiscard]] core::TimeDuration requireLoadedFixtureAudio(IEdit& edit)
{
    const auto duration = loadFixtureAudio(edit);
    REQUIRE(duration.has_value());
    return duration.value_or(core::TimeDuration{});
}

// Records callback activity from the project-owned transport listener surface.
class TransportNotificationRecorder final : public ITransport::Listener
{
public:
    void onTransportStateChanged(TransportState state) override
    {
        last_transport_state = state;
        ++transport_state_call_count;
    }

    TransportState last_transport_state{};
    int transport_state_call_count{0};
};

// Owns the JUCE runtime guard before constructing the real Tracktion-backed engine.
class EngineTestHarness final
{
public:
    // Creates JUCE's message-manager lifetime needed by Tracktion timers and callbacks in tests.
    juce::ScopedJuceInitialiser_GUI scoped_gui;

    // Real adapter under test; destroyed before scoped_gui because members tear down in reverse.
    Engine engine;
};

} // namespace

// Verifies the concrete engine starts with empty state and a zero live position.
TEST_CASE("Engine starts with empty transport state", "[audio][engine][integration]")
{
    const EngineTestHarness harness;
    const Engine& engine = harness.engine;
    ITransport const& transport = engine;

    const auto current_state = transport.state();

    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
}

// Verifies the concrete engine factory returns a usable Tracktion-backed thumbnail adapter.
TEST_CASE("Engine thumbnail factory creates an adapter", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;

    const auto thumbnail = engine.createThumbnail(owner);

    REQUIRE(thumbnail != nullptr);
    CHECK_FALSE(thumbnail->hasSource());
}

// Verifies a real thumbnail adapter can load fixture metadata and render into JUCE graphics.
TEST_CASE("Engine thumbnail loads and draws fixture audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    const core::TimeDuration audio_duration = requireLoadedFixtureAudio(engine);
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);

    thumbnail->setSource(audio_asset);

    CHECK(thumbnail->hasSource());
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    CHECK(thumbnail->drawChannels(
        graphics, image.getBounds(), timelineRangeForDuration(audio_duration), 1.0f));
}

// Verifies invalid visible ranges fail before the adapter asks Tracktion to draw them.
TEST_CASE("Engine thumbnail rejects invalid visible ranges", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);
    thumbnail->setSource(audio_asset);
    REQUIRE(thumbnail->hasSource());

    const juce::Image image(juce::Image::RGB, 128, 48, true);
    juce::Graphics graphics{image};
    CHECK_FALSE(thumbnail->drawChannels(
        graphics,
        image.getBounds(),
        core::TimeRange{
            .start = core::TimePosition{4.0},
            .end = core::TimePosition{2.0},
        },
        1.0f));
}

// Verifies missing assets do not leave stale source-readiness state in the thumbnail adapter.
TEST_CASE("Engine thumbnail clears source for missing assets", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    juce::Component owner;
    auto thumbnail = engine.createThumbnail(owner);
    REQUIRE(thumbnail != nullptr);
    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));
    thumbnail->setSource(audio_asset);
    REQUIRE(thumbnail->hasSource());

    const core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing-thumbnail.wav",
    };
    thumbnail->setSource(missing_asset);

    CHECK_FALSE(thumbnail->hasSource());
    CHECK(thumbnail->getProxyProgress() >= 0.0f);
    CHECK(thumbnail->getProxyProgress() <= 1.0f);
}

// Verifies audio loading returns a duration while leaving transport stopped.
TEST_CASE("Engine edit loads audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const auto audio_asset = fixtureAudioAsset();
    REQUIRE(std::filesystem::exists(audio_asset.path));

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto audio_duration = edit.loadAudio(audio_asset);

    transport.removeListener(recorder);

    REQUIRE(audio_duration.has_value());
    CHECK(audio_duration->seconds > 0.0);
    const auto current_state = transport.state();
    CHECK_FALSE(current_state.playing);
    CHECK(transport.position() == core::TimePosition{});
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies the single Tracktion arrangement track can replace its loaded audio.
TEST_CASE("Engine edit replaces arrangement audio", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;

    const auto first_audio = loadFixtureAudio(edit);
    const auto replacement_audio = loadFixtureAudio(edit);

    CHECK(first_audio.has_value());
    CHECK(replacement_audio.has_value());
}

// Verifies a failed edit request leaves the previous transport state visible through state().
TEST_CASE("Failed engine edit preserves existing transport state", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    [[maybe_unused]] const auto audio_duration = requireLoadedFixtureAudio(edit);

    const auto loaded_state = transport.state();
    const core::AudioAsset missing_asset{
        fixtureAudioPath().parent_path() / "missing.wav",
    };
    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const auto loaded_audio = edit.loadAudio(missing_asset);

    transport.removeListener(recorder);

    CHECK_FALSE(loaded_audio.has_value());
    CHECK(transport.state() == loaded_state);
    CHECK(recorder.transport_state_call_count == 0);
    CHECK(recorder.last_transport_state == TransportState{});
}

// Verifies direct ITransport seek commands update the live position without mutating state.
TEST_CASE("Engine seek updates live transport position", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const core::TimeDuration audio_duration = requireLoadedFixtureAudio(edit);

    const auto loaded_state = transport.state();
    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.25, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(transport.state() == loaded_state);
    CHECK(transport.position() == core::TimePosition{target_seconds});
}

// Verifies the cursor-position read reports the post-seek position from the concrete adapter.
TEST_CASE("Engine position reflects public transport seeks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;
    const ITransport& read_only_transport = engine;

    const core::TimeDuration audio_duration = requireLoadedFixtureAudio(edit);

    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    const double target_seconds = std::min(0.3, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(read_only_transport.position() == core::TimePosition{target_seconds});
}

// Verifies position-only seeking remains invisible to coarse transport state listeners.
TEST_CASE("Engine seek does not emit state callbacks", "[audio][engine][integration]")
{
    EngineTestHarness harness;
    Engine& engine = harness.engine;
    IEdit& edit = engine;
    ITransport& transport = engine;

    const core::TimeDuration audio_duration = requireLoadedFixtureAudio(edit);

    const double duration_seconds = audio_duration.seconds;
    REQUIRE(duration_seconds > 0.0);

    TransportNotificationRecorder recorder;
    transport.addListener(recorder);

    const double target_seconds = std::min(0.2, duration_seconds * 0.5);
    transport.seek(core::TimePosition{target_seconds});

    CHECK(recorder.transport_state_call_count == 0);
    CHECK(transport.position() == core::TimePosition{target_seconds});

    transport.removeListener(recorder);
}

} // namespace rock_hero::audio
