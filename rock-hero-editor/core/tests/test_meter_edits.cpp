#include "timeline/meter_edits.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/testing/tempo_map_fixtures.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <rock_hero/common/core/tone/tone_automation.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/editor/core/testing/song_timeline_fixtures.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::BeatAnchor;
using common::core::Chart;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::SongSection;
using common::core::SongTimeline;
using common::core::TempoMap;
using common::core::TimeSignatureChange;
using common::core::ToneAutomationPoint;
using common::core::ToneRegion;
using common::core::testing::anchor;
using common::core::testing::downbeat;
using common::core::testing::metersAt;
using testing::addressesErased;
using testing::at;
using testing::chartOf;
using testing::Dice;
using testing::forEachSlicePair;
using testing::makeNote;
using testing::randomMeter;
using testing::randomTimeline;
using testing::regionId;
using testing::requireValid;
using testing::respelled;
using testing::sliceArrangement;
using testing::SliceKind;
using testing::sliceMap;
using testing::SlicePair;
using testing::sliceTimeline;

using Catch::Matchers::WithinULP;

[[nodiscard]] TimeSignatureChange placed(Fraction place, int numerator, int denominator)
{
    return TimeSignatureChange{
        .place = place,
        .meter = {.numerator = numerator, .denominator = denominator},
    };
}

// The fixture with its 3/4 respelled as 6/8.
[[nodiscard]] SongTimeline respelledSlices()
{
    const SongTimeline before = sliceTimeline();
    requireValid(before);
    const SongTimeline after = readdressUnderMeters(
        before, std::vector{placed(Fraction{}, 4, 4), placed(Fraction{2}, 6, 8)});
    requireValid(after);
    return after;
}

// One random meter edit of the kinds the verbs make, and the freer ones a later verb may: a
// respelling, an inserted respelling of the prevailing meter, a restated last meter (its reign runs
// to the terminal, which may be left mid-measure), a free restatement and a deletion (either may
// leave a later meter mid-measure, cutting a derived short bar before it).
[[nodiscard]] std::vector<TimeSignatureChange> randomMeterEdit(
    Dice& dice, const TempoMap& tempo_map)
{
    std::vector<TimeSignatureChange> meters = tempo_map.timeSignatures();
    const auto pick = [&dice, &meters] {
        return static_cast<std::size_t>(dice.roll(0, static_cast<int>(meters.size()) - 1));
    };
    switch (dice.roll(0, 4))
    {
        case 0:
        {
            TimeSignatureChange& respelt = meters[pick()];
            respelt.meter = respelled(respelt.meter);
            break;
        }
        case 1:
        {
            const int terminal_measure = common::core::terminalGridPosition(tempo_map).measure;
            const int measure = dice.roll(1, terminal_measure - 1);
            const TimeSignatureChange inserted{
                .place = tempo_map.wholeNotePositionAt(downbeat(measure)),
                .meter = respelled(tempo_map.timeSignatureAt(measure).meter),
            };
            const auto place =
                std::ranges::find_if(meters, [&inserted](const TimeSignatureChange& held) {
                    return held.place >= inserted.place;
                });
            if (place != meters.end() && place->place == inserted.place)
            {
                *place = inserted;
            }
            else
            {
                meters.insert(place, inserted);
            }
            break;
        }
        case 2:
        {
            meters.back().meter = randomMeter(dice);
            break;
        }
        case 3:
        {
            meters[pick()].meter = randomMeter(dice);
            break;
        }
        default:
        {
            if (meters.size() > 1)
            {
                meters.erase(meters.begin() + dice.roll(1, static_cast<int>(meters.size()) - 1));
            }
            break;
        }
    }
    return meters;
}

// ---------------------------------------------------------------------------------------------
// The time-preservation property.

// Rings and keyframes whose onset and end lie under different denominators in either map: the
// durations a meter change sits inside, which must keep their value all the same.
struct Crossings
{
    int rings{0};
    int keyframes{0};
};

