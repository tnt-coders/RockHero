#include "project/gp_chart_builder.h"
#include "project/gp_score.h"
#include "project/gp_song_importer.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <rock_hero/common/audio/testing/audio_fixtures.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
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

// Finds the generated fret-hand position at an exact grid position, or null. The slide tests use
// this to assert the slide-driven hand move rather than the whole generated track, whose shape is
// the phrase-aware generator's own concern (generateFretHandPositions in gp_chart_builder.cpp).
[[nodiscard]] const common::core::FretHandPosition* fretHandPositionAt(
    const common::core::Chart& chart, const common::core::GridPosition& position)
{
    for (const common::core::FretHandPosition& fhp : chart.fret_hand_positions)
    {
        if (fhp.position == position)
        {
            return &fhp;
        }
    }
    return nullptr;
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
    // next onset one beat later (minimum-sustain-distance margin 1/4 in 4/4) and KEEPS that
    // trimmed tail: the drop rule reads the notated length, and a full beat notated is a
    // deliberate sustain.
    CHECK(chart.notes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.notes[0].string == 1);
    CHECK(chart.notes[0].fret == 3);
    CHECK(chart.notes[0].mute == common::core::NoteMute::Palm);
    CHECK(chart.notes[0].sustain == Fraction{3, 4});

    // Hammer-on destination that shift-slides into the next note: an ordinary pitched waypoint
    // glides to the fret-7 landing, ending the minimum sustain distance (1/16 whole note — a
    // quarter beat in 4/4) before the landing's onset, and the sustain ends at the glide end.
    CHECK(chart.notes[1].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(chart.notes[1].attack == common::core::NoteAttack::Hammer);
    REQUIRE(chart.notes[1].slides.size() == 1);
    CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
    CHECK(chart.notes[1].slides[0].fret == 7);
    CHECK_FALSE(chart.notes[1].slide_out.has_value());
    CHECK(chart.notes[1].sustain == Fraction{1, 4});

    CHECK(
        chart.notes[2].position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    CHECK(chart.notes[2].fret == 7);

    // The tie chain merges into one note whose sustain crosses the barline: onset 1:3, a
    // quarter in bar one plus a half in bar two makes four notated beats. The ring only
    // reaches — never crosses — the changed onset at 2:3, so it is no held ring and the
    // minimum-distance trim applies like any other tail (policy rule 1 holds).
    CHECK(chart.notes[3].position == GridPosition{.measure = 1, .beat = 3});
    CHECK(chart.notes[3].string == 2);
    CHECK(chart.notes[3].sustain == Fraction{15, 4});
    CHECK(chart.notes[3].vibrato);

    // Between-fret natural harmonic with the GP bend mapped to [offset, semitones] pairs.
    CHECK(chart.notes[4].position == GridPosition{.measure = 2, .beat = 3});
    CHECK(chart.notes[4].attack == common::core::NoteAttack::Pick);
    REQUIRE(chart.notes[4].harmonic_node.has_value());
    if (chart.notes[4].harmonic_node.has_value())
    {
        // The score writes "3.2", a conventional label naming the 6th partial. Two corrections land
        // here: the label resolves to that partial's true offset (3.156, not 3.2, since a position
        // even slightly off chokes the harmonic), and it is placed against the real stop — this
        // fixture has a CAPO AT 2, so the string speaks from there and its 6th-partial node sits at
        // 5.156. Guitar Pro ignores the capo and would have the player touch 3.2, which is not a node
        // of that string at all and would not ring.
        CHECK(*chart.notes[4].harmonic_node == Catch::Approx(5.1564).margin(0.001));
        // A natural harmonic has no stop of its own, so its fret IS the capo, not a copy of its node.
        CHECK(chart.notes[4].fret == 2);
        CHECK(*chart.notes[4].harmonic_node > static_cast<double>(chart.notes[4].fret));
        // The fretting hand is at the node it touches, not down at the capo.
        CHECK(
            common::core::handFret(
                chart.notes[4].fret, chart.notes[4].harmonic_node, chart.notes[4].attack) == 5);
    }
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

    // The generated fret-hand track opens on the fret-3 palm mute, then the five-to-seven shift
    // glide drags the anchor up by its own +2 delta to a fret-5 window at the pitched waypoint
    // (rule 9), keeping the fretting finger on its slot. (The full track shape is the
    // phrase-aware generator's own concern, so this asserts the slide-driven move, not the
    // whole sequence.)
    CHECK(chart.fret_hand_positions.front().fret == 3);
    const common::core::FretHandPosition* const shift_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(shift_glide != nullptr);
    CHECK(shift_glide->fret == 5);
    CHECK(shift_glide->width == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A legato slide is a continuation of the same note: the landing is not
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
    CHECK_FALSE(origin.slide_out.has_value());

    // With no landing onset, hand movement at fret 7 comes from the pitched waypoint alone: the
    // glide drags the window up by its own +2 delta at the waypoint's mid-sustain position
    // (rule 9), landing a fret-5 window there.
    const common::core::FretHandPosition* const legato_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    REQUIRE(legato_glide != nullptr);
    CHECK(legato_glide->fret == 5);
    CHECK(legato_glide->width == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// An unpitched trail-off is a release, not a rule-9 pitched drag — the generator's walk never
// repositions for it — but the window still rides the gesture: a dip
// placement at the compressed trail-off end, returning at the next onset (here the real
// fret-7 landing placement, which the restore yields to).
TEST_CASE("Guitar Pro import rides the window through a released trail-off", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_unpitched_slide_fhp_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Flags 4 is the downward slide-out; the fret-5 note now trails off unpitched toward
    // fret 1 instead of gliding into the fret-7 landing (which stays a real onset).
    const std::string gpif = fixtureWithReplacement(
        "<Property name=\"Slide\"><Flags>1</Flags></Property>",
        "<Property name=\"Slide\"><Flags>4</Flags></Property>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());

    REQUIRE(chart.notes.size() == 5);
    CHECK(chart.notes[1].slides.empty());
    const auto* const slide_out = common::core::slideOutOrNull(chart.notes[1]);
    REQUIRE(slide_out != nullptr);
    CHECK(slide_out->fret == 1);
    // The trail-off is not a protected payload (rule 2 carve-out): its
    // notated half-beat end trims back with the sustain to the minimum-sustain-distance margin
    // before the fret-7 onset.
    CHECK(slide_out->offset == Fraction{1, 4});
    CHECK(chart.notes[1].sustain == Fraction{1, 4});

    // The natural walk is untouched by the gesture (no rule-9 drag), but the exit pass dips
    // the window with the trail-off — the fret-1 exit pulls the anchor down to the neck's
    // edge at the compressed end — and the real fret-7 landing placement (the minimal fret-4
    // window) takes over at the next onset, standing in for the restore.
    CHECK(chart.fret_hand_positions.front().fret == 3);
    const common::core::FretHandPosition* const dip =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(dip != nullptr);
    CHECK(dip->fret == 1);
    const common::core::FretHandPosition* const landing =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    REQUIRE(landing != nullptr);
    CHECK(landing->fret == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// The slide-out-into-slide-in dip: the scoop stays in its own notated
// slot, so the previous slide-out compresses the plain margin before the notated onset and
// the two gestures never overlap — no fabricated head intrudes into the gap.
TEST_CASE("Guitar Pro import keeps a slide-out clear of a following slide-in", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_slide_dip_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Flags 4 turns the fret-5 note's glide into a downward slide-out; flags 16 makes the
    // fret-7 landing a slide-in from below, scooping on its own beat.
    std::string gpif = fixtureWithReplacement(
        "<Property name=\"Slide\"><Flags>1</Flags></Property>",
        "<Property name=\"Slide\"><Flags>4</Flags></Property>");
    const std::string landing_marker = "<Property name=\"Fret\"><Fret>7</Fret></Property>";
    const std::size_t landing_position = gpif.find(landing_marker);
    REQUIRE(landing_position != std::string::npos);
    gpif.insert(
        landing_position + landing_marker.size(),
        "\n<Property name=\"Slide\"><Flags>16</Flags></Property>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);

    // The slide-in head stays on its notated onset, scooping from two frets below over an
    // eighth of a beat (a quarter of its notated duration, floored at the minimum window).
    const common::core::ChartNote& landing = chart.notes[2];
    CHECK(landing.fret == 5);
    REQUIRE_FALSE(landing.slides.empty());
    CHECK(landing.slides.front().offset == Fraction{1, 8});
    CHECK(landing.slides.front().fret == 7);
    CHECK(landing.position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});

    // With no fabricated head in the gap, the half-beat trail-off compresses the plain
    // margin before the notated onset — the two gestures stay clear of each other.
    const common::core::ChartNote& dip = chart.notes[1];
    const auto* const slide_out = common::core::slideOutOrNull(dip);
    REQUIRE(slide_out != nullptr);
    CHECK(slide_out->offset == Fraction{1, 4});
    CHECK(dip.sustain == Fraction{1, 4});

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Guitar Pro's two tap articulations are different hands and must import differently:
// "Tapped" (two-hand tapping) becomes the chart's Tap attack, while
// "LeftHandTapped" — the fretting hand hammering the note from nowhere — imports as a plain
// hammer-on, so it anchors the fret hand and closes chord spans like any fretted note.
TEST_CASE("Guitar Pro import maps the two tap articulations by hand", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_tap_articulation_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    const auto import_with_property = [&](const std::string& property) {
        const std::string gpif = fixtureWithReplacement(
            "<Property name=\"Fret\"><Fret>7</Fret></Property>",
            "<Property name=\"Fret\"><Fret>7</Fret></Property>" + property);
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);
        GpSongImporter importer;
        return importer.importSong(archive, workspace);
    };

    SECTION("a left-hand tap imports as a hammer-on")
    {
        const auto song =
            import_with_property("<Property name=\"LeftHandTapped\"><Enable/></Property>");
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() >= 3);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Hammer);
    }

    SECTION("a two-hand tap imports as a tap")
    {
        const auto song = import_with_property("<Property name=\"Tapped\"><Enable/></Property>");
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() >= 3);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Tap);
    }

    SECTION("a note carrying both marks imports as the left-hand tap")
    {
        // Left-hand is the specialization; the generic tap mark adds nothing to it.
        const auto song = import_with_property(
            "<Property name=\"Tapped\"><Enable/></Property>"
            "<Property name=\"LeftHandTapped\"><Enable/></Property>");
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() >= 3);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Hammer);
    }

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A pitched glide drags the hand by its own fret delta even when the target already fits the
// window (normalization policy rule 9): with the landing lowered to fret 6, the five-to-six
// slide's target sits inside the opening 3-6 window, yet the +1 delta still moves the window to
// 4-7 so the fretting finger keeps its slot.
TEST_CASE(
    "Guitar Pro import shifts the hand by the slide delta for in-window targets",
    "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_in_window_slide_fhp_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // The fret-7 landing becomes fret 6, turning the shift slide into a one-fret glide whose
    // target the opening window already covers.
    const std::string gpif = fixtureWithReplacement("<Fret>7</Fret>", "<Fret>6</Fret>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());

    REQUIRE(chart.notes.size() == 5);
    REQUIRE(chart.notes[1].slides.size() == 1);
    CHECK(chart.notes[1].slides[0].fret == 6);

    // Minimal-shift coverage alone would leave the window at 3-6 through the glide; the slide
    // delta moves it anyway, to a fret-4 window at the waypoint's mid-sustain position.
    CHECK(chart.fret_hand_positions.front().fret == 3);
    const common::core::FretHandPosition* const in_window_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(in_window_glide != nullptr);
    CHECK(in_window_glide->fret == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// One 4/4 bar, quarter notes: a two-string chord holds frets 2 and 5, and the lower fret-2 note
// shift-slides to a beat-2 landing while the fret-5 note keeps ringing. The held 5 is a planted
// finger that pins the top edge, so at the slide waypoint the hand window reshapes to the exact
// sounding hull instead of translating: a slide inward shrinks the window
// below the usual four-fret span, a slide outward grows it. At 120 BPM the fret-2 note's shift
// glide ends the 1/4-beat minimum-sustain margin before the beat-2 landing, so its waypoint sits
// at beat 1 + 3/4, where the fret-5 quarter note is still sounding.
constexpr const char* g_held_slide_gpif = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
<GPVersion>8.1.4</GPVersion>
<Score>
<Title><![CDATA[HeldSlide]]></Title>
<Artist><![CDATA[Tester]]></Artist>
<Album><![CDATA[Album]]></Album>
</Score>
<MasterTrack>
<Automations>
<Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>120 2</Value></Automation>
</Automations>
</MasterTrack>
<BackingTrack><AssetId>0</AssetId></BackingTrack>
<Assets><Asset id="0"><EmbeddedFilePath>Content/Assets/audio.wav</EmbeddedFilePath></Asset></Assets>
<Tracks>
<Track id="0">
<Name>Guitar</Name>
<Staves><Staff><Properties>
<Property name="CapoFret"><Fret>0</Fret></Property>
<Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>
</Properties></Staff></Staves>
</Track>
</Tracks>
<MasterBars>
<MasterBar><Time>4/4</Time><Bars>0</Bars></MasterBar>
</MasterBars>
<Bars>
<Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
</Bars>
<Voices>
<Voice id="0"><Beats>0 1</Beats></Voice>
</Voices>
<Beats>
<Beat id="0"><Rhythm ref="0"/><Notes>0 1</Notes></Beat>
<Beat id="1"><Rhythm ref="0"/><Notes>2</Notes></Beat>
</Beats>
<Notes>
<Note id="0"><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>2</Fret></Property>
<Property name="Slide"><Flags>1</Flags></Property>
</Properties></Note>
<Note id="1"><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>5</Fret></Property>
</Properties></Note>
<Note id="2"><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>3</Fret></Property>
</Properties></Note>
</Notes>
<Rhythms>
<Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
</Rhythms>
</GPIF>
)";

TEST_CASE(
    "Guitar Pro import reshapes the hand around a held note during a slide", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_held_slide_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // The slide waypoint sits a 1/4-beat margin before the beat-2 landing, at beat 1 + 3/4.
    const GridPosition waypoint{.measure = 1, .beat = 1, .offset = Fraction{3, 4}};

    SECTION("a lower note sliding inward under a held top shrinks the window")
    {
        const std::filesystem::path archive = writeFixtureArchive(scratch, g_held_slide_gpif);
        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        REQUIRE(song->arrangements.size() == 1);
        const common::core::Chart& chart = requiredChart(song->arrangements.front());

        // The opening hand spans the struck {2,5} chord at the usual four-fret width.
        REQUIRE_FALSE(chart.fret_hand_positions.empty());
        CHECK(chart.fret_hand_positions.front().fret == 2);
        CHECK(chart.fret_hand_positions.front().width == 4);

        // At the waypoint the held 5 pins the top and the sliding 2->3 carries the bottom, so the
        // window shrinks to the exact hull [3,5] rather than translating up to [3,6].
        const common::core::FretHandPosition* const reshape = fretHandPositionAt(chart, waypoint);
        REQUIRE(reshape != nullptr);
        CHECK(reshape->fret == 3);
        CHECK(reshape->width == 3);
    }

    SECTION("a lower note sliding outward under a held top grows the window")
    {
        // Drop the landing to fret 1, so the fret-2 note slides down and away from the held 5.
        std::string grow_gpif = g_held_slide_gpif;
        const std::string landing = "<Fret>3</Fret>";
        const std::size_t landing_at = grow_gpif.find(landing);
        REQUIRE(landing_at != std::string::npos);
        grow_gpif.replace(landing_at, landing.size(), "<Fret>1</Fret>");

        const std::filesystem::path archive = writeFixtureArchive(scratch, grow_gpif);
        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        REQUIRE(song->arrangements.size() == 1);
        const common::core::Chart& chart = requiredChart(song->arrangements.front());

        // The held 5 still pins the top; the sliding 2->1 carries the bottom outward, so the
        // window grows to the exact hull [1,5] (width five) instead of dropping the held note.
        const common::core::FretHandPosition* const reshape = fretHandPositionAt(chart, waypoint);
        REQUIRE(reshape != nullptr);
        CHECK(reshape->fret == 1);
        CHECK(reshape->width == 5);
    }

    std::filesystem::remove_all(scratch, cleanup_error);
}

// One bar, quarter notes: a tied fret-6 whose continuation sits inside a chord and shift-slides
// down, next to a fret-8 chord member shift-sliding down, with the chord's fret-3 member tied
// through into the landing chord — the Periphery measure-20 shape the tie/slide/arpeggio rules
// were written for (policy rules 12, 13, 15).
constexpr const char* g_tied_chord_gpif = R"(<?xml version="1.0" encoding="utf-8"?>
<GPIF>
<GPVersion>8.1.4</GPVersion>
<Score>
<Title><![CDATA[TiedChord]]></Title>
<Artist><![CDATA[Tester]]></Artist>
<Album><![CDATA[Album]]></Album>
</Score>
<MasterTrack>
<Automations>
<Automation><Type>Tempo</Type><Bar>0</Bar><Position>0</Position><Value>120 2</Value></Automation>
</Automations>
</MasterTrack>
<BackingTrack><AssetId>0</AssetId></BackingTrack>
<Assets><Asset id="0"><EmbeddedFilePath>Content/Assets/audio.wav</EmbeddedFilePath></Asset></Assets>
<Tracks>
<Track id="0">
<Name>Guitar</Name>
<Staves><Staff><Properties>
<Property name="CapoFret"><Fret>0</Fret></Property>
<Property name="Tuning"><Pitches>40 45 50 55 59 64</Pitches></Property>
</Properties></Staff></Staves>
</Track>
</Tracks>
<MasterBars>
<MasterBar><Time>4/4</Time><Bars>0</Bars></MasterBar>
</MasterBars>
<Bars>
<Bar id="0"><Voices>0 -1 -1 -1</Voices></Bar>
</Bars>
<Voices>
<Voice id="0"><Beats>0 1 2</Beats></Voice>
</Voices>
<Beats>
<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>
<Beat id="1"><Rhythm ref="0"/><Notes>1 2 3</Notes></Beat>
<Beat id="2"><Rhythm ref="0"/><Notes>4 5 6</Notes></Beat>
</Beats>
<Notes>
<Note id="0"><Tie origin="true" destination="false"/><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>6</Fret></Property>
</Properties></Note>
<Note id="1"><Tie origin="false" destination="true"/><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>6</Fret></Property>
<Property name="Slide"><Flags>1</Flags></Property>
</Properties></Note>
<Note id="2"><Properties>
<Property name="String"><String>2</String></Property>
<Property name="Fret"><Fret>8</Fret></Property>
<Property name="Slide"><Flags>1</Flags></Property>
</Properties></Note>
<Note id="3"><Tie origin="true" destination="false"/><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>3</Fret></Property>
</Properties></Note>
<Note id="4"><Properties>
<Property name="String"><String>1</String></Property>
<Property name="Fret"><Fret>2</Fret></Property>
</Properties></Note>
<Note id="5"><Properties>
<Property name="String"><String>2</String></Property>
<Property name="Fret"><Fret>4</Fret></Property>
</Properties></Note>
<Note id="6"><Tie origin="false" destination="true"/><Properties>
<Property name="String"><String>0</String></Property>
<Property name="Fret"><Fret>3</Fret></Property>
</Properties></Note>
</Notes>
<Rhythms>
<Rhythm id="0"><NoteValue>Quarter</NoteValue></Rhythm>
</Rhythms>
</GPIF>
)";

TEST_CASE(
    "Guitar Pro import carries tied slides and derives ring-through arpeggios", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_tied_chord_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    const std::filesystem::path archive = writeFixtureArchive(scratch, g_tied_chord_gpif);
    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());

    // (position, string) order: the tied 6, the chord pair at beat 2, the landing pair at 3
    // (the fret-3 tie continuation merged away).
    REQUIRE(chart.notes.size() == 5);

    // The tied 6: its continuation merged in (sustain through the chord) and the continuation's
    // shift-slide flags folded into the merged note (rule 15). A hold waypoint pins fret 6
    // until the chord where the sliding segment was notated, then the glide waypoint runs to
    // its fret-2 landing, ending the minimum sustain distance before the landing onset (rule 13)
    // — the tail continues INTO the chord and up to the glide end.
    const common::core::ChartNote& tied = chart.notes[0];
    CHECK(tied.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(tied.string == 2);
    CHECK(tied.fret == 6);
    CHECK(tied.sustain == Fraction{7, 4});
    REQUIRE(tied.slides.size() == 2);
    CHECK(tied.slides[0].offset == Fraction{1});
    CHECK(tied.slides[0].fret == 6);
    CHECK(tied.slides[1].offset == Fraction{7, 4});
    CHECK(tied.slides[1].fret == 2);
    CHECK_FALSE(tied.slide_out.has_value());

    // The chord's fret 8 shift-slides toward its fret-4 landing (no hold: its own onset
    // carried the flags); the glide waypoint ends the minimum sustain distance before the landing.
    const common::core::ChartNote& eight = chart.notes[2];
    CHECK(eight.position == GridPosition{.measure = 1, .beat = 2});
    CHECK(eight.string == 3);
    CHECK(eight.fret == 8);
    REQUIRE(eight.slides.size() == 1);
    CHECK(eight.slides[0].offset == Fraction{3, 4});
    CHECK(eight.slides[0].fret == 4);
    CHECK(eight.sustain == Fraction{3, 4});

    // Both landings keep their own onsets (and heads) inside the beat-3 chord.
    CHECK(chart.notes[3].position == GridPosition{.measure = 1, .beat = 3});
    CHECK(chart.notes[3].string == 2);
    CHECK(chart.notes[3].fret == 2);
    CHECK(chart.notes[4].string == 3);
    CHECK(chart.notes[4].fret == 4);

    // Two arpeggio shapes split at the hand move (rule 12): the beat-2 chord's posture includes
    // the ringing 6, and the beat-3 landing chord — the re-picked 2 and 4 strummed while the
    // tied fret 3 keeps ringing — is its own arpeggio including that 3. The first span's end
    // trims to the minimum sustain distance before the landing onset (rule 12a — spans keep the
    // same margin as every other element) even though its tied member rings on, so the landing
    // reads as its own arrival with the standard gap.
    REQUIRE(chart.shapes.size() == 2);
    CHECK(chart.shapes[0].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(chart.shapes[0].sustain == Fraction{3, 4});
    REQUIRE(chart.shapes[0].chord < chart.templates.size());
    CHECK(
        chart.templates[chart.shapes[0].chord].frets ==
        std::vector<std::optional<int>>{3, 6, 8, std::nullopt, std::nullopt, std::nullopt});
    CHECK(common::core::chartShapeArrivesAsArpeggio(chart, chart.shapes[0], song->tempo_map));
    CHECK(chart.shapes[1].position == GridPosition{.measure = 1, .beat = 3});
    REQUIRE(chart.shapes[1].chord < chart.templates.size());
    CHECK(
        chart.templates[chart.shapes[1].chord].frets ==
        std::vector<std::optional<int>>{3, 2, 4, std::nullopt, std::nullopt, std::nullopt});
    CHECK(common::core::chartShapeArrivesAsArpeggio(chart, chart.shapes[1], song->tempo_map));

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

    // Beats one and two both strike the fret 3 + fret 5 power chord unmuted (a quarter then an
    // eighth); the single fret-7 note at 1:2+1/2 ends the held posture. The fixture's note 0 is
    // palm-muted, and a mute change is a span boundary, so the first strum uses an unmuted
    // fret-3 note instead.
    std::string gpif{g_fixture_gpif};
    const auto replace_once = [&gpif](const std::string& marker, const std::string& replacement) {
        const std::size_t position = gpif.find(marker);
        REQUIRE(position != std::string::npos);
        gpif.replace(position, marker.size(), replacement);
    };
    replace_once("<Notes>0</Notes>", "<Notes>9 6</Notes>");
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
        "<Note id=\"9\"><Properties>\n"
        "<Property name=\"String\"><String>0</String></Property>\n"
        "<Property name=\"Fret\"><Fret>3</Fret></Property>\n"
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

    // Both strums merge into one span from 1:1 toward the eighth strum's notated end at
    // 1:2+1/2, trimmed to the minimum sustain distance before the closing fret-7 onset there
    // (rule 12a) — even though the sustain policy dropped the notes' own tails.
    REQUIRE(chart.shapes.size() == 1);
    CHECK(chart.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.shapes.front().sustain == Fraction{5, 4});
    CHECK(chart.shapes.front().chord == 0);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Any articulation difference is a new chord: a palm mute or a hammered attack on the same
// frets ends the span and opens a new box, while both spans share one frets-deduplicated
// template.
TEST_CASE("Guitar Pro import splits chord spans on articulation changes", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_articulation_chord_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // The same power chord strummed twice — plain, then palm-muted or hammered: two spans, one
    // template either way.
    const std::string articulation_property = GENERATE(
        std::string{"<Property name=\"PalmMuted\"><Enable/></Property>\n"},
        std::string{"<Property name=\"HopoDestination\"><Enable/></Property>\n"});
    std::string gpif{g_fixture_gpif};
    const auto replace_once = [&gpif](const std::string& marker, const std::string& replacement) {
        const std::size_t position = gpif.find(marker);
        REQUIRE(position != std::string::npos);
        gpif.replace(position, marker.size(), replacement);
    };
    replace_once("<Notes>0</Notes>", "<Notes>9 6</Notes>");
    replace_once("<Notes>1</Notes>", "<Notes>7 8</Notes>");
    replace_once(
        "</Notes>\n<Rhythms>",
        "<Note id=\"6\"><Properties>\n"
        "<Property name=\"String\"><String>1</String></Property>\n"
        "<Property name=\"Fret\"><Fret>5</Fret></Property>\n"
        "</Properties></Note>\n"
        "<Note id=\"7\"><Properties>\n"
        "<Property name=\"String\"><String>0</String></Property>\n"
        "<Property name=\"Fret\"><Fret>3</Fret></Property>\n" +
            articulation_property +
            "</Properties></Note>\n"
            "<Note id=\"8\"><Properties>\n"
            "<Property name=\"String\"><String>1</String></Property>\n"
            "<Property name=\"Fret\"><Fret>5</Fret></Property>\n" +
            articulation_property +
            "</Properties></Note>\n"
            "<Note id=\"9\"><Properties>\n"
            "<Property name=\"String\"><String>0</String></Property>\n"
            "<Property name=\"Fret\"><Fret>3</Fret></Property>\n"
            "</Properties></Note>\n"
            "</Notes>\n<Rhythms>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());

    // One frets-identical template, but the palm-muted strum is its own chord: two spans, each
    // trimmed to the minimum sustain distance before the event that closes it (rule 12a) — the
    // differing strum at beat 2, then the fret-7 onset at 1:2+1/2.
    REQUIRE(chart.templates.size() == 1);
    REQUIRE(chart.shapes.size() == 2);
    CHECK(chart.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.shapes[0].sustain == Fraction{3, 4});
    CHECK(chart.shapes[0].chord == 0);
    CHECK(chart.shapes[1].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(chart.shapes[1].sustain == Fraction{1, 4});
    CHECK(chart.shapes[1].chord == 0);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A tie-merged tail that stops at a changed onset trims like any other (the old blanket tie
// exemption is gone — only rings crossing the next onset hold); the trimmed tail is over a
// beat, so the effect-free drop rule leaves it in place.
TEST_CASE("Guitar Pro import trims tie-merged sustains at a changed onset", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_sustain_keep_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Removing the vibrato from the tie-chain origin makes the merged four-beat note
    // effect-free; it still trims to the margin and keeps the beat-plus remainder.
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

// A two-string chord beat for the hold-semantics tests below.
[[nodiscard]] GpBeat chordBeat(
    const Fraction duration, const int fret_a, const int fret_b, const bool tie_origin,
    const bool tie_destination)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.notes = {
        GpNote{
            .string = 1,
            .fret = fret_a,
            .tie_origin = tie_origin,
            .tie_destination = tie_destination,
            .harmonic_type = ""
        },
        GpNote{
            .string = 2,
            .fret = fret_b,
            .tie_origin = tie_origin,
            .tie_destination = tie_destination,
            .harmonic_type = ""
        }
    };
    return beat;
}

// A single-note beat marked tremolo picked, for the spell-out tests below.
[[nodiscard]] GpBeat tremoloBeat(const Fraction duration, const int fret, const Fraction stroke)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.tremolo_stroke = stroke;
    beat.notes = {GpNote{.string = 1, .fret = fret, .harmonic_type = ""}};
    return beat;
}

} // namespace

// The sustain hold semantics (policy rule 1): every tail that merely
// reaches the next onset trims to the minimum sustain distance — ties and repeated chords included
// — while a ring notated strictly past the next onset is a deliberate hold and stays whole.
// Repeated chords keep their held reading through the merged shape span, which derives from
// notated pre-trim durations, so the box continues while the tails keep the gap.
TEST_CASE("Guitar Pro import trims reaching tails and holds crossing rings", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a repeated chord trims its tails while the merged span runs through the restrike")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(Fraction{1, 2}, 5, 7, false, false),
                     chordBeat(Fraction{1, 2}, 5, 7, false, false)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].sustain == Fraction{7, 4});
        CHECK(chart.notes[1].sustain == Fraction{7, 4});
        // One merged span from the first strum through the last strum's notated end.
        REQUIRE(chart.shapes.size() == 1);
        CHECK(chart.shapes[0].sustain == Fraction{4});
    }

    SECTION("a changed chord trims the tails the same way but splits the span")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(Fraction{1, 2}, 5, 7, false, false),
                     chordBeat(Fraction{1, 2}, 3, 5, false, false)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].sustain == Fraction{7, 4});
        CHECK(chart.notes[1].sustain == Fraction{7, 4});
        CHECK(chart.shapes.size() == 2);
    }

    SECTION("a tie-merged chord reaching a changed onset trims like any other")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(Fraction{1, 4}, 5, 7, true, false),
                     chordBeat(Fraction{1, 4}, 5, 7, false, true),
                     chordBeat(Fraction{1, 2}, 3, 5, false, false)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].sustain == Fraction{7, 4});
        CHECK(chart.notes[1].sustain == Fraction{7, 4});
    }

    SECTION("a ring notated across voices past the next onset is held whole")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat held;
        held.duration_whole = Fraction{1};
        held.notes = {
            GpNote{.string = 1, .fret = 5, .harmonic_type = ""},
            GpNote{.string = 2, .fret = 7, .harmonic_type = ""}
        };
        GpBeat rest;
        rest.duration_whole = Fraction{1, 4};
        GpBeat melody;
        melody.duration_whole = Fraction{1, 4};
        melody.notes = {GpNote{.string = 5, .fret = 8, .harmonic_type = ""}};
        score.tracks[0].bars.push_back(GpBar{.voices = {{held}, {rest, rest, melody, rest}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[0].sustain == Fraction{4});
        CHECK(chart.notes[1].sustain == Fraction{4});
    }
}

