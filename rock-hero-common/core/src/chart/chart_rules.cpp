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
// when present (the slide-out keyframe, last in the same list) — all strictly differ. Only the
// POSITION channel is a neck position; a keyframe carrying nothing but a latent bend or vibrato
// passes through without breaking the travel.
[[nodiscard]] bool pickSlidePathTravels(const ChartNote& note)
{
    int previous_fret = note.fret;
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object
        // (bugprone-unchecked-optional-access does not credit a guard on a loop variable's member).
        const std::optional<int>& fret = keyframe.fret;
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
    return true;
}

// True when any keyframe states a pitch modulation — the channels a dead string cannot carry.
[[nodiscard]] bool statesModulation(const std::vector<Keyframe>& keyframes)
{
    return std::ranges::any_of(keyframes, [](const Keyframe& keyframe) {
        return keyframe.bend.has_value() || hasVibrato(keyframe.vibrato);
    });
}

// A scrape's terminal, given a new AIM where compression makes its fret meet the fret it now
// follows: the nearest EARLIER differing fret takes over — including one the clip is about to
// remove — so the path never sits still. Asked of the terminal already detached from the note and
// of the keyframes the new `sustain` leaves standing. Only the POSITION channel is re-aimed; a
// terminal stating no fret is no scrape's (the attack requires one) and is left alone.
void reAimScrapeTerminal(const ChartNote& note, Keyframe& terminal, const Fraction sustain)
{
    // Bound to a local so the presence test and every read below are provably the same object.
    std::optional<int>& aim = terminal.fret;
    if (!aim.has_value())
    {
        return;
    }
    int surviving_fret = note.fret;
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (!(keyframe.offset < sustain))
        {
            break;
        }
        surviving_fret = keyframe.fret.value_or(surviving_fret);
    }
    for (const Keyframe& keyframe : std::ranges::reverse_view(note.keyframes))
    {
        if (*aim != surviving_fret)
        {
            break;
        }
        const std::optional<int>& fret = keyframe.fret;
        if (fret.has_value())
        {
            aim = fret;
        }
    }
}

} // namespace

void dropNotePath(ChartNote& note)
{
    static_cast<void>(stripKeyframeChannels(note, [](Keyframe& keyframe) {
        const bool stated = keyframe.fret.has_value();
        keyframe.fret.reset();
        return stated;
    }));
}

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1} &&
           isOnTickLattice(tempo_map, position);
}

// The grid rule plus the song's end: every marker kind asks this one question, so a section, a tone
// change and a hand position can never disagree about where the song stops holding one.
bool markerCanStartAt(const GridPosition& position, const TempoMap& tempo_map)
{
    return isValidGridPosition(position, tempo_map) && position < terminalGridPosition(tempo_map);
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
    // with the frets it reads — every one of them belongs to a note this validator judges — and a
    // derived span's length and order are properties of the walk that built it.

    if (auto placements =
            validateFretHandPositions(chart.fret_hand_positions, chart.tuning, tempo_map);
        !placements.has_value())
    {
        return std::unexpected{std::move(placements.error())};
    }

    return validateChartNotes(chart.notes, chart.tuning, tempo_map);
}

