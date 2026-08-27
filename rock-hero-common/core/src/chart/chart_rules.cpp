#include "chart/chart_rules.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string_view>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// A full octave: fine tuning stays within a semitone, but real bass arrangements charted on
// guitar strings pitch down a whole octave via -1200 cents (a common charting practice).
constexpr double g_max_cent_offset{1200.0};

[[nodiscard]] std::string positionText(const GridPosition& position)
{
    return formatGridPositionToken(position);
}

// True when consecutive neck positions along a scrape's path — start, turnarounds, and the exit
// when present — all strictly differ. Only the POSITION channel is a neck position; a waypoint
// carrying nothing but a latent bend or vibrato passes through without breaking the travel.
[[nodiscard]] bool pickSlidePathTravels(const ChartNote& note)
{
    int previous_fret = note.fret;
    for (const Waypoint& waypoint : note.waypoints)
    {
        // Bound to a local so the optional check and the access are provably the same object
        // (bugprone-unchecked-optional-access does not credit a guard on a loop variable's member).
        const std::optional<int>& fret = waypoint.fret;
        if (!fret.has_value())
        {
            continue;
        }
        if (*fret == previous_fret)
        {
            return false;
        }
        previous_fret = *fret;
    }
    const int* const slide_out = slideOutFretOrNull(note);
    return slide_out == nullptr || *slide_out != previous_fret;
}

// True when any waypoint states a pitch modulation — the channels a dead string cannot carry.
[[nodiscard]] bool statesModulation(const std::vector<Waypoint>& waypoints)
{
    return std::ranges::any_of(waypoints, [](const Waypoint& waypoint) {
        return waypoint.bend.has_value() || waypoint.vibrato.has_value();
    });
}

} // namespace

void dropNotePath(ChartNote& note)
{
    static_cast<void>(stripWaypointChannels(note.waypoints, [](Waypoint& waypoint) {
        const bool stated = waypoint.fret.has_value();
        waypoint.fret.reset();
        return stated;
    }));
    note.slide_out.reset();
}

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1};
}

std::expected<void, ChartError> validateChartRules(const Chart& chart, const TempoMap& tempo_map)
{
    const auto string_count = static_cast<int>(chart.tuning.strings.size());
    if (string_count < 1 || string_count > g_max_chart_strings)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidTuning,
            .message = "chart tuning must name between 1 and " +
                       std::to_string(g_max_chart_strings) + " strings",
        }};
    }
    for (const std::string& open_string : chart.tuning.strings)
    {
        if (open_string.empty())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidTuning,
                .message = "chart tuning strings must name their open pitch",
            }};
        }
    }
    if (chart.tuning.capo < 0 || chart.tuning.capo > g_max_capo ||
        std::abs(chart.tuning.cent_offset) > g_max_cent_offset)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidTuning,
            .message = "chart capo or cent offset is out of range",
        }};
    }

    // No posture or span rules: both are derived from the notes (deriveChartShapes), so there is
    // no authored span here that could be wrong. The posture's capo floor and board ceiling come
    // with the frets it reads — every one of them belongs to a note this validator judges,
    // silently-held stops included — and a derived span's length and order are properties of the
    // walk that built it.

    const FretHandPosition* previous_fhp = nullptr;
    for (const FretHandPosition& fhp : chart.fret_hand_positions)
    {
        // A window of no width and a position off the grid have no repair; where the window SITS
        // is the normalizer's fit (above the capo, under the last fret, the whole width on the
        // board), asked as the fixpoint.
        if (fhp.width < 1 || !isValidGridPosition(fhp.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand position is invalid at " + positionText(fhp.position),
            }};
        }
        FretHandPosition normal = fhp;
        if (const std::vector<ChartRepair> repairs =
                normalizeFretHandPosition(normal, chart.tuning);
            !repairs.empty())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = std::string{chartRepairText(repairs.front())} + " at " +
                           positionText(fhp.position),
            }};
        }
        if (previous_fhp != nullptr && fhp.position < previous_fhp->position)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand positions must be sorted at " + positionText(fhp.position),
            }};
        }
        previous_fhp = &fhp;
    }

    return validateChartNotes(chart.notes, chart.tuning, tempo_map);
}

