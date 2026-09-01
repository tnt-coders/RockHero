#include "project/gp_chart_builder.h"
#include "project/gp_score.h"
#include "project/gp_score_parser.h"
#include "project/gp_song_importer.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <rock_hero/common/audio/testing/audio_fixtures.h>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/package/archive_io.h>
#include <rock_hero/common/core/package/package_id.h>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::Fraction;
using common::core::GridPosition;

// One reading of a note's bend channel: the offset a value is stated at and the value itself.
// The channel is spread across the note's onset amount and the keyframes that continue it, so a
// test asserting on the whole curve reads it back through `bendCurve` below rather than through
// one field.
struct BendReading
{
    Fraction offset{};
    double semitones{0.0};
};

// A note's bend channel as the curve the source wrote: the onset value first, then every keyframe
// stating one. Empty for a note whose channel never leaves rest, matching what the projection
// draws — an unbent note has no curve at all, not a curve of one zero.
[[nodiscard]] std::vector<BendReading> bendCurve(const common::core::ChartNote& note)
{
    if (!common::core::noteIsBent(note))
    {
        return {};
    }
    std::vector<BendReading> curve{BendReading{.offset = Fraction{}, .semitones = note.bend}};
    for (const common::core::Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<double>& bend = keyframe.bend;
        if (bend.has_value())
        {
            curve.push_back(BendReading{.offset = keyframe.offset, .semitones = *bend});
        }
    }
    return curve;
}

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

// What the surfaces DRAW from an imported chart. Import stores the ACTUAL ring durations Guitar
// Pro notated, so a test pinning what a note looks like — the readability the import policy used
// to bake in — asks the presentation rules for it (chart_presentation.h, which owns those rules
// and is tested on its own in common/core; these assertions pin the importer's output THROUGH
// them, not the rules themselves).
[[nodiscard]] std::vector<common::core::ChartNote> presentedNotesOf(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map)
{
    return common::core::presentedChartNotes(
        common::core::chartConnections(chart.notes, tempo_map).saved_notes, tempo_map);
}

// The hand-posture spans an imported chart implies. The chart stores none — every reader derives
// them from the notes (chart_shapes.h, whose rule is tested on its own in common/core) — so these
// assertions pin the importer's NOTES through that derivation, not the derivation itself.
[[nodiscard]] common::core::ChartShapes spansOf(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map)
{
    common::core::ChartResolutions resolutions =
        common::core::chartResolutions(chart.notes, tempo_map);
    return common::core::ChartShapes{
        .shapes = std::move(resolutions.shapes),
        .postures = std::move(resolutions.postures),
    };
}

// The frets a posture actually holds, with the trailing unheld strings dropped. A posture array is
// indexed by string number and is therefore always the model's string bound wide, so spelling that
// width into every expectation would state the bound rather than the hand shape — and a spuriously
// held string above the shape still fails, because it survives the trim.
[[nodiscard]] std::vector<std::optional<int>> heldFrets(const common::core::ChartPosture& posture)
{
    std::vector<std::optional<int>> held = posture.frets;
    while (!held.empty() && !held.back().has_value())
    {
        held.pop_back();
    }
    return held;
}

// Which of an imported chart's spans render arpeggio-style. The rule reads the same presented
// stream, for the same reason: whether a posture string still sounds across a span start is a
// question about what is drawn, not about what the note stores.
[[nodiscard]] std::vector<bool> shapeArrivalsOf(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map)
{
    const common::core::ChartShapes derived = spansOf(chart, tempo_map);
    return common::core::chartShapeArrivals(
        presentedNotesOf(chart, tempo_map), derived.shapes, tempo_map);
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
    const std::vector<common::core::ChartNote> presented = presentedNotesOf(chart, song->tempo_map);

    // Quarter palm mute on the low string, notated at capo-relative fret 3 and stored at the
    // absolute 5 (the fixture has a CAPO AT 2, and GP's frame is capo-relative). It STORES its
    // notated one-beat ring; what it draws is 3/4, trimmed against the next onset one beat later
    // (minimum-sustain-distance margin 1/4 in 4/4), and it keeps that drawn tail because a full
    // beat notated is a deliberate sustain.
    CHECK(chart.notes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.notes[0].string == 1);
    CHECK(chart.notes[0].fret == 5);
    CHECK(chart.notes[0].palm_mute);
    CHECK_FALSE(chart.notes[0].dead);
    CHECK(chart.notes[0].sustain == Fraction{1});
    CHECK(presented[0].sustain == Fraction{3, 4});

    // Legato destination that shift-slides into the next note: an ordinary pitched keyframe
    // glides to the landing (absolute fret 9) and ARRIVES the minimum sustain distance (1/16
    // whole note — a quarter beat in 4/4) before the landing's onset. The string rings on to that
    // landing, which re-picks it, so the stored ring is the whole half-beat gap while the drawn
    // tail stops at the arrival. The score says the notes connect but not which way, which is
    // exactly what the stored claim says — no direction is imported.
    CHECK(chart.notes[1].position == GridPosition{.measure = 1, .beat = 2});
    CHECK(chart.notes[1].attack == common::core::NoteAttack::Legato);
    REQUIRE(chart.notes[1].keyframes.size() == 1);
    CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 4});
    CHECK(chart.notes[1].keyframes[0].fret == 9);
    CHECK_FALSE(chart.notes[1].slide_out.has_value());
    CHECK(chart.notes[1].sustain == Fraction{1, 2});
    CHECK(presented[1].sustain == Fraction{1, 4});

    CHECK(
        chart.notes[2].position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    CHECK(chart.notes[2].fret == 9);

    // The tie chain merges into one note whose ring crosses the barline: onset 1:3, a quarter in
    // bar one plus a half in bar two makes four notated beats, all four stored. The ring only
    // reaches — never crosses — the changed onset at 2:3, so it is no deliberate hold and the
    // drawn tail keeps the margin before it like any other.
    CHECK(chart.notes[3].position == GridPosition{.measure = 1, .beat = 3});
    CHECK(chart.notes[3].string == 2);
    CHECK(chart.notes[3].sustain == Fraction{4});
    CHECK(presented[3].sustain == Fraction{15, 4});
    // The score marks the shake on the tie's ORIGIN and not on its continuation, so the merged
    // ring shakes from its onset and stops where the continuation begins — two beats in, on a
    // keyframe that states nothing else. The whole-note flag this replaced could only smear the
    // shake across the continuation it was never written on.
    // The score's word is `Slight`, which is Guitar Pro's house label for the ORDINARY vibrato
    // and resolves onto the chart's narrow tier — the tier mapping read at its own seam.
    CHECK(chart.notes[3].vibrato == common::core::VibratoState::Narrow);
    REQUIRE(chart.notes[3].keyframes.size() == 1);
    CHECK(chart.notes[3].keyframes[0].offset == Fraction{2});
    CHECK(chart.notes[3].keyframes[0].vibrato == common::core::VibratoState::Off);
    CHECK_FALSE(chart.notes[3].keyframes[0].fret.has_value());

    // Between-fret natural harmonic with the GP bend mapped to [offset, semitones] pairs. Bound to
    // a local so the node check and its reads are provably the same object.
    const common::core::ChartNote& harmonic = chart.notes[4];
    CHECK(harmonic.position == GridPosition{.measure = 2, .beat = 3});
    CHECK(harmonic.attack == common::core::NoteAttack::Pick);
    REQUIRE(harmonic.harmonic_node.has_value());
    if (harmonic.harmonic_node.has_value())
    {
        // The score writes "3.2", a conventional label naming the 6th partial. Two corrections land
        // here: the label resolves to that partial's true offset (3.156, not 3.2, since a position
        // even slightly off chokes the harmonic), and it is placed against the real stop — the
        // capo at 2, so the string speaks from there and its 6th-partial node sits at 5.156.
        // GP's frame is confirmed capo-relative, which is exactly what capo + snapped offset
        // encodes.
        CHECK(*harmonic.harmonic_node == Catch::Approx(5.1564).margin(0.001));
        // Under the 0-means-open convention a natural harmonic's fret is 0 even on a capo'd
        // string — the capo never appears as a fret number — while the node stays absolute.
        CHECK(harmonic.fret == 0);
        CHECK(*harmonic.harmonic_node > static_cast<double>(chart.tuning.capo));
        // The fretting hand is at the node it touches, not down at the capo.
        CHECK(common::core::fretFor(harmonic) == 6);
    }
    // The score notates a bend ON the natural harmonic — data a fretting hand touching a node
    // cannot execute — so import sheds it rather than producing an invalid chart. The
    // bend-mapping coverage lives in the plateau test, which strips the harmonic; the shed's
    // conversion note is asserted in the harmonic-shedding section.
    CHECK(bendCurve(chart.notes[4]).empty());

    // No two notes strike together in the fixture, so no chord furniture is derived.
    const common::core::ChartShapes fixture_spans = spansOf(chart, song->tempo_map);
    CHECK(fixture_spans.postures.empty());
    CHECK(fixture_spans.shapes.empty());

    // The generated fret-hand track opens on the fret-5 palm mute, then the seven-to-nine shift
    // glide drags the anchor up by its own +2 delta to a fret-7 window at the pitched keyframe
    // (rule 9), keeping the fretting finger on its slot. (The full track shape is the
    // phrase-aware generator's own concern, so this asserts the slide-driven move, not the
    // whole sequence.)
    CHECK(chart.fret_hand_positions.front().fret == 5);
    const common::core::FretHandPosition* const shift_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(shift_glide != nullptr);
    CHECK(shift_glide->fret == 7);
    CHECK(shift_glide->width == 4);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A legato slide is a continuation of the same note: the landing is not
// re-picked, so it folds into the origin as a pitched keyframe instead of keeping its own onset
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

    // The absolute-fret-9 landing at 1:2+1/2 is no longer an onset: four notes remain.
    REQUIRE(chart.notes.size() == 4);
    CHECK(chart.notes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(chart.notes[2].position == GridPosition{.measure = 1, .beat = 3});

    // The origin keeps its connection claim and carries the junction keyframe; its ring extends
    // through the landing's notated end, so it STORES the whole beat, and the drawn tail keeps
    // the margin before the next onset — floored above the keyframe, so the glide still reaches
    // fret 9.
    const common::core::ChartNote& origin = chart.notes[1];
    CHECK(origin.position == GridPosition{.measure = 1, .beat = 2});
    CHECK(origin.fret == 7);
    CHECK(origin.attack == common::core::NoteAttack::Legato);
    CHECK(origin.sustain == Fraction{1});
    CHECK(presentedNotesOf(chart, song->tempo_map)[1].sustain == Fraction{3, 4});
    REQUIRE(origin.keyframes.size() == 1);
    CHECK(origin.keyframes[0].offset == Fraction{1, 2});
    CHECK(origin.keyframes[0].fret == 9);
    CHECK_FALSE(origin.slide_out.has_value());

    // With no landing onset, hand movement at fret 9 comes from the pitched keyframe alone: the
    // glide drags the window up by its own +2 delta at the keyframe's mid-sustain position
    // (rule 9), landing a fret-7 window there.
    const common::core::FretHandPosition* const legato_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    REQUIRE(legato_glide != nullptr);
    CHECK(legato_glide->fret == 7);
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

    // Flags 4 is the downward slide-out; the absolute-fret-7 note now trails off unpitched
    // toward fret 3 instead of gliding into the fret-9 landing (which stays a real onset).
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
    CHECK(chart.notes[1].keyframes.empty());
    const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[1]);
    REQUIRE(slide_out != nullptr);
    CHECK(*slide_out == 3);
    // The stored gesture ends at the note's own half-beat ring. The trail-off is not protected
    // payload, so what is DRAWN compresses back with the tail to the minimum-sustain-distance
    // margin before the fret-9 onset — and that drawn end is what the hand exit below rides.
    CHECK(chart.notes[1].sustain == Fraction{1, 2});
    CHECK(chart.notes[1].sustain == Fraction{1, 2});
    const std::vector<common::core::ChartNote> presented = presentedNotesOf(chart, song->tempo_map);
    const common::core::ChartNote& second = presented[1];
    REQUIRE(second.slide_out.has_value());
    if (second.slide_out.has_value())
    {
        CHECK(second.sustain == Fraction{1, 4});
    }
    CHECK(second.sustain == Fraction{1, 4});

    // The natural walk is untouched by the gesture (no rule-9 drag), but the exit pass dips
    // the window with the trail-off — the fret-3 exit pulls the anchor down at the compressed
    // end, stopping at the capo-2 floor — and the real fret-9 landing placement (the minimal
    // fret-6 window) takes over at the next onset, standing in for the restore.
    CHECK(chart.fret_hand_positions.front().fret == 5);
    const common::core::FretHandPosition* const dip =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(dip != nullptr);
    CHECK(dip->fret == 3);
    const common::core::FretHandPosition* const landing =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
    REQUIRE(landing != nullptr);
    CHECK(landing->fret == 6);

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
    CHECK(landing.fret == 7);
    REQUIRE_FALSE(landing.keyframes.empty());
    CHECK(landing.keyframes.front().offset == Fraction{1, 8});
    CHECK(landing.keyframes.front().fret == 9);
    CHECK(landing.position == GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});

    // With no fabricated head in the gap, the half-beat trail-off DRAWS the plain margin before
    // the notated onset — the two gestures stay clear of each other — while the note itself
    // stores the ring it was notated with.
    const common::core::ChartNote& dip = chart.notes[1];
    const auto* const slide_out = common::core::slideOutFretOrNull(dip);
    REQUIRE(slide_out != nullptr);
    CHECK(dip.sustain == Fraction{1, 2});
    CHECK(dip.sustain == Fraction{1, 2});
    const std::vector<common::core::ChartNote> presented = presentedNotesOf(chart, song->tempo_map);
    const common::core::ChartNote& second = presented[1];
    REQUIRE(second.slide_out.has_value());
    if (second.slide_out.has_value())
    {
        CHECK(second.sustain == Fraction{1, 4});
    }
    CHECK(second.sustain == Fraction{1, 4});

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Guitar Pro's two tap articulations are different hands and must import differently:
// "Tapped" (two-hand tapping) becomes the chart's Tap attack, while "LeftHandTapped" — the fretting
// hand striking the note from nowhere — becomes the LeftTap attack verbatim, so it anchors the fret
// hand and closes chord spans like any fretted note. Importing it as a connection would have been
// the shipped aliasing bug: the score states a LOCAL articulation, and nothing about a neighbour.
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

    SECTION("a left-hand tap imports as the left-hand tap attack")
    {
        const auto song =
            import_with_property("<Property name=\"LeftHandTapped\"><Enable/></Property>");
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() >= 3);
        CHECK(chart.notes[2].fret == 9);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::LeftTap);
    }

    SECTION("a two-hand tap imports as a tap")
    {
        const auto song = import_with_property("<Property name=\"Tapped\"><Enable/></Property>");
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() >= 3);
        CHECK(chart.notes[2].fret == 9);
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
        CHECK(chart.notes[2].fret == 9);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::LeftTap);
    }

    std::filesystem::remove_all(scratch, cleanup_error);
}

// A pitched glide drags the hand by its own fret delta even when the target already fits the
// window (normalization policy rule 9): with the landing lowered, the one-fret glide's target
// sits inside the opening 5-8 window, yet the +1 delta still moves the window to 6-9 so the
// fretting finger keeps its slot.
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
    REQUIRE(chart.notes[1].keyframes.size() == 1);
    CHECK(chart.notes[1].keyframes[0].fret == 8);

    // Minimal-shift coverage alone would leave the window at 5-8 through the glide; the slide
    // delta moves it anyway, to a fret-6 window at the keyframe's mid-sustain position.
    CHECK(chart.fret_hand_positions.front().fret == 5);
    const common::core::FretHandPosition* const in_window_glide =
        fretHandPositionAt(chart, GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 4}});
    REQUIRE(in_window_glide != nullptr);
    CHECK(in_window_glide->fret == 6);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// One 4/4 bar, quarter notes: a two-string chord holds frets 2 and 5, and the lower fret-2 note
// shift-slides to a beat-2 landing while the fret-5 note keeps ringing. The held 5 is a planted
// finger that pins the top edge, so at the slide keyframe the hand window reshapes to the exact
// sounding hull instead of translating: a slide inward shrinks the window
// below the usual four-fret span, a slide outward grows it. At 120 BPM the fret-2 note's shift
// glide ends the 1/4-beat minimum-sustain margin before the beat-2 landing, so its keyframe sits
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

    // The slide keyframe sits a 1/4-beat margin before the beat-2 landing, at beat 1 + 3/4.
    const GridPosition keyframe{.measure = 1, .beat = 1, .offset = Fraction{3, 4}};

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

        // At the keyframe the held 5 pins the top and the sliding 2->3 carries the bottom, so the
        // window shrinks to the exact hull [3,5] rather than translating up to [3,6].
        const common::core::FretHandPosition* const reshape = fretHandPositionAt(chart, keyframe);
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
        const common::core::FretHandPosition* const reshape = fretHandPositionAt(chart, keyframe);
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

    // The tied 6: its continuation merged in (the ring runs through the chord) and the
    // continuation's shift-slide flags folded into the merged note (rule 15). A hold keyframe
    // pins fret 6 until the chord where the sliding segment was notated, then the glide keyframe
    // ARRIVES the minimum sustain distance before the landing onset (rule 13). The string itself
    // rings until that landing re-picks it, so two beats are stored — and the ring passes the
    // chord it crosses but ends exactly ON the landing pair, so rule 1 binds it there (user rule
    // 2026-08-28: the trim binds on the first onset a ring does not pass) and the presented tail
    // stops on its own synthesized arrival at 7/4.
    const common::core::ChartNote& tied = chart.notes[0];
    const std::vector<common::core::ChartNote> presented = presentedNotesOf(chart, song->tempo_map);
    CHECK(tied.position == GridPosition{.measure = 1, .beat = 1});
    CHECK(tied.string == 2);
    CHECK(tied.fret == 6);
    CHECK(tied.sustain == Fraction{2});
    CHECK(presented[0].sustain == Fraction{7, 4});
    REQUIRE(tied.keyframes.size() == 2);
    CHECK(tied.keyframes[0].offset == Fraction{1});
    CHECK(tied.keyframes[0].fret == 6);
    CHECK(tied.keyframes[1].offset == Fraction{7, 4});
    CHECK(tied.keyframes[1].fret == 2);
    CHECK_FALSE(tied.slide_out.has_value());

    // The chord's fret 8 shift-slides toward its fret-4 landing (no hold: its own onset
    // carried the flags); the glide keyframe ends the minimum sustain distance before the landing.
    const common::core::ChartNote& eight = chart.notes[2];
    CHECK(eight.position == GridPosition{.measure = 1, .beat = 2});
    CHECK(eight.string == 3);
    CHECK(eight.fret == 8);
    REQUIRE(eight.keyframes.size() == 1);
    CHECK(eight.keyframes[0].offset == Fraction{3, 4});
    CHECK(eight.keyframes[0].fret == 4);
    CHECK(eight.sustain == Fraction{1});
    CHECK(presented[2].sustain == Fraction{3, 4});

    // Both landings keep their own onsets (and heads) inside the beat-3 chord.
    CHECK(chart.notes[3].position == GridPosition{.measure = 1, .beat = 3});
    CHECK(chart.notes[3].string == 2);
    CHECK(chart.notes[3].fret == 2);
    CHECK(chart.notes[4].string == 3);
    CHECK(chart.notes[4].fret == 4);

    // Two arpeggio shapes tiling at the hand move: the departing grip and the one its travels
    // land in.
    //
    // THE DATING RULE (user ruling 2026-08-31) puts the first span's FRONT at beat one, not at the
    // beat-2 chord: the tied fret-6 ring the chord picks around began there and no preceding span
    // covers it, so the statement runs from the ring's own onset and the chord arrives inside it.
    // Its extent is unchanged in kind — the span COVERS ITS OWN GLIDE (rule 11b, [D2] amended
    // 2026-08-29) and ends at the LANDING, three quarters of a beat after the chord — and the
    // number moved only because the front did.
    //
    // The landed grip then BREATHES for the quarter beat before the beat-3 chord, so it opens a
    // carry-opened successor at the landing and that chord MERGES into it (rule 11's corollary 2:
    // a full restatement of the landed grip rides inside the successor, and the strike's own box
    // comes from the display law). Edge (b) is unchanged and untested here — it suppresses a
    // successor a restrike leaves NO room for, which is not this figure.
    const common::core::ChartShapes derived = spansOf(chart, song->tempo_map);
    REQUIRE(derived.shapes.size() == 2);
    CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes[0].sustain == Fraction{7, 4});
    REQUIRE(derived.shapes[0].posture < derived.postures.size());
    CHECK(
        heldFrets(derived.postures[derived.shapes[0].posture]) ==
        std::vector<std::optional<int>>{3, 6, 8});
    CHECK(shapeArrivalsOf(chart, song->tempo_map)[0]);
    // The successor tiles onto that landing exactly, with no gap and no overlap, and runs through
    // the chord that merged into it.
    CHECK(
        derived.shapes[1].position ==
        GridPosition{.measure = 1, .beat = 2, .offset = Fraction{3, 4}});
    CHECK(derived.shapes[1].sustain == Fraction{5, 4});
    CHECK(derived.shapes[1].carry_opened);
    REQUIRE(derived.shapes[1].posture < derived.postures.size());
    CHECK(
        heldFrets(derived.postures[derived.shapes[1].posture]) ==
        std::vector<std::optional<int>>{3, 2, 4});
    CHECK(shapeArrivalsOf(chart, song->tempo_map)[1]);

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

    // One deduplicated posture, in absolute frets (the fixture's capo is 2, and Guitar Pro's frets
    // are capo-relative): absolute 5 on the lowest string, absolute 7 on the second.
    const common::core::ChartShapes derived = spansOf(chart, song->tempo_map);
    REQUIRE(derived.postures.size() == 1);
    const common::core::ChartPosture& posture = derived.postures.front();
    REQUIRE(posture.frets.size() >= 2);
    CHECK(posture.frets[0] == std::optional{5});
    CHECK(posture.frets[1] == std::optional{7});

    // Both strums merge into one span from 1:1 toward the eighth strum's ring end at 1:2+1/2,
    // trimmed to the minimum sustain distance before the closing fret-7 onset there (rule 12a) —
    // even though presentation draws no tail on either strum.
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{5, 4});
    CHECK(derived.shapes.front().posture == 0);

    std::filesystem::remove_all(scratch, cleanup_error);
}