// Whether two places lie under different denominators in either map.
[[nodiscard]] bool crossesDenominator(
    const TempoMap& old_map, const GridPosition& old_onset, const GridPosition& old_end,
    const TempoMap& new_map, const GridPosition& new_onset, const GridPosition& new_end)
{
    return old_map.timeSignatureAt(old_onset.measure).meter.denominator !=
               old_map.timeSignatureAt(old_end.measure).meter.denominator ||
           new_map.timeSignatureAt(new_onset.measure).meter.denominator !=
               new_map.timeSignatureAt(new_end.measure).meter.denominator;
}

// Checks every slice of a landed edit against the timeline it came from: whole-note places exact,
// times bit-identical (both maps resolve one exact place through the same anchors), every other
// field untouched. Every anchor keeps its place and its time, the terminal included, and a section
// is a slice like any other.
[[nodiscard]] Crossings checkKeepsTime(
    const SongTimeline& before, const SongTimeline& after,
    const std::vector<TimeSignatureChange>& meters)
{
    requireValid(after);
    const TempoMap& old_map = before.tempo_map;
    const TempoMap& new_map = after.tempo_map;
    CHECK(new_map.timeSignatures() == meters);

    const std::vector<BeatAnchor>& old_anchors = old_map.anchors();
    const std::vector<BeatAnchor>& new_anchors = new_map.anchors();
    REQUIRE(new_anchors.size() == old_anchors.size());
    for (std::size_t index = 0; index < old_anchors.size(); ++index)
    {
        CHECK(
            new_map.wholeNotePositionAt(new_anchors[index].position) ==
            old_map.wholeNotePositionAt(old_anchors[index].position));
        CHECK(new_anchors[index].milliseconds == old_anchors[index].milliseconds);
    }

    Crossings crossings;
    forEachSlicePair(before, after, [&](const SlicePair& pair) {
        CHECK(
            new_map.wholeNotePositionAt(pair.new_position) ==
            old_map.wholeNotePositionAt(pair.old_position));
        CHECK_THAT(
            new_map.secondsAt(pair.new_position),
            WithinULP(old_map.secondsAt(pair.old_position), 0));
        if (pair.kind != SliceKind::RingEnd && pair.kind != SliceKind::Keyframe)
        {
            return;
        }
        if (crossesDenominator(
                old_map,
                pair.old_onset,
                pair.old_position,
                new_map,
                pair.new_onset,
                pair.new_position))
        {
            if (pair.kind == SliceKind::RingEnd)
            {
                ++crossings.rings;
            }
            else
            {
                ++crossings.keyframes;
            }
        }
    });
    // A ring and a keyframe offset are note values, so a meter edit leaves every one untouched.
    REQUIRE(after.arrangements.size() == before.arrangements.size());
    for (std::size_t arrangement = 0; arrangement < before.arrangements.size(); ++arrangement)
    {
        const Chart& old_chart = chartOf(before, arrangement);
        const Chart& new_chart = chartOf(after, arrangement);
        REQUIRE(new_chart.notes.size() == old_chart.notes.size());
        for (std::size_t index = 0; index < old_chart.notes.size(); ++index)
        {
            CHECK(new_chart.notes[index].sustain == old_chart.notes[index].sustain);
            CHECK(new_chart.notes[index].keyframes == old_chart.notes[index].keyframes);
        }
    }
    // Every field but the re-addressed ones is untouched.
    CHECK(addressesErased(after) == addressesErased(before));
    return crossings;
}

// ---------------------------------------------------------------------------------------------
// The written document.

