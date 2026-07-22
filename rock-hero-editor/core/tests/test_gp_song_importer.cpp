#include "project/gp_chart_builder.h"
#include "project/gp_score.h"
#include "project/gp_song_importer.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <rock_hero/common/audio/testing/audio_fixtures.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/package/archive_io.h>
#include <rock_hero/common/core/package/package_id.h>
#include <string>
#include <system_error>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;

// Two 4/4 bars at 120 BPM with audio sync points, exercising ties across bars, hammer-ons,
// shift slides, palm mutes, vibrato, a bend, and a between-fret natural harmonic.
constexpr const char* g_fixture_gpif = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
<GPVersion>8.1.4</GPVersion>
<Score>
<Title><![CDATA[Fixture]]></Title>
<Artist><![CDATA[Tester]]></Artist>
<Album><![CDATA[Album]]></Album>
</Score>
<MasterTrack>
<Automations>
<Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>120 2</Value></Automation>
<Automation><Type>SyncPoint</Type><Bar>0</Bar><Position>0</Position>
<Value><BarIndex>0</BarIndex><BarOccurrence>0</BarOccurrence><ModifiedTempo>120</ModifiedTempo>
<OriginalTempo>120</OriginalTempo><FrameOffset>0</FrameOffset></Value></Automation>
<Automation><Type>SyncPoint</Type><Bar>1</Bar><Position>0</Position>
<Value><BarIndex>1</BarIndex><BarOccurrence>0</BarOccurrence><ModifiedTempo>120</ModifiedTempo>
<OriginalTempo>120</OriginalTempo><FrameOffset>88200</FrameOffset></Value></Automation>
</Automations>
</MasterTrack>
<BackingTrack><AssetId>0</AssetId></BackingTrack>
<Assets><Asset id="0"><EmbeddedFilePath>Content/Assets/audio.wav</EmbeddedFilePath></Asset></Assets>
<Tracks>
<Track id="0">
<Name>Guitar</Name>
<Staves><Staff><Properties>
<Property name="CapoFret"><Fret>2</Fret></Property>
<Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>
</Properties></Staff></Staves>
</Track>
</Tracks>
<MasterBars>
<MasterBar><Time>4/4</Time><Bars>0</Bars>
<Section><Letter><![CDATA[]]></Letter><Text><![CDATA[verse]]></Text></Section></MasterBar>
<MasterBar><Time>4/4</Time><Bars>1</Bars></MasterBar>
</MasterBars>
<Bars>
<Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
<Bar id="1"><Voices>1 -1 -1 -1</Voices></Bar>
</Bars>
<Voices>
<Voice id="0"><Beats>0 1 2 3</Beats></Voice>
<Voice id="1"><Beats>4 5</Beats></Voice>
</Voices>
<Beats>
<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>
<Beat id="1"><Rhythm ref="1"/><Notes>1</Notes></Beat>
<Beat id="2"><Rhythm ref="1"/><Notes>2</Notes></Beat>
<Beat id="3"><Rhythm ref="0"/><Notes>3</Notes></Beat>
<Beat id="4"><Rhythm ref="2"/><Notes>4</Notes></Beat>
<Beat id="5"><Rhythm ref="1"/><Notes>5</Notes></Beat>
</Beats>
<Notes>
<Note id="0"><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>3</Fret></Property>
<Property name="PalmMuted"><Enable/></Property>
</Properties></Note>
<Note id="1"><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>5</Fret></Property>
<Property name="HopoDestination"><Enable/></Property>
<Property name="Slide"><Flags>1</Flags></Property>
</Properties></Note>
<Note id="2"><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>7</Fret></Property>
</Properties></Note>
<Note id="3"><Tie origin="true" destination="false"/><Vibrato>Slight</Vibrato><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>2</Fret></Property>
</Properties></Note>
<Note id="4"><Tie origin="false" destination="true"/><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>2</Fret></Property>
</Properties></Note>
<Note id="5"><Properties>
<Property name="String"><String>2</String></Property>
<Property name="Fret"><Fret>3</Fret></Property>
<Property name="HarmonicType"><HType>Natural</HType></Property>
<Property name="HarmonicFret"><HFret>3.200000</HFret></Property>
<Property name="Bended"><Enable/></Property>
<Property name="BendOriginValue"><Float>0.000000</Float></Property>
<Property name="BendOriginOffset"><Float>0.000000</Float></Property>
<Property name="BendMiddleValue"><Float>50.000000</Float></Property>
<Property name="BendMiddleOffset1"><Float>50.000000</Float></Property>
<Property name="BendMiddleOffset2"><Float>50.000000</Float></Property>
<Property name="BendDestinationValue"><Float>100.000000</Float></Property>
<Property name="BendDestinationOffset"><Float>100.000000</Float></Property>
</Properties></Note>
</Notes>
<Rhythms>
<Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
<Rhythm id="1"><NoteValue>Eighth</NoteValue></Rhythm>
<Rhythm id="2"><NoteValue>Half</NoteValue></Rhythm>
</Rhythms>
</GPIF>
)";