// The tie merge is the importer's canonical producer of a long ACTUAL ring, and it does not
// depend on the chain carrying a technique: strip the vibrato and the four beats are still stored
// whole. What that ring draws is the presentation rules' business — 15/4 here, the margin before
// the changed onset — and it survives the effect-free drop because four beats is a sustain.
TEST_CASE("Guitar Pro import merges a tie chain into one four-beat ring", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_sustain_keep_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // Removing the vibrato from the tie-chain origin makes the merged four-beat note
    // effect-free.
    const std::string gpif = fixtureWithReplacement("<Vibrato>Slight</Vibrato>", "");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);
    CHECK_FALSE(common::core::isShaking(chart.notes[3].vibrato));
    CHECK(chart.notes[3].sustain == Fraction{4});
    CHECK(presentedNotesOf(chart, song->tempo_map)[3].sustain == Fraction{15, 4});

    std::filesystem::remove_all(scratch, cleanup_error);
}

// The score's two dynamics marks land on one axis. Written against the real XML because the
// element is what the parser reads: Guitar Pro spells a ghost note as an `AntiAccent` SIBLING of
// `Accent` rather than another bit in the accent bitset, so a builder-level fixture would prove
// nothing about the parse. Every occurrence in the corpus carries the text "Normal", which is why
// presence alone is the claim.
TEST_CASE("Guitar Pro import maps accents and ghost notes onto emphasis", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_emphasis_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    SECTION("an AntiAccent note imports as a ghost")
    {
        const std::string gpif = fixtureWithReplacement(
            "<Note id=\"2\">", "<Note id=\"2\"><AntiAccent>Normal</AntiAccent>");
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() == 5);
        CHECK(chart.notes[2].emphasis == common::core::NoteEmphasis::Ghost);
        // Its neighbours stay normal: the mark belongs to the note that carries it.
        CHECK(chart.notes[1].emphasis == common::core::NoteEmphasis::Normal);
    }

    // The accent bitset: 1 = staccato, 4 = heavy accent, 8 = accent. Both loud bits import as an
    // accent (the ruling that a heavy accent folds into the one loud tier for now), while
    // staccato is not dynamics at all — it is duration, and counts as the halved ring the
    // last section below pins. Returns the eighth-note fixture note the bits were written on, so
    // one lambda can answer both axes.
    const auto note_with_accent_bits = [&](const std::string& flags) -> common::core::ChartNote {
        const std::string gpif = fixtureWithReplacement(
            "<Note id=\"2\">", "<Note id=\"2\"><Accent>" + flags + "</Accent>");
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() == 5);
        return chart.notes[2];
    };

    SECTION("the accent bit imports as an accent")
    {
        CHECK(note_with_accent_bits("8").emphasis == common::core::NoteEmphasis::Accent);
    }

    SECTION("the heavy-accent bit imports as an accent too")
    {
        CHECK(note_with_accent_bits("4").emphasis == common::core::NoteEmphasis::Accent);
    }

    SECTION("staccato alone is not an accent")
    {
        CHECK(note_with_accent_bits("1").emphasis == common::core::NoteEmphasis::Normal);
    }

    SECTION("staccato riding an accent still reads as an accent")
    {
        CHECK(note_with_accent_bits("9").emphasis == common::core::NoteEmphasis::Accent);
    }

    SECTION("only the staccato bit touches the ring")
    {
        // The two axes are independent in the one bitset: the loud bits say how hard the note is
        // struck and leave the notated eighth alone, while bit 1 says how long it sounds and
        // halves it. Both at once do both.
        CHECK(note_with_accent_bits("8").sustain == Fraction{1, 2});
        CHECK(note_with_accent_bits("4").sustain == Fraction{1, 2});
        CHECK(note_with_accent_bits("1").sustain == Fraction{1, 4});
        CHECK(note_with_accent_bits("9").sustain == Fraction{1, 4});
    }

    SECTION("a note claiming both loud and quiet resolves to the louder claim")
    {
        // Contradictory data rather than a state we model, and no real file exercises it, so
        // this pins the tie-break rather than describing material anyone has charted.
        const std::string gpif = fixtureWithReplacement(
            "<Note id=\"2\">", "<Note id=\"2\"><Accent>8</Accent><AntiAccent>Normal</AntiAccent>");
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        const common::core::Chart& chart = requiredChart(song->arrangements.front());
        REQUIRE(chart.notes.size() == 5);
        CHECK(chart.notes[2].emphasis == common::core::NoteEmphasis::Accent);
    }

    std::filesystem::remove_all(scratch, cleanup_error);
}

// Staccato is duration truth, so it imports as a halved ring and as nothing else — no field, no
// conversion note, the short ring IS the record. Written against the real XML because bit 1 of the
// `Accent` bitset is what the parser reads, which a builder-level fixture would not exercise. The
// fixture's first note is a notated quarter carrying the legato claim of the note after it, so one
// variant shows both what the mark shortens and what the shortening costs.
TEST_CASE("Guitar Pro import halves a staccato note's ring", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_staccato_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    const std::string gpif =
        fixtureWithReplacement("<Note id=\"0\">", "<Note id=\"0\"><Accent>1</Accent>");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);

    // The palm-muted quarter of the main fixture, which stores a full beat there, rings an eighth
    // here. Its emphasis stays normal: the bit it rode in on is not a loud tier.
    CHECK(chart.notes[0].fret == 5);
    CHECK(chart.notes[0].sustain == Fraction{1, 2});
    CHECK(chart.notes[0].emphasis == common::core::NoteEmphasis::Normal);

    // What the shortened ring costs, accepted by the ruling: the next note's legato claim was
    // justified only while that quarter rang to its onset a beat later, so the claim now settles
    // into the plain pick it plays as (the main fixture pins the same note as Legato unmarked).
    CHECK(chart.notes[1].attack == common::core::NoteAttack::Pick);

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
    // plateau to a single point. The harmonic is stripped from the bend note first: a fret-hand
    // harmonic sheds its bend on import, and this test is about the bend mapping.
    std::string gpif = fixtureWithReplacement(
        "<Property name=\"BendMiddleOffset2\"><Float>50.000000</Float></Property>",
        "<Property name=\"BendMiddleOffset2\"><Float>75.000000</Float></Property>");
    const std::string harmonic_marker =
        "<Property name=\"HarmonicType\"><HType>Natural</HType></Property>";
    const std::size_t harmonic_position = gpif.find(harmonic_marker);
    REQUIRE(harmonic_position != std::string::npos);
    gpif.replace(harmonic_position, harmonic_marker.size(), "");
    const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);

    GpSongImporter importer;
    const auto song = importer.importSong(archive, workspace);
    REQUIRE(song.has_value());
    REQUIRE(song->arrangements.size() == 1);
    const common::core::Chart& chart = requiredChart(song->arrangements.front());
    REQUIRE(chart.notes.size() == 5);
    REQUIRE(bendCurve(chart.notes[4]).size() == 4);
    CHECK(bendCurve(chart.notes[4])[1].offset == Fraction{1, 4});
    CHECK(bendCurve(chart.notes[4])[1].semitones == Catch::Approx(1.0));
    CHECK(bendCurve(chart.notes[4])[2].offset == Fraction{3, 8});
    CHECK(bendCurve(chart.notes[4])[2].semitones == Catch::Approx(1.0));
    CHECK(bendCurve(chart.notes[4])[3].offset == Fraction{1, 2});
    CHECK(bendCurve(chart.notes[4])[3].semitones == Catch::Approx(2.0));

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

// One single-note beat on the given zero-based string, shared by the grace, slide-in and let-ring
// tests below.
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

// The same beat carrying Guitar Pro's LET RING mark, which is the whole of what the file states.
[[nodiscard]] GpBeat letRingBeat(const Fraction duration, const int fret, const int string = 0)
{
    GpBeat beat = noteBeat(duration, fret, string);
    beat.notes.front().let_ring = true;
    return beat;
}

// A rest: Guitar Pro states one as a beat holding no notes at all.
[[nodiscard]] GpBeat restBeat(const Fraction duration)
{
    GpBeat beat;
    beat.duration_whole = duration;
    return beat;
}

// The FIRST chart note on a string, in the stream's own (position, string) order — so for a string
// carrying several it is the EARLIEST onset, and every caller below is asking about that one. The
// helper used to promise the fixtures held exactly one note per string; the span-clip figures
// state whole textures and hold several, so the contract is stated as what the search does rather
// than as a property of the fixtures.
[[nodiscard]] const common::core::ChartNote* noteOnChartString(
    const std::vector<common::core::ChartNote>& notes, const int string)
{
    const auto found = std::ranges::find(notes, string, &common::core::ChartNote::string);
    return found == notes.end() ? nullptr : &*found;
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

// What the fixture track's lowest string sounds open, which is what a trill's auxiliary pitch is
// named against below.
constexpr int g_low_string_midi{40};

// A single-note beat carrying a trill, for the spell-out tests below. Guitar Pro names the
// auxiliary by ABSOLUTE PITCH rather than by fret, so the fret a test means is written as the open
// string's pitch plus that fret — which is also what the derivation under test has to undo.
[[nodiscard]] GpBeat trillBeat(const Fraction duration, const int fret, const int auxiliary_fret)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.notes = {GpNote{
        .string = 0,
        .fret = fret,
        .harmonic_type = "",
        .trill_value = g_low_string_midi + auxiliary_fret,
    }};
    return beat;
}

// A ROLLED chord beat — Guitar Pro's `Arpeggio` mark, engraving's vertical wavy line — for the
// spell-out tests below. The frets are given lowest string first and the spread is in Guitar
// Pro's own MIDI ticks (480 to the quarter note), which is the number the stagger divides. No
// start time is stated, so the beat carries the field's on-the-beat default; the sections that
// exercise anticipation set the slider themselves.
[[nodiscard]] GpBeat rollBeat(
    const Fraction duration, const GpRollDirection direction, const int spread_ticks,
    const std::vector<int>& frets)
{
    GpBeat beat;
    beat.duration_whole = duration;
    beat.roll_direction = direction;
    beat.roll_spread_ticks = spread_ticks;
    for (std::size_t string = 0; string < frets.size(); ++string)
    {
        beat.notes.push_back(
            GpNote{
                .string = static_cast<int>(string),
                .fret = frets[string],
                .harmonic_type = "",
            });
    }
    return beat;
}

// True when a fabricated onset lands on the chart's position lattice — 1/3840 of a whole note,
// which in the 4/4 fixture's beat unit is 1/960 of a beat.
[[nodiscard]] bool landsOnGridQuantum(const Fraction offset_beats)
{
    return (offset_beats * Fraction{960}).denominator == 1;
}

// The global beat a chart position sits on in the 4/4 fixtures, so a figure that crosses a beat
// line can still state in one number where it opens and where its rings stop.
[[nodiscard]] Fraction globalBeatOf(const common::core::ChartNote& note)
{
    return Fraction{((note.position.measure - 1) * 4) + note.position.beat - 1} +
           note.position.offset;
}

// The one note struck at a beat's own position, which for a rolled chord is its first-sounded
// member. Silent holds sit at that position too and are not it.
[[nodiscard]] const common::core::ChartNote* firstStruckNote(
    const std::vector<common::core::ChartNote>& notes)
{
    for (const common::core::ChartNote& note : notes)
    {
        if (!common::core::silentHold(note.attack))
        {
            return &note;
        }
    }
    return nullptr;
}

} // namespace

// Guitar Pro states the two mutes as independent note properties, and so does the chart now: a
// dead string inside a palm-muted chord carries BOTH marks and must import with both flags. The
// single mute axis this replaced had to pick one, and it dropped the palm.
TEST_CASE("Guitar Pro import carries both mutes independently", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    // One strum: a palm-muted string, a dead one, a both-muted one, and a plain one.
    GpScore score = makeLinearScore(1, syncs);
    GpBeat beat;
    beat.duration_whole = Fraction{1, 4};
    beat.notes = {
        GpNote{.string = 0, .fret = 5, .palm_mute = true, .harmonic_type = ""},
        GpNote{.string = 1, .fret = 5, .full_mute = true, .harmonic_type = ""},
        GpNote{.string = 2, .fret = 5, .palm_mute = true, .full_mute = true, .harmonic_type = ""},
        GpNote{.string = 3, .fret = 5, .harmonic_type = ""},
    };
    score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 4);
    CHECK(chart.notes[0].palm_mute);
    CHECK_FALSE(chart.notes[0].dead);
    CHECK_FALSE(chart.notes[1].palm_mute);
    CHECK(chart.notes[1].dead);
    CHECK(chart.notes[2].palm_mute);
    CHECK(chart.notes[2].dead);
    CHECK_FALSE(chart.notes[3].palm_mute);
    CHECK_FALSE(chart.notes[3].dead);
}