std::string_view chartRepairText(const ChartRepair repair)
{
    switch (repair)
    {
        case ChartRepair::DeadNoteModulation:
        {
            return "a dead note sounds no pitch, so its bend or vibrato was dropped";
        }
        case ChartRepair::DeadPinch:
        {
            return "a damped string cannot squeal, so a dead pinch harmonic became a plain pick";
        }
        case ChartRepair::TapHarmonicTremolo:
        {
            return "a tap harmonic cannot be tremolo picked, so its tremolo was dropped";
        }
        case ChartRepair::FretHandHarmonicPayload:
        {
            return "a fret-hand harmonic presses nothing, so its bend, vibrato, or slide was "
                   "dropped";
        }
        case ChartRepair::OpenStringSlide:
        {
            return "an open string cannot slide, so its slide was dropped";
        }
        case ChartRepair::StrandedStrike:
        {
            return "a tap with nothing to strike became a plain pick";
        }
        case ChartRepair::OverlappingTail:
        {
            return "a re-strike stops the ring, so a tail was truncated at the next onset on its "
                   "string";
        }
        case ChartRepair::FretPastBoard:
        {
            return "a position past the last fret was clamped onto the board";
        }
        case ChartRepair::FretBelowCapo:
        {
            return "a slide position or hand window on or below the capo was lifted above it";
        }
        case ChartRepair::StilledScrape:
        {
            return "a pick slide that no longer travels became a plain pick";
        }
        case ChartRepair::UnjustifiedLegato:
        {
            return "a legato mark had nothing to connect to and reads as a plain pick";
        }
        case ChartRepair::InertSilentHold:
        {
            return "a held stop belonged to no shape, so it stated nothing and was removed";
        }
    }
    return "chart repaired";
}

std::string chartConversionText(const ChartConversion& conversion)
{
    return std::string{chartRepairText(conversion.repair)} + " at " + conversion.where;
}

bool flattenStrandedStrike(ChartNote& note)
{
    if (!nothingToStrike(note))
    {
        return false;
    }
    note.attack = NoteAttack::Pick;
    return true;
}

// A slide-out is never dropped here and never re-placed: it ends the ring by definition, so a
// shortened ring simply carries it (chart.h). What a scrape's terminal still needs is a new AIM
// when compression makes its fret meet the fret it now follows.
void clipPayloadsToSustain(ChartNote& note, const bool end_lands_on_onset)
{
    // Read before the clip, because the fret the terminal falls back on may be one the clip is
    // about to remove: the nearest EARLIER differing fret takes over so the path never sits still.
    std::optional<int> aimed_terminal;
    if (const int* const slide_out = slideOutFretOrNull(note);
        slide_out != nullptr && isScrape(note.attack))
    {
        int surviving_fret = note.fret;
        for (const Waypoint& waypoint : note.waypoints)
        {
            if (!(waypoint.offset < note.sustain))
            {
                break;
            }
            surviving_fret = waypoint.fret.value_or(surviving_fret);
        }
        int aimed = *slide_out;
        for (const Waypoint& waypoint : std::ranges::reverse_view(note.waypoints))
        {
            if (aimed != surviving_fret)
            {
                break;
            }
            const std::optional<int>& fret = waypoint.fret;
            if (fret.has_value())
            {
                aimed = *fret;
            }
        }
        aimed_terminal = aimed;
    }

    std::erase_if(note.waypoints, [&note](const Waypoint& waypoint) {
        return note.sustain < waypoint.offset;
    });
    // The POSITION channel takes a STRICT bound where the general one is inclusive, for two
    // reasons that meet at the same line. A slide-out is the ring's last position statement, so
    // nothing may state a fret where it ends; and `end_lands_on_onset` says the new end IS a
    // following same-string onset, where a stated fret would store the landing's coordinates a
    // second time — the encoding \ref validateChartNotes exists to keep unrepresentable. Bend and
    // vibrato are other channels and keep the inclusive bound, which is what lets an imported bend
    // arriving exactly at the ring's end survive a truncation that shortens the path.
    if (end_lands_on_onset || note.slide_out.has_value())
    {
        static_cast<void>(stripWaypointChannels(note.waypoints, [&note](Waypoint& waypoint) {
            if (!waypoint.fret.has_value() || waypoint.offset < note.sustain)
            {
                return false;
            }
            waypoint.fret.reset();
            return true;
        }));
    }
    if (aimed_terminal.has_value())
    {
        note.slide_out = *aimed_terminal;
    }
}