// Guitar Pro's tremolo picking is measured (the mark carries a stroke duration), and the chart
// reserves `tremolo` for unmeasured noise, so import spells the strokes out as individual notes
// at the marked subdivision. Ties into a tremolo beat release their origin (the strokes
// re-pick), the first stroke alone keeps the accent, and beats whose notes carry bends or slide
// payloads keep the mark with a conversion note instead.
// A harmonic is asserted by its node now, so import must always set one for a fret-hand harmonic —
// including when Guitar Pro's HarmonicFret matches the fret, or omits it. Storing it only when it
// differed (the old shape, where a separate field carried the harmonic) would now import the note as
// not a harmonic at all.
TEST_CASE("Guitar Pro import always gives a fret-hand harmonic its node", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    const auto importNote = [&syncs](const GpNote& note) {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{GpBeat{.duration_whole = Fraction{1, 4}, .notes = {note}}}}});
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        return chart.notes[0];
    };

    SECTION("a node equal to the fret is still stored")
    {
        const common::core::ChartNote note = importNote(
            GpNote{.string = 1, .fret = 12, .harmonic_type = "Natural", .harmonic_fret = 12.0});
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(12.0));
        }
        CHECK(common::core::isHarmonic(note.harmonic_node));
    }

    SECTION("a missing node falls back to the fret rather than leaving the note unharmonic")
    {
        const common::core::ChartNote note =
            importNote(GpNote{.string = 1, .fret = 7, .harmonic_type = "Natural"});
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            // Snapped to the 3rd partial's true node, not the "7" the score wrote.
            CHECK(*note.harmonic_node == Catch::Approx(7.0196).margin(0.001));
        }
        CHECK(common::core::isHarmonic(note.harmonic_node));
    }

    SECTION("a pinch becomes the attack and carries the node Guitar Pro recorded")
    {
        // For a FRETTED harmonic Guitar Pro's HarmonicFret is a partial LABEL, not a position:
        // measured across 118 files, HarmonicFret - Fret is scattered and 18 of 56 pinches name a
        // position below their own fret, which no thumb can reach. Fret units are logarithmic, so
        // the real node is fret + the label's offset. 24.0 labels the 4th partial's third node, so a
        // pinch stopped at 5 grazes at 29 — past the neck, over the pickups, exactly where a thumb
        // is.
        const common::core::ChartNote note = importNote(
            GpNote{.string = 1, .fret = 5, .harmonic_type = "Pinch", .harmonic_fret = 24.0});
        CHECK(note.attack == common::core::NoteAttack::Pinch);
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(29.0));
            // Beyond the stop, which the chart rules require.
            CHECK(*note.harmonic_node > static_cast<double>(note.fret));
        }
        CHECK(common::core::isHarmonic(note.harmonic_node));
        // Off the neck, so no 2D/3D anchor comes from it.
        CHECK_FALSE(common::core::anchorNode(note.harmonic_node, note.attack).has_value());
    }

    SECTION("a pinch Guitar Pro left without a fret defaults to the octave node")
    {
        // Unreached against real scores, but a pinch cannot be represented without its node. The
        // octave is the 2nd partial — the lowest-order harmonic available at any fret, so the
        // easiest to ring — which beats both dropping the technique and reading the stop as a label.
        const common::core::ChartNote note =
            importNote(GpNote{.string = 1, .fret = 5, .harmonic_type = "Pinch"});
        CHECK(note.attack == common::core::NoteAttack::Pinch);
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(17.0));
        }
    }
}

