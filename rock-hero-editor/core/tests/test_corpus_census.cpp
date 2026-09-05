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

// [D4]'s fold-in reach, kept as ONE family so it can be measured TWICE (Q8, user ruling
// 2026-08-31). The two populations answer different questions and pooling them answered neither:
// a fold-in an EVENT-opened span absorbs is the SOURCE-HYGIENE population, where the carried
// finger's distance from the fingers the chord actually put down says whether a hand could have
// held both at once; a fold-in inside a LANDING SUCCESSOR is every member that span has, by
// construction, so its distances measure the arm's own shape and never a reach. The successor arm
// grew a second cause on 2026-08-31 (a member's death beside the landing), which is what swamped
// the pooled histograms and made the hygiene question unreadable; the grip-tenure law deleted that
// cause again on 2026-09-04, and the split stays because the landing arm alone was always the
// unreadable one.
//
// Spelled once and instantiated twice rather than written out per arm: two field families that
// must agree by hand are the defect this rig exists to report on.
struct FoldInReach
{
    long long foldins{0};
    long long unmeasurable{0};
    Histogram carried_fret_distance;

    // The same distances split by WHAT is carried, because the two are different physical claims
    // and only one of them is a reach question at all: an OPEN string is a voicing member no
    // finger holds, so no distance from it means anything about the hand, while a FRETTED carry
    // asserts that a finger stayed down while the chord was struck somewhere else.
    long long open{0};
    long long fretted{0};
    Histogram carried_distance_open;
    Histogram carried_distance_fretted;
    Samples carried_distance_fretted_spread;

    // Fretted carries against the board POSITION of the shape they cross, because how far a hand
    // can span is not one constant: the frets narrow as they climb, so the same fret distance is
    // a different reach at the nut and at the twelfth.
    std::vector<long long> fretted_by_position{0, 0, 0};
    std::vector<long long> fretted_over_six_by_position{0, 0, 0};
    std::vector<Histogram> fretted_distance_by_position = std::vector<Histogram>(3);
};

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
    // sitting exactly on the span's own end is where the two readings part: since the span stores
    // its MUSICAL CLOSE (user ruling 2026-09-04), the onset that CLOSED it sits exactly there —
    // and so does the last strum the statement rode wherever its own ring is what ran out. The
    // first kind is no continuation at all, and no window over a finished span can tell them apart.
    long long ii_spans_end_slot_only{0};
    long long ii_spans_end_slot_only_boxed{0};

    // [D3] — the continuity gates.
    long long ii_gap_repicks{0};
    long long ii_gap_repicks_sound{0};
    long long ii_gap_repicks_claim{0};
    long long interior_gap_spans{0};

    // [D4] — trigger 4, a carried ring folding into a span onset. The reach families are split by
    // WHICH ARM absorbed the fold-in (\ref FoldInReach), so the source-hygiene population is a
    // population again.
    long long trigger4_spans{0};
    long long trigger4_only_spans{0};
    FoldInReach foldins_event;
    FoldInReach foldins_successor;

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

    // [D2] AMENDED — the landing split. `zero_length_spans` is the amendment's own headline
    // population (it promises there are none left), `travel_covering_spans` is what replaced them
    // (a span whose extent reaches the grip its members are travelling to), and
    // `travel_landings_absorbed` is review F9's population: a landing the SOURCE side says should
    // re-open, with no successor span standing at that instant.
    long long zero_length_spans{0};
    long long travel_covering_spans{0};
    long long travel_landings_absorbed{0};

    // A landing the span it belongs to never REACHED: some other statement closed the travelling
    // span before the hand arrived. Under the departure split that was every landing; under the
    // landing split it is the whole population a walk-side hand-off would have had to remember,
    // and the successors those landings would have opened are the price of not remembering. Once
    // a mid-travel sounding is judged per MEMBER it reads zero in this corpus — every travelling
    // span reaches its own landing — which is what makes that price nothing.
    long long travel_landings_outrun{0};

    // Successors whose start falls ON a note slot, which is the blindness review F8 named: the
    // old reading called a span a successor only when NO slot sat at its position, so every one
    // of these was counted as an ordinary span.
    long long successor_spans_at_slot{0};

    // THE SOURCE-SIDE READING of the same population, kept as the rig's independent second
    // opinion: a boundary is a LANDING where some ring crossing it arrives at its resting stop
    // exactly there, read off the fret channels rather than off the walk.
    //
    // ONE CAUSE NOW (grip-tenure law rule 7, user-signed 2026-09-04): ring-out opens NOTHING, so
    // the member's-DEATH boundary the one-authority gate admitted on 2026-08-31 is deleted with
    // the concept. `landing_opened` therefore carries exactly one cause and this row is no longer
    // a SPLIT of the successor population but a CONVERGENCE check on it — every span the walk
    // opened should be a landing the source side can also see, and the remainder is the rig's own
    // attribution shortfall rather than a second cause.
    long long successor_spans_landing{0};
    long long successor_spans_landing_box{0};

    // THE ONE-COUNT OPENING LAW's population, AS FAR AS PUBLISHED DATA REACHES (user ruling
    // 2026-08-31, review #2, narrowed by review #5): a span whose FRONT slot sounds nothing with
    // the fretting hand. The carry fold-in used to be gated on a strike, so such a slot could only
    // ever open on its own records; a ring crossing it is now a member like any other, and a slot
    // stating a LONE record is exactly the shape that could not open before and can now.
    //
    // NAMED FOR THE FRONT, not for the opening, because the front is the only slot a reader can
    // find. THE DATING RULE backdates a span to its earliest UNCOVERED member onset, and a member
    // that backdates is always a ringing fretting-hand onset — so a span opened at a strike-less
    // slot that folded in an uncovered ring is DATED at that ring's own strike and reads as struck
    // here. The row therefore counts the half of the strike-less openings whose carried members
    // were all COVERED (or that carried none at all), and cannot see the other half.
    //
    // That leaves the R-B ruling only half priced, and the missing half is not recoverable from
    // what the walk publishes: the opening slot is the walk's own, it is not a field on
    // \ref common::core::ChartShape, and every reconstruction of it here would be this rig
    // re-deriving the opening law it exists to measure — the substitution the `landing_opened`
    // header forbids. Measuring it needs the walk to publish the slot it opened at.
    long long strikeless_front_spans{0};
    long long strikeless_front_lone_record{0};

    // THE SIX FOUNDING COUNTERS ARE DELETED WITH THEIR SUBJECT (grip-tenure law, user-signed
    // 2026-09-04, "`founding` is DELETED outright, and its six census counters delete with their
    // subject"). They split every span into STATEMENT- and ACCUMULATION-founded off
    // `ChartShape::founding`, and there is no founding classification in the law at all now: the
    // opening test is one disjunction (`own >= 2 || total >= 3`) that names no mode, so a
    // reconstruction here would be this rig inventing a concept the model dropped. The ONE honest
    // opening-cause key that survives is `landing_opened`, censused in section [5].

    // THE DATING RULE's invariant, and the whole of what it was ruled to fix: a span dates from
    // its earliest member onset NOT COVERED by a preceding span, so no span may start before the
    // one before it ended. This counts the violations; the rule promises zero.
    long long overlapping_spans{0};

    // THE FHP CONVERGENCE INVARIANT (user ruling 2026-08-31): FHP is the POSITION story and spans
    // are the GRIP story, never merged — so where they disagree, one of them is describing a hand
    // that cannot exist. Every FRETTED stop a span's posture holds should lie inside the reach of
    // the fret-hand window covering that span's start ([fret, fret + width - 1]). Open strings are
    // excluded: a 0 is a voicing member no finger holds.
    //
    // REPORTED, NEVER ENFORCED. The two derivations are independent by design and this is the
    // instrument that says whether they agree; making it a rule would give one of them authority
    // over the other, which is exactly the merge the ruling refused.
    long long fhp_checked_spans{0};
    long long fhp_out_of_reach_spans{0};
    long long fhp_out_of_reach_stops{0};
    Histogram fhp_reach_overshoot;

    // THE HAND-COUPLING GATE (user law 2026-09-05: "a span should not exist across an FHP shift
    // unless a slide carries it"). Two rows that are one measurement serving both directions of
    // the ruled pipeline: a window ARRIVING strictly inside a span's run is either a span the
    // planned coupling would split or a window the generator should not have moved (the sighted
    // mid-figure shift), and a window arriving where nothing fretted sounds is a hand told to
    // move with nothing to move for (the sighted lone-open class). Slide carriage is not
    // subtracted — the aggregate is the refinement's ceiling, and the slide subpopulation is the
    // first split to make when the number matters.
    long long spans_crossed_by_fhp_shift{0};
    long long fhp_shifts_inside_spans{0};
    long long fhp_placements{0};
    long long fhp_placements_unfretted{0};
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