// Returns the fixture gpif with the first occurrence of a marker replaced, for score variants.
[[nodiscard]] std::string fixtureWithReplacement(
    const std::string& marker, const std::string& replacement)
{
    std::string gpif{g_fixture_gpif};
    const std::size_t position = gpif.find(marker);
    REQUIRE(position != std::string::npos);
    gpif.replace(position, marker.size(), replacement);
    return gpif;
}

// Builds a .gp archive on disk from the given gpif text and returns its path.
[[nodiscard]] std::filesystem::path writeFixtureArchive(
    const std::filesystem::path& scratch, const std::string& gpif_text)
{
    const std::filesystem::path content = scratch / "gp_content";
    std::filesystem::create_directories(content / "Content" / "Assets");
    {
        std::ofstream gpif{content / "Content" / "score.gpif", std::ios::binary};
        gpif << gpif_text;
    }
    {
        std::ofstream audio{content / "Content" / "Assets" / "audio.wav", std::ios::binary};
        audio << common::audio::testing::makeWavBytes(44100.0, 1, 512);
    }

    const std::filesystem::path archive = scratch / "fixture.gp";
    REQUIRE(common::core::writeWorkspaceToArchive(content, archive).has_value());
    return archive;
}

// Returns the arrangement's parsed chart, failing the test loudly when it is missing.
[[nodiscard]] const common::core::Chart& requiredChart(const common::core::Arrangement& arrangement)
{
    REQUIRE(arrangement.chart.has_value());
    if (arrangement.chart.has_value())
    {
        return *arrangement.chart;
    }
    // Unreachable fallback: the REQUIRE above aborts the test when the chart is missing.
    static const common::core::Chart g_missing_chart{};
    return g_missing_chart;
}

} // namespace