TEST_CASE("Guitar Pro import spells out tremolo picking", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a quarter beat with sixteenth strokes becomes four spelled-out notes")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        beat.notes.front().accent = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        for (std::size_t index = 0; index < chart.notes.size(); ++index)
        {
            CHECK_FALSE(chart.notes[index].tremolo);
            CHECK(chart.notes[index].fret == 5);
            CHECK(chart.notes[index].position.offset == Fraction{static_cast<int>(index), 4});
        }
        // Only the first stroke carries the notated accent; the rest are plain picks.
        CHECK(chart.notes[0].accent);
        CHECK_FALSE(chart.notes[1].accent);
        // Strokes normalize to sustainless like any hand-charted sixteenth run, the final
        // stroke's sub-margin tail included.
        CHECK(chart.notes[0].sustain == Fraction{});
        CHECK(chart.notes[3].sustain == Fraction{});
    }

    SECTION("a tie into a tremolo beat releases the origin and the strokes re-pick")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat first = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        first.notes.front().tie_origin = true;
        GpBeat second = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        second.notes.front().tie_destination = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{first, second}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Eight fresh onsets: nothing merges across the notated tie.
        REQUIRE(chart.notes.size() == 8);
        CHECK(chart.notes[4].position.beat == 2);
        CHECK(chart.notes[4].position.offset == Fraction{});
    }

    SECTION("a bent tremolo spells out as progressively larger prebends")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        beat.notes.front().bend = GpBend{
            .origin_value = 0.0,
            .middle_value = 50.0,
            .destination_value = 100.0,
        };
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // Each stroke samples the master curve at its own onset: silent start, then half,
        // whole, and one-and-a-half steps as flat prebends on sustainless picks.
        CHECK(chart.notes[0].bend.empty());
        REQUIRE(chart.notes[1].bend.size() == 1);
        CHECK(chart.notes[1].bend[0].offset == Fraction{});
        CHECK(chart.notes[1].bend[0].semitones == Catch::Approx(0.5));
        REQUIRE(chart.notes[3].bend.size() == 1);
        CHECK(chart.notes[3].bend[0].semitones == Catch::Approx(1.5));
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK_FALSE(note.tremolo);
        }
    }

    SECTION("a slide-carrying tremolo beat keeps its mark with a conversion note")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        beat.notes.front().slide_flags = 4;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].tremolo);
        CHECK(anyNoteContains(built->notes, "tremolo beats kept their mark"));
    }

    SECTION("a beat no longer than one stroke drops the mark as a single pick")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{tremoloBeat(Fraction{1, 16}, 5, Fraction{1, 16})}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK_FALSE(chart.notes[0].tremolo);
    }
}