// Owns a clean temporary directory the document property writes its packages into.
class TemporaryDirectory final
{
public:
    TemporaryDirectory()
        : m_path(
              std::filesystem::temp_directory_path() /
              std::filesystem::path{
                  "rock-hero-meter-edits-test-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
              })
    {
        std::filesystem::remove_all(m_path);
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() noexcept
    {
        try
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
        catch (...)
        {
            m_path.clear();
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

void writeTextFile(const std::filesystem::path& path, const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    file << contents;
}

[[nodiscard]] std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    REQUIRE(file.is_open());
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// The lines of a text, so two documents compare line by line.
[[nodiscard]] std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size())
    {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return lines;
}

// The lines of a song document a meter edit may not change: everything outside the
// `timeSignatures` array.
[[nodiscard]] std::vector<std::string> linesOutsideTimeSignatures(
    const std::vector<std::string>& lines)
{
    std::vector<std::string> kept;
    bool inside_signatures = false;
    for (const std::string& text : lines)
    {
        if (text.find("\"timeSignatures\"") != std::string::npos)
        {
            inside_signatures = true;
        }
        else if (text.find("\"anchors\"") != std::string::npos)
        {
            inside_signatures = false;
        }
        if (!inside_signatures)
        {
            kept.push_back(text);
        }
    }
    return kept;
}

// The package a timeline saves as: the song document and one chart document per arrangement, as
// text, written into a fresh workspace holding the audio and tone documents the writer requires.
struct WrittenPackage
{
    std::vector<std::string> song_lines;
    std::vector<std::string> chart_texts;
};

[[nodiscard]] WrittenPackage writtenPackage(
    const std::filesystem::path& workspace, const SongTimeline& timeline)
{
    common::core::Song song;
    song.tempo_map = timeline.tempo_map;
    song.sections = timeline.sections;
    const std::filesystem::path audio = workspace / "audio" / "backing.flac";
    writeTextFile(audio, "audio");
    for (const common::core::ArrangementTimeline& share : timeline.arrangements)
    {
        common::core::Arrangement arrangement;
        arrangement.id = share.id == "lead" ? "4f3a1c5e-9d2b-48a6-b1f0-c7e8d9a2b3c4"
                                            : "9b26d8e8-3ec5-4f97-9a81-d18ef6bce30d";
        arrangement.part = common::core::Part::Lead;
        arrangement.audio_asset.path = audio;
        arrangement.tone_track = share.tone_track;
        arrangement.tone_automation = share.tone_automation;
        for (const ToneRegion& region : share.tone_track.regions)
        {
            writeTextFile(workspace / region.tone_document_ref, "{}");
        }
        arrangement.chart_ref = "charts/" + arrangement.id + ".chart.json";
        arrangement.chart = share.chart;
        song.arrangements.push_back(std::move(arrangement));
    }
    const auto written = common::core::writeRockSongPackageDirectory(workspace, song);
    INFO((written.has_value() ? std::string{} : written.error().message));
    REQUIRE(written.has_value());
    WrittenPackage package{.song_lines = linesOf(readTextFile(workspace / "song.json"))};
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        package.chart_texts.push_back(readTextFile(workspace / arrangement.chart_ref));
    }
    return package;
}

// The transform never refuses: the one refusal a meter edit keeps is a reign shorter than one of
// its measures, a tempo-map rule the verb asks of the result.
[[nodiscard]] bool breaksAReignRule(const SongTimeline& edited)
{
    const auto rules = common::core::validateTempoMapRules(edited.tempo_map);
    if (rules.has_value())
    {
        return false;
    }
    CHECK(rules.error().code == common::core::TempoMapErrorCode::ReignTooShort);
    return true;
}

[[nodiscard]] bool hasDerivedShortBar(const TempoMap& tempo_map)
{
    return std::ranges::any_of(tempo_map.reigns(), &common::core::MeterReign::derived);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Properties over random songs.

TEST_CASE("Meter edits keep every object's time over random songs", "[core][meter-edits]")
{
    int landed_count = 0;
    int refused_count = 0;
    int short_bar_count = 0;
    int crossing_rings = 0;
    int crossing_keyframes = 0;
    for (std::uint32_t seed = 1; seed <= 150; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);
        const std::vector<TimeSignatureChange> meters = randomMeterEdit(dice, before.tempo_map);

        const SongTimeline edited = readdressUnderMeters(before, meters);
        if (breaksAReignRule(edited))
        {
            ++refused_count;
            continue;
        }
        ++landed_count;
        const Crossings crossings = checkKeepsTime(before, edited, meters);
        if (hasDerivedShortBar(edited.tempo_map))
        {
            ++short_bar_count;
        }
        crossing_rings += crossings.rings;
        crossing_keyframes += crossings.keyframes;

        // A -> B -> A restores the song exactly: every token, section, meter and the terminal.
        CHECK(readdressUnderMeters(edited, before.tempo_map.timeSignatures()) == before);
    }
    // The dice must reach every branch, or the property proves less than it claims.
    CHECK(landed_count >= 75);
    CHECK(refused_count > 0);
    CHECK(short_bar_count > 0);
    CHECK(crossing_rings > 0);
    CHECK(crossing_keyframes > 0);
}

// The file stores every position as a whole-note place, so a meter edit, which moves nothing,
// changes nothing in the saved documents but the time signatures: every chart document is
// byte-identical, and song.json differs only inside `timeSignatures`.
TEST_CASE("Meter edits change only the meter lines of a saved project", "[core][meter-edits]")
{
    int compared = 0;
    for (std::uint32_t seed = 1; seed <= 40; ++seed)
    {
        INFO("seed " << seed);
        Dice dice{seed};
        const SongTimeline before = randomTimeline(dice);
        requireValid(before);
        const SongTimeline after =
            readdressUnderMeters(before, randomMeterEdit(dice, before.tempo_map));
        if (breaksAReignRule(after))
        {
            continue;
        }

        const TemporaryDirectory directory;
        const WrittenPackage was = writtenPackage(directory.path() / "before", before);
        const WrittenPackage now = writtenPackage(directory.path() / "after", after);
        ++compared;

        CHECK(now.chart_texts == was.chart_texts);
        CHECK(
            linesOutsideTimeSignatures(now.song_lines) ==
            linesOutsideTimeSignatures(was.song_lines));
    }
    CHECK(compared >= 20);
}

// ---------------------------------------------------------------------------------------------
// One test per slice.

TEST_CASE("Meter edit re-addresses tempo anchors keeping their time", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    // 4:2+1/2 in 3/4 is three eighths into measure 4: beat 4 of 6/8. The others stand where the
    // respelling reaches no address.
    CHECK(
        after.tempo_map.anchors() == std::vector<BeatAnchor>{
                                         anchor("1:1", 0),
                                         anchor("2:2+1/2", 2750),
                                         anchor("4:4", 6250),
                                         anchor("6:1", 8500),
                                     });
    CHECK(after.tempo_map.timeSignatures() == metersAt({{"1:1", 4, 4}, {"3:1", 6, 8}}));
}

TEST_CASE("Meter edit re-addresses note onsets", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].position == at("2:4"));
    // Beat 2 of 3/4 is beat 3 of 6/8.
    CHECK(chart.notes[1].position == at("4:3"));
}