// The one walk that answers "when is this string struck again", which the truncation below, the
// span-implied hold's cap and the editor's growth verbs all read. Same-position notes are on
// different strings by construction (a duplicate onset is an invalid chart), so the search starts
// past the note's whole onset group and stops at the first later note on the string.
std::optional<Fraction> sustainBoundOf(
    const std::vector<ChartNote>& notes, const ChartNote& note, const TempoMap& tempo_map)
{
    const auto later = std::ranges::subrange(
        std::ranges::upper_bound(notes, note.position, std::ranges::less{}, &ChartNote::position),
        notes.end());
    // The next STRIKE on the string, which is why the search is a predicate rather than a
    // projection match: a silent hold occupies a slot on the string and stops nothing, so a bound
    // read off one would make authoring a held shape truncate every ring behind it.
    const auto next = std::ranges::find_if(later, [&note](const ChartNote& candidate) {
        return candidate.string == note.string && !silentHold(candidate.attack);
    });
    if (next == later.end())
    {
        return std::nullopt;
    }
    return beatDistance(tempo_map, note.position, next->position);
}

// A ring past its bound ends exactly on it (adjacency is legal), clipping payloads with the tail.
std::vector<std::size_t> normalizeSustainOverlaps(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    std::vector<std::size_t> truncated;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        const std::optional<Fraction> bound = sustainBoundOf(notes, note, tempo_map);
        if (!bound.has_value() || !(*bound < note.sustain))
        {
            continue;
        }
        note.sustain = *bound;
        clipPayloadsToSustain(note, /*end_lands_on_onset=*/true);
        truncated.push_back(index);
    }
    return truncated;
}

