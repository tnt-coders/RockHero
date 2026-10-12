#include "tracktion/engine_behaviors.h"
#include "tracktion/tempo_mirror.h"
#include "tracktion/tone_automation_curve.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

using rock_hero::common::core::testing::anchor;

// Owns the Tracktion objects needed to exercise the tempo mirror against a real edit headlessly,
// under the application's own engine behaviour.
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
    return core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("2:1", 2000),
    });
}

// 4/4 then 6/8 map with a tempo change at the meter change: 120 BPM over measure 1 (4 quarters in
// 2 s), then 180 BPM over measure 2 (6/8 = 3 quarters in 1 s).
[[nodiscard]] core::TempoMap makeMeterChangeMap()
{
    return core::TempoMap{
        core::testing::metersAt(
            {{.at = "1:1", .numerator = 4, .denominator = 4},
             {.at = "2:1", .numerator = 6, .denominator = 8}}),
        {
            anchor("1:1", 0),
            anchor("2:1", 2000),
            anchor("3:1", 3000),
        },
    };
}

// What the host hands a plugin at a time: the bar and beat it counts, and the song position in
// quarter notes (ppq) with the quarter at which the bar began.
struct HostPosition
{
    int bar{};
    double beat{};
    double ppq{};
    double ppq_of_bar_start{};
};

[[nodiscard]] HostPosition hostPositionAt(
    const tracktion::TempoSequence& sequence, const double seconds)
{
    const tracktion::TimePosition time = tracktion::TimePosition::fromSeconds(seconds);
    const tracktion::tempo::BarsAndBeats bars_beats = sequence.toBarsAndBeats(time);
    tracktion::tempo::Sequence::Position position{sequence.getInternalSequence()};
    position.set(time);
    return HostPosition{
        .bar = bars_beats.bars,
        .beat = bars_beats.beats.inBeats(),
        .ppq = position.getPPQTime(),
        .ppq_of_bar_start = position.getPPQTimeOfBarStart(),
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

    // The rebuilt sequence must reproduce the map's seconds at every anchor and inside spans. A
    // beat of the 6/8 bar is an eighth, so measure 3 is beat 4 + 6 and its middle beat 4 + 3.
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(4.0)).inSeconds() == Catch::Approx(2.0));
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(10.0)).inSeconds() ==
        Catch::Approx(3.0));
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(7.0)).inSeconds() == Catch::Approx(2.5));
}

TEST_CASE("Tempo mirror steps the tempo at a sub-beat anchor", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    // Two and a half quarters in the first second, one and a half in the next: the step lands
    // between beats 3 and 4, not on the beat the anchor's position floors to.
    mirrorTempoMapIntoSequence(
        sequence,
        core::testing::commonTimeMap({
            anchor("1:1", 0),
            anchor("1:3+1/2", 1000),
            anchor("2:1", 2000),
        }));

    REQUIRE(sequence.getNumTempos() == 2);
    const tracktion::TempoSetting* const first = sequence.getTempo(0);
    const tracktion::TempoSetting* const second = sequence.getTempo(1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->getBpm() == Catch::Approx(150.0));
    CHECK(second->getStartBeat().inBeats() == Catch::Approx(2.5));
    CHECK(second->getBpm() == Catch::Approx(90.0));
    CHECK(
        sequence.toTime(tracktion::BeatPosition::fromBeats(2.5)).inSeconds() == Catch::Approx(1.0));
}