// A ring is a note value, so a meter edit leaves it untouched: the half note from 2:4 that ended
// at 3:2 of the 3/4 ends at 3:3 of the 6/8 the same edit respelled it as, at the very same second.
TEST_CASE("Meter edit leaves a ring crossing a meter change untouched", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].sustain == Fraction{1, 2});
    CHECK(
        common::core::advanceGridPositionByWholeNotes(
            after.tempo_map, chart.notes[0].position, chart.notes[0].sustain) == at("3:3"));
    CHECK_THAT(
        after.tempo_map.secondsAt(
            common::core::advanceGridPositionByWholeNotes(
                after.tempo_map, chart.notes[0].position, chart.notes[0].sustain)),
        Catch::Matchers::WithinULP(before.tempo_map.secondsAt(at("3:2")), 0));
    // A ring inside the respelled reign keeps its value too.
    CHECK(chart.notes[1].sustain == Fraction{1, 8});
}

// A keyframe offset is a note value from the onset, so it stays too: a dotted quarter from 2:4
// stood at 3:1+1/2 of the 3/4 and stands at 3:2 of the 6/8, at the same second.
TEST_CASE("Meter edit leaves a keyframe crossing a meter change untouched", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE_FALSE(chart.notes.empty());
    REQUIRE(chart.notes[0].keyframes.size() == 1);
    CHECK(chart.notes[0].keyframes[0].offset == Fraction{3, 8});
    CHECK(chart.notes[0].keyframes[0].bend == std::optional{1.0});
    const GridPosition instant = common::core::advanceGridPositionByWholeNotes(
        after.tempo_map, chart.notes[0].position, chart.notes[0].keyframes[0].offset);
    CHECK(instant == at("3:2"));
    CHECK_THAT(
        after.tempo_map.secondsAt(instant),
        Catch::Matchers::WithinULP(before.tempo_map.secondsAt(at("3:1+1/2")), 0));
}