std::vector<ChartRepair> normalizeChartNote(ChartNote& note, const ChartTuning& tuning)
{
    std::vector<ChartRepair> repairs;
    const auto fired = [&repairs](const ChartRepair repair) { repairs.push_back(repair); };

    // 1. The board ceiling. Clamps before floors and before the travel test, so those read final
    //    values; a stated fret is clamped rather than stripped because it still names real travel.
    bool past_board = note.fret > g_max_fret;
    note.fret = std::min(note.fret, g_max_fret);
    for (Waypoint& waypoint : note.waypoints)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        std::optional<int>& fret = waypoint.fret;
        if (fret.has_value() && *fret > g_max_fret)
        {
            past_board = true;
            fret = g_max_fret;
        }
    }
    std::optional<int>& slide_out = note.slide_out;
    if (slide_out.has_value() && *slide_out > g_max_fret)
    {
        past_board = true;
        slide_out = g_max_fret;
    }
    if (past_board)
    {
        fired(ChartRepair::FretPastBoard);
    }

    // 2. The capo floor for every fret a slide gesture names (user ruling 2026-08-20, closing
    //    W9-J): a scrape's start and every exit lift to the first playable fret, because the pick
    //    travels the sounding string and a "scrape at the nut" is no scrape; a waypoint on or
    //    below the floor loses its POSITION, since a pitched stop there is nothing pressed —
    //    stripped per channel, so a bend or vibrato change authored at the same instant survives
    //    the lift and only a waypoint left stating nothing goes. A pressed NOTE on a capo'd fret
    //    is not repaired here: no lift can know the pitch the author meant, so it stays a refusal.
    const int floor = firstPlayableFret(tuning.capo);
    bool below_capo = false;
    if (isScrape(note.attack) && note.fret < floor)
    {
        below_capo = true;
        note.fret = floor;
    }
    const bool lifted_a_waypoint =
        stripWaypointChannels(note.waypoints, [floor](Waypoint& waypoint) {
            // Bound to a local so the optional check and the access are provably the same object.
            std::optional<int>& fret = waypoint.fret;
            if (!fret.has_value() || *fret >= floor)
            {
                return false;
            }
            fret.reset();
            return true;
        });
    below_capo = below_capo || lifted_a_waypoint;
    if (slide_out.has_value() && *slide_out < floor)
    {
        below_capo = true;
        slide_out = floor;
    }
    if (below_capo)
    {
        fired(ChartRepair::FretBelowCapo);
    }

    // 3. The technique exclusions. The DEADENING outranks the harmonic (user ruling 2026-08-18): a
    //    player can hold a harmonic's shape while damping, and the node then says where the hand
    //    is rather than what rings — exactly how a dead note's own FRET already reads — so the
    //    note stays dead and keeps its node. What it cannot keep is pitch MODULATION, which has
    //    no positional reading. The palm flag is untouched throughout: it says where the picking
    //    hand is, never what the string sounds.
    if (note.dead &&
        (std::is_neq(note.bend <=> 0.0) || note.vibrato || statesModulation(note.waypoints)))
    {
        note.bend = 0.0;
        note.vibrato = false;
        // The modulation CHANNELS go; the position channel stays, because a dead string still
        // travels — a dragged mute is exactly that.
        static_cast<void>(stripWaypointChannels(note.waypoints, [](Waypoint& waypoint) {
            const bool modulated = waypoint.bend.has_value() || waypoint.vibrato.has_value();
            waypoint.bend.reset();
            waypoint.vibrato.reset();
            return modulated;
        }));
        fired(ChartRepair::DeadNoteModulation);
    }
    // The pinch is the one harmonic the deadening takes with it: its node lies off the neck and
    // so survives as neither pitch nor hand position. Attack and node go together, because a
    // pinch carrying no node is missing DATA rather than shed technique.
    if (note.dead && note.harmonic_node.has_value() && !nodeIsOnNeck(note.attack))
    {
        note.attack = NoteAttack::Pick;
        note.harmonic_node.reset();
        fired(ChartRepair::DeadPinch);
    }
    // A tap harmonic's damping finger leaves the string, so nothing holds the node under
    // re-picking.
    if (note.attack == NoteAttack::Tap && note.harmonic_node.has_value() && note.tremolo)
    {
        note.tremolo = false;
        fired(ChartRepair::TapHarmonicTremolo);
    }
    // A fret-hand harmonic touches its node with nothing pressed: there is no press to bend,
    // shake, or carry anywhere, and moving the touch off the node just stops the harmonic.
    if (fretHandHarmonic(note) && (std::is_neq(note.bend <=> 0.0) || note.vibrato ||
                                   !note.waypoints.empty() || note.slide_out.has_value()))
    {
        note.bend = 0.0;
        note.vibrato = false;
        // Every channel goes here rather than one of them, so the whole array goes with them:
        // there is no statement a touch with nothing pressed can make about its own ring.
        note.waypoints.clear();
        note.slide_out.reset();
        fired(ChartRepair::FretHandHarmonicPayload);
    }
    // An open string cannot slide: nothing is pressed to travel, so a fret-0 glide or trail-off
    // loses its position channel. A scrape never reaches this — its start was floored above.
    if (!isScrape(note.attack) && note.fret == 0 &&
        (anyWaypointStatesFret(note.waypoints) || note.slide_out.has_value()))
    {
        dropNotePath(note);
        fired(ChartRepair::OpenStringSlide);
    }

    // 4. A strike from nowhere needs somewhere to land.
    if (flattenStrandedStrike(note))
    {
        fired(ChartRepair::StrandedStrike);
    }

    // 5. Last: a scrape keeps traveling or it is no scrape. After the clamps, floors, and drops
    //    above, consecutive neck positions — start, turnarounds, exit — must strictly differ,
    //    because a pick cannot rest on a fret and still be scraping (an ordinary slide's
    //    equal-fret segment is a legitimate hold). A scrape without its terminal at all is missing
    //    data and stays a refusal, so only a present exit is judged. Demoted to the plain pick it
    //    sounds like, with its path cleared. The editor's scrape verb asks no question of its own
    //    here: it builds the path and lets the fixpoint judge it, so a held segment skips the note
    //    the same way.
    if (isScrape(note.attack) && note.slide_out.has_value() && !pickSlidePathTravels(note))
    {
        note.attack = NoteAttack::Pick;
        dropNotePath(note);
        fired(ChartRepair::StilledScrape);
    }
    return repairs;
}