// A bend notated on a tie continuation belongs to the merged origin note, not a new onset: the
// continuation carries no head, so its bend curve rebases onto the open origin — every point
// shifted by the onset gap (continuation onset minus origin onset) and appended only while the
// offset keeps climbing, so the merged curve stays strictly ascending. The standing bend tests
// only bend a standalone note, and the tie chains only carry vibrato; this covers the
// tie-continuation bend fold in buildChart.
TEST_CASE("Guitar Pro import folds a tied continuation's bend onto the origin", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    // A fret-5 quarter tied into a fret-5 quarter continuation that bends a whole step (0 to 100%
    // over its own one-beat sustain). The continuation onset sits one beat after the origin, so
    // its bend points at 0, 1/2, and 1 beat rebase to 1, 3/2, and 2 beats on the merged note.
    GpBeat origin;
    origin.duration_whole = Fraction{1, 4};
    origin.notes = {GpNote{
        .string = 1, .fret = 5, .tie_origin = true, .tie_destination = false, .harmonic_type = ""
    }};
    GpBeat continuation;
    continuation.duration_whole = Fraction{1, 4};
    continuation.notes = {GpNote{
        .string = 1,
        .fret = 5,
        .tie_origin = false,
        .tie_destination = true,
        .harmonic_type = "",
        .bend = GpBend{
            .origin_value = 0.0,
            .middle_value = 50.0,
            .destination_value = 100.0,
            .origin_offset = 0.0,
            .middle_offset1 = 50.0,
            .middle_offset2 = 50.0,
            .destination_offset = 100.0,
        }
    }};

    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(GpBar{.voices = {{origin, continuation}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;

    // The continuation merges away, leaving one fret-5 note ringing across both beats.
    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& merged = chart.notes[0];
    CHECK(merged.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(merged.string == 2);
    CHECK(merged.fret == 5);
    CHECK(merged.sustain == Fraction{2});

    // The folded curve: every continuation point offset by the one-beat onset gap, semitones
    // intact, offsets strictly ascending.
    REQUIRE(merged.bend.size() == 3);
    CHECK(merged.bend[0].offset == Fraction{1});
    CHECK(merged.bend[0].semitones == Catch::Approx(0.0));
    CHECK(merged.bend[1].offset == Fraction{3, 2});
    CHECK(merged.bend[1].semitones == Catch::Approx(1.0));
    CHECK(merged.bend[2].offset == Fraction{2});
    CHECK(merged.bend[2].semitones == Catch::Approx(2.0));
    CHECK(merged.bend[0].offset < merged.bend[1].offset);
    CHECK(merged.bend[1].offset < merged.bend[2].offset);
}

namespace
{

// One single-note beat on the given zero-based string for the grace and slide-in tests below.
[[nodiscard]] GpBeat noteBeat(
    const Fraction duration, const int fret, const int string = 0, const int slide_flags = 0)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.notes = {
        GpNote{.string = string, .fret = fret, .slide_flags = slide_flags, .harmonic_type = ""}
    };
    return beat;
}

// The same beat marked as a grace with the given placement.
[[nodiscard]] GpBeat graceBeat(
    const GpGracePlacement placement, const int fret, const int string = 0)
{
    GpBeat beat = noteBeat(Fraction{1, 32}, fret, string);
    beat.grace = placement;
    return beat;
}

// One dead-string pick-slide carrier: Guitar Pro notates the gesture as a fully muted fret-0
// note carrying Slide flag 64 (down) or 128 (up).
[[nodiscard]] GpNote pickSlideCarrier(const int flags, const int string = 0)
{
    GpNote note;
    note.string = string;
    note.fret = 0;
    note.full_mute = true;
    note.slide_flags = flags;
    return note;
}

// One beat holding the given carrier notes for the pick-slide tests below.
[[nodiscard]] GpBeat carrierBeat(const Fraction duration, std::vector<GpNote> carriers)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.notes = std::move(carriers);
    return beat;
}

// One note carrying a bend curve that rises from unbent, for the payload-trim tests below.
// Values are Guitar Pro's percent-of-a-whole-step scale (100 = one whole step = two semitones)
// and offsets are percent of the note duration; the plateau is a single point (both middle
// offsets coincide), so the curve reads origin, middle, destination.
[[nodiscard]] GpNote bentNote(
    const int string, const int fret, const double middle_value, const double destination_value,
    const double middle_offset, const double destination_offset)
{
    GpNote note;
    note.string = string;
    note.fret = fret;
    note.bend = GpBend{
        .origin_value = 0.0,
        .middle_value = middle_value,
        .destination_value = destination_value,
        .origin_offset = 0.0,
        .middle_offset1 = middle_offset,
        .middle_offset2 = middle_offset,
        .destination_offset = destination_offset,
    };
    return note;
}

// One beat holding the given notes verbatim.
[[nodiscard]] GpBeat beatOf(const Fraction duration, std::vector<GpNote> beat_notes)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.notes = std::move(beat_notes);
    return beat;
}

} // namespace

// Policy rule 3: the sub-beat drop decision belongs to the notated strum, not the single string.
// Runs in 4/4, where the minimum sustain distance is a quarter beat.
TEST_CASE("Guitar Pro import keeps a chord's tails together", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a chord's unbent partner keeps a tail beside its bent one")
    {
        // Both strings of the double stop ring from one stroke, so the plain partner keeps its
        // margin-trimmed tail instead of losing it to the sub-beat drop rule while the bent
        // string shows a lone tail. The lengths differ because only one string carries the bend:
        // rule 2 decides each tail's length per string, rule 3 only shares the keep-or-drop
        // verdict.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {beatOf(
                         Fraction{1, 8},
                         {bentNote(0, 5, 50.0, 100.0, 50.0, 100.0),
                          GpNote{.string = 1, .fret = 7, .harmonic_type = ""}}),
                     noteBeat(Fraction{1, 8}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[0].string == 1);
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
        CHECK(chart.notes[1].string == 2);
        CHECK(chart.notes[1].sustain == Fraction{1, 4});
    }

    SECTION("a strum with nothing to say still loses every tail")
    {
        // The companion case: no member carries a technique and the notated ring is sub-beat, so
        // the whole strum drops its tails and renders as plain heads.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(Fraction{1, 8}, 5, 7, false, false), noteBeat(Fraction{1, 8}, 3, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[0].sustain == Fraction{});
        CHECK(chart.notes[1].sustain == Fraction{});
    }
}

// Policy rule 2: carrying a technique is not an exemption from the minimum sustain distance. The
// margin yields only to payload that CHANGES something, and only as far as that change reaches.
// Runs in 4/4, where the margin is a quarter beat.
TEST_CASE("Guitar Pro import trims technique tails to their last information", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a bend plateau running to the notated end does not exempt the tail from the margin")
    {
        // The curve reaches two semitones a quarter beat in and then holds that value to the
        // notated end. The plateau is not information, so the one-beat tail trims to the margin
        // before the next onset and the redundant final point leaves with it.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {beatOf(Fraction{1, 4}, {bentNote(0, 5, 100.0, 100.0, 25.0, 100.0)}),
                     noteBeat(Fraction{1, 4}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{3, 4});
        REQUIRE(chart.notes[0].bend.size() == 2);
        CHECK(chart.notes[0].bend[0].offset == Fraction{});
        CHECK(chart.notes[0].bend[1].offset == Fraction{1, 4});
        CHECK(chart.notes[0].bend[1].semitones == Catch::Approx(2.0));
    }

    SECTION("a bend change inside the trimmed region extends the tail to exactly that change")
    {
        // The destination lands at 95% of a two-beat note — past the margin limit of 7/4 — so
        // the tail overrides the margin, but only out to the change itself, not to the notated
        // end at two beats.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {beatOf(Fraction{1, 2}, {bentNote(0, 5, 50.0, 100.0, 50.0, 95.0)}),
                     noteBeat(Fraction{1, 2}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{19, 10});
        REQUIRE(chart.notes[0].bend.size() == 3);
        CHECK(chart.notes[0].bend.back().offset == Fraction{19, 10});
        CHECK(chart.notes[0].bend.back().semitones == Catch::Approx(2.0));
    }

    SECTION("a change landing exactly at the margin truncates there and keeps its information")
    {
        // 87.5% of two beats is 7/4 — precisely the margin limit. The tail ends there with the
        // bend's full information intact.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {beatOf(Fraction{1, 2}, {bentNote(0, 5, 50.0, 100.0, 50.0, 87.5)}),
                     noteBeat(Fraction{1, 2}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{7, 4});
        REQUIRE(chart.notes[0].bend.size() == 3);
        CHECK(chart.notes[0].bend.back().offset == Fraction{7, 4});
        CHECK(chart.notes[0].bend.back().semitones == Catch::Approx(2.0));
    }

    SECTION("a trailing equal-fret hold waypoint holds no tail open")
    {
        // A legato slide onto the same fret is a hold, not a glide: it pins a pitch the note is
        // already sounding, so it cannot override the margin. The tail trims and the waypoint
        // leaves with it (the hold-versus-glide distinction the 3D hand window also reads).
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 5, 0, 2),
                     noteBeat(Fraction{1, 32}, 5, 0),
                     noteBeat(Fraction{1, 32}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].sustain == Fraction{7, 8});
        CHECK(chart.notes[0].slides.empty());
    }

    SECTION("a scrape's path still ends exactly at its trimmed sustain")
    {
        // The scrape's path is gesture geometry, so the trim compresses its final point with the
        // tail rather than flooring on it — and the pick-slide rule that the path end IS the
        // sustain still holds afterward. R is a half beat here, exactly 2d, so the gesture still
        // yields the full margin.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {carrierBeat(Fraction{1, 8}, {pickSlideCarrier(64)}),
                     noteBeat(Fraction{1, 8}, 3, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::PickSlide);
        CHECK(chart.notes[0].sustain == Fraction{1, 4});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
        CHECK(chart.notes[0].slides.back().fret != chart.notes[0].fret);
    }
}