TEST_CASE("Meter edit re-addresses fret-hand positions", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.fret_hand_positions.size() == 2);
    CHECK(chart.fret_hand_positions[0].position == at("1:1"));
    CHECK(chart.fret_hand_positions[1].position == at("3:6"));
    CHECK(chart.fret_hand_positions[1].fret == 5);
}

TEST_CASE("Meter edit re-addresses tone region starts", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    REQUIRE_FALSE(after.arrangements.empty());
    const std::vector<ToneRegion>& regions = after.arrangements[0].tone_track.regions;
    REQUIRE(regions.size() == 2);
    CHECK(regions[0].start == at("1:1"));
    CHECK(regions[1].start == at("4:2"));
    CHECK(regions[1].id == regionId(2));
}

TEST_CASE("Meter edit re-addresses tone automation points", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    REQUIRE_FALSE(after.arrangements.empty());
    REQUIRE(after.arrangements[0].tone_automation.size() == 1);
    const std::vector<ToneAutomationPoint>& points =
        after.arrangements[0].tone_automation[0].points;
    REQUIRE(points.size() == 2);
    CHECK(points[0].position == at("1:1"));
    CHECK(points[1].position == at("4:1+2/3"));
}

TEST_CASE("Meter edit re-addresses sections on a downbeat", "[core][meter-edits]")
{
    // A shorter first reign renumbers every later measure: 2 whole notes of 4/4 are 4 of 2/4.
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = readdressUnderMeters(
        before, std::vector{placed(Fraction{}, 2, 4), placed(Fraction{2}, 3, 4)});
    requireValid(after);
    CHECK(
        after.sections == std::vector<SongSection>{
                              SongSection{.position = at("1:1"), .name = "Intro"},
                              SongSection{.position = at("6:1"), .name = "Bridge"},
                          });
}

TEST_CASE("Meter edit re-addresses every arrangement", "[core][meter-edits]")
{
    const SongTimeline after = respelledSlices();
    const Chart& chart = chartOf(after, 1);
    REQUIRE(chart.notes.size() == 1);
    // Beat 3 of 3/4 is beat 5 of 6/8.
    CHECK(chart.notes[0].position == at("5:5"));
    REQUIRE(after.arrangements.size() == 2);
    CHECK(after.arrangements[1].id == "rhythm");
    REQUIRE(after.arrangements[1].tone_automation.size() == 1);
    REQUIRE(after.arrangements[1].tone_automation[0].points.size() == 2);
    CHECK(after.arrangements[1].tone_automation[0].points[1].position == at("4:1+2/3"));
}

TEST_CASE("Meter edit renumbers the measures after a changed reign", "[core][meter-edits]")
{
    const SongTimeline before = sliceTimeline();
    const SongTimeline after = readdressUnderMeters(
        before, std::vector{placed(Fraction{}, 2, 4), placed(Fraction{2}, 3, 4)});
    requireValid(after);
    // The meters keep their places; the 3/4 at whole note 2 is measure 5 under the 2/4.
    CHECK(after.tempo_map.timeSignatures() == metersAt({{"1:1", 2, 4}, {"5:1", 3, 4}}));
    CHECK(after.tempo_map.anchors().back() == anchor("8:1", 8500));
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 2);
    CHECK(chart.notes[0].position == at("4:2"));
    CHECK(chart.notes[1].position == at("6:2"));
}

TEST_CASE("A map's signatures are placed at their whole notes", "[core][meter-edits]")
{
    CHECK(
        sliceMap().timeSignatures() ==
        std::vector<TimeSignatureChange>{placed(Fraction{}, 4, 4), placed(Fraction{2}, 3, 4)});
}

