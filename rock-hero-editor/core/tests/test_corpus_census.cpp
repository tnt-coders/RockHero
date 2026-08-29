// The corpus census rig — the measurement instrument the chart ruleset's census gates read
// (docs/plans/in-progress/chart-ruleset.md, [D2] [D3] [D4] [D6] and LAW I's let-ring amendment).
//
// CORPUS FIREWALL (docs/plans/roadmap/23-detection-verification-harness.md, "Corpus firewall"):
// this file names no song, no path and no per-song datum. It iterates whatever directory
// ROCKHERO_GP_CORPUS_DIR points at, and every line it prints is an AGGREGATE — a count, a
// distribution, or a median. The hidden `[.local-corpus]` tag keeps ctest and CI from running it
// (Catch2 excludes tags beginning `[.` from default runs), and the case SKIPs when the variable is
// unset, so a checkout without the corpus is unaffected. Files that fail to parse are counted,
// never named.
//
// The pipeline is the production one end to end and NOTHING here re-implements it: `parseGpScore`
// -> `buildGpSong` -> `chartResolutions` / `chartShapeArrivals`, read once. The let-ring extension
// this rig used to apply in memory is now the shipped import (`letRingEnds` in
// gp_chart_builder.cpp), so the bare/extended split it was measured through is gone and every
// derived counter below reports the built chart as it ships.
//
// What survives of the walk is a MEASUREMENT of the source (`walkLetRing`): which of Guitar Pro's
// three stops bounds each marked ring, and what those rings cross. That is the evidence base for
// the two divergence candidates the ruleset leaves open at [D4] — a section-marker stop and a
// region-end stop — and it stays a reading of the score rather than a second statement of the
// import's rule.
#include "project/gp_chart_builder.h"
#include "project/gp_score.h"
#include "project/gp_score_parser.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/juce_path.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

using common::core::ChartNote;
using common::core::ChartPosture;
using common::core::ChartShape;
using common::core::Fraction;
using common::core::GridPosition;
using common::core::TempoMap;

// ---------------------------------------------------------------------------------------------
// Aggregation primitives. Nothing here can hold a per-song value: a histogram bin and a sample
// list are the only shapes the corpus firewall lets this rig carry across files.
// ---------------------------------------------------------------------------------------------

struct Histogram
{
    std::map<long long, long long> bins;

    void add(const long long key)
    {
        ++bins[key];
    }

    [[nodiscard]] long long at(const long long key) const
    {
        const auto bin = bins.find(key);
        return bin == bins.end() ? 0 : bin->second;
    }

    [[nodiscard]] long long countAtLeast(const long long key) const
    {
        long long total = 0;
        for (const auto& bin : bins)
        {
            total += bin.first >= key ? bin.second : 0;
        }
        return total;
    }

    [[nodiscard]] std::string text() const
    {
        if (bins.empty())
        {
            return "(none)";
        }
        std::ostringstream out;
        bool first = true;
        for (const auto& bin : bins)
        {
            out << (first ? "" : ", ") << bin.first << ':' << bin.second;
            first = false;
        }
        return out.str();
    }
};

struct Samples
{
    std::vector<double> values;

    void add(const double value)
    {
        values.push_back(value);
    }

    // Sorts in place, so it is deliberately non-const; the rig reads each distribution at the end.
    [[nodiscard]] std::string summary()
    {
        if (values.empty())
        {
            return "(no samples)";
        }
        std::ranges::sort(values);
        std::ostringstream out;
        out << std::fixed << std::setprecision(3) << "n=" << values.size()
            << " min=" << values.front() << " p25=" << quantile(0.25) << " median=" << quantile(0.5)
            << " p75=" << quantile(0.75) << " max=" << values.back();
        return out.str();
    }

    [[nodiscard]] double median()
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::ranges::sort(values);
        return quantile(0.5);
    }

private:
    [[nodiscard]] double quantile(const double fraction) const
    {
        const auto last = static_cast<double>(values.size() - 1);
        return values[static_cast<std::size_t>(fraction * last)];
    }
};