// What the importer STORES under repeated and held chords: each strum's own notated ring, and the
// spans derived over them. The tail rules those rings draw through belong to core
// (test_chart_presentation.cpp); the presented values here pin that this chart reaches them.
TEST_CASE(
    "Guitar Pro import stores whole rings under repeated and held chords", "[core][gp-import]")
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
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].sustain == Fraction{2});
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        CHECK(presented[0].sustain == Fraction{7, 4});
        CHECK(presented[1].sustain == Fraction{7, 4});
        // One merged span from the first strum through the last strum's ring.
        const common::core::ChartShapes derived = spansOf(chart, built->tempo_map);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].sustain == Fraction{4});
    }

    SECTION("a changed chord rings the same way but splits the span")
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
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].sustain == Fraction{2});
        CHECK(spansOf(chart, built->tempo_map).shapes.size() == 2);
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
// differed (the old shape, where a separate field carried the harmonic) would now import the
// note as not a harmonic at all.
TEST_CASE("Guitar Pro import always gives a fret-hand harmonic its node", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    const auto import_note = [&syncs](const GpNote& note) {
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
        const common::core::ChartNote note = import_note(
            GpNote{.string = 1, .fret = 12, .harmonic_type = "Natural", .harmonic_fret = 12.0});
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(12.0));
        }
    }

    SECTION("a missing node falls back to the fret rather than leaving the note unharmonic")
    {
        const common::core::ChartNote note =
            import_note(GpNote{.string = 1, .fret = 7, .harmonic_type = "Natural"});
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            // Snapped to the 3rd partial's true node, not the "7" the score wrote.
            CHECK(*note.harmonic_node == Catch::Approx(7.0196).margin(0.001));
        }
    }

    SECTION("a pinch becomes the attack and carries the node Guitar Pro recorded")
    {
        // For a FRETTED harmonic Guitar Pro's HarmonicFret is a partial LABEL, not a position:
        // measured across 118 files, HarmonicFret - Fret is scattered and 18 of 56 pinches name a
        // position below their own fret, which no thumb can reach. Fret units are logarithmic, so
        // the real node is fret + the label's offset. 24.0 labels the 4th partial's third node,
        // so a pinch stopped at 5 grazes at 29 — past the neck, over the pickups, exactly where a
        // thumb is.
        const common::core::ChartNote note = import_note(
            GpNote{.string = 1, .fret = 5, .harmonic_type = "Pinch", .harmonic_fret = 24.0});
        CHECK(note.attack == common::core::NoteAttack::Pinch);
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(29.0));
            // Beyond the stop, which the chart rules require.
            CHECK(*note.harmonic_node > static_cast<double>(note.fret));
        }
        // Off the neck, so no 2D/3D anchor comes from it.
        CHECK_FALSE(common::core::nodeIsOnNeck(note.attack));
    }

    SECTION("a pinch Guitar Pro left without a fret defaults to the octave node")
    {
        // Unreached against real scores, but a pinch cannot be represented without its node. The
        // octave is the 2nd partial — the lowest-order harmonic available at any fret, so the
        // easiest to ring — beating both dropping the technique and reading the stop as a label.
        const common::core::ChartNote note =
            import_note(GpNote{.string = 1, .fret = 5, .harmonic_type = "Pinch"});
        CHECK(note.attack == common::core::NoteAttack::Pinch);
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(17.0));
        }
    }

    SECTION("a tapped harmonic keeps its stop and becomes the Tap attack")
    {
        // The natural path would erase the stop (fret := capo) and read the label against the
        // wrong string. A tap harmonic is a harmonic over a real stop, like a pinch — but its
        // damping finger lands ON the neck, so the node anchors displays.
        const common::core::ChartNote note = import_note(
            GpNote{.string = 1, .fret = 5, .harmonic_type = "Tap", .harmonic_fret = 12.0});
        CHECK(note.attack == common::core::NoteAttack::Tap);
        CHECK(note.fret == 5);
        REQUIRE(note.harmonic_node.has_value());
        if (note.harmonic_node.has_value())
        {
            CHECK(*note.harmonic_node == Catch::Approx(17.0));
        }
        CHECK(common::core::nodeIsOnNeck(note.attack));
    }

    SECTION("feedback harmonics drop the harmonic loudly, never corrupt the note")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{GpBeat{
                    .duration_whole = Fraction{1, 4},
                    .notes = {GpNote{
                        .string = 1, .fret = 5, .harmonic_type = "Feedback", .harmonic_fret = 12.0
                    }}
                }}}
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        // The note survives as an ordinary note — the stop is NOT overwritten with the capo the
        // way the natural path writes it — and the dropped harmonic is reported.
        CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(anyNoteContains(built->notes, "harmonics of unsupported types"));
    }

    SECTION("a semi-harmonic imports as a pinch, loudly")
    {
        // A semi-harmonic is a pinch whose fundamental keeps ringing — a pinch not fully
        // executed — and the format does not distinguish them yet, so the pinch is the honest
        // nearest technique and the approximation is reported.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{GpBeat{
                    .duration_whole = Fraction{1, 4},
                    .notes = {GpNote{
                        .string = 1, .fret = 5, .harmonic_type = "Semi", .harmonic_fret = 12.0
                    }}
                }}}
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const common::core::ChartNote& pinch = chart.notes[0];
        CHECK(pinch.attack == common::core::NoteAttack::Pinch);
        CHECK(pinch.fret == 5);
        REQUIRE(pinch.harmonic_node.has_value());
        if (pinch.harmonic_node.has_value())
        {
            CHECK(*pinch.harmonic_node == Catch::Approx(17.0));
        }
        CHECK(anyNoteContains(built->notes, "semi-harmonics were imported as pinch"));
    }

    SECTION("a natural label matching no real node drops the harmonic loudly")
    {
        // Integer frets 1, 11, 13, 14, 18, 20, 21, 23 sit 0.669+ from every true node; snapping
        // one anyway would move the touch a whole fret and sound a different partial.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{GpBeat{
                    .duration_whole = Fraction{1, 4},
                    .notes = {GpNote{.string = 1, .fret = 13, .harmonic_type = "Natural"}}
                }}}
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK_FALSE(chart.notes[0].harmonic_node.has_value());
        CHECK(chart.notes[0].fret == 13);
        CHECK(anyNoteContains(built->notes, "matched no real node"));
    }

    SECTION("a fret-hand harmonic sheds what it cannot execute, loudly")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpNote bent_harmonic{.string = 1, .fret = 7, .harmonic_type = "Natural"};
        bent_harmonic.vibrato = common::core::VibratoState::Narrow;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{GpBeat{.duration_whole = Fraction{1, 4}, .notes = {bent_harmonic}}}}
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        REQUIRE(chart.notes[0].harmonic_node.has_value());
        CHECK_FALSE(common::core::isShaking(chart.notes[0].vibrato));
        CHECK(anyNoteContains(built->notes, "fret-hand harmonic presses nothing"));
    }

    SECTION("capo-relative frets shift to absolute; the open string stays 0")
    {
        // Confirmed by authored experiment: with a capo at 3, Guitar Pro's "1" sounds the pitch
        // at absolute fret 4. The chart stores absolute frets with 0 meaning the open string.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].capo = 3;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {GpBeat{
                         .duration_whole = Fraction{1, 4},
                         .notes = {GpNote{.string = 1, .fret = 1, .harmonic_type = ""}}
                     },
                     GpBeat{
                         .duration_whole = Fraction{1, 4},
                         .notes = {GpNote{.string = 1, .fret = 0, .harmonic_type = ""}}
                     }}
                }
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].fret == 4);
        CHECK(chart.notes[1].fret == 0);
    }

    SECTION("an open-string pinch on a capo'd track speaks from the capo")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].capo = 2;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{GpBeat{
                    .duration_whole = Fraction{1, 4},
                    .notes = {GpNote{
                        .string = 1, .fret = 0, .harmonic_type = "Pinch", .harmonic_fret = 12.0
                    }}
                }}}
            });
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const common::core::ChartNote& octave = chart.notes[0];
        REQUIRE(octave.harmonic_node.has_value());
        if (octave.harmonic_node.has_value())
        {
            // The capo stops the open string, so the octave squeals from capo + 12, not the nut.
            CHECK(*octave.harmonic_node == Catch::Approx(14.0));
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
        beat.notes.front().emphasis = common::core::NoteEmphasis::Accent;
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
        // Only the first stroke carries the notated emphasis; the rest are plain picks.
        CHECK(chart.notes[0].emphasis == common::core::NoteEmphasis::Accent);
        CHECK(chart.notes[1].emphasis == common::core::NoteEmphasis::Normal);
        CHECK(chart.notes[2].emphasis == common::core::NoteEmphasis::Normal);
        CHECK(chart.notes[3].emphasis == common::core::NoteEmphasis::Normal);
        // Each stroke stores its own sixteenth of ring; drawn, the run presents as plain heads
        // like any hand-charted sixteenth run, the final stroke's sub-margin tail included.
        CHECK(chart.notes[0].sustain == Fraction{1, 4});
        CHECK(chart.notes[3].sustain == Fraction{1, 4});
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        CHECK(presented[0].sustain == Fraction{});
        CHECK(presented[3].sustain == Fraction{});
    }

    SECTION("a ghosted tremolo beat ghosts only its first stroke")
    {
        // The quiet end of the axis has to survive the spell-out the same way the loud end does:
        // repeating it would ghost a whole run the score marked once.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = tremoloBeat(Fraction{1, 4}, 5, Fraction{1, 16});
        beat.notes.front().emphasis = common::core::NoteEmphasis::Ghost;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].emphasis == common::core::NoteEmphasis::Ghost);
        CHECK(chart.notes[1].emphasis == common::core::NoteEmphasis::Normal);
        CHECK(chart.notes[3].emphasis == common::core::NoteEmphasis::Normal);
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
        CHECK(bendCurve(chart.notes[0]).empty());
        REQUIRE(bendCurve(chart.notes[1]).size() == 1);
        CHECK(bendCurve(chart.notes[1])[0].offset == Fraction{});
        CHECK(bendCurve(chart.notes[1])[0].semitones == Catch::Approx(0.5));
        REQUIRE(bendCurve(chart.notes[3]).size() == 1);
        CHECK(bendCurve(chart.notes[3])[0].semitones == Catch::Approx(1.5));
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

// A trill is the fretting hand's measured alternation, so import spells it out as the discrete
// hammer/pull run a player performs — the note-level sibling of the tremolo spell-out above. The
// score names only the auxiliary PITCH and no speed at all, so the fret is derived against the
// string's open pitch and the rate is the format-forced sixteenth. The first note keeps the
// source's attack and marks; every continuation claims legato carrying only the mutes, and the
// resolver reads the direction off the frets. A trill with nothing to alternate, or naming an
// auxiliary no hand can take, keeps its single note and is counted.
TEST_CASE("Guitar Pro import spells out trills", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a quarter-note trill becomes four alternating sixteenths")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{trillBeat(Fraction{1, 4}, 5, 7)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // The run leaves the note's own stop for the auxiliary and comes back, one step at a time.
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[2].fret == 5);
        CHECK(chart.notes[3].fret == 7);
        for (std::size_t index = 0; index < chart.notes.size(); ++index)
        {
            CHECK(chart.notes[index].position.offset == Fraction{static_cast<int>(index), 4});
            CHECK(chart.notes[index].sustain == Fraction{1, 4});
        }
        // One stroke starts the run; everything after it is the fretting hand alone.
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(chart.notes[1].attack == common::core::NoteAttack::Legato);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Legato);
        CHECK(chart.notes[3].attack == common::core::NoteAttack::Legato);
        CHECK(anyNoteContains(built->notes, "trills were spelled out"));
    }

    SECTION("the alternation resolves as hammer-ons and pull-offs")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{trillBeat(Fraction{1, 4}, 5, 7)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // No direction is imported, and none is needed: the run rises to the auxiliary and falls
        // back, so the resolver answers hammer, pull, hammer from the stored frets alone. A run
        // whose rings did not reach the next onset would resolve to nothing and be swept flat.
        const std::vector<common::core::LegatoMotion> motions =
            common::core::chartConnections(chart.notes, built->tempo_map).legato;
        REQUIRE(motions.size() == 4);
        CHECK(motions[0] == common::core::LegatoMotion::Unjustified);
        CHECK(motions[1] == common::core::LegatoMotion::Hammer);
        CHECK(motions[2] == common::core::LegatoMotion::Pull);
        CHECK(motions[3] == common::core::LegatoMotion::Hammer);
    }

    SECTION("the open string is a reachable auxiliary")
    {
        // Trilling a stopped note against the open string is ordinary playing — the run pulls off
        // to the open string and hammers back onto the stop — so the refusal floor is the capo'd
        // open, not the first stopped fret.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{trillBeat(Fraction{1, 4}, 5, 0)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[1].fret == 0);
        CHECK(chart.notes[2].fret == 5);
        CHECK(chart.notes[3].fret == 0);
        const std::vector<common::core::LegatoMotion> motions =
            common::core::chartConnections(chart.notes, built->tempo_map).legato;
        REQUIRE(motions.size() == 4);
        CHECK(motions[0] == common::core::LegatoMotion::Unjustified);
        CHECK(motions[1] == common::core::LegatoMotion::Pull);
        CHECK(motions[2] == common::core::LegatoMotion::Hammer);
        CHECK(motions[3] == common::core::LegatoMotion::Pull);
    }

    SECTION("an indivisible ring hands its remainder to the last note")
    {
        // A quarter-note triplet rings two thirds of a beat: one whole sixteenth step, and the
        // rest absorbed by the note that closes the run, so the source's ring survives exactly.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{trillBeat(Fraction{1, 6}, 5, 7)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].sustain == Fraction{1, 4});
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.offset == Fraction{1, 4});
        CHECK(chart.notes[1].sustain == Fraction{5, 12});
        CHECK(chart.notes[0].sustain + chart.notes[1].sustain == Fraction{2, 3});
    }

    SECTION("a ring no longer than one step keeps its single note")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {{trillBeat(Fraction{1, 16}, 5, 7)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(anyNoteContains(built->notes, "trills rang no longer than one sixteenth"));
    }

    SECTION("an auxiliary no hand can take keeps its single note")
    {
        // A pitch below the string's open reach is no position at all, the note's own fret is no
        // alternation, and fret 30 is off the board — each is data the run must refuse rather
        // than spell.
        const int auxiliary_fret = GENERATE(-3, 5, 30);
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{trillBeat(Fraction{1, 4}, 5, auxiliary_fret)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(anyNoteContains(built->notes, "trills named an auxiliary the note cannot alternate"));
    }

    SECTION("the mutes carry through the alternation")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = trillBeat(Fraction{1, 4}, 5, 7);
        beat.notes.front().palm_mute = true;
        beat.notes.front().full_mute = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        for (const common::core::ChartNote& note : chart.notes)
        {
            // Both mutes are what the hands are still doing, so they hold for the whole run.
            CHECK(note.palm_mute);
            CHECK(note.dead);
        }
    }

    SECTION("the onset's own marks stay on the note that was struck")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = trillBeat(Fraction{1, 4}, 5, 7);
        beat.notes.front().emphasis = common::core::NoteEmphasis::Accent;
        beat.notes.front().vibrato = common::core::VibratoState::Narrow;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // Repeating either onto the alternation would state marks the score made once: an
        // accented run, and a shake on stops the hand only passes through.
        CHECK(chart.notes[0].emphasis == common::core::NoteEmphasis::Accent);
        CHECK(chart.notes[1].emphasis == common::core::NoteEmphasis::Normal);
        CHECK(chart.notes[3].emphasis == common::core::NoteEmphasis::Normal);
        CHECK(common::core::isShaking(chart.notes[0].vibrato));
        CHECK_FALSE(common::core::isShaking(chart.notes[1].vibrato));
        CHECK_FALSE(common::core::isShaking(chart.notes[3].vibrato));
    }
}

// How the halved ring composes with everything that reads a ring. The mark is per NOTE, so a
// chord's one marked member shortens alone; the halving happens as the events are collected, so
// the trill spell-out that runs after collection alternates through the ring the note actually
// has; and a legato claim whose whole justification was the predecessor ringing to its onset
// settles into a pick when the halved ring no longer reaches. Built straight from a score because
// the parse of the mark is pinned above and these are the composition rules.
TEST_CASE("Guitar Pro import halves staccato rings per note", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("one marked chord member shortens while the rest hold")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = chordBeat(Fraction{1, 4}, 5, 7, false, false);
        beat.notes.front().staccato = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        // Uneven rings under one strum: the mark belongs to the string it was written on, and the
        // beat's stated duration still stands for the string that was not marked.
        CHECK(chart.notes[0].string == 2);
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
        CHECK(chart.notes[1].string == 3);
        CHECK(chart.notes[1].sustain == Fraction{1});
    }

    SECTION("an unmarked chord rings exactly what the beat states")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{chordBeat(Fraction{1, 4}, 5, 7, false, false)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == Fraction{1});
        CHECK(chart.notes[1].sustain == Fraction{1});
    }

    SECTION("a staccato trill alternates through the halved ring")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat = trillBeat(Fraction{1, 4}, 5, 7);
        beat.notes.front().staccato = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Half the ring is half the alternation: the same quarter spells four sixteenths unmarked
        // (the trill test above), and the run's total ring is still exactly what the note has.
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].position.offset == Fraction{});
        CHECK(chart.notes[0].sustain == Fraction{1, 4});
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.offset == Fraction{1, 4});
        CHECK(chart.notes[1].sustain == Fraction{1, 4});
        CHECK(anyNoteContains(built->notes, "trills were spelled out"));
    }

    SECTION("a legato claim the halved ring no longer reaches reads as a pick")
    {
        // Generated against the unmarked case so the degrade is pinned to the mark and not to the
        // shape of the pair: the same two beats keep their claim when the first rings its quarter.
        const bool staccato = GENERATE(false, true);
        GpScore score = makeLinearScore(1, syncs);
        GpBeat first;
        first.duration_whole = Fraction{1, 4};
        first.notes = {GpNote{.string = 0, .fret = 5, .staccato = staccato, .harmonic_type = ""}};
        GpBeat second;
        second.duration_whole = Fraction{1, 4};
        second.notes = {
            GpNote{.string = 0, .fret = 7, .hopo_destination = true, .harmonic_type = ""}
        };
        score.tracks[0].bars.push_back(GpBar{.voices = {{first, second}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].sustain == (staccato ? Fraction{1, 2} : Fraction{1}));
        // The source contradicting itself, resolved toward what sounds: the normalizer records the
        // flattened claim through its own counted path rather than the import inventing a notice.
        CHECK(
            chart.notes[1].attack ==
            (staccato ? common::core::NoteAttack::Pick : common::core::NoteAttack::Legato));
        const std::string unjustified{common::core::chartRepairText(
            common::core::ChartRepair::UnjustifiedLegato)};
        CHECK(anyNoteContains(built->notes, unjustified) == staccato);
    }
}

