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

// Returns the text with the first occurrence of a marker replaced, failing the test when the
// marker is absent — a variant built on a marker the fixture no longer spells would otherwise
// assert against the unmodified fixture and pass for the wrong reason.
[[nodiscard]] std::string replaceOnce(
    std::string text, const std::string& marker, const std::string& replacement)
{
    const std::size_t position = text.find(marker);
    REQUIRE(position != std::string::npos);
    text.replace(position, marker.size(), replacement);
    return text;
}

// Returns the fixture gpif with the first occurrence of a marker replaced, for score variants.
[[nodiscard]] std::string fixtureWithReplacement(
    const std::string& marker, const std::string& replacement)
{
    return replaceOnce(std::string{g_fixture_gpif}, marker, replacement);
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
    // NO FURNITURE: these assertions are about the importer's RINGS through the rule set, and the
    // span-scoped tail law is tested on its own in common/core. Handing the spans in would make
    // every one of them a test of the law as well.
    return common::core::presentedChartNotes(
               common::core::chartConnections(chart.notes, tempo_map),
               common::core::ChartShapes{},
               tempo_map)
        .notes;
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
        .claim_shapes = std::move(resolutions.claim_shapes),
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

// A Guitar Pro file STATES how it wants its tuning spelled, and the import honours that statement
// and nothing else: a score tuned down a half step is written with flats, so its lowest string
// reads Eb2 rather than D#2. Pitches alone cannot answer the question — the two spellings name the
// same MIDI numbers — so a file that says nothing keeps the ordinary sharp spelling, and no preset
// name or key signature is ever consulted to guess one. The single exception is a Guitar Pro 7.0.0
// file, which stamped the mark onto every staff it wrote and therefore states no preference by it.
TEST_CASE("Guitar Pro import spells the tuning the way the score asks", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_tuning_spelling_test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);

    // The fixture's staff tuning swapped for the given pitches, with any extra staff properties
    // appended beside it.
    const auto tuned_fixture = [](const std::string& pitches, const std::string& extra_properties) {
        return fixtureWithReplacement(
            "<Property name=\"Tuning\"><Pitches>40 45 50 55 59 64</Pitches></Property>",
            "<Property name=\"Tuning\"><Pitches>" + pitches + "</Pitches></Property>" +
                extra_properties);
    };
    // Half a step below the fixture's E standard, which puts every one of the six strings on a
    // black key: the two spellings then disagree about all six names rather than about none.
    const std::string black_key_pitches = "39 44 49 54 58 63";
    const std::vector<std::string> sharp_names{"D#2", "G#2", "C#3", "F#3", "A#3", "D#4"};
    const std::vector<std::string> flat_names{"Eb2", "Ab2", "Db3", "Gb3", "Bb3", "Eb4"};
    const std::string flat_property = "<Property name=\"TuningFlat\"><Enable/></Property>";

    const auto imported_tuning = [&](const std::string& gpif) {
        const std::filesystem::path archive = writeFixtureArchive(scratch, gpif);
        GpSongImporter importer;
        const auto song = importer.importSong(archive, workspace);
        REQUIRE(song.has_value());
        return requiredChart(song->arrangements.front()).tuning.strings;
    };

    SECTION("a staff asking for flats spells its black-key strings with flats")
    {
        CHECK(imported_tuning(tuned_fixture(black_key_pitches, flat_property)) == flat_names);
    }

    SECTION("the same staff without the mark keeps the sharp spelling")
    {
        CHECK(imported_tuning(tuned_fixture(black_key_pitches, "")) == sharp_names);
    }

    SECTION("a version-7 score's mark is not a preference and is ignored")
    {
        // Guitar Pro 7.0.0 wrote the mark on every staff regardless of what its author asked for,
        // so honouring it there would spell every black-key string flat across a whole generation
        // of files.
        const std::string version_7_gpif = replaceOnce(
            tuned_fixture(black_key_pitches, flat_property),
            "<GPVersion>8.1.4</GPVersion>",
            "<GPVersion>7.0.0</GPVersion>");
        CHECK(imported_tuning(version_7_gpif) == sharp_names);
    }

    SECTION("a tuning of naturals reads the same either way")
    {
        // The fixture's own E standard has no black key to respell, so the mark changes nothing:
        // the preference is a spelling of accidentals, not a transformation of every name.
        const std::string natural_pitches = "40 45 50 55 59 64";
        const std::vector<std::string> natural_names{"E2", "A2", "D3", "G3", "B3", "E4"};
        CHECK(imported_tuning(tuned_fixture(natural_pitches, "")) == natural_names);
        CHECK(imported_tuning(tuned_fixture(natural_pitches, flat_property)) == natural_names);

        // C and F are the two naturals the E-standard fixture never reaches; pinning them here
        // means every shared entry of the paired name tables is guarded against drift.
        const std::string c_f_pitches = "36 41 50 55 59 60";
        const std::vector<std::string> c_f_names{"C2", "F2", "D3", "G3", "B3", "C4"};
        CHECK(imported_tuning(tuned_fixture(c_f_pitches, "")) == c_f_names);
        CHECK(imported_tuning(tuned_fixture(c_f_pitches, flat_property)) == c_f_names);
    }

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
    // landing successor at the landing and that chord MERGES into it (rule 11's corollary 2:
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
    CHECK(derived.shapes[1].landing_opened);
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

    // Both strums merge into one span from 1:1 to the closing fret-7 onset at 1:2+1/2, which is
    // also where the eighth strum's own ring ends — the musical close, with rule 12a's margin taken
    // off it at the projection and not here (user ruling 2026-09-04).
    REQUIRE(derived.shapes.size() == 1);
    CHECK(derived.shapes.front().position == GridPosition{.measure = 1, .beat = 1});
    CHECK(derived.shapes.front().sustain == Fraction{3, 2});
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
// halves what the beat states, let ring lengthens it to what the string actually sounds. THE
// LET-RING FIGURE LAW (user signing 2026-09-04, re-signed the same day after a sighting) says how
// far, in one breath: a marked ring runs to its figure's ANCHOR, floored at the WRITTEN duration
// and bounded by the same-string clamp, which is physics and the one bound this pass never
// restates.
//
// THE ANCHOR is the first onset the figure's OWN VOICE states strictly after its LAST marked note.
// The mark is the transcriber asking material to ring on, so the ring runs exactly as far as the
// asking does: material past the marked run never asked for it. Where that voice states nothing
// more, the first onset anywhere in the track answers; where nothing follows at all, the latest
// written end among the figure's marked members does; and unconditionally no further than one
// ORIGIN-BAR metric length past that last marked onset, which is the audibility cap.
//
// THE SEAM NEVER ENTERS THAT ARITHMETIC. A grip contradiction still closes one figure and founds
// the next (the seam's own test case is at the bottom of this file), but its whole job for the
// tails is GROUPING THE MARKS — which stack a mark belongs to, and therefore whose last mark the
// anchor is measured from. A seam is itself an own-voice onset past the marks, so it always sits
// at or after the anchor and can never bind.
//
// RESTS ARE INVISIBLE to every arm of it — a marked ring sails straight through a written silence —
// and so is the neighbourhood of the marks: the walk reads every onset the voice states, marked or
// not, so there is no "marked region" for a figure to be the inside of. Both readings are deleted
// mechanisms, and the sections below pin their absence rather than assuming it.
TEST_CASE("Guitar Pro import rings a let-ring note on to what sounds", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    // One quarter note in the 4/4 fixtures, which is one signature beat of stored ring.
    constexpr Fraction quarter{1, 4};
    constexpr Fraction eighth{1, 8};

    SECTION("the anchor stops a lone mark at the next onset, before seam or clamp can bind")
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
        // THE ANCHOR, and here it binds before either the seam or the clamp gets to speak. The
        // lone mark is on beat one, so the first onset its own voice states after it is beat two
        // — that is the whole derivation, and the ring is the one beat up to it, which is exactly
        // the written quarter. Beat three's fret three does contradict the grip's five and closes
        // the figure there, but the seam only decides that no LATER mark joins this stack; it
        // never reaches the arithmetic. Two is a tail read off that seam, and is also what the
        // same-string clamp at the beat-three restrike would have allowed; four is the audibility
        // cap.
        CHECK(marked->sustain == Fraction{1});
    }

    SECTION("a written rest is invisible: the ring sails through it")
    {
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     restBeat(quarter),
                     noteBeat(quarter, 7, 5),
                     noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        // The rest on beat two states nothing any arm of the law reads — THE REST SCAN IS DELETED
        // — so the first onset the mark's own voice states after it is the fresh string on beat
        // three, and THE ANCHOR hands it that instant. One beat is what the retired rest arm gave,
        // stopping the ring at the silence; four is the cap this figure never reaches.
        CHECK(marked->sustain == Fraction{2});
        CHECK(
            anyNoteContains(built->notes, "1 let-ring rings were extended to their figure's end"));
    }

    SECTION("the anchor stops the figure at the very next onset its voice states")
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
        // Nothing here contradicts the grip — beat two grows the melody's string and every beat
        // after it restates the same fret — so this is one figure throughout, and THE ANCHOR ends
        // its lone mark at the first onset the voice states after it: beat two, a beat later. The
        // written quarter is the floor it lands on, so the mark changes nothing; four is the
        // audibility cap, which a ceiling nothing states under would have handed it.
        CHECK(marked->sustain == Fraction{1});
        CHECK_FALSE(anyNoteContains(built->notes, "extended to their figure's end"));
    }

    SECTION("the cap crosses the barline and lands mid-beat")
    {
        // The mark sits half a beat into the bar, so its own measure-duration runs out half a beat
        // into the NEXT bar — the sliding cap the rule states, not a truncation at the barline.
        // RE-ANCHORED SO THE CAP IS THE BINDING BOUND: the next onset this voice states sits at
        // bar two's third beat, well past the cap — so the anchor asks for beat seven and the cap
        // answers first. The eighth rest ahead of the mark is what puts it on the half beat, and
        // it states nothing the law reads.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{restBeat(eighth), letRingBeat(eighth, 5)}}});
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     noteBeat(quarter, 7, 5),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->position.measure == 1);
        CHECK(marked->position.beat == 1);
        CHECK(marked->position.offset == Fraction{1, 2});
        // THE SLID CAP: the mark's onset half a beat into bar one plus one measure-duration lands
        // four and a half beats in — measure two, beat one, half a beat PAST the barline — so the
        // ring is the four beats up to it. A cap truncated at the barline stops it at three and a
        // half; the uncapped anchor at bar two's third beat gives five and a half. Only a whole
        // measure-duration carried from the mark's own onset lands on the half beat.
        CHECK(marked->sustain == Fraction{4});
    }

    SECTION("the cap is the ORIGIN bar's length under a meter change")
    {
        // A 4/4 bar into two 6/8 bars: the cap is a measure-DURATION — a whole note — and not the
        // origin bar's count of four beats, so it reaches further into the shorter bars than any
        // beat count does. RE-ANCHORED SO THE CAP IS THE BINDING BOUND, exactly as its sibling
        // above: the next onset this voice states is bar three's downbeat, two whole bars away, so
        // the anchor asks for far more than the cap gives.
        GpScore score = makeLinearScore(3, syncs);
        score.master_bars[1] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.master_bars[2] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 5),
                     restBeat(quarter)}
                }
            });
        std::vector<GpBeat> second;
        second.reserve(6);
        for (int step = 0; step < 6; ++step)
        {
            second.push_back(restBeat(eighth));
        }
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(second)}});
        std::vector<GpBeat> third{noteBeat(eighth, 7, 5)};
        for (int step = 0; step < 5; ++step)
        {
            third.push_back(restBeat(eighth));
        }
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(third)}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->position.measure == 1);
        CHECK(marked->position.beat == 3);
        // THE LENGTH, not the count: the mark sits on the 4/4 bar's third beat, so its cap is a
        // whole note later — four eighth-beats into the first 6/8 bar, the eighth global beat —
        // and the ring is the SIX beats up to it. Both rival readings land on four instead: four
        // beats counted from the mark, and the 6/8 destination bar's own three-quarter length,
        // each stopping two eighth-beats into bar two. Eight is the uncapped anchor, which would
        // run to bar three's downbeat.
        CHECK(marked->sustain == Fraction{6});
    }

    SECTION("dead, palm-muted and staccato notes are never extended")
    {
        // Guitar Pro's playback returns on each of these before it ever reads the let-ring mark,
        // so all three keep exactly the ring their own mark gives them — the staccato member's
        // being the halved one, which the extension must not undo. The chord's fourth member is
        // the one live mark, and its voice states its next onset on beat two (a fresh string the
        // beats after it restate), so THE ANCHOR ends it there and it lands back on its own
        // written quarter. The staccato half is what still separates a pre-empted
        // ring from an unpre-empted one here; the muted pair reads the same length as the plain
        // member.
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
        CHECK(chart.notes[3].sustain == Fraction{1});
        // Nothing lengthened, so there is no conversion to report: the anchor left the one
        // unpre-empted ring at its written length and the notice disappears with the count.
        CHECK_FALSE(anyNoteContains(built->notes, "extended to their figure's end"));
    }

    SECTION("a note that absorbed a TIE keeps a merged ring LONGER than the figure's end")
    {
        // Rule 1 of the baseline law: ties combine into a single note at its true written
        // duration, and the figure law then LENGTHENS ONLY — written is its floor — so a merged
        // ring already reaching past the figure's end simply stands. The anchor puts that end on
        // beat two, one beat in, while the merged chain runs eight beats; the sibling section
        // below is the other side of the same floor, where the figure end sits past the merged end
        // and the mark extends like any other.
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

    SECTION("a tie-merged marked note extends past its merged end like any other")
    {
        // The other side of that floor, and the deleted exemption's own figure: a marked tie
        // origin whose continuation merges into one two-beat note, under a figure whose end sits
        // a beat past the merged end. The exemption held the merged ring at two; the law extends
        // it like every other marked note, because the reference's tie-end cap is its walk's
        // tie-BLIND string lookup, not a statement about the sound — Guitar Pro audibly rings tied
        // let-ring notes past the written duration (user-verified by ear 2026-09-01).
        //
        // The beat-three REST is why the figure reaches beat four at all: the anchor asks for the
        // first onset the mark's own voice states after it, and a rest states none.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat tied_origin = letRingBeat(quarter, 5);
        tied_origin.notes.front().tie_origin = true;
        GpBeat tied_continuation = noteBeat(quarter, 5);
        tied_continuation.notes.front().tie_destination = true;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {tied_origin, tied_continuation, restBeat(quarter), noteBeat(quarter, 7, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        // Three beats, the figure's end on beat four; two is the merged written ring the deleted
        // exemption froze it at.
        CHECK(marked->sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "1 let-ring rings were extended to their figure's end"));
        CHECK_FALSE(anyNoteContains(built->notes, "kept their shipped rings"));
    }

    SECTION("a note that absorbed a LEGATO SLIDE extends like any other marked note")
    {
        // The legato-slide landing merges into the origin as a keyframe, and the merged note then
        // extends to the figure's end exactly as a tie-merged one does: the merge states the
        // WRITTEN duration, never a cap on the mark. The exemption that held this chain at its two
        // merged beats is deleted — its "the walk would collapse to the merged end anyway"
        // justification measured false corpus-wide (674 of 697 exempt rings had figure ends past
        // their merged end).
        //
        // The beat-three REST is what leaves the figure an end past the merged one: the landing
        // beat is swallowed by the merge, so the first onset the voice states after the mark is
        // the fresh string on beat four.
        GpScore score = makeLinearScore(1, syncs);
        // Slide flag 2 is the legato slide: the landing continues the same sounding note.
        GpBeat origin = letRingBeat(quarter, 5);
        origin.notes.front().slide_flags = 2;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {origin, noteBeat(quarter, 7), restBeat(quarter), noteBeat(quarter, 9, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // The landing merged away, so the string carries one note — two merged beats extended to
        // the figure's end on beat four. The fresh string there grows the grip and contradicts
        // nothing, so no seam intervenes.
        REQUIRE(chart.notes.size() == 2);
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "1 let-ring rings were extended to their figure's end"));
        CHECK_FALSE(anyNoteContains(built->notes, "kept their shipped rings"));
    }

    SECTION("a slide-out marked note keeps its shipped ring")
    {
        // The one exemption that stands: an unpitched slide-out states the ring's own end (LAW I
        // — the release IS the end, physically forced), so the mark extends nothing and the law's
        // REACH is stated rather than assumed.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat slid = letRingBeat(quarter, 5);
        slid.notes.front().slide_flags = 4;
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {slid,
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
        CHECK_FALSE(anyNoteContains(built->notes, "extended to their figure's end"));
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
        // Every ring the extension lengthened is a conversion the reader should be able to find,
        // and the count is per RING rather than per figure: one marked chord beat, two members,
        // two rings reported. The two rests are what leave the figure an end to reach — the
        // anchor takes the first onset the voice states after the marks, and that is the fresh
        // string on beat four — so both members ring the three beats up to it instead of
        // landing back on their written quarter and leaving nothing to count.
        GpScore score = makeLinearScore(1, syncs);
        GpBeat chord;
        chord.duration_whole = quarter;
        chord.notes = {
            GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
            GpNote{.string = 1, .fret = 5, .let_ring = true, .harmonic_type = ""}
        };
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {{chord, restBeat(quarter), restBeat(quarter), noteBeat(quarter, 7, 5)}}
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        REQUIRE(chart.notes.size() == 3);
        // Both extensions survive the clamp — neither marked string is ever restruck — so both
        // are still longer than what they replaced when the report is written, which is what
        // makes the count TWO.
        CHECK(chart.notes[0].sustain == Fraction{3});
        CHECK(chart.notes[1].sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "2 let-ring rings were extended to their figure's end"));
    }

    SECTION("a ring the clamp takes straight back is not reported as a conversion")
    {
        // THE REPORT IS WRITTEN AFTER THE CLAMP, which is the whole of this section: an extension
        // physics takes back is not a conversion the reader could find in the chart, and a note
        // they cannot find is worse than no note. The figure walk reads ONE voice, so it never
        // sees the second voice re-striking the first mark's string on beat two; the chart's own
        // cross-voice clamp does, and it puts that ring back exactly where it stood.
        //
        // TWO marks, because the anchor is measured from the figure's LAST one: the beat-three
        // mark carries the figure's end out to bar two's downbeat, which is what leaves the
        // beat-one mark an extension for the clamp to take back.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5),
                     restBeat(quarter),
                     letRingBeat(quarter, 7, 1),
                     restBeat(quarter)},
                    {restBeat(quarter), noteBeat(quarter, 3), restBeat(quarter), restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 9, 5)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const clamped = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const kept = noteOnChartString(chart.notes, 2);
        REQUIRE(clamped != nullptr);
        REQUIRE(kept != nullptr);
        // Both marks were lengthened to the figure's end on bar two's downbeat — four beats and
        // two. The clamp then takes the first one back to exactly its written quarter, so only
        // the second is a conversion the chart still shows: the count is ONE, never two.
        CHECK(clamped->sustain == Fraction{1});
        CHECK(kept->sustain == Fraction{2});
        CHECK(
            anyNoteContains(built->notes, "1 let-ring rings were extended to their figure's end"));
        CHECK_FALSE(anyNoteContains(built->notes, "2 let-ring rings"));
    }

    SECTION("a following grace steals from the ring, but the bend keeps its destination")
    {
        // Rule 17's steal takes the ornament's lead out of this beat, and the figure law then
        // out-rings it. What must not happen anywhere in that order is the truth loss this pins:
        // the bend was once clipped to the STOLEN ring the instant it was mapped, so the
        // destination — which Guitar Pro states as a percentage of the NOTATED quarter and which
        // therefore sits past the stolen end — was gone by the time the ring came back. The trim
        // runs ONCE, after every pass that can lengthen a ring.
        //
        // THE FIGURE HAS TO REACH PAST THE ORNAMENT for that to be observable, so the beat the
        // grace ornaments carries a mark of its own: the anchor is measured from the figure's LAST
        // mark, which puts the end on beat three. The grace's own fret still joins the grammar —
        // it grows the melody's string at fret nine — and every beat after it restates that fret,
        // so nothing contradicts and no seam intervenes.
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
                     letRingBeat(quarter, 9, 5),
                     noteBeat(quarter, 9, 5),
                     noteBeat(quarter, 9, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::ChartNote* const marked =
            noteOnChartString(built->arrangements.front().chart.notes, 1);
        REQUIRE(marked != nullptr);
        // Two beats, from the onset to the figure's end on beat three; seven eighths is the
        // stolen written ring the mark lengthened away from.
        CHECK(marked->sustain == Fraction{2});
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
        // The `founding == Accumulation` assertion that stood here is DELETED WITH ITS SUBJECT
        // (grip-tenure law, user-signed 2026-09-04): `SpanFounding` is gone and there is no
        // founding classification to read. Accumulation survives as a RULE — sound alone opens a
        // span at three or more overlapping members — and the roll is still exactly that figure,
        // which the surviving verdicts (one span, no claim, the whole ring, arpeggio) already say.
        // The plan's gate row for this family is "roll figures: unchanged".

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
            R"(<Beat id="0"><Rhythm ref="0"/><Notes>0</Notes></Beat>)",
            "<Beat id=\"0\"><Rhythm ref=\"0\"/><Arpeggio>Up</Arpeggio><Notes>0</Notes>\n"
            "<XProperties>\n" +
                roll_properties + extra_properties + "</XProperties></Beat>");
    };
    // The first beat of the first track's first bar, which every section below reads.
    const auto rolled_beat = [](const GpScore& score) -> const GpBeat& {
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
            const GpBeat& beat = rolled_beat(*score);
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
            const GpBeat& beat = rolled_beat(*score);
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
            const GpBeat& beat = rolled_beat(*score);
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
            const GpBeat& beat = rolled_beat(*score);
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

// A backing track in a format this build cannot decode — AAC/.m4a on Windows and Linux — refuses
// LOUDLY before anything is staged, naming the extension in the user's language rather than a
// jargon transcode failure over an already-deleted path (the sighted 2026-09-02 defect; the plan
// to decode it for real is docs/plans/todo/m4a-audio-decode.md). The entry's bytes never matter:
// the refusal fires before any decode is attempted. On Apple the same archive takes the OTHER
// branch by design — CoreAudioFormat reads m4a, so the garbage bytes fail at the transcode
// instead, which is exactly the correct behaviour where a real decoder exists.
TEST_CASE(
    "Guitar Pro import refuses an undecodable backing-audio format loudly", "[core][gp-import]")
{
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "rh_gp_import_m4a_refusal";
    std::error_code cleanup_error;
    std::filesystem::remove_all(scratch, cleanup_error);
    std::filesystem::create_directories(scratch);

    const std::string gpif =
        fixtureWithReplacement("Content/Assets/audio.wav", "Content/Assets/audio.m4a");
    const std::filesystem::path content = scratch / "gp_content";
    std::filesystem::create_directories(content / "Content" / "Assets");
    {
        std::ofstream file{content / "Content" / "score.gpif", std::ios::binary};
        file << gpif;
    }
    {
        std::ofstream audio{content / "Content" / "Assets" / "audio.m4a", std::ios::binary};
        audio << "not real AAC data, and it never needs to be";
    }
    const std::filesystem::path archive = scratch / "fixture.gp";
    REQUIRE(common::core::writeWorkspaceToArchive(content, archive).has_value());

    const std::filesystem::path workspace = scratch / "song";
    std::filesystem::create_directories(workspace);
    GpSongImporter importer;
    const auto imported = importer.importSong(archive, workspace);
    REQUIRE_FALSE(imported.has_value());
    CHECK(imported.error().code == SongImportErrorCode::InvalidImportedSong);
    // The extension is named either way; the refusal's own wording discriminates this fix from
    // the pre-fix transcode failure, whose message also mentioned .m4a.
    CHECK(imported.error().message.find(".m4a") != std::string::npos);
#if !defined(__APPLE__)
    CHECK(imported.error().message.find("cannot decode on this platform") != std::string::npos);
    // Nothing was staged: the refusal precedes every workspace write on the audio path.
    CHECK_FALSE(std::filesystem::exists(workspace / "audio" / "backing_source.m4a"));
#endif

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

// THE FIGURE HAS ONE END (user signing 2026-09-04, the simple law; re-signed the same day when the
// sighting walk anchored the tails at the marked run). A let-ring passage is a TEXTURE: the notes
// stack up and the whole stack stops together, so a marked note's notated duration is a DEMAND
// rather than a length and the length is the FIGURE's. Every mark in a figure takes that one end —
// THE ANCHOR, the first onset the figure's own voice states after its LAST mark — and the figure
// is delimited by the GRIP, not by which beats wear the mark, which is what the deleted "marked
// region" read. What separates the two is an unmarked beat sitting inside a marked passage: it
// ends a region and it does not end a figure, and the section that used to pin the first now pins
// the second.
//
// THE ANCHOR IS UNIVERSAL. It is not an arm that fires only where nothing contradicts: every
// figure's tails are measured from its last mark, and a seam's whole contribution is deciding
// which marks that "last" is chosen among.
TEST_CASE("Guitar Pro import normalizes a let-ring figure to one end", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};

    SECTION("THE FIGURE'S ONE END: whatever bounds the last mark bounds every member")
    {
        // Three marks stacking up on three strings, then an unmarked beat and a plain bar. Nothing
        // contradicts the grip — every onset either grows a fresh string or restates the fret it
        // already holds — so this is one figure throughout, and THE ANCHOR ends it at the first
        // onset its voice states after the LAST mark: the fret-3 strike on beat four. The
        // audibility cap, one measure-duration from that same last mark, would have allowed beat
        // seven, so the anchor is what binds here and every member stops together at it.
        //
        // THE DISCRIMINATION, and the whole of what the one-end reading changed: under a per-note
        // walk each mark answered with its OWN bound, so all three rang four beats and stopped one
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
        // Every member ends at beat four, the anchor's instant: three beats from the first, two
        // from the second, and the last mark on its written quarter — ONE end, not three. Six and
        // five are what the interior members reach when each answers with its own audibility cap,
        // a reading that leaves them ringing PAST the mark they were stacked under and rebuilds
        // the staircase upside down; the figure scope is what puts the stack on one instant.
        CHECK(first->sustain == Fraction{3});
        CHECK(second->sustain == Fraction{2});
        CHECK(tail->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "2 let-ring rings were extended to their figure's end"));
    }

    SECTION("THE CAP'S OWN PIN: a quiet track ends the figure one origin bar past its last mark")
    {
        // The figure above with the track SILENCED after it: bar one's fourth beat and the whole
        // of bar two are rests, and the next onset the track sounds is bar three's downbeat, eight
        // beats past the last mark. The anchor asks for that onset and THE AUDIBILITY CAP answers
        // first — one measure-duration from the LAST MARK's onset on beat three, which is beat
        // seven.
        //
        // This section exists because the cap governs exactly one shape, and this is it: every
        // other figure here reaches its anchor first. A live bound no
        // figure pins is a bound the next reader deletes as dead, so the figure rule's own case
        // pins that the cap bounds a whole figure, while the sliding-cap sections in "rings a
        // let-ring note on to what sounds" pin the other half — what the cap MEASURES.
        GpScore score = makeLinearScore(3, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter), restBeat(quarter), restBeat(quarter), restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 3, 4)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const tail = noteOnChartString(chart.notes, 3);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(tail != nullptr);
        // Beat seven for the whole figure: six beats from the first member, five from the second
        // and four from the last mark. Eight, seven and six are the uncapped anchor at bar
        // three's downbeat; four, three and two are a cap anchored at the figure's FOUNDING onset
        // instead of its last mark. Only a cap slid to the last mark lands on this triple.
        CHECK(first->sustain == Fraction{6});
        CHECK(second->sustain == Fraction{5});
        CHECK(tail->sustain == Fraction{4});
    }

    SECTION("an unmarked beat does NOT end the figure, so one end still serves both marked runs")
    {
        // THE MARKED REGION IS DELETED, and this is the fixture that used to pin it. An unmarked
        // beat between two marked runs ended a region, which made them two passages with two ends;
        // the figure walk reads every onset the voice states and asks only what the grip says, and
        // the fret-3 stab on beat two grows a fresh string like any other onset. So this is ONE
        // figure, its last mark is on beat four, and the anchor gives the whole of it the bar-two
        // strike on beat five.
        //
        // Four beats for the lone mark is exactly the reach the region reading refused, and is now
        // the law: it stops with the run it shares a grip with. One is what the region reading
        // gave it, and it is the value this figure now discriminates against.
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
        // The other two members stop on that same beat five: two beats from the beat-three mark
        // and the written quarter from the beat-four one. All three quit together, which is what
        // ONE end means.
        CHECK(early->sustain == Fraction{2});
        CHECK(late->sustain == Fraction{1});
    }

    SECTION("the normalized figure derives as ONE accumulation span over the whole texture")
    {
        // The point of the rule, read back by the derivation that knows nothing about let ring:
        // the marked members stack into ONE statement dated from the first note rather than three
        // staircase spans — which is the figure the whole ruling grew from. The anchor moves where
        // that statement closes without breaking it up: the whole figure takes the anchor's
        // instant, so the three members quit together on beat four instead of running to the
        // audibility cap three beats further out.
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
        // PINNED EXACTLY, where a `>= 1` stood (2026-09-04). The old form leaned on the
        // `founding == Accumulation` assertion below it to carry the section's subject, and that
        // assertion is DELETED WITH ITS SUBJECT — `SpanFounding` is gone and there is no founding
        // classification to read. With the founding gone the COUNT is the only thing left that can
        // say "the texture is ONE span", so it says it exactly. ONE, and the whole song holds no
        // other: the figure's one end stops all three marks on beat four together, and past that
        // instant nothing ever sounds two members at once — the unmarked stab is alone and the
        // plain bar is one string struck four times — so rule 10 never opens a second span. Three
        // is the staircase this section exists to refuse, and three of another shape is what a
        // per-mark bound makes when each member quits under its own cap.
        REQUIRE(derived.shapes.size() == 1);
        CHECK(derived.shapes[0].position == GridPosition{.measure = 1, .beat = 1});
        // THE TEXTURE'S CLOSE, which the grip-tenure law's member-quit arm decides: the bracket
        // ends where its shortest member's audible life does, and under the figure's one end every
        // member's life ends at the same instant — beat four, where the unmarked fret-3 beat
        // strikes and the anchor stops the figure. Three beats, not the four the last mark's
        // own written quarter gives when it alone quits there, and never the interior members'
        // six.
        CHECK(derived.shapes[0].sustain == Fraction{3});
        CHECK(shapeArrivalsOf(chart, built->tempo_map)[0]);
        REQUIRE(derived.shapes[0].posture < derived.postures.size());
        // Three stops, one per mark: the figure's rings die exactly AT the unmarked beat, so that
        // beat crosses no slot the texture still holds and states nothing into this posture. Three
        // stops with a three-beat close is also what keeps the count above honest: ONE span here
        // is the texture, not the derivation swallowing the song whole.
        CHECK(
            heldFrets(derived.postures[derived.shapes[0].posture]) ==
            std::vector<std::optional<int>>{5, 7, 9});
    }
}