// ---------------------------------------------------------------------------------------------
// A reign that does not divide: the derived short bar, the terminal and the sections stay put.

TEST_CASE("Meter edit lands a derived short bar before a later meter", "[core][meter-edits]")
{
    // Sixteen beats of 4/4 re-metered to 3/4 are five measures and a beat, so measure 6 is a
    // derived 1/4 bar and the 6/8, keeping its place, begins on 7:1; the terminal keeps its place
    // and its time too, two 6/8 bars later on 9:1.
    const SongTimeline before{
        .tempo_map =
            TempoMap{
                metersAt({{"1:1", 4, 4}, {"5:1", 6, 8}}),
                {anchor("1:1", 0), anchor("7:1", 11000)},
            },
        .sections = {},
        .arrangements = {},
    };
    requireValid(before);
    const std::vector<TimeSignatureChange> meters{
        placed(Fraction{}, 3, 4), placed(Fraction{4}, 6, 8)
    };
    const SongTimeline after = readdressUnderMeters(before, meters);
    requireValid(after);
    CHECK(after.tempo_map.timeSignatures() == meters);
    CHECK(
        after.tempo_map.timeSignatureAt(6) == common::core::MeterReign{
                                                  .first_measure = 6,
                                                  .place = Fraction{15, 4},
                                                  .meter = {.numerator = 1, .denominator = 4},
                                                  .derived = true,
                                              });
    CHECK(after.tempo_map.timeSignatureAt(7).first_measure == 7);
    CHECK(after.tempo_map.anchors().back() == anchor("9:1", 11000));
}

TEST_CASE(
    "Meter edit with no later meter leaves the terminal where it stands", "[core][meter-edits]")
{
    // The same sixteen beats of 4/4 to 3/4 with nothing after them: the terminal, kept at whole
    // note 4, stands on 6:2 and the song ends on that one-beat bar. Nothing is re-timed, so the
    // note at 4:4 (whole note 3 3/4, beat 1 of 6) keeps its 7.5 s exactly.
    const SongTimeline before{
        .tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 8000)}),
        .sections = {},
        .arrangements = {sliceArrangement("lead", {makeNote(at("4:4"), 1, 5, Fraction{1}, {})})},
    };
    requireValid(before);
    const SongTimeline after = readdressUnderMeters(before, std::vector{placed(Fraction{}, 3, 4)});
    requireValid(after);
    CHECK(
        after.tempo_map ==
        TempoMap{metersAt({{"1:1", 3, 4}}), {anchor("1:1", 0), anchor("6:2", 8000)}});
    CHECK_FALSE(hasDerivedShortBar(after.tempo_map));
    const Chart& chart = chartOf(after, 0);
    REQUIRE(chart.notes.size() == 1);
    CHECK(chart.notes[0].position == at("6:1"));
    CHECK_THAT(after.tempo_map.secondsAt(chart.notes[0].position), WithinULP(7.5, 0));
}

TEST_CASE("Meter edit keeps a section left mid-measure", "[core][meter-edits]")
{
    // Verse at whole note 2 falls on 3:3 of 3/4 and stays there; the 4/4 restated restores it on
    // 3:1 exactly, the terminal with it.
    const SongTimeline before{
        .tempo_map = common::core::testing::commonTimeMap({anchor("1:1", 0), anchor("5:1", 8000)}),
        .sections =
            {
                SongSection{.position = at("1:1"), .name = "Intro"},
                SongSection{.position = at("3:1"), .name = "Verse"},
            },
        .arrangements = {},
    };
    requireValid(before);
    const SongTimeline after = readdressUnderMeters(before, std::vector{placed(Fraction{}, 3, 4)});
    requireValid(after);
    CHECK(
        after.sections == std::vector<SongSection>{
                              SongSection{.position = at("1:1"), .name = "Intro"},
                              SongSection{.position = at("3:3"), .name = "Verse"},
                          });
    CHECK(readdressUnderMeters(after, before.tempo_map.timeSignatures()) == before);
}

} // namespace rock_hero::editor::core