// LET RING is the other half of the duration pair the staccato test above is one of: staccato
// halves what the beat states, let ring lengthens it to what the string actually sounds. Playback
// rings such a note until the FIRST of the next same-string SOUNDING onset, the next rest in its
// own voice, and one full measure-duration from its own onset. The import walks the last two and
// leaves the first to the clamp that already bounds every ring, so no bound is stated twice — and
// where a merge hid the beat that would have stopped it, the merged ring answers instead.
TEST_CASE("Guitar Pro import rings a let-ring note on to what sounds", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // One quarter note in the 4/4 fixtures, which is one signature beat of stored ring.
    constexpr Fraction quarter{1, 4};
    constexpr Fraction eighth{1, 8};

    SECTION("the next sounding onset on its string stops it, through the clamp")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 3),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        // The walk answers with the bar cap; the string's own re-strike two beats later is what
        // actually ends the ring, and the clamp is the one place that bound is ever stated.
        CHECK(marked->sustain == Fraction{2});
    }

    SECTION("the voice's next rest stops it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     restBeat(quarter),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{2});
    }

    SECTION("one measure-duration caps a ring nothing else stops")
    {
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{4});
    }

    SECTION("the cap crosses the barline and lands mid-beat")
    {
        // The mark sits half a beat into the bar, so its own measure-duration runs out half a beat
        // into the NEXT bar — the sliding cap the rule states, not a truncation at the barline.
        GpScore score = makeLinearScore(2, syncs);
        std::vector<GpBeat> first{noteBeat(eighth, 7, 5), letRingBeat(eighth, 5)};
        std::vector<GpBeat> second;
        for (int step = 0; step < 6; ++step)
        {
            first.push_back(noteBeat(eighth, 7, 5));
        }
        for (int step = 0; step < 8; ++step)
        {
            second.push_back(noteBeat(eighth, 7, 5));
        }
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(first)}});
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(second)}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->position.measure == 1);
        CHECK(marked->position.beat == 1);
        CHECK(marked->position.offset == Fraction{1, 2});
        CHECK(marked->sustain == Fraction{4});
    }

    SECTION("the cap is the ORIGIN bar's length under a meter change")
    {
        // A 4/4 bar into two 6/8 bars: the cap is a whole note, which is FIVE of the beats the
        // ring crosses (three quarters and two eighths) rather than the four the origin bar
        // counts. Reading the cap as a beat count instead of a length would stop it a beat early.
        GpScore score = makeLinearScore(3, syncs);
        score.master_bars[1] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.master_bars[2] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 7, 5),
                     letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });
        for (int bar = 0; bar < 2; ++bar)
        {
            std::vector<GpBeat> eighths;
            for (int step = 0; step < 6; ++step)
            {
                eighths.push_back(noteBeat(eighth, 7, 5));
            }
            score.tracks[0].bars.push_back(GpBar{.voices = {std::move(eighths)}});
        }

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->position.measure == 1);
        CHECK(marked->position.beat == 2);
        CHECK(marked->sustain == Fraction{5});
    }

    SECTION("another voice's rest does not stop it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)},
                    {noteBeat(quarter, 9, 4),
                     restBeat(quarter),
                     noteBeat(quarter, 9, 4),
                     noteBeat(quarter, 9, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        // The rest is the transcriber's statement about the voice that holds it, so the ring runs
        // on to its own cap. The melody over it only ever restates its own fret — or speaks after
        // its previous sound has ended — so no statement contradicts a sounding grip and the
        // grip-contradiction cut never fires.
        CHECK(marked->sustain == Fraction{4});
    }

    SECTION("the note's own voice's rest does stop it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     restBeat(quarter),
                     noteBeat(quarter, 7, 5)},
                    {noteBeat(quarter, 9, 4),
                     noteBeat(quarter, 9, 4),
                     noteBeat(quarter, 9, 4),
                     noteBeat(quarter, 9, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{2});
    }

    SECTION("dead, palm-muted and staccato notes are never extended")
    {
        // Guitar Pro's playback returns on each of these before it ever reads the let-ring mark,
        // so all three keep exactly the ring their own mark gives them — the staccato member's
        // being the halved one, which the extension must not undo.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat chord;
        chord.duration_whole = quarter;
        chord.notes = {
            GpNote{
                .string = 0, .fret = 5, .full_mute = true, .let_ring = true, .harmonic_type = ""
            },
            GpNote{
                .string = 1, .fret = 5, .palm_mute = true, .let_ring = true, .harmonic_type = ""
            },
            GpNote{.string = 2, .fret = 5, .let_ring = true, .staccato = true, .harmonic_type = ""},
            GpNote{.string = 3, .fret = 5, .let_ring = true, .harmonic_type = ""}
        };
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord,
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 7);
        CHECK(chart.notes[0].sustain == Fraction{1});
        CHECK(chart.notes[1].sustain == Fraction{1});
        CHECK(chart.notes[2].sustain == Fraction{1, 2});
        CHECK(chart.notes[3].sustain == Fraction{4});
        CHECK(anyNoteContains(built->notes, "1 let-ring rings were normalized to their region"));
    }

    SECTION("a note that absorbed a TIE keeps a merged ring LONGER than the region cap")
    {
        // Rule 1 of the baseline law: ties combine into a single note at its true written
        // duration, and Rule B then lengthens ONLY — so a merged ring already reaching past the
        // region cap simply stands. Here the chain runs a whole bar past the cap, which is what
        // makes the two answers visibly different; the tie-merged note the cap CAN still reach is
        // the cut test case's t6 figure, where it extends like any other mark.
        GpScore score = makeLinearScore(2, syncs);
        GpBeat origin = letRingBeat(quarter, 5);
        origin.notes.front().tie_origin = true;
        GpBeat continuation = noteBeat(Fraction{1}, 5);
        continuation.notes.front().tie_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {origin,
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{continuation}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{8});
    }

    SECTION("a note that absorbed a LEGATO SLIDE extends like any other marked note")
    {
        // The legato-slide landing merges into the origin as a keyframe, and the merged note then
        // extends to the region cap exactly as a tie-merged one does (the cut test case's t6
        // figure): the merge states the WRITTEN duration, never a cap on the mark. The exemption
        // that held this chain at its two merged beats is deleted — its "the walk would collapse
        // to the merged end anyway" justification measured false corpus-wide (674 of 697 exempt
        // rings had region ends past their merged end).
        GpScore score = makeLinearScore(1, syncs);
        // Slide flag 2 is the legato slide: the landing continues the same sounding note.
        GpBeat origin = letRingBeat(quarter, 5);
        origin.notes.front().slide_flags = 2;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {origin, noteBeat(quarter, 7), noteBeat(quarter, 9, 5), noteBeat(quarter, 9, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // The landing merged away, so the string carries one note — two merged beats extended to
        // the region cap at four. The fillers restate their own fret and cut nothing.
        REQUIRE(chart.notes.size() == 3);
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{4});
        CHECK(anyNoteContains(built->notes, "1 let-ring rings were normalized to their region"));
        CHECK_FALSE(anyNoteContains(built->notes, "kept their shipped rings"));
    }

    SECTION("an unmarked note rings exactly what its beat states")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{1});
        CHECK_FALSE(anyNoteContains(built->notes, "let-ring"));
    }

    SECTION("the conversion log counts every ring it extended")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat chord;
        chord.duration_whole = quarter;
        chord.notes = {
            GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
            GpNote{.string = 1, .fret = 5, .let_ring = true, .harmonic_type = ""}
        };
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord,
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        CHECK(anyNoteContains(built->notes, "2 let-ring rings were normalized to their region"));
    }

    SECTION("a ring the clamp takes straight back is not reported as a conversion")
    {
        // The walk reads ONE voice, so it cannot see the other voice re-striking the marked
        // string a beat later; the chart's own same-string clamp does, and it puts the ring back
        // exactly where it stood. Nothing about the stored chart changed, so nothing is reported
        // — a conversion note the reader cannot find in the chart is worse than no note.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)},
                    {restBeat(quarter), noteBeat(quarter, 3), restBeat(quarter), restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{1});
        CHECK_FALSE(anyNoteContains(built->notes, "let-ring"));
    }

    SECTION("a following grace steals from the ring, but the bend keeps its destination")
    {
        // Rule 17's steal takes the ornament's lead out of this beat, the let-ring pass then
        // out-rings it, and the grip-contradiction cut takes the extension back: the grace's own
        // string is restruck at ANOTHER fret on the very beat it ornaments, and that restrike is
        // a cut event, so the marked ring caps at its written beat. What must not happen anywhere
        // in that order is the truth loss this pins: the bend was once clipped to the STOLEN ring
        // the instant it was mapped, so the destination — which sits exactly at the ring's final
        // end here — was gone by the time the ring came back.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat marked_beat = letRingBeat(quarter, 5);
        marked_beat.notes.front().bend = GpBend{
            .origin_value = 0.0,
            .middle_value = 50.0,
            .destination_value = 100.0,
            .origin_offset = 0.0,
            .middle_offset1 = 50.0,
            .middle_offset2 = 50.0,
            .destination_offset = 100.0,
        };
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {marked_beat,
                     graceBeat(GpGracePlacement::BeforeBeat, 9, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{1});
        const std::vector<BendReading> curve = bendCurve(*marked);
        REQUIRE(curve.size() == 3);
        CHECK(curve[2].offset == Fraction{1});
        CHECK(curve[2].semitones == Catch::Approx(2.0));
    }

    SECTION("an UNMARKED note's bend still clips to the ring the grace left it")
    {
        // The same fixture without the mark, which is what keeps the trim honest: nothing
        // lengthens this ring afterwards, so the destination the curve wrote past the stolen end
        // is a part of the bend that never sounds and it goes, exactly as it always has.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat bent = noteBeat(quarter, 5);
        bent.notes.front().bend = GpBend{
            .origin_value = 0.0,
            .middle_value = 50.0,
            .destination_value = 100.0,
            .origin_offset = 0.0,
            .middle_offset1 = 50.0,
            .middle_offset2 = 50.0,
            .destination_offset = 100.0,
        };
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {bent,
                     graceBeat(GpGracePlacement::BeforeBeat, 9, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const bent_note =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(bent_note != nullptr);
        // A thirty-second of the beat went to the ornament.
        CHECK(bent_note->sustain == Fraction{7, 8});
        const std::vector<BendReading> curve = bendCurve(*bent_note);
        REQUIRE(curve.size() == 2);
        CHECK(curve[1].offset == Fraction{1, 2});
        CHECK(curve[1].semitones == Catch::Approx(1.0));
    }
}

// Guitar Pro's beat-level roll mark — engraving's vertical wavy line, which the file spells
// `Arpeggio` — says one grip is sounded member by member, and the import writes exactly the sound:
// each member struck at its turn over the stored spread, every one of them ringing to the end the
// beat gave it. THE ROLL IS AN ACCUMULATION FIGURE PLAYED FAST (user ruling 2026-08-31, Q7), so
// nothing further is needed to READ it: the members' rings overlap and the ordinary opening law
// turns them into one arpeggio span with no rule of its own, which THE RE-FORMED GATE below is the
// proof of. D11's fronted-claims machinery — a silent hold authored at the front for every member
// still to come — is DELETED with this ruling, and the import now authors no claims at all.
TEST_CASE("Guitar Pro import spreads rolled chords over a held grip", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // A half-note beat is two beats of 4/4, and 240 Guitar Pro ticks is an eighth note, so the
    // stagger across two gaps is a quarter of a beat — comfortably inside every member's ring.
    constexpr int eighth_ticks{240};

    SECTION("the members speak in turn and every ring still stops with the beat")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(
                    Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5, 7, 7})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Three soundings and NOTHING ELSE. The two silent holds this section used to assert
        // were D11's fronted-claims machinery, deleted with the ruling (2026-08-31): the members'
        // own rings state the grip, so a claim beside them would restate what sound already says.
        REQUIRE(chart.notes.size() == 3);
        CHECK(std::ranges::none_of(chart.notes, [](const common::core::ChartNote& note) {
            return common::core::silentHold(note.attack);
        }));
        CHECK(chart.notes[0].string == 1);
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(chart.notes[0].position.offset == Fraction{});
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].string == 2);
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.offset == Fraction{1, 4});
        CHECK(chart.notes[1].sustain == Fraction{7, 4});
        CHECK(chart.notes[2].string == 3);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].position.offset == Fraction{1, 2});
        CHECK(chart.notes[2].sustain == Fraction{3, 2});
        // The whole point of subtracting the wait from the ring rather than moving the ring: the
        // hand releases the grip as one, so every member stops where the beat stated.
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK(note.position.offset + note.sustain == Fraction{2});
        }
        CHECK(anyNoteContains(built->notes, "rolled chords were spread"));
    }

    SECTION("the direction decides which string speaks first")
    {
        const bool highest_first = GENERATE(false, true);
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(
                    Fraction{1, 2},
                    highest_first ? GpRollDirection::HighestFirst : GpRollDirection::LowestFirst,
                    eighth_ticks,
                    {5, 6, 7})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        const common::core::ChartNote* const first = firstStruckNote(chart.notes);
        REQUIRE(first != nullptr);
        if (first != nullptr)
        {
            // Guitar Pro's "Down" is a DOWNSTROKE: the pick starts at the lowest-pitched string
            // and sweeps up, which is the reading the file's own word invites getting backwards.
            CHECK(first->string == (highest_first ? 3 : 1));
            CHECK(first->fret == (highest_first ? 7 : 5));
            CHECK(first->position.offset == Fraction{});
        }
        // The far end of the sweep speaks a whole spread later.
        CHECK(chart.notes.back().string == (highest_first ? 1 : 3));
        CHECK(chart.notes.back().position.offset == Fraction{1, 2});
    }

    SECTION("THE RE-FORMED Q7 GATE: the derived span answers all four verdicts")
    {
        // THE GATE D11's machinery had to pass before it could be deleted (re-ruled 2026-08-31,
        // W-D). The original form demanded byte equality with the claims-produced span and FAILED
        // on extent, and the failure was the finding: the claims-produced span ran only as far as
        // the roll GESTURE because that is all the claims stated — the scaffolding's justification
        // figure wearing a ruling's clothes. Under [D3] the hold is the RING, so the extent change
        // is deliberate and the gate re-formed around what the ruling actually promises.
        //
        // Four verdicts, and every one of them is asserted below:
        //   (1) COVERAGE — every roll note lies inside a derived span;
        //   (2) CLASS    — that span is a bracket, not a box;
        //   (3) MEMBERSHIP — every roll stop is a member of its posture;
        //   (4) FRONTING — where the roll fronts its own figure, the span fronts with it.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(
                    Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5, 7, 7})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartResolutions resolutions =
            common::core::chartResolutions(chart.notes, built->tempo_map);
        // The figure the ruling asked for, read back by rules that know nothing about rolls: the
        // members' rings overlap into one shape, so the ordinary opening law brackets the whole
        // chord as ONE posture with no claim anywhere in it.
        REQUIRE(resolutions.shapes.size() == 1);
        const common::core::ChartShape& span = resolutions.shapes.front();

        // (3) MEMBERSHIP: every stop the roll sounds is in the posture, and nothing else is.
        REQUIRE(span.posture < resolutions.postures.size());
        CHECK(
            heldFrets(resolutions.postures[span.posture]) ==
            std::vector<std::optional<int>>{5, 7, 7});

        // (4) FRONTING: the span fronts where the figure does. THE DATING RULE puts its front at
        // the earliest member onset no preceding span covers, which for a roll is the first
        // string to speak — so the bracket opens with the gesture rather than at whichever
        // arrival reached the threshold.
        CHECK(span.position == GridPosition{.measure = 1, .beat = 1});
        CHECK(span.bracket_position == std::optional<GridPosition>{span.position});
        CHECK(span.founding == common::core::SpanFounding::Accumulation);

        // (1) COVERAGE: every roll note lies inside the span, the last arrival included. This is
        // where the DELIBERATE extent change shows — the bracket runs the RING (two beats), not
        // the stagger (half a beat), because under [D3] the hold IS the ring.
        CHECK(span.sustain == Fraction{2});
        const Fraction span_start =
            common::core::beatDistance(built->tempo_map, GridPosition{}, span.position);
        for (const common::core::ChartNote& note : chart.notes)
        {
            const Fraction onset =
                common::core::beatDistance(built->tempo_map, GridPosition{}, note.position);
            CAPTURE(note.string);
            CHECK(!(onset < span_start));
            CHECK(onset < span_start + span.sustain);
        }

        // (2) CLASS: a bracket, because the members sound SEPARATELY — which for a roll is the
        // whole of what the mark says. No claim is involved in reaching that verdict, and none
        // exists to reach it with.
        const std::vector<bool> arrivals = common::core::chartShapeArrivals(
            resolutions.presented_notes, resolutions.shapes, built->tempo_map);
        REQUIRE(arrivals.size() == 1);
        CHECK(arrivals.front());
        CHECK_FALSE(span.silent_member);
        CHECK(span.sounds_in_parts);
        CHECK(
            std::ranges::none_of(
                resolutions.claim_shapes,
                [](const std::optional<std::size_t>& reach) { return reach.has_value(); }));
    }

    SECTION("a stagger the beat cannot hold leaves the chord simultaneous")
    {
        // No spread at all, one that rounds to nothing across the gaps, and one as long as the
        // beat itself — the last would leave the final member no ring, which is no roll.
        const int spread_ticks = GENERATE(0, 1, 960);
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(
                    Fraction{1, 2}, GpRollDirection::LowestFirst, spread_ticks, {5, 7, 7})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK(note.attack == common::core::NoteAttack::Pick);
            CHECK(note.position.offset == Fraction{});
            CHECK(note.sustain == Fraction{2});
        }
        CHECK(anyNoteContains(built->notes, "roll marks had no stagger"));
    }

    SECTION("a lone note wearing the mark has nothing to roll")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(
                    Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(anyNoteContains(built->notes, "roll marks had no stagger"));
    }

    SECTION("a staccato member speaks late and still stops halfway")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat =
            rollBeat(Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5, 7, 7});
        beat.notes[1].staccato = true;
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // The mark halves the ring as the beat is collected, so the stagger eats the front of the
        // ring the mark already shortened rather than of the one the beat notated.
        CHECK(chart.notes[1].string == 2);
        CHECK(chart.notes[1].position.offset == Fraction{1, 4});
        CHECK(chart.notes[1].sustain == Fraction{3, 4});
        CHECK(chart.notes[1].position.offset + chart.notes[1].sustain == Fraction{1});
        // Its unmarked neighbours still release with the beat.
        CHECK(chart.notes[0].position.offset + chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[2].position.offset + chart.notes[2].sustain == Fraction{2});
    }

    SECTION("an indivisible spread still lands every onset on the grid")
    {
        // 239 ticks across two gaps is 119.5 apiece; the stagger takes whole ticks, and one
        // Guitar Pro tick is two of the chart's own position quanta, so nothing can fall between
        // two lattice lines.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{rollBeat(Fraction{1, 2}, GpRollDirection::LowestFirst, 239, {5, 7, 7})}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[1].position.offset == Fraction{119, 480});
        CHECK(chart.notes[2].position.offset == Fraction{119, 240});
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK(landsOnGridQuantum(note.position.offset));
        }
    }

    SECTION("a tremolo-picked beat drops the roll rather than re-taking the grip per stroke")
    {
        // The two marks contradict each other: the roll states one grip sounded member by member,
        // and a stroke carrying it would state that grip re-taken on every repetition.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat beat =
            rollBeat(Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5, 7, 7});
        beat.tremolo_stroke = Fraction{1, 8};
        score.tracks[0].bars.push_back(GpBar{.voices = {{beat}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Four strokes of three struck notes, and not one silent hold among them.
        REQUIRE(chart.notes.size() == 12);
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK_FALSE(common::core::silentHold(note.attack));
        }
        CHECK(anyNoteContains(built->notes, "dropped their roll mark"));
    }

    SECTION("a following grace steals only from the members already sounding")
    {
        // 460 ticks across two gaps staggers the last member past the point a thirty-second-note
        // lead reaches back to, which is the one composition where a beat's events are not all
        // ringing when the next beat's ornament begins. The ornament sounds in the preceding
        // note's time, and a member that has not spoken by then has no time to give it.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat grace;
        grace.duration_whole = Fraction{1, 32};
        grace.grace = GpGracePlacement::BeforeBeat;
        grace.notes = {GpNote{.string = 3, .fret = 9, .harmonic_type = ""}};
        GpBeat principal;
        principal.duration_whole = Fraction{1, 4};
        principal.notes = {GpNote{.string = 3, .fret = 10, .harmonic_type = ""}};
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {rollBeat(Fraction{1, 4}, GpRollDirection::LowestFirst, 460, {5, 7, 7}),
                     grace,
                     principal}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 5);
        // The two members already sounding stop where the ornament starts, a thirty-second before
        // the principal.
        CHECK(globalBeatOf(chart.notes[0]) + chart.notes[0].sustain == Fraction{7, 8});
        CHECK(globalBeatOf(chart.notes[1]) + chart.notes[1].sustain == Fraction{7, 8});
        // The one that had not: it keeps the ring the roll timed for it, where taking a lead out
        // of a ring that has not started would have handed it a negative one.
        CHECK(chart.notes[3].string == 3);
        CHECK(globalBeatOf(chart.notes[3]) == Fraction{23, 24});
        CHECK(chart.notes[3].sustain == Fraction{1, 24});
    }
}