std::vector<ChartRepair> normalizeFretHandPosition(
    FretHandPosition& position, const ChartTuning& tuning)
{
    // The playable board is what lies above the capo; a window wider than that cannot fit
    // anywhere, so the width shrinks first and the two placements below then always succeed.
    const int floor = firstPlayableFret(tuning.capo);
    const int playable = g_max_fret - tuning.capo;
    bool past_board = position.width > playable;
    position.width = std::min(position.width, playable);
    const bool below_capo = position.fret < floor;
    position.fret = std::max(position.fret, floor);
    // The whole window must fit under the last fret: bounding only the index finger let a wide
    // hand run off the end. With the width already inside the playable board, this never pushes
    // the finger back below the floor.
    if (position.fret + position.width - 1 > g_max_fret)
    {
        past_board = true;
        position.fret = g_max_fret - position.width + 1;
    }
    std::vector<ChartRepair> repairs;
    if (past_board)
    {
        repairs.push_back(ChartRepair::FretPastBoard);
    }
    if (below_capo)
    {
        repairs.push_back(ChartRepair::FretBelowCapo);
    }
    return repairs;
}

std::vector<ChartConversion> normalizeChart(Chart& chart, const TempoMap& tempo_map)
{
    std::vector<ChartConversion> conversions;
    const auto record =
        [&conversions](const std::vector<ChartRepair>& repairs, const std::string& where) {
            for (const ChartRepair repair : repairs)
            {
                conversions.push_back(ChartConversion{.repair = repair, .where = where});
            }
        };
    for (ChartNote& note : chart.notes)
    {
        record(
            normalizeChartNote(note, chart.tuning),
            positionText(note.position) + " string " + std::to_string(note.string));
    }
    // The one rule a note cannot obey alone (40-Q2-B): a re-strike stops the ring. It runs here
    // rather than at each producer, so a chart written before the rule — or by a converter that
    // never learned it — is truncated and REPORTED on load instead of drawing a tail through a
    // later head.
    for (const std::size_t index : normalizeSustainOverlaps(chart.notes, tempo_map))
    {
        conversions.push_back(
            ChartConversion{
                .repair = ChartRepair::OverlappingTail,
                .where = positionText(chart.notes[index].position) + " string " +
                         std::to_string(chart.notes[index].string),
            });
    }
    for (FretHandPosition& position : chart.fret_hand_positions)
    {
        record(
            normalizeFretHandPosition(position, chart.tuning),
            "hand position " + positionText(position.position));
    }
    // The relational settles run LAST, against the stream as it will actually stand: a trimmed
    // tail may have been the hold a neighbour's claim depended on. The legato settle goes first of
    // the two because flattening a claim CHANGES an articulation, and an articulation is what the
    // shapes the hold sweep judges against are keyed by; nothing the hold sweep removes can
    // justify or withdraw a claim, since a silent hold neither sounds nor bounds a ring.
    std::vector<ChartConversion> settled = sweepUnjustifiedLegato(chart.notes, tempo_map);
    std::vector<ChartConversion> swept = sweepInertSilentHolds(chart.notes, tempo_map);
    settled.insert(
        settled.end(),
        std::make_move_iterator(swept.begin()),
        std::make_move_iterator(swept.end()));
    conversions.insert(
        conversions.end(),
        std::make_move_iterator(settled.begin()),
        std::make_move_iterator(settled.end()));
    return conversions;
}