// THE SEAM (user signing 2026-09-04, the simple law; re-signed the same day when the sighting walk
// deleted the anacrusis and anchored the tails at the marked run). A let-ring figure is CLOSED
// where its GRIP is contradicted, and the grip is FIGURE-SCOPED MEMORY rather than sound: each
// voice accumulates the stop stated on each string SINCE THE FIGURE BEGAN, a first-time string
// growing it and a same-stop statement confirming it, with nothing else touching it. An onset
// stating a DIFFERENT stop on a gripped string closes the figure and founds the next AT ITSELF —
// exactly there, with no step back over anything. THERE IS NO RETREAT MECHANISM IN THE LAW.
//
// WHAT THE SEAM IS FOR, now that no tail reads one: GROUPING THE MARKS. It decides which marks
// share a figure, and therefore whose last mark each stack's tails are measured from — THE ANCHOR,
// the first onset the figure's own voice states after that last mark. A seam is itself an own-voice
// onset past the figure's marks, so it always sits at or after the anchor and can never bind a
// ring; a stack that happens to stop exactly at its seam is a stack whose seam was the very next
// onset. The sections below pin both halves: the anchor beating the seam wherever unmarked material
// sits between them, and the grouping the seam still does.
//
// The grip is PER VOICE (user ruling 2026-09-01, "events should not cut rings in another voice"):
// grammar takes the voice, and only the same-string clamp — physics — crosses the boundary. The
// anchor is voice-scoped for the same reason, so another line's onsets never shorten a marked ring.
// The walk reads onsets and statements only, never a ring, so both are pure functions of the
// written stream: no sounding test, no staleness bound, no strike-order exemption anywhere in it.
// Both halves of every comparison read through the one statement authority, so a slid finger
// carries its statement forward instead of manufacturing a contradiction.
//
// Each section below kills one measured rival reading, and several kill a mechanism a predecessor
// law shipped: the sound-scoped grip, its staleness bound, the self-exclusion that spared a cutting
// note, and the anacrusis step-back are all gone — the first three subsumed by figure membership,
// the last by the anchor.
TEST_CASE("Guitar Pro import seams a let-ring figure at a grip contradiction", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};
    // A beat sounding several strings at once: the shared fixtures build one note per beat, and
    // the grip figures here need marks struck WITH a drone on the string a statement then moves.
    const auto chord_beat_of = [](const Fraction duration, const std::vector<GpNote>& sounded) {
        GpBeat beat;
        beat.duration_whole = duration;
        beat.notes = sounded;
        return beat;
    };

    SECTION("every marked tail in the figure stops at ONE instant")
    {
        // Two marks and an unmarked drone open the figure, a third mark joins on beat two, beat
        // three restates the drone's own fret, and beat four states fret three where the grip
        // holds five — a contradiction that closes the figure there. EVERY marked tail in it stops
        // at one instant, not just the contradicted string's, which is what "the figure has one
        // end" means.
        //
        // WHICH instant is the anchor's answer, not the seam's: the first onset this voice states
        // after the figure's last mark is the beat-three restatement, a beat BEFORE the seam. The
        // marked run ended on beat two, and the unmarked chug after it never asked to ring under
        // anything. Bar two restrikes the second mark's string two beats past the anchor, so that
        // tail's same-string clamp is far later and the anchor is demonstrably what bounds it.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     letRingBeat(quarter, 9, 2),
                     noteBeat(quarter, 3, 5),
                     noteBeat(quarter, 3, 0)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 7, 1)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const third = noteOnChartString(chart.notes, 3);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(third != nullptr);
        // The anchor is on beat three and all three marked rings end exactly there: two beats from
        // the pair struck on beat one, and the mark struck on beat two back on its written quarter.
        // Three, three and two are those same tails read off the SEAM a beat later, which is the
        // reading the sighting killed; the second mark's own string is not restruck until bar two's
        // downbeat, so its clamp would allow four and it takes two. Only co-termination at the
        // anchor produces this triple. TWO conversions, not three: the beat-two mark ends where its
        // own quarter already ended, so nothing was lengthened for it to report.
        CHECK(first->sustain == Fraction{2});
        CHECK(second->sustain == Fraction{2});
        CHECK(third->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "2 let-ring rings were extended to their figure's end"));
    }

    SECTION("a same-fret restatement never seams: the chug rides through")
    {
        // A statement restating the fret the grip already holds CONFIRMS it, and confirmation is
        // not a new grip — so a chug on a held string can never split a figure. Here the drone's
        // string is restated at its own fret three on beat three, between two marks and a third,
        // and the figure runs on through it to the anchor at bar two's downbeat. That restatement
        // sits BEFORE the figure's last mark, so it bounds nothing either.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     letRingBeat(quarter, 7, 1),
                     noteBeat(quarter, 3, 5),
                     letRingBeat(quarter, 9, 2)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 11, 3)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const third = noteOnChartString(chart.notes, 3);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(third != nullptr);
        // Bar two's downbeat is the figure's one end, so the marks ring four, three and their own
        // written quarter to it. Two and one are what the first two marks take if the beat-three
        // restatement seams — which is the whole of what a confirmation must not do.
        CHECK(first->sustain == Fraction{4});
        CHECK(second->sustain == Fraction{3});
        CHECK(third->sustain == Fraction{1});
    }

    SECTION("a full repetition of the figure stays ONE figure with one end")
    {
        // The repetition invariant, and under this law it is STRUCTURAL rather than satisfied:
        // with confirmation the only thing a restatement can do, a figure stated twice cannot be
        // divided by its own second statement. Two marked strings, then the same two marked again
        // at the same frets, then bar two's downbeat as the figure's anchoring onset.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 3, 3)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Onset order, which is (beat, string): the two opening marks, then the two repeating
        // them, then bar two's strike. Stated as guards rather than assumed, so an index cannot
        // silently come to mean another note.
        REQUIRE(chart.notes.size() == 5);
        REQUIRE(globalBeatOf(chart.notes[0]) == Fraction{0});
        REQUIRE(chart.notes[0].string == 1);
        REQUIRE(globalBeatOf(chart.notes[1]) == Fraction{1});
        REQUIRE(chart.notes[1].string == 2);
        REQUIRE(globalBeatOf(chart.notes[2]) == Fraction{2});
        REQUIRE(chart.notes[2].string == 1);
        REQUIRE(globalBeatOf(chart.notes[3]) == Fraction{3});
        REQUIRE(chart.notes[3].string == 2);
        // The whole of the discrimination is the SECOND mark. One figure ends on bar two's
        // downbeat, so its ring is three beats and its own string's restrike on beat four clamps
        // it to two; a figure seamed by the repetition would end on beat three and leave it at its
        // written one. The other three land on the same numbers either way — the first mark is
        // clamped by its restrike, and the repeating pair sits inside whichever figure it opens.
        CHECK(chart.notes[1].sustain == Fraction{2});
        CHECK(chart.notes[0].sustain == Fraction{2});
        CHECK(chart.notes[2].sustain == Fraction{2});
        CHECK(chart.notes[3].sustain == Fraction{1});
    }

    SECTION("co-struck notes are judged against the pre-instant grip and never seam each other")
    {
        // A chord contradicting the grip on TWO strings at once states ONE seam, and its own
        // members found the figure on the far side of it: every member of a slot is judged
        // against the grip as it stood BEFORE the instant, and the slot's notes are added to the
        // figure the seam opened. So a contradicting mark is never its own victim — the retired
        // cut needed a self-exclusion rule for exactly this, and figure membership subsumes it.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     noteBeat(quarter, 3, 5),
                     chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 9, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 1, .fret = 10, .let_ring = true, .harmonic_type = ""}}),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{restBeat(quarter), noteBeat(quarter, 3, 3)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        // Onset order is (beat, string): the opening chord's three notes, the beat-two
        // restatement, the contradicting chord's two marks, then bar two's strike. Stated as
        // guards rather than assumed, so an index cannot silently come to mean another note.
        REQUIRE(chart.notes.size() == 7);
        REQUIRE(globalBeatOf(chart.notes[0]) == Fraction{0});
        REQUIRE(chart.notes[0].string == 1);
        REQUIRE(globalBeatOf(chart.notes[1]) == Fraction{0});
        REQUIRE(chart.notes[1].string == 2);
        REQUIRE(globalBeatOf(chart.notes[4]) == Fraction{2});
        REQUIRE(chart.notes[4].string == 1);
        REQUIRE(globalBeatOf(chart.notes[5]) == Fraction{2});
        REQUIRE(chart.notes[5].string == 2);
        // The opening pair stops at its own ANCHOR — the beat-two restatement of the drone's fret,
        // the first onset the voice states after the pair — so each lands back on its written
        // quarter. Two is that pair read off the seam on beat three, which the marked run never
        // asked to reach.
        CHECK(chart.notes[0].sustain == Fraction{1});
        CHECK(chart.notes[1].sustain == Fraction{1});
        // The contradicting pair founds the next figure AT ITSELF and rings on to that figure's own
        // anchor on bar two's second beat — three beats each. One is the written quarter each would
        // keep were a contradicting note clipped by the seam it states.
        CHECK(chart.notes[4].sustain == Fraction{3});
        CHECK(chart.notes[5].sustain == Fraction{3});
    }

    SECTION("unmarked material in the figure bounds the ring: the anchor, not the seam")
    {
        // THE SIGHTED RULING, in its own fixture: a marked drone must not ring into the unmarked
        // chords that follow it in its own line, because material past the marked run never asked
        // to ring. Two marks and a drone open the figure, a third mark joins on beat two, and then
        // TWO unmarked chords follow on beats three and four before bar two's fret nine contradicts
        // the grip's five and seams the figure.
        //
        // The marked run ends on beat two, so the anchor is beat THREE — the FIRST unmarked onset,
        // not the last one before the seam and not the seam itself. Both rivals are live here and
        // both are refused by the numbers below.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     letRingBeat(quarter, 9, 2),
                     chord_beat_of(
                         quarter,
                         {GpNote{.string = 4, .fret = 2, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     chord_beat_of(
                         quarter,
                         {GpNote{.string = 4, .fret = 2, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}})}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 9, 0)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const third = noteOnChartString(chart.notes, 3);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        REQUIRE(third != nullptr);
        // Two, two and the written quarter, every ring ending on beat three. THREE, three and two
        // is the anchor slid to the LAST unmarked onset on beat four; FOUR, four and three is the
        // seam on bar two's downbeat, which is what the tails read before the sighting. The
        // first mark's string is not restruck until that same downbeat, so the clamp would allow
        // four and it takes two — physics is nowhere near this number.
        CHECK(first->sustain == Fraction{2});
        CHECK(second->sustain == Fraction{2});
        CHECK(third->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "2 let-ring rings were extended to their figure's end"));
    }

    SECTION("the seam still groups the marks: a contradiction starts a new marked stack")
    {
        // WHY THE GRIP MACHINERY IS STILL HERE, now that no tail reads a seam. Two marked runs in
        // one voice, separated by a contradiction: a mark and a drone open, a second mark joins on
        // beat two, and beat three moves the drone from fret three to fret ten — closing the figure
        // and founding the next at that instant, where a third mark is struck with it. A fourth
        // mark follows on beat four, and bar two's downbeat is a plain strike.
        //
        // Two stacks, two anchors, measured independently. The FIRST stack's last mark is on beat
        // two, so its anchor is beat three — which here is both the anchor and the seam, since the
        // seam was the very next onset. The SECOND stack's last mark is on beat four, so its anchor
        // is bar two's downbeat.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     letRingBeat(quarter, 7, 1),
                     chord_beat_of(
                         quarter,
                         {GpNote{.string = 2, .fret = 9, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 10, .harmonic_type = ""}}),
                     letRingBeat(quarter, 11, 3)}
                }
            });
        score.tracks[0].bars.push_back(GpBar{.voices = {{noteBeat(quarter, 3, 4)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const opening = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const closing = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const reopening = noteOnChartString(chart.notes, 3);
        const common::core::ChartNote* const last = noteOnChartString(chart.notes, 4);
        REQUIRE(opening != nullptr);
        REQUIRE(closing != nullptr);
        REQUIRE(reopening != nullptr);
        REQUIRE(last != nullptr);
        // The first stack: two beats and one, stopping together on beat three. FOUR and THREE is
        // what one undivided figure gives them — every mark measured from the beat-four mark, whose
        // anchor is bar two's downbeat — and that is exactly the grouping the seam refuses. No
        // string is restruck anywhere in this fixture, so the clamp is silent and the numbers are
        // the law's alone.
        CHECK(opening->sustain == Fraction{2});
        CHECK(closing->sustain == Fraction{1});
        // The second stack, anchored on its own last mark: two beats and one, stopping together on
        // bar two's downbeat.
        CHECK(reopening->sustain == Fraction{2});
        CHECK(last->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "2 let-ring rings were extended to their figure's end"));
    }

    SECTION("a seam never shortens: the merged written ring is the floor")
    {
        // A marked tie origin merging into one two-beat note, with the note's OWN voice sounding
        // again ONE beat in — inside the merged written duration — on a co-struck string it moves.
        // That onset is both the figure's ANCHOR (the first the voice states after its only mark)
        // and its seam, and the law LENGTHENS ONLY: written is its floor, and shortening a ring is
        // the same-string clamp's job and nobody else's. ONE is the whole discrimination — a figure
        // end allowed to shorten cuts the merged ring in half at its own instant — and the merged
        // two beats is the law. The instant is deliberately placed INSIDE the merged duration,
        // which is the only arrangement where the floor is observable at all.
        GpScore score = makeLinearScore(1, syncs);
        const GpBeat origin = chord_beat_of(
            quarter,
            {GpNote{
                 .string = 0, .fret = 5, .tie_origin = true, .let_ring = true, .harmonic_type = ""
             },
             GpNote{.string = 4, .fret = 3, .harmonic_type = ""}});
        const GpBeat continuation = chord_beat_of(
            quarter,
            {GpNote{.string = 0, .fret = 5, .tie_destination = true, .harmonic_type = ""},
             GpNote{.string = 4, .fret = 7, .harmonic_type = ""}});
        score.tracks[0].bars.push_back(
            GpBar{.voices = {{origin, continuation, noteBeat(quarter, 7, 5), restBeat(quarter)}}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        CHECK(marked->sustain == Fraction{2});
        CHECK_FALSE(anyNoteContains(built->notes, "extended to their figure's end"));
    }

    SECTION("a contradiction in ANOTHER voice seams nothing, but its restrike still clamps")
    {
        // THE GRAMMAR IS PER VOICE AND THE CLAMP IS NOT, both read off one number. The marked
        // figure sits in the SECOND voice slot — the walk is per voice, not per first voice —
        // holding a mark and a drone on the melody's string; the first voice then states fret
        // eleven on that drone's string on beat two, and fret seven on the MARK's string on beat
        // three. Neither is grammar this figure can hear, and neither ANCHORS it either: the anchor
        // is the first onset the figure's OWN voice states after the mark, which is its own
        // beat-four statement of fret nine — the same instant that seams it.
        //
        // Physics still crosses the boundary: the beat-three strike is a restrike of the marked
        // string, whichever line wrote it, so the clamp stops the ring there.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     noteBeat(quarter, 11, 5),
                     noteBeat(quarter, 7, 0),
                     restBeat(quarter)},
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     restBeat(quarter),
                     restBeat(quarter),
                     noteBeat(quarter, 9, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = noteOnChartString(chart.notes, 1);
        REQUIRE(marked != nullptr);
        // Two beats, and the number says both halves at once: ONE is a TRACK-scoped anchor,
        // stopping the ring dead at the other voice's beat-two onset; THREE is the figure's own
        // beat-four anchor, which the ring would reach if the clamp stopped at the voice boundary.
        // Only voice-scoped grammar with a cross-voice clamp lands on two.
        CHECK(marked->sustain == Fraction{2});
        // The restrike the clamp answered to, so the fixture cannot silently stop stating one.
        const auto restrike =
            std::ranges::find_if(chart.notes, [](const common::core::ChartNote& note) {
                return note.string == 1 &&
                       note.position == GridPosition{.measure = 1, .beat = 3, .offset = {}};
            });
        REQUIRE(restrike != chart.notes.end());
        CHECK(restrike->fret == 7);
    }

    SECTION("figure membership decides, not strike order: the staleness bound is gone")
    {
        // FIGURE MEMBERSHIP DECIDES, and the two marks are struck a beat apart to say so. Both sit
        // in ONE figure, so both tails are measured from the figure's LAST mark on beat two, whose
        // anchor is the beat-three restatement of the other drone. The mark struck on beat ONE
        // therefore rings the two beats up to it rather than the one its own onset would give.
        //
        // TWO and ONE is that stack. ONE and ONE is a per-mark bound, each mark stopping at the
        // next onset after ITSELF. THREE and TWO is a tail read off the SEAM on beat four, where
        // the statement of fret five contradicts the first drone's fret three — and note the grip
        // finds that contradiction even though the drone's quarter fell silent on beat two, which
        // is the FIGURE-scoped grip doing what the retired SOUND-scoped one could not.
        //
        // The retired staleness bound spared exactly the later mark, letting it ride past the seam
        // to the audibility cap on beat six — four beats. Nothing spares it now: it stops with the
        // figure it belongs to, on the same instant as the mark struck before it.
        GpScore score = makeLinearScore(1, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chord_beat_of(
                         quarter,
                         {GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 4, .fret = 11, .harmonic_type = ""},
                          GpNote{.string = 5, .fret = 3, .harmonic_type = ""}}),
                     chord_beat_of(
                         quarter,
                         {GpNote{.string = 1, .fret = 7, .let_ring = true, .harmonic_type = ""},
                          GpNote{.string = 4, .fret = 11, .harmonic_type = ""}}),
                     noteBeat(quarter, 11, 4),
                     noteBeat(quarter, 5, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const first = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const second = noteOnChartString(chart.notes, 2);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        // Two beats and one: both marks end on beat three, the figure's anchor, whichever side of
        // the contradicted statement they were struck on. ONE conversion, not two — the beat-two
        // mark ends where its own written quarter already ended, so it lengthened nothing.
        CHECK(first->sustain == Fraction{2});
        CHECK(second->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "1 let-ring rings were extended to their figure's end"));
    }
}

// THE HORIZON SEAM (user signing 2026-09-05, the sighted 33-bar silence). The grip seam above is
// blind to TIME: silence states nothing, so a rest of any length closes no figure, and material
// re-entering bars later that merely CONFIRMS the held grip — or grows fresh strings, which
// contradicts nothing either — joins the same figure. One corpus chart shipped a figure spanning
// 33 bars whose ONE end, computed from its LAST member as the law's own arithmetic requires, was
// handed to five notes struck 130 beats earlier that the tab writes at half a beat each.
//
// So a figure ALSO closes when the arriving onset lies past the AUDIBILITY HORIZON of its most
// recent member — one ORIGIN-BAR metric length past that member's onset, the very length the tail
// cap already reads, now stated once for both. The mark is the transcriber asking material to ring
// ON, and material arriving after the asking expired cannot belong to the same asking.
//
// MEMBER TO MEMBER, never from the figure's opening: a continuous texture of any length is still
// one figure, and only real silence expires an asking. The sections below pin that pair — the
// silence that seams and the gap that does not — plus the origin-bar reading between members and
// the one repair that must not undo the seam.
TEST_CASE(
    "Guitar Pro import seams a let-ring figure at the audibility horizon", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};
    constexpr Fraction eighth{1, 8};

    // A bar of silence, which is what the seam is being asked to read.
    const auto silent_bar = []() {
        return GpBar{
            .voices = {
                {restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4}),
                 restBeat(Fraction{1, 4})}
            }
        };
    };

    SECTION("a silence past the audibility horizon seams the figure")
    {
        // THE SIGHTED SHAPE, in miniature: three marks across two beats, two bars of silence, then
        // marked material re-entering on a grip that contradicts nothing — here by GROWING fresh
        // strings, the arrival that most plainly cannot seam on the grip. Every string is struck
        // exactly once, so the same-string clamp is silent and the numbers are the law's alone.
        GpScore score = makeLinearScore(4, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(silent_bar());
        score.tracks[0].bars.push_back(silent_bar());
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 3),
                     letRingBeat(quarter, 7, 4),
                     letRingBeat(quarter, 9, 5),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const opening = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const middle = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const last_of_stack = noteOnChartString(chart.notes, 3);
        const common::core::ChartNote* const returning = noteOnChartString(chart.notes, 4);
        REQUIRE(opening != nullptr);
        REQUIRE(middle != nullptr);
        REQUIRE(last_of_stack != nullptr);
        REQUIRE(returning != nullptr);
        // TWO figures. The first stack's last mark is on beat three, its horizon is one 4/4 bar
        // later — global beat six — and the material after the silence lands on global beat
        // twelve, so the asking has expired and that material founds its own figure. The anchor
        // asks for twelve and the cap answers six: six, five and four beats, the stack stopping
        // TOGETHER one bar into the silence.
        //
        // FIFTEEN, FOURTEEN and THIRTEEN is the time-blind law this section exists to refuse —
        // one figure whose end, measured from the LAST mark thirteen beats away, is handed to
        // notes that stopped asking a bar into the rest.
        CHECK(opening->sustain == Fraction{6});
        CHECK(middle->sustain == Fraction{5});
        CHECK(last_of_stack->sustain == Fraction{4});
        CHECK(globalBeatOf(*opening) + opening->sustain == Fraction{6});
        CHECK(globalBeatOf(*middle) + middle->sustain == Fraction{6});
        CHECK(globalBeatOf(*last_of_stack) + last_of_stack->sustain == Fraction{6});
        // The returning stack is a figure of its own, measured from its own last mark: three
        // beats, two and its own written quarter, all three ending on global beat fifteen.
        CHECK(returning->sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "5 let-ring rings were extended to their figure's end"));
    }

    SECTION("a silence inside the horizon seams nothing: the figure stays whole")
    {
        // THE CONTROL, identical to the section above except for WHERE the returning material
        // sits: three beats past the last mark instead of thirteen, which is inside its four-beat
        // horizon. The asking has not expired, so nothing seams and the six marks are one figure
        // with one end — the grouping the law had before the horizon existed, and must still have
        // wherever a rest is merely a rest.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     letRingBeat(quarter, 5, 3),
                     letRingBeat(quarter, 7, 4),
                     letRingBeat(quarter, 9, 5)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const opening = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const middle = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const last_of_stack = noteOnChartString(chart.notes, 3);
        const common::core::ChartNote* const returning = noteOnChartString(chart.notes, 4);
        REQUIRE(opening != nullptr);
        REQUIRE(middle != nullptr);
        REQUIRE(last_of_stack != nullptr);
        REQUIRE(returning != nullptr);
        // ONE figure, its last mark on bar two's fourth beat: nothing follows it anywhere in the
        // track, so the figure runs to its own latest written end on global beat eight and every
        // mark stops there. Eight, seven and six for the opening stack — SIX, FIVE and FOUR is
        // that stack seamed at its horizon, the answer this gap must not produce.
        CHECK(opening->sustain == Fraction{8});
        CHECK(middle->sustain == Fraction{7});
        CHECK(last_of_stack->sustain == Fraction{6});
        CHECK(returning->sustain == Fraction{3});
        CHECK(globalBeatOf(*opening) + opening->sustain == Fraction{8});
        CHECK(globalBeatOf(*returning) + returning->sustain == Fraction{8});
        CHECK(
            anyNoteContains(built->notes, "5 let-ring rings were extended to their figure's end"));
    }

    SECTION("the horizon between members reads the PREVIOUS member's own bar")
    {
        // THE ORIGIN BAR, the same reading the cap takes: the horizon is a metric LENGTH carried
        // from the member it is measured at, so it is that member's bar that states how long it
        // is. Three marks in a 4/4 bar, then two 6/8 bars with a mark on the second's fifth eighth
        // — global beat eight, EXACTLY one whole note past the last 4/4 mark and so exactly ON
        // its horizon, which is not past it. A horizon measured with the ARRIVING bar's shorter
        // 6/8 length expires on global beat six instead and seams here.
        GpScore score = makeLinearScore(3, syncs);
        score.master_bars[1] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.master_bars[2] = GpMasterBar{.numerator = 6, .denominator = 8, .section = {}};
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 9, 2),
                     restBeat(quarter)}
                }
            });
        std::vector<GpBeat> second{
            restBeat(eighth), restBeat(eighth), restBeat(eighth), restBeat(eighth)
        };
        second.push_back(letRingBeat(eighth, 11, 3));
        second.push_back(restBeat(eighth));
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(second)}});
        std::vector<GpBeat> third{letRingBeat(eighth, 13, 4)};
        for (int step = 0; step < 5; ++step)
        {
            third.push_back(restBeat(eighth));
        }
        score.tracks[0].bars.push_back(GpBar{.voices = {std::move(third)}});

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const opening = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const middle = noteOnChartString(chart.notes, 2);
        const common::core::ChartNote* const last_of_stack = noteOnChartString(chart.notes, 3);
        const common::core::ChartNote* const across = noteOnChartString(chart.notes, 4);
        const common::core::ChartNote* const furthest = noteOnChartString(chart.notes, 5);
        REQUIRE(opening != nullptr);
        REQUIRE(middle != nullptr);
        REQUIRE(last_of_stack != nullptr);
        REQUIRE(across != nullptr);
        REQUIRE(furthest != nullptr);
        // The fixture states where the two 6/8 marks landed, so a mis-built bar cannot quietly
        // become a different fixture.
        CHECK(across->position.measure == 2);
        CHECK(across->position.beat == 5);
        CHECK(furthest->position.measure == 3);
        CHECK(furthest->position.beat == 1);
        // ONE figure across the meter change: its last mark is bar three's downbeat, nothing
        // follows it, and the figure runs to its own latest written end on global beat eleven.
        // Eleven, ten and nine for the 4/4 stack, three and its own written eighth for the two
        // marks after it, every one of them ending together.
        //
        // EIGHT, SEVEN and SIX is the 6/8-measured horizon seaming at global beat eight: the
        // opening stack would close there with its own cap, and only the origin-bar reading keeps
        // this figure whole.
        CHECK(opening->sustain == Fraction{11});
        CHECK(middle->sustain == Fraction{10});
        CHECK(last_of_stack->sustain == Fraction{9});
        CHECK(across->sustain == Fraction{3});
        CHECK(furthest->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "4 let-ring rings were extended to their figure's end"));
    }

    SECTION("a fragment never donates across the horizon")
    {
        // THE ONE REPAIR THAT MUST NOT UNDO THE SEAM. A figure too small to found a span donates
        // its non-contradicting notes forward, because a GRIP seam that leaves a remnant that
        // small has mis-grouped a stack's opening notes. A HORIZON seam never mis-groups: it says
        // the asking expired, and donating across it re-joins the very asking the seam closed.
        //
        // A LONE mark — the smallest fragment there is — two bars of silence, then a returning
        // marked stack on fresh strings. The donation's own test passes (nothing contradicts), so
        // only the horizon refuses it.
        GpScore score = makeLinearScore(4, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(silent_bar());
        score.tracks[0].bars.push_back(silent_bar());
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 7, 3),
                     letRingBeat(quarter, 9, 4),
                     letRingBeat(quarter, 11, 5),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const lone = noteOnChartString(chart.notes, 1);
        const common::core::ChartNote* const returning = noteOnChartString(chart.notes, 4);
        REQUIRE(lone != nullptr);
        REQUIRE(returning != nullptr);
        // The lone mark keeps its own figure and rings to its own horizon — one 4/4 bar, four
        // beats. FIFTEEN is what a donation gives it: swallowed by the returning stack, it would
        // take that stack's end thirteen beats away, which is the sighted defect wearing the
        // repair's clothes.
        CHECK(lone->sustain == Fraction{4});
        CHECK(returning->sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "3 let-ring rings were extended to their figure's end"));
    }
}