// Guitar Pro's SECOND roll slider, "Start time", says where the figure sits against its beat: at 1
// the first member is struck on it, at 0 the roll ANTICIPATES and its LAST member lands on it. The
// import honours the reading between them, which moves the whole figure — every member together,
// since the span opens where the hand takes the grip — while every ring still stops where the beat
// stated, so an early member simply rings longer.
TEST_CASE("Guitar Pro import honours a rolled chord's stated anticipation", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // A half-note beat is two beats of 4/4, and 240 Guitar Pro ticks is an eighth note, so the
    // stagger across two gaps is a quarter of a beat and the whole written span is half a beat.
    constexpr int eighth_ticks{240};

    // The rolled beat on the bar's second half. A figure that opens early needs room in front of
    // its beat, and every section below places whatever it wants in that room.
    const auto late_roll = [] {
        return rollBeat(Fraction{1, 2}, GpRollDirection::LowestFirst, eighth_ticks, {5, 7, 7});
    };
    const auto rest_beat = [](const Fraction duration) {
        GpBeat beat;
        beat.duration_whole = duration;
        return beat;
    };
    // One note on the roll's own lowest string, which is the string its first member speaks on.
    const auto low_string_beat = [](const Fraction duration, const int fret) {
        GpBeat beat;
        beat.duration_whole = duration;
        beat.notes = {GpNote{.string = 0, .fret = fret, .harmonic_type = ""}};
        return beat;
    };
    const auto score_with = [&syncs](const std::vector<GpBeat>& voice) {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(GpBar{.voices = {voice}});
        return score;
    };

    SECTION("full anticipation opens a spread early and lands the last member on the beat")
    {
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.0;

        const auto built = buildGpSong(score_with({rest_beat(Fraction{1, 2}), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // The grip is taken half a beat — the whole written span — before the beat the figure
        // belongs to.
        CHECK(chart.notes[0].string == 1);
        CHECK(chart.notes[0].attack == common::core::NoteAttack::Pick);
        CHECK(globalBeatOf(chart.notes[0]) == Fraction{3, 2});
        CHECK(chart.notes[0].sustain == Fraction{5, 2});
        CHECK(globalBeatOf(chart.notes[1]) == Fraction{7, 4});
        // What the slider's zero end means, stated as the grid position a reader would see: the
        // far side of the sweep arrives exactly on the beat.
        CHECK(
            chart.notes[2].position == GridPosition{.measure = 1, .beat = 3, .offset = Fraction{}});
        // Every ring still stops where the beat stated, so the early members ring longer rather
        // than the figure sliding whole.
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK(globalBeatOf(note) + note.sustain == Fraction{4});
        }
        CHECK(anyNoteContains(built->notes, "rolled chords were spread"));
        CHECK_FALSE(anyNoteContains(built->notes, "had no room before their beat"));
    }

    SECTION("the anticipated records still derive as one arpeggio span, fronting with the figure")
    {
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.0;

        const auto built = buildGpSong(score_with({rest_beat(Fraction{1, 2}), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartResolutions resolutions =
            common::core::chartResolutions(chart.notes, built->tempo_map);
        // THE GATE'S FOURTH VERDICT, on the one corpus figure that actually fronts (two of the
        // three corpus rolls anticipate): moving the whole figure moves the span with it, because
        // THE DATING RULE puts the front at the earliest member onset — which is the first string
        // to speak, wherever the slider put it. Nothing here involves a claim; the rings are the
        // whole statement.
        REQUIRE(resolutions.shapes.size() == 1);
        REQUIRE(resolutions.shapes.front().posture < resolutions.postures.size());
        CHECK(
            heldFrets(resolutions.postures[resolutions.shapes.front().posture]) ==
            std::vector<std::optional<int>>{5, 7, 7});
        CHECK(
            resolutions.shapes.front().position ==
            GridPosition{.measure = 1, .beat = 2, .offset = Fraction{1, 2}});
        CHECK_FALSE(resolutions.shapes.front().silent_member);
        CHECK(
            std::ranges::none_of(
                resolutions.claim_shapes,
                [](const std::optional<std::size_t>& reach) { return reach.has_value(); }));
    }

    SECTION("a partial slider value shifts by the rounded fraction of the written span")
    {
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.89;

        const auto built = buildGpSong(score_with({rest_beat(Fraction{1, 2}), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // 11% of the 240-tick span is 26.4 ticks. The chart's lattice is twice as fine as the
        // tick, so the figure opens 53 quanta early — 26.5 ticks, the nearest line, and not a
        // whole tick at all.
        CHECK(globalBeatOf(chart.notes[0]) == Fraction{1867, 960});
        CHECK(globalBeatOf(chart.notes[1]) == Fraction{2107, 960});
        CHECK(globalBeatOf(chart.notes[2]) == Fraction{2347, 960});
        for (const common::core::ChartNote& note : chart.notes)
        {
            CHECK(landsOnGridQuantum(note.position.offset));
            CHECK(globalBeatOf(note) + note.sustain == Fraction{4});
        }
    }

    SECTION("a roll that states the on-beat start, or states nothing, is placed on its beat")
    {
        const bool states_start_time = GENERATE(false, true);
        GpBeat roll = late_roll();
        if (states_start_time)
        {
            roll.roll_start_time = 1.0;
        }

        const auto built = buildGpSong(score_with({rest_beat(Fraction{1, 2}), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // Saying nothing is the ordinary roll, not full anticipation, so both readings land the
        // figure exactly where every roll landed before the slider was honoured.
        CHECK(globalBeatOf(chart.notes[0]) == Fraction{2});
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(globalBeatOf(chart.notes[1]) == Fraction{9, 4});
        CHECK(chart.notes[1].sustain == Fraction{7, 4});
        CHECK(globalBeatOf(chart.notes[2]) == Fraction{5, 2});
        CHECK(chart.notes[2].sustain == Fraction{3, 2});
        CHECK_FALSE(anyNoteContains(built->notes, "had no room before their beat"));
    }

    SECTION("a staccato member rings from its early onset into the halved end")
    {
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.0;
        roll.notes[1].staccato = true;

        const auto built = buildGpSong(score_with({rest_beat(Fraction{1, 2}), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // The mark halves the ring as the beat is collected, so the halved end is measured from
        // the BEAT and the anticipated onset rings into it: a quarter-beat late off a figure that
        // opened half a beat early, stopping where half the beat's stated duration runs out.
        CHECK(chart.notes[1].string == 2);
        CHECK(globalBeatOf(chart.notes[1]) == Fraction{7, 4});
        CHECK(chart.notes[1].sustain == Fraction{5, 4});
        CHECK(globalBeatOf(chart.notes[1]) + chart.notes[1].sustain == Fraction{3});
        // Its unmarked neighbours still release with the beat.
        CHECK(globalBeatOf(chart.notes[0]) + chart.notes[0].sustain == Fraction{4});
        CHECK(globalBeatOf(chart.notes[2]) + chart.notes[2].sustain == Fraction{4});
    }

    SECTION("the previous ring on the first member's string ends at the early onset")
    {
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.0;

        const auto built = buildGpSong(
            score_with({rest_beat(Fraction{1, 4}), low_string_beat(Fraction{1, 4}, 3), roll}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // The figure opens half a beat early on a string that is still ringing, and the ordinary
        // same-string clamp is what yields to it — a re-strike stops the ring, wherever it lands.
        CHECK(chart.notes[0].string == 1);
        CHECK(globalBeatOf(chart.notes[0]) == Fraction{1});
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
        CHECK(chart.notes[1].string == 1);
        CHECK(globalBeatOf(chart.notes[1]) == Fraction{3, 2});
        CHECK_FALSE(anyNoteContains(built->notes, "had no room before their beat"));
    }

    SECTION("an anticipation with nowhere to open starts on the beat and is counted")
    {
        // The two ways the room can be missing: the song itself begins where the figure would
        // have opened, and an earlier sounding already holds that slot on the first member's own
        // string — which the same-string clamp has no bound for, two notes at one (position,
        // string) being a collision rather than an overlap.
        const bool blocked_by_a_sounding = GENERATE(false, true);
        GpBeat roll = late_roll();
        roll.roll_start_time = 0.0;
        std::vector<GpBeat> voice{roll};
        if (blocked_by_a_sounding)
        {
            // An eighth note on the first member's own string, landing exactly where a whole
            // spread of anticipation would have opened the figure.
            voice = {rest_beat(Fraction{3, 8}), low_string_beat(Fraction{1, 8}, 3), roll};
        }

        const auto built = buildGpSong(score_with(voice));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == (blocked_by_a_sounding ? 4 : 3));
        // Clamped back to the placement every roll had before the slider was honoured, and said
        // out loud: the figure is written, the anticipation is the part that could not be.
        const std::size_t opening = blocked_by_a_sounding ? 1 : 0;
        CHECK(chart.notes[opening].string == 1);
        CHECK(globalBeatOf(chart.notes[opening]) == Fraction{blocked_by_a_sounding ? 2 : 0});
        CHECK(chart.notes[opening].sustain == Fraction{2});
        CHECK(anyNoteContains(built->notes, "had no room before their beat"));
        CHECK(anyNoteContains(built->notes, "rolled chords were spread"));
    }
}

// The parse side of that same figure, which a score-built test cannot reach: the mark is an
// element while its two settings ride EXTENDED properties keyed by opaque numeric ids, and Guitar
// Pro keeps the strum dialog's identical pair on the same beat. So what is pinned here is that the
// roll's own id is the one read, and that the file's direction word lands on the right end of the
// sweep — the two readings a wrong guess would leave silently plausible.
TEST_CASE("Guitar Pro parsing reads the roll mark and its own spread", "[core][gp-import]")
{
    // The roll's spread on the fixture's first beat: a sixteenth note, 120 ticks at Guitar Pro's
    // 480 to the quarter. The start time rides beside it and each section states its own.
    const std::string roll_properties = "<XProperty id=\"687931393\"><Int>120</Int></XProperty>\n";
    // A start time that is neither endpoint, so a reading that defaulted or ignored the property
    // cannot pass for one that read it.
    const std::string quarter_start_time =
        "<XProperty id=\"687931394\"><Float>0.25</Float></XProperty>\n";
    const auto rolled_fixture = [&roll_properties](const std::string& extra_properties) {
        return fixtureWithReplacement(
            "<Beat id=\"0\"><Rhythm ref=\"0\"/><Notes>0</Notes></Beat>",
            "<Beat id=\"0\"><Rhythm ref=\"0\"/><Arpeggio>Up</Arpeggio><Notes>0</Notes>\n"
            "<XProperties>\n" +
                roll_properties + extra_properties + "</XProperties></Beat>");
    };
    // The first beat of the first track's first bar, which every section below reads.
    const auto rolledBeat = [](const GpScore& score) -> const GpBeat& {
        REQUIRE(score.tracks.size() == 1);
        REQUIRE(!score.tracks.front().bars.empty());
        REQUIRE(!score.tracks.front().bars.front().voices.empty());
        REQUIRE(!score.tracks.front().bars.front().voices.front().empty());
        return score.tracks.front().bars.front().voices.front().front();
    };

    SECTION("the mark, its spread and its start time all arrive")
    {
        const auto score = parseGpScore(rolled_fixture(quarter_start_time));
        REQUIRE(score.has_value());
        if (score.has_value())
        {
            const GpBeat& beat = rolledBeat(*score);
            // "Up" is an UPSTROKE, so the highest-pitched member is the one that speaks first.
            CHECK(beat.roll_direction == GpRollDirection::HighestFirst);
            CHECK(beat.roll_spread_ticks == 120);
            CHECK(beat.roll_start_time == Catch::Approx(0.25));
        }
    }

    SECTION("a roll stating no start time starts on its beat")
    {
        const auto score = parseGpScore(rolled_fixture(""));
        REQUIRE(score.has_value());
        if (score.has_value())
        {
            const GpBeat& beat = rolledBeat(*score);
            // Saying nothing is the ordinary on-the-beat roll. A zero default would read every
            // such beat as FULLY anticipated, which is the opposite of what the score states.
            CHECK(beat.roll_spread_ticks == 120);
            CHECK(beat.roll_start_time == Catch::Approx(1.0));
        }
    }

    SECTION("the strum's own duration property is not read as the roll's")
    {
        // Guitar Pro keeps both dialogs' settings on a beat whichever mark is active, at
        // independent values, so reading the strum's id here would import one mark's setting as
        // another's — which is exactly what the reference reimplementation does.
        const auto score = parseGpScore(rolled_fixture(
            quarter_start_time + "<XProperty id=\"687935489\"><Int>480</Int></XProperty>\n" +
            "<XProperty id=\"687935490\"><Float>0</Float></XProperty>\n"));
        REQUIRE(score.has_value());
        if (score.has_value())
        {
            const GpBeat& beat = rolledBeat(*score);
            CHECK(beat.roll_spread_ticks == 120);
            CHECK(beat.roll_start_time == Catch::Approx(0.25));
        }
    }

    SECTION("an unmarked beat carries no roll at all")
    {
        const auto score = parseGpScore(std::string{g_fixture_gpif});
        REQUIRE(score.has_value());
        if (score.has_value())
        {
            const GpBeat& beat = rolledBeat(*score);
            CHECK(beat.roll_direction == GpRollDirection::None);
            CHECK(beat.roll_spread_ticks == 0);
        }
    }
}

// The parse side of voice identity, which a score-built test cannot reach: gpif spells a bar's
// voices as SLOTS and writes -1 for one the bar does not use, while a voice's continuation across
// bar lines is asked of the SLOT (the reference chains a beat to `bar.nextBar.voices[this.index]`).
// Compacting the absences away renumbered every voice after a gap, which spliced two different
// voices into one chain — and the let-ring walk reads exactly that chain for its rest and
// same-string stops. Nothing downstream can tell a spliced chain from a real one, so it is pinned
// here.
TEST_CASE("Guitar Pro parsing keeps a bar's voice slots", "[core][gp-import]")
{
    SECTION("an absent slot a real voice follows is kept as an empty voice")
    {
        const auto score = parseGpScore(
            fixtureWithReplacement("<Voices>0 -1 -1 -1</Voices>", "<Voices>-1 0 -1 -1</Voices>"));
        REQUIRE(score.has_value());
        REQUIRE(score->tracks.size() == 1);
        REQUIRE(score->tracks.front().bars.size() == 2);
        const GpBar& bar = score->tracks.front().bars.front();
        REQUIRE(bar.voices.size() == 2);
        CHECK(bar.voices[0].empty());
        CHECK(bar.voices[1].size() == 4);
    }

    SECTION("trailing absences stay absent, because a chain runs off the end either way")
    {
        const auto score = parseGpScore(std::string{g_fixture_gpif});
        REQUIRE(score.has_value());
        REQUIRE(score->tracks.size() == 1);
        REQUIRE(score->tracks.front().bars.size() == 2);
        CHECK(score->tracks.front().bars[0].voices.size() == 1);
        CHECK(score->tracks.front().bars[1].voices.size() == 1);
    }
}

// The parse side of the width axis, which a score-built test cannot reach: Guitar Pro states the
// tier as the `Vibrato` element's TEXT, and its word for the ordinary shake is its own house label
// rather than either of the chart's. Pinned here so a reading that took presence alone — which is
// what this importer did before the axis existed — cannot silently import every wide vibrato as
// the ordinary one.
TEST_CASE("Guitar Pro parsing reads both vibrato tiers", "[core][gp-import]")
{
    const auto shaken_note = [](const std::string& gpif) {
        const auto score = parseGpScore(gpif);
        REQUIRE(score.has_value());
        REQUIRE(score->tracks.size() == 1);
        // The fixture's tie origin, the one note carrying the mark.
        const GpBeat& beat = score->tracks.front().bars.front().voices.front()[3];
        REQUIRE(beat.notes.size() == 1);
        return beat.notes.front().vibrato;
    };

    CHECK(shaken_note(std::string{g_fixture_gpif}) == common::core::VibratoState::Narrow);
    CHECK(
        shaken_note(
            fixtureWithReplacement("<Vibrato>Slight</Vibrato>", "<Vibrato>Wide</Vibrato>")) ==
        common::core::VibratoState::Wide);
    // Absence is the only thing that means no shake at all: a present element the reading does not
    // recognise is still a shake, at the ordinary tier, rather than a dropped mark.
    CHECK(
        shaken_note(fixtureWithReplacement("<Vibrato>Slight</Vibrato>", "<Vibrato/>")) ==
        common::core::VibratoState::Narrow);
    CHECK(
        shaken_note(fixtureWithReplacement("<Vibrato>Slight</Vibrato>", "")) ==
        common::core::VibratoState::Off);
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

    // The folded curve: the origin's own unbent onset in front, then every continuation point
    // offset by the one-beat onset gap, semitones intact, offsets strictly ascending.
    REQUIRE(bendCurve(merged).size() == 4);
    CHECK(bendCurve(merged)[0].offset == Fraction{});
    CHECK(bendCurve(merged)[0].semitones == Catch::Approx(0.0));
    CHECK(bendCurve(merged)[1].offset == Fraction{1});
    CHECK(bendCurve(merged)[1].semitones == Catch::Approx(0.0));
    CHECK(bendCurve(merged)[2].offset == Fraction{3, 2});
    CHECK(bendCurve(merged)[2].semitones == Catch::Approx(1.0));
    CHECK(bendCurve(merged)[3].offset == Fraction{2});
    CHECK(bendCurve(merged)[3].semitones == Catch::Approx(2.0));
    CHECK(bendCurve(merged)[1].offset < bendCurve(merged)[2].offset);
    CHECK(bendCurve(merged)[2].offset < bendCurve(merged)[3].offset);
}

namespace
{

// How the second segment of the vibrato fixtures below joins the first: Guitar Pro's two ways of
// continuing one ringing string, and the two places the importer folds a segment's flags into a
// note that already exists.
enum class SegmentJoin : std::uint8_t
{
    // A tie: the continuation is the same stop, sounding on.
    Tie,

    // A legato slide (Guitar Pro's Flags 2): the continuation is a new stop the glide arrives at.
    LegatoSlide
};

// One string, one merged ring, two Guitar Pro segments — a quarter at fret 5 joined to a quarter
// that the merge folds away — each carrying its own vibrato flag. Both joins produce a single
// two-beat note whose second segment begins one beat in, which is where the anchorless flag has to
// land.
[[nodiscard]] GpScore mergedVibratoScore(
    const SegmentJoin join, const common::core::VibratoState first_vibrato,
    const common::core::VibratoState second_vibrato)
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    const bool tied = join == SegmentJoin::Tie;
    const GpNote first{
        .string = 0,
        .fret = 5,
        .tie_origin = tied,
        .tie_destination = false,
        .vibrato = first_vibrato,
        .slide_flags = tied ? 0 : 2,
        .harmonic_type = ""
    };
    const GpNote second{
        .string = 0,
        // A tie continues the same stop; a legato slide glides to a new one.
        .fret = tied ? 5 : 7,
        .tie_origin = false,
        .tie_destination = tied,
        .vibrato = second_vibrato,
        .harmonic_type = ""
    };
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {first}},
                 GpBeat{.duration_whole = Fraction{1, 4}, .notes = {second}}}
            }
        });
    return score;
}

// The vibrato statements a merged note carries along its ring, offsets included — the channel read
// the way its consumers read it, so a statement written on the wrong keyframe (or on a second
// keyframe beside the right one) fails rather than hiding behind a matching count.
[[nodiscard]] std::vector<std::pair<Fraction, common::core::VibratoState>> vibratoStatements(
    const common::core::ChartNote& note)
{
    std::vector<std::pair<Fraction, common::core::VibratoState>> statements;
    for (const common::core::Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<common::core::VibratoState>& vibrato = keyframe.vibrato;
        if (vibrato.has_value())
        {
            statements.emplace_back(keyframe.offset, *vibrato);
        }
    }
    return statements;
}

} // namespace

// Guitar Pro states vibrato per NOTE and names no instant inside it, so a segment folded into a
// ring that already exists — a tie continuation, or a legato slide's landing — used to OR its flag
// onto the whole merged note. That smear lied in both directions: a landing's shake ran backward
// over the origin's onset, and a landing without one inherited a shake it never played. The
// keyframe model gives the flag a place to land, and the import anchors it where the folded segment
// BEGINS: the junction the glide arrives at (the carried sign-off's last keyframe) or the
// continuation's own onset. A ring that shakes end to end still stores nothing but its onset flag,
// which is what every chart written before the model says.
TEST_CASE("Guitar Pro import anchors a folded segment's vibrato", "[core][gp-import]")
{
    const auto merged_note = [](const GpScore& score) {
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Both joins fold the second segment away, which is what makes the flag anchorless.
        REQUIRE(chart.notes.size() == 1);
        return chart.notes.front();
    };

    SECTION("a legato landing's shake starts at the junction it arrives at")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::LegatoSlide,
            common::core::VibratoState::Off,
            common::core::VibratoState::Narrow));
        // The onset does not shake: the glide's origin was never marked.
        CHECK_FALSE(common::core::isShaking(note.vibrato));
        CHECK(note.sustain == Fraction{2});
        // ONE keyframe carries both facts — the fret the glide reaches and the shake that starts
        // on arrival are one moment, which is the coupling the model exists for.
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK(note.keyframes[0].fret == 7);
        CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Narrow);
    }

    SECTION("a plain landing ends the origin's shake at the junction")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::LegatoSlide,
            common::core::VibratoState::Narrow,
            common::core::VibratoState::Off));
        CHECK(common::core::isShaking(note.vibrato));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].fret == 7);
        CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Off);
    }

    SECTION("a glide that shakes throughout states its shake once")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::LegatoSlide,
            common::core::VibratoState::Narrow,
            common::core::VibratoState::Narrow));
        // Byte-identical to what the whole-note flag stored: the state never changes, so the
        // channel says nothing after its opening statement.
        CHECK(common::core::isShaking(note.vibrato));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].fret == 7);
        CHECK_FALSE(note.keyframes[0].vibrato.has_value());
    }

    SECTION("a tie continuation's shake starts where the continuation does")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::Tie, common::core::VibratoState::Off, common::core::VibratoState::Narrow));
        CHECK_FALSE(common::core::isShaking(note.vibrato));
        CHECK(note.sustain == Fraction{2});
        // A tie states no new position, so the keyframe carrying the shake states no fret: the
        // fret-less keyframe the model made legal is exactly what a state change without a hand
        // move needs.
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK_FALSE(note.keyframes[0].fret.has_value());
        CHECK_FALSE(note.keyframes[0].bend.has_value());
        CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Narrow);
    }

    SECTION("a plain tie continuation ends the shake mid-ring")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::Tie, common::core::VibratoState::Narrow, common::core::VibratoState::Off));
        CHECK(common::core::isShaking(note.vibrato));
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Off);
    }

    SECTION("a tie chain that shakes throughout stores only its onset flag")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::Tie,
            common::core::VibratoState::Narrow,
            common::core::VibratoState::Narrow));
        CHECK(common::core::isShaking(note.vibrato));
        CHECK(note.keyframes.empty());
    }

    SECTION("a landing that widens the shake states the wider tier at the junction")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::LegatoSlide,
            common::core::VibratoState::Narrow,
            common::core::VibratoState::Wide));
        // A step BETWEEN the two widths is a statement exactly as a start or a stop is: the state
        // in force changed, so the channel says so, and everything downstream reads a narrow
        // region followed by a wide one with no rule of its own.
        CHECK(note.vibrato == common::core::VibratoState::Narrow);
        REQUIRE(note.keyframes.size() == 1);
        CHECK(note.keyframes[0].offset == Fraction{1});
        CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Wide);
        // And it holds until something restates it: nothing does, so the wide shake runs to the
        // ring's end without a second statement.
        CHECK(vibratoStatements(note).size() == 1);
        CHECK(
            common::core::ringStateAt(note, Fraction{2}).vibrato ==
            common::core::VibratoState::Wide);
    }

    SECTION("a tie chain that never shakes stores nothing at all")
    {
        const common::core::ChartNote note = merged_note(mergedVibratoScore(
            SegmentJoin::Tie, common::core::VibratoState::Off, common::core::VibratoState::Off));
        CHECK_FALSE(common::core::isShaking(note.vibrato));
        CHECK(note.keyframes.empty());
    }
}

