#include "tracktion/engine_behaviors.h"
#include "tracktion/tempo_mirror.h"
#include "tracktion/tone_automation_curve.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Owns the Tracktion objects needed to exercise the tempo mirror against a real edit headlessly.
// RockHeroEngineBehavior matters here: it pins the edit's beat unit to quarter notes, which the
// mirror's quarter-position math relies on.
struct TempoMirrorHarness
{
    juce::ScopedJuceInitialiser_GUI gui;
    tracktion::Engine engine{
        "RockHeroTempoMirrorTest",
        nullptr,
        std::make_unique<RockHeroEngineBehavior>(),
    };
    std::unique_ptr<tracktion::Edit> edit{tracktion::Edit::createSingleTrackEdit(engine)};
};

// 4/4 map holding 120 quarter-note BPM across one anchored measure.
[[nodiscard]] core::TempoMap makeSteadyMap()
{
    return core::TempoMap{
        {core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {
            core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            core::BeatAnchor{.measure = 2, .beat = 1, .seconds = 2.0},
        },
    };
}

// 4/4 then 6/8 map with a tempo change at the meter change: 120 BPM over measure 1 (4 quarters in
// 2 s), then 180 BPM over measure 2 (6/8 = 3 quarters in 1 s).
[[nodiscard]] core::TempoMap makeMeterChangeMap()
{
    return core::TempoMap{
        {
            core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4},
            core::TimeSignatureChange{.measure = 2, .numerator = 6, .denominator = 8},
        },
        {
            core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            core::BeatAnchor{.measure = 2, .beat = 1, .seconds = 2.0},
            core::BeatAnchor{.measure = 3, .beat = 1, .seconds = 3.0},
        },
    };
}

} // namespace

TEST_CASE("Tempo mirror writes one flat step per anchor span", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    mirrorTempoMapIntoSequence(sequence, makeMeterChangeMap());

    REQUIRE(sequence.getNumTempos() == 2);
    const tracktion::TempoSetting* const first = sequence.getTempo(0);
    const tracktion::TempoSetting* const second = sequence.getTempo(1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->getStartBeat().inBeats() == Catch::Approx(0.0));
    CHECK(first->getBpm() == Catch::Approx(120.0));
    CHECK(first->getCurve() == Catch::Approx(1.0));
    CHECK(second->getStartBeat().inBeats() == Catch::Approx(4.0));
    CHECK(second->getBpm() == Catch::Approx(180.0));
    CHECK(second->getCurve() == Catch::Approx(1.0));

    REQUIRE(sequence.getNumTimeSigs() == 2);
    const tracktion::TimeSigSetting* const first_sig = sequence.getTimeSig(0);
    const tracktion::TimeSigSetting* const second_sig = sequence.getTimeSig(1);
    REQUIRE(first_sig != nullptr);
    REQUIRE(second_sig != nullptr);
    CHECK(first_sig->getStartBeat().inBeats() == Catch::Approx(0.0));
    CHECK(first_sig->numerator.get() == 4);
    CHECK(first_sig->denominator.get() == 4);
    CHECK(second_sig->getStartBeat().inBeats() == Catch::Approx(4.0));
    CHECK(second_sig->numerator.get() == 6);
    CHECK(second_sig->denominator.get() == 8);

    // The rebuilt sequence must reproduce the map's seconds at every anchor and inside spans.
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(4.0)).inSeconds() == Catch::Approx(2.0));
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(7.0)).inSeconds() == Catch::Approx(3.0));
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(5.5)).inSeconds() == Catch::Approx(2.5));
}

TEST_CASE("Tempo mirror preserves BPM beyond the official clamp", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    // Four quarters in half a second: 480 quarter-note BPM, well past Tracktion's 300 BPM
    // mutator clamp. Real charts exceed 300 BPM, so clamping would corrupt the mirror.
    const core::TempoMap map{
        {core::TimeSignatureChange{.measure = 1, .numerator = 4, .denominator = 4}},
        {
            core::BeatAnchor{.measure = 1, .beat = 1, .seconds = 0.0},
            core::BeatAnchor{.measure = 2, .beat = 1, .seconds = 0.5},
        },
    };
    mirrorTempoMapIntoSequence(sequence, map);

    REQUIRE(sequence.getNumTempos() == 1);
    const tracktion::TempoSetting* const tempo = sequence.getTempo(0);
    REQUIRE(tempo != nullptr);
    CHECK(tempo->getBpm() == Catch::Approx(480.0));
}