// WHICH SPANS ARE SUCCESSORS is read straight off \ref common::core::ChartShape::landing_opened,
// which the walk publishes for exactly this reason — and the field's own header says no reader may
// substitute a test of its own for it, this rig included. Renamed from `carry_opened` with the
// grip-tenure law (2026-09-04) and narrowed to its one surviving cause: a LANDED TRAVEL is the
// only onset-less open the law admits, so the field is now named for the whole of what it means.
//
// The proxy that stood here ("no note at the span's position is a member of its posture") was
// exactly such a substitute. It agreed with the field only by construction and could not survive
// the arm changing under it, which is the failure mode the rig exists to catch rather than to
// reproduce: an instrument that re-derives its subject stops being able to disagree with it.
void countDerivation(
    const std::vector<ChartNote>& saved, const std::vector<ChartNote>& presented,
    const std::vector<ChartShape>& shapes, const std::vector<ChartPosture>& postures,
    const std::vector<bool>& arrivals,
    const std::vector<common::core::FretHandPosition>& hand_positions, const TempoMap& tempo_map,
    DerivationCounters& out)
{
    const StreamIndex index = makeStreamIndex(saved, tempo_map);
    constexpr auto string_count = static_cast<std::size_t>(common::core::g_max_chart_strings);

    // The fret-hand windows on the same beat axis the spans are read on, so the convergence
    // invariant compares two derivations rather than two coordinate systems. Ascending by
    // construction (the chart stores them sorted by position), so the window covering an instant
    // is the last one at or before it.
    std::vector<Fraction> hand_position_beats;
    hand_position_beats.reserve(hand_positions.size());
    for (const common::core::FretHandPosition& window : hand_positions)
    {
        hand_position_beats.push_back(
            common::core::beatDistance(tempo_map, GridPosition{}, window.position));
    }
    const auto window_covering = [&hand_positions, &hand_position_beats](
                                     const Fraction beat) -> const common::core::FretHandPosition* {
        const auto after = std::ranges::upper_bound(hand_position_beats, beat);
        if (after == hand_position_beats.begin())
        {
            return nullptr;
        }
        return &hand_positions[static_cast<std::size_t>(
            std::distance(hand_position_beats.begin(), after) - 1)];
    };

    // THE HAND-COUPLING GATE's placement classes, read once per track: which windows arrive
    // where nothing fretted sounds (a nonzero fret is fretted; an open string moves no finger
    // to the window's post).
    {
        std::map<Fraction, std::pair<bool, bool>> onsets; // beat -> {any note, any fretted}
        for (const ChartNote& note : saved)
        {
            const Fraction beat =
                common::core::beatDistance(tempo_map, GridPosition{}, note.position);
            auto& [any, fretted] = onsets[beat];
            any = true;
            fretted = fretted || note.fret != 0;
        }
        out.fhp_placements += static_cast<long long>(hand_position_beats.size());
        for (const Fraction& beat : hand_position_beats)
        {
            const auto at = onsets.find(beat);
            if (at != onsets.end() && at->second.first && !at->second.second)
            {
                ++out.fhp_placements_unfretted;
            }
        }
    }

    // THE DATING RULE's frontier, walked beside the spans: how far the spans already read cover
    // the axis. A span starting behind it is the overlap the rule forbids.
    Fraction covered_through{};

    // Where the derivation's successors actually stand, so the travel reading below can ask
    // whether the landing it just attributed re-opened or was ABSORBED (review F9). Collected
    // ahead of the walk because the landing is read from the span the travel STARTS in, which the
    // loop reaches before the successor it produced.
    std::set<Fraction> successor_starts;
    for (const ChartShape& shape : shapes)
    {
        if (shape.landing_opened)
        {
            successor_starts.insert(
                common::core::beatDistance(tempo_map, GridPosition{}, shape.position));
        }
    }

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

        // THE DATING RULE: the ruled promise is that no span starts inside the one before it.
        out.overlapping_spans += start < covered_through ? 1 : 0;
        covered_through = std::max(covered_through, end);

        // THE HAND-COUPLING GATE: windows arriving strictly inside this span's run.
        {
            const auto first_inside = std::ranges::upper_bound(hand_position_beats, start);
            const auto past_run = std::ranges::lower_bound(hand_position_beats, end);
            const auto inside = std::distance(first_inside, past_run);
            if (inside > 0)
            {
                ++out.spans_crossed_by_fhp_shift;
                out.fhp_shifts_inside_spans += static_cast<long long>(inside);
            }
        }

        // THE FHP CONVERGENCE INVARIANT: reported, never enforced.
        if (const common::core::FretHandPosition* const window = window_covering(start);
            window != nullptr)
        {
            ++out.fhp_checked_spans;
            long long out_of_reach = 0;
            for (const std::optional<int>& fret : posture)
            {
                if (!fret.has_value() || *fret == 0)
                {
                    continue;
                }
                const int low = window->fret;
                const int high = window->fret + window->width - 1;
                if (*fret >= low && *fret <= high)
                {
                    continue;
                }
                ++out_of_reach;
                out.fhp_reach_overshoot.add(*fret < low ? low - *fret : *fret - high);
            }
            out.fhp_out_of_reach_stops += out_of_reach;
            out.fhp_out_of_reach_spans += out_of_reach > 0 ? 1 : 0;
        }

        // The span's own opening slot: who strikes there, which strings sound at all, and the
        // articulation each sounding member states — the identity a re-pick has to repeat.
        std::vector<std::size_t> struck_at_start;
        std::set<int> sounded_strings;
        const auto opening = index.slot_of.find(shape.position);
        const bool successor = shape.landing_opened;
        if (successor)
        {
            // [D2]: the landing successor is the ONE span the model opens where nothing STATES it,
            // read from the walk's own published mark. The class count beside it is no longer an
            // equality pin: since the 2026-08-30 ruling a landing is NOT a sounding, so a
            // successor classifies by the ordinary triggers like every other span and the split
            // between boxes and brackets is a real measurement of what the corpus's chord slides
            // actually land in.
            ++out.successor_spans;
            out.successor_spans_arpeggio += arpeggio ? 1 : 0;
            out.successor_spans_at_slot += opening != index.slot_of.end() ? 1 : 0;

            // THE SOURCE-SIDE READING of the same opening (user ruling 2026-08-31, review #8): a
            // LANDING is a ring crossing this instant whose fret channel comes to rest exactly
            // here, which is the hand ARRIVING. Read per string off the last note before the
            // front, because that is the record whose chain crosses.
            //
            // A CONVERGENCE CHECK NOW, not a cause split (grip-tenure law rule 7, 2026-09-04):
            // ring-out opens nothing, so a member's DEATH is no longer a boundary that can open
            // anything and every span counted above should be a landing this reading also sees.
            // The rows that do not converge are the rig's attribution shortfall.
            //
            // Scanned over the PREDECESSOR's MEMBER strings and no others, and only where that
            // member's ring actually CROSSES the boundary. Both narrowings say one thing: the
            // rings that carry a statement over a boundary are the statement's own members, so a
            // fret-channel arrival on a string this shape never held — or on a member whose ring
            // ended before the boundary — is some other figure's business and says nothing about
            // why this span opened. Scanning every string of the tuning and never asking whether
            // the ring reached here attributed a landing to any successor an unrelated string
            // happened to arrive under.
            //
            // The predecessor is the span emitted just before this one: spans never overlap (the
            // dating rule) and a successor starts exactly where its predecessor ended, so nothing
            // can stand between them.
            const std::vector<std::optional<int>>* const predecessor =
                shape_index > 0 && shapes[shape_index - 1].posture < postures.size()
                    ? &postures[shapes[shape_index - 1].posture].frets
                    : nullptr;
            bool landing_here = false;
            for (std::size_t string_index = 0;
                 predecessor != nullptr && string_index < predecessor->size() && !landing_here;
                 ++string_index)
            {
                // Bound once so the presence test provably covers the string it admits.
                const std::optional<int>& member = (*predecessor)[string_index];
                if (!member.has_value())
                {
                    continue;
                }
                const std::optional<std::size_t> crossing =
                    lastNoteBefore(index, static_cast<int>(string_index) + 1, start);
                if (!crossing.has_value())
                {
                    continue;
                }
                const std::size_t ringing = *crossing;
                // A MEMBER's ring, so a right-hand onset ends the string's answer rather than
                // giving one: a tap joins no posture, and its own onset already clamped the
                // member's stored ring under it, so nothing of the member crosses past a tap.
                if (!soundsWithFrettingHand(saved[ringing]))
                {
                    continue;
                }
                // Crossing is read from the STORED ring, exactly as the walk's own carry test is
                // (`ring_end_of` in chart_shapes.cpp): whether a finger is still down is a fact
                // about the hands, and a string the ear stops hearing is not one the hand left.
                if (index.onset[ringing] + saved[ringing].sustain <= start)
                {
                    continue;
                }
                const std::optional<SourceLanding> landed = sourceLanding(saved[ringing]);
                if (!landed.has_value())
                {
                    continue;
                }
                landing_here = index.onset[ringing] + landed->arrival == start;
            }
            if (landing_here)
            {
                ++out.successor_spans_landing;
                // The founding filter this carried is DELETED WITH ITS SUBJECT (2026-09-04): it
                // read `founding == Statement`, and there is no founding classification to filter
                // on. What the row measures is the class of the landed grip, which is the question
                // the 2026-08-30 signature was actually about.
                out.successor_spans_landing_box += arpeggio ? 0 : 1;
            }
        }
        // The amendment's headline population: the departure split left a span at its own start
        // whenever the hand travelled straight out of the strike, and the landing split promises
        // there are none of those left. A span the hand ALONE stated is the one honest zero — it
        // has no sounding member to be positive for.
        out.zero_length_spans += shape.sustain.numerator == 0 ? 1 : 0;
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
            }

            // THE STRIKE-LESS FRONT (user ruling 2026-08-31, review #2, narrowed by review #5),
            // counted only where an EVENT opened the span: a front slot whose records all state
            // the fretting hand's stops WITHOUT sounding them — held fingers, and taps whose pitch
            // the other hand's stop decides. A slot stating a LONE record is the shape the ungated
            // fold-in newly admits, since one record can only reach the threshold with a ring
            // carried in beside it.
            //
            // The front is not always the opening slot, and this is the half of the population
            // where it IS: a span opened at a strike-less slot backdates onto its carried member's
            // strike whenever that member was uncovered, and reads as struck here. The row is
            // named for the front for exactly that reason, and the field's own comment carries
            // what that leaves unmeasured.
            if (!successor && struck_at_start.empty())
            {
                ++out.strikeless_front_spans;
                out.strikeless_front_lone_record +=
                    index.slot_last[slot] - index.slot_first[slot] == std::size_t{1} ? 1 : 0;
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
        // SPLIT BY ARM since 2026-08-31 (Q8), because the LANDING SUCCESSORS' every member is a
        // carried ring by construction: pooled in, they were the whole of the "no struck fret to
        // measure" column and the reach question is meaningless there (nothing was struck for the
        // carry to be a reach from). The flip count below is unaffected, because a successor
        // strikes fewer than two strings and so already carries another trigger.
        FoldInReach& reach = successor ? out.foldins_successor : out.foldins_event;
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
            ++reach.foldins;
            if (struck_at_start.empty())
            {
                ++reach.unmeasurable;
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
            reach.carried_fret_distance.add(nearest);

            // An open carry is a voicing member, so its distance says nothing about reach; the
            // fretted ones are the population the physical question is actually about, and they
            // carry the shape's board position with them.
            if (*stop == 0)
            {
                ++reach.open;
                reach.carried_distance_open.add(nearest);
                continue;
            }
            ++reach.fretted;
            reach.carried_distance_fretted.add(nearest);
            reach.carried_distance_fretted_spread.add(static_cast<double>(nearest));
            ++reach.fretted_by_position[position_bucket];
            reach.fretted_distance_by_position[position_bucket].add(nearest);
            if (nearest > 6)
            {
                ++reach.fretted_over_six_by_position[position_bucket];
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

            // ---- (ii): a LONE re-pick of a string the span already states, AT THE STOP it states
            // there. ONE comparison, matching the production rule since rule 11 was amended
            // (2026-08-29): a span is a fretting-hand statement, so palm-mute, dead, accent and
            // ghost move no finger and the fret is the whole test however the shape came to state
            // it. What stood here was the rig's own copy of the retired ARTICULATION identity —
            // the presented note with its position and duration neutralised — kept for the
            // sound-stated arm alone while the claimed arm already compared stops. An instrument
            // measuring a rule the model no longer has cannot report on the model.
            const std::size_t repick = struck_here.front();
            const auto repick_string = static_cast<std::size_t>(saved[repick].string - 1);
            if (repick_string >= posture.size())
            {
                continue;
            }
            const std::optional<int>& stated = posture[repick_string];
            if (!stated.has_value() || *stated != saved[repick].fret)
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
        // The FIRST landing of the group, which is what bounds the extent when the landings are
        // staggered (edge c) and is the same instant as `common_landing` when they coincide.
        std::optional<Fraction> earliest_landing;
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
            earliest_landing =
                earliest_landing.has_value() ? std::min(*earliest_landing, here) : here;
        }
        // Bound once so the presence test and every read below are provably the same object.
        const std::optional<Fraction>& lands = common_landing;
        if (travels && lands.has_value())
        {
            ++out.travel_any_spans;
            // Does the span COVER the travel it starts? The amendment's whole promise: the transit
            // rides the predecessor, so the extent reaches the grip its members are moving to
            // rather than stopping at the departure. Asked of the EARLIEST landing, which is the
            // one the extent is bounded by when the landings are staggered.
            const std::optional<Fraction>& first_landing = earliest_landing;
            out.travel_covering_spans += first_landing.has_value() && *first_landing <= end ? 1 : 0;
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
                // Review F9's population, measured rather than inferred: the source side says
                // this landing states a grip, and no successor stands at it. The walk ABSORBED it
                // — another statement was already standing there, or the statement that should
                // have handed off to it had already been replaced.
                out.travel_landings_absorbed +=
                    resting >= 2 && !successor_starts.contains(*lands) ? 1 : 0;
                // ... and the reason a walk needs no state to hand the grip over: this span never
                // reached the landing, because something else closed it first. Every landing was
                // in this bucket under the departure split; what stays in it is the whole of what
                // a parked hand-off would have delivered.
                out.travel_landings_outrun += resting >= 2 && end < *lands ? 1 : 0;
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

    // THE RULING'S OWN INVARIANT (2026-08-31, Q7): with rolls derived, IMPORTS AUTHOR ZERO CLAIMS.
    // D11's fronted-claims machinery was the last producer of a `NoteAttack::None` record on the
    // import path, so a silent hold anywhere in a built chart means the machinery came back.
    long long imported_claims{0};

    // DERIVED HELD (user ruling 2026-08-31): the population the derivation populates — right-hand
    // onsets a PULL-OFF states a fretting-hand stop under, which is a fact about the note's
    // NEIGHBOUR and therefore a corpus question rather than a per-note one. The residue row beside
    // it is a construction promise with teeth: `normalizeChart` clears every stored `held` the
    // notation already states, so a built chart carrying one means the sweep did not run or did
    // not reach it. Teeth it cannot bite with on an IMPORT-ONLY corpus, though — the importer
    // writes no `held`, so `stored_held_stops` is zero here by construction and the residue with
    // it; the row guards the editor-authored path, and starts discriminating when authored charts
    // reach this rig.
    long long right_hand_onsets{0};
    long long derived_held_stops{0};
    long long stored_held_stops{0};
    long long derived_held_residue{0};

    long long letring_marks{0};
    long long letring_marks_on_graces{0};

    // THE LET-RING FIGURE LAW's reach (user signing 2026-09-04, the simple law): rings the
    // figure end lengthened past written, and marks left at EXACTLY written — the mis-seam
    // detector, since under the law the figure end is the only thing that ever lengthens a marked
    // ring. Read from the importer's own structural pair (`GpBuiltSong::let_ring`) rather than
    // re-derived here — the census measures the shipped pass, it never re-implements it.
    long long letring_extended_rings{0};
    long long letring_marks_at_written{0};

    // THE TAIL LAW's reach on real material (user ruling 2026-09-04), figure-scoped and read off
    // the production verdict (`ChartResolutions::hidden`) rather than re-derived — the census
    // measures the shipped law, it never re-implements it. The denominator is every tail rules 1
    // through 4 left standing, since those are exactly the tails the law is offered; a tail rule 3
    // or rule 4 emptied is never hidden and never counted here.
    //
    // Beats accumulate as double because a corpus-wide Fraction sum would overflow its int terms.
    long long tails_after_rules{0};
    long long hidden_rings{0};
    long long hidden_strokes{0};
    double hidden_ring_beats{0.0};
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
    // THE LAST-OF-SERIES BOUND CANDIDATE (#164): for the final marked beat of each region, split
    // the rings by whether a STATEMENT produced their end (Strike/Rest — the voice's own line) or
    // the BLIND stops did (Cap/ScoreEnd — the walk extrapolating). The candidate rule clips only
    // the blind kind at the next onset on any string of the track, floored at the written
    // duration, so the blind crossings row is its whole population and the clip samples are what
    // it would remove. The stated rows sit beside them as the control the rule must not touch.
    long long last_of_region_stated{0};
    long long last_of_region_blind{0};
    long long last_blind_foreign_crossings{0};
    long long last_stated_foreign_crossings{0};
    Samples last_blind_clip_whole;
    Samples last_stated_clip_whole;
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

                        // ---- the last-of-series bound candidate (#164). Only the region's final
                        // marked beat is in the candidate's scope, and the clip it would apply is
                        // to the NEXT ONSET ANYWHERE in the track — any voice, any string —
                        // floored at the note's own written duration.
                        if (index == region.last)
                        {
                            const bool blind =
                                ring.stop == LetRingStop::Cap || ring.stop == LetRingStop::ScoreEnd;
                            (blind ? census.last_of_region_blind : census.last_of_region_stated) +=
                                1;
                            std::optional<Fraction> next_onset;
                            for (const std::vector<ChainBeat>& line : chains)
                            {
                                for (const ChainBeat& elsewhere : line)
                                {
                                    if (elsewhere.rest ||
                                        !(ring.onset_whole < elsewhere.onset_whole))
                                    {
                                        continue;
                                    }
                                    if (!next_onset.has_value() ||
                                        elsewhere.onset_whole < *next_onset)
                                    {
                                        next_onset = elsewhere.onset_whole;
                                    }
                                }
                            }
                            const Fraction written =
                                chain[index].onset_whole + chain[index].duration_whole;
                            if (next_onset.has_value() && *next_onset < ring.end_whole)
                            {
                                const Fraction floor_end = std::max(*next_onset, written);
                                const Fraction clip = ring.end_whole - floor_end;
                                if (Fraction{} < clip)
                                {
                                    (blind ? census.last_blind_foreign_crossings
                                           : census.last_stated_foreign_crossings) += 1;
                                    (blind ? census.last_blind_clip_whole
                                           : census.last_stated_clip_whole)
                                        .add(clip.toDouble());
                                }
                            }
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

// One aggregate line: a label and its count, in the report's own two columns.
void row(const char* const label, const long long value)
{
    std::cout << "  " << std::left << std::setw(42) << label << std::right << std::setw(10) << value
              << "\n";
}

// One arm of [D4]'s fold-in reach (\ref FoldInReach), printed identically for both so the two
// populations can be read against each other line by line. Takes its arm by mutable reference for
// \ref Samples::summary's reason: the spread is read by sorting the samples in place.
void printFoldInReach(const char* const title, FoldInReach& reach)
{
    std::cout << "\n  --- " << title << " ---\n";
    row("fold-ins", reach.foldins);
    row("fold-ins with no struck fret to measure", reach.unmeasurable);
    std::cout << "  carried-fret distance |carried - nearest struck| (the source-hygiene proxy):\n";
    std::cout << "    " << reach.carried_fret_distance.text() << "\n";
    row("fold-ins carried 5+ frets from any struck", reach.carried_fret_distance.countAtLeast(5));

    std::cout << "  what is being carried: an OPEN string or a FRETTED note\n";
    row("fold-ins carrying an OPEN string", reach.open);
    row("fold-ins carrying a FRETTED note", reach.fretted);
    std::cout << "  open-string carry distance:\n";
    std::cout << "    " << reach.carried_distance_open.text() << "\n";
    std::cout << "  FRETTED carry distance (the reach question's real population):\n";
    std::cout << "    " << reach.carried_distance_fretted.text() << "\n";
    std::cout << "    spread : " << reach.carried_distance_fretted_spread.summary() << "\n";
    row("fretted fold-ins at 5+ frets", reach.carried_distance_fretted.countAtLeast(5));
    row("fretted fold-ins beyond 6 frets (7+)", reach.carried_distance_fretted.countAtLeast(7));

    std::cout << "  fretted carries against the shape's board position\n";
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
                  << std::setw(10) << reach.fretted_by_position[bucket] << std::setw(10)
                  << reach.fretted_over_six_by_position[bucket] << "   "
                  << reach.fretted_distance_by_position[bucket].text() << "\n";
    }
}

struct CrossCheck
{
    std::string label;
    double rig{0.0};
    // The independently recorded figure this row is held to — a PRIOR census or a signed number,
    // never this rig's own latest output, which would turn the check into a tautology.
    //
    // ABSENT means the row is REPORTED ONLY: nobody has signed a figure for the population it
    // measures — because a build moved that population, or because the row is context beside an
    // enforced one. Such a row prints its measurement, wears the marker so the reader can see
    // which rows the table is not checking, and enforces nothing — a value invented here to fill
    // the column would be this rig checking itself.
    std::optional<double> expected{};
};

// The band a signed figure is held to: a tenth of it, and EXACT where the signed value is zero —
// a ruled zero has no band in which a violation would be acceptable.
[[nodiscard]] double crossCheckTolerance(const double expected)
{
    return expected > 0.0 ? 0.10 * expected : 0.0;
}

// Prints the cross-check table AND ENFORCES it (user ruling 2026-08-31, review #9). A signed
// expectation is a real `CHECK`, because a marker printed into a report nobody diffs is not a
// gate: this case carries the hidden `[.local-corpus]` tag and runs only where the corpus is, so
// failing it is exactly its job.
//
// TWO BLOCKS, because they are two different kinds of statement and mixing them taught the reader
// to skim both: the enforced rows are figures somebody signed, and the awaiting rows are
// measurements with nothing to check against yet. ONE predicate decides what prints and what is
// checked, so the marker and the failure can never disagree.
void reportCrossCheck(const std::vector<CrossCheck>& rows)
{
    std::cout << "\n[8] CROSS-CHECK against the prior scratch censuses and the signed figures the\n"
              << "    let-ring import was accepted on\n"
              << "    (this rig runs the production parser and is the authority; a flagged row is\n"
              << "     a FINDING to explain, not an error to hide.)\n";
    std::cout << "\n  --- ENFORCED: signed figures, checked ---\n";
    std::cout << "    " << std::left << std::setw(44) << "metric" << std::right << std::setw(12)
              << "rig" << std::setw(12) << "expected" << std::setw(12) << "delta" << "\n";
    for (const CrossCheck& entry : rows)
    {
        // Bound once so the presence test and the reads are provably the same object.
        const std::optional<double>& expected = entry.expected;
        if (!expected.has_value())
        {
            continue;
        }
        const double delta = entry.rig - *expected;
        const bool within = std::abs(delta) <= crossCheckTolerance(*expected);
        std::cout << "    " << std::left << std::setw(44) << entry.label << std::right << std::fixed
                  << std::setprecision(2) << std::setw(12) << entry.rig << std::setw(12)
                  << *expected << std::setw(12) << delta << (within ? "" : "  <== FLAG") << "\n";
        INFO("cross-check row: " << entry.label);
        CHECK(within);
    }

    std::cout << "\n  --- REPORTED, NOT ENFORCED: no signed figure to check against ---\n";
    std::cout << "    " << std::left << std::setw(44) << "metric" << std::right << std::setw(12)
              << "rig" << "\n";
    for (const CrossCheck& entry : rows)
    {
        if (entry.expected.has_value())
        {
            continue;
        }
        std::cout << "    " << std::left << std::setw(44) << entry.label << std::right << std::fixed
                  << std::setprecision(2) << std::setw(12) << entry.rig
                  << "  <== unsigned: reported only\n";
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

        census.letring_extended_rings += built->let_ring.extended;
        census.letring_marks_at_written += built->let_ring.at_written;

        const ScoreWalk walk = walkScore(*score, built->tempo_map, census);

        for (std::size_t track = 0; track < built->arrangements.size(); ++track)
        {
            const common::core::Chart& chart = built->arrangements[track].chart;
            ++census.arrangements;
            census.chart_notes += static_cast<long long>(chart.notes.size());
            census.imported_claims +=
                std::ranges::count_if(chart.notes, [](const common::core::ChartNote& note) {
                    return common::core::silentHold(note.attack);
                });
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

            // DERIVED HELD's population, read off the production derivation rather than restated:
            // an entry is present exactly where a pull-off states the stop under a right-hand
            // onset. The residue count beside it is what the normalizer promises is zero.
            const std::vector<std::optional<int>> derived_stops =
                common::core::chartDerivedStops(resolutions.connections);
            for (std::size_t note = 0; note < chart.notes.size(); ++note)
            {
                const ChartNote& record = chart.notes[note];
                census.right_hand_onsets += common::core::rightHandOnset(record.attack) ? 1 : 0;
                census.stored_held_stops += record.held.has_value() ? 1 : 0;
                if (!derived_stops[note].has_value())
                {
                    continue;
                }
                ++census.derived_held_stops;
                census.derived_held_residue += record.held.has_value() ? 1 : 0;
            }
            // THE TAIL LAW's reach, from the one place that decides it. A stroke counts once,
            // which is the atom the law itself judges by.
            {
                const std::vector<ChartNote>& saved = resolutions.connections.saved_notes;
                bool stroke_hidden = false;
                for (std::size_t note = 0; note < saved.size(); ++note)
                {
                    if (note > 0 && !(saved[note].position == saved[note - 1].position))
                    {
                        census.hidden_strokes += stroke_hidden ? 1 : 0;
                        stroke_hidden = false;
                    }
                    if (resolutions.hidden[note])
                    {
                        ++census.hidden_rings;
                        ++census.tails_after_rules;
                        stroke_hidden = true;
                        census.hidden_ring_beats +=
                            static_cast<double>(saved[note].sustain.numerator) /
                            static_cast<double>(saved[note].sustain.denominator);
                    }
                    else if (resolutions.presented_notes[note].sustain.numerator > 0)
                    {
                        ++census.tails_after_rules;
                    }
                }
                census.hidden_strokes += stroke_hidden ? 1 : 0;
            }

            const std::vector<bool> arrivals = common::core::chartShapeArrivals(
                resolutions.presented_notes, resolutions.shapes, built->tempo_map);
            countDerivation(
                resolutions.connections.saved_notes,
                resolutions.presented_notes,
                resolutions.shapes,
                resolutions.postures,
                arrivals,
                chart.fret_hand_positions,
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
    std::cout << "  right-hand onsets                       : " << census.right_hand_onsets << "\n";
    std::cout << "  ... whose held stop a PULL-OFF derives  : " << census.derived_held_stops
              << "\n";
    std::cout << "  stored held stops surviving the sweep   : " << census.stored_held_stops << "\n";
    std::cout << "  ... beside a derived one (promise: 0)   : " << census.derived_held_residue
              << "\n";
    std::cout << "  let-ring marked note occurrences        : " << census.letring_marks << "\n";
    std::cout << "  ... plus, on grace beats, not walked    : " << census.letring_marks_on_graces
              << "\n";
    std::cout << "  let-ring rings the figure end extended  : " << census.letring_extended_rings
              << "\n";
    std::cout << "  ... marks left at exactly written       : " << census.letring_marks_at_written
              << "\n";
    // THE TAIL LAW, figure-scoped. The denominator is every tail rules 1 through 4 left standing;
    // the law is offered exactly those and can only empty them, so the second row is its whole
    // reach and the third is how much ring the figures are carrying in place of ribbon.
    std::cout << "  tails standing after rules 1-4          : " << census.tails_after_rules << "\n";
    std::cout << "  ... hidden: the figure accounts for it  : " << census.hidden_rings << "\n";
    std::cout << "  ... strokes with a hidden member        : " << census.hidden_strokes << "\n";
    std::cout << "  ... beats of stored ring they carry     : " << census.hidden_ring_beats << "\n";
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

    std::cout << "\n[3] [D4] TRIGGER 4 — a carried ring folding into a span onset\n";
    row("spans", census.derivation.spans);
    row("spans classified arpeggio", census.derivation.spans_arpeggio);
    row("trigger-4 spans", census.derivation.trigger4_spans);
    row("box -> arpeggio flips (trigger 4 alone)", census.derivation.trigger4_only_spans);
    row("fold-ins, both arms",
        census.derivation.foldins_event.foldins + census.derivation.foldins_successor.foldins);
    std::cout << "  (SPLIT BY ARM below, Q8: a successor's every member is a carried ring by\n"
                 "   construction, so its distances measure the arm and never a hand's reach —\n"
                 "   pooled in, they buried the source-hygiene population the question is about)\n";
    printFoldInReach(
        "EVENT-opened spans — THE SOURCE-HYGIENE POPULATION", census.derivation.foldins_event);
    printFoldInReach(
        "LANDING SUCCESSORS — the arm's own shape", census.derivation.foldins_successor);

    std::cout << "\n[4] [D3] CONTINUITY GATES\n";
    row("gap re-picks under witnesses", census.derivation.ii_gap_repicks);
    row("  of those, a SOUND witness", census.derivation.ii_gap_repicks_sound);
    row("  of those, a CLAIM witness", census.derivation.ii_gap_repicks_claim);
    row("interior-gap spans", census.derivation.interior_gap_spans);
    row("lone re-pick slots (context)", census.derivation.ii_slots);

    std::cout << "\n[4a] THE OPENING LAW's populations and the two invariants they carry\n";
    std::cout << "  (the FOUNDING SPLIT that stood here — ACCUMULATION- against\n"
                 "   STATEMENT-founded spans, over six counters — is DELETED WITH ITS SUBJECT\n"
                 "   (grip-tenure law, 2026-09-04). There is no founding classification: the\n"
                 "   slot open is one disjunction naming no mode, so the split had nothing left\n"
                 "   to read, and re-deriving it here would be this rig inventing a concept the\n"
                 "   model dropped. The one honest opening-cause key, `landing_opened`, is\n"
                 "   censused in section [5].)\n";
    row("spans (context)", census.derivation.spans);
    std::cout
        << "  --- THE ONE-COUNT OPENING LAW's new population (review #2), HALF-MEASURED ---\n"
           "  (a FRONT slot nothing sounds at, opened by an EVENT: held fingers, and taps\n"
           "   whose pitch the other hand's stop decides. The carry fold-in used to be gated\n"
           "   on a strike, so a LONE record there could not reach the threshold at all.\n"
           "   The DATING RULE hides the rest: a strike-less opening that folded in an\n"
           "   UNCOVERED ring is dated at that ring's own strike, so it reads as struck. The\n"
           "   R-B ruling is priced by these rows only for the covered-carry half; the other\n"
           "   half stays UNMEASURED until the walk publishes the slot it opened at.)\n";
    row("spans DATED at a strike-less slot", census.derivation.strikeless_front_spans);
    row("  ... whose front states a LONE record", census.derivation.strikeless_front_lone_record);
    std::cout << "  --- the dating rule's invariant (the ruled promise is ZERO) ---\n";
    row("spans starting inside a preceding span", census.derivation.overlapping_spans);
    std::cout << "  --- THE FHP CONVERGENCE, reported and never enforced ---\n"
                 "  (FHP is the POSITION story, spans are the GRIP story; this says whether the\n"
                 "   two independent derivations describe one hand. Open members are excluded —\n"
                 "   a 0 is a voicing member no finger holds.)\n";
    row("spans under a fret-hand window", census.derivation.fhp_checked_spans);
    row("  ... holding a stop outside its reach", census.derivation.fhp_out_of_reach_spans);
    row("  those stops", census.derivation.fhp_out_of_reach_stops);
    std::cout << "  " << std::setw(42) << std::left << "  frets past the window's edge"
              << census.derivation.fhp_reach_overshoot.text() << "\n"
              << std::right;
    std::cout << "  --- THE HAND-COUPLING GATE (user law 2026-09-05: a span should not exist\n"
                 "   across an FHP shift unless a slide carries it; one measurement, both\n"
                 "   directions - the coupling's readiness and the generator's error signal) ---\n";
    row("spans crossed by an FHP shift", census.derivation.spans_crossed_by_fhp_shift);
    row("  those interior shifts", census.derivation.fhp_shifts_inside_spans);
    row("fret-hand windows placed", census.derivation.fhp_placements);
    row("  ... arriving where nothing fretted sounds", census.derivation.fhp_placements_unfretted);

    std::cout << "\n[5] [D2] TRAVEL AND THE LANDED GRIP — THE OPENING-CAUSE CENSUS\n";
    std::cout << "  (keyed on `landing_opened`, the one opening-cause datum the walk publishes\n"
                 "   and the only honest key there is: a LANDED TRAVEL is the single onset-less\n"
                 "   open the grip-tenure law admits, and everything else is opened by an EVENT.\n"
                 "   Ring-out opens NOTHING since 2026-09-04, so the member's-DEATH cause that\n"
                 "   used to share this field is deleted rather than moved.)\n";
    row("spans opened by a LANDING", census.derivation.successor_spans);
    row("  ... classified arpeggio", census.derivation.successor_spans_arpeggio);
    row("  ... opening ON a note slot (F8)", census.derivation.successor_spans_at_slot);
    row("  ... their carried fold-ins", census.derivation.foldins_successor.foldins);
    row("spans opened by anything else (an EVENT)",
        census.derivation.spans - census.derivation.successor_spans);
    std::cout << "  --- the SOURCE-SIDE second opinion on the landing arm ---\n"
                 "  (a LANDING is a crossing ring arriving at its resting stop exactly there,\n"
                 "   read off the fret channels. Under the one-cause law this is a CONVERGENCE\n"
                 "   check rather than a split: the unattributed row is this rig's own shortfall,\n"
                 "   not a second cause.)\n";
    row("  ... the source side also sees a LANDING", census.derivation.successor_spans_landing);
    row("  ... the source side cannot attribute",
        census.derivation.successor_spans - census.derivation.successor_spans_landing);
    row("  ... of the attributed, classified BOX", census.derivation.successor_spans_landing_box);
    std::cout << "  --- the source-side reading beside it, by edge ---\n";
    std::cout << "  (this denominator is SPANS, and it collapsed from 1494 to 621 when rule 11\n"
                 "   was amended: a chug run over one grip is now ONE span where it used to be\n"
                 "   many, and each of those spans counted its start member's travel separately.\n"
                 "   The travels themselves did not change — the thing being counted did.)\n";
    row("spans a start member travels in", census.derivation.travel_any_spans);
    row("  ... whose extent COVERS the travel", census.derivation.travel_covering_spans);
    row("  landings that re-open", census.derivation.travel_landings_open);
    row("  suppressed: staggered (edge c)", census.derivation.travel_landings_staggered);
    row("  suppressed: no room to state (edge b)", census.derivation.travel_landings_crowded);
    row("  absorbed: no successor stands there", census.derivation.travel_landings_absorbed);
    row("  outrun: the span never reached it", census.derivation.travel_landings_outrun);
    row("zero-length spans", census.derivation.zero_length_spans);
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
    std::cout << "  LAST-OF-SERIES rings, blind / stated    : " << census.last_of_region_blind
              << " / " << census.last_of_region_stated << "  <- #164's scope split\n";
    std::cout << "    blind crossing a foreign onset        : "
              << census.last_blind_foreign_crossings << "  <- the candidate's whole population\n";
    std::cout << "    ... clip it would take (whole notes)  : "
              << census.last_blind_clip_whole.summary() << "\n";
    std::cout << "    stated crossing a foreign onset       : "
              << census.last_stated_foreign_crossings
              << "  <- the control the rule must not touch\n";
    std::cout << "    ... ring past that onset (whole notes): "
              << census.last_stated_clip_whole.summary() << "\n";
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

    reportCrossCheck(
        std::vector<CrossCheck>{
            CrossCheck{
                .label = "files parsed",
                .rig = static_cast<double>(census.files_parsed),
                .expected = 113.0,
            },
            CrossCheck{
                // THE DENOMINATOR every derived row is read against, and until now the one figure
                // the report printed without ever checking (user ruling 2026-08-31, review #9). It
                // is the import's own output rather than a derivation of it, so nothing in this
                // build moves it — the span law changes what the notes MEAN and never how many
                // there are — which is exactly what makes it the row that says whether the parse
                // itself drifted. SIGNED 2026-08-31 (user) at 245866, its FIRST signature: no prior
                // census recorded it, so the first run of the rig that prints it is what the
                // figure was read off.
                .label = "chart notes built",
                .rig = static_cast<double>(census.chart_notes),
                .expected = 245866.0,
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
                // THE RULING'S OWN INVARIANT, signed rather than measured (2026-08-31, Q7): with
                // rolls derived, IMPORTS AUTHOR ZERO CLAIMS. Exact-match by design — one silent
                // hold in a built chart means D11's fronted-claims machinery came back, and there
                // is no tolerance band in which that would be acceptable.
                .label = "imported claims (the ruling says ZERO)",
                .rig = static_cast<double>(census.imported_claims),
                .expected = 0.0,
            },
            CrossCheck{
                // THE DATING RULE's own promise, signed rather than measured (2026-08-31): a span
                // dates from its earliest member onset NOT COVERED by a preceding span, so no span
                // may start inside the one before it. The gate census priced the defect at 182
                // spans and the rule takes it "to 0 by construction". Exact-match: an overlap is
                // two statements claiming one instant, which nothing makes acceptable.
                .label = "spans starting inside a preceding span",
                .rig = static_cast<double>(census.derivation.overlapping_spans),
                .expected = 0.0,
            },
            CrossCheck{
                // RE-SIGNED 2026-08-31 (user) at 775 / 15, and it is the POPULATION that moved
                // rather than the corpus. These two rows count NOTE OCCURRENCES ON THE TIMELINE:
                // the walk visits every beat of every voice chain and asks each note that beat
                // carries. The 318 / 10 the ruleset quoted came from a one-level-deep scan of the
                // file's AUTHORED note elements instead, and a GP note pool is SHARED BY ID — one
                // authored record sounds at every beat that references it. The two figures answer
                // two different questions rather than disagreeing: 423 authored records, 775
                // narrow and 15 wide occurrences. The occurrence count is the one this report is
                // built on, every other row here being an occurrence count too.
                .label = "vibrato narrow (note occurrences)",
                .rig = static_cast<double>(census.vibrato_narrow),
                .expected = 775.0,
            },
            CrossCheck{
                .label = "vibrato wide (note occurrences)",
                .rig = static_cast<double>(census.vibrato_wide),
                .expected = 15.0,
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
                //
                // The [D2] AMENDMENT (the split moves to the LANDING, 2026-08-29) took the
                // successor component from +779 to +854, and moved six ordinary spans the other
                // way. The successor movement is three named parts: a successor now needs its
                // members only to RING ON past the landing, where the departure split also
                // demanded a display margin of room (review F3) and refused landings that do in
                // fact breathe; the four successors that used to open ON a note slot are gone,
                // since a landing on a closing slot leaves the successor no length at all; and
                // every landing is now REACHED (section [5]'s "outrun" row reads zero), which is
                // what let the walk's parked hand-off be deleted outright.
                //
                // The -6 is the per-member reading of a mid-travel sounding (same day): an open
                // member restruck mid-slide sounds a stop the shape still states, so it rides the
                // travelling span as an interior subset sounding instead of truncating it and
                // opening a span of its own. Six spans in this corpus were that figure.
                //
                // RULE 11 AMENDED (a change in articulation does not split the span, 2026-08-29)
                // then moved this row by -2404 and flipped its sign, which is the largest single
                // movement it has ever carried and exactly the amendment's whole point: the row
                // now reads 1546 BELOW its last independent figure. Two components, and they pull
                // opposite ways:
                //
                //   -2915 EVERYTHING EXCEPT THE SUCCESSORS, and that is exactly as much as this
                //         arithmetic can say. The dominant component is the merges continuation
                //         gained by comparing POSITION only — a chord, the dead chugs played on it
                //         and the chord again are ONE span where they used to be three or more,
                //         the doctrine's own U2 chug-section population collapsing — but the same
                //         amendment relaxed the lone re-pick's member test and the growth split's
                //         supersession in the same breath, and this rig has no instrument that
                //         separates the three. Reading the figure as "merges" alone would credit
                //         one relaxation with the other two's work.
                //   +511  landing successors that now EXIST. A full restrike of the landed grip
                //         restates the successor's stops, so it rides inside it instead of
                //         closing it with no room (corollary 2 deletes "the bracket span never
                //         strums"); the successor is emitted and the restrike's own former span
                //         is one of the merges above. Section [5]'s source-side reading measures
                //         the same movement from the other end: edge (b)'s suppression falls from
                //         698 to 247.
                //
                // The -2915 is read off this arithmetic rather than counted separately, exactly as
                // the -4 in the row below is: the successor counter is measured, the total is
                // measured, and the remainder is what is left — which is why it is named as a
                // remainder rather than as any one rule's population.
                //
                // THE ACCUMULATION LAW (user ruling 2026-08-31) unpinned this row, and the ledger
                // above became history in one step. 23355 was the last SIGNED figure and the rig
                // read 21809 against it under the shipped law; no census had measured the
                // composition the law ships. The gate census measured two worlds that were NOT
                // ruled — the relay world (a conjunction that never held) and the strict
                // dead-members-block world (62577 spans, over-fragmented) — and the ruling closed
                // with "NO RE-CENSUS BEFORE THE BUILD", so absorb + min-extent + death-successor
                // arrived unmeasured by construction, with no expectation writable for it that
                // would not have been this rig quoting itself.
                //
                // RE-SIGNED 2026-08-31 (user) at 23865, off the first run of that composition. The
                // ATTRIBUTION is measured rather than predicted and lives where it can be
                // re-derived: section [4a] splits the total into 21327 STATEMENT-founded and 2538
                // ACCUMULATION-founded, which is 21809 + 2538 - 482. The two components:
                //
                //   +2538 ACCUMULATION-founded spans, the law's own population and one that could
                //         not exist before it: 2147 founded at a slot (a lone strike whose ring
                //         overlaps what is already sounding) and 391 opened by carried rings at a
                //         boundary.
                //   -482  NET on the STATEMENT-founded side, and net is all this rig can say. The
                //         absorption half removes statement openings — a slot that used to close a
                //         standing statement and open its own now grows an ACCUMULATION in place —
                //         while both successor arms add them, and no counter here separates the
                //         two movements.
                //
                // THAT ATTRIBUTION IS NOW HISTORY: `founding` and its six section-[4a] counters
                // are deleted with the concept (2026-09-04), so the split it cites can no longer
                // be printed. The SIGNATURE stands and the SUBJECT is unchanged — this row has
                // always counted every span — but the grip-tenure law merges same-grip restrike
                // chains into one span, the rebuild's largest ruled delta, so the row is expected
                // to FLAG until the census re-sign (#158) reads a new figure off the corpus. A
                // flagged row is the finding this table exists to surface, which is why it keeps
                // its pin rather than quietly acquiring an invented one.
                .label = "spans total",
                .rig = static_cast<double>(census.derivation.spans),
                .expected = 23865.0,
            },
            CrossCheck{
                // SIGNED 2026-08-31 (user) at 2631, for the `spans` row's reason: 736 was the last
                // independent figure and the rig read 836 against it under the shipped law, so the
                // ruled composition arrived with nothing to check it against.
                //
                // The attribution was section [4a]'s and it was nearly the whole movement: 2423 of
                // the 2538 accumulation-founded spans classified ARPEGGIO, which is the class law
                // falling out rather than a decision — an accumulation's opening slot strikes
                // fewer strings than its shape sounds BY DEFINITION, since the rings it overlapped
                // into are the rest. The ruling's own "100% arpeggio classification" is that
                // statement; the 115 that did not were spans whose founding carry was superseded
                // before any interior slot sounded.
                //
                // The founding split those numbers were read off is DELETED (2026-09-04), so this
                // paragraph is history rather than a live cross-reference. The row keeps its pin
                // for the `spans total` row's reason: same subject, moved population, and the
                // movement is for #158 to sign rather than for this file to invent.
                .label = "arpeggio spans",
                .rig = static_cast<double>(census.derivation.spans_arpeggio),
                .expected = 2631.0,
            },
            CrossCheck{
                // SIGNED 2026-08-31 (user) at 69, for the same reason: 727 was signed pre-let-ring
                // and the rig read 664 under the shipped law, so nothing independent covered the
                // composition that shipped. This counter asks how many spans trigger 4 flips
                // ALONE, and the law moved its POPULATION rather than its rule: a carried ring is
                // now also what FOUNDS a span, and a span an interior sounding already flipped is
                // no longer flipped by the carry alone. It fell by an order of magnitude where it
                // used to rise — 2150 spans still meet trigger 4 (section [3]), and only these 69
                // need it — which is exactly why it could not be checked against a figure counted
                // over the older denominator.
                .label = "trigger-4-only flips",
                .rig = static_cast<double>(census.derivation.trigger4_only_spans),
                .expected = 69.0,
            },
            CrossCheck{
                // THE LONGEST-UNSIGNED SERIES in this table, and the history is why the signature
                // reads the way it does. The earlier censuses only ever printed the pre-let-ring
                // 188, the import moved it to roughly 297, and the (ii) narrowing then took it
                // from 313 to 305. Quoting any of those back would have been the rig checking
                // itself, which is the one thing this column may never hold.
                // [D2] then took it from 305 to 295: a re-pick cannot ride a shape the hand has
                // already travelled out of, and those ten spans are the +10 in the `spans` row.
                // Its amendment gave eight back, to 303. One is a span that now covers its
                // members' travel and so reaches an onset it used to end before, which the rig
                // reads as a slot sitting on the span's own end — the ambiguous kind this
                // counter's `end_slot` rows exist to separate, and it prints as a box. The other
                // seven are review F7 landing: a lone re-pick of a LANDED member now rides its
                // successor, where the successor's carried record used to refuse it, so those
                // slots are inside a span at all for the first time.
                // 303 -> 333 on 2026-08-30 is the RIG moving, not the model: this instrument kept
                // its own copy of the retired ARTICULATION identity for the sound-stated arm, so it
                // was refusing thirty spans the production rule has ridden since rule 11 was
                // amended. Deleting the copy is what makes the number a measurement of the shipped
                // law again.
                //
                // SIGNED 2026-08-31 (user) at 1926, the first expectation this row has ever
                // carried. The accumulation law moved the DENOMINATOR under it — 23865 spans — and
                // section [2] carries the reading beside it: 1839 of the 1926 classify arpeggio
                // today, over 5115 lone re-pick slots.
                .label = "lone re-pick spans",
                .rig = static_cast<double>(census.derivation.ii_spans),
                .expected = 1926.0,
            },
            CrossCheck{
                // [D2] built 2026-08-28, and nobody had signed a figure for the successor
                // population: the only prior number was this rig's own PRE-BUILD instrument (915
                // spans whose members all travel with a breathing landing), which measured neither
                // of the two edges the ruling then suppressed, so quoting it would have stood
                // permanently red for a reason section [5] already explains. The amendment
                // (2026-08-29) moved it from 783 to 854, and section [5] carries that attribution
                // edge by edge.
                //
                // IT COUNTED TWO CAUSES from 2026-08-31: the landing was one way for carried
                // rings to cross a boundary, and a member's DEATH was the other. SIGNED
                // 2026-08-31 (user) at 1768 over that two-cause population.
                //
                // UNPINNED 2026-09-04 — THE SUBJECT NARROWED, and this row may not hold a
                // two-cause signature over a one-cause population. Rule 7 of the grip-tenure law
                // deleted the DEATH arm outright ("strings that merely ring on past a break are
                // tails; ring-out opens nothing"), so `landing_opened` counts landings alone and
                // 1768 was signed over a set this can no longer produce. Nothing here may invent
                // its replacement: the figure is the census re-sign's (#158), and until the user
                // signs one the row reports.
                .label = "landing-opened spans (awaiting the #158 re-sign)",
                .rig = static_cast<double>(census.derivation.successor_spans),
                .expected = std::nullopt,
            },
            CrossCheck{
                // A REAL CLASSIFICATION CENSUS since 2026-08-30, where it used to be an equality
                // pin. The old row asserted zero because the successor arm STATED the class — a
                // constant `true` on a span nothing could strum — so the row could only ever
                // report that the constant was still there. A LANDING IS NOT A SOUNDING: the
                // successor now classifies by the ordinary triggers, so this counts how many of
                // the corpus's chord slides land in a grip that is then STRUMMED WHOLE (a box)
                // rather than picked apart (a bracket).
                //
                // The expectation was INDEPENDENT of the rig: section [5]'s source-side reading
                // says 371 landings re-open, and every successor's class comes from what sounds
                // inside it, so a corpus dominated by chord slides into chord stabs should read
                // overwhelmingly BOX. Signed at the measured 1331 of 1365 the first time the ruling
                // ran, which is that prediction in numbers.
                //
                // THE EXPRESSION NOW COMPUTES WHAT THE LABEL NAMES (user ruling 2026-08-31, review
                // #8). It was signed over the LANDING arm, and the one-authority gate then admitted
                // a second cause — a member's DEATH — whose successors the old subtraction swept in
                // beside the landings, so the row silently changed subject while keeping its pin.
                //
                // IT HAS NOW MET THE CORPUS, and the pin STOOD (user 2026-08-31): the first run
                // after that build read 1314 against the signed 1331, inside the band.
                //
                // UNPINNED 2026-09-04 — THE EXPRESSION LOST A FILTER IT WAS SIGNED WITH. The
                // count read `!arpeggio && founding == Statement`, and `founding` is deleted with
                // its enum, so the Statement half cannot be spelled and the row now counts every
                // source-attributed landing that classifies BOX. That is a WIDER population than
                // 1331 was signed over, and holding a signature across a widened subject is the
                // exact defect the paragraph above records this row committing once already. The
                // replacement figure belongs to the census re-sign (#158), not to this file.
                .label = "  landing successors classified BOX (awaiting #158)",
                .rig = static_cast<double>(census.derivation.successor_spans_landing_box),
                .expected = std::nullopt,
            },
            CrossCheck{
                // DERIVED HELD's residue (user ruling 2026-08-31): `normalizeChart` clears every
                // stored held stop a pull-off already states, so a BUILT chart carrying one is the
                // sweep having failed to run or failed to reach it. Exact-match for the imported-
                // claims row's reason — there is no band in which a document holding two spellings
                // of one statement is acceptable.
                //
                // ON THIS CORPUS IT CANNOT DISCRIMINATE, and says so rather than reading as a
                // green light: every chart here is IMPORTED, and the importer writes no `held` at
                // all, so the count is zero whether the sweep ran or not. What the row genuinely
                // guards is the EDITOR-AUTHORED path — a held stop typed onto an onset a pull-off
                // later states — which reaches this rig only once authored charts do. It stays
                // pinned because the promise is a promise; it is simply not evidence yet.
                .label = "stored held beside a derived one (ZERO)",
                .rig = static_cast<double>(census.derived_held_residue),
                .expected = 0.0,
            },
            CrossCheck{
                // DERIVED HELD's population itself, which no census had ever measured: the stop
                // was authored per note until 2026-08-31, so there was nothing to check it against
                // and the first run of the derivation is what the signature was read off. SIGNED
                // 2026-08-31 (user) at 220.
                .label = "held stops a pull-off derives",
                .rig = static_cast<double>(census.derived_held_stops),
                .expected = 220.0,
            },
            CrossCheck{
                // THE ONE-COUNT OPENING LAW's own new population (review #2), HALF-MEASURED
                // (review #5). A strike-less slot could not fold a carried ring in before this
                // build, so this row has no prior figure by construction — and the LONE-record
                // sub-count in section [4a] is what prices the change rather than the total here.
                //
                // It counts spans DATED at such a slot, which is the covered-carry half: a
                // strike-less opening that folded in an UNCOVERED ring backdates onto that ring's
                // strike and is invisible from published data. The other half stays UNMEASURED
                // until the walk publishes the slot it opened at, so this number is a floor on the
                // ruling's price and never the price itself.
                //
                // SIGNED 2026-08-31 (user) at 2, AND SIGNED AS A FLOOR: the row is held to the
                // covered-carry half it can see, so a movement here is a movement in that half and
                // says nothing about the half nothing publishes.
                .label = "spans DATED at a strike-less slot (floor)",
                .rig = static_cast<double>(census.derivation.strikeless_front_spans),
                .expected = 2.0,
            },
            CrossCheck{
                // WAS "successors opened by a member's DEATH", signed 2026-08-31 (user) at 269.
                // DELETED WITH ITS SUBJECT and REPLACED IN PLACE (grip-tenure law rule 7,
                // 2026-09-04): ring-out opens nothing, so there is no death cause left to count
                // and the identical subtraction now means something else entirely — the spans the
                // walk opened at a landing that this rig's own fret-channel reading cannot stand
                // an arrival on. That is the rig disagreeing with the derivation, which section
                // [5] prints as a convergence row and the file's own doctrine calls a FINDING
                // rather than a defect in either. It carries no signature because nobody has ever
                // measured it: the two-cause world made the same subtraction unreadable as
                // convergence.
                .label = "landings the source side cannot attribute",
                .rig = static_cast<double>(
                    census.derivation.successor_spans - census.derivation.successor_spans_landing),
                .expected = std::nullopt,
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
                // SIGNED AS A COUNT 2026-08-31 (user), where its three siblings above stay
                // percentages, because at THIS magnitude the percentage cannot be both written and
                // checked. The band is a tenth of the signed figure, so 0.2 is held to 0.02 either
                // side, while the smallest step a one-decimal percentage can express is 0.1 — five
                // times its own tolerance. The rig's 27 rings are 0.23%, and the 23.7 rings a
                // signed 0.2 implies sit 3.3 rings below that, so the row flagged for a
                // quantisation gap rather than for any movement in the corpus. The count has no
                // such floor: 27 rings, band 2.7. Section [7] prints the percentage beside the
                // count as context.
                .label = "let-ring stop: score end (rings)",
                .rig = static_cast<double>(census.stop_score_end),
                .expected = 27.0,
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
                // RE-SIGNED 2026-08-31 (user) at 0.375, and THE LABEL NOW NAMES THE STATISTIC so
                // the quantile cannot drift under the row: this is the MEDIAN of the 125
                // first-crossing bleeds, in whole notes, and section [7] prints the whole spread
                // beside it (min 0.062, p25 0.250, median 0.375, p75 0.562, max 0.938). The 0.56
                // this replaces sits at that p75 rather than anywhere near the median — which is
                // the reading that made writing the quantile into the label worth doing.
                .label = "  median bleed, 125 crossings (whole notes)",
                .rig = census.marker_bleed_whole.median(),
                .expected = 0.375,
            },
            CrossCheck{
                // POINTED AT THE POPULATION THE RULING IS ABOUT (user ruling 2026-08-31). The
                // A-vs-composed discriminator asks whether a live let-ring passage is ever written
                // ACROSS a boundary the transcriber NAMED, and the row was checking every section
                // mark in the score instead — bar marks included, which carry no such statement at
                // all. Read over named marks it is zero, and it is an exact-match row for the
                // imported-claims row's reason: a named boundary a live region straddles is a
                // passage stated across a section somebody put a name on, and there is no band in
                // which one of those is acceptable.
                .label = "live-region straddles of NAMED marks (ZERO)",
                .rig = static_cast<double>(census.named_live_region_straddles),
                .expected = 0.0,
            },
            CrossCheck{
                // The all-marks reading beside it, reported and never enforced: straddles of EVERY
                // section mark, named or not. Nobody has signed a figure for that population and
                // it is not the one the discriminator rules on — it is here because the 14 it
                // reads is what makes the zero above a statement about NAMES rather than a claim
                // that no region straddles anything.
                .label = "live-region straddles, EVERY mark (context)",
                .rig = static_cast<double>(census.live_region_straddles),
                .expected = std::nullopt,
            },
            CrossCheck{
                .label = "region-end overshoots",
                .rig = static_cast<double>(census.region_end_overshoots),
                .expected = 997.0,
            },
            CrossCheck{
                // RE-SIGNED 2026-08-31 (user) at 162, with THE BUCKET DEFINITION written into the
                // row so the figure can never be read against a different one. This is the
                // run-length histogram's ENTRY FOR 1 — overshooting rings whose let-ring region is
                // a single beat long — and "isolated" in no other sense. The 380 it replaces was
                // signed without that definition recorded anywhere, so what it bucketed can no
                // longer be recovered, which is exactly why the definition now lives in the row.
                .label = "  of those, regions running ONE beat",
                .rig = static_cast<double>(census.overshoot_by_run_length.at(1)),
                .expected = 162.0,
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