TEST_CASE("Guitar Pro import builds arrangements from the score", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_import_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    const std::filesystem::path archive = writeFixtureArchive(scratch, g_fixture_gpif);
    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());

    CHECK(song->metadata.title == "Fixture");
    CHECK(song->metadata.artist == "Tester");
    CHECK(song->metadata.album == "Album");

    // Sync points pin bar downbeats to the audio: bar two lands exactly at two seconds.
    CHECK(song->tempo_map.secondsAtBeat(1, 1) == Catch::Approx(0.0));
    CHECK(song->tempo_map.secondsAtBeat(2, 1) == Catch::Approx(2.0));

    // Master-bar section markers hoist to the song level rather than into each track's chart.
    REQUIRE(song->sections.size() == 1);
    CHECK(song->sections[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(song->sections[0].name == "verse");

    REQUIRE(song->arrangements.size() == 1);
    const common::core::Arrangement& arrangement = song->arrangements.front();
    CHECK(arrangement.part == common::core::Part::Lead);
    CHECK(common::core::isCanonicalChartDocumentRef(arrangement.chart_ref));
    CHECK(std::filesystem::is_regular_file(workspace / arrangement.chart_ref));
    CHECK(std::filesystem::is_regular_file(workspace / arrangement.audio_asset.path));
    // Imported audio is transcoded to the canonical FLAC format, and the staged source is removed.
    CHECK(arrangement.audio_asset.path == std::filesystem::path{"audio"} / "backing.flac");
    CHECK_FALSE(std::filesystem::exists(workspace / "audio" / "backing_source.wav"));
    // No frame padding in the fixture, so the audio starts at the score's first beat.
    CHECK_THAT(arrangement.audio_asset.start_offset.seconds, Catch::Matchers::WithinULP(0.0, 0));

    const common::core::Chart& chart = requiredChart(arrangement);
    CHECK(chart.tuning.strings == std::vector<std::string>{"E2", "A2", "D3", "G3", "B3", "E4"});
    CHECK(chart.tuning.capo == 2);

    REQUIRE(chart.notes.size() == 5);

    // Quarter palm mute on the low string. Its notated one-beat tail trims to 3/4 against the
    // next onset one beat later (minimum-note-distance margin 1/4 in 4/4) and then drops as a
    // sub-beat effect-free sustain — the import sustain policy.
    CHECK(chart.notes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.notes[0].string == 1);
    CHECK(chart.notes[0].fret == 3);
    CHECK(chart.notes[0].mute == common::core::NoteMute::Palm);
    CHECK(chart.notes[0].sustain == Fraction{});

    // Hammer-on destination that shift-slides into the next note.
    CHECK(chart.notes[1].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(chart.notes[1].attack == common::core::NoteAttack::Hammer);
    REQUIRE(chart.notes[1].slides.size() == 1);
    CHECK(chart.notes[1].slides[0].fret == 7);
    CHECK(chart.notes[1].slides[0].offset == Fraction{1, 2});
    CHECK_FALSE(chart.notes[1].slides[0].unpitched);

    CHECK(
        chart.notes[2].position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    CHECK(chart.notes[2].fret == 7);

    // The tie chain merges into one note whose sustain crosses the barline: onset 1:3, a
    // quarter in bar one plus a half in bar two makes four beats to the continuation's end,
    // trimmed by the sustain policy to 15/4 (margin 1/4 before the next onset at 2:3) and kept —
    // vibrato is a sustain-carried technique, so the sub-beat drop rule never applies to it.
    CHECK(chart.notes[3].position == GridPosition{.measure = 1, .beat = 3});
    CHECK(chart.notes[3].string == 2);
    CHECK(chart.notes[3].sustain == Fraction{15, 4});
    CHECK(chart.notes[3].vibrato);

    // Between-fret natural harmonic with the GP bend mapped to [offset, semitones] pairs.
    CHECK(chart.notes[4].position == GridPosition{.measure = 2, .beat = 3});
    CHECK(chart.notes[4].harmonic == common::core::NoteHarmonic::Natural);
    CHECK(chart.notes[4].touch == std::optional{3.2});
    REQUIRE(chart.notes[4].bend.size() == 3);
    CHECK(chart.notes[4].bend[0].offset == Fraction{0});
    CHECK(chart.notes[4].bend[0].semitones == Catch::Approx(0.0));
    CHECK(chart.notes[4].bend[1].offset == Fraction{1, 4});
    CHECK(chart.notes[4].bend[1].semitones == Catch::Approx(1.0));
    CHECK(chart.notes[4].bend[2].offset == Fraction{1, 2});
    CHECK(chart.notes[4].bend[2].semitones == Catch::Approx(2.0));

    // No two notes strike together in the fixture, so no chord furniture is derived.
    CHECK(chart.templates.empty());
    CHECK(chart.shapes.empty());

    // The generated fret-hand track walks a minimal-shift window: fret 3 opens a 3-6 window,
    // fret 7 shifts it minimally up to 4-7, fret 2 shifts it minimally down to 2-5, and the
    // final fret-3 bend note fits without another move.
    REQUIRE(chart.fret_hand_positions.size() == 3);
    CHECK(
        chart.fret_hand_positions[0] ==
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 1, .beat = 1}, .fret = 3, .width = 4
        });
    CHECK(
        chart.fret_hand_positions[1] ==
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}},
            .fret = 4,
            .width = 4
        });
    CHECK(
        chart.fret_hand_positions[2] ==
        common::core::FretHandPosition{
            .position = GridPosition{.measure = 1, .beat = 3}, .fret = 2, .width = 4
        });

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A legato slide is a continuation of the same note (user rule 2026-07-21): the landing is not
// re-picked, so it folds into the origin as a pitched waypoint instead of keeping its own onset
// — unlike the shift slide in the main fixture, whose target stays a real note with its own head.
TEST_CASE("Guitar Pro import merges legato slide landings into the origin", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_legato_slide_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Flags 2 is the legato slide; the main fixture's Flags 1 is the shift slide.
    const std::string gpif = fixtureWithReplacement(
        "<Property name=\"Slide\"><Flags>1</Flags></Property>",
        "<Property name=\"Slide\"><Flags>2</Flags></Property>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());

    // The fret-7 landing at 1:2+1/2 is no longer an onset: four notes remain.
    REQUIRE(chart.notes.size() == 4);
    CHECK(chart.notes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.notes[2].position == GridPosition{.measure = 1, .beat = 3});

    // The origin keeps its hammer attack and carries the junction waypoint; its sustain extends
    // through the landing's notated end (one beat), then the minimum-distance trim takes it to
    // 3/4 — floored above the waypoint, so the glide still reaches fret 7.
    const common::core::ChartNote& origin = chart.notes[1];
    CHECK(origin.position == GridPosition{.measure = 1, .beat = 2});
    CHECK(origin.fret == 5);
    CHECK(origin.attack == common::core::NoteAttack::Hammer);
    CHECK(origin.sustain == Fraction{3, 4});
    REQUIRE(origin.slides.size() == 1);
    CHECK(origin.slides[0].offset == Fraction{1, 2});
    CHECK(origin.slides[0].fret == 7);
    CHECK_FALSE(origin.slides[0].unpitched);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Chord derivation: notes struck together become a deduplicated posture template, and
// consecutive strums of the same posture merge into one shape span covering their notated
// durations — the grouping the tab renders as a chord box over repeated strums.
TEST_CASE("Guitar Pro import derives chord templates and spans", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_chord_shapes_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Beats one and two both strike the fret 3 + fret 5 power chord (a quarter then an eighth);
    // the single fret-7 note at 1:2+1/2 ends the held posture.
    std::string gpif{g_fixture_gpif};
    const auto replace_once = [&gpif](const std::string& marker, const std::string& replacement) {
        const std::size_t position = gpif.find(marker);
        REQUIRE(position != std::string::npos);
        gpif.replace(position, marker.size(), replacement);
    };
    replace_once("<Notes>0</Notes>", "<Notes>0 6</Notes>");
    replace_once("<Notes>1</Notes>", "<Notes>7 8</Notes>");
    replace_once(
        "</Notes>\n<Rhythms>",
        "<Note id=\"6\"><Properties>\n"
        "<Property name=\"String\"><String>1</String></Property>\n"
        "<Property name=\"Fret\"><Fret>5</Fret></Property>\n"
        "</Properties></Note>\n"
        "<Note id=\"7\"><Properties>\n"
        "<Property name=\"String\"><String>0</String></Property>\n"
        "<Property name=\"Fret\"><Fret>3</Fret></Property>\n"
        "</Properties></Note>\n"
        "<Note id=\"8\"><Properties>\n"
        "<Property name=\"String\"><String>1</String></Property>\n"
        "<Property name=\"Fret\"><Fret>5</Fret></Property>\n"
        "</Properties></Note>\n"
        "</Notes>\n<Rhythms>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 7);

    // One deduplicated unnamed posture: fret 3 on the lowest string, fret 5 on the second.
    REQUIRE(chart.templates.size() == 1);
    const common::core::ChordTemplate& posture = chart.templates.front();
    CHECK(posture.name.empty());
    REQUIRE(posture.frets.size() == 6);
    CHECK(posture.frets[0] == std::optional{3});
    CHECK(posture.frets[1] == std::optional{5});
    CHECK_FALSE(posture.frets[2].has_value());
    REQUIRE(posture.fingers.size() == 6);
    CHECK_FALSE(posture.fingers[0].has_value());

    // Both strums merge into one span from 1:1 through the eighth strum's notated end at
    // 1:2+1/2 — three half-beats — even though the sustain policy dropped the notes' own tails.
    REQUIRE(chart.shapes.size() == 1);
    CHECK(chart.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.shapes.front().sustain == Fraction{3, 2});
    CHECK(chart.shapes.front().chord == 0);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// An effect-free sustain that is still at least one beat long after the minimum-distance trim
// keeps its (trimmed) tail — the sub-beat drop rule only removes tails shorter than one beat.
TEST_CASE("Guitar Pro import keeps trimmed sustains of a beat or longer", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_sustain_keep_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Removing the vibrato from the tie-chain origin makes the merged four-beat note effect-free;
    // the trim to 15/4 still leaves more than a beat, so the tail survives.
    const std::string gpif = fixtureWithReplacement("<Vibrato>Slight</Vibrato>", "");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);
    CHECK_FALSE(chart.notes[3].vibrato);
    CHECK(chart.notes[3].sustain == Fraction{15, 4});

    std::filesystem::remove_all(scratch, cleanup_error);
}

TEST_CASE("Guitar Pro import pins the terminal downbeat to a final sync point", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_terminal_sync_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // A sync point at the end of the last bar rolls over onto the terminal downbeat (measure
    // three, beat one). Its 4.2-second frame offset must pin the map's end exactly; constant-
    // tempo extrapolation from the bar-two sync would land at 4.0 seconds instead.
    const std::string gpif = fixtureWithReplacement(
        "</Automations>",
        "<Automation><Type>SyncPoint</Type><Bar>1</Bar><Position>1</Position>\n"
        "<Value><BarIndex>1</BarIndex><BarOccurrence>0</BarOccurrence>"
        "<ModifiedTempo>120</ModifiedTempo>\n"
        "<OriginalTempo>120</OriginalTempo><FrameOffset>185220</FrameOffset></Value>"
        "</Automation>\n</Automations>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    CHECK(song->tempo_map.secondsAtBeat(2, 1) == Catch::Approx(2.0));
    CHECK(song->tempo_map.secondsAtBeat(3, 1) == Catch::Approx(4.2));

    std::filesystem::remove_all(scratch, cleanup_error);
}

TEST_CASE("Guitar Pro import drops sync points that regress on the grid", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_sync_regress_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // This sync point advances in audio time but points back to bar one after the bar-two sync;
    // accepting it would corrupt the anchor order, so it must be dropped and beat 1:3 must keep
    // its interpolated one-second position.
    const std::string gpif = fixtureWithReplacement(
        "</Automations>",
        "<Automation><Type>SyncPoint</Type><Bar>0</Bar><Position>0.5</Position>\n"
        "<Value><BarIndex>0</BarIndex><BarOccurrence>0</BarOccurrence>"
        "<ModifiedTempo>130</ModifiedTempo>\n"
        "<OriginalTempo>120</OriginalTempo><FrameOffset>132300</FrameOffset></Value>"
        "</Automation>\n</Automations>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    CHECK(song->tempo_map.secondsAtBeat(1, 3) == Catch::Approx(1.0));
    CHECK(song->tempo_map.secondsAtBeat(2, 1) == Catch::Approx(2.0));

    std::filesystem::remove_all(scratch, cleanup_error);
}

TEST_CASE("Guitar Pro import keeps the bend plateau between middle offsets", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_bend_plateau_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Distinct middle offsets hold the middle bend value from half to three quarters of the
    // eighth-note sustain, so the chart curve gains a fourth point instead of collapsing the
    // plateau to a single point.
    const std::string gpif = fixtureWithReplacement(
        "<Property name=\"BendMiddleOffset2\"><Float>50.000000</Float></Property>",
        "<Property name=\"BendMiddleOffset2\"><Float>75.000000</Float></Property>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);
    REQUIRE(chart.notes[4].bend.size() == 4);
    CHECK(chart.notes[4].bend[1].offset == Fraction{1, 4});
    CHECK(chart.notes[4].bend[1].semitones == Catch::Approx(1.0));
    CHECK(chart.notes[4].bend[2].offset == Fraction{3, 8});
    CHECK(chart.notes[4].bend[2].semitones == Catch::Approx(1.0));
    CHECK(chart.notes[4].bend[3].offset == Fraction{1, 2});
    CHECK(chart.notes[4].bend[3].semitones == Catch::Approx(2.0));

    std::filesystem::remove_all(scratch, cleanup_error);
}

namespace
{

// Builds a minimal all-4/4 score with one six-string track and the given start-aligned sync
// points, so tempo-map coverage can be tested without a full gpif fixture.
[[nodiscard]] GpScore makeLinearScore(int bar_count, const std::vector<GpSyncPoint>& syncs)
{
    GpScore score;
    score.title = "Coverage";
    score.base_tempo_quarter_bpm = 120.0;
    score.master_bars.assign(
        static_cast<std::size_t>(bar_count),
        GpMasterBar{.numerator = 4, .denominator = 4, .section = {}});
    score.sync_points = syncs;
    GpTrack track;
    track.name = "Guitar";
    track.tuning_midi = {40, 45, 50, 55, 59, 64};
    score.tracks.push_back(std::move(track));
    return score;
}

// Reports whether any conversion note contains the fragment.
[[nodiscard]] bool anyNoteContains(
    const std::vector<std::string>& notes, const std::string& fragment)
{
    return std::ranges::any_of(
        notes, [&](const std::string& note) { return note.find(fragment) != std::string::npos; });
}

} // namespace

// A song whose audio sync points stop early leaves most bars to constant-tempo extrapolation, so
// the build records a drift warning naming the covered range.
// Guitar Pro's positive backing-track frame padding is silence before the audio (the first
// measure precedes the recording); the import turns it into the asset start offset so playback
// lines up, counting the padding at a fixed 44.1kHz regardless of the audio's real rate.
TEST_CASE("Guitar Pro import offsets the audio by positive frame padding", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_frame_padding_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // 22050 frames at the fixed 44.1kHz rate is half a second of lead-in silence.
    const std::string gpif = fixtureWithReplacement(
        "<BackingTrack><AssetId>0</AssetId></BackingTrack>",
        "<BackingTrack><FramePadding>22050</FramePadding><AssetId>0</AssetId></BackingTrack>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    CHECK(song->arrangements.front().audio_asset.start_offset.seconds == Catch::Approx(0.5));

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Negative frame padding pulls the recording's head before the score's first beat, and with the
// origin sync point pinned at frame 0 (as on 99 of 114 surveyed corpus files) it is the only
// carrier of the audio alignment — dropping it plays the audio |padding|/44100 seconds late.
// The import must keep it as a signed start offset so playback skips the pre-score head.
TEST_CASE("Guitar Pro import keeps negative frame padding as a signed offset", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_negative_padding_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    const std::string gpif = fixtureWithReplacement(
        "<BackingTrack><AssetId>0</AssetId></BackingTrack>",
        "<BackingTrack><FramePadding>-88200</FramePadding><AssetId>0</AssetId></BackingTrack>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    CHECK(song->arrangements.front().audio_asset.start_offset.seconds == Catch::Approx(-2.0));

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Guitar Pro sync frames divided by 44100 rarely land on a whole millisecond, but the package
// format stores anchor seconds at three decimals, so an unrounded map imports yet cannot be saved.
// Every anchor must be snapped onto the millisecond grid.
TEST_CASE("Guitar Pro import snaps tempo anchors to the millisecond grid", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_anchor_grid_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // 88289 frames / 44100 = 2.00201814... seconds, which is not a whole millisecond.
    const std::string gpif = fixtureWithReplacement(
        "<FrameOffset>88200</FrameOffset>", "<FrameOffset>88289</FrameOffset>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());

    const auto& anchors = song->tempo_map.anchors();
    REQUIRE(anchors.size() >= 2);
    for (const common::core::BeatAnchor& anchor : anchors)
    {
        const double milliseconds = anchor.seconds * 1000.0;
        // Explicit cast: -Wimplicit-int-float-conversion flags the long long result widening
        // back to double in the subtraction.
        CHECK(std::abs(milliseconds - static_cast<double>(std::llround(milliseconds))) < 1.0e-6);
    }
    // The off-grid sync rounds to the nearest millisecond rather than staying at 2.00201814.
    CHECK(song->tempo_map.secondsAtBeat(2, 1) == Catch::Approx(2.002));

    std::filesystem::remove_all(scratch, cleanup_error);
}

TEST_CASE("Guitar Pro build warns about sparse audio sync coverage", "[core][gp-import]")
{
    const GpScore score = makeLinearScore(
        16,
        {
            GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0},
            GpSyncPoint{.bar = 1, .bar_fraction = 0.0, .seconds = 2.0, .modified_tempo = 120.0},
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    CHECK(anyNoteContains(built->notes, "audio sync points cover only up to measure 2 of 16"));
}

// A song whose sync points reach the final bars needs no extrapolation warning.
TEST_CASE("Guitar Pro build stays quiet when sync coverage is full", "[core][gp-import]")
{
    const GpScore score = makeLinearScore(
        16,
        {
            GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0},
            GpSyncPoint{.bar = 15, .bar_fraction = 0.0, .seconds = 30.0, .modified_tempo = 120.0},
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    CHECK_FALSE(anyNoteContains(built->notes, "audio sync points cover only up to"));
    // The heuristic part guess is always recorded so a misfiled track stays visible.
    CHECK(anyNoteContains(built->notes, "assigned parts by track order and name"));
}

TEST_CASE("Guitar Pro import rejects unusable sources", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_import_reject_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    std::filesystem::create_directories(scratch);

    GpSongImporter importer;
    CHECK_FALSE(importer.importSong(scratch / "missing.gp", scratch).has_value());

    // A zip without a score document is not a Guitar Pro file.
    const std::filesystem::path content = scratch / "not_gp";
    std::filesystem::create_directories(content);
    {
        std::ofstream stray{content / "readme.txt", std::ios::binary};
        stray << "not a score";
    }
    const std::filesystem::path archive = scratch / "not_gp.gp";
    REQUIRE(common::core::writeWorkspaceToArchive(content, archive).has_value());
    const auto imported = importer.importSong(archive, scratch);
    REQUIRE_FALSE(imported.has_value());
    CHECK(imported.error().code == SongImportErrorCode::ExtractionFailed);

    std::filesystem::remove_all(scratch, cleanup_error);
}

} // namespace rock_hero::editor::core