// Policy rule 19's crowding case: a scrape's path is derived gesture geometry, so when the room
// runs short the terminal leg is crunched, and where the leg STARTS decides how. A leg starting
// before the margin line ends on it, giving up no spacing at all. A leg starting on or after that
// line is already inside the window, so it halves its distance to the onset — the exception the
// spacing rule sanctions, since the gesture is literally defined in there. Runs in 4/4, where the
// minimum sustain distance is a quarter beat.
TEST_CASE("Guitar Pro import squishes a crowded scrape against its gap", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    // The scrape's notated span always equals R here, so the gesture merely REACHES the next
    // onset — a span notated past it would be a deliberate hold and keep its full length.
    const auto crowded_scrape = [&syncs](const Fraction span) {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{carrierBeat(span, {pickSlideCarrier(64)}), noteBeat(span, 3, 2)}}});
        return buildGpSong(score);
    };

    SECTION("room above twice the margin still yields the full margin")
    {
        // R = one beat, comfortably above 2d: the gap is the whole quarter-beat margin.
        const auto built = crowded_scrape(Fraction{1, 4});
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{3, 4});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
    }

    SECTION("room exactly at the conflict threshold still pays the full margin")
    {
        // R = 3/8 beat = d + w, the tightest room where the full margin costs the leg nothing it
        // cannot spare: gap = d = 1/4 and the leg takes the remaining 1/8, landing exactly on its
        // window. Nothing is crunched because nothing conflicts.
        const auto built = crowded_scrape(Fraction{3, 32});
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{1, 8});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
        CHECK(chart.notes[0].slides.back().fret != chart.notes[0].fret);
    }

    SECTION("room above the threshold pays the margin whole rather than sharing it")
    {
        // R = 7/16 beat, inside the band an earlier revision of this rule crunched: it set the gap
        // to min(d, R/2) = 7/32, manufacturing a spacing violation where the full 1/4 margin was
        // affordable. The margin is paid whole and the leg takes 3/16.
        const auto built = crowded_scrape(Fraction{7, 64});
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{3, 16});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
    }

    SECTION("room equal to the margin halves it, the worked example")
    {
        // R = 1/16 whole note against d = 1/16: a 1/32-whole-note gesture and a 1/32 gap — a
        // quarter beat of room splitting into an eighth beat each way. A genuine conflict, since
        // paying d whole would leave the leg nothing, and the even split lands it exactly on its
        // window.
        const auto built = crowded_scrape(Fraction{1, 16});
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{1, 8});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
    }

    SECTION("the tightest room still keeps a gap, because halving always leaves one")
    {
        // R = 1/8 beat, half the margin: the leg starts well inside the window, so it halves the
        // room and both sides get 1/16. The old compression floor forced the leg up to 1/8 here
        // and left NO gap at all — exact adjacency with the next onset — which is precisely the
        // spacing the rule exists to protect, so dropping that floor improves the tightest case
        // rather than costing anything. The path still ends exactly at the sustain.
        const auto built = crowded_scrape(Fraction{1, 32});
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{1, 16});
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
        CHECK(chart.notes[0].slides.back().fret != chart.notes[0].fret);
    }
}

// Pick-slide carriers convert in place into pick-slide notes: the dead carrier is Guitar Pro's
// encoding vehicle for the right-hand gesture, so it sheds its mute and gains the attack plus
// the corpus-derived default path (down 17 -> 3, up the mirror), ready for reshaping.
TEST_CASE("Guitar Pro import converts pick-slide flags into pick-slide notes", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a dead carrier with flag 64 becomes one down pick-slide note")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 5),
                     carrierBeat(Fraction{1, 4}, {pickSlideCarrier(64)}),
                     noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        const common::core::ChartNote& scrape = chart.notes[1];
        CHECK(scrape.attack == common::core::NoteAttack::PickSlide);
        CHECK(scrape.position.beat == 2);
        CHECK(scrape.string == 1);
        CHECK(scrape.fret == 17);
        CHECK(scrape.mute == common::core::NoteMute::None);
        // The path ends at the sustain, which keeps the ordinary quarter-beat margin before
        // the fret-8 onset one beat later.
        REQUIRE(scrape.slides.size() == 1);
        CHECK(scrape.slides[0].offset == Fraction{3, 4});
        CHECK(scrape.slides[0].fret == 3);
        CHECK(scrape.sustain == Fraction{3, 4});
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[2].fret == 8);
    }

    SECTION("flag 128 mirrors the default path upward")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{carrierBeat(Fraction{1, 4}, {pickSlideCarrier(128)})}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::PickSlide);
        CHECK(chart.notes[0].fret == 3);
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides[0].fret == 17);
        CHECK(chart.notes[0].sustain == Fraction{1});
    }

    SECTION("simultaneous same-direction carriers each become a scrape on their own string")
    {
        // One scrape sounds on every string the pick crosses, so both carriers survive as notes
        // rather than collapsing onto one string — otherwise a two-string scrape imports as a
        // one-string scrape and the chart understates what is played.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{carrierBeat(
                    Fraction{1, 4}, {pickSlideCarrier(64, 0), pickSlideCarrier(64, 1)})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        for (const common::core::ChartNote& scrape : chart.notes)
        {
            CHECK(scrape.attack == common::core::NoteAttack::PickSlide);
            CHECK(scrape.position.beat == chart.notes.front().position.beat);
            CHECK(scrape.fret == 17);
            // One gesture, so both carry the same travel and end together.
            CHECK(scrape.sustain == chart.notes.front().sustain);
        }
        // Distinct strings, one per carrier.
        CHECK(chart.notes[0].string != chart.notes[1].string);
    }

    SECTION("simultaneous carriers share the longest notated span")
    {
        // The pick reaches the end of its travel once, so a shorter carrier does not cut the
        // gesture short on its own string.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat =
            carrierBeat(Fraction{1, 4}, {pickSlideCarrier(64, 0), pickSlideCarrier(64, 1)});
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == chart.notes[1].sustain);
        REQUIRE_FALSE(chart.notes[0].slides.empty());
        REQUIRE_FALSE(chart.notes[1].slides.empty());
        CHECK(chart.notes[0].slides.back().offset == chart.notes[0].sustain);
        CHECK(chart.notes[1].slides.back().offset == chart.notes[1].sustain);
    }

    SECTION("conflicting simultaneous directions keep the first and report")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{carrierBeat(
                    Fraction{1, 4}, {pickSlideCarrier(64, 0), pickSlideCarrier(128, 1)})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].fret == 17);
        bool reported = false;
        for (const std::string& note : built->notes)
        {
            reported =
                reported || note.find("conflicting simultaneous pick-slide") != std::string::npos;
        }
        CHECK(reported);
    }

    SECTION("scrapes hold through later scrapes exactly as notes hold through onsets")
    {
        // Voice one notates a two-beat scrape; voice two starts another one beat in. The first
        // rings strictly past the second's notated onset, so the deliberate-hold exemption
        // keeps its full span — the same distance rules as any note.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {carrierBeat(Fraction{1, 2}, {pickSlideCarrier(64, 0)})},
                    {carrierBeat(Fraction{1, 4}, {}),
                     carrierBeat(Fraction{1, 4}, {pickSlideCarrier(64, 1)})}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides[0].offset == Fraction{2});
        CHECK(chart.notes[1].sustain == Fraction{1});
    }

    SECTION("a note tail keeps the sustain margin before a scrape onset")
    {
        // A two-beat note trims its tail to the quarter-beat margin before the scrape's onset,
        // exactly as it would before any note.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 2}, 5),
                     carrierBeat(Fraction{1, 4}, {pickSlideCarrier(64)})}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{7, 4});
    }

    SECTION("a scrape notated ringing past a later onset stays a deliberate hold")
    {
        // Scraping through sounding strings is physically real, so a two-beat scrape notated
        // across a later note keeps its span, like any held tail.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {carrierBeat(Fraction{1, 2}, {pickSlideCarrier(64)})},
                    {carrierBeat(Fraction{1, 4}, {}), noteBeat(Fraction{1, 4}, 5, 1)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::PickSlide);
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides[0].offset == Fraction{2});
    }

    SECTION("the fret-hand track is identical with the gesture present or absent")
    {
        // The transparency invariant, at the import seam: the same score with the carrier beat
        // as a rest produces the identical fret-hand track (the carrier never reaches the
        // generator), so the gesture cannot move, anchor, or dip the window.
        const auto make_score = [&syncs](const bool with_carrier) {
            GpScore score = makeLinearScore(1, syncs);
            score.tracks[0].bars.push_back(
                GpBar{
                    .voices = {
                        {noteBeat(Fraction{1, 4}, 5),
                         carrierBeat(
                             Fraction{1, 4},
                             with_carrier ? std::vector<GpNote>{pickSlideCarrier(64)}
                                          : std::vector<GpNote>{}),
                         noteBeat(Fraction{1, 4}, 8),
                         noteBeat(Fraction{1, 4}, 10)}
                    }
                });
            return score;
        };

        const auto with_gesture = buildGpSong(make_score(true));
        const auto without_gesture = buildGpSong(make_score(false));
        REQUIRE(with_gesture.has_value());
        REQUIRE(without_gesture.has_value());
        CHECK(
            with_gesture->arrangements.front().chart.fret_hand_positions ==
            without_gesture->arrangements.front().chart.fret_hand_positions);
    }

    SECTION("dead notes with ordinary slide-out flags stay muted notes")
    {
        // The measure-3 figure class: a fret-hand-muted note carrying plain slide-out flags is
        // a LEFT-hand gesture and must never reclassify.
        GpScore score = makeLinearScore(1, syncs);
        GpNote dead_slide;
        dead_slide.string = 0;
        dead_slide.fret = 5;
        dead_slide.full_mute = true;
        dead_slide.slide_flags = 4;
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{carrierBeat(Fraction{1, 4}, {dead_slide})}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].attack != common::core::NoteAttack::PickSlide);
        CHECK(chart.notes[0].mute == common::core::NoteMute::Full);
        CHECK(chart.notes[0].slide_out.has_value());
    }

    SECTION("the fret-hand track ignores a carrier alone in a rest")
    {
        // The rest-driven anchor path: a phrase break re-anchors the hand, and a carrier
        // sitting alone where the rest would be must not turn the break into an anchor move.
        const auto make_score = [&syncs](const bool with_carrier) {
            GpScore score = makeLinearScore(1, syncs);
            score.tracks[0].bars.push_back(
                GpBar{.voices = {{noteBeat(Fraction{1, 4}, 5), noteBeat(Fraction{1, 4}, 8)}}});
            score.tracks[0].bars.push_back(
                GpBar{
                    .voices = {{carrierBeat(
                        Fraction{1, 2},
                        with_carrier ? std::vector<GpNote>{pickSlideCarrier(64)}
                                     : std::vector<GpNote>{})}}
                });
            return score;
        };

        const auto with_gesture = buildGpSong(make_score(true));
        const auto without_gesture = buildGpSong(make_score(false));
        REQUIRE(with_gesture.has_value());
        REQUIRE(without_gesture.has_value());
        CHECK(
            with_gesture->arrangements.front().chart.fret_hand_positions ==
            without_gesture->arrangements.front().chart.fret_hand_positions);
    }

    SECTION("the fret-hand track ignores a carrier sharing an onset with a fretted mate")
    {
        const auto make_score = [&syncs](const bool with_carrier) {
            GpScore score = makeLinearScore(1, syncs);
            GpNote mate;
            mate.string = 2;
            mate.fret = 7;
            std::vector<GpNote> onset{mate};
            if (with_carrier)
            {
                onset.push_back(pickSlideCarrier(64));
            }
            score.tracks[0].bars.push_back(
                GpBar{
                    .voices = {
                        {noteBeat(Fraction{1, 4}, 5),
                         carrierBeat(Fraction{1, 4}, onset),
                         noteBeat(Fraction{1, 4}, 10)}
                    }
                });
            return score;
        };

        const auto with_gesture = buildGpSong(make_score(true));
        const auto without_gesture = buildGpSong(make_score(false));
        REQUIRE(with_gesture.has_value());
        REQUIRE(without_gesture.has_value());
        CHECK(
            with_gesture->arrangements.front().chart.fret_hand_positions ==
            without_gesture->arrangements.front().chart.fret_hand_positions);
    }

    SECTION("a carrier with a tremolo stroke converts once instead of spelling out")
    {
        // The pick-slide vocabulary carries the noise intrinsically (chart.h), so a carrier
        // beat marked tremolo is exempt from the measured spell-out and sheds the flag.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = carrierBeat(Fraction{1, 2}, {pickSlideCarrier(64)});
        beat.tremolo_stroke = Fraction{1, 8};
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::PickSlide);
        CHECK_FALSE(chart.notes[0].tremolo);
    }
}