// THE SIGHTED LEDGER (user signing 2026-09-04; re-signed the same day when the sighting walk
// deleted the anacrusis and anchored the tails at the marked run). The figure law was chosen
// against seven corpus figures sighted in the editor plus the user's repetition invariant; these
// sections pin the LEDGER itself — each sighted shape as a synthetic analog, letter-coded per
// the corpus firewall, with the one discriminating value each figure was sighted FOR. Figure F
// falls out of A's pickup, figure G (chugs never fragment) is pinned by the seam case above.
// Figure B is pinned at the LAW's answer, which the user accepted as a deviation: the sighted
// desire splits earlier, and the standing watch item ("figure-law import seams that read
// not-quite-right") collects such locations until a pattern can be hunted.
TEST_CASE("Guitar Pro import reproduces the sighted let-ring ledger", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};

    // A note by grid position, because the ledger figures restate strings: the first-on-string
    // helper cannot see a restatement's own tail.
    const auto note_at = [](const std::vector<common::core::ChartNote>& notes,
                            const int measure,
                            const int beat,
                            const int chart_string) -> const common::core::ChartNote* {
        for (const common::core::ChartNote& note : notes)
        {
            if (note.position.measure == measure && note.position.beat == beat &&
                note.string == chart_string)
            {
                return &note;
            }
        }
        return nullptr;
    };

    SECTION(
        "A: the same-fret wrap holds, the pickup clips the stack, the pickup rings to its own "
        "break")
    {
        // The figure restates its opening fret mid-stream (the wrap that must NOT split), grows a
        // fresh string, and a marked pickup late in the empty next bar contradicts the opening
        // string. Seam at the pickup (the onset before it is a CONFIRMATION, so no step-back);
        // every tail ends there except the ones the physics clamps earlier; the pickup founds its
        // own figure and rings exactly to the restatement that breaks it — the old last-of-series
        // yield, now the seam of the pickup's own figure.
        GpScore score = makeLinearScore(3, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 2, 0),
                     letRingBeat(quarter, 4, 2),
                     letRingBeat(quarter, 0, 4),
                     letRingBeat(quarter, 2, 0)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 0, 0)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 2, 0),
                     restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const opening = note_at(chart.notes, 1, 1, 1);
        const common::core::ChartNote* const grown = note_at(chart.notes, 1, 3, 5);
        const common::core::ChartNote* const wrap = note_at(chart.notes, 1, 4, 1);
        const common::core::ChartNote* const pickup = note_at(chart.notes, 2, 4, 1);
        REQUIRE(opening != nullptr);
        REQUIRE(grown != nullptr);
        REQUIRE(wrap != nullptr);
        REQUIRE(pickup != nullptr);
        // Seam at the bar-2 pickup (beat 8 of the stream): the opening clamps at its own wrap,
        // the wrap and the grown string ride to the seam, and the pickup rings one beat to the
        // bar-3 restatement that breaks its own figure.
        CHECK(opening->sustain == Fraction{3});
        CHECK(grown->sustain == Fraction{5});
        CHECK(wrap->sustain == Fraction{4});
        CHECK(pickup->sustain == Fraction{1});
    }

    SECTION(
        "B: the accepted deviation — the tails end at the first onset past the marks, past the "
        "sighted desire")
    {
        // The identical-prefix-then-divergence shape: the figure restates its opening fret, a
        // fresh-string pickup follows, and the divergence lands next. The pickup joins the
        // figure as growth, so the marked run's anchor is the pickup's own onset — where the
        // sighting wanted the split back at the restated note itself. Pinned at the LAW's
        // answer deliberately: the user priced this correction in, and the watch item tracks
        // the population.
        GpScore score = makeLinearScore(3, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 2, 0),
                     letRingBeat(quarter, 0, 1),
                     letRingBeat(quarter, 4, 2),
                     letRingBeat(quarter, 2, 0)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 0, 5),
                     noteBeat(quarter, 4, 1),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 9, 0),
                     restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const displaced = note_at(chart.notes, 1, 2, 2);
        const common::core::ChartNote* const third = note_at(chart.notes, 1, 3, 3);
        const common::core::ChartNote* const restated = note_at(chart.notes, 1, 4, 1);
        REQUIRE(displaced != nullptr);
        REQUIRE(third != nullptr);
        REQUIRE(restated != nullptr);
        // Seam at the bar-2 pickup (beat 5): the displaced string's tail ends there rather than
        // at its own beat-6 restrike, and the restated opening note keeps only its written beat.
        CHECK(displaced->sustain == Fraction{3});
        CHECK(third->sustain == Fraction{2});
        CHECK(restated->sustain == Fraction{1});
    }

    SECTION("C: the anchor clips the old tail at the new figure's head, before its own clamp")
    {
        // A lone marked note, then the next figure announced by a fresh-string head one onset
        // before the contradiction of the marked string. The head is the first onset after the
        // figure's last mark, so the marked tail ends THERE — a beat before its own same-string
        // clamp, which is the whole sighting: the clamp-bound reading left the tail hanging
        // into the new figure's bracket. No retreat rule is involved: the anchor alone lands
        // the clip on the head.
        GpScore score = makeLinearScore(3, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 0, 1)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 5, 2),
                     noteBeat(quarter, 7, 1),
                     noteBeat(quarter, 0, 3),
                     noteBeat(quarter, 5, 2)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const marked = note_at(chart.notes, 1, 4, 2);
        REQUIRE(marked != nullptr);
        // One beat: the anchor at the bar-2 downbeat, not the beat-2 clamp of its own string.
        CHECK(marked->sustain == Fraction{1});
    }

    SECTION("C: a fragment's marked head is donated to the figure it opens")
    {
        // The real junction shape (user signing 2026-09-04, the fragment donation): the remnant
        // and the new figure's MARKED head land in one figure — a two-note fragment that could
        // never found a span — before the contradiction seams. The head does not contradict the
        // new figure's grip (the arpeggio restates its stop), so it is donated forward and rings
        // with its own stack to its restrike; the remnant contradicts and stays, clipping at
        // the head's onset. Stranding the head would clip it at one written beat.
        GpScore score = makeLinearScore(3, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 0, 1)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 2),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 0, 3),
                     letRingBeat(quarter, 5, 2)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 9, 1),
                     restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const remnant = note_at(chart.notes, 1, 4, 2);
        const common::core::ChartNote* const head = note_at(chart.notes, 2, 1, 3);
        REQUIRE(remnant != nullptr);
        REQUIRE(head != nullptr);
        // The remnant clips at the head's onset; the donated head rings three beats to its own
        // restrike inside the figure it opened — one written beat is the stranded reading.
        CHECK(remnant->sustain == Fraction{1});
        CHECK(head->sustain == Fraction{3});
        CHECK(
            anyNoteContains(built->notes, "3 let-ring rings were extended to their figure's end"));
    }

    SECTION("D: a marked drone never rings into unmarked material in its own voice")
    {
        // The user's failing sighting, the ruling that anchored the tails at the marked run:
        // the drone and the unmarked chord chugs share ONE voice, the chords join the figure as
        // growth, and under the seam-bound reading the drone rang a full bar into them (the
        // figure only closed far away). The anchor ends it at the first own-voice onset after
        // the mark — the chug's downbeat.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 0, 4)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const drone = note_at(chart.notes, 1, 4, 5);
        REQUIRE(drone != nullptr);
        CHECK(drone->sustain == Fraction{1});
    }

    SECTION("D: a trailing drone's figure ends at the track's next onset, one voice over")
    {
        // The drone sits alone in its own voice; the other voice chugs a dyad on other strings
        // the next bar. Nothing ever contradicts the drone, its voice states nothing more, so
        // the trailing arm ends its figure at the first onset anywhere in the track — the
        // chug's downbeat — and the drone does NOT ring through the chug.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter), restBeat(quarter), restBeat(quarter), restBeat(quarter)},
                    {restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter),
                     letRingBeat(quarter, 0, 4)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false),
                     chordBeat(quarter, 5, 7, false, false)},
                    {restBeat(quarter), restBeat(quarter), restBeat(quarter), restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const drone = note_at(chart.notes, 1, 4, 5);
        REQUIRE(drone != nullptr);
        CHECK(drone->sustain == Fraction{1});
    }

    SECTION("E: the moved grip clips every tail at the seam, ahead of the members' own clamps")
    {
        // The figure restates itself, then the WHOLE grip moves at the next bar. The late
        // restatements' tails end at the seam — a beat BEFORE their own strings restrike in the
        // new figure. The shipped staleness bound let exactly these late confirmations escape
        // the shared clip and staircase into the new figure; figure membership ends them
        // together.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1),
                     letRingBeat(quarter, 5, 0),
                     letRingBeat(quarter, 7, 1)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 7, 0),
                     noteBeat(quarter, 9, 1),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const late_first = note_at(chart.notes, 1, 3, 1);
        const common::core::ChartNote* const late_second = note_at(chart.notes, 1, 4, 2);
        REQUIRE(late_first != nullptr);
        REQUIRE(late_second != nullptr);
        // The seam at the bar-2 downbeat binds both: the beat-4 restatement keeps only its
        // written beat even though its own string is not restruck until bar 2 beat 2.
        CHECK(late_first->sustain == Fraction{2});
        CHECK(late_second->sustain == Fraction{1});
    }

    SECTION("H: a repetition of the figure over a grown grip never splits")
    {
        // The user's invariant, in its hardest shape: the grip grows past the repeated chord,
        // the chord repeats note for note, and the divergence lands on the GROWN string. With no
        // retreat mechanism anywhere in the law, the whole repetition rides
        // in ONE figure and every tail ends at the divergence.
        GpScore score = makeLinearScore(2, syncs);
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 2, 0),
                     letRingBeat(quarter, 4, 1),
                     letRingBeat(quarter, 7, 2),
                     letRingBeat(quarter, 2, 0)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {letRingBeat(quarter, 4, 1),
                     noteBeat(quarter, 9, 2),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const grown = note_at(chart.notes, 1, 3, 3);
        const common::core::ChartNote* const repeated = note_at(chart.notes, 1, 4, 1);
        const common::core::ChartNote* const repeated_second = note_at(chart.notes, 2, 1, 2);
        REQUIRE(grown != nullptr);
        REQUIRE(repeated != nullptr);
        REQUIRE(repeated_second != nullptr);
        // The seam sits at the divergence (bar 2 beat 2), so the repetition's first note rings
        // TWO beats across the repetition — one beat is what a split at the repetition's head
        // would leave it. The grown string rides to its own restrike at the divergence.
        CHECK(grown->sustain == Fraction{3});
        CHECK(repeated->sustain == Fraction{2});
        CHECK(repeated_second->sustain == Fraction{1});
        CHECK(
            anyNoteContains(built->notes, "4 let-ring rings were extended to their figure's end"));
    }

    SECTION("I: a member's written length floors the figure's end — the stack stops together")
    {
        // The sighted ragged stack (2026-09-04): one member is tie-merged to a written end far
        // past the figure's anchor, and without the written-reach floor it alone rang there
        // while its stackmates stopped at the anchor — the stack stopped raggedly. A written
        // length is authored truth, not an estimate, so the whole figure runs to it: the anchor
        // and the cap bound only what the law is estimating.
        const auto tied_note = [](const int fret, const bool origin, const bool destination) {
            return GpNote{
                .string = 2,
                .fret = fret,
                .tie_origin = origin,
                .tie_destination = destination,
                .let_ring = true,
                .harmonic_type = "",
            };
        };
        GpScore score = makeLinearScore(3, syncs);
        GpBeat opening;
        opening.duration_whole = quarter;
        opening.notes = {tied_note(12, true, false)};
        opening.notes.push_back(
            GpNote{.string = 0, .fret = 5, .let_ring = true, .harmonic_type = ""});
        GpBeat second;
        second.duration_whole = quarter;
        second.notes = {tied_note(12, true, true)};
        second.notes.push_back(
            GpNote{.string = 3, .fret = 7, .let_ring = true, .harmonic_type = ""});
        GpBeat third;
        third.duration_whole = quarter;
        third.notes = {tied_note(12, true, true)};
        third.notes.push_back(GpNote{.string = 1, .fret = 3, .harmonic_type = ""});
        GpBeat fourth;
        fourth.duration_whole = quarter;
        fourth.notes = {tied_note(12, false, true)};
        score.tracks[0].bars.push_back(GpBar{.voices = {{opening, second, third, fourth}}});
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {restBeat(quarter), restBeat(quarter), restBeat(quarter), restBeat(quarter)}
                }
            });
        score.tracks[0].bars.push_back(
            GpBar{
                .voices = {
                    {noteBeat(quarter, 2, 4),
                     restBeat(quarter),
                     restBeat(quarter),
                     restBeat(quarter)}
                }
            });

        const auto built = buildGpSong(score);
        REQUIRE(built.has_value());
        const common::core::Chart& chart = built->arrangements.front().chart;
        const common::core::ChartNote* const long_written = note_at(chart.notes, 1, 1, 3);
        const common::core::ChartNote* const stackmate = note_at(chart.notes, 1, 1, 1);
        const common::core::ChartNote* const late_mark = note_at(chart.notes, 1, 2, 4);
        REQUIRE(long_written != nullptr);
        REQUIRE(stackmate != nullptr);
        REQUIRE(late_mark != nullptr);
        // The merged 12 is written four beats; the anchor sits at the beat-3 unmarked onset. The
        // floor takes the whole stack to the written end: 4, 4 and 3 — where the anchor alone
        // left the stackmates ragged at 2 and 1 beside the 12's own 4.
        CHECK(long_written->sustain == Fraction{4});
        CHECK(stackmate->sustain == Fraction{4});
        CHECK(late_mark->sustain == Fraction{3});
    }
}