double harmonicNodeCeiling(const ChartNote& note)
{
    // A finger on the fretboard cannot be past the last fret, so where the FRETTING finger is the
    // one touching the node, the neck caps it rather than the string.
    return frettingFingerOnNode(note) ? static_cast<double>(g_max_fret) : g_max_harmonic_node;
}

std::expected<void, ChartError> validateChartNoteAlone(
    const ChartNote& note, const ChartTuning& tuning, const TempoMap& tempo_map)
{
    // The model's cap bounds the string domain as much as the tuning does. validateChartRules
    // refuses a wider tuning outright; a direct caller that passes one has no string past the cap
    // this rule set can speak about. A fret past the board is NOT refused here — it is the
    // normalizer's clamp, asked as the fixpoint at the end.
    const int string_count = std::min(static_cast<int>(tuning.strings.size()), g_max_chart_strings);
    if (note.string < 1 || note.string > string_count || note.fret < 0 ||
        !isValidGridPosition(note.position, tempo_map))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "note is out of range at " + positionText(note.position),
        }};
    }
    // The ring, and it is the one rule the attack decides outright rather than shading. Every
    // STRUCK string rings for SOME length — a dead note's damped stroke included — so the sustain
    // is the actual duration and is strictly positive. No repair can express that: a duration is
    // information, and inventing one would be authoring the chart. It doubles as the format
    // tripwire for any zero that reaches memory, which is why the message names the cause rather
    // than the field. A package written before the duration model rarely arrives here: that writer
    // OMITTED the key on every tail-less note, so the document reader refuses it first, with the
    // same re-import remedy.
    //
    // A silent hold is the mirror image: nothing is struck, so there is no ring to state and a
    // stored one would be a length the shape derivation never reads and \ref sustainBoundOf could
    // only ever contradict. Zero is not a fallback here but the required value, refused in the
    // other direction.
    if (silentHold(note.attack))
    {
        if (note.sustain.numerator != 0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message =
                    "a silently held stop has no ring of its own at " + positionText(note.position),
            }};
        }
    }
    else if (note.sustain.numerator <= 0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "note sustain must be positive at " + positionText(note.position) +
                       "; this chart predates the note duration model and must be re-imported",
        }};
    }
    if (note.harmonic_node.has_value() &&
        (*note.harmonic_node <= 0.0 || *note.harmonic_node > g_max_harmonic_node))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "harmonic node position is out of range at " + positionText(note.position),
        }};
    }
    // A node lies on the speaking length, so it cannot sit at or behind the physical stop —
    // nothing vibrates there.
    if (note.harmonic_node.has_value() &&
        *note.harmonic_node <= static_cast<double>(physicalStopFret(note, tuning.capo)))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "harmonic node must lie beyond the stop at " + positionText(note.position),
        }};
    }
    // A fret-hand harmonic's node carries the fretting finger, so it must lie on the neck; the
    // ceiling states which notes that binds and is shared with import, so a builder can tell
    // an unreachable node from a reachable one instead of handing this rule a whole song to
    // refuse. Only the neck ceiling reaches here — the universal bound above already caught
    // everything else — which is also what keeps the derived hand window inside `g_max_fret`.
    if (note.harmonic_node.has_value() && *note.harmonic_node > harmonicNodeCeiling(note))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message =
                "fret-hand harmonic node must lie on the neck at " + positionText(note.position),
        }};
    }
    // A pinch is picking while damping a node, so a pinch without one is missing data rather
    // than a different technique — the overtone that squeals is *determined* by where the thumb
    // lands. Enforcing it is what lets node presence alone assert the harmonic.
    if (note.attack == NoteAttack::Pinch && !note.harmonic_node.has_value())
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "pinch harmonic must carry its node at " + positionText(note.position),
        }};
    }
    // The capo is the string's floor: 0 means the capo'd open string, and the frets it covers
    // do not exist to play. A pressed note on one has no repair that is not an invented pitch,
    // so it stays a refusal; a SCRAPE's start on one is the normalizer's lift (a scrape has no
    // open form — user ruling 2026-08-20, closing W9-J), asked as the fixpoint below.
    if (note.fret != 0 && note.fret < firstPlayableFret(tuning.capo) && !isScrape(note.attack))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "fret must be 0 or above the capo at " + positionText(note.position),
        }};
    }
    // A finger cannot lower a stopped string's pitch, so a negative push is a data error rather
    // than a technique (W9-K, ratified 2026-08-25); dips and dives belong to the whammy bar's own
    // model. Checked at the onset value here and at every waypoint below, because the channel is
    // one channel.
    if (note.bend < 0.0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNotePayload,
            .message = "bend amount must not be negative at " + positionText(note.position),
        }};
    }
    // Payload geometry no repair can express: a statement outside the sustain, out of order, or
    // stating nothing at all is incoherent data, not a technique to shed. Where a waypoint sits on
    // the NECK is the normalizer's (the board clamp and the capo floor), asked as the fixpoint
    // below.
    //
    // Offsets are STRICTLY positive: offset zero is the onset, whose facts the note itself carries,
    // so a waypoint there would be a second spelling of a value the note already states.
    const bool trails_off = note.slide_out.has_value();
    Fraction previous_offset{0};
    for (const Waypoint& waypoint : note.waypoints)
    {
        if (waypoint.offset <= previous_offset || waypoint.offset > note.sustain)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "waypoint offsets must ascend within the sustain at " +
                           positionText(note.position),
            }};
        }
        previous_offset = waypoint.offset;
        if (waypointStatesNothing(waypoint))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "waypoint must state a fret, a bend, or a vibrato change at " +
                           positionText(note.position),
            }};
        }
        // Bound to locals so each optional check and its access are provably the same object.
        const std::optional<int>& fret = waypoint.fret;
        if (fret.has_value() && *fret < 0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "waypoint fret must not be negative at " + positionText(note.position),
            }};
        }
        // A slide-out is the ring's LAST position statement — it releases off the end — so every
        // stated fret lies strictly before it. Bend and vibrato are other channels and reach the
        // end like any payload.
        if (fret.has_value() && trails_off && !(waypoint.offset < note.sustain))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "a slide-out ends the ring after every stated fret at " +
                           positionText(note.position),
            }};
        }
        const std::optional<double>& bend = waypoint.bend;
        if (bend.has_value() && *bend < 0.0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "bend amount must not be negative at " + positionText(note.position),
            }};
        }
    }
    const int* const slide_out = slideOutFretOrNull(note);
    if (slide_out != nullptr && *slide_out < 0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNotePayload,
            .message = "slide-out fret must not be negative at " + positionText(note.position),
        }};
    }
    // WHAT THIS ATTACK MAY STATE, asked as a FIXPOINT rather than by listing fields: a saved note
    // must already equal its own saved form. Two attacks carry less than the whole record — a
    // SAVED pick slide carries no pitched technique, because the writer omits the in-memory
    // overrides (chart.h), and a SILENT HOLD carries nothing at all beyond its stop, because
    // nothing sounds for a technique to describe — and enumerating either set here would duplicate
    // exactly what savedChartNote strips, leaving the writer and this rule to agree by hand while
    // a field added to ChartNote updated only one of them. Asked unconditionally because the
    // comparison is identity for every attack that overrides nothing, which is also why the
    // fallthrough below can name the silent hold: no other attack can reach it.
    //
    // Emphasis is a scrape's own dynamics and passes there; on a silent hold it is refused with
    // the rest, since a stop nothing strikes has no dynamics to state.
    if (!(savedChartNote(note) == note))
    {
        if (isScrape(note.attack))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidPickSlide,
                .message = "pick-slide note must not carry pitched techniques at " +
                           positionText(note.position),
            }};
        }
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "a silently held stop states its fret and nothing else at " +
                       positionText(note.position),
        }};
    }
    // The scrape's own gesture: the required unpitched slide-out terminal, exactly at the sustain
    // (nothing rings past a scrape). That the path keeps traveling is the normalizer's demotion,
    // asked as the fixpoint below.
    if (isScrape(note.attack))
    {
        // Presence is the whole rule now: a slide-out ends the ring by definition, so a terminal
        // that exists is a terminal exactly at the sustain and there is no second coordinate left
        // to disagree with (W11).
        if (slide_out == nullptr)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidPickSlide,
                .message = "pick slide must end in a slide-out at " + positionText(note.position),
            }};
        }
    }
    // Everything else a note can break on its own is a repair the normalizer owns, so the rule
    // is asked exactly once: the note must already be its own normal form.
    ChartNote normal = note;
    if (const std::vector<ChartRepair> repairs = normalizeChartNote(normal, tuning);
        !repairs.empty())
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = std::string{chartRepairText(repairs.front())} + " at " +
                       positionText(note.position),
        }};
    }
    return std::expected<void, ChartError>{};
}