// The state holds from each statement until the next, so it carries THROUGH travel: a middle
// segment that shakes and then glides on is the corpus's rare-but-real "vibrato during a slide",
// and it needs no rule of its own — the shake simply has not been restated yet when the second
// glide leaves. The chain also proves the anchor is the folded segment's own start rather than
// whatever keyframe happens to be last: a later junction would be wrong for the middle segment's
// flag, and the onset would be wrong for both.
TEST_CASE("Guitar Pro import shakes through a slide it has not left yet", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // Three quarters on one string: 5 glides to 7, which shakes and glides on to 9, which does not.
    const GpNote first{.string = 0, .fret = 5, .slide_flags = 2, .harmonic_type = ""};
    const GpNote middle{
        .string = 0,
        .fret = 7,
        .vibrato = common::core::VibratoState::Narrow,
        .slide_flags = 2,
        .harmonic_type = ""
    };
    const GpNote last{.string = 0, .fret = 9, .harmonic_type = ""};
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {first}},
                 GpBeat{.duration_whole = Fraction{1, 4}, .notes = {middle}},
                 GpBeat{.duration_whole = Fraction{1, 4}, .notes = {last}}}
            }
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& note = chart.notes.front();

    CHECK_FALSE(common::core::isShaking(note.vibrato));
    CHECK(note.sustain == Fraction{3});
    // Two junctions, each carrying its own segment's state: the shake starts on the first arrival
    // and ends on the second, which leaves it true across the whole 7-to-9 travel between them.
    const std::vector<std::pair<Fraction, common::core::VibratoState>> statements =
        vibratoStatements(note);
    REQUIRE(statements.size() == 2);
    CHECK(statements[0] == std::pair{Fraction{1}, common::core::VibratoState::Narrow});
    CHECK(statements[1] == std::pair{Fraction{2}, common::core::VibratoState::Off});
    REQUIRE(note.keyframes.size() == 2);
    CHECK(note.keyframes[0].fret == 7);
    CHECK(note.keyframes[1].fret == 9);
}

// Two voices can hold the same string at the same instant — a sustained lower voice under a fresh
// upper one — and the tie merge is keyed by STRING alone, so such a continuation folds into a note
// that begins at the very same beat. Its statements have nowhere later to land: offset zero is the
// onset, and a keyframe there is the one shape validation refuses outright, which for an import
// costs the WHOLE song rather than the one junk pairing. The channel's opening statement takes it
// instead, exactly as a bend point at zero becomes the note's own onset bend.
TEST_CASE("Guitar Pro import folds a same-instant tie into the onset", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // One fret-5 quarter per voice on string 0, at the same beat: the lower voice opens the tie
    // and the upper voice's shaking note claims to continue it.
    const GpNote opens{
        .string = 0, .fret = 5, .tie_origin = true, .tie_destination = false, .harmonic_type = ""
    };
    const GpNote continues{
        .string = 0,
        .fret = 5,
        .tie_origin = false,
        .tie_destination = true,
        .vibrato = common::core::VibratoState::Narrow,
        .harmonic_type = ""
    };
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {opens}}},
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {continues}}}
            }
        });

    // The song survives: import is a commit point, and no pairing of junk voices may refuse it.
    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& note = chart.notes.front();

    CHECK(common::core::isShaking(note.vibrato));
    CHECK(note.keyframes.empty());
}

// Two folds can claim the SAME instant on one ring: a tie continuation carrying a legato slide
// hands the glide its junction, and a second voice's note sits on that string exactly there. Both
// segments state the channel at one offset, and the ring can hold only one state from that
// instant. The later fold restates it, which is what the shared keyframe's FRET already does — a
// ring reading the landing's position with the continuation's shake would describe neither note.
TEST_CASE(
    "Guitar Pro import lets the later fold restate one instant's vibrato", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // Voice one ties a shaking, legato-sliding continuation onto its fret 5; voice two strikes
    // fret 7 on the same string at the instant that continuation begins, and the glide lands on
    // it. The landing does not shake.
    const GpNote opens{
        .string = 0, .fret = 5, .tie_origin = true, .tie_destination = false, .harmonic_type = ""
    };
    const GpNote continues{
        .string = 0,
        .fret = 5,
        .tie_origin = false,
        .tie_destination = true,
        .vibrato = common::core::VibratoState::Narrow,
        .slide_flags = 2,
        .harmonic_type = ""
    };
    const GpNote lands{.string = 0, .fret = 7, .harmonic_type = ""};
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {opens}},
                 GpBeat{.duration_whole = Fraction{1, 4}, .notes = {continues}}},
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {}},
                 GpBeat{.duration_whole = Fraction{1, 4}, .notes = {lands}}}
            }
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 1);
    const common::core::ChartNote& note = chart.notes.front();

    CHECK_FALSE(common::core::isShaking(note.vibrato));
    // ONE keyframe at the junction: the landing's fret and the landing's state, not a mixture.
    REQUIRE(note.keyframes.size() == 1);
    CHECK(note.keyframes[0].offset == Fraction{1});
    CHECK(note.keyframes[0].fret == 7);
    CHECK(note.keyframes[0].vibrato == common::core::VibratoState::Off);
}

// The anchor moves only for a flag the import has to RE-HOME. A note that merges nothing states
// its own flag at its own onset, and a shift slide leaves the landing a re-picked note of its own,
// so neither the glide's arrival keyframe nor the landing's head takes a statement it was not
// given: the shake stays exactly where the score wrote it, as it imported before the model.
TEST_CASE("Guitar Pro import leaves an unmerged note's vibrato at its onset", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // Flags 1 is the shift slide: the landing is re-picked, so it keeps its own onset and head.
    const GpNote sliding{
        .string = 0,
        .fret = 5,
        .vibrato = common::core::VibratoState::Narrow,
        .slide_flags = 1,
        .harmonic_type = ""
    };
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {sliding}},
                 GpBeat{
                     .duration_whole = Fraction{1, 4},
                     .notes = {GpNote{.string = 0, .fret = 9, .harmonic_type = ""}}
                 }}
            }
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 2);

    CHECK(common::core::isShaking(chart.notes[0].vibrato));
    CHECK(vibratoStatements(chart.notes[0]).empty());
    CHECK_FALSE(common::core::isShaking(chart.notes[1].vibrato));
    CHECK(vibratoStatements(chart.notes[1]).empty());
}

namespace
{

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

// A downward trail-off from a low fret: the four-fret exit is held onto the playable board at the
// first fret above the capo, never the nut. Before 2026-08-20 the importer floored this exit at 0
// while the rules demanded above-the-capo, so a whole track's import failed on one trail-off from
// frets 1-4 — a rule tightened centrally on a false belief about what the producer did. The
// importer now asks the one floor, and the chart validates without any normalizer repair.
TEST_CASE(
    "Guitar Pro import floors a trail-off exit at the first playable fret", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("at no capo the floor is fret 1")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Flag 4 is the downward trail-off; a half note leaves room for the exit.
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 2}, 3, 0, 4)}}});
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const common::core::ChartNote& only = chart.notes[0];
        REQUIRE(only.slide_out.has_value());
        if (only.slide_out.has_value())
        {
            CHECK(*only.slide_out == common::core::firstPlayableFret(0));
        }
        // The floor was authored, not repaired: nothing for the normalizer to report.
        CHECK_FALSE(anyNoteContains(built->notes, "on or below the capo"));
    }

    SECTION("under a capo the floor follows it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].capo = 3;
        // Capo-relative fret 2 is absolute fret 5; four frets down would be 1, under the capo.
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(Fraction{1, 2}, 2, 0, 4)}}});
        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const common::core::ChartNote& only = chart.notes[0];
        CHECK(only.fret == 5);
        REQUIRE(only.slide_out.has_value());
        if (only.slide_out.has_value())
        {
            CHECK(*only.slide_out == common::core::firstPlayableFret(3));
        }
        CHECK_FALSE(anyNoteContains(built->notes, "on or below the capo"));
    }
}

// What the importer STORES for a note carrying a technique: the whole notated ring and the whole
// curve mapped onto it, nothing clipped. The presented values beside them are how far that
// information survives the margin — rule 2's arithmetic, which core owns and tests on its own; the
// point here is that the import's synthesized geometry reaches it intact. Runs in 4/4, where the
// margin is a quarter beat.
TEST_CASE("Guitar Pro import stores whole payloads that presentation trims", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    SECTION("a bend plateau running to the ring's end presents no longer for it")
    {
        // The curve reaches two semitones a quarter beat in and then holds that value to the
        // notated end. Both points are stored on the whole one-beat ring; the plateau is not
        // information, so the drawn tail keeps only the margin before the next onset and the
        // redundant final point leaves with it.
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
        CHECK(chart.notes[0].sustain == Fraction{1});
        REQUIRE(bendCurve(chart.notes[0]).size() == 3);
        CHECK(bendCurve(chart.notes[0])[0].offset == Fraction{});
        CHECK(bendCurve(chart.notes[0])[1].offset == Fraction{1, 4});
        CHECK(bendCurve(chart.notes[0])[1].semitones == Catch::Approx(2.0));
        CHECK(bendCurve(chart.notes[0])[2].offset == Fraction{1});
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        CHECK(presented[0].sustain == Fraction{3, 4});
        CHECK(bendCurve(presented[0]).size() == 2);
    }

    SECTION("a bend change inside the trimmed region extends the tail to that change")
    {
        // The destination lands at 95% of a two-beat note — past the margin limit of 7/4 — so
        // the drawn tail overrides the margin, but only out to the change itself, not to the
        // stored end at two beats.
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
        CHECK(chart.notes[0].sustain == Fraction{2});
        REQUIRE(bendCurve(chart.notes[0]).size() == 3);
        CHECK(bendCurve(chart.notes[0]).back().offset == Fraction{19, 10});
        CHECK(bendCurve(chart.notes[0]).back().semitones == Catch::Approx(2.0));
        CHECK(presentedNotesOf(chart, built->tempo_map)[0].sustain == Fraction{19, 10});
    }

    SECTION("a change landing exactly at the margin truncates there, information intact")
    {
        // 87.5% of two beats is 7/4 — precisely the margin limit. The drawn tail ends there with
        // the bend's full information intact.
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
        CHECK(chart.notes[0].sustain == Fraction{2});
        REQUIRE(bendCurve(chart.notes[0]).size() == 3);
        CHECK(bendCurve(chart.notes[0]).back().offset == Fraction{7, 4});
        CHECK(bendCurve(chart.notes[0]).back().semitones == Catch::Approx(2.0));
        CHECK(presentedNotesOf(chart, built->tempo_map)[0].sustain == Fraction{7, 4});
    }

    SECTION("a trailing equal-fret hold keyframe holds no tail open")
    {
        // A legato slide onto the same fret is a hold, not a glide: it pins a pitch the note is
        // already sounding, so it cannot override the margin. The merge stores the whole ring and
        // its hold keyframe; the drawn tail trims and the keyframe leaves with it (the
        // hold-versus-glide distinction the 3D hand window also reads).
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
        CHECK(chart.notes[0].sustain == Fraction{9, 8});
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].fret == 5);
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        CHECK(presented[0].sustain == Fraction{7, 8});
        CHECK(presented[0].keyframes.empty());
    }

    SECTION("a scrape's terminal sits exactly at its sustain, stored and presented")
    {
        // The scrape's path is gesture geometry, so the trim compresses its terminal with the
        // tail rather than flooring on it — and the pick-slide rule that the slide-out IS the
        // sustain end holds on both sides of the derivation. R is a half beat here, exactly 2d,
        // so the gesture still yields the full margin.
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
        CHECK(chart.notes[0].sustain == Fraction{1, 2});
        const auto* const terminal = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(terminal != nullptr);
        CHECK(*terminal != chart.notes[0].fret);
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        const common::core::ChartNote& first = presented[0];
        CHECK(first.sustain == Fraction{1, 4});
        REQUIRE(first.slide_out.has_value());
    }
}

