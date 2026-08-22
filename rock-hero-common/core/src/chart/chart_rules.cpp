#include "chart/chart_rules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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
// when present — all strictly differ.
[[nodiscard]] bool pickSlidePathTravels(const ChartNote& note)
{
    int previous_fret = note.fret;
    for (const SlideWaypoint& waypoint : note.slides)
    {
        if (waypoint.fret == previous_fret)
        {
            return false;
        }
        previous_fret = waypoint.fret;
    }
    return !note.slide_out.has_value() || note.slide_out->fret != previous_fret;
}

} // namespace

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1};
}

std::vector<bool> chartShapeArrivals(const Chart& chart, const TempoMap& tempo_map)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(chart.shapes.size());
    // Both streams ascend, so one note cursor serves every shape. It carries the one thing the rule
    // needs from the past — the most recent note on each string — which is what turns the whole
    // classification into a single forward pass. Answering it per shape instead meant walking BACK
    // through the note stream from each span, all the way to the first note whenever a posture
    // string had none, and both projections do this for every shape on every chart revision.
    constexpr std::size_t no_note = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(no_note);
    std::size_t next_note = 0;
    for (const ChartShape& shape : chart.shapes)
    {
        while (next_note < chart.notes.size() && chart.notes[next_note].position < shape.position)
        {
            const int string = chart.notes[next_note].string;
            if (string >= 1 && string <= g_max_chart_strings)
            {
                last_per_string.at(static_cast<std::size_t>(string)) = next_note;
            }
            ++next_note;
        }
        // The cursor now sits on the first note AT the span start, and the notes sharing that onset
        // are the contiguous run from there.
        std::size_t after_start = next_note;
        while (after_start < chart.notes.size() &&
               chart.notes[after_start].position == shape.position)
        {
            ++after_start;
        }
        if (after_start - next_note < 2)
        {
            // A single onset at the span start is a sequential arrival, whatever else is ringing.
            arpeggio.push_back(true);
            continue;
        }

        // A held chord played under a right-hand onset reads as a held arpeggio, not a strummed
        // box: the fretting hand holds the shape while the other hand sounds above it — taps and
        // pick slides alike. Any such note sounding within the span flips the box.
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
        bool held_under_right_hand = false;
        for (std::size_t scan = next_note;
             scan < chart.notes.size() && chart.notes[scan].position < span_end;
             ++scan)
        {
            held_under_right_hand =
                held_under_right_hand || rightHandOnset(chart.notes[scan].attack);
        }
        if (held_under_right_hand || shape.chord >= chart.templates.size())
        {
            arpeggio.push_back(held_under_right_hand);
            continue;
        }

        // A posture string still ringing at the start without an onset there was not re-struck —
        // the strum picks around the held note, so the span cannot be one full strum. Only that
        // string's most recent earlier note can still be ringing, which the cursor already knows.
        const ChordTemplate& chord_template = chart.templates[shape.chord];
        bool rings_unstruck = false;
        for (std::size_t index = 0; index < chord_template.frets.size(); ++index)
        {
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = chord_template.frets[index];
            const int string = static_cast<int>(index) + 1;
            if (!fret.has_value() || string > g_max_chart_strings)
            {
                continue;
            }
            bool struck = false;
            for (std::size_t scan = next_note; scan < after_start; ++scan)
            {
                struck = struck || chart.notes[scan].string == string;
            }
            const std::size_t earlier = last_per_string.at(static_cast<std::size_t>(string));
            rings_unstruck = rings_unstruck ||
                             (!struck && earlier != no_note &&
                              shape.position < sustainEndPosition(tempo_map, chart.notes[earlier]));
        }
        arpeggio.push_back(rings_unstruck);
    }
    return arpeggio;
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

    for (std::size_t index = 0; index < chart.templates.size(); ++index)
    {
        const ChordTemplate& chord_template = chart.templates[index];
        if (chord_template.frets.size() != chart.tuning.strings.size() ||
            chord_template.fingers.size() != chart.tuning.strings.size())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidTemplate,
                .message = "chord template arrays must match the string count: template " +
                           std::to_string(index),
            }};
        }
        for (const std::optional<int>& fret : chord_template.frets)
        {
            // Postures obey the capo floor exactly like notes: 0 is the capo'd open string, and
            // the frets the capo covers do not exist to hold. Neither a negative fret nor one on
            // a capo'd fret has a repair that is not invented data, so both stay refusals; the
            // board ceiling is the normalizer's clamp, asked as the fixpoint below.
            if (fret.has_value() && (*fret < 0 || (*fret != 0 && *fret <= chart.tuning.capo)))
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidTemplate,
                    .message =
                        "chord template fret is out of range: template " + std::to_string(index),
                }};
            }
        }
        ChordTemplate normal = chord_template;
        if (const std::vector<ChartRepair> repairs = normalizeChordTemplate(normal);
            !repairs.empty())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidTemplate,
                .message = std::string{chartRepairText(repairs.front())} + ": template " +
                           std::to_string(index),
            }};
        }
    }

    const ChartShape* previous_shape = nullptr;
    for (const ChartShape& shape : chart.shapes)
    {
        if (shape.chord >= chart.templates.size() || shape.sustain.numerator <= 0 ||
            !isValidGridPosition(shape.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidShape,
                .message = "shape span is invalid at " + positionText(shape.position),
            }};
        }
        if (previous_shape != nullptr && shape.position < previous_shape->position)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidShape,
                .message = "shape spans must be sorted at " + positionText(shape.position),
            }};
        }
        previous_shape = &shape;
    }

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

    if (auto notes_result = validateChartNotes(chart.notes, chart.tuning, tempo_map);
        !notes_result.has_value())
    {
        return notes_result;
    }

    return std::expected<void, ChartError>{};
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
        case ChartRepair::MutedTail:
        {
            return "a dead note rings nothing, so its plain tail was trimmed (tremolo or a slide "
                   "keeps one)";
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

bool trimMutedTail(ChartNote& note)
{
    // The two things that keep a dead string making noise or travelling are what keep its tail:
    // repeated raking, or a dragged mute. A scrape always carries a slide-out, so a scrape with a
    // latent dead flag in memory is never trimmed — which is what lets the editor apply this to
    // the in-memory note rather than the saved form.
    if (!note.dead || note.tremolo || !note.slides.empty() || note.slide_out.has_value() ||
        note.sustain.numerator <= 0)
    {
        return false;
    }
    note.sustain = Fraction{};
    return true;
}

// The scrape branch returns early on purpose: a scrape's terminal is RE-PLACED at the new sustain
// rather than dropped, and the waypoint erase below (which would judge the turnarounds against
// end_lands_on_onset) and the slide-out drop after it would both be wrong for it.
void clipPayloadsToSustain(ChartNote& note, const bool end_lands_on_onset)
{
    std::erase_if(
        note.bend, [&note](const BendPoint& point) { return note.sustain < point.offset; });
    if (isScrape(note.attack) && note.slide_out.has_value())
    {
        const std::vector<SlideWaypoint> path = std::move(note.slides);
        note.slides = {};
        for (const SlideWaypoint& waypoint : path)
        {
            if (waypoint.offset < note.sustain)
            {
                note.slides.push_back(waypoint);
            }
        }
        const int previous_fret = note.slides.empty() ? note.fret : note.slides.back().fret;
        int terminal_fret = note.slide_out->fret;
        std::size_t candidate = path.size();
        while (terminal_fret == previous_fret && candidate > 0)
        {
            --candidate;
            terminal_fret = path[candidate].fret;
        }
        note.slide_out = SlideOut{.offset = note.sustain, .fret = terminal_fret};
        return;
    }
    std::erase_if(note.slides, [&note, end_lands_on_onset](const SlideWaypoint& waypoint) {
        return end_lands_on_onset ? !(waypoint.offset < note.sustain)
                                  : note.sustain < waypoint.offset;
    });
    if (note.slide_out.has_value() && note.sustain < note.slide_out->offset)
    {
        note.slide_out.reset();
    }
}

// Walks each string's sorted notes: a sustain ringing across the next onset on that string ends
// exactly there instead (adjacency is legal), clipping payloads with it. One inner scan per note
// finds that string's next onset, and it stops at the first one found — later notes on the string
// are bounded by their own predecessor in turn.
void normalizeSustainOverlaps(std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        if (note.sustain.numerator <= 0)
        {
            continue;
        }
        for (std::size_t later = index + 1; later < notes.size(); ++later)
        {
            const ChartNote& next = notes[later];
            if (next.string != note.string)
            {
                continue;
            }
            if (next.position < sustainEndPosition(tempo_map, note))
            {
                note.sustain = beatDistance(tempo_map, note.position, next.position);
                clipPayloadsToSustain(note, /*end_lands_on_onset=*/true);
            }
            break;
        }
    }
}