std::expected<void, ChartError> validateChartNotes(
    const std::vector<ChartNote>& notes, const ChartTuning& tuning, const TempoMap& tempo_map)
{
    const ChartNote* previous_note = nullptr;
    for (const ChartNote& note : notes)
    {
        // Every rule a note can break on its own, asked of the one authority for them rather than
        // restated here. What remains below is only what reads a note's NEIGHBOURS.
        if (auto alone = validateChartNoteAlone(note, tuning, tempo_map); !alone.has_value())
        {
            return alone;
        }
        if (previous_note != nullptr)
        {
            if (!chartNoteOrderLess(*previous_note, note))
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::UnsortedOrDuplicateNotes,
                    .message = "notes must be sorted by position and string with unique onsets"
                               " at " +
                               positionText(note.position),
                }};
            }
        }

        // A waypoint may never STATE A FRET on a later onset of its own string: a glide into a
        // real note is the slideEnd "next" terminal, which stores no coordinates. Rejecting the
        // coordinate copy here is what keeps the desyncable encoding unrepresentable. Scrape
        // turnarounds are bound too; the scrape's terminal is its slide-out, which stores no
        // offset at all and so never reaches this rule. A bend or vibrato statement there names no
        // position and copies nothing, so the rule does not bind it.
        //
        // ONSET is the word: a silently-held stop (\ref NoteAttack::None) at the same slot is no
        // re-pick, states no fret the glide could desync from, and does not bound the ring the
        // waypoint lies inside either (\ref sustainBoundOf asks the same question there). A glide
        // travelling under a held shape is ordinary playing, so it is not refused here.
        for (const Waypoint& waypoint : note.waypoints)
        {
            if (!waypoint.fret.has_value())
            {
                continue;
            }
            const GridPosition waypoint_position =
                advanceGridPosition(tempo_map, note.position, waypoint.offset);
            for (auto at_waypoint = std::ranges::lower_bound(
                     notes, waypoint_position, std::ranges::less{}, &ChartNote::position);
                 at_waypoint != notes.end() && at_waypoint->position == waypoint_position;
                 ++at_waypoint)
            {
                if (at_waypoint->string == note.string && !silentHold(at_waypoint->attack))
                {
                    return std::unexpected{ChartError{
                        .code = ChartErrorCode::InvalidNotePayload,
                        .message = "waypoint fret may not sit on a later onset of its string at " +
                                   positionText(note.position) +
                                   "; a glide ends before its re-picked landing",
                    }};
                }
            }
        }

        previous_note = &note;
    }

    return std::expected<void, ChartError>{};
}

} // namespace rock_hero::common::core