// Policy rule 19's crowding case: a scrape's path is derived gesture geometry, so when the room
// runs short the terminal leg is crunched, and where the leg STARTS decides how. A leg starting
// before the margin line ends on it, giving up no spacing at all. A leg starting on or after that
// line is already inside the window, so it halves its distance to the onset — the exception the
// spacing rule sanctions, since the gesture is literally defined in there. Runs in 4/4, where the
// minimum sustain distance is a quarter beat.
TEST_CASE("Guitar Pro import shows a crowded scrape squished against its gap", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    // The scrape's notated span always equals R here, so the gesture merely REACHES the next
    // onset — a span notated past it would be a deliberate hold and keep its full length. The
    // import stores that whole span with its terminal at the end; the numbers each section pins
    // are the DRAWN ones, where the leg rule has done its crunching.
    const auto crowded_scrape = [&syncs](const Fraction span) {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{carrierBeat(span, {pickSlideCarrier(64)}), noteBeat(span, 3, 2)}}});
        return buildGpSong(score);
    };
    // The gesture as the surfaces draw it, with the invariant every case shares asserted once:
    // a scrape's terminal sits exactly at its sustain, before and after the derivation alike.
    const auto squished_scrape = [](const auto& built) {
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        REQUIRE(chart.notes[0].slide_out.has_value());
        std::vector<common::core::ChartNote> presented = presentedNotesOf(chart, built->tempo_map);
        REQUIRE(presented[0].slide_out.has_value());
        return std::move(presented[0]);
    };

    SECTION("room above twice the margin still yields the full margin")
    {
        // R = one beat, comfortably above 2d: the gap is the whole quarter-beat margin.
        const auto built = crowded_scrape(Fraction{1, 4});
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.notes[0].sustain == Fraction{1});
        CHECK(squished_scrape(built).sustain == Fraction{3, 4});
    }

    SECTION("room exactly at the conflict threshold still pays the full margin")
    {
        // R = 3/8 beat = d + w, the tightest room where the full margin costs the leg nothing it
        // cannot spare: gap = d = 1/4 and the leg takes the remaining 1/8, landing exactly on its
        // window. Nothing is crunched because nothing conflicts.
        const auto built = crowded_scrape(Fraction{3, 32});
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.notes[0].sustain == Fraction{3, 8});
        const common::core::ChartNote scrape = squished_scrape(built);
        CHECK(scrape.sustain == Fraction{1, 8});
        REQUIRE(scrape.slide_out.has_value());
        if (scrape.slide_out.has_value())
        {
            CHECK(*scrape.slide_out != scrape.fret);
        }
    }

    SECTION("room above the threshold pays the margin whole rather than sharing it")
    {
        // R = 7/16 beat, inside the band an earlier revision of this rule crunched: it set the gap
        // to min(d, R/2) = 7/32, manufacturing a spacing violation where the full 1/4 margin was
        // affordable. The margin is paid whole and the leg takes 3/16.
        const auto built = crowded_scrape(Fraction{7, 64});
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.notes[0].sustain == Fraction{7, 16});
        CHECK(squished_scrape(built).sustain == Fraction{3, 16});
    }

    SECTION("room equal to the margin halves it, the worked example")
    {
        // R = 1/16 whole note against d = 1/16: a 1/32-whole-note gesture and a 1/32 gap — a
        // quarter beat of room splitting into an eighth beat each way. A genuine conflict, since
        // paying d whole would leave the leg nothing, and the even split lands it exactly on its
        // window.
        const auto built = crowded_scrape(Fraction{1, 16});
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.notes[0].sustain == Fraction{1, 4});
        CHECK(squished_scrape(built).sustain == Fraction{1, 8});
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
        CHECK(built->arrangements.front().chart.notes[0].sustain == Fraction{1, 8});
        const common::core::ChartNote scrape = squished_scrape(built);
        CHECK(scrape.sustain == Fraction{1, 16});
        REQUIRE(scrape.slide_out.has_value());
        if (scrape.slide_out.has_value())
        {
            CHECK(*scrape.slide_out != scrape.fret);
        }
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
        CHECK_FALSE(scrape.palm_mute);
        CHECK_FALSE(scrape.dead);
        // The terminal sits at the sustain, on both sides of the derivation: stored across the
        // carrier's whole notated beat, drawn back to the ordinary quarter-beat margin before the
        // fret-8 onset one beat later.
        const auto* const terminal = common::core::slideOutFretOrNull(scrape);
        REQUIRE(terminal != nullptr);
        CHECK(*terminal == 3);
        CHECK(scrape.sustain == Fraction{1});
        const common::core::ChartNote presented = presentedNotesOf(chart, built->tempo_map)[1];
        REQUIRE(presented.slide_out.has_value());
        CHECK(presented.sustain == Fraction{3, 4});
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
        const auto* const terminal = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(terminal != nullptr);
        CHECK(*terminal == 17);
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
        // Both carriers keep their required terminal, which ends each ring by definition — the
        // equal sustains above are therefore equal gesture lengths too.
        CHECK(chart.notes[0].slide_out.has_value());
        CHECK(chart.notes[1].slide_out.has_value());
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
        REQUIRE(chart.notes[0].slide_out.has_value());
        // The gesture spans the carrier's whole notated ring, which is where it ends.
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].sustain == Fraction{1});
    }

    SECTION("a note tail keeps the sustain margin before a scrape onset")
    {
        // A two-beat note draws its tail to the quarter-beat margin before the scrape's onset,
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
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(presentedNotesOf(chart, built->tempo_map)[0].sustain == Fraction{7, 4});
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
        REQUIRE(chart.notes[0].slide_out.has_value());
        CHECK(chart.notes[0].sustain == Fraction{2});
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
        CHECK(chart.notes[0].dead);
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
        // The ornament is played in the preceding note's time, so the fret-5 quarter STOPS at
        // the grace's onset rather than ringing under it: seven eighths of a beat, ending exactly
        // where the ornament starts.
        CHECK(chart.notes[0].sustain == Fraction{7, 8});
    }

    SECTION("a before-beat grace steals its lead from the beat before it")
    {
        // The steal is the BEAT's, not the string's: both members of the preceding beat end at
        // the ornament's onset even though the grace is on a third string of its own, which is
        // what keeps this distinct from the same-string clamp.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {beatOf(
                         Fraction{1, 4},
                         {GpNote{.string = 0, .fret = 5, .harmonic_type = ""},
                          GpNote{.string = 2, .fret = 9, .harmonic_type = ""}}),
                     graceBeat(GpGracePlacement::BeforeBeat, 7, 1),
                     noteBeat(Fraction{1, 4}, 8, 3)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        CHECK(chart.notes[0].string == 1);
        CHECK(chart.notes[0].sustain == Fraction{7, 8});
        CHECK(chart.notes[1].string == 3);
        CHECK(chart.notes[1].sustain == Fraction{7, 8});
        // The ornament itself lands one thirty-second ahead of its principal, which is the onset
        // the two rings above now end on.
        CHECK(chart.notes[2].string == 2);
        CHECK(chart.notes[2].position.offset == Fraction{7, 8});
    }

    SECTION("a tied predecessor shortened by the steal still merges past the principal")
    {
        // The tie merge keys on the tie flag, not on adjacency, so shortening the origin to the
        // ornament's onset cannot break the chain: the merged note rings through the grace and on
        // past the principal it is tied to, and presents as the deliberate hold it is.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat held = noteBeat(Fraction{1, 4}, 5);
        held.notes[0].tie_origin = true;
        GpBeat principal = noteBeat(Fraction{1, 4}, 5);
        principal.notes[0].tie_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{held, graceBeat(GpGracePlacement::BeforeBeat, 7, 1), principal}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        CHECK(chart.notes[0].string == 1);
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(presentedNotesOf(chart, built->tempo_map)[0].sustain == Fraction{2});
    }

    SECTION("a run of two before-beat graces steals back to the FIRST ornament")
    {
        // The run's leads stack backward from the principal, so the beat before it ends where the
        // run STARTS, not where its last ornament does — the boundary case the arithmetic exists
        // for.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(Fraction{1, 4}, 5),
                     graceBeat(GpGracePlacement::BeforeBeat, 7),
                     graceBeat(GpGracePlacement::BeforeBeat, 9),
                     noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 4);
        // Two thirty-second leads: the ornaments sound at 3/4 and 7/8 of the beat, and the quarter
        // before them stops at 3/4 rather than at the second ornament.
        CHECK(chart.notes[0].fret == 5);
        CHECK(chart.notes[0].sustain == Fraction{3, 4});
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position.offset == Fraction{3, 4});
        CHECK(chart.notes[2].fret == 9);
        CHECK(chart.notes[2].position.offset == Fraction{7, 8});
        CHECK(chart.notes[3].fret == 8);
        CHECK(chart.notes[3].position.beat == 2);
    }

    SECTION("a bend on a stolen ring is clipped, not squeezed into it")
    {
        // Guitar Pro states bend points as percentages of the NOTATED duration, so the curve is
        // laid out over the whole quarter and then loses the part the ornament took. Squeezing it
        // into the shortened ring would move every point and state a curve nobody wrote.
        GpBeat bent;
        bent.duration_whole = Fraction{1, 4};
        bent.notes = {GpNote{
            .string = 0,
            .fret = 5,
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
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {bent, graceBeat(GpGracePlacement::BeforeBeat, 7), noteBeat(Fraction{1, 4}, 8)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        CHECK(chart.notes[0].sustain == Fraction{7, 8});
        // The half-way point keeps the offset the score wrote it at; the destination point sat
        // past the stolen lead and leaves with it.
        REQUIRE(bendCurve(chart.notes[0]).size() == 2);
        CHECK(bendCurve(chart.notes[0])[0].offset == Fraction{});
        CHECK(bendCurve(chart.notes[0])[0].semitones == Catch::Approx(0.0));
        CHECK(bendCurve(chart.notes[0])[1].offset == Fraction{1, 2});
        CHECK(bendCurve(chart.notes[0])[1].semitones == Catch::Approx(1.0));
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
        // The steal is the grace's own voice's business: the first voice's half note rings on
        // through the ornament as the deliberate cross-voice hold it is, while the fret-5 quarter
        // sharing the grace's voice stops at the ornament's onset.
        CHECK(chart.notes[0].fret == 3);
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[1].fret == 5);
        CHECK(chart.notes[1].sustain == Fraction{7, 8});
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

    // The import stores no direction, so what proves the predecessor was read correctly is the
    // RESOLUTION: point the resolver at the wrong earlier note and the claim reads back as the
    // other motion — or, where the frets then match, resolves to nothing and the sweep drops it.
    const auto resolution = [](const GpBuiltSong& built, const std::size_t index) {
        const common::core::Chart& chart = built.arrangements.front().chart;
        return common::core::chartConnections(chart.notes, built.tempo_map).legato[index];
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
        // as a pull-off), so the claim resolves as a hammer-on.
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Legato);
        CHECK(resolution(*built, 2) == common::core::LegatoMotion::Hammer);
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
        // read as a hammer-on), so the claim resolves as a pull-off.
        CHECK(chart.notes[2].fret == 8);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Legato);
        CHECK(resolution(*built, 2) == common::core::LegatoMotion::Pull);
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
        const common::core::ChartShapes derived = spansOf(chart, built->tempo_map);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].sustain == Fraction{2});
        CHECK(shapeArrivalsOf(chart, built->tempo_map)[0]);
    }

    SECTION("a short-ringing chord's span still ends before the taps")
    {
        const auto built = buildGpSong(make_score(Fraction{1, 4}));
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // The quarter chord's ring ends at the first tap's onset, so the span keeps its own
        // notated duration, the taps land outside it, and the box stays a strummed box.
        const common::core::ChartShapes derived = spansOf(chart, built->tempo_map);
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].sustain == Fraction{1});
        CHECK_FALSE(shapeArrivalsOf(chart, built->tempo_map)[0]);
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
        CHECK(chart.notes[0].attack == common::core::NoteAttack::LeftTap);
        CHECK(chart.notes[1].attack == common::core::NoteAttack::Tap);
        CHECK(chart.notes[2].attack == common::core::NoteAttack::Tap);
        CHECK(chart.notes[3].attack == common::core::NoteAttack::Legato);
        CHECK(spansOf(chart, built->tempo_map).shapes.empty());
    }
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
        CHECK(chart.notes[1].fret == 7);
        CHECK(chart.notes[1].position == GridPosition{.measure = 1, .beat = 2});
        CHECK(chart.notes[2].fret == 9);
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
        CHECK(chart.notes[1].fret == 7);
        CHECK(
            chart.notes[1].position ==
            GridPosition{.measure = 1, .beat = 1, .offset = Fraction{7, 8}});
        CHECK(chart.notes[2].fret == 9);
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].keyframes[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{1});
        // No onset was fabricated, so the fret-3 note stores its whole beat and draws the plain
        // margin before the scoop's notated beat.
        CHECK(chart.notes[0].sustain == Fraction{1});
        CHECK(presentedNotesOf(chart, built->tempo_map)[0].sustain == Fraction{3, 4});
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 4});
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 8});
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
        // extends to hold the floored scoop (the keyframe may land exactly on the end).
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 8});
        CHECK(chart.notes[1].sustain == Fraction{1, 8});
    }

    SECTION("an existing chain keyframe halves the scoop window")
    {
        GpScore score = makeLinearScore(1, syncs);
        // Flags 17 = slide-in from below (16) + shift slide (1): the chain resolver first
        // glides the note to the fret-10 landing, then the scoop is inserted ahead of that
        // keyframe. A thirty-second head with the landing an eighth of a beat later gives a
        // degenerate gap, so the shift keyframe sits at half of it (1/16) — under the 1/8
        // floor the scoop would otherwise take, so the scoop halves the keyframe instead and
        // the payload stays strictly ascending.
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{noteBeat(Fraction{1, 32}, 8, 0, 17), noteBeat(Fraction{1, 32}, 10)}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        REQUIRE(chart.notes[0].keyframes.size() == 2);
        CHECK(chart.notes[0].keyframes[0].offset == Fraction{1, 32});
        CHECK(chart.notes[0].keyframes[0].fret == 8);
        CHECK(chart.notes[0].keyframes[1].offset == Fraction{1, 16});
        CHECK(chart.notes[0].keyframes[1].fret == 10);
        CHECK(chart.notes[0].keyframes[0].offset < chart.notes[0].keyframes[1].offset);
        CHECK(chart.notes[0].fret == 6);
    }

    SECTION("a bend on the landing coexists with the scoop")
    {
        GpScore score = makeLinearScore(1, syncs);
        GpBeat bent = noteBeat(Fraction{1, 4}, 8, 0, 16);
        // A rise to a whole step at the half: the bend states land at the onset, 1/2 and 1 beat,
        // and the quarter-beat scoop keyframe interleaves between the first two. One array now
        // holds both channels, so the scoop takes its place in the SAME order without touching a
        // bend value — which is the coupling the model exists for.
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
        // Three moments on the ring: the scoop's arrival first, then the two stated bend values.
        REQUIRE(chart.notes[1].keyframes.size() == 3);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 4});
        CHECK(chart.notes[1].keyframes[0].fret == 8);
        CHECK_FALSE(chart.notes[1].keyframes[0].bend.has_value());
        CHECK(chart.notes[1].keyframes[1].offset == Fraction{1, 2});
        CHECK_FALSE(chart.notes[1].keyframes[1].fret.has_value());
        REQUIRE_FALSE(bendCurve(chart.notes[1]).empty());
        CHECK(bendCurve(chart.notes[1]).back().offset == Fraction{1});
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
        REQUIRE(chart.notes[0].keyframes.size() == 1);
        CHECK(chart.notes[0].keyframes[0].offset == Fraction{1, 16});
        CHECK(chart.notes[0].keyframes[0].fret == 8);
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(chart.notes[0].sustain == Fraction{1, 8});
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].fret == 7);
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
        // The transform runs before anything reads the stream, so the landing is already a slide
        // when the tail rules judge it: head ON its beat, scoop over an eighth (a quarter of the
        // half-beat duration, floored at the minimum slide window), and the stored ring still the
        // notated half beat.
        CHECK(chart.notes[1].fret == 6);
        CHECK(chart.notes[1].position.beat == 2);
        CHECK(chart.notes[1].position.offset == Fraction{});
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].offset == Fraction{1, 8});
        CHECK(chart.notes[1].keyframes[0].fret == 8);
        CHECK(chart.notes[1].sustain == Fraction{1, 2});
        // No onset was fabricated: the fret-3 note stores its whole beat and draws the plain
        // margin before the scoop's notated beat.
        CHECK(chart.notes[0].sustain == Fraction{1});
        const std::vector<common::core::ChartNote> presented =
            presentedNotesOf(chart, built->tempo_map);
        CHECK(presented[0].sustain == Fraction{3, 4});
        // The scoop survives the effect-free drop — it is a slide now — and draws a tail down to
        // the margin before the fret-5 onset.
        CHECK(presented[1].sustain == Fraction{1, 4});
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].fret == 5);

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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].fret == 3);
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
        REQUIRE(chart.notes[1].keyframes.size() == 1);
        CHECK(chart.notes[1].keyframes[0].fret == 3);

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
        CHECK(chart.notes[1].keyframes.empty());
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
        REQUIRE_FALSE(chart.notes[1].keyframes.empty());
        CHECK(chart.notes[1].keyframes.back().fret == 7);
        CHECK(chart.notes[2].fret == 7);
        CHECK(chart.notes[2].keyframes.empty());
    }
}

// The crush fallback compresses a crowded trail-off to the SMALLEST LEGAL end rather than
// keeping its full length (normalization rule 2): strictly positive, and strictly after the
// note's last chain keyframe. A legato chain inheriting a trail-off is where the keyframe floor
// bites — the plain margin target lands on the junction itself, so the end steps one minimum
// window past it instead of colliding with the glide.
TEST_CASE(
    "Guitar Pro import floors a crushed trail-off after its last keyframe", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    // Flags 2 = legato: the fret-10 landing folds into the fret-8 origin as a keyframe one
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
    REQUIRE(merged.keyframes.size() == 1);
    CHECK(merged.keyframes[0].offset == Fraction{1});
    CHECK(merged.keyframes[0].fret == 10);
    const auto* const slide_out = common::core::slideOutFretOrNull(merged);
    REQUIRE(slide_out != nullptr);
    // Stored, the gesture ends with the merged ring; drawn, it compresses to one minimum slide
    // window past the junction — never on or before it, which chart validation rejects, and never
    // the uncompressed 5/4 end that would run past the onset.
    CHECK(merged.sustain == Fraction{5, 4});
    CHECK(merged.sustain == Fraction{5, 4});
    const common::core::ChartNote presented = presentedNotesOf(chart, built->tempo_map)[0];
    REQUIRE(presented.slide_out.has_value());
    REQUIRE_FALSE(presented.keyframes.empty());
    if (presented.slide_out.has_value() && !presented.keyframes.empty())
    {
        CHECK(presented.sustain == Fraction{9, 8});
        CHECK(presented.sustain > presented.keyframes.back().offset);
    }
    CHECK(presented.sustain == Fraction{9, 8});
}

// A glide whose landing is the OPEN string has nothing pressed to arrive with (user rule
// 2026-08-20: a pitched keyframe may not be fret 0), so it degrades to the unpitched trail-off
// exactly like a missing landing, and the open-string landing keeps its own onset.
TEST_CASE(
    "Guitar Pro import degrades a glide to the open string into a trail-off", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };

    GpScore score = makeLinearScore(1, syncs);
    // Flags 2 = legato slide from the fret-8 note; its landing is the open string.
    score.tracks[0].bars.push_back(
        GpBar{.voices = {{noteBeat(Fraction{1, 4}, 8, 0, 2), noteBeat(Fraction{1, 4}, 0)}}});

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.notes.size() == 2);

    const common::core::ChartNote& origin = chart.notes[0];
    CHECK(origin.fret == 8);
    CHECK(origin.keyframes.empty());
    CHECK(common::core::slideOutFretOrNull(origin) != nullptr);
    CHECK(chart.notes[1].fret == 0);
    CHECK(chart.notes[1].keyframes.empty());
    CHECK(common::core::validateChartRules(chart, built->tempo_map).has_value());
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 3);
        // The gesture is stored at the note's own beat-long ring; what it DRAWS compresses to the
        // margin before the fret-3 onset, and the exit placement lands exactly on that drawn end
        // so the window rides the gesture into the fret-3 arrival.
        CHECK(chart.notes[0].sustain == Fraction{1});
        const common::core::ChartNote drawn = presentedNotesOf(chart, built->tempo_map)[0];
        REQUIRE(drawn.slide_out.has_value());
        if (drawn.slide_out.has_value())
        {
            CHECK(drawn.sustain == Fraction{3, 4});
        }
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 5);
        CHECK(chart.notes[0].sustain == Fraction{1});
        const common::core::ChartNote drawn = presentedNotesOf(chart, built->tempo_map)[0];
        REQUIRE(drawn.slide_out.has_value());
        if (drawn.slide_out.has_value())
        {
            CHECK(drawn.sustain == Fraction{3, 4});
        }
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
        // Fret 22 with the four-fret upward default would exit at 26 — off a 24-fret neck.
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{noteBeat(Fraction{1, 4}, 22, 0, 8), noteBeat(Fraction{1, 4}, 22)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == common::core::g_max_fret);
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 4);
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

        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 4);
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        // Nothing follows, so nothing compresses the gesture: the full one-beat span with
        // the four-fret default, and the window rests where it ends.
        CHECK(*slide_out == 4);
        CHECK(chart.notes[0].sustain == Fraction{1});
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 4);
        CHECK(chart.notes[0].sustain == Fraction{1, 8});
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
        const auto* const slide_out = common::core::slideOutFretOrNull(chart.notes[0]);
        REQUIRE(slide_out != nullptr);
        CHECK(*slide_out == 4);
        CHECK(chart.notes[0].sustain == Fraction{1, 8});
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

// A hold keyframe places NO hand position, not even at a phrase boundary. Nothing travels across an
// equal-fret keyframe, so it announces no new hand position; the generator's coverage suppression
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
    // the continuation trails off, so the merged note's hold keyframe lands on bar 2 beat 1 —
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
    REQUIRE_FALSE(held->keyframes.empty());
    // The keyframe the continuation left behind holds the same fret, which is what makes it a hold.
    CHECK(held->keyframes.front().fret == held->fret);

    const GridPosition hold_position = common::core::advanceGridPosition(
        built->tempo_map, held->position, held->keyframes.front().offset);
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

// A bend on a note that shift-slides into its landing. Ordinary lead vocabulary, and it used to
// refuse the whole song two different ways: the trim that ends the glide before the landing set the
// sustain without clipping the payload past it, so a flat prebend left a bend point outside the
// tail — and when the bend's own last CHANGING point reached the landing, the informative floor
// pushed the arrival onto the landing's own onset, where a pitched keyframe may not sit.
TEST_CASE("Guitar Pro import keeps a bend and a shift slide on one note", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // A quarter note that shift-slides (flag 1 = shift) into a re-picked landing a quarter later,
    // with the bend shape under test, plus the landing itself.
    const auto import_with_bend = [&syncs](const GpBend& bend) {
        GpScore score = makeLinearScore(1, syncs);
        GpNote sliding{.string = 0, .fret = 5, .harmonic_type = ""};
        sliding.slide_flags = 1;
        sliding.bend = bend;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {GpBeat{.duration_whole = Fraction{1, 4}, .notes = {sliding}},
                     GpBeat{
                         .duration_whole = Fraction{1, 4},
                         .notes = {GpNote{.string = 0, .fret = 9, .harmonic_type = ""}}
                     }}
                }
            });
        return buildGpSong(score);
    };

    SECTION("a flat prebend, whose points pin nothing")
    {
        // Every value equal: no point CHANGES anything, so the informative floor is zero and the
        // tail trims to the margin — leaving the destination point outside it unless it is clipped.
        GpBend flat;
        flat.origin_value = 100.0;
        flat.middle_value = 100.0;
        flat.destination_value = 100.0;
        const auto built = import_with_bend(flat);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE_FALSE(chart.notes.empty());
        const common::core::ChartNote& sliding = chart.notes.front();
        for (const BendReading& point : bendCurve(sliding))
        {
            CHECK_FALSE(sliding.sustain < point.offset);
        }
    }

    SECTION("a bend that keeps rising to the note's end")
    {
        // The last changing point sits at the note's end, which is the landing's onset, so the
        // informative floor would place the arrival exactly there.
        GpBend rising;
        rising.origin_value = 0.0;
        rising.middle_value = 50.0;
        rising.destination_value = 100.0;
        const auto built = import_with_bend(rising);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote& sliding = chart.notes.front();
        // Whatever the glide became, nothing pitched may sit on the landing's own onset.
        const Fraction gap =
            common::core::beatDistance(built->tempo_map, sliding.position, chart.notes[1].position);
        for (const common::core::Keyframe& keyframe : sliding.keyframes)
        {
            CHECK(keyframe.offset < gap);
        }
    }
}

// No single out-of-range value in a score may cost the whole song. Every field below arrives from
// the file unvalidated and is bounded by the chart rules, so each one used to be able to reach
// validation and refuse the import outright — a song lost to one junk integer. Import is a commit
// point: it reduces what it cannot represent and says so in the conversion notes.
TEST_CASE("Guitar Pro import survives every out-of-range field", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // Builds a one-note score and returns the built song, so each case below differs only in the
    // field it puts out of range.
    const auto import_with = [&syncs](const GpNote& note, const int capo, const bool nine_strings) {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].capo = capo;
        if (nine_strings)
        {
            score.tracks[0].tuning_midi = {28, 33, 40, 45, 50, 55, 59, 64, 69};
        }
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{GpBeat{.duration_whole = Fraction{1, 4}, .notes = {note}}}}});
        return buildGpSong(score);
    };

    SECTION("a capo past the last usable fret")
    {
        const auto built =
            import_with(GpNote{.string = 0, .fret = 3, .harmonic_type = ""}, 40, false);
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.tuning.capo == 12);
        CHECK(anyNoteContains(built->notes, "capo at fret 40"));
    }

    SECTION("a string the tuning does not have")
    {
        const auto built =
            import_with(GpNote{.string = 8, .fret = 3, .harmonic_type = ""}, 0, false);
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.notes.empty());
        CHECK(anyNoteContains(built->notes, "string the tuning does not have"));
    }

    SECTION("more strings than the model speaks about")
    {
        const auto built =
            import_with(GpNote{.string = 0, .fret = 3, .harmonic_type = ""}, 0, true);
        REQUIRE(built.has_value());
        CHECK(built->arrangements.front().chart.tuning.strings.size() == 8);
        CHECK(anyNoteContains(built->notes, "more than 8 strings"));
    }

    SECTION("a fret that lands past the neck once the capo shifts it")
    {
        // Capo 12 plus a fret-24 note is absolute fret 36, past the cap the model bounds notes
        // by (g_max_fret, the drawn board's 24).
        const auto built =
            import_with(GpNote{.string = 0, .fret = 24, .harmonic_type = ""}, 12, false);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        CHECK(chart.notes[0].fret == common::core::g_max_fret);
        CHECK(
            anyNoteContains(built->notes, "past fret " + std::to_string(common::core::g_max_fret)));
    }

    SECTION("a natural harmonic whose node would sit off the neck")
    {
        // The node carries the fretting finger, so the neck is its ceiling; a high partial's
        // bridge-side alternate plus a capo pushes it past the last fret.
        GpNote harmonic{.string = 0, .fret = 0, .harmonic_type = ""};
        harmonic.harmonic_type = "Natural";
        harmonic.harmonic_fret = 24.0;
        const auto built = import_with(harmonic, 12, false);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 1);
        const common::core::ChartNote& only = chart.notes[0];
        REQUIRE(only.harmonic_node.has_value());
        if (only.harmonic_node.has_value())
        {
            CHECK(*only.harmonic_node <= common::core::harmonicNodeCeiling(only));
        }
    }
}