std::vector<ChartRepair> normalizeChartNote(ChartNote& note, const ChartTuning& tuning)
{
    std::vector<ChartRepair> repairs;
    const auto fired = [&repairs](const ChartRepair repair) { repairs.push_back(repair); };

    // 1. The board ceiling. Clamps before floors and before the travel test, so those read final
    //    values; a waypoint is clamped rather than dropped because it still names real travel.
    bool past_board = note.fret > g_max_fret;
    note.fret = std::min(note.fret, g_max_fret);
    for (SlideWaypoint& waypoint : note.slides)
    {
        past_board = past_board || waypoint.fret > g_max_fret;
        waypoint.fret = std::min(waypoint.fret, g_max_fret);
    }
    if (note.slide_out.has_value() && note.slide_out->fret > g_max_fret)
    {
        past_board = true;
        note.slide_out->fret = g_max_fret;
    }
    if (past_board)
    {
        fired(ChartRepair::FretPastBoard);
    }

    // 2. The capo floor for every fret a slide gesture names (user ruling 2026-08-20, closing
    //    W9-J): a scrape's start and every exit lift to the first playable fret, because the pick
    //    travels the sounding string and a "scrape at the nut" is no scrape; a waypoint on or
    //    below the floor is dropped, since a pitched stop there is nothing pressed. A pressed
    //    NOTE on a capo'd fret is not repaired here: no lift can know the pitch the author meant,
    //    so it stays a refusal.
    const int floor = firstPlayableFret(tuning.capo);
    bool below_capo = false;
    if (isScrape(note.attack) && note.fret < floor)
    {
        below_capo = true;
        note.fret = floor;
    }
    const std::size_t waypoints_before = note.slides.size();
    std::erase_if(
        note.slides, [floor](const SlideWaypoint& waypoint) { return waypoint.fret < floor; });
    below_capo = below_capo || note.slides.size() != waypoints_before;
    if (note.slide_out.has_value() && note.slide_out->fret < floor)
    {
        below_capo = true;
        note.slide_out->fret = floor;
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
    if (note.dead && (!note.bend.empty() || note.vibrato))
    {
        note.bend.clear();
        note.vibrato = false;
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
    if (fretHandHarmonic(note) &&
        (!note.bend.empty() || note.vibrato || !note.slides.empty() || note.slide_out.has_value()))
    {
        note.bend.clear();
        note.vibrato = false;
        note.slides.clear();
        note.slide_out.reset();
        fired(ChartRepair::FretHandHarmonicPayload);
    }
    // An open string cannot slide: nothing is pressed to travel, so a fret-0 glide or trail-off
    // is dropped whole. A scrape never reaches this — its start was floored above.
    if (!isScrape(note.attack) && note.fret == 0 &&
        (!note.slides.empty() || note.slide_out.has_value()))
    {
        note.slides.clear();
        note.slide_out.reset();
        fired(ChartRepair::OpenStringSlide);
    }

    // 4. A strike from nowhere needs somewhere to land.
    if (flattenStrandedStrike(note))
    {
        fired(ChartRepair::StrandedStrike);
    }

    // 5. A scrape keeps traveling or it is no scrape: after the clamps, floors, and drops above,
    //    consecutive neck positions — start, turnarounds, exit — must strictly differ, because a
    //    pick cannot rest on a fret and still be scraping (an ordinary slide's equal-fret segment
    //    is a legitimate hold). A scrape without its terminal at all is missing data and stays a
    //    refusal, so only a present exit is judged. Demoted to the plain pick it sounds like, with
    //    its path cleared. The editor's scrape verb asks no question of its own here: it builds
    //    the path and lets the fixpoint judge it, so a held segment skips the note the same way.
    if (isScrape(note.attack) && note.slide_out.has_value() && !pickSlidePathTravels(note))
    {
        note.attack = NoteAttack::Pick;
        note.slides.clear();
        note.slide_out.reset();
        fired(ChartRepair::StilledScrape);
    }

    // 6. The muted tail, last: the tap-harmonic arm above can clear the tremolo that was a
    //    tail's only justification, and the stilled-scrape demotion can clear the slide payload
    //    that was, so the trim must read the note as it now stands.
    if (trimMutedTail(note))
    {
        fired(ChartRepair::MutedTail);
    }
    return repairs;
}

std::vector<ChartRepair> normalizeChordTemplate(ChordTemplate& chord_template)
{
    bool past_board = false;
    for (std::optional<int>& fret : chord_template.frets)
    {
        if (fret.has_value() && *fret > g_max_fret)
        {
            past_board = true;
            fret = g_max_fret;
        }
    }
    return past_board ? std::vector<ChartRepair>{ChartRepair::FretPastBoard}
                      : std::vector<ChartRepair>{};
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
    for (std::size_t index = 0; index < chart.templates.size(); ++index)
    {
        record(normalizeChordTemplate(chart.templates[index]), "template " + std::to_string(index));
    }
    for (FretHandPosition& position : chart.fret_hand_positions)
    {
        record(
            normalizeFretHandPosition(position, chart.tuning),
            "hand position " + positionText(position.position));
    }
    // The relational settle runs LAST, against the stream as it will actually stand: a trimmed
    // tail may have been the hold a neighbour's claim depended on.
    std::vector<ChartConversion> flattened =
        sweepUnjustifiedLegato(chart.notes, chart.shapes, tempo_map);
    conversions.insert(
        conversions.end(),
        std::make_move_iterator(flattened.begin()),
        std::make_move_iterator(flattened.end()));
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
    if (note.sustain.numerator < 0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "note sustain must not be negative at " + positionText(note.position),
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
    // Payload geometry no repair can express: a bend or slide point outside the sustain or out of
    // order is incoherent data, not a technique to shed. Where a waypoint sits on the NECK is the
    // normalizer's (the board clamp and the capo floor), asked as the fixpoint below.
    Fraction previous_offset{-1, 1};
    for (const BendPoint& point : note.bend)
    {
        if (point.offset.numerator < 0 || point.offset > note.sustain ||
            point.offset <= previous_offset)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message =
                    "bend offsets must ascend within the sustain at " + positionText(note.position),
            }};
        }
        previous_offset = point.offset;
    }
    previous_offset = Fraction{0};
    for (const SlideWaypoint& waypoint : note.slides)
    {
        if (waypoint.offset <= previous_offset || waypoint.offset > note.sustain ||
            waypoint.fret < 0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "slide waypoints must ascend within the sustain at " +
                           positionText(note.position),
            }};
        }
        previous_offset = waypoint.offset;
    }
    // A slide-out owns its geometry and must stay ordered like any payload.
    const SlideOut* const slide_out = slideOutOrNull(note);
    if (slide_out != nullptr && (slide_out->offset <= previous_offset ||
                                 slide_out->offset > note.sustain || slide_out->fret < 0))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNotePayload,
            .message = "slide-out must end after every waypoint, within the sustain at " +
                       positionText(note.position),
        }};
    }
    // A SAVED pick-slide note carries no pitched technique — the document writer omits them (the
    // in-memory override design, chart.h) — so a document that does is hand-made or a bug and
    // fails loudly; emphasis is a scrape's own dynamics and passes. The gesture is the required
    // unpitched slide-out terminal, exactly at the sustain (nothing rings past a scrape). That the
    // path keeps traveling is the normalizer's demotion, asked as the fixpoint below.
    if (isScrape(note.attack))
    {
        // Stated as a FIXPOINT rather than by listing the overridden fields: a saved note must
        // already equal its own saved form. Enumerating mute/node/vibrato/tremolo/bend here
        // duplicated exactly the set savedChartNote strips, so the writer and the validator had
        // to agree by hand and a sixth overridden field would have updated only one of them. (If
        // another attack ever gains latent overrides, lift this check out of the PickSlide branch
        // — the comparison is identity for every attack that has none.)
        if (!(savedChartNote(note) == note))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidPickSlide,
                .message = "pick-slide note must not carry pitched techniques at " +
                           positionText(note.position),
            }};
        }
        if (slide_out == nullptr || !(slide_out->offset == note.sustain))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidPickSlide,
                .message = "pick slide must end in a slide-out exactly at the sustain at " +
                           positionText(note.position),
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

        // A curve waypoint may never sit on a later onset of its own string: a glide into a real
        // note is the slideEnd "next" terminal, which stores no coordinates. Rejecting the
        // coordinate copy here is what keeps the desyncable encoding unrepresentable. Scrape
        // turnarounds are bound too; the scrape's sustain-parked terminal is its slide-out, which
        // this rule never sees.
        for (const SlideWaypoint& waypoint : note.slides)
        {
            const GridPosition waypoint_position =
                advanceGridPosition(tempo_map, note.position, waypoint.offset);
            for (auto at_waypoint = std::ranges::lower_bound(
                     notes, waypoint_position, std::ranges::less{}, &ChartNote::position);
                 at_waypoint != notes.end() && at_waypoint->position == waypoint_position;
                 ++at_waypoint)
            {
                if (at_waypoint->string == note.string)
                {
                    return std::unexpected{ChartError{
                        .code = ChartErrorCode::InvalidNotePayload,
                        .message = "slide waypoint may not sit on a later onset of its string at " +
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