// Grace beats take no bar time; the import places them against their principal:
// a before-beat grace sounds a thirty-second-note lead ahead of the principal, an
// on-beat grace sounds on the principal's position and delays the principal by the same lead.
// The lead halves when the neighboring onset sits closer than the full lead, and a grace with
// no room at all is dropped.
TEST_CASE("Guitar Pro import places grace notes against their principal", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a before-beat grace sounds a thirty-second before the principal")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 5),
                     graceBeat(GpGracePlacement::BeforeBeat, 7),
                     noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.measure == 1);
        CHECK(chart.notes[1].position.beat == 1);
        CHECK(chart.notes[1].position.offset == Fraction{7, 8});
        CHECK(chart.notes[2].fret == 8);
        CHECK(chart.notes[2].position.beat == 2);
        CHECK(chart.notes[2].position.offset == Fraction{});
        // The grace lead sounds inside the fret-5 note's notated tail, but a fabricated onset
        // cannot make that ring a deliberate hold: the tail trims against the grace's sounding
        // beat (7/8 minus the 1/4 margin) and, notated a full beat, keeps the trimmed tail.
        CHECK(chart.notes[0].sustain == Fraction{5, 8});
    }

    SECTION("a hold notated across voices stays a hold with a grace inside it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 2}, 3)},
                    {noteBeat(Fraction{1, 4}, 5, 1),
                     graceBeat(GpGracePlacement::BeforeBeat, 7, 1),
                     noteBeat(Fraction{1, 4}, 8, 1)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // The half note rings strictly past the second voice's NOTATED beat-2 onset, so the
        // deliberate cross-voice hold survives even though the grace's fabricated onset sounds
        // earlier inside it; the fret-5 quarter under the same grace trims against the grace's
        // sounding beat and, notated a full beat, keeps the trimmed tail.
        CHECK(chart.notes[0].fret == 3);
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].fret == 5);
        CHECK(chart.notes[1].sustain == Fraction{5, 8});
    }

    SECTION("an on-beat grace takes the beat and delays the principal")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {graceBeat(GpGracePlacement::OnBeat, 7),
                     noteBeat(Fraction{1, 4}, 8),
                     noteBeat(Fraction{1, 4}, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[0].fret == 7);
        CHECK(chart.notes[0].position.beat == 1);
        CHECK(chart.notes[0].position.offset == Fraction{});
        CHECK(chart.notes[1].fret == 8);
        CHECK(chart.notes[1].position.beat == 1);
        CHECK(chart.notes[1].position.offset == Fraction{1, 8});
        CHECK(chart.notes[2].position.beat == 2);
    }

    SECTION("a before-beat grace crosses the bar line backward")
    {
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1}, 5)}}});
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {graceBeat(GpGracePlacement::BeforeBeat, 7), noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.measure == 1);
        CHECK(chart.notes[1].position.beat == 4);
        CHECK(chart.notes[1].position.offset == Fraction{7, 8});
        CHECK(chart.notes[2].position.measure == 2);
        CHECK(chart.notes[2].position.beat == 1);
    }

    SECTION("a crowded lead halves against the previous onset")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 32}, 5),
                     graceBeat(GpGracePlacement::BeforeBeat, 7),
                     noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.offset == Fraction{1, 16});
    }

    SECTION("a grace before the song's first onset has no room and is dropped")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {graceBeat(GpGracePlacement::BeforeBeat, 7), noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(anyNoteContains(built->notes, "grace-note beats had no room"));
    }
}

// A grace ornaments through any technique, not only slides: the grace beat's notes flow through
// the same technique mapping as every note, and hammer/pull classification reads the previous
// fret on the string — which the grace supplies, since it sounds (and is processed) before its
// principal. Each fixture's pre-grace note sits on the OTHER side of the principal, so a wrong
// classification source would flip the expected attack.
TEST_CASE("Guitar Pro import maps grace-note hammer-ons and pull-offs", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a grace below the principal hammers on")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat principal = noteBeat(Fraction{1, 4}, 7);
        principal.notes[0].hopo_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 9),
                     graceBeat(GpGracePlacement::BeforeBeat, 5),
                     principal}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].fret == 5);
        CHECK(chart.notes[1].attack == common::core::NoteAttack::Pick);
        // The principal rises from the grace's fret 5 (not the earlier fret 9, which would read
        // as a pull-off), so the destination is a hammer-on.
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Hammer);
    }

    SECTION("a grace above the principal pulls off")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat principal = noteBeat(Fraction{1, 4}, 8);
        principal.notes[0].hopo_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 5),
                     graceBeat(GpGracePlacement::BeforeBeat, 10),
                     principal}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].fret == 10);
        // The principal falls from the grace's fret 10 (not the earlier fret 5, which would
        // read as a hammer-on), so the destination is a pull-off.
        CHECK(chart.notes[2].fret == 8);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Pull);
    }
}

// Tap-only onsets are transparent to span derivation: a chord held under
// two-hand tapping keeps its span ringing through the taps, and the arrival rule renders the
// covered span as a held arpeggio; a chord whose ring ends before the taps is unaffected.
TEST_CASE("Guitar Pro import rings chord spans through tap-only onsets", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    // Two-string chord against a second voice: a beat of rest, then two tapped eighths that
    // sound inside the chord's notated ring.
    const auto make_score = [&syncs](const Fraction chord_duration) {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat chord;
        chord.duration_whole = chord_duration;
        chord.notes = {
            GpNote{.string = 0, .fret = 3, .harmonic_type = ""},
            GpNote{.string = 1, .fret = 5, .harmonic_type = ""},
        };
        GpBeat rest;
        rest.duration_whole = Fraction{1, 4};
        GpBeat tap_one = noteBeat(Fraction{1, 8}, 12, 5);
        tap_one.notes[0].tapped = true;
        GpBeat tap_two = noteBeat(Fraction{1, 8}, 14, 5);
        tap_two.notes[0].tapped = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{chord}, {rest, tap_one, tap_two}}});
        return score;
    };

    SECTION("a held chord's span covers the taps and reads as an arpeggio")
    {
        const auto built = buildGpSong(make_score(Fraction{1, 2}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // The half-note chord rings strictly past the taps' notated beats — a deliberate
        // cross-voice hold (rule 1) — so its tails survive under the tapping.
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Tap);
        // One span from the strum through its notated ring: the taps neither close nor trim it,
        // and the covered span arrives as a held arpeggio.
        REQUIRE(chart.shapes.size() == 1);
        CHECK(chart.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(chart.shapes[0].sustain == Fraction{2});
        CHECK(common::core::chartShapeArrivesAsArpeggio(chart, chart.shapes[0], built->tempo_map));
    }

    SECTION("a short-ringing chord's span still ends before the taps")
    {
        const auto built = buildGpSong(make_score(Fraction{1, 4}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // The quarter chord's ring ends at the first tap's onset, so the span keeps its own
        // notated duration, the taps land outside it, and the box stays a strummed box.
        REQUIRE(chart.shapes.size() == 1);
        CHECK(chart.shapes[0].sustain == Fraction{1});
        CHECK_FALSE(
            common::core::chartShapeArrivesAsArpeggio(chart, chart.shapes[0], built->tempo_map));
    }

    SECTION("a left-hand note under simultaneous right-hand taps derives no chord")
    {
        // The two-hand-tapping staple (a real-score regression): a left-hand tap struck
        // together with two right-hand taps. The taps are invisible to span derivation, so the
        // onset counts one non-tap member — an ordinary single onset, no chord posture — and
        // the dense run that follows cannot crowd a derived span into zero length.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat mixed;
        mixed.duration_whole = Fraction{1, 32};
        mixed.notes = {
            GpNote{.string = 2, .fret = 9, .harmonic_type = ""},
            GpNote{.string = 4, .fret = 12, .harmonic_type = ""},
            GpNote{.string = 5, .fret = 14, .harmonic_type = ""},
        };
        mixed.notes[0].left_hand_tapped = true;
        mixed.notes[1].tapped = true;
        mixed.notes[2].tapped = true;
        GpBeat follow = noteBeat(Fraction{1, 32}, 11, 2);
        follow.notes[0].hopo_destination = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{mixed, follow}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Hammer);
        CHECK(chart.notes[1].attack == common::core::NoteAttack::Tap);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Tap);
        CHECK(chart.notes[3].attack == common::core::NoteAttack::Hammer);
        CHECK(chart.shapes.empty());
    }
}

// A dense run can close a span exactly at its notated end, inside the margin: the crowded close
// falls back to exact adjacency (the earlier of the notated ring and the closing onset) so the
// span keeps positive length instead of collapsing to zero and failing chart validation.
TEST_CASE("Guitar Pro import keeps crowded chord spans at positive length", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    GpBeat chord;
    chord.duration_whole = Fraction{1, 32};
    chord.notes = {
        GpNote{.string = 0, .fret = 3, .harmonic_type = ""},
        GpNote{.string = 1, .fret = 5, .harmonic_type = ""},
    };
    score.tracks[0].bars.push_back(GpBar{.voices = {{chord, noteBeat(Fraction{1, 32}, 7)}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    // The closing onset lands exactly on the 1/8-beat chord's notated end, closer than the
    // margin: the span ends there, exact-adjacent, rather than collapsing to zero.
    REQUIRE(chart.shapes.size() == 1);
    CHECK(chart.shapes[0].sustain == Fraction{1, 8});
}

// The gpif spells grace placement as the GraceNotes element's text ("OnBeat" for Ctrl+Shift+G
// graces, "BeforeBeat" for plain ones); both spellings must arrive with their timing semantics.
// The fixture's eighth beat becomes the grace, so the beats after it close up by its duration.
TEST_CASE("Guitar Pro import reads gpif grace placements", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_grace_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    SECTION("OnBeat takes the beat and delays the principal")
    {
        const std::string gpif = fixtureWithReplacement(
            R"(<Beat id="1"><Rhythm ref="1"/><Notes>1</Notes></Beat>)",
            R"(<Beat id="1"><Rhythm ref="1"/><GraceNotes>OnBeat</GraceNotes>)"
            R"(<Notes>1</Notes></Beat>)");
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);
        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() == 5);
        CHECK(chart.notes[1].fret == 5);
        CHECK(chart.notes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.notes[2].fret == 7);
        CHECK(
            chart.notes[2].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 8}});
    }

    SECTION("BeforeBeat leads the principal")
    {
        const std::string gpif = fixtureWithReplacement(
            R"(<Beat id="1"><Rhythm ref="1"/><Notes>1</Notes></Beat>)",
            R"(<Beat id="1"><Rhythm ref="1"/><GraceNotes>BeforeBeat</GraceNotes>)"
            R"(<Notes>1</Notes></Beat>)");
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);
        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() == 5);
        CHECK(chart.notes[1].fret == 5);
        CHECK(
            chart.notes[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{7, 8}});
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].position == GridPosition{.measure = 1, .beat = 2});
    }
}