// THE PINNED-FINGER UNION (the FHP certainty fix, 2026-09-05). A fretted note still SOUNDING at
// an onset pins its finger — lifting it would end the ring — so the onset's coverage includes it
// exactly as if it were struck there, and the window can never abandon held material to describe
// a hand that cannot exist. The doctrine behind it: FHP placement is partly charter opinion, so
// the generator is corrected only where it is provably WRONG, and a window excluding a sounding
// fretted ring is the certainty class.
TEST_CASE("Guitar Pro import pins the hand window to sounding rings", "[core][gp-import]")
{
    const std::vector<GpSyncPoint> syncs{
        GpSyncPoint{.bar = 0, .bar_fraction = 0.0, .seconds = 0.0, .modified_tempo = 120.0}
    };
    constexpr Fraction quarter{1, 4};
    constexpr Fraction half{1, 2};

    // A high note rings across a low onset in another voice: the fret-10 ring is still sounding
    // when the fret-2 note strikes a beat in, so the hand demonstrably holds both.
    GpScore score = makeLinearScore(1, syncs);
    score.tracks[0].bars.push_back(
        GpBar{
            .voices = {
                {noteBeat(half, 10, 2), restBeat(quarter), restBeat(quarter)},
                {restBeat(quarter),
                 noteBeat(quarter, 2, 0),
                 noteBeat(quarter, 2, 0),
                 restBeat(quarter)}
            }
        });

    const auto built = buildGpSong(score);
    REQUIRE(built.has_value());
    const common::core::Chart& chart = built->arrangements.front().chart;
    REQUIRE(chart.fret_hand_positions.size() == 2);
    // The opening window sits on the 10; the beat-2 window must still COVER the sounding 10
    // while reaching the struck 2 — anchor 2, width 9 — where the un-unioned walk snapped to a
    // four-wide window at 2 and abandoned the ringing finger. The beat-3 restrike changes
    // nothing: the 10 has ended (end-exclusive) and the 2 already fits the standing window.
    CHECK(chart.fret_hand_positions[0].fret == 10);
    CHECK(chart.fret_hand_positions[1].fret == 2);
    CHECK(chart.fret_hand_positions[1].width == 9);
}

} // namespace rock_hero::editor::core