// Separate from validateChartRules so a placement edit can be judged without re-judging every note
// under it; the chart gate asks exactly this for its own stream.
std::expected<void, ChartError> validateFretHandPositions(
    const std::vector<FretHandPosition>& placements, const ChartTuning& tuning,
    const TempoMap& tempo_map)
{
    const FretHandPosition* previous_fhp = nullptr;
    for (const FretHandPosition& fhp : placements)
    {
        // A position off the grid or past the song has no repair; where the window SITS is the
        // normalizer's fit (above the capo, the narrowest window under the last fret), asked as the
        // fixpoint.
        if (!markerCanStartAt(fhp.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand position is off the grid or past the song at " +
                           positionText(fhp.position),
            }};
        }
        // An authored end below the index finger is a window reaching nowhere, and no fit can say
        // which of the two frets was meant.
        if (fhp.end_fret.has_value() && *fhp.end_fret < fhp.fret)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message =
                    "fret-hand window ends below its index finger at " + positionText(fhp.position),
            }};
        }
        FretHandPosition normal = fhp;
        if (const std::vector<ChartRepair> repairs = normalizeFretHandPosition(normal, tuning);
            !repairs.empty())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = std::string{chartRepairText(repairs.front())} + " at " +
                           positionText(fhp.position),
            }};
        }
        // Strictly ascending: two placements at one instant would leave "where the hand is" with
        // two answers.
        if (previous_fhp != nullptr && !(previous_fhp->position < fhp.position))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand positions must be strictly ascending at " +
                           positionText(fhp.position),
            }};
        }
        previous_fhp = &fhp;
    }
    return {};
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
        case ChartRepair::DisabledHarmonic:
        {
            return "artificial and tapped harmonics are not supported yet, so one became a plain "
                   "note at its pressed stop";
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
        case ChartRepair::EndStatementVibrato:
        {
            return "vibrato stated where the string is let go had no ring to vibrate in and was "
                   "dropped";
        }
        case ChartRepair::MidTravelVibrato:
        {
            return "a vibrato change stood where the hand travels between two frets and was taken "
                   "back";
        }
        case ChartRepair::SilentKeyframe:
        {
            return "a keyframe stated nothing the path did not already say and was dropped";
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

// A POINT NEVER LEAVES THE RING, AND NEVER MOVES BECAUSE THE RING DID — except the one whose
// moment IS the ring's end. That statement is stated AT the end, so a ring shortened under it
// carries it with the end (it comes sooner — there is nowhere else for it to be), WHATEVER it
// states: a slide-out toward a fret, the bend curve's last value, or both. A ring lengthened past
// it leaves the statement where it was, a pitched stop now, the tail running on as a plain ring —
// the ribbon moved and the point stayed, exactly as every other keyframe stays. Kind is position,
// so that is how a slide-out becomes a regular slide; the slide-out's own handle for the
// SLIDE-OUT's length is the move verb, which drags the ring's end with it (planMoveSelection) — and
// only for the point that IS the slide-out, since that verb keeps every other point strictly inside
// the ring rather than letting a step change what a point is. A scrape's terminal rides both ways,
// because a scrape rings exactly as long as the pick travels and its terminal is required at the
// end.
bool clipPayloadsToSustain(ChartNote& note, const Fraction sustain)
{
    const bool shortening = sustain < note.sustain;
    // The end's statement detaches WHOLE — the keyframe carrying it is about to be clipped like any
    // statement past the new end — and re-attaches at the end the clip settles, so no keyframe ever
    // sits past the ring and no two sit at one offset. A ring ending at ZERO carries nothing: an
    // offset is strictly positive, so there is no end for a statement to stand at, and the erase
    // below takes every keyframe with the dropped tail.
    std::optional<Keyframe> ridden;
    if (const Keyframe* const end = endStatement(note);
        end != nullptr && sustain.numerator > 0 && (shortening || isScrape(note.attack)))
    {
        // Emplaced into a reference so the copy off the note happens before the pop and nothing
        // below reads through the optional.
        Keyframe& carried = ridden.emplace(*end);
        note.keyframes.pop_back();
        // A scrape re-aims before the clip, because the fret the terminal falls back on may be one
        // the clip is about to remove.
        if (isScrape(note.attack))
        {
            reAimScrapeTerminal(note, carried, sustain);
        }
    }

    note.sustain = sustain;
    // The bound is inclusive for every channel: a statement standing exactly at the new end
    // survives, whatever it states — and the end's own statement, arriving back on top of it,
    // overlays it (setEndStatement). Where that end is a head of the note's own string the
    // statement STANDS on it, which is what the store says the hands did; presentation only stops
    // the ink one margin before that head (chartPresentation rule 1). Heads are no business of a
    // clip.
    bool lost = std::erase_if(note.keyframes, [&note](const Keyframe& keyframe) {
                    return note.sustain < keyframe.offset;
                }) != 0;
    if (ridden.has_value())
    {
        lost = setEndStatement(note, *ridden) || lost;
    }
    // Whatever stands at the end leaves no VIBRATO: a state stated where the ring stops has no ring
    // left to vibrate in. Its BEND stays, the curve's last value shaping the final leg — the
    // channel table decides, and it asks nothing about the gesture the fret beside it proves.
    // Vibrato shed here was a point's own, standing at the new end: an authored statement lost.
    return shedEndStatementVibrato(note) || lost;
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
    // Every note is a strike, so the next one on the string is the bound.
    const auto next = std::ranges::find(later, note.string, &ChartNote::string);
    if (next == later.end())
    {
        return std::nullopt;
    }
    return beatDistance(tempo_map, note.position, next->position);
}

Fraction ringEndWithinBound(
    const std::vector<ChartNote>& notes, const ChartNote& note, const TempoMap& tempo_map,
    const Fraction target)
{
    const std::optional<Fraction> bound = sustainBoundOf(notes, note, tempo_map);
    if (bound.has_value() && !(target < *bound))
    {
        return *bound;
    }
    return target;
}

// A ring past its bound ends exactly on it (adjacency is legal), clipping payloads with the tail.
std::vector<TailTruncation> normalizeSustainOverlaps(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    std::vector<TailTruncation> truncated;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        const Fraction end = ringEndWithinBound(notes, note, tempo_map, note.sustain);
        if (!(end < note.sustain))
        {
            continue;
        }
        truncated.push_back(
            TailTruncation{
                .index = index,
                .statement_lost = clipPayloadsToSustain(note, end),
            });
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
    for (Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        std::optional<int>& fret = keyframe.fret;
        if (fret.has_value() && *fret > g_max_fret)
        {
            past_board = true;
            fret = g_max_fret;
        }
    }
    if (past_board)
    {
        fired(ChartRepair::FretPastBoard);
    }

    // 2. The capo floor for every fret a slide gesture names: a scrape's start and every slide-out
    //    lift to the first playable fret, because the pick travels the sounding string and a
    //    "scrape at the nut" is no scrape, and a slide-out names a direction as much as a fret — a
    //    slide-out toward the floor is still a slide-out; a PITCHED keyframe on or below the floor
    //    loses its position instead, since a stop there is nothing pressed — stripped per channel,
    //    so a bend or vibrato change authored at the same instant survives the lift, and a
    //    keyframe left bare stays as the beginning of the leg it was (the commit law sweeps it
    //    where that says nothing). A pressed NOTE on a capo'd fret is not repaired here: no lift
    //    can know the pitch the author meant, so it stays a refusal.
    const int floor = firstPlayableFret(tuning.capo);
    bool below_capo = false;
    if (isScrape(note.attack) && note.fret < floor)
    {
        below_capo = true;
        note.fret = floor;
    }
    const bool lifted_a_keyframe = stripKeyframeChannels(note, [floor, &note](Keyframe& keyframe) {
        // Bound to a local so the optional check and the access are provably the same object.
        std::optional<int>& fret = keyframe.fret;
        if (!fret.has_value() || *fret >= floor)
        {
            return false;
        }
        if (keyframe.offset == note.sustain)
        {
            fret = floor;
            return true;
        }
        fret.reset();
        return true;
    });
    below_capo = below_capo || lifted_a_keyframe;
    if (below_capo)
    {
        fired(ChartRepair::FretBelowCapo);
    }

    // 3. The technique exclusions. The DEADENING outranks the harmonic: a player can hold a
    //    harmonic's shape while damping, and the node then says where the hand is rather than what
    //    rings — exactly how a dead note's own FRET already reads — so the note stays dead and
    //    keeps its node. What it cannot keep is pitch MODULATION, which has no positional reading.
    //    The palm flag is untouched throughout: it says where the picking hand is, never what the
    //    string sounds.
    if (note.dead && (std::is_neq(note.bend <=> 0.0) || hasVibrato(note.vibrato) ||
                      statesModulation(note.keyframes)))
    {
        note.bend = 0.0;
        note.vibrato = VibratoState::None;
        // The modulation CHANNELS go; the position channel stays, because a dead string still
        // travels — a dragged mute is exactly that.
        static_cast<void>(stripKeyframeChannels(note, [](Keyframe& keyframe) {
            const bool modulated = keyframe.bend.has_value() || hasVibrato(keyframe.vibrato);
            keyframe.bend.reset();
            keyframe.vibrato = VibratoState::None;
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
    // Only the natural and the pinch harmonic are supported for now. An ARTIFICIAL harmonic (a node
    // over a pressed stop under a fretting-hand attack) and a TAPPED one (a node under the tap
    // attack) are reduced to a plain note at the pressed stop — the fundamental the harmonic
    // divides — so no chart can hold either form while its display is unsettled: the normal-form
    // fixpoint in validateChartNoteAlone refuses the un-normalized record, the loader and the
    // import shed apply this repair and report it. The code that would draw the forms stays in
    // place behind this one rule, but it reads the pressed stop off the note's own `fret`, which is
    // NOT the reopened design: those harmonics will take their held stop from the stated grip, so
    // that code needs rework before this rule lifts
    // (docs/plans/in-progress/harmonic-display-followups.md). Pinch is
    // excluded by harmonicOverPressedStop (its node lies off the neck); the tap arm catches the
    // open-string tapped harmonic, whose stop is 0.
    if (harmonicOverPressedStop(note) ||
        (note.attack == NoteAttack::Tap && note.harmonic_node.has_value()))
    {
        // The tapping finger only touched the node; with the node gone the fretting hand's stop is
        // what sounds, so the onset becomes the pick that sounds it.
        if (note.attack == NoteAttack::Tap)
        {
            note.attack = NoteAttack::Pick;
        }
        note.harmonic_node.reset();
        fired(ChartRepair::DisabledHarmonic);
    }
    // A tap harmonic's damping finger leaves the string, so nothing holds the node under
    // re-picking.
    if (note.attack == NoteAttack::Tap && note.harmonic_node.has_value() && note.tremolo)
    {
        note.tremolo = false;
        fired(ChartRepair::TapHarmonicTremolo);
    }
    // A fret-hand harmonic touches its node with nothing pressed: there is no press to bend,
    // vibrato, or carry anywhere, and moving the touch off the node just stops the harmonic.
    if (fretHandHarmonic(note) &&
        (std::is_neq(note.bend <=> 0.0) || hasVibrato(note.vibrato) || !note.keyframes.empty()))
    {
        note.bend = 0.0;
        note.vibrato = VibratoState::None;
        // Every channel goes here rather than one of them, so the whole array goes with them:
        // there is no statement a touch with nothing pressed can make about its own ring.
        note.keyframes.clear();
        fired(ChartRepair::FretHandHarmonicPayload);
    }
    // An open string cannot slide: nothing is pressed to travel, so a fret-0 glide or slide-out
    // loses its position channel. A scrape never reaches this — its start was floored above.
    if (!isScrape(note.attack) && note.fret == 0 && anyKeyframeStatesFret(note.keyframes))
    {
        dropNotePath(note);
        fired(ChartRepair::OpenStringSlide);
    }
    // AN END STATEMENT LEAVES NO VIBRATO: the string is let go there, so a state stated at that
    // instant has no ring to sound in. The bend stays, being the curve's last value, which shapes
    // the final leg into the end whether that end slides out or arrives into the next head.
    if (shedEndStatementVibrato(note))
    {
        fired(ChartRepair::EndStatementVibrato);
    }
    // NO VIBRATO CHANGE MID-TRAVEL: a width may run through a glide, but none begins strictly
    // inside one. After the end's shed, so a slide-out's own statement is already settled.
    if (shedMidTravelVibrato(note))
    {
        fired(ChartRepair::MidTravelVibrato);
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
    if (isScrape(note.attack) && endStatedFretOrNull(note) != nullptr &&
        !pickSlidePathTravels(note))
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
    // The window's reach is derived from the notes (deriveFretHandWidths), which never hold a fret
    // past the board, so the index finger only has to leave room for the narrowest window — kept
    // even under an authored end, so clearing that end always leaves a legal window. Every legal
    // capo leaves that room above it; the floor still wins on a capo out of range, which the
    // validator refuses after this runs.
    static_assert(g_max_fret - g_max_capo >= g_min_fret_hand_width);
    const int floor = firstPlayableFret(tuning.capo);
    const int overshoot =
        FretWindow{.fret = position.fret, .width = g_min_fret_hand_width}.top() - g_max_fret;
    bool past_board = overshoot > 0;
    if (past_board)
    {
        position.fret -= overshoot;
    }
    const bool below_capo = position.fret < floor;
    position.fret = std::max(position.fret, floor);
    // An authored end past the last fret comes down onto it, the same fit the finger takes.
    if (position.end_fret.has_value() && *position.end_fret > g_max_fret)
    {
        position.end_fret = g_max_fret;
        past_board = true;
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
        // THE KEYFRAME COMMIT LAW's load half: a point that says nothing the path does not
        // already say is never written, so one that arrives is junk and goes
        // (keyframeSaysNothingNew). Here and not in the per-note normalizer, deliberately: the
        // validator mirrors that one as a fixpoint, and such a point is legal in memory — the
        // editor's plan gate must keep accepting it, the end's own statement included: every silent
        // point wears a mark the charter can see and reach, and the editor takes it when its note
        // leaves focus rather than on any load or presentation path.
        if (stripSilentKeyframes(note))
        {
            record(
                {ChartRepair::SilentKeyframe},
                positionText(note.position) + " string " + std::to_string(note.string));
        }
    }
    // The one rule a note cannot obey alone (40-Q2-B): a re-strike stops the ring. It runs here
    // rather than at each producer, so a chart written before the rule — or by a converter that
    // never learned it — is truncated and REPORTED on load instead of drawing a tail through a
    // later head.
    for (const TailTruncation& truncation : normalizeSustainOverlaps(chart.notes, tempo_map))
    {
        const ChartNote& note = chart.notes[truncation.index];
        conversions.push_back(
            ChartConversion{
                .repair = ChartRepair::OverlappingTail,
                .where = positionText(note.position) + " string " + std::to_string(note.string),
            });
    }
    for (FretHandPosition& position : chart.fret_hand_positions)
    {
        record(
            normalizeFretHandPosition(position, chart.tuning),
            "hand position " + positionText(position.position));
    }
    // The relational settle runs LAST, against the stream as it will actually stand: a trimmed
    // tail may have been the hold a neighbour's legato claim depended on.
    std::vector<ChartConversion> settled = sweepUnjustifiedLegato(chart.notes, tempo_map);
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
    // The ring. Every struck string rings for SOME length — a dead note's damped stroke included —
    // so the sustain is the actual duration and is strictly positive on every note. No repair can
    // express that: a duration is information, and inventing one would be authoring the chart. It
    // doubles as the format tripwire for any zero that reaches memory, which is why the message
    // names the cause rather than the field. A package written before the duration model rarely
    // arrives here: that writer OMITTED the key on every tail-less note, so the document reader
    // refuses it first, with the same re-import remedy.
    if (note.sustain.numerator <= 0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "note sustain must be positive at " + positionText(note.position) +
                       "; this chart predates the note duration model and must be re-imported",
        }};
    }
    // Where the ring ends is a stored instant like the onset, so it lies on the tick lattice too:
    // the chart can state no instant finer than a tick, and none between two.
    if (!isOnTickLattice(tempo_map, sustainEndPosition(tempo_map, note)))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "note ring must end on the tick lattice at " + positionText(note.position),
        }};
    }
    // A legal node is stated as what it IS, in positive form: a node now takes part in the
    // posture map's ordering key (\ref ChartStop), and NaN — which passes both halves of the
    // negative form — would be a strict-weak-ordering violation there.
    if (note.harmonic_node.has_value() &&
        !(*note.harmonic_node > 0.0 && *note.harmonic_node <= g_max_harmonic_node))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "harmonic node position is out of range at " + positionText(note.position),
        }};
    }
    // A node lies on the speaking length, so it cannot sit at or behind the physical stop —
    // nothing vibrates there. The stop is the note's own fret (\ref physicalStopFret) whichever
    // hand touches the node: a tapped harmonic states the fret its fretting hand presses exactly
    // as an artificial one does, so the node rides that stop under either.
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
    // open form), asked as the fixpoint below.
    if (note.fret != 0 && note.fret < firstPlayableFret(tuning.capo) && !isScrape(note.attack))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNote,
            .message = "fret must be 0 or above the capo at " + positionText(note.position),
        }};
    }
    // A finger cannot lower a stopped string's pitch, so a negative push is a data error rather
    // than a technique; dips and dives belong to the whammy bar's own model. Checked at the onset
    // value here and at every keyframe below, because the channel is one channel.
    if (note.bend < 0.0)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidNotePayload,
            .message = "bend amount must not be negative at " + positionText(note.position),
        }};
    }
    // Payload geometry no repair can express: a statement outside the sustain or out of order is
    // incoherent data, not a technique to shed. Where a keyframe sits on
    // the NECK is the normalizer's (the board clamp and the capo floor), asked as the fixpoint
    // below.
    //
    // Offsets are STRICTLY positive: offset zero is the onset, whose facts the note itself carries,
    // so a keyframe there would be a second spelling of a value the note already states. Strictly
    // ascending and bounded by the sustain is also what makes the slide-out unique: at most one
    // keyframe can sit at the ring's end, so "the fret stated where the sound stops" names one
    // statement or none.
    Fraction previous_offset{0};
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.offset <= previous_offset || keyframe.offset > note.sustain)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "keyframe offsets must ascend within the sustain at " +
                           positionText(note.position),
            }};
        }
        previous_offset = keyframe.offset;
        if (!isOnTickLattice(
                tempo_map, advanceGridPosition(tempo_map, note.position, keyframe.offset)))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message =
                    "keyframe must lie on the tick lattice at " + positionText(note.position),
            }};
        }
        // Bound to locals so each optional check and its access are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (fret.has_value() && *fret < 0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "keyframe fret must not be negative at " + positionText(note.position),
            }};
        }
        const std::optional<double>& bend = keyframe.bend;
        if (bend.has_value() && *bend < 0.0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "bend amount must not be negative at " + positionText(note.position),
            }};
        }
    }
    // WHAT THIS NOTE MAY STATE, asked as a FIXPOINT rather than by listing fields: a saved note
    // must already equal its own saved form. A SAVED pick slide carries no pitched technique,
    // because the writer omits the in-memory overrides (chart.h), and enumerating that set here
    // would duplicate exactly what savedChartNote strips, leaving the writer and this rule to agree
    // by hand while a field added to ChartNote updated only one of them. Asked unconditionally
    // because the comparison is identity for every note that overrides nothing, so only a scrape
    // can fail it. Emphasis is a scrape's own dynamics and is never stripped.
    if (!(savedChartNote(note) == note))
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidPickSlide,
            .message = "pick-slide note must not carry pitched techniques at " +
                       positionText(note.position),
        }};
    }
    // The scrape's own gesture: the required unpitched terminal, exactly at the sustain (nothing
    // rings past a scrape) — the slide-out keyframe. That the path keeps traveling is the
    // normalizer's demotion, asked as the fixpoint below.
    if (isScrape(note.attack))
    {
        // Presence is the whole rule: a slide-out IS the keyframe at the ring's end, so one that
        // exists sits exactly at the sustain and there is no second coordinate to disagree with.
        if (endStatedFretOrNull(note) == nullptr)
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

        // What a note's neighbours make of its ring — the same-string bound — is normalized, never
        // refused: every producer runs normalizeSustainOverlaps before it validates.
        previous_note = &note;
    }

    return std::expected<void, ChartError>{};
}

} // namespace rock_hero::common::core