// RULE B, THE ELASTIC LET-RING TRANSLATION (user ruling 2026-08-31). A let-ring passage is a
// TEXTURE: the notes stack up and the whole stack stops together, so a marked note's notated
// duration is a DEMAND rather than a length and the length is the REGION's. The region is a
// maximal run of consecutive marked beats in one voice; its end is the TAIL's own GP-playback
// answer — the latest-onset mark, the one place the source can still say where the texture stops —
// and every interior member takes it.
TEST_CASE("Guitar Pro import normalizes a let-ring region to its tail", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};

    SECTION("THE TAIL CAP EXACT: the tail's own cap is the whole region's end")
    {
        // Three marks stacking up on three strings, then an unmarked beat and a plain bar. The
        // TAIL is the third mark, its sliding one-measure cap runs from ITS onset — beat three
        // plus a bar is beat seven — and every member of the region rings to exactly there.
        //
        // THE DISCRIMINATION, and the whole of what the rule changed: under the shipped per-note
        // walk each mark answered with its OWN cap, so all three rang four beats and stopped one
        // after another like a staircase. A texture does not stop like that; it stops together.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const tail = noteOnChartString(chart.notes, 3);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(tail != nullptr);
        // Every member ends at beat seven: six beats from the first, five from the second, four
        // from the tail — which is the tail's own cap and nobody else's.
        CHECK(first->sustain == Fraction{6});
        CHECK(second->sustain == Fraction{5});
        CHECK(tail->sustain == Fraction{4});
        CHECK(anyNoteContains(built->notes, "let-ring rings were normalized to their region"));
    }

    SECTION("an unmarked beat ends the region, so a later passage never lengthens an earlier one")
    {
        // The boundary the region definition buys, and the reason it is CONSECUTIVE marks rather
        // than "everything up to the next rest": an unmarked beat between two marked runs is two
        // passages, and the second one's tail must not reach back and stretch the first. Here the
        // lone mark at beat one is its own region — cap at beat five — while the pair at beats
        // three and four run to the LATER tail's cap at beat eight.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     noteBeat(quarter, 3, 3),
                     letRingBeat(quarter, 9, 1),
                     letRingBeat(quarter, 10, 2)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const lone = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const early = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const late = noteOnChartString(chart.notes, 3);
        REQUIRE(lone != nullptr);
        REQUIRE(early != nullptr);
        REQUIRE(late != nullptr);
        CHECK(lone->sustain == Fraction{4});
        // The second region's own tail is beat four, so its cap is beat seven and both of its
        // members take it. Nothing here states a new grip — the figure only ever adds strings —
        // so the grip-contradiction cut leaves all three extensions standing.
        CHECK(early->sustain == Fraction{5});
        CHECK(late->sustain == Fraction{4});
    }

    SECTION("the voice's next REST is the tail's stop, and the region ends there")
    {
        // The cap is a CEILING, never a floor: where the tail's own voice states silence first,
        // that is what the region takes. Every member normalizes to it, the interior included, so
        // an authored rest ends the whole texture rather than one note of it.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     restBeat(quarter),
                     noteBeat(quarter, 3, 3)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const tail = noteOnChartString(chart.notes, 2);
        REQUIRE(first != nullptr);
        REQUIRE(tail != nullptr);
        // The rest lands on beat three, which is where both rings stop — two beats for the first
        // member and one for the tail.
        CHECK(first->sustain == Fraction{2});
        CHECK(tail->sustain == Fraction{1});
    }

    SECTION("the normalized region derives as ONE accumulation span over the whole texture")
    {
        // The point of the rule, read back by the derivation that knows nothing about let ring:
        // with the members' rings ending together, the opening law brackets the texture as one
        // statement dated from its first note — which is the figure the whole ruling grew from.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4),
                     noteBeat(quarter, 3, 4)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartShapes derived = spansOf(chart, built->tempo_map);
        REQUIRE(derived.shapes.size() >= 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        CHECK(derived.shapes[0].founding == common::core::SpanFounding::Accumulation);
        CHECK(shapeArrivalsOf(chart, built->tempo_map)[0]);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        // Four stops, not three: the unmarked note that ends the region still SOUNDS inside the
        // texture's own ring, so the accumulation absorbs it — an unmarked lone arrival rides
        // along, and what it never does is extend the series' bound (the region ends where the
        // MARKS do, which is what the tail cap above already fixed).
        CHECK(
            heldFrets(derived.postures[derived.shapes[0].posture]) ==
            std::vector<std::optional<int>>{5, 7, 9, 3});
    }
}

// THE GRIP-CONTRADICTION CUT (user ruling 2026-09-01, the clean let-ring baseline). Rule B's
// region end is the audibility cap the extension may reach, and the cut is the one thing that can
// stop it earlier: a fretting statement stating a DIFFERENT fret on a GRIPPED string — a string
// whose sound covers the statement's instant, END-INCLUSIVE — cuts every marked extension of ITS
// OWN VOICE crossing that instant, floored at the note's written (tie-merged) duration. The grip
// is SOUND-scoped: it expires with its sound, never persisting as hand memory and never frozen at
// any ring's own strike. It is VOICE-scoped as well (user ruling 2026-09-01, "events should not
// cut rings in another voice"): grip, statement and victim are all one line of the transcription,
// while the same-string clamp keeps its cross-voice reach because a restrike is physics. Each
// section below kills one measured rival reading.
TEST_CASE("Guitar Pro import cuts let-ring extensions at grip contradictions", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};
    constexpr Fraction whole{1};
    // A beat sounding several strings at once: the shared fixtures build one note per beat, and
    // the grip figures here need a drone struck WITH a note on the string a statement then moves.
    const auto chordBeatOf = [](const Fraction duration, const std::vector<GpNote>& sounded) {
        GpBeat beat;
        beat.duration_whole = duration;
        beat.notes = sounded;
        return beat;
    };
    // Four rests fill the second bar where a figure needs one: the tail's own-voice rest is what
    // pins the region end inside the score instead of leaving it to the score's edge.
    const auto restBar = [] {
        return GpBar{
            .voices = {
                {restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4})}
            }
        };
    };

    SECTION("a fret-changing restrike cuts every crossing marked extension at its onset")
    {
        // Two marks stack up while a drone THE SAME VOICE struck holds a third string — carried
        // there by a tie, because one voice's beats tile its bar and a drone under them can only
        // be written as a held member of those beats. The third beat restrikes that string at
        // ANOTHER fret. That statement contradicts the grip its own voice is sounding, so it is a
        // cut event, and BOTH extensions crossing it cap there — rule 3a's "ALL let-ring tails
        // leading to that contradiction", not just the contradicted string's.
        //
        // The drone was a SECOND VOICE's until the cut became voice-scoped (user ruling
        // 2026-09-01); that fixture now belongs to the sibling section proving no such cut fires
        // across the voice boundary, and this one states the within-voice law it always meant to.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeatOf(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .tie_origin = true, .harmonic_type = ""}}),
                     chordBeatOf(
                         quarter,
                         {GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{
                              .string = 5, .fret = 3, .tie_destination = true, .harmonic_type = ""
                          }}),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const drone = noteOnChartString(chart.notes, 6);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(drone != nullptr);
        // The region (rest-capped) would run both marks to beat five; the cut caps them at the
        // restrike on beat three — two beats from the first mark, one from the second.
        CHECK(first->sustain == Fraction{2});
        CHECK(second->sustain == Fraction{1});
        // The tie-carried drone itself is unmarked: its merged ring is the two beats it was
        // written for, the clamp bounds it at its own restrike, and the cut never touches an
        // unextended ring.
        CHECK(drone->sustain == Fraction{2});
        CHECK(anyNoteContains(built->notes, "2 let-ring rings were clipped"));
        CHECK(built->let_ring_clip.rings == 2);
        CHECK(built->let_ring_clip.beats == Fraction{4});
    }

    SECTION("the cutting note itself rings on — the last note of the sequence is never its victim")
    {
        // The sequence's own third mark restrikes the first mark's string at a new fret: it is
        // the cut event that ends every tail under it, and it is struck AT that instant, so it
        // cannot cut itself — rule 2's last-note behavior falling out of the self-exclusion
        // rather than being a special case. Three values discriminate its sustain: one is its
        // written ring (wrongly cut by its own event), two is the region cap it should reach.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 0),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // The first mark's ring was already ended by its own string's restrike (the clamp); the
        // second mark's crossing tail is what the cut takes back to its written beat.
        const common::core::ChartNote* const undercut = noteOnChartString(chart.notes, 2);
        REQUIRE(undercut != nullptr);
        CHECK(undercut->sustain == Fraction{1});
        // The cutting note, found past the strike (noteOnChartString answers with the EARLIEST
        // onset on a string, and here that is the first mark).
        const auto found =
            std::ranges::find_if(chart.notes, [](const common::core::ChartNote& note) {
                return note.string == 1 &&
                       GridPosition{.measure = 1, .beat = 1, .offset = {}} < note.position;
            });
        REQUIRE(found != chart.notes.end());
        CHECK(found->sustain == Fraction{2});
        CHECK(built->let_ring_clip.rings == 1);
        CHECK(built->let_ring_clip.beats == Fraction{2});
    }

    SECTION("a same-fret restrike cuts nothing")
    {
        // The same figure with the restrike stating the fret the grip already holds: the grip
        // standing is not a new grip, so the crossing extension rides through to the region end.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     noteBeat(quarter, 5, 0),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const crossing = noteOnChartString(chart.notes, 2);
        REQUIRE(crossing != nullptr);
        // Three beats: onset two to the rest-capped region end at five. One is what a cut at the
        // restrike would leave.
        CHECK(crossing->sustain == Fraction{3});
        CHECK_FALSE(anyNoteContains(built->notes, "clipped"));
        CHECK(built->let_ring_clip.rings == 0);
        CHECK(built->let_ring_clip.beats == Fraction{});
    }

    SECTION("a statement on a string whose sound ended STRICTLY earlier cuts nothing")
    {
        // The drone is struck WITH a quarter note on a second string, and that string is
        // restated at another fret a beat AFTER its sound ended. Both dead rivals would cut
        // here: a grip persisting as hand memory still holds the silent string, and the retired
        // per-ring reading froze the drone's grip at its own strike, where the string really was
        // held. The sound-scoped grip expired with the sound — the motivating figure's own
        // measure-5 restatement of a long-silent string is this case — so nothing is contradicted
        // and the drone rings to its region end. Two beats is what either rival would leave.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeatOf(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     noteBeat(quarter, 3, 3),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const drone = noteOnChartString(chart.notes, 1);
        REQUIRE(drone != nullptr);
        CHECK(drone->sustain == Fraction{4});
        CHECK_FALSE(anyNoteContains(built->notes, "clipped"));
        CHECK(built->let_ring_clip.rings == 0);
    }

    SECTION("a ring ending exactly at the statement's instant still counts as gripped")
    {
        // The same co-struck figure with the restatement arriving exactly where the string's
        // quarter-note sound ends: END-INCLUSIVE cover, so the statement contradicts and the
        // drone caps at its written beat. Under the end-exclusive reading the cut is unreachable
        // by construction — the clamp guarantees no ring outlives the next onset on its own
        // string — measured as ZERO cuts corpus-wide, which is what killed that reading. This is
        // also the accepted drone tradeoff (user 2026-09-01): a drone co-struck with a note on a
        // melody's string cuts at the melody's first move, the watch-itemed clean-baseline cost
        // (docs/tracking/watch-items.md).
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeatOf(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3),
                     noteBeat(quarter, 3, 3)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const drone = noteOnChartString(chart.notes, 1);
        REQUIRE(drone != nullptr);
        // One beat, the written floor at the cut; four is what a spared (or exclusive-read) ring
        // would keep.
        CHECK(drone->sustain == Fraction{1});
        CHECK(anyNoteContains(built->notes, "1 let-ring rings were clipped"));
        CHECK(built->let_ring_clip.rings == 1);
        CHECK(built->let_ring_clip.beats == Fraction{3});
    }

    SECTION("a tie-merged marked note extends past its merged end like any other")
    {
        // The deleted exemption's own figure: a marked tie origin whose continuation merges into
        // one two-beat note, under a region whose cap runs to beat five. The exemption held the
        // merged ring at two; the baseline law extends it to the cap like every other marked
        // note, because the reference's tie-end cap is its walk's tie-BLIND string lookup, not a
        // statement about the sound — Guitar Pro audibly rings tied let-ring notes past the
        // written duration (user-verified by ear 2026-09-01).
        GpScore score = makeLinearScore(1, syncs);
        GpBeat origin = letRingBeat(quarter, 5);
        origin.notes.front().tie_origin = true;
        GpBeat continuation = noteBeat(quarter, 5);
        continuation.notes.front().tie_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{origin, continuation, noteBeat(quarter, 7, 5), noteBeat(quarter, 7, 5)}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        // Four beats, the region cap; two is the merged written ring the exemption froze it at.
        CHECK(marked->sustain == Fraction{4});
        CHECK(anyNoteContains(built->notes, "1 let-ring rings were normalized to their region"));
        CHECK_FALSE(anyNoteContains(built->notes, "kept their shipped rings"));
        CHECK(built->let_ring_clip.rings == 0);
    }

    SECTION("a slide-out marked note keeps its shipped ring")
    {
        // The one exemption that stands: an unpitched slide-out states the ring's own end (LAW I
        // — the release IS the end, physically forced), so the mark extends nothing.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat origin = letRingBeat(quarter, 5);
        origin.notes.front().slide_flags = 4;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {origin,
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        REQUIRE(marked->slide_out.has_value());
        CHECK(marked->sustain == Fraction{1});
        CHECK(anyNoteContains(built->notes, "1 let-ring marks kept their shipped rings"));
        CHECK_FALSE(anyNoteContains(built->notes, "normalized to their region"));
    }

    SECTION("the merged-written floor holds when a contradiction precedes it")
    {
        // The tie-merged figure again, with the marked note's OWN voice stating a cut event ONE
        // beat in — inside the merged written duration — by moving a co-struck string. The cut
        // lands there but never goes below the written (merged) end: three values discriminate
        // the assertion — four is no cut at all, one is a cut with no floor, and the merged two
        // beats is the law. The contradiction was a second voice's until the cut became
        // voice-scoped (user ruling 2026-09-01); scoped, it has to come from this note's own line.
        GpScore score = makeLinearScore(1, syncs);
        const GpBeat origin = chordBeatOf(
            quarter,
            {GpNote{
                 .string = 0, .fret = 5, .tie_origin = true, .let_ring = true, .harmonic_type = ""
             },
             GpNote{.string = 4, .fret = 3, .harmonic_type = ""}});
        const GpBeat continuation = chordBeatOf(
            quarter,
            {GpNote{.string = 0, .fret = 5, .tie_destination = true, .harmonic_type = ""},
             GpNote{.string = 4, .fret = 7, .harmonic_type = ""}});
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{origin, continuation, noteBeat(quarter, 7, 5), noteBeat(quarter, 7, 5)}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{2});
        CHECK(anyNoteContains(built->notes, "1 let-ring rings were clipped"));
        CHECK(built->let_ring_clip.rings == 1);
        CHECK(built->let_ring_clip.beats == Fraction{2});
    }

    // THE CUT IS PER VOICE, END TO END (user ruling 2026-09-01: "events should not cut rings in
    // another voice"). The four sections below scope all three halves of the pass — the grip, the
    // statement judged against it, and the extensions the event caps — to one line of the
    // transcription, and pin the one bound that stays deliberately cross-voice: the same-string
    // clamp, which is physics rather than grammar.

    SECTION("a voice's own contradiction cuts its own tails and spares another voice's")
    {
        // ONE cut event, two marked extensions crossing it, one in each voice. Voice zero states
        // the contradiction against a string ITS OWN tie-carried drone is sounding, so the event
        // is real and its own mark caps at it; the second voice's mark, crossing the very same
        // instant, runs on to its region end. Under the pre-change GLOBAL law both capped at two
        // — the numbers discriminate the reading rather than merely agreeing with it.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeatOf(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .tie_origin = true, .harmonic_type = ""}}),
                     chordBeatOf(
                         quarter,
                         {GpNote{
                             .string = 5, .fret = 3, .tie_destination = true, .harmonic_type = ""
                         }}),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3)},
                    {letRingBeat(quarter, 7, 1),
                     noteBeat(quarter, 9, 2),
                     noteBeat(quarter, 9, 2),
                     noteBeat(quarter, 9, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const own = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const other = noteOnChartString(chart.notes, 2);
        REQUIRE(own != nullptr);
        REQUIRE(other != nullptr);
        // The cutting voice's own mark: written one, region end four, capped at the contradiction
        // on beat three.
        CHECK(own->sustain == Fraction{2});
        // The other voice's mark, crossing the same instant, reaches the region end untouched.
        CHECK(other->sustain == Fraction{4});
        CHECK(built->let_ring_clip.rings == 1);
        CHECK(built->let_ring_clip.beats == Fraction{2});
    }

    SECTION("another voice restriking the ring's own string still clamps it")
    {
        // The bound that stays CROSS-VOICE on purpose. A second voice restrikes the very string
        // the marked ring sounds on: one finger, one string, and the sound stops whichever line
        // wrote the strike — so the clamp bounds the extension at that onset. Three values
        // discriminate: four is the region end with no clamp, one is the written ring a cut would
        // leave, and two is the clamp. That the CLIP counter stays zero is what proves the
        // shortening came from the clamp and not from a cut this ruling would have had to spare.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)},
                    {restBeat(quarter),
                     restBeat(quarter),
                     noteBeat(quarter, 9, 0),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{2});
        CHECK(built->let_ring_clip.rings == 0);
        // The restrike the clamp answered to, so the fixture cannot silently stop stating one.
        const auto restrike =
            std::ranges::find_if(chart.notes, [](const common::core::ChartNote& note) {
                return note.string == 1 &&
                       note.position == GridPosition{.measure = 1, .beat = 3, .offset = {}};
            });
        REQUIRE(restrike != chart.notes.end());
        CHECK(restrike->fret == 9);
    }

    SECTION("a within-voice contradiction cuts in any voice slot, beside another line")
    {
        // The cut is per voice, not per FIRST voice: the whole figure sits in the second slot
        // while an unrelated line runs in the first, and it cuts exactly as it does alone (the
        // sibling section above, same marks, same instants, same numbers).
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 12, 2),
                     noteBeat(quarter, 12, 2),
                     noteBeat(quarter, 12, 2),
                     noteBeat(quarter, 12, 2)},
                    {chordBeatOf(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .tie_origin = true, .harmonic_type = ""}}),
                     chordBeatOf(
                         quarter,
                         {GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{
                              .string = 5, .fret = 3, .tie_destination = true, .harmonic_type = ""
                          }}),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        CHECK(first->sustain == Fraction{2});
        CHECK(second->sustain == Fraction{1});
        CHECK(built->let_ring_clip.rings == 2);
        CHECK(built->let_ring_clip.beats == Fraction{4});
    }

    SECTION("a statement contradicting only another voice's sound is no cut event at all")
    {
        // THE GRIP is voice-scoped too, not just the victim set — which is what makes the
        // half-measure (a global grip with voice-scoped victims) a different law, and this
        // section the one that separates them. The statement and both marked extensions are the
        // SAME voice's, and the only string the statement could be contradicting is held by a
        // drone in the other one. There is nothing in its own line to contradict, so no event is
        // stated and nothing caps: both marks run to their region end. The half-measure would cut
        // both to two and one — this fixture is the pre-change law's own figure, and those were
        // its numbers.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     noteBeat(quarter, 5, 5),
                     noteBeat(quarter, 3, 3)},
                    {noteBeat(whole, 3, 5)}
                }
            });
        score.tracks[0].bars.push_back(restBar());

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const drone = noteOnChartString(chart.notes, 6);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(drone != nullptr);
        CHECK(first->sustain == Fraction{4});
        CHECK(second->sustain == Fraction{3});
        CHECK_FALSE(anyNoteContains(built->notes, "clipped"));
        CHECK(built->let_ring_clip.rings == 0);
        // The drone is still bounded where the other voice restrikes its string: the clamp keeps
        // its cross-voice reach even where the cut has lost its own.
        CHECK(drone->sustain == Fraction{2});
    }
}

} // namespace rock_hero::editor::core