TEST_CASE("Tempo mirror prunes stale entries on re-mirror", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    mirrorTempoMapIntoSequence(sequence, makeMeterChangeMap());
    REQUIRE(sequence.getNumTempos() == 2);
    REQUIRE(sequence.getNumTimeSigs() == 2);

    mirrorTempoMapIntoSequence(sequence, makeSteadyMap());

    REQUIRE(sequence.getNumTempos() == 1);
    REQUIRE(sequence.getNumTimeSigs() == 1);
    const tracktion::TempoSetting* const tempo = sequence.getTempo(0);
    const tracktion::TimeSigSetting* const signature = sequence.getTimeSig(0);
    REQUIRE(tempo != nullptr);
    REQUIRE(signature != nullptr);
    CHECK(tempo->getStartBeat().inBeats() == Catch::Approx(0.0));
    CHECK(tempo->getBpm() == Catch::Approx(120.0));
    CHECK(signature->numerator.get() == 4);
    CHECK(signature->denominator.get() == 4);
}

TEST_CASE("Tempo mirror leaves maps without tempo information alone", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;
    const double default_bpm = sequence.getTempo(0)->getBpm();

    mirrorTempoMapIntoSequence(sequence, core::TempoMap{{}, {}});

    REQUIRE(sequence.getNumTempos() == 1);
    CHECK(sequence.getTempo(0)->getBpm() == Catch::Approx(default_bpm));
}

TEST_CASE(
    "Tempo mirror never moves absolute-synced clips or seconds-based automation",
    "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;

    // Backing clip stand-in, placed off-origin like a start-offset asset and pinned to absolute
    // seconds exactly as the production song-audio path pins it.
    const std::filesystem::path fixture = std::filesystem::path{TEST_DATA_DIR} / "drum_loop.wav";
    REQUIRE(std::filesystem::exists(fixture));
    const juce::File fixture_file{juce::String{fixture.string()}};
    const auto audio_tracks = tracktion::getAudioTracks(*harness.edit);
    REQUIRE_FALSE(audio_tracks.isEmpty());
    tracktion::AudioTrack* const track = audio_tracks.getFirst();
    const auto clip_start = tracktion::TimePosition::fromSeconds(0.75);
    const tracktion::ClipPosition clip_position{
        .time = {clip_start, clip_start + tracktion::TimeDuration::fromSeconds(2.0)},
        .offset = tracktion::TimeDuration{},
    };
    const auto clip = track->insertWaveClip("backing", fixture_file, clip_position, false);
    REQUIRE(clip != nullptr);
    clip->setSyncType(tracktion::Clip::syncAbsolute);
    // Insertion may normalize the requested range against the source length, so capture the
    // settled placement; the invariant under test is stability across the mirror.
    const double start_before = clip->getPosition().getStart().inSeconds();
    const double length_before = clip->getPosition().getLength().inSeconds();
    CHECK(start_before == Catch::Approx(0.75));

    // Chain-plugin stand-in on the same track carrying a derived seconds curve.
    const tracktion::Plugin::Ptr plugin = harness.edit->getPluginCache().createNewPlugin(
        tracktion::VolumeAndPanPlugin::xmlTypeName, {});
    REQUIRE(plugin != nullptr);
    track->pluginList.insertPlugin(plugin, 0, nullptr);
    const std::vector<tracktion::Plugin::Ptr> chain{plugin};
    const std::vector<AutomatableParamInfo> parameters = listChainAutomatableParameters(chain);
    REQUIRE_FALSE(parameters.empty());
    const std::string param_id = parameters.front().param_id;
    const std::vector<AutomationCurvePoint> points{
        AutomationCurvePoint{.seconds = 0.5, .norm_value = 0.25F, .curve_shape = 0.0F},
        AutomationCurvePoint{.seconds = 2.5, .norm_value = 0.75F, .curve_shape = 0.0F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));

    mirrorTempoMapIntoSequence(harness.edit->tempoSequence, makeMeterChangeMap());
    // The sequence delivers its tempo-changed clip notifications through an async update; force
    // them synchronously (headless tests pump no message loop) so clip reactions actually run
    // before the stability assertions below.
    harness.edit->tempoSequence.updateTempoData();
    harness.edit->sendTempoOrPitchSequenceChangedUpdates();

    CHECK(clip->getPosition().getStart().inSeconds() == Catch::Approx(start_before));
    CHECK(clip->getPosition().getLength().inSeconds() == Catch::Approx(length_before));
    const std::optional<std::vector<AutomationCurvePoint>> read_back =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(read_back.has_value());
    if (read_back.has_value())
    {
        REQUIRE(read_back->size() == points.size());
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            CHECK((*read_back)[index].seconds == Catch::Approx(points[index].seconds));
            CHECK((*read_back)[index].norm_value == Catch::Approx(points[index].norm_value));
        }
    }
}

} // namespace rock_hero::common::audio