[[nodiscard]] std::string percentText(const long long part, const long long whole)
{
    if (whole <= 0)
    {
        return "n/a";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << (100.0 * static_cast<double>(part) / static_cast<double>(whole)) << '%';
    return out.str();
}

[[nodiscard]] double sharePercent(const long long part, const long long whole)
{
    return whole <= 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

// ---------------------------------------------------------------------------------------------
// The seam between Guitar Pro's terms and the built chart: a note is identified by where it sits
// and which string it is on, which is the pair both models agree about.
// ---------------------------------------------------------------------------------------------

struct NoteKey
{
    GridPosition position{};
    int string{0};

    friend std::strong_ordering operator<=>(const NoteKey& lhs, const NoteKey& rhs) = default;
    friend bool operator==(const NoteKey& lhs, const NoteKey& rhs) = default;
};

// ---------------------------------------------------------------------------------------------
// The measure table, in BOTH axes the census needs.
//
// The chart's own axis counts a measure as its numerator of SIGNATURE beats, which is the unit
// `ChartNote::sustain` is measured in. Guitar Pro's playback rule is stated in absolute time, so
// the let-ring walk runs on the whole-note axis, where a 6/8 bar really is shorter than a 4/4 one.
// Both are prefix sums over the same master bars `makeMeasureGrid` reads in the builder, so
// nothing here can drift from the positions the import produces.
// ---------------------------------------------------------------------------------------------

struct MeasureTable
{
    std::vector<int> numerator;
    std::vector<int> denominator;
    std::vector<Fraction> first_beat;
    std::vector<Fraction> start_whole;
};

[[nodiscard]] MeasureTable makeMeasureTable(const GpScore& score)
{
    MeasureTable table;
    Fraction beats{};
    Fraction whole{};
    for (const GpMasterBar& bar : score.master_bars)
    {
        table.numerator.push_back(bar.numerator);
        table.denominator.push_back(bar.denominator);
        table.first_beat.push_back(beats);
        table.start_whole.push_back(whole);
        beats = beats + Fraction{bar.numerator};
        whole = whole + Fraction{bar.numerator, bar.denominator};
    }
    return table;
}

// Converts an absolute whole-note position back onto the chart's signature-beat axis. A position
// past the final bar extrapolates at the last measure's meter, which is what a ring running off
// the end of the score needs.
[[nodiscard]] Fraction beatAtWhole(const MeasureTable& table, const Fraction whole)
{
    if (table.start_whole.empty())
    {
        return Fraction{};
    }
    const auto after = std::ranges::upper_bound(table.start_whole, whole);
    const std::size_t measure =
        after == table.start_whole.begin()
            ? 0
            : static_cast<std::size_t>(std::distance(table.start_whole.begin(), after)) - 1;
    return table.first_beat[measure] +
           ((whole - table.start_whole[measure]) * Fraction{table.denominator[measure]});
}

// ---------------------------------------------------------------------------------------------
// One voice's beat chain. alphaTab's `Beat.nextBeat` walks the SAME voice slot across bar lines,
// so both of the let-ring rule's authored stops — the rest and the same-string strike — are asked
// of this chain and of nothing else.
// ---------------------------------------------------------------------------------------------

struct ChainBeat
{
    const GpBeat* beat{nullptr};
    std::size_t measure{0};
    Fraction onset_whole{};
    Fraction duration_whole{};
    GridPosition position{};
    bool rest{false};
    bool let_ring{false};
};

[[nodiscard]] const GpNote* noteOnString(const GpBeat& beat, const int string)
{
    const auto found = std::ranges::find(beat.notes, string, &GpNote::string);
    return found == beat.notes.end() ? nullptr : &*found;
}

// What the chain walk had to pass over, so the validation counters can account for every mark the
// score states against every mark the census measures.
struct ChainDiagnostics
{
    long long graces{0};
    long long grace_letring_marks{0};
    long long overfull{0};
};

// Builds one chain per voice SLOT of a track. Grace beats take no time from the bar and the
// builder skips them for exactly that reason, so the chain skips them too and counts what it
// passed over — the same for a beat notated past the end of its bar.
[[nodiscard]] std::vector<std::vector<ChainBeat>> makeVoiceChains(
    const GpTrack& track, const MeasureTable& table, ChainDiagnostics& diagnostics)
{
    if (table.numerator.empty())
    {
        return {};
    }
    std::size_t voice_count = 0;
    for (const GpBar& bar : track.bars)
    {
        voice_count = std::max(voice_count, bar.voices.size());
    }

    std::vector<std::vector<ChainBeat>> chains(voice_count);
    for (std::size_t bar_index = 0; bar_index < track.bars.size(); ++bar_index)
    {
        const std::size_t measure = std::min(bar_index, table.numerator.size() - 1);
        const int denominator = table.denominator[measure];
        const Fraction beat_whole{1, denominator};
        for (std::size_t voice = 0; voice < track.bars[bar_index].voices.size(); ++voice)
        {
            Fraction position_beats{};
            for (const GpBeat& beat : track.bars[bar_index].voices[voice])
            {
                if (beat.grace != GpGracePlacement::None)
                {
                    ++diagnostics.graces;
                    diagnostics.grace_letring_marks += static_cast<long long>(std::ranges::count_if(
                        beat.notes, [](const GpNote& note) { return note.let_ring; }));
                    continue;
                }
                const Fraction onset = position_beats;
                position_beats = position_beats + (beat.duration_whole * Fraction{denominator});
                if (onset >= Fraction{table.numerator[measure]})
                {
                    ++diagnostics.overfull;
                    continue;
                }
                const int whole_beats = onset.numerator / onset.denominator;
                chains[voice].push_back(
                    ChainBeat{
                        .beat = &beat,
                        .measure = measure,
                        .onset_whole = table.start_whole[measure] + (onset * beat_whole),
                        .duration_whole = beat.duration_whole,
                        .position =
                            GridPosition{
                                .measure = static_cast<int>(measure) + 1,
                                .beat = whole_beats + 1,
                                .offset = onset - Fraction{whole_beats},
                            },
                        .rest = beat.notes.empty(),
                        .let_ring = std::ranges::any_of(
                            beat.notes, [](const GpNote& note) { return note.let_ring; }),
                    });
            }
        }
    }
    return chains;
}

// ---------------------------------------------------------------------------------------------
// Guitar Pro's playback rule as the SOURCE states it, transcribed rather than invented (LAW I's
// playback-truth principle). The import ships the same rule with its strike stop delegated to the
// chart's own same-string clamp, so what this walk adds is the ATTRIBUTION the divergence
// candidates need: which stop bounds each ring, and what the ring crosses on the way there. Where
// the two part company is a same-string note the build merges away — a tie continuation, a
// legato-slide landing — which this walk reads as a stop and the shipped import answers with the
// merged ring instead, reaching the same end by a different route.
// ---------------------------------------------------------------------------------------------

enum class LetRingStop : std::uint8_t
{
    // The next beat of the note's own voice strikes the same string — an authored statement, and
    // in the shipped import the clamp's bound rather than the walk's own.
    Strike,
    // The next beat of the note's own voice is a rest — the transcriber's silence statement.
    Rest,
    // One full measure-duration from the note's own onset: the rule's one BLIND stop.
    Cap,
    // The voice simply ends; nothing later states anything at all.
    ScoreEnd
};

struct LetRingRing
{
    Fraction onset_whole{};
    Fraction end_whole{};
    LetRingStop stop{LetRingStop::ScoreEnd};
};

// A maximal run of consecutive let-ring-marked beats in one voice chain — the mark's OWN extent,
// which is what a region-end divergence candidate would stop a ring at.
struct LetRingRegion
{
    std::size_t first{0};
    std::size_t last{0};
    Fraction start_whole{};
    Fraction end_whole{};
    long long beats{0};
    long long marks{0};
};

[[nodiscard]] LetRingRing walkLetRing(
    const std::vector<ChainBeat>& chain, const MeasureTable& table, const std::size_t index,
    const int string)
{
    const std::size_t measure = chain[index].measure;
    const Fraction cap{table.numerator[measure], table.denominator[measure]};
    std::size_t last = index;
    Fraction ring = chain[index].duration_whole;
    LetRingStop stop = LetRingStop::ScoreEnd;
    for (std::size_t step = index; step + 1 < chain.size();)
    {
        const ChainBeat& next = chain[step + 1];
        if (next.rest)
        {
            stop = LetRingStop::Rest;
            break;
        }
        if (noteOnString(*next.beat, string) != nullptr)
        {
            stop = LetRingStop::Strike;
            break;
        }
        ++step;
        last = step;
        ring = (chain[step].onset_whole - chain[index].onset_whole) + chain[step].duration_whole;
        if (ring > cap)
        {
            ring = cap;
            stop = LetRingStop::Cap;
            break;
        }
    }
    // The reference implementation's own tail: a mark whose very next beat already stops it rings
    // for its notated duration rather than for nothing.
    if (last == index)
    {
        ring = chain[index].duration_whole;
    }
    return LetRingRing{
        .onset_whole = chain[index].onset_whole,
        .end_whole = chain[index].onset_whole + ring,
        .stop = stop,
    };
}

[[nodiscard]] std::vector<LetRingRegion> makeLetRingRegions(const std::vector<ChainBeat>& chain)
{
    std::vector<LetRingRegion> regions;
    for (std::size_t index = 0; index < chain.size(); ++index)
    {
        if (!chain[index].let_ring)
        {
            continue;
        }
        const Fraction end = chain[index].onset_whole + chain[index].duration_whole;
        const auto marks = static_cast<long long>(std::ranges::count_if(
            chain[index].beat->notes, [](const GpNote& note) { return note.let_ring; }));
        if (!regions.empty() && regions.back().last + 1 == index)
        {
            regions.back().last = index;
            regions.back().end_whole = end;
            ++regions.back().beats;
            regions.back().marks += marks;
            continue;
        }
        regions.push_back(
            LetRingRegion{
                .first = index,
                .last = index,
                .start_whole = chain[index].onset_whole,
                .end_whole = end,
                .beats = 1,
                .marks = marks,
            });
    }
    return regions;
}

// Whether the score states this note as the DESTINATION of a hammer or pull.
[[nodiscard]] bool claimsLegato(const GpNote& note)
{
    return note.hopo_destination || note.left_hand_tapped;
}

// Whether the next note on this string in this voice claims a legato connection, which is what
// makes THIS note the origin the hand hammers or pulls off from. Derived from the pair rather than
// read from a flag, because the pair is already in the chain and gpif's origin bit says exactly
// this about it — so nothing new has to be parsed to ask the question.
[[nodiscard]] bool originOfLegato(
    const std::vector<ChainBeat>& chain, const std::size_t index, const int string)
{
    for (std::size_t ahead = index + 1; ahead < chain.size(); ++ahead)
    {
        const GpNote* const next = noteOnString(*chain[ahead].beat, string);
        if (next != nullptr)
        {
            return claimsLegato(*next);
        }
    }
    return false;
}

// The region a beat belongs to, or `regions.size()` when the beat carries no mark. Regions are
// ascending and disjoint, so the containing one is the last whose first index is not past it.
[[nodiscard]] std::size_t regionContaining(
    const std::vector<LetRingRegion>& regions, const std::size_t index)
{
    const auto after =
        std::ranges::upper_bound(regions, index, std::ranges::less{}, &LetRingRegion::first);
    if (after == regions.begin())
    {
        return regions.size();
    }
    const auto region = std::prev(after);
    return index <= region->last ? static_cast<std::size_t>(std::distance(regions.begin(), region))
                                 : regions.size();
}

// ---------------------------------------------------------------------------------------------
// The derived counters. Every one is a READING of what the production derivation produced, never a
// second derivation: the spans, postures and arrival flags all come from `chartResolutions` and
// `chartShapeArrivals`, and what is written here is only the classification each gate asks for.
// ---------------------------------------------------------------------------------------------

struct DerivationCounters
{
    long long spans{0};
    long long spans_arpeggio{0};

    // (ii) — the lone re-pick, by witness kind.
    long long ii_spans{0};
    long long ii_slots{0};
    long long ii_sound_witness{0};
    long long ii_claim_witness{0};
    long long ii_spans_arpeggio{0};

    // What reconciles the row above with LAW III's own consequence — every span the WALK continues
    // through a lone re-pick is an arpeggio, because the class rule asks whether that slot sounded
    // fewer strings than the shape does and one always is fewer than two. This rig does not walk;
    // it reads the FINISHED span and asks which of its slots LOOK like lone re-picks, and a slot
    // sitting exactly on the span's own end is where the two readings part: it may be the last
    // strum the statement rode (rule 12a's trim floors the end there whenever a closing onset
    // crowds inside the margin) or it may be the onset that CLOSED the span, which the exact-
    // adjacency fallback puts on the end too. The second kind is no continuation at all.
    long long ii_spans_end_slot_only{0};
    long long ii_spans_end_slot_only_boxed{0};

    // [D3] — the continuity gates.
    long long ii_gap_repicks{0};
    long long ii_gap_repicks_sound{0};
    long long ii_gap_repicks_claim{0};
    long long interior_gap_spans{0};

    // [D4] — trigger 4, a carried ring folding into a span onset.
    long long trigger4_spans{0};
    long long trigger4_only_spans{0};
    long long trigger4_foldins{0};
    long long trigger4_foldins_unmeasurable{0};
    Histogram carried_fret_distance;

    // The same distances split by WHAT is carried, because the two are different physical claims
    // and only one of them is a reach question at all: an OPEN string is a voicing member no
    // finger holds, so no distance from it means anything about the hand, while a FRETTED carry
    // asserts that a finger stayed down while the chord was struck somewhere else.
    long long foldins_open{0};
    long long foldins_fretted{0};
    Histogram carried_distance_open;
    Histogram carried_distance_fretted;
    Samples carried_distance_fretted_spread;

    // Fretted carries against the board POSITION of the shape they cross, because how far a hand
    // can span is not one constant: the frets narrow as they climb, so the same fret distance is
    // a different reach at the nut and at the twelfth.
    std::vector<long long> fretted_by_position{0, 0, 0};
    std::vector<long long> fretted_over_six_by_position{0, 0, 0};
    std::vector<Histogram> fretted_distance_by_position = std::vector<Histogram>(3);

    // [D2] — travel and the breathing landing. The first two are the PRE-BUILD instrument, kept
    // exactly as they were so the build's before/after reads against one unchanged ruler.
    long long travel_spans{0};
    long long travel_breathing_spans{0};

    // [D2] built — the landed grip. `successor_spans` is the DERIVATION's own count and the
    // authority here; everything beside it is the source-side reading that says which edge
    // suppressed a landing, which the finished spans do not record.
    long long successor_spans{0};
    long long successor_spans_arpeggio{0};
    long long travel_any_spans{0};
    long long travel_landings_open{0};
    long long travel_landings_staggered{0};
    long long travel_landings_crowded{0};
};

// Where a note's fret channel comes to REST after leaving its onset stop, and where that rest ends
// — [D2]'s landing, read from the SOURCE side. This is the model's own reading of the channel
// ("equal frets are a HOLD, different frets are travel"), so a fret the channel leaves again is a
// point on the path and never a grip.
//
// Spelled here rather than approximated, for the reason the lone-re-pick member test is: the
// finished spans record which landings OPENED and never which ones were suppressed, so a rig that
// cannot read the channel cannot attribute the movement to an edge. The derivation's own
// `successor_spans` stands beside these as the authority, and a disagreement between the two is a
// finding rather than a defect in either.
struct SourceLanding
{
    Fraction arrival{};
    Fraction statement_end{};
};

[[nodiscard]] std::optional<SourceLanding> sourceLanding(const ChartNote& note)
{
    // The first stop the channel comes to rest on after leaving the note's own.
    std::optional<Fraction> arrival;
    int landed = note.fret;
    for (const common::core::Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (!fret.has_value())
        {
            continue;
        }
        if (!arrival.has_value())
        {
            if (*fret != note.fret)
            {
                arrival = keyframe.offset;
                landed = *fret;
            }
            continue;
        }
        if (*fret == landed)
        {
            break;
        }
        arrival = keyframe.offset;
        landed = *fret;
    }
    const std::optional<Fraction>& lands = arrival;
    if (!lands.has_value())
    {
        return std::nullopt;
    }
    // Where that rest ends: its last restatement before the channel leaves again, or the ring's
    // own end where it never does.
    Fraction held = *lands;
    Fraction ends = note.sustain;
    for (const common::core::Keyframe& keyframe : note.keyframes)
    {
        const std::optional<int>& fret = keyframe.fret;
        if (!fret.has_value() || keyframe.offset < *lands)
        {
            continue;
        }
        if (*fret == landed)
        {
            held = keyframe.offset;
            continue;
        }
        ends = held;
        break;
    }
    return SourceLanding{.arrival = *lands, .statement_end = std::min(ends, note.sustain)};
}

// A sounding fretting-hand onset: what opens a span, re-picks one, and travels. A silent hold
// states a posture without sound; the picking hand's own onsets are evidence, never members.
[[nodiscard]] bool soundsWithFrettingHand(const ChartNote& note)
{
    return !common::core::silentHold(note.attack) && !common::core::rightHandOnset(note.attack);
}

// The identity two strums are compared by, reduced exactly as the shape walk reduces it: the
// PRESENTED note with its position and duration neutralised, so any technique difference splits.
// Spelled here rather than approximated, because the lone-re-pick member test is that comparison.
[[nodiscard]] ChartNote articulationOf(const ChartNote& presented)
{
    ChartNote key = presented;
    key.position = GridPosition{};
    key.sustain = Fraction{};
    return key;
}

// The note stream indexed the three ways every gate below asks about it: each note's exact global
// beat, the slots it groups into, and one ascending column per string. Built once per derivation
// so no gate walks the stream looking backward.
struct StreamIndex
{
    std::vector<Fraction> onset;
    std::vector<Fraction> slot_beat;
    std::vector<std::size_t> slot_first;
    std::vector<std::size_t> slot_last;
    std::map<GridPosition, std::size_t> slot_of;

    // SOUNDING onsets only, per string. Every question this column answers is a question about
    // sound — where a ring's next same-string onset lands, and which note last sounded a string —
    // so it holds exactly the set production reads for those (`sounding_rings` in
    // chart_shapes.cpp). The slot arrays above still carry the whole stream, silent holds
    // included, because a slot is a position and not a sound.
    std::vector<std::vector<std::size_t>> by_string;
};

[[nodiscard]] StreamIndex makeStreamIndex(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    StreamIndex index;
    index.by_string.resize(static_cast<std::size_t>(common::core::g_max_chart_strings) + 1);
    index.onset.reserve(notes.size());
    for (const ChartNote& note : notes)
    {
        index.onset.push_back(common::core::beatDistance(tempo_map, GridPosition{}, note.position));
    }
    for (std::size_t note_index = 0; note_index < notes.size(); ++note_index)
    {
        const GridPosition& position = notes[note_index].position;
        if (index.slot_beat.empty() || index.slot_of.find(position) == index.slot_of.end())
        {
            index.slot_of.emplace(position, index.slot_beat.size());
            index.slot_beat.push_back(index.onset[note_index]);
            index.slot_first.push_back(note_index);
            index.slot_last.push_back(note_index + 1);
        }
        else
        {
            index.slot_last.back() = note_index + 1;
        }
        // A silent hold joins no column: it sounds nothing, so the law reads it as neither the
        // onset that ends a ring nor a witness that a string is still going. Leaving it in would
        // let a held finger bridge a stored gap the production walk calls a detachment, and let it
        // shadow the note that really rings there.
        const int string = notes[note_index].string;
        if (string >= 1 && string <= common::core::g_max_chart_strings &&
            !common::core::silentHold(notes[note_index].attack))
        {
            index.by_string[static_cast<std::size_t>(string)].push_back(note_index);
        }
    }
    return index;
}

// The latest note on `string` whose onset is strictly before `beat`, or nothing.
[[nodiscard]] std::optional<std::size_t> lastNoteBefore(
    const StreamIndex& index, const int string, const Fraction beat)
{
    if (string < 1 || string > common::core::g_max_chart_strings)
    {
        return std::nullopt;
    }
    const std::vector<std::size_t>& column = index.by_string[static_cast<std::size_t>(string)];
    const auto after = std::ranges::lower_bound(
        column, beat, std::ranges::less{}, [&index](const std::size_t note_index) {
            return index.onset[note_index];
        });
    if (after == column.begin())
    {
        return std::nullopt;
    }
    return *std::prev(after);
}

// Whether any onset on `string` sits exactly at `beat` — the adjacency half of the continuity law.
[[nodiscard]] bool onsetLandsAt(const StreamIndex& index, const int string, const Fraction beat)
{
    if (string < 1 || string > common::core::g_max_chart_strings)
    {
        return false;
    }
    const std::vector<std::size_t>& column = index.by_string[static_cast<std::size_t>(string)];
    const auto at = std::ranges::lower_bound(
        column, beat, std::ranges::less{}, [&index](const std::size_t note_index) {
            return index.onset[note_index];
        });
    return at != column.end() && index.onset[*at] == beat;
}

void countDerivation(
    const std::vector<ChartNote>& saved, const std::vector<ChartNote>& presented,
    const std::vector<ChartShape>& shapes, const std::vector<ChartPosture>& postures,
    const std::vector<bool>& arrivals, const TempoMap& tempo_map, DerivationCounters& out)
{
    const StreamIndex index = makeStreamIndex(saved, tempo_map);
    constexpr auto string_count = static_cast<std::size_t>(common::core::g_max_chart_strings);

    for (std::size_t shape_index = 0; shape_index < shapes.size(); ++shape_index)
    {
        const ChartShape& shape = shapes[shape_index];
        const bool arpeggio = shape_index < arrivals.size() && arrivals[shape_index];
        ++out.spans;
        out.spans_arpeggio += arpeggio ? 1 : 0;

        std::vector<std::optional<int>> posture;
        if (shape.posture < postures.size())
        {
            posture = postures[shape.posture].frets;
        }
        posture.resize(string_count);

        const Fraction start =
            common::core::beatDistance(tempo_map, GridPosition{}, shape.position);
        const Fraction end = start + shape.sustain;

        // The span's own opening slot: who strikes there, which strings sound at all, and the
        // articulation each sounding member states — the identity a re-pick has to repeat.
        std::vector<std::size_t> struck_at_start;
        std::set<int> sounded_strings;
        std::vector<std::optional<ChartNote>> start_articulation(string_count);
        const auto opening = index.slot_of.find(shape.position);
        if (opening == index.slot_of.end())
        {
            // [D2]: the landing successor is the ONE span the model opens where no note sits — its
            // members are carried rings and nothing is struck or claimed at its start. That makes
            // "opens at no slot" a structural reading of the derivation rather than a guess, and
            // the arpeggio count beside it is the class law's own discriminator: a span striking
            // nothing of a shape that sounds two or more strings has its members arriving
            // separately, so the two figures must agree.
            ++out.successor_spans;
            out.successor_spans_arpeggio += arpeggio ? 1 : 0;
        }
        if (opening != index.slot_of.end())
        {
            const std::size_t slot = opening->second;
            for (std::size_t note = index.slot_first[slot]; note < index.slot_last[slot]; ++note)
            {
                sounded_strings.insert(saved[note].string);
                if (!soundsWithFrettingHand(saved[note]))
                {
                    continue;
                }
                struck_at_start.push_back(note);
                const auto string_index = static_cast<std::size_t>(saved[note].string - 1);
                if (string_index < start_articulation.size())
                {
                    start_articulation[string_index] = articulationOf(presented[note]);
                }
            }
        }

        // The board POSITION of the shape a carry crosses: the lowest STOPPED fret its struck
        // members hold. Open members are passed over because an open string states no hand
        // position at all, and a shape struck entirely open falls to the nut — which is where the
        // hand is.
        int shape_position = 0;
        for (const std::size_t struck : struck_at_start)
        {
            const int fret = saved[struck].fret;
            if (fret > 0 && (shape_position == 0 || fret < shape_position))
            {
                shape_position = fret;
            }
        }
        const std::size_t position_bucket =
            shape_position <= 6 ? 0 : (shape_position <= 11 ? 1 : 2);

        // ---- [D4] trigger 4: an earlier PRESENTED tail crossing the span start on a posture
        // string with no onset at it. The fold-in is what puts that carried fret into the posture.
        //
        // Since the [D2] build these raw counts include the LANDING SUCCESSORS, whose every member
        // is a carried ring by construction — they are the whole of the "no struck fret to
        // measure" column, and the reach question they answer is meaningless there (nothing was
        // struck for the carry to be a reach from). The flip count below is unaffected, because a
        // successor strikes fewer than two strings and so already carries another trigger.
        long long foldins_here = 0;
        for (std::size_t string_index = 0; string_index < posture.size(); ++string_index)
        {
            const std::optional<int>& stop = posture[string_index];
            if (!stop.has_value())
            {
                continue;
            }
            const int string = static_cast<int>(string_index) + 1;
            if (sounded_strings.contains(string))
            {
                continue;
            }
            const std::optional<std::size_t> carried = lastNoteBefore(index, string, start);
            if (!carried.has_value())
            {
                continue;
            }
            const std::size_t ringing = *carried;
            if (index.onset[ringing] + presented[ringing].sustain <= start)
            {
                continue;
            }
            ++foldins_here;
            ++out.trigger4_foldins;
            if (struck_at_start.empty())
            {
                ++out.trigger4_foldins_unmeasurable;
                continue;
            }
            // The source-hygiene proxy: how far the carried finger sits from the nearest finger
            // the chord actually put down. A ring crossing a chord's onset proves the finger
            // STAYED only where a hand could plausibly have held both at once.
            //
            // The carried fret is read off the DERIVED posture, so this measures whatever the rule
            // folded in — which since the F1 fix (2026-08-29) is the stop the ring's own fret
            // channel states at the crossing, not the fret it was struck at. Reading the onset
            // fret here instead would make the rig a second statement of the rule it measures.
            long long nearest = -1;
            for (const std::size_t struck : struck_at_start)
            {
                const long long distance = std::abs(
                    static_cast<long long>(*stop) - static_cast<long long>(saved[struck].fret));
                nearest = nearest < 0 ? distance : std::min(nearest, distance);
            }
            out.carried_fret_distance.add(nearest);

            // An open carry is a voicing member, so its distance says nothing about reach; the
            // fretted ones are the population the physical question is actually about, and they
            // carry the shape's board position with them.
            if (*stop == 0)
            {
                ++out.foldins_open;
                out.carried_distance_open.add(nearest);
                continue;
            }
            ++out.foldins_fretted;
            out.carried_distance_fretted.add(nearest);
            out.carried_distance_fretted_spread.add(static_cast<double>(nearest));
            ++out.fretted_by_position[position_bucket];
            out.fretted_distance_by_position[position_bucket].add(nearest);
            if (nearest > 6)
            {
                ++out.fretted_over_six_by_position[position_bucket];
            }
        }

        // ---- one pass over the slots the span covers answers every interior question: the
        // picking-hand trigger, the lone re-pick and its witness, and the continuity law's own
        // interior gap.
        const auto first_slot = std::ranges::lower_bound(index.slot_beat, start);
        bool picking_hand_inside = false;
        bool lone_repick_here = false;
        bool interior_gap_here = false;
        bool interior_repick_here = false;

        for (auto slot_at = first_slot; slot_at != index.slot_beat.end() && *slot_at <= end;
             ++slot_at)
        {
            const auto slot =
                static_cast<std::size_t>(std::distance(index.slot_beat.begin(), slot_at));
            const Fraction now = index.slot_beat[slot];
            std::vector<std::size_t> struck_here;
            for (std::size_t note = index.slot_first[slot]; note < index.slot_last[slot]; ++note)
            {
                if (common::core::rightHandOnset(saved[note].attack))
                {
                    picking_hand_inside = true;
                }
                if (!soundsWithFrettingHand(saved[note]))
                {
                    continue;
                }
                struck_here.push_back(note);

                // ---- [D3] the continuity law's own test, asked of every sounding member inside
                // the span: a STORED ring that has ended with no same-string onset at its end is
                // an authored statement of detachment.
                const auto string_index = static_cast<std::size_t>(saved[note].string - 1);
                if (string_index < posture.size() && posture[string_index].has_value())
                {
                    const Fraction ring_end = index.onset[note] + saved[note].sustain;
                    if (ring_end < end && !onsetLandsAt(index, saved[note].string, ring_end))
                    {
                        interior_gap_here = true;
                    }
                }
            }
            if (now == start || struck_here.size() != 1)
            {
                continue;
            }

            // ---- (ii): a LONE re-pick of a string the span already states. Both arms of the
            // production rule are asked here rather than one proxy for both: a member the SOUND
            // states must be re-picked with the identical articulation (rule 11's question asked
            // of one string), and a member the chart HOLDS has no articulation to match, so its
            // claim's own stop is the whole test.
            const std::size_t repick = struck_here.front();
            const auto repick_string = static_cast<std::size_t>(saved[repick].string - 1);
            if (repick_string >= posture.size())
            {
                continue;
            }
            const std::optional<ChartNote>& sound_stated = start_articulation[repick_string];
            const std::optional<int>& stated = posture[repick_string];
            const bool member = sound_stated.has_value()
                                    ? *sound_stated == articulationOf(presented[repick])
                                    : stated.has_value() && *stated == saved[repick].fret;
            if (!member)
            {
                continue;
            }

            // The witness: some OTHER posture string still PRESENTED as ringing here. Where none
            // is, the span was standing on the hand's own statement — a silent-only span waiting
            // for exactly this arrival.
            bool sound_witness = false;
            for (std::size_t other = 0; other < posture.size(); ++other)
            {
                const std::optional<int>& held = posture[other];
                if (other == repick_string || !held.has_value())
                {
                    continue;
                }
                const std::optional<std::size_t> witness =
                    lastNoteBefore(index, static_cast<int>(other) + 1, now);
                if (!witness.has_value())
                {
                    continue;
                }
                const std::size_t witness_note = *witness;
                sound_witness = sound_witness ||
                                now < index.onset[witness_note] + presented[witness_note].sustain;
            }

            ++out.ii_slots;
            interior_repick_here = interior_repick_here || now != end;
            lone_repick_here = true;
            if (sound_witness)
            {
                ++out.ii_sound_witness;
            }
            else
            {
                ++out.ii_claim_witness;
            }

            // ---- [D3] amendment 1: is this re-pick ADJACENT to its own string's prior ring, or
            // does it follow a stored GAP? An adjacent re-pick is continuity itself; a gap re-pick
            // is the population the narrowing would stop continuing.
            const std::optional<std::size_t> prior =
                lastNoteBefore(index, saved[repick].string, now);
            if (!prior.has_value())
            {
                continue;
            }
            const std::size_t previous = *prior;
            if (index.onset[previous] + saved[previous].sustain < now)
            {
                ++out.ii_gap_repicks;
                if (sound_witness)
                {
                    ++out.ii_gap_repicks_sound;
                }
                else
                {
                    ++out.ii_gap_repicks_claim;
                }
            }
        }

        if (lone_repick_here)
        {
            ++out.ii_spans;
            out.ii_spans_arpeggio += arpeggio ? 1 : 0;
            if (!interior_repick_here)
            {
                ++out.ii_spans_end_slot_only;
                out.ii_spans_end_slot_only_boxed += arpeggio ? 0 : 1;
            }
        }
        out.interior_gap_spans += interior_gap_here ? 1 : 0;

        // ---- [D4] the flip: trigger 4 firing while no OTHER arrival trigger does, which is
        // exactly a span that would print as a chord box without it.
        if (foldins_here > 0)
        {
            ++out.trigger4_spans;
            const bool other_trigger =
                struck_at_start.size() < 2 || picking_hand_inside || shape.silent_member;
            if (!other_trigger)
            {
                ++out.trigger4_only_spans;
            }
        }

        if (struck_at_start.size() < 2)
        {
            continue;
        }

        // ---- [D2] built: which travel spans re-open, and which edge suppressed the rest. The
        // margin is taken at the span's own measure, which is the landing's except where a glide
        // crosses a barline into a changed signature — a rounding this attribution accepts, since
        // `successor_spans` above is what the movement is actually counted by.
        std::optional<Fraction> common_landing;
        bool staggered = false;
        bool travels = false;
        for (const std::size_t member : struck_at_start)
        {
            const std::optional<SourceLanding> landed = sourceLanding(saved[member]);
            if (!landed.has_value())
            {
                continue;
            }
            travels = true;
            const Fraction here = index.onset[member] + landed->arrival;
            staggered = staggered || (common_landing.has_value() && *common_landing != here);
            common_landing = here;
        }
        // Bound once so the presence test and every read below are provably the same object.
        const std::optional<Fraction>& lands = common_landing;
        if (travels && lands.has_value())
        {
            ++out.travel_any_spans;
            if (staggered)
            {
                ++out.travel_landings_staggered;
            }
            else
            {
                const Fraction margin = common::core::minimumSustainDistanceBeats(
                    tempo_map.timeSignatureAt(shape.position.measure).denominator);
                long long resting = 0;
                for (const std::size_t member : struck_at_start)
                {
                    const std::optional<SourceLanding> landed = sourceLanding(saved[member]);
                    const Fraction ends =
                        index.onset[member] +
                        (landed.has_value() ? landed->statement_end : saved[member].sustain);
                    resting += *lands + margin < ends ? 1 : 0;
                }
                out.travel_landings_open += resting >= 2 ? 1 : 0;
                out.travel_landings_crowded += resting >= 2 ? 0 : 1;
            }
        }

        // ---- [D2] the PRE-BUILD instrument, unchanged: every sounding member's fret channel
        // states a differing stop, all of them land inside their own rings, and two or more rings
        // continue past the last landing.
        bool every_member_travels = true;
        bool every_travel_lands = true;
        Fraction last_landing{};
        bool landing_seen = false;
        for (const std::size_t member : struck_at_start)
        {
            const ChartNote& note = saved[member];
            std::optional<Fraction> landing;
            bool differs = false;
            for (const common::core::Keyframe& keyframe : note.keyframes)
            {
                const std::optional<int>& fret = keyframe.fret;
                if (!fret.has_value())
                {
                    continue;
                }
                differs = differs || *fret != note.fret;
                landing = keyframe.offset;
            }
            if (!differs || !landing.has_value())
            {
                every_member_travels = false;
                break;
            }
            const Fraction arrival = *landing;
            if (note.slide_out.has_value() || arrival >= note.sustain)
            {
                every_travel_lands = false;
            }
            const Fraction absolute = index.onset[member] + arrival;
            last_landing = landing_seen ? std::max(last_landing, absolute) : absolute;
            landing_seen = true;
        }
        if (!every_member_travels || !landing_seen)
        {
            continue;
        }
        ++out.travel_spans;
        if (!every_travel_lands)
        {
            continue;
        }
        long long still_ringing = 0;
        for (const std::size_t member : struck_at_start)
        {
            if (index.onset[member] + saved[member].sustain > last_landing)
            {
                ++still_ringing;
            }
        }
        if (still_ringing >= 2)
        {
            ++out.travel_breathing_spans;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The whole census, accumulated across every file.
// ---------------------------------------------------------------------------------------------

struct Census
{
    long long files_found{0};
    long long files_parsed{0};
    long long files_skipped{0};
    long long arrangements{0};
    long long chart_notes{0};

    long long letring_marks{0};
    long long letring_marks_on_graces{0};
    long long roll_beats{0};
    long long vibrato_narrow{0};
    long long vibrato_wide{0};
    long long built_vibrato_narrow{0};
    long long built_vibrato_wide{0};
    long long staccato_notes{0};
    long long section_marks{0};
    long long section_marks_named{0};
    long long grace_beats_skipped{0};
    long long overfull_beats_skipped{0};
    long long denominator_changes{0};

    // [D6]
    long long d6_staccato_then_legato{0};
    long long d6_rhythmically_adjacent{0};
    long long d6_resolves_unjustified{0};

    // LAW I — the let-ring stop reasons and what the rings they produce cross.
    long long rings{0};
    long long stop_strike{0};
    long long stop_rest{0};
    long long stop_cap{0};
    long long stop_score_end{0};
    long long marker_crossings{0};
    long long marker_crossings_strike{0};
    long long marker_crossings_rest{0};
    long long marker_crossings_cap{0};
    long long marker_crossings_score_end{0};
    long long named_marker_crossings{0};
    long long named_marker_crossings_cap{0};
    Samples marker_bleed_whole;
    // Selective marking: does the transcriber take the mark OFF individual notes inside a passage
    // — releasing one string to hammer on it — or paint whole passages blindly? Per-note intent
    // and blanket paint read the same in the file and mean opposite things about how much the
    // mark can be trusted to state a hold.
    long long selective_regions_fully_marked{0};
    long long selective_regions_with_unmarked{0};
    long long selective_beats_in_regions{0};
    long long selective_mixed_beats{0};
    long long selective_marked_notes{0};
    long long selective_unmarked_notes{0};
    long long selective_unmarked_legato_destination{0};
    long long selective_unmarked_legato_origin{0};
    long long selective_unmarked_plain{0};
    // Unmarking EVERY note of a beat ends a region rather than sitting inside one, so a single
    // released note shows up as a short unmarked gap between two regions of the same voice.
    Histogram region_gap_sounding_beats;
    long long region_gaps_with_rest{0};
    long long region_gaps_short{0};
    long long region_gap_short_notes{0};
    long long region_gap_short_legato{0};

    long long letring_regions{0};
    long long live_region_straddles{0};
    long long named_live_region_straddles{0};
    long long region_end_overshoots{0};
    Histogram overshoot_by_run_length;
    Histogram overshoot_by_region_marks;
    long long next_region_start_crossings{0};
    long long cap_past_region_end_no_marker{0};
    long long rest_stops_other_voice_sounding{0};
    Samples cap_ring_seconds;

    DerivationCounters derivation;
};

[[nodiscard]] std::optional<std::string> readScoreXml(const std::filesystem::path& gp_file)
{
    juce::ZipFile archive{common::core::juceFileFromPath(gp_file)};
    const int entry_index = archive.getIndexOfFileName("Content/score.gpif");
    if (entry_index < 0)
    {
        return std::nullopt;
    }
    const std::unique_ptr<juce::InputStream> stream{archive.createStreamForEntry(entry_index)};
    if (stream == nullptr)
    {
        return std::nullopt;
    }
    juce::MemoryBlock contents;
    stream->readIntoMemoryBlock(contents);
    return contents.toString().toStdString();
}

// What one score's Guitar Pro side hands to its built charts: the [D6] successors whose legato
// claim sits behind a halved staccato ring.
struct ScoreWalk
{
    std::vector<std::set<NoteKey>> d6_successors;
};

[[nodiscard]] ScoreWalk walkScore(const GpScore& score, const TempoMap& tempo_map, Census& census)
{
    const MeasureTable table = makeMeasureTable(score);
    ScoreWalk walk;
    walk.d6_successors.resize(score.tracks.size());

    // Section marks on the absolute whole-note axis; a mark sits on its master bar's downbeat.
    // TWO populations, because the corpus makes them wildly different questions: every bar the
    // score MARKS, and the subset carrying a label — which is all the chart itself can store, and
    // therefore all a section-marker stop could fire on as the importer stands today.
    std::vector<Fraction> markers;
    std::vector<Fraction> named_markers;
    for (std::size_t bar = 0; bar < score.master_bars.size(); ++bar)
    {
        const std::optional<std::string>& section = score.master_bars[bar].section;
        if (section.has_value())
        {
            markers.push_back(table.start_whole[bar]);
            ++census.section_marks;
            if (!section->empty())
            {
                named_markers.push_back(table.start_whole[bar]);
                ++census.section_marks_named;
            }
        }
        if (bar > 0 && table.denominator[bar] != table.denominator[bar - 1])
        {
            ++census.denominator_changes;
        }
    }

    const auto crosses =
        [](const std::vector<Fraction>& marks, const Fraction from, const Fraction to) {
            return std::ranges::any_of(
                marks, [from, to](const Fraction& mark) { return mark > from && mark < to; });
        };

    for (std::size_t track_index = 0; track_index < score.tracks.size(); ++track_index)
    {
        ChainDiagnostics diagnostics;
        const std::vector<std::vector<ChainBeat>> chains =
            makeVoiceChains(score.tracks[track_index], table, diagnostics);
        census.grace_beats_skipped += diagnostics.graces;
        census.letring_marks_on_graces += diagnostics.grace_letring_marks;
        census.overfull_beats_skipped += diagnostics.overfull;

        for (std::size_t voice = 0; voice < chains.size(); ++voice)
        {
            const std::vector<ChainBeat>& chain = chains[voice];
            const std::vector<LetRingRegion> regions = makeLetRingRegions(chain);
            census.letring_regions += static_cast<long long>(regions.size());

            // A marker strictly INSIDE a live region is the A-vs-composed discriminator: it says
            // the author deliberately rang a let-ring passage through a section boundary, which is
            // the one thing a section-marker stop would silently truncate.
            for (const LetRingRegion& region : regions)
            {
                if (crosses(markers, region.start_whole, region.end_whole))
                {
                    ++census.live_region_straddles;
                }
                if (crosses(named_markers, region.start_whole, region.end_whole))
                {
                    ++census.named_live_region_straddles;
                }
            }

            // ---- SELECTIVE MARKING, arm one: inside a region's own span, how much of the
            // passage the transcriber left UNMARKED. A marked beat carrying unmarked notes is
            // per-MEMBER selectivity — the mark aimed at one string and not its neighbours — and
            // that is what says the mark is a per-note statement rather than a painted block.
            for (const LetRingRegion& region : regions)
            {
                long long unmarked_in_region = 0;
                for (std::size_t beat_index = region.first; beat_index <= region.last; ++beat_index)
                {
                    ++census.selective_beats_in_regions;
                    long long marked_here = 0;
                    long long unmarked_here = 0;
                    for (const GpNote& note : chain[beat_index].beat->notes)
                    {
                        if (note.let_ring)
                        {
                            ++marked_here;
                            continue;
                        }
                        ++unmarked_here;
                        if (claimsLegato(note))
                        {
                            ++census.selective_unmarked_legato_destination;
                        }
                        else if (originOfLegato(chain, beat_index, note.string))
                        {
                            ++census.selective_unmarked_legato_origin;
                        }
                        else
                        {
                            ++census.selective_unmarked_plain;
                        }
                    }
                    census.selective_marked_notes += marked_here;
                    census.selective_unmarked_notes += unmarked_here;
                    unmarked_in_region += unmarked_here;
                    if (marked_here > 0 && unmarked_here > 0)
                    {
                        ++census.selective_mixed_beats;
                    }
                }
                if (unmarked_in_region > 0)
                {
                    ++census.selective_regions_with_unmarked;
                }
                else
                {
                    ++census.selective_regions_fully_marked;
                }
            }

            // ---- SELECTIVE MARKING, arm two: the other shape it takes. Unmarking EVERY note of
            // a beat ENDS the region rather than sitting inside one, so a released single note
            // appears as a short unmarked gap between two regions and arm one cannot see it at
            // all. A gap holding a REST is the passage genuinely stopping, so it is separated out
            // rather than counted as selectivity.
            for (std::size_t region_index = 0; region_index + 1 < regions.size(); ++region_index)
            {
                long long sounding_beats = 0;
                long long gap_notes = 0;
                long long gap_legato = 0;
                bool holds_rest = false;
                for (std::size_t beat_index = regions[region_index].last + 1;
                     beat_index < regions[region_index + 1].first;
                     ++beat_index)
                {
                    if (chain[beat_index].rest)
                    {
                        holds_rest = true;
                        continue;
                    }
                    ++sounding_beats;
                    for (const GpNote& note : chain[beat_index].beat->notes)
                    {
                        ++gap_notes;
                        gap_legato +=
                            claimsLegato(note) || originOfLegato(chain, beat_index, note.string)
                                ? 1
                                : 0;
                    }
                }
                if (holds_rest)
                {
                    ++census.region_gaps_with_rest;
                    continue;
                }
                census.region_gap_sounding_beats.add(sounding_beats);
                if (sounding_beats >= 1 && sounding_beats <= 2)
                {
                    ++census.region_gaps_short;
                    census.region_gap_short_notes += gap_notes;
                    census.region_gap_short_legato += gap_legato;
                }
            }

            for (std::size_t index = 0; index < chain.size(); ++index)
            {
                const GpBeat& beat = *chain[index].beat;
                census.roll_beats += beat.roll_direction == GpRollDirection::None ? 0 : 1;
                const std::size_t region_index = regionContaining(regions, index);

                for (const GpNote& note : beat.notes)
                {
                    census.vibrato_narrow +=
                        note.vibrato == common::core::VibratoState::Narrow ? 1 : 0;
                    census.vibrato_wide += note.vibrato == common::core::VibratoState::Wide ? 1 : 0;
                    census.staccato_notes += note.staccato ? 1 : 0;

                    // ---- [D6]: a staccato note whose next same-string neighbour in this voice
                    // claims a legato connection. The halving is what can take the adjacency the
                    // claim needs away, so the RHYTHMICALLY ADJACENT subset is the population the
                    // ruling names.
                    if (note.staccato)
                    {
                        for (std::size_t ahead = index + 1; ahead < chain.size(); ++ahead)
                        {
                            const GpNote* const successor =
                                noteOnString(*chain[ahead].beat, note.string);
                            if (successor == nullptr)
                            {
                                continue;
                            }
                            if (successor->hopo_destination || successor->left_hand_tapped)
                            {
                                ++census.d6_staccato_then_legato;
                                if (chain[index].onset_whole + chain[index].duration_whole ==
                                    chain[ahead].onset_whole)
                                {
                                    ++census.d6_rhythmically_adjacent;
                                    walk.d6_successors[track_index].insert(
                                        NoteKey{
                                            .position = chain[ahead].position,
                                            .string = note.string + 1,
                                        });
                                }
                            }
                            break;
                        }
                    }

                    if (!note.let_ring)
                    {
                        continue;
                    }
                    ++census.letring_marks;

                    const LetRingRing ring = walkLetRing(chain, table, index, note.string);
                    ++census.rings;
                    switch (ring.stop)
                    {
                        case LetRingStop::Strike:
                            ++census.stop_strike;
                            break;
                        case LetRingStop::Rest:
                            ++census.stop_rest;
                            break;
                        case LetRingStop::Cap:
                            ++census.stop_cap;
                            break;
                        case LetRingStop::ScoreEnd:
                            ++census.stop_score_end;
                            break;
                    }

                    // ---- what the ring crosses. The FIRST marker inside it is the one it bleeds
                    // past; the bleed is stated in whole notes so meters compare.
                    bool crossed_marker = false;
                    for (const Fraction& marker : markers)
                    {
                        if (marker <= ring.onset_whole || marker >= ring.end_whole)
                        {
                            continue;
                        }
                        if (!crossed_marker)
                        {
                            census.marker_bleed_whole.add((ring.end_whole - marker).toDouble());
                        }
                        crossed_marker = true;
                    }
                    if (crossed_marker)
                    {
                        ++census.marker_crossings;
                        switch (ring.stop)
                        {
                            case LetRingStop::Strike:
                                ++census.marker_crossings_strike;
                                break;
                            case LetRingStop::Rest:
                                ++census.marker_crossings_rest;
                                break;
                            case LetRingStop::Cap:
                                ++census.marker_crossings_cap;
                                break;
                            case LetRingStop::ScoreEnd:
                                ++census.marker_crossings_score_end;
                                break;
                        }
                    }
                    if (crosses(named_markers, ring.onset_whole, ring.end_whole))
                    {
                        ++census.named_marker_crossings;
                        census.named_marker_crossings_cap += ring.stop == LetRingStop::Cap ? 1 : 0;
                    }

                    if (region_index < regions.size())
                    {
                        const LetRingRegion& region = regions[region_index];
                        if (ring.end_whole > region.end_whole)
                        {
                            ++census.region_end_overshoots;
                            census.overshoot_by_run_length.add(region.beats);
                            census.overshoot_by_region_marks.add(region.marks);
                            if (ring.stop == LetRingStop::Cap && !crossed_marker)
                            {
                                ++census.cap_past_region_end_no_marker;
                            }
                        }
                        if (region_index + 1 < regions.size() &&
                            ring.end_whole > regions[region_index + 1].start_whole)
                        {
                            ++census.next_region_start_crossings;
                        }
                    }

                    // ---- a REST stop is the transcriber's silence statement, but another voice
                    // may still be sounding through it, which is what makes the stop a statement
                    // about this voice alone rather than about the music.
                    if (ring.stop == LetRingStop::Rest)
                    {
                        bool other_voice_sounds = false;
                        for (std::size_t other = 0; other < chains.size(); ++other)
                        {
                            if (other == voice)
                            {
                                continue;
                            }
                            for (const ChainBeat& elsewhere : chains[other])
                            {
                                if (elsewhere.rest || elsewhere.onset_whole > ring.end_whole)
                                {
                                    continue;
                                }
                                other_voice_sounds = other_voice_sounds ||
                                                     (elsewhere.onset_whole +
                                                      elsewhere.duration_whole) > ring.end_whole;
                            }
                        }
                        census.rest_stops_other_voice_sounding += other_voice_sounds ? 1 : 0;
                    }

                    // ---- the cap is the rule's one BLIND stop, so what it costs in real time is
                    // the number the divergence candidates are weighed against.
                    if (ring.stop == LetRingStop::Cap)
                    {
                        const double onset_seconds = tempo_map.secondsAtGlobalBeatPosition(
                            beatAtWhole(table, ring.onset_whole).toDouble());
                        const double end_seconds = tempo_map.secondsAtGlobalBeatPosition(
                            beatAtWhole(table, ring.end_whole).toDouble());
                        census.cap_ring_seconds.add(end_seconds - onset_seconds);
                    }
                }
            }
        }
    }
    return walk;
}

// ---------------------------------------------------------------------------------------------
// Reporting.
// ---------------------------------------------------------------------------------------------

struct CrossCheck
{
    std::string label;
    double rig{0.0};
    // The independently recorded figure this row is checked against — a PRIOR census or a signed
    // number, never this rig's own latest output, which would turn the check into a tautology.
    // Absent for a row that is reported for context only: an expectation nobody has signed would
    // stand permanently red, and a marker that is always red stops being read.
    std::optional<double> expected{};
};

void printCrossCheck(const std::vector<CrossCheck>& rows)
{
    std::cout << "\n[8] CROSS-CHECK against the prior scratch censuses and the signed figures the\n"
              << "    let-ring import was accepted on\n"
              << "    (this rig runs the production parser and is the authority; a flagged row is\n"
              << "     a FINDING to explain, not an error to hide. A row with no expectation is\n"
              << "     reported for context and never flags.)\n";
    std::cout << "    " << std::left << std::setw(44) << "metric" << std::right << std::setw(12)
              << "rig" << std::setw(12) << "expected" << std::setw(12) << "delta" << "\n";
    for (const CrossCheck& entry : rows)
    {
        std::cout << "    " << std::left << std::setw(44) << entry.label << std::right << std::fixed
                  << std::setprecision(2) << std::setw(12) << entry.rig;
        // Bound once so the presence test and the reads are provably the same object.
        const std::optional<double>& expected = entry.expected;
        if (!expected.has_value())
        {
            std::cout << std::setw(12) << "-" << std::setw(12) << "-" << "\n";
            continue;
        }
        const double delta = entry.rig - *expected;
        const bool flagged =
            *expected > 0.0 ? std::abs(delta) > 0.10 * *expected : std::abs(delta) > 0.0;
        std::cout << std::setw(12) << *expected << std::setw(12) << delta
                  << (flagged ? "  <== FLAG" : "") << "\n";
    }
}

} // namespace

TEST_CASE("Corpus census over the local Guitar Pro corpus", "[.local-corpus]")
{
    const std::string corpus_dir =
        juce::SystemStats::getEnvironmentVariable("ROCKHERO_GP_CORPUS_DIR", "").toStdString();
    if (corpus_dir.empty())
    {
        SKIP("ROCKHERO_GP_CORPUS_DIR is not set; this census reads a local-only corpus");
    }

    Census census;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus_dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".gp")
        {
            continue;
        }
        ++census.files_found;

        const std::optional<std::string> xml = readScoreXml(entry.path());
        if (!xml.has_value())
        {
            ++census.files_skipped;
            continue;
        }
        const auto score = parseGpScore(*xml);
        if (!score.has_value())
        {
            ++census.files_skipped;
            continue;
        }
        const auto built = buildGpSong(*score);
        if (!built.has_value())
        {
            ++census.files_skipped;
            continue;
        }
        ++census.files_parsed;

        const ScoreWalk walk = walkScore(*score, built->tempo_map, census);

        for (std::size_t track = 0; track < built->arrangements.size(); ++track)
        {
            const common::core::Chart& chart = built->arrangements[track].chart;
            ++census.arrangements;
            census.chart_notes += static_cast<long long>(chart.notes.size());
            // The built-chart incidence beside the source incidence: the builder merges ties and
            // spells ornaments out, so the two populations are genuinely different numbers and a
            // prior census quoting one of them has to be read against the right one.
            for (const ChartNote& note : chart.notes)
            {
                census.built_vibrato_narrow +=
                    note.vibrato == common::core::VibratoState::Narrow ? 1 : 0;
                census.built_vibrato_wide +=
                    note.vibrato == common::core::VibratoState::Wide ? 1 : 0;
            }

            const common::core::ChartResolutions resolutions =
                common::core::chartResolutions(chart.notes, built->tempo_map);
            const std::vector<bool> arrivals = common::core::chartShapeArrivals(
                resolutions.presented_notes, resolutions.shapes, built->tempo_map);
            countDerivation(
                resolutions.connections.saved_notes,
                resolutions.presented_notes,
                resolutions.shapes,
                resolutions.postures,
                arrivals,
                built->tempo_map,
                census.derivation);

            // ---- [D6]: does the halved staccato ring actually cost the successor its claim?
            if (track < walk.d6_successors.size())
            {
                const std::set<NoteKey>& successors = walk.d6_successors[track];
                for (std::size_t note = 0; note < resolutions.connections.saved_notes.size();
                     ++note)
                {
                    const NoteKey key{
                        .position = resolutions.connections.saved_notes[note].position,
                        .string = resolutions.connections.saved_notes[note].string,
                    };
                    if (successors.contains(key) && resolutions.connections.legato[note] ==
                                                        common::core::LegatoMotion::Unjustified)
                    {
                        ++census.d6_resolves_unjustified;
                    }
                }
            }
        }
    }

    std::cout << "\n================ ROCKHERO CORPUS CENSUS (aggregates only) ================\n";

    std::cout << "\n[1] VALIDATION\n";
    std::cout << "  gp files found                          : " << census.files_found << "\n";
    std::cout << "  parsed and built                        : " << census.files_parsed << "\n";
    std::cout << "  skipped (unreadable or rejected)        : " << census.files_skipped << "\n";
    std::cout << "  arrangements built                      : " << census.arrangements << "\n";
    std::cout << "  chart notes (bare build)                : " << census.chart_notes << "\n";
    std::cout << "  let-ring marked note occurrences        : " << census.letring_marks << "\n";
    std::cout << "  ... plus, on grace beats, not walked    : " << census.letring_marks_on_graces
              << "\n";
    std::cout << "  roll beats                              : " << census.roll_beats << "\n";
    std::cout << "  vibrato narrow / wide, in the source    : " << census.vibrato_narrow << " / "
              << census.vibrato_wide << "\n";
    std::cout << "  vibrato narrow / wide, in the built chart: " << census.built_vibrato_narrow
              << " / " << census.built_vibrato_wide << "\n";
    std::cout << "  staccato marked notes                   : " << census.staccato_notes << "\n";
    std::cout << "  section marks (every marked bar)        : " << census.section_marks << "\n";
    std::cout << "  ... of those, carrying a NAME           : " << census.section_marks_named
              << "\n";
    std::cout << "  grace beats skipped by the walk         : " << census.grace_beats_skipped
              << "\n";
    std::cout << "  overfull beats skipped by the walk      : " << census.overfull_beats_skipped
              << "\n";
    std::cout << "  measures changing the denominator       : " << census.denominator_changes
              << "\n";

    std::cout << "\n[2] (ii) LONE RE-PICK — the three counters\n";
    std::cout << "  spans total                             : " << census.derivation.spans << "\n";
    std::cout << "  spans containing a lone re-pick         : " << census.derivation.ii_spans
              << "\n";
    std::cout << "  lone re-pick slots                      : " << census.derivation.ii_slots
              << "\n";
    std::cout << "    with a SOUND witness (member ringing) : "
              << census.derivation.ii_sound_witness << "\n";
    std::cout << "    with a CLAIM witness (hand-stated)    : "
              << census.derivation.ii_claim_witness << "\n";
    std::cout << "  those spans classified arpeggio today   : "
              << census.derivation.ii_spans_arpeggio << " of " << census.derivation.ii_spans
              << "\n";
    std::cout << "  ... whose re-pick sits only at the END  : "
              << census.derivation.ii_spans_end_slot_only << "\n";
    std::cout << "    of those, still a box                 : "
              << census.derivation.ii_spans_end_slot_only_boxed << "\n";

    const auto row = [](const char* label, const long long value) {
        std::cout << "  " << std::left << std::setw(42) << label << std::right << std::setw(10)
                  << value << "\n";
    };

    std::cout << "\n[3] [D4] TRIGGER 4 — a carried ring folding into a span onset\n";
    row("spans", census.derivation.spans);
    row("spans classified arpeggio", census.derivation.spans_arpeggio);
    row("trigger-4 spans", census.derivation.trigger4_spans);
    row("box -> arpeggio flips (trigger 4 alone)", census.derivation.trigger4_only_spans);
    row("fold-ins", census.derivation.trigger4_foldins);
    row("fold-ins with no struck fret to measure", census.derivation.trigger4_foldins_unmeasurable);
    std::cout << "  carried-fret distance |carried - nearest struck| (the source-hygiene proxy):\n";
    std::cout << "    " << census.derivation.carried_fret_distance.text() << "\n";
    row("fold-ins carried 5+ frets from any struck",
        census.derivation.carried_fret_distance.countAtLeast(5));

    std::cout << "\n  --- what is being carried: an OPEN string or a FRETTED note ---\n";
    row("fold-ins carrying an OPEN string", census.derivation.foldins_open);
    row("fold-ins carrying a FRETTED note", census.derivation.foldins_fretted);
    std::cout << "  open-string carry distance:\n";
    std::cout << "    " << census.derivation.carried_distance_open.text() << "\n";
    std::cout << "  FRETTED carry distance (the reach question's real population):\n";
    std::cout << "    " << census.derivation.carried_distance_fretted.text() << "\n";
    std::cout << "    spread : " << census.derivation.carried_distance_fretted_spread.summary()
              << "\n";
    row("fretted fold-ins at 5+ frets", census.derivation.carried_distance_fretted.countAtLeast(5));
    row("fretted fold-ins beyond 6 frets (7+)",
        census.derivation.carried_distance_fretted.countAtLeast(7));

    std::cout << "\n  --- fretted carries against the shape's board position ---\n";
    std::cout << "  (position = the lowest STOPPED fret the struck members hold; frets narrow as\n"
                 "   they climb, so one fret distance is a different reach in each band)\n";
    std::cout << "    " << std::left << std::setw(20) << "shape position" << std::right
              << std::setw(10) << "fretted" << std::setw(10) << "dist >6"
              << "   distance histogram\n";
    const std::vector<const char*> position_names{
        "low  (frets 0-6)",
        "mid  (frets 7-11)",
        "high (frets 12+)",
    };
    for (std::size_t bucket = 0; bucket < position_names.size(); ++bucket)
    {
        std::cout << "    " << std::left << std::setw(20) << position_names[bucket] << std::right
                  << std::setw(10) << census.derivation.fretted_by_position[bucket] << std::setw(10)
                  << census.derivation.fretted_over_six_by_position[bucket] << "   "
                  << census.derivation.fretted_distance_by_position[bucket].text() << "\n";
    }

    std::cout << "\n[4] [D3] CONTINUITY GATES\n";
    row("gap re-picks under witnesses", census.derivation.ii_gap_repicks);
    row("  of those, a SOUND witness", census.derivation.ii_gap_repicks_sound);
    row("  of those, a CLAIM witness", census.derivation.ii_gap_repicks_claim);
    row("interior-gap spans", census.derivation.interior_gap_spans);
    row("lone re-pick slots (context)", census.derivation.ii_slots);

    std::cout << "\n[5] [D2] TRAVEL AND THE LANDED GRIP\n";
    row("successor spans (the derivation's own)", census.derivation.successor_spans);
    row("  ... classified arpeggio", census.derivation.successor_spans_arpeggio);
    std::cout << "  --- the source-side reading beside it, by edge ---\n";
    row("spans a start member travels in", census.derivation.travel_any_spans);
    row("  landings that re-open", census.derivation.travel_landings_open);
    row("  suppressed: staggered (edge c)", census.derivation.travel_landings_staggered);
    row("  suppressed: no room to state (edge b)", census.derivation.travel_landings_crowded);
    std::cout << "  --- the PRE-BUILD instrument, unchanged ---\n";
    row("spans whose sounding members all travel", census.derivation.travel_spans);
    row("  ... with a breathing landing", census.derivation.travel_breathing_spans);

    std::cout << "\n[6] [D6] STACCATO -> SAME-STRING LEGATO ADJACENCY\n";
    std::cout << "  staccato marked notes                   : " << census.staccato_notes << "\n";
    std::cout << "  followed on its string by a legato claim: " << census.d6_staccato_then_legato
              << "\n";
    std::cout << "    rhythmically adjacent (halving bites) : " << census.d6_rhythmically_adjacent
              << "\n";
    std::cout << "    built successor resolves Unjustified  : " << census.d6_resolves_unjustified
              << "\n";

    std::cout << "\n[7] LET-RING as the SOURCE states it (Guitar Pro's own playback rule)\n";
    std::cout << "  rings measured                          : " << census.rings << "\n";
    std::cout << "  stop reason  strike                     : " << census.stop_strike << " ("
              << percentText(census.stop_strike, census.rings) << ")\n";
    std::cout << "               cap (the one BLIND stop)   : " << census.stop_cap << " ("
              << percentText(census.stop_cap, census.rings) << ")\n";
    std::cout << "               rest                       : " << census.stop_rest << " ("
              << percentText(census.stop_rest, census.rings) << ")\n";
    std::cout << "               score end                  : " << census.stop_score_end << " ("
              << percentText(census.stop_score_end, census.rings) << ")\n";
    std::cout << "  section-mark crossings (every mark)     : " << census.marker_crossings << "\n";
    std::cout << "    by stop reason strike/cap/rest/end    : " << census.marker_crossings_strike
              << " / " << census.marker_crossings_cap << " / " << census.marker_crossings_rest
              << " / " << census.marker_crossings_score_end << "\n";
    std::cout << "    bleed past the mark (whole notes)     : "
              << census.marker_bleed_whole.summary() << "\n";
    std::cout << "  crossings of NAMED marks only           : " << census.named_marker_crossings
              << " (cap-stopped: " << census.named_marker_crossings_cap << ")\n";
    std::cout << "  let-ring regions (marked runs)          : " << census.letring_regions << "\n";
    std::cout << "  LIVE-REGION mark straddles              : " << census.live_region_straddles
              << " (named only: " << census.named_live_region_straddles
              << ")  <- the A-vs-composed discriminator\n";
    std::cout << "  region-end overshoots                   : " << census.region_end_overshoots
              << "\n";
    std::cout << "    by region run length in beats         : "
              << census.overshoot_by_run_length.text() << "\n";
    std::cout << "    by let-ring marks in the region       : "
              << census.overshoot_by_region_marks.text() << "\n";
    std::cout << "  next-region-start crossings             : "
              << census.next_region_start_crossings << "\n";
    std::cout << "  cap rings past region end, no marker    : "
              << census.cap_past_region_end_no_marker << "\n";
    std::cout << "  rest stops with another voice sounding  : "
              << census.rest_stops_other_voice_sounding << " of " << census.stop_rest << "\n";
    std::cout << "  cap-ring wall clock (seconds)           : " << census.cap_ring_seconds.summary()
              << "\n";

    const long long notes_in_regions =
        census.selective_marked_notes + census.selective_unmarked_notes;
    std::cout << "\n  --- SELECTIVE MARKING inside let-ring passages ---\n";
    std::cout << "  (per-note intent vs blanket paint: how much of a marked passage the\n"
                 "   transcriber deliberately left unmarked)\n";
    std::cout << "  regions fully marked                    : "
              << census.selective_regions_fully_marked << " of " << census.letring_regions << "\n";
    std::cout << "  regions holding an UNMARKED sounding note: "
              << census.selective_regions_with_unmarked << " ("
              << percentText(census.selective_regions_with_unmarked, census.letring_regions)
              << ")\n";
    std::cout << "  notes on beats inside regions, marked    : " << census.selective_marked_notes
              << "\n";
    std::cout << "                                 unmarked  : " << census.selective_unmarked_notes
              << " (" << percentText(census.selective_unmarked_notes, notes_in_regions)
              << " of the passage)\n";
    std::cout << "  beats inside regions                     : "
              << census.selective_beats_in_regions << "\n";
    std::cout << "    MIXED beats (some marked, some not)    : " << census.selective_mixed_beats
              << " ("
              << percentText(census.selective_mixed_beats, census.selective_beats_in_regions)
              << ")\n";
    std::cout << "  unmarked-inside notes by kind, a legato DESTINATION : "
              << census.selective_unmarked_legato_destination << "\n";
    std::cout << "                                 a legato ORIGIN      : "
              << census.selective_unmarked_legato_origin << "\n";
    std::cout << "                                 plain                : "
              << census.selective_unmarked_plain << "\n";
    std::cout << "  whole-beat unmarking, seen as gaps BETWEEN regions of one voice:\n";
    std::cout << "    gap length in sounding beats           : "
              << census.region_gap_sounding_beats.text() << "\n";
    std::cout << "    gaps holding a rest (passage ended)    : " << census.region_gaps_with_rest
              << "\n";
    std::cout << "    short rest-free gaps of 1-2 beats      : " << census.region_gaps_short
              << "\n";
    std::cout << "      notes in them / of those legato      : " << census.region_gap_short_notes
              << " / " << census.region_gap_short_legato << "\n";

    printCrossCheck(
        std::vector<CrossCheck>{
            CrossCheck{
                .label = "files parsed",
                .rig = static_cast<double>(census.files_parsed),
                .expected = 113.0,
            },
            CrossCheck{
                .label = "files skipped",
                .rig = static_cast<double>(census.files_skipped),
                .expected = 2.0,
            },
            CrossCheck{
                // The ruleset quotes 12098 from the FIRST scratch census. This rig reads 11849,
                // and that census recorded the whole of the difference rather than leaving it
                // open: 226 marks in repeat files, and 23 on GRACE beats, which take no bar time
                // and are passed over by this walk and by the emission alike (the grace figure is
                // re-measured every run, two lines up in section [1]).
                .label = "let-ring marked notes (12098 - 226 - 23)",
                .rig = static_cast<double>(census.letring_marks),
                .expected = 11849.0,
            },
            CrossCheck{
                .label = "roll beats",
                .rig = static_cast<double>(census.roll_beats),
                .expected = 3.0,
            },
            CrossCheck{
                .label = "vibrato narrow",
                .rig = static_cast<double>(census.vibrato_narrow),
                .expected = 318.0,
            },
            CrossCheck{
                .label = "vibrato wide",
                .rig = static_cast<double>(census.vibrato_wide),
                .expected = 10.0,
            },
            CrossCheck{
                // ADJUSTED at the [D3] build (THE CONTINUITY LAW, 2026-08-27), never re-quoted
                // from the rig: an expectation copied off this rig's own output would check
                // nothing. Two components, named so the row can be re-derived — the SIGNED 22015,
                // plus the +1340 the law itself predicts, because a span now ends at the first
                // genuine stored gap on any sounding member and every chain that used to merge
                // across a gap is two statements instead of one.
                //
                // The standing -2 rides along NUMERICALLY: the rig measured 22013 against the
                // signed 22015 before the law, nobody has explained it, and adding it into the
                // expectation would hide it. It stays visible as this row's own delta instead.
                //
                // NOT re-quoted at the [D2] build either (travel splits and the landed grip,
                // 2026-08-28), and for the same reason: the only figure that would close the gap
                // is this rig's own successor count. The delta carries two named components on
                // top of the standing -2 — +779 landing successors (section [5], and every one of
                // them a span the model did not have before), and +10 spans where a lone re-pick
                // that used to ride a travelling shape can no longer do so, the statement having
                // ended at its departure. The (ii) row below moves by exactly that -10.
                //
                // A THIRD component joined at the F1 fix (2026-08-29): +6. A carried ring now
                // folds into a posture at the stop its own fret channel states THERE, so two
                // slots either side of a hand move no longer present identical articulations and
                // no longer merge — six shapes that used to span a slide are two statements each.
                // The [D4] histogram in section [3] is the same fix seen from the other end: 18
                // fretted carries moved 8 frets further from the chord they cross, because the
                // fret being measured is now the one the finger reached.
                .label = "spans",
                .rig = static_cast<double>(census.derivation.spans),
                .expected = 23355.0,
            },
            CrossCheck{
                // NOT re-quoted at the B6c build (LAW III's CLASS rule, interior arm, 2026-08-28).
                // 736 is the last INDEPENDENT figure this row has — the 738 signed before [D3],
                // plus that law's predicted -2, the two spans whose arrival flag depended on a span
                // reaching content past its own first gap — and it stands until someone signs a
                // post-let-ring one.
                //
                // So the row FLAGS, and the flag is the finding this column exists to surface
                // rather than an error to hide. Three deltas ride inside it, every one of them
                // explained and none folded away:
                //
                //   -2  the standing [D3] figure above;
                //   -7  spans that stop short of an interior tap, now that a tap cannot write a
                //       span's chain;
                //   +126 LAW III's class rule asked of a span's INTERIOR — a partial restrike or a
                //       lone re-pick inside a span is its members sounding SEPARATELY, so the span
                //       is an arpeggio. That is the ruling's own intent landing, not a side effect.
                //
                // The ruling predicted +188 for that last one ("flips 188 corpus spans, arpeggios
                // 37 -> 225"), and the gap is population drift rather than disagreement: 188 was
                // counted on the pre-let-ring, pre-continuity corpus — the same drift the
                // lone-re-pick row below carries in its own label. Adding it to the expectation
                // would have silenced this row by construction, a whole-corpus figure plus a
                // sub-population one, which is exactly what a cross-check must never be made of.
                //
                // A FOURTH delta was expected here and measured ZERO: the classification-stream
                // ruling (user 2026-08-28) moved trigger (a) — a posture string carried into a
                // span's start — off the PRESENTED ring and onto the stored one the walk's fold-in
                // has always read, unifying it with the delta above into one comparison. Every one
                // of these spans classifies the same either way, which is the finding: the two
                // readings part only where a DEAD string's ring is carried across a chord's onset
                // (E25 takes that tail off the drawn form and not off the stored one), and this
                // corpus holds no such figure. The ruling changes what the rule MEANS and what a
                // charter can author into it; it changes no imported chart today.
                //
                // A FIFTH delta joined at the [D2] build (2026-08-28): +773. Its two parts are
                // +779 landing successors, every one an arpeggio by construction (the row two
                // below is that law's own discriminator and reads zero), and -6 among the spans
                // that already existed — a span the travel shortened to its departure can stop
                // covering the interior slot that flipped it, a right-hand onset for trigger (d)
                // or a partial restrike for (c). That -6 is read off this arithmetic rather than
                // counted separately, and it is stated here so nobody mistakes it for drift.
                .label = "arpeggio spans",
                .rig = static_cast<double>(census.derivation.spans_arpeggio),
                .expected = 736.0,
            },
            CrossCheck{
                .label = "box -> arpeggio flips (trigger 4 alone)",
                .rig = static_cast<double>(census.derivation.trigger4_only_spans),
                .expected = 727.0,
            },
            CrossCheck{
                // STILL the one derived row nobody has signed a post-let-ring figure for: the
                // earlier censuses only ever printed the pre-let-ring 188, the import moved it to
                // roughly 297, and the (ii) narrowing has since taken it from 313 to 305. Quoting
                // that 305 would only quote the rig back to itself, which is the one thing this
                // column may never hold — so the figure stays in the label, where it reads as
                // context, and the row keeps reporting without an expectation until someone signs
                // one. A row that is red on purpose every run teaches the reader to ignore the
                // marker. (The label stays inside the report's 44-column metric field.)
                // [D2] then took it from 305 to 295: a re-pick cannot ride a shape the hand has
                // already travelled out of, and those ten spans are the +10 in the `spans` row.
                .label = "lone re-pick spans (188 / ~297 / 305 / 295)",
                .rig = static_cast<double>(census.derivation.ii_spans),
                .expected = std::nullopt,
            },
            CrossCheck{
                // [D2] built 2026-08-28. Nobody has signed a figure for the successor population:
                // the only prior number is this rig's own PRE-BUILD instrument (915 spans whose
                // members all travel with a breathing landing), which measured neither of the two
                // edges the ruling then suppressed, so quoting it would stand permanently red for
                // a reason the report already explains in section [5]. The row reports without an
                // expectation until someone signs one, exactly as the lone-re-pick row above does.
                .label = "landing successor spans (pre-build est. 915)",
                .rig = static_cast<double>(census.derivation.successor_spans),
                .expected = std::nullopt,
            },
            CrossCheck{
                // The class law's own discriminator, and the one D2 row with a figure that is not
                // a measurement: EVERY successor strikes nothing of a shape that sounds two or
                // more strings, so every one of them arrives an arpeggio by construction. A
                // non-zero delta here means the successor arm and LAW III have come apart.
                .label = "  successors NOT classified arpeggio",
                .rig = static_cast<double>(
                    census.derivation.successor_spans - census.derivation.successor_spans_arpeggio),
                .expected = 0.0,
            },
            CrossCheck{
                .label = "let-ring stop: strike %",
                .rig = sharePercent(census.stop_strike, census.rings),
                .expected = 90.9,
            },
            CrossCheck{
                .label = "let-ring stop: cap %",
                .rig = sharePercent(census.stop_cap, census.rings),
                .expected = 8.1,
            },
            CrossCheck{
                .label = "let-ring stop: rest %",
                .rig = sharePercent(census.stop_rest, census.rings),
                .expected = 0.9,
            },
            CrossCheck{
                .label = "let-ring stop: score end %",
                .rig = sharePercent(census.stop_score_end, census.rings),
                .expected = 0.2,
            },
            CrossCheck{
                .label = "section-marker crossings",
                .rig = static_cast<double>(census.marker_crossings),
                .expected = 125.0,
            },
            CrossCheck{
                .label = "  of those, blind-cap stops",
                .rig = static_cast<double>(census.marker_crossings_cap),
                .expected = 63.0,
            },
            CrossCheck{
                .label = "  median bleed (whole notes)",
                .rig = census.marker_bleed_whole.median(),
                .expected = 0.56,
            },
            CrossCheck{
                .label = "live-region marker straddles",
                .rig = static_cast<double>(census.live_region_straddles),
                .expected = 0.0,
            },
            CrossCheck{
                .label = "region-end overshoots",
                .rig = static_cast<double>(census.region_end_overshoots),
                .expected = 997.0,
            },
            CrossCheck{
                .label = "  of those, isolated regions",
                .rig = static_cast<double>(census.overshoot_by_run_length.at(1)),
                .expected = 380.0,
            },
            CrossCheck{
                .label = "next-region-start crossings",
                .rig = static_cast<double>(census.next_region_start_crossings),
                .expected = 483.0,
            },
            CrossCheck{
                .label = "cap rings past region end, no marker",
                .rig = static_cast<double>(census.cap_past_region_end_no_marker),
                .expected = 283.0,
            },
            CrossCheck{
                .label = "rest stops total",
                .rig = static_cast<double>(census.stop_rest),
                .expected = 102.0,
            },
            CrossCheck{
                .label = "  of those, another voice sounding",
                .rig = static_cast<double>(census.rest_stops_other_voice_sounding),
                .expected = 42.0,
            },
        });
    std::cout << "\n=========================================================================\n";
    std::cout.flush();

    CHECK(census.files_parsed > 0);
}

} // namespace rock_hero::editor::core