// A bare slide-in imports as an on-beat scoop — an ordinary slide in the note's own slot:
// the head keeps its notated position at a derived approach fret
// and rises to the notated fret over the scoop window (a quarter of the notated duration,
// capped at the margin, floored at the minimum slide window). Guitar Pro gives the gesture no
// start fret, so the fret-hand positions supply it; the flag's stated direction wins over a
// contradicting placement delta, a still hand falls back to two frets out in the flag's
// direction, an agreeing one-fret hand move widens to the same two-fret minimum, and an
// open-string landing stays plain. The hand stays planted (a scoop is a finger gesture). A
// grace note sliding into its principal carries the explicit start fret instead and resolves
// through the ordinary slide chain — that pathway is how anticipation is notated.
TEST_CASE("Guitar Pro import derives slide-in ramps from the hand positions", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a hand move consistent with the flag supplies the start fret")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), noteBeat(Fraction{1, 4}, 8, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // The window walk moves the anchor minimally (3 to 5), so the head departs two frets
        // below the notated fret: 8 + (3 - 5) — ON its notated beat — scooping to 8 over a
        // quarter of the notated beat (capped exactly at the margin here). The sustain stays
        // the notated duration.
        CHECK(chart.notes[1].fret == 6);
        CHECK(chart.notes[1].position.beat == 2);
        CHECK(chart.notes[1].position.offset == Fraction{});
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].slides[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{1});
        // No onset was fabricated, so the fret-3 tail trims the plain margin before the
        // scoop's notated beat.
        CHECK(chart.notes[0].sustain == Fraction{3, 4});
        // The 5-8 window already covers the fret-6 approach, so the hand stays planted: the
        // track is exactly the natural walk, with nothing fabricated at the scoop's end.
        REQUIRE(chart.fret_hand_positions.size() == 2);
        CHECK(chart.fret_hand_positions[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(chart.fret_hand_positions[0].fret == 3);
        CHECK(chart.fret_hand_positions[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[1].fret == 5);
    }

    SECTION("a long landing caps the scoop window at the margin")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), noteBeat(Fraction{1, 2}, 8, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // A half note's quarter would be a half-beat scoop; the margin caps it at 1/4.
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].sustain == Fraction{2});
    }

    SECTION("a short landing floors the scoop window at the minimum")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), noteBeat(Fraction{1, 16}, 8, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // A sixteenth's quarter (1/16 beat) reads as nothing; the window floors at 1/8.
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 8});
        CHECK(chart.notes[1].sustain == Fraction{1, 4});
    }

    SECTION("a landing shorter than the floored window extends its sustain to fit")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), noteBeat(Fraction{1, 64}, 8, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // A sixty-fourth sustains 1/16 beat — shorter than the 1/8 floor — so the sustain
        // extends to hold the floored scoop (the waypoint may land exactly on the end).
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 8});
        CHECK(chart.notes[1].sustain == Fraction{1, 8});
    }

    SECTION("an existing chain waypoint halves the scoop window")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Flags 17 = slide-in from below (16) + shift slide (1): the chain resolver first
        // glides the note to the fret-10 landing, then the scoop is inserted ahead of that
        // waypoint. A thirty-second head with the landing an eighth of a beat later gives a
        // degenerate gap, so the shift waypoint sits at half of it (1/16) — under the 1/8
        // floor the scoop would otherwise take, so the scoop halves the waypoint instead and
        // the payload stays strictly ascending.
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{noteBeat(Fraction{1, 32}, 8, 0, 17), noteBeat(Fraction{1, 32}, 10)}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        REQUIRE(chart.notes[0].slides.size() == 2);
        CHECK(chart.notes[0].slides[0].offset == Fraction{1, 32});
        CHECK(chart.notes[0].slides[0].fret == 8);
        CHECK(chart.notes[0].slides[1].offset == Fraction{1, 16});
        CHECK(chart.notes[0].slides[1].fret == 10);
        CHECK(chart.notes[0].slides[0].offset < chart.notes[0].slides[1].offset);
        CHECK(chart.notes[0].fret == 6);
    }

    SECTION("a bend on the landing coexists with the scoop")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat bent = noteBeat(Fraction{1, 4}, 8, 0, 16);
        // A rise to a whole step at the half: bend points land at 0, 1/2, and 1 beat, and
        // the quarter-beat scoop waypoint interleaves between the first two. Bend curves
        // order only against the sustain, so the scoop leaves them untouched.
        bent.notes[0].bend = GpBend{
            .origin_value = 0.0,
            .middle_value = 100.0,
            .destination_value = 100.0,
            .origin_offset = 0.0,
            .middle_offset1 = 50.0,
            .middle_offset2 = 50.0,
            .destination_offset = 100.0,
        };
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), bent}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[1].fret == 6);
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].slides[0].fret == 8);
        REQUIRE_FALSE(chart.notes[1].bend.empty());
        CHECK(chart.notes[1].bend.back().offset == Fraction{1});
    }

    SECTION("a slide-out on the same short note keeps the scoop strictly before it")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Flags 20 = slide-in from below (16) + downward trail-off (4) on one thirty-second
        // note: the trail-off end pins at the 1/8-beat sustain, exactly where the floored
        // scoop window would land, so the scoop halves to stay strictly before it (the
        // ascending-payload invariant chart validation enforces).
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 32}, 8, 0, 20)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].fret == 6);
        REQUIRE(chart.notes[0].slides.size() == 1);
        CHECK(chart.notes[0].slides[0].offset == Fraction{1, 16});
        CHECK(chart.notes[0].slides[0].fret == 8);
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->offset == Fraction{1, 8});
    }

    SECTION("a one-fret hand move widens to the two-fret minimum")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), noteBeat(Fraction{1, 4}, 7, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // The window walk moves the anchor minimally (3 to 4, since the width-4 window reaches
        // fret 7 from there) — a one-fret delta — but the approach never travels less than two
        // frets: the head departs at 5, not 6.
        CHECK(chart.notes[1].fret == 5);
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].fret == 7);
    }

    SECTION("a slide-in into a held landing keeps the hold")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 3),
                     noteBeat(Fraction{1, 8}, 8, 0, 16),
                     noteBeat(Fraction{1, 8}, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // The transform runs before the sustain policy, so the half-beat landing hold
        // survives the effect-free drop (the note is a slide now) and trims only against the
        // next onset like any tail: head ON its beat, scoop over an eighth (a quarter of the
        // half-beat duration, floored at the minimum slide window), tail to a quarter before
        // the fret-5 onset.
        CHECK(chart.notes[1].fret == 6);
        CHECK(chart.notes[1].position.beat == 2);
        CHECK(chart.notes[1].position.offset == Fraction{});
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 8});
        CHECK(chart.notes[1].slides[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{1, 4});
        // No onset was fabricated: the fret-3 tail trims the plain margin before the scoop's
        // notated beat.
        CHECK(chart.notes[0].sustain == Fraction{3, 4});
    }

    SECTION("a still hand falls back to two frets in the flag's direction")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 5), noteBeat(Fraction{1, 4}, 5, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[1].fret == 3);
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].fret == 5);

        // The fret-3 approach falls below the window anchored at 5, so the window dips with
        // the scoop for exactly its duration — the onset's window derives backward from the
        // active one (5 minus the +2 scoop delta) — and the natural window returns at the
        // scoop's quarter-beat end.
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(chart.fret_hand_positions[0].fret == 5);
        CHECK(chart.fret_hand_positions[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[1].fret == 3);
        CHECK(
            chart.fret_hand_positions[2].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
        CHECK(chart.fret_hand_positions[2].fret == 5);
    }

    SECTION("a from-above flag rides a downward hand move")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8), noteBeat(Fraction{1, 4}, 3, 0, 32)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // The anchor walks from 8 down to 3, so the head departs the full delta above: 3 + 5.
        CHECK(chart.notes[1].fret == 8);
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].fret == 3);
    }

    SECTION("the flag's direction wins over a contradicting hand move")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8), noteBeat(Fraction{1, 4}, 3, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[1].fret == 1);
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].fret == 3);

        // The fret-1 approach falls below the 3-6 window, so a dip REPLACES the natural
        // placement at the scoop's onset (positions stay unique — one entry at that
        // instant) and the restore brings the natural anchor back at the scoop's end.
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(chart.fret_hand_positions[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[1].fret == 1);
        CHECK(
            chart.fret_hand_positions[2].position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
        CHECK(chart.fret_hand_positions[2].fret == 3);
    }

    SECTION("an open-string landing stays plain")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 5), noteBeat(Fraction{1, 4}, 0, 0, 16)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[1].fret == 0);
        CHECK(chart.notes[1].slides.empty());
        CHECK(anyNoteContains(built->notes, "slide-ins had no representable start"));
    }

    SECTION("a grace slide into its principal keeps the explicit start fret")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat grace = graceBeat(GpGracePlacement::BeforeBeat, 5);
        grace.notes[0].slide_flags = 1;
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3), grace, noteBeat(Fraction{1, 4}, 7)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // The grace note itself glides to the principal's fret; the principal keeps its own
        // head and fret untouched.
        CHECK(chart.notes[1].fret == 5);
        REQUIRE_FALSE(chart.notes[1].slides.empty());
        CHECK(chart.notes[1].slides.back().fret == 7);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].slides.empty());
    }
}

// The crush fallback compresses a crowded trail-off to the SMALLEST LEGAL end rather than
// keeping its full length (normalization rule 2): strictly positive, and strictly after the
// note's last chain waypoint. A legato chain inheriting a trail-off is where the waypoint floor
// bites — the plain margin target lands on the junction itself, so the end steps one minimum
// window past it instead of colliding with the glide.
TEST_CASE(
    "Guitar Pro import floors a crushed trail-off after its last waypoint", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    // Flags 2 = legato: the fret-10 landing folds into the fret-8 origin as a waypoint one
    // beat in, and its own flags-4 trail-off carries onto the merged note. The fret-5 onset a
    // quarter beat after the landing then crowds the gesture: the margin target lands exactly
    // on the junction, so the floor pushes the end to 1 + 1/8.
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {noteBeat(Fraction{1, 4}, 8, 0, 2),
                 noteBeat(Fraction{1, 16}, 10, 0, 4),
                 noteBeat(Fraction{1, 4}, 5)}
            }
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 2);

    const common::core::ChartNote& merged = chart.notes[0];
    REQUIRE(merged.slides.size() == 1);
    CHECK(merged.slides[0].offset == Fraction{1});
    CHECK(merged.slides[0].fret == 10);
    const auto* const slide_out = common::core::slideOutOrNull(merged);
    REQUIRE(slide_out != nullptr);
    // One minimum slide window past the junction — never on or before it, which chart
    // validation rejects, and never the uncompressed 5/4 end that would run past the onset.
    CHECK(slide_out->offset == Fraction{9, 8});
    CHECK(slide_out->offset > merged.slides.back().offset);
    CHECK(merged.sustain == Fraction{9, 8});
}

