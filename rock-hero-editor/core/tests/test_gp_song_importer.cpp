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
#include <rock_hero/common/audio/testing/audio_fixtures.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
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

// True when any generated fret-hand position sits at the given fret.
[[nodiscard]] bool hasFretHandPositionAtFret(const common::core::Chart& chart, const int fret)
{
    return std::ranges::any_of(
        chart.fret_hand_positions,
        [fret](const common::core::FretHandPosition& fhp) { return fhp.fret == fret; });
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
    // deliberate sustain (user rule 2026-07-28).
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
    // minimum-distance trim applies like any other tail (policy rule 1 holds, 2026-07-22).
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

// An unpitched trail-off releases pressure instead of repositioning (normalization policy rule
// 9), so its waypoint never moves the hand: the fret-hand track stays the plain onset walk.
TEST_CASE("Guitar Pro import keeps the hand still through unpitched slides", "[core][gp-import]")
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
    // The trail-off is not a protected payload (rule 2 carve-out, user rule 2026-07-28): its
    // notated half-beat end trims back with the sustain to the minimum-sustain-distance margin
    // before the fret-7 onset.
    CHECK(slide_out->offset == Fraction{1, 4});
    CHECK(chart.notes[1].sustain == Fraction{1, 4});

    // Were the trail-off treated as a pitched glide, its fret-1 target would drag a hand position
    // down mid-sustain; instead it never repositions the hand (no fret-1 position), and the
    // fret-7 landing shifts the window minimally to a fret-4 window at its onset.
    CHECK(chart.fret_hand_positions.front().fret == 3);
    CHECK_FALSE(hasFretHandPositionAtFret(chart, 1));
    const common::core::FretHandPosition* const landing =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    REQUIRE(landing != nullptr);
    CHECK(landing->fret == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Guitar Pro's two tap articulations are different hands and must import differently (user rule
// 2026-07-28): "Tapped" (two-hand tapping) becomes the chart's Tap attack, while
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
        // Left-hand is the specialization; the generic tap mark adds nothing to it (user rule
        // 2026-07-28).
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

} // namespace

// The sustain hold semantics (policy rule 1, user rule 2026-07-22): every tail that merely
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

} // namespace

// Grace beats take no bar time; the import places them against their principal (user rules
// 2026-07-27): a before-beat grace sounds a thirty-second-note lead ahead of the principal, an
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

// Tap-only onsets are transparent to span derivation (user rule 2026-07-28): a chord held under
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

// A bare slide-in imports as an ordinary slide — no new notation: the head moves onto the lead
// at a derived start fret and glides to the notated fret, whose position keeps no head of its
// own. Guitar Pro gives the gesture no start fret, so the fret-hand positions supply it: the
// head departs from the same window slot in the preceding placement. The flag's stated
// direction wins over a contradicting placement delta, a still hand falls back to two frets
// out in the flag's direction, an agreeing one-fret hand move widens to the same two-fret
// minimum, and an open-string landing stays plain. A grace note sliding
// into its principal carries the explicit start fret instead and resolves through the
// ordinary slide chain.
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
        // below the notated fret: 8 + (3 - 5), a quarter-beat lead ahead, gliding to 8 at the
        // notated position. The sustain end stays at the notated end.
        CHECK(chart.notes[1].fret == 6);
        CHECK(chart.notes[1].position.beat == 1);
        CHECK(chart.notes[1].position.offset == Fraction{3, 4});
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].slides[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{5, 4});
        // The moved head sounds inside the fret-3 note's notated tail, but a fabricated onset
        // cannot make that ring a deliberate hold: the tail trims against the head's sounding
        // beat at 3/4 (same string — the old ring overlapped the ramp) and, notated a full
        // beat, keeps the trimmed tail.
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
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
        // frets (user rule 2026-07-29): the head departs at 5, not 6.
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
        // next onset like any tail: head at 0.75 beats, glide at +1/4, tail to 1.25 (a
        // quarter before the fret-5 onset at 1.5 beats).
        CHECK(chart.notes[1].fret == 6);
        CHECK(chart.notes[1].position.offset == Fraction{3, 4});
        REQUIRE(chart.notes[1].slides.size() == 1);
        CHECK(chart.notes[1].slides[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].slides[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{1, 2});
        // The fret-3 tail trims against the moved head's sounding beat rather than ringing
        // through it as a fabricated hold, and — notated a full beat — keeps the trimmed tail.
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
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

        // Window-neutral ramp (user rule 2026-07-28): the head's placement derives backward
        // from the target's natural window — anchor 5 minus the +2 ramp delta, keeping the
        // head on the target's slot — and the target keeps its unmodified window, pinned by a
        // placement landing exactly on the glide's waypoint.
        REQUIRE(chart.fret_hand_positions.size() == 3);
        CHECK(chart.fret_hand_positions[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(chart.fret_hand_positions[0].fret == 5);
        CHECK(
            chart.fret_hand_positions[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{3, 4}});
        CHECK(chart.fret_hand_positions[1].fret == 3);
        CHECK(chart.fret_hand_positions[2].position == GridPosition{.measure = 1, .beat = 2});
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
// window (user rule 2026-07-28): the first placement comes from the first fretted note and
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