TEST_CASE("Tempo mirror preserves BPM beyond the official clamp", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    // Four quarters in half a second: 480 quarter-note BPM, well past Tracktion's 300 BPM
    // mutator clamp. Real charts exceed 300 BPM, so clamping would corrupt the mirror.
    const core::TempoMap map = core::testing::commonTimeMap({
        anchor("1:1", 0),
        anchor("2:1", 500),
    });
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

// A meter placed mid-measure cuts the bar before it short, and the derived short bar reaches the
// host as a time signature of its own: 4/4 then 7/8 from beat 2 of measure 2 is a 1/4 bar on
// quarter 4, the 7/8 from quarter 5.
TEST_CASE("Tempo mirror writes a derived short bar as a time signature", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    mirrorTempoMapIntoSequence(
        sequence,
        core::TempoMap{
            core::testing::metersAt(
                {{.at = "1:1", .numerator = 4, .denominator = 4},
                 {.at = "2:2", .numerator = 7, .denominator = 8}}),
            {anchor("1:1", 0), anchor("4:1", 4250)},
        });

    REQUIRE(sequence.getNumTimeSigs() == 3);
    const tracktion::TimeSigSetting* const short_bar = sequence.getTimeSig(1);
    const tracktion::TimeSigSetting* const seven_eight = sequence.getTimeSig(2);
    REQUIRE(short_bar != nullptr);
    REQUIRE(seven_eight != nullptr);
    CHECK(short_bar->getStartBeat().inBeats() == Catch::Approx(4.0));
    CHECK(short_bar->numerator.get() == 1);
    CHECK(short_bar->denominator.get() == 4);
    CHECK(seven_eight->getStartBeat().inBeats() == Catch::Approx(5.0));
    CHECK(seven_eight->numerator.get() == 7);
    CHECK(seven_eight->denominator.get() == 8);
}

// The host counts the bars the editor shows and the quarters the song holds, in x/8 meters too:
// one bar of 6/8 is three quarters, so at 120 quarter-note BPM the 4/4 after it begins at 1.5 s as
// the second bar's downbeat, three quarters into the song. A beat is the meter's own note value
// (an eighth in 6/8), as a host reports it.
TEST_CASE(
    "Tempo mirror counts x/8 bars and quarter-note positions as the map does",
    "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    mirrorTempoMapIntoSequence(
        sequence,
        core::TempoMap{
            core::testing::metersAt(
                {{.at = "1:1", .numerator = 6, .denominator = 8},
                 {.at = "2:1", .numerator = 4, .denominator = 4}}),
            {anchor("1:1", 0), anchor("4:1", 5500)},
        });

    const HostPosition inside = hostPositionAt(sequence, 0.75);
    CHECK(inside.bar == 0);
    CHECK(inside.beat == Catch::Approx(3.0));
    CHECK(inside.ppq == Catch::Approx(1.5));

    const HostPosition second_bar = hostPositionAt(sequence, 1.5);
    CHECK(second_bar.bar == 1);
    CHECK(second_bar.beat == Catch::Approx(0.0).margin(1e-9));
    CHECK(second_bar.ppq == Catch::Approx(3.0));
    CHECK(second_bar.ppq_of_bar_start == Catch::Approx(3.0));

    const HostPosition third_bar = hostPositionAt(sequence, 3.5);
    CHECK(third_bar.bar == 2);
    CHECK(third_bar.beat == Catch::Approx(0.0).margin(1e-9));
    CHECK(third_bar.ppq == Catch::Approx(7.0));
}

// A bar of 7/8 ends between two quarters: the 4/4 after it begins three and a half quarters in, at
// 1.75 s, and the host counts that instant as the second bar's downbeat.
TEST_CASE("Tempo mirror places a meter that starts between quarters", "[audio][tempo-mirror]")
{
    const TempoMirrorHarness harness;
    tracktion::TempoSequence& sequence = harness.edit->tempoSequence;

    mirrorTempoMapIntoSequence(
        sequence,
        core::TempoMap{
            core::testing::metersAt(
                {{.at = "1:1", .numerator = 7, .denominator = 8},
                 {.at = "2:1", .numerator = 4, .denominator = 4}}),
            {anchor("1:1", 0), anchor("4:1", 5750)},
        });

    const HostPosition downbeat = hostPositionAt(sequence, 1.75);
    CHECK(downbeat.bar == 1);
    CHECK(downbeat.beat == Catch::Approx(0.0).margin(1e-9));
    CHECK(downbeat.ppq == Catch::Approx(3.5));
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
        AutomationCurvePoint{.seconds = 0.5, .norm_value = 0.25F},
        AutomationCurvePoint{.seconds = 2.5, .norm_value = 0.75F},
    };
    REQUIRE(writePluginParameterCurve(*plugin, param_id, points));
    // The seam writes more than the authored list (it prepends the lane's derived anchor), so the
    // invariant is stated against what it actually produced: the mirror must move none of it.
    const std::optional<std::vector<AutomationCurvePoint>> before_mirror =
        readPluginParameterCurve(*plugin, param_id);
    REQUIRE(before_mirror.has_value());
    // clang-tidy does not treat Catch2 REQUIRE as an optional guard, so assert engagement
    // explicitly before dereferencing.
    if (!before_mirror.has_value())
    {
        return;
    }
    REQUIRE_FALSE(before_mirror->empty());

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
        CHECK(*read_back == *before_mirror);
    }
}

} // namespace rock_hero::common::audio