// The window always rides an unpitched trail-off; the hand's next move
// picks the figure. A next placement departing in the trail-off's direction AND serving the very
// next onset makes the gesture a departure: the exit fret rides that anchor travel (widened to
// the slide-in rule's two-fret minimum) and the window flows onward. Otherwise it is a release:
// the fixed four-fret exit, the window dipping with it and a restore at the next onset so no
// note is stranded. The edges override both: a gesture with no room before the next onset stays
// planted, and one with no note after it may rest where it ends.
TEST_CASE("Guitar Pro import chooses the trail-off window figure", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a departing hand carries the exit fret downward")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8, 0, 4), noteBeat(Fraction{1, 4}, 3)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);

        // The hand's next anchor departs 8 -> 3, agreeing with the downward trail-off, so the
        // exit rides the full five-fret travel instead of the fixed four: 8 + (3 - 8) = 3.
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 3);
        // The trim compresses the trail-off to the margin before the fret-3 onset, and the
        // exit placement lands exactly on that compressed end so the window rides the gesture
        // into the fret-3 arrival.
        CHECK(slide_out->offset == Fraction{3, 4});
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        CHECK(chart.fret_hand_positions[1].fret == 3);
        CHECK(chart.fret_hand_positions[2].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[2].fret == 3);
    }

    SECTION("a departing hand carries the exit fret upward")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 3, 0, 8), noteBeat(Fraction{1, 4}, 8)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);

        // The upward mirror: the hand's next anchor departs 3 -> 5 (the minimal move that
        // reaches fret 8), agreeing with the flags-8 trail-off, so the exit rides the widened
        // travel 3 + max(+2, +2) = 5 rather than the four-fret default 7.
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 5);
        CHECK(slide_out->offset == Fraction{3, 4});
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].fret == 3);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        CHECK(chart.fret_hand_positions[1].fret == 5);
        CHECK(chart.fret_hand_positions[2].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[2].fret == 5);
    }

    SECTION("an upward trail-off at the top of the neck clamps to the last fret")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Fret 28 with the four-fret upward default would exit at 32 — off a 30-fret neck.
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 28, 0, 8), noteBeat(Fraction{1, 4}, 28)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == common::core::g_max_fret);
        // The hand never moves, so this is a release: the exit window must still cover the
        // clamped exit fret without running off the neck itself.
        const common::core::FretHandPosition* const exit = fretHandPositionAt(
            chart, GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        REQUIRE(exit != nullptr);
        CHECK(exit->fret >= 1);
        CHECK(exit->fret <= common::core::g_max_fret);
        CHECK(exit->fret + exit->width > common::core::g_max_fret);
    }

    SECTION("a lingering hand releases and restores the window")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 8, 0, 4),
                     noteBeat(Fraction{1, 4}, 8),
                     noteBeat(Fraction{1, 4}, 3)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);

        // The hand's move to fret 3 serves the THIRD onset, not the next one, so the exit
        // keeps the fixed four-fret release — but the window still rides it: a dip at the
        // compressed trail-off end and a restore at the second onset, real move untouched.
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 4);
        REQUIRE(chart.fret_hand_positions.size() == 4);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        CHECK(chart.fret_hand_positions[1].fret == 4);
        CHECK(chart.fret_hand_positions[2].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[2].fret == 8);
        CHECK(chart.fret_hand_positions[3].position == GridPosition{.measure = 1, .beat = 3});
        CHECK(chart.fret_hand_positions[3].fret == 3);
    }

    SECTION("a hand that never moves still releases and restores")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8, 0, 4), noteBeat(Fraction{1, 4}, 8)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);

        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 4);
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        CHECK(chart.fret_hand_positions[1].fret == 4);
        CHECK(chart.fret_hand_positions[2].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[2].fret == 8);
    }

    SECTION("chord mates trailing off together share one exit window")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat chord;
        chord.duration_whole = Fraction{1, 4};
        // The mates scrape apart — the lower string down (flags 4), the upper up (flags 8) —
        // so their exits want opposite windows at the same instant, which is the only figure
        // where ownership of that instant is observable.
        chord.notes = {
            GpNote{.string = 0, .fret = 8, .slide_flags = 4, .harmonic_type = ""},
            GpNote{.string = 1, .fret = 8, .slide_flags = 8, .harmonic_type = ""}
        };
        score.tracks[0].bars.push_back(GpBar{.voices = {{chord, noteBeat(Fraction{1, 4}, 8)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);

        // Each mate classifies against the onset AFTER the chord — never against the mate
        // sitting at its own beat, which would read as no room and plant the gesture — so
        // both fabricate an exit at the shared compressed end. The first inserted owns the
        // instant and the track keeps its unique-ascending positions; a duplicate would break
        // the placement invariant.
        for (std::size_t index = 1; index < chart.fret_hand_positions.size(); ++index)
        {
            CHECK(
                chart.fret_hand_positions[index - 1].position <
                chart.fret_hand_positions[index].position);
        }
        const common::core::FretHandPosition* const exit = fretHandPositionAt(
            chart, GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        REQUIRE(exit != nullptr);
        // The lower string's downward exit (fret 4) owns the window, not the upper string's
        // upward one (fret 12) that yields to it.
        CHECK(exit->fret < chart.fret_hand_positions.front().fret);
    }

    SECTION("the song's last note gets an exit and no restore")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8, 0, 4)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        // Nothing follows, so nothing compresses the gesture: the full one-beat span with
        // the four-fret default, and the window rests where it ends.
        CHECK(slide_out->fret == 4);
        CHECK(slide_out->offset == Fraction{1});
        REQUIRE(chart.fret_hand_positions.size() == 2);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(chart.fret_hand_positions[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.fret_hand_positions[1].fret == 4);
    }

    SECTION("a trail-off ending on the next onset stays planted")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Two thirty-seconds an eighth of a beat apart: the trail-off's smallest legal end
        // (the 1/8 minimum window, which the crush fallback keeps) IS the next onset, so
        // there is no room to ride and no placement is fabricated.
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 32}, 8, 0, 4), noteBeat(Fraction{1, 32}, 8)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 4);
        CHECK(slide_out->offset == Fraction{1, 8});
        // Both notes sit at fret 8: the natural walk is one placement, and the planted
        // gesture adds nothing.
        REQUIRE(chart.fret_hand_positions.size() == 1);
        CHECK(chart.fret_hand_positions[0].fret == 8);
    }

    SECTION("no room to ride plants even an agreeing departure")
    {
        GpScore score = makeLinearScore(1, syncs);
        // The hand's next placement departs downward and serves the very next onset — a
        // departure by every other measure — but the trail-off end coincides with that
        // onset, so the planted rule wins: the exit fret keeps the four-fret default
        // instead of riding the five-fret travel.
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 32}, 8, 0, 4), noteBeat(Fraction{1, 32}, 3)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const auto* const slide_out = common::core::slideOutOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(slide_out->fret == 4);
        CHECK(slide_out->offset == Fraction{1, 8});
        REQUIRE(chart.fret_hand_positions.size() == 2);
        CHECK(chart.fret_hand_positions[0].fret == 8);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{1, 8}});
        CHECK(chart.fret_hand_positions[1].fret == 3);
    }
}

// The phrase-aware fret-hand generator tracks the LEFT hand: a tapped note floats above the
// window instead of dragging the anchor up to it, matching how authored charts anchor two-hand
// tapping (the source corpus puts taps a median seven frets above the anchor).
TEST_CASE("Guitar Pro import anchors the hand below tapped notes", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    GpBeat fretted = noteBeat(Fraction{1, 4}, 3);
    GpBeat tap = noteBeat(Fraction{1, 4}, 15);
    tap.notes[0].tapped = true;
    GpBeat low = noteBeat(Fraction{1, 4}, 5);
    score.tracks[0].bars.push_back(GpBar{.voices = {{fretted, tap, low}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;

    // The tapped fret-15 note never anchors the hand: every generated position stays down where
    // the fretted notes (3 and 5) are, none up in the tap's region — the greedy walk would have
    // dragged the window all the way to fret 15.
    REQUIRE_FALSE(chart.fret_hand_positions.empty());
    for (const common::core::FretHandPosition& fhp : chart.fret_hand_positions)
    {
        CHECK(fhp.fret <= 5);
    }
}

// An opening open-string note anchors nothing, so it must not pin the hand at the nut-reference
// window: the first placement comes from the first fretted note and
// retimes back to the chart's first note, so the window is already settled there at song start.
TEST_CASE(
    "Guitar Pro import bases the opening position on the first fretted note", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    const GpBeat open = noteBeat(Fraction{1, 4}, 0);
    const GpBeat fretted = noteBeat(Fraction{1, 4}, 7);
    score.tracks[0].bars.push_back(GpBar{.voices = {{open, fretted}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;

    REQUIRE_FALSE(chart.fret_hand_positions.empty());
    CHECK(chart.fret_hand_positions.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.fret_hand_positions.front().fret == 7);
}

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

// A hold waypoint places NO hand position, not even at a phrase boundary. Nothing travels across an
// equal-fret waypoint, so it announces no new hand position; the generator's coverage suppression
// normally drops it anyway, because a hold cannot shift the hand and the window that covered the
// note still covers it. A phrase boundary bypasses that suppression on purpose, so the hand can
// re-anchor to a new phrase's floor — and a hold riding that bypass re-anchored the hand mid-note,
// beats into a held note, before the slide that was the actual reason to move. Found on a tie chain
// that held fret 11 across three beats and then trailed off into a sectioned measure.
TEST_CASE(
    "Guitar Pro import places no hand position at a hold on a phrase boundary", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    GpScore score = makeLinearScore(2, syncs);
    // The section marker on bar 2 is what makes this reproduce: it is a phrase boundary, so the
    // event landing there re-anchors instead of being suppressed as already-covered.
    score.master_bars[1].section = "Chorus";

    // Bar 1: a low fret sets a window that already covers 11, then the 11 is tied onward. Bar 2:
    // the continuation trails off, so the merged note's hold waypoint lands on bar 2 beat 1 —
    // exactly the boundary — with no other onset there to justify a placement.
    GpBeat origin = noteBeat(Fraction{7, 8}, 11, 2);
    origin.notes[0].tie_origin = true;
    GpBeat continuation = noteBeat(Fraction{1, 4}, 11, 2, 8);
    continuation.notes[0].tie_destination = true;
    score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 8}, 8, 1), origin}}});
    score.tracks[0].bars.push_back(
        GpBar{.voices = {{continuation, noteBeat(Fraction{1, 4}, 3, 2)}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;

    const auto held = std::ranges::find_if(
        chart.notes, [](const common::core::ChartNote& note) { return note.fret == 11; });
    REQUIRE(held != chart.notes.end());
    REQUIRE_FALSE(held->slides.empty());
    // The waypoint the continuation left behind holds the same fret, which is what makes it a hold.
    CHECK(held->slides.front().fret == held->fret);

    const GridPosition hold_position = common::core::advanceGridPosition(
        built->tempo_map, held->position, held->slides.front().offset);
    CHECK(hold_position == GridPosition{.measure = 2, .beat = 1});
    const bool placed_at_hold = std::ranges::any_of(
        chart.fret_hand_positions, [&hold_position](const common::core::FretHandPosition& fhp) {
            return fhp.position == hold_position;
        });
    CHECK_FALSE(placed_at_hold);
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
