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
// when present (the release keyframe, last in the same list) — all strictly differ. Only the
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
        return keyframe.bend.has_value() || keyframe.vibrato.has_value();
    });
}

} // namespace

void dropNotePath(ChartNote& note)
{
    static_cast<void>(stripKeyframeChannels(note.keyframes, [](Keyframe& keyframe) {
        const bool stated = keyframe.fret.has_value();
        keyframe.fret.reset();
        return stated;
    }));
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
    // with the frets it reads — every one of them belongs to a note this validator judges — and a
    // derived span's length and order are properties of the walk that built it.

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
        case ChartRepair::CrowdedKeyframe:
        {
            return "a keyframe stood on the next onset of its string and was moved back to its "
                   "clearance";
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
        case ChartRepair::InertHeldStop:
        {
            return "a held stop belonged to no shape, so it was cleared from the onset carrying it";
        }
        case ChartRepair::DerivedHeldStop:
        {
            return "a pull-off already states the stop under its onset, so the stored held fret "
                   "was dropped";
        }
        case ChartRepair::ReleasePayload:
        {
            return "a bend or shake stated where the string is let go sounds nothing and was "
                   "dropped";
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

// A POINT NEVER LEAVES THE RING, AND NEVER MOVES BECAUSE THE RING DID. A release is stated AT the
// end, so a ring shortened under it carries the release with the end (the release comes sooner —
// there is nowhere else for it to be); a ring lengthened past it leaves the statement where it
// was, a pitched stop now, the tail running on as a plain ring — the ribbon moved and the point
// stayed, exactly as every other keyframe stays. Kind is position, so that is how a slide-out
// becomes a regular slide; the release's own handle for the FALL's length is the move verb, which
// drags the ring's end with it (planMoveSelection). A scrape's terminal rides both ways, because a
// scrape rings exactly as long as the pick travels and its terminal is required at the end. What
// a scrape's terminal still needs is a new AIM when compression makes its fret meet the fret it
// now follows.
void clipPayloadsToSustain(ChartNote& note, const Fraction sustain)
{
    const bool shortening = sustain < note.sustain;
    // The release detaches first — read as the fret it names, because the keyframe carrying it is
    // about to be clipped like any statement past the new end — and re-attaches at the end the
    // clip settles, so no keyframe ever sits past the ring and no two sit at one offset.
    std::optional<int> ridden;
    if (const int* const release = slideOutFretOrNull(note);
        release != nullptr && (shortening || isScrape(note.attack)))
    {
        ridden = *release;
        clearSlideOut(note);
    }
    // A scrape re-aims before the clip, because the fret the terminal falls back on may be one the
    // clip is about to remove: the nearest EARLIER differing fret takes over so the path never
    // sits still.
    if (ridden.has_value() && isScrape(note.attack))
    {
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
            if (*ridden != surviving_fret)
            {
                break;
            }
            const std::optional<int>& fret = keyframe.fret;
            if (fret.has_value())
            {
                ridden = fret;
            }
        }
    }

    note.sustain = sustain;
    // The bound is inclusive for every channel: a statement standing exactly at the new end
    // survives, whatever it states. Where that end is a head of the note's own string, the
    // clearance repair (normalizeKeyframeClearances) moves the statement back — heads are no
    // business of a clip.
    std::erase_if(note.keyframes, [&note](const Keyframe& keyframe) {
        return note.sustain < keyframe.offset;
    });
    if (ridden.has_value())
    {
        setSlideOut(note, *ridden);
    }
    // An end that lands exactly on a stated fret makes that fret the release — and a release
    // states its fret and nothing else, so a shake or a bend the point carried as a stop goes with
    // the ring that would have sounded it.
    static_cast<void>(stripReleaseChannels(note));
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

std::optional<Fraction> keyframeClearanceOf(
    const std::vector<ChartNote>& notes, const ChartNote& note, const TempoMap& tempo_map)
{
    if (note.keyframes.empty())
    {
        return std::nullopt;
    }
    const std::optional<Fraction> bound = sustainBoundOf(notes, note, tempo_map);
    if (!bound.has_value())
    {
        return std::nullopt;
    }
    // The last leg starts at the statement before the last keyframe — the onset when there is
    // none — and the clearance never takes it: a real landing at a junction the margin line falls
    // on would otherwise be overwritten by the statement moved onto it.
    const std::size_t count = note.keyframes.size();
    const Fraction leg_start = count > 1 ? note.keyframes[count - 2].offset : Fraction{};
    return latestStatementBeforeStrike(
        *bound,
        minimumSustainDistanceBeats(tempo_map.timeSignatureAt(note.position.measure).denominator),
        leg_start);
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
        clipPayloadsToSustain(note, *bound);
        truncated.push_back(index);
    }
    return truncated;
}

// Only a keyframe ON the head moves — one inside the margin is a charter's deliberate placement
// and stands. The release IS the ring's end, so it moves by resizing the ring
// (clipPayloadsToSustain re-attaches it at the new end); any other statement moves alone and the
// ring keeps its length. Only the last keyframe can reach the head: every other one stands
// strictly before it.
std::vector<std::size_t> normalizeKeyframeClearances(
    std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    std::vector<std::size_t> moved;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        ChartNote& note = notes[index];
        const std::optional<Fraction> bound = sustainBoundOf(notes, note, tempo_map);
        if (note.keyframes.empty() || !bound.has_value() || note.keyframes.back().offset < *bound)
        {
            continue;
        }
        const std::optional<Fraction> clearance = keyframeClearanceOf(notes, note, tempo_map);
        if (!clearance.has_value())
        {
            continue;
        }
        if (releaseKeyframe(note) != nullptr)
        {
            clipPayloadsToSustain(note, *clearance);
        }
        else
        {
            note.keyframes.back().offset = *clearance;
        }
        moved.push_back(index);
    }
    return moved;
}

std::vector<ChartRepair> normalizeChartNote(ChartNote& note, const ChartTuning& tuning)
{
    std::vector<ChartRepair> repairs;
    const auto fired = [&repairs](const ChartRepair repair) { repairs.push_back(repair); };

    // 1. The board ceiling. Clamps before floors and before the travel test, so those read final
    //    values; a stated fret is clamped rather than stripped because it still names real travel.
    bool past_board = note.fret > g_max_fret;
    note.fret = std::min(note.fret, g_max_fret);
    // The held stop is a stop on the same neck, so the same ceiling clamps it. Bound to a local so
    // the optional check and the access are provably the same object.
    std::optional<int>& held = note.held;
    if (held.has_value() && *held > g_max_fret)
    {
        past_board = true;
        held = g_max_fret;
    }
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

    // 2. The capo floor for every fret a slide gesture names: a scrape's start and every release
    //    lift to the first playable fret, because the pick travels the sounding string and a
    //    "scrape at the nut" is no scrape, and a release names a direction as much as a fret — a
    //    fall toward the floor is still a fall; a PITCHED keyframe on or below the floor loses its
    //    position instead, since a stop there is nothing pressed — stripped per channel, so a bend
    //    or vibrato change authored at the same instant survives the lift and only a keyframe left
    //    stating nothing goes. A pressed NOTE on a capo'd fret is not repaired here: no lift can
    //    know the pitch the author meant, so it stays a refusal.
    const int floor = firstPlayableFret(tuning.capo);
    bool below_capo = false;
    if (isScrape(note.attack) && note.fret < floor)
    {
        below_capo = true;
        note.fret = floor;
    }
    const bool lifted_a_keyframe =
        stripKeyframeChannels(note.keyframes, [floor, &note](Keyframe& keyframe) {
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
    if (note.dead && (std::is_neq(note.bend <=> 0.0) || isShaking(note.vibrato) ||
                      statesModulation(note.keyframes)))
    {
        note.bend = 0.0;
        note.vibrato = VibratoState::Off;
        // The modulation CHANNELS go; the position channel stays, because a dead string still
        // travels — a dragged mute is exactly that.
        static_cast<void>(stripKeyframeChannels(note.keyframes, [](Keyframe& keyframe) {
            const bool modulated = keyframe.bend.has_value() || keyframe.vibrato.has_value();
            keyframe.bend.reset();
            keyframe.vibrato.reset();
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
    if (fretHandHarmonic(note) &&
        (std::is_neq(note.bend <=> 0.0) || isShaking(note.vibrato) || !note.keyframes.empty()))
    {
        note.bend = 0.0;
        note.vibrato = VibratoState::Off;
        // Every channel goes here rather than one of them, so the whole array goes with them:
        // there is no statement a touch with nothing pressed can make about its own ring.
        note.keyframes.clear();
        fired(ChartRepair::FretHandHarmonicPayload);
    }
    // An open string cannot slide: nothing is pressed to travel, so a fret-0 glide or trail-off
    // loses its position channel. A scrape never reaches this — its start was floored above.
    if (!isScrape(note.attack) && note.fret == 0 && anyKeyframeStatesFret(note.keyframes))
    {
        dropNotePath(note);
        fired(ChartRepair::OpenStringSlide);
    }
    // A release states its fret and nothing else: the string is let go there, so a bend or a
    // shake stated at that instant has no ring to sound in.
    if (stripReleaseChannels(note))
    {
        fired(ChartRepair::ReleasePayload);
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
    if (isScrape(note.attack) && slideOutFretOrNull(note) != nullptr && !pickSlidePathTravels(note))
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
        // THE KEYFRAME COMMIT LAW's load half: a point that says nothing the path does not
        // already say is never written, so one that arrives is junk and goes
        // (keyframeSaysNothingNew). Here and not in the per-note normalizer, deliberately: the
        // validator mirrors that one as a fixpoint, and such a point is legal in memory — the
        // editor's plan gate must keep accepting it.
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
    for (const std::size_t index : normalizeSustainOverlaps(chart.notes, tempo_map))
    {
        conversions.push_back(
            ChartConversion{
                .repair = ChartRepair::OverlappingTail,
                .where = positionText(chart.notes[index].position) + " string " +
                         std::to_string(chart.notes[index].string),
            });
    }
    // The other rule a note cannot obey alone: no keyframe sits on a head of its own string. After
    // the truncation, which is what carries a statement onto the head.
    for (const std::size_t index : normalizeKeyframeClearances(chart.notes, tempo_map))
    {
        conversions.push_back(
            ChartConversion{
                .repair = ChartRepair::CrowdedKeyframe,
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
    // tail may have been the hold a neighbour's claim depended on. Nothing the claim sweep takes
    // can justify or withdraw a legato claim, since a held stop neither sounds nor bounds a ring —
    // clearing the field leaves the onset carrying it entirely untouched.
    //
    // The legato settle's OWN precedence over the claim sweep is not a dependency: rule 11 keys
    // spans by POSITION rather than by articulation, and flattening writes an attack and nothing
    // else, so the spans the claim sweep judges against are the same either way. The order is kept
    // because it is the order the repairs read in, not because the answer depends on it.
    std::vector<ChartConversion> settled = sweepUnjustifiedLegato(chart.notes, tempo_map);
    // The residue sweep runs BEFORE the inert one and AFTER the legato settle, and both orders are
    // the same rule: judge a stored held stop against the connections as they will finally stand.
    // A flattened claim is no longer a pull-off, so it states nothing and its predecessor's field
    // is authored truth again; and clearing residue first is what reports a superseded field under
    // the law that explains it rather than as a claim that stated nothing.
    std::vector<ChartConversion> residue = sweepDerivedHeldStops(chart.notes, tempo_map);
    std::vector<ChartConversion> swept = sweepInertClaimedStops(chart.notes, tempo_map);
    settled.insert(
        settled.end(),
        std::make_move_iterator(residue.begin()),
        std::make_move_iterator(residue.end()));
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
    // The finger the fretting hand plants under an onset the picking hand stops the string for.
    // WHICH notes may carry one is the fixpoint below; these are the two facts a stop of its own
    // has. The board and the capo bind it exactly as they bind `fret` — 0 is the open string a
    // voicing deliberately leaves, and a stop the capo covers has no repair that is not an invented
    // pitch — while the ceiling is the normalizer's clamp, asked as that same fixpoint.
    //
    // And it must lie OUTSIDE the onset's own travel: the planted finger is on the string, so the
    // picking hand cannot start on it, end on it, or pass through it. One rule for both shapes
    // that can carry a stop, because \ref travelsThroughFret reads the PATH rather than the attack
    // — an onset stating none has a hull of one point, which is the equal-fret refusal as the
    // degenerate case, while a scrape always states a path and a tap does wherever the charter
    // wrote one, and the finger is in the way anywhere along it. Such a record is a physical
    // impossibility rather than a technique to shed, so it stays a refusal.
    //
    // Bound to a local so the optional check and the accesses are provably the same object.
    const std::optional<int>& held = note.held;
    if (held.has_value())
    {
        if (*held < 0 || (*held != 0 && *held < firstPlayableFret(tuning.capo)))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message =
                    "held stop must be 0 or above the capo at " + positionText(note.position),
            }};
        }
        if (travelsThroughFret(note, *held))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message = "a held stop is a planted finger: the onset cannot start on, end on, "
                           "or pass through its fret at " +
                           positionText(note.position),
            }};
        }
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
    // Payload geometry no repair can express: a statement outside the sustain, out of order, or
    // stating nothing at all is incoherent data, not a technique to shed. Where a keyframe sits on
    // the NECK is the normalizer's (the board clamp and the capo floor), asked as the fixpoint
    // below.
    //
    // Offsets are STRICTLY positive: offset zero is the onset, whose facts the note itself carries,
    // so a keyframe there would be a second spelling of a value the note already states. Strictly
    // ascending and bounded by the sustain is also what makes the release unique: at most one
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
        if (keyframeStatesNothing(keyframe))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "keyframe must state a fret, a bend, or a vibrato change at " +
                           positionText(note.position),
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
    // must already equal its own saved form. Two things carry less than the whole record — a SAVED
    // pick slide carries no pitched technique, because the writer omits the in-memory overrides
    // (chart.h); and a HELD stop rides only a note the picking hand stops the string for, because
    // everywhere else — an ordinary press, and a harmonic of either hand — the fretting hand's stop
    // already is the note's own fret — and enumerating either set here would duplicate exactly what
    // savedChartNote strips, leaving the writer and this rule to agree by hand while a field added
    // to ChartNote updated only one of them. Asked unconditionally because the comparison is
    // identity for every note that overrides nothing.
    //
    // The message names the cause because the two cases are disjoint: a scrape can only have failed
    // on the pitched latents (it is the one attack that keeps a held stop AND sheds techniques),
    // and every other note on exactly one thing — the held stop it may not carry.
    // Emphasis is a scrape's own dynamics and is never stripped.
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
            .message = "only a plain tap or a pick slide carries a held stop at " +
                       positionText(note.position),
        }};
    }
    // The scrape's own gesture: the required unpitched terminal, exactly at the sustain (nothing
    // rings past a scrape) — the release keyframe. That the path keeps traveling is the
    // normalizer's demotion, asked as the fixpoint below.
    if (isScrape(note.attack))
    {
        // Presence is the whole rule: a release IS the keyframe at the ring's end, so one that
        // exists sits exactly at the sustain and there is no second coordinate to disagree with.
        if (slideOutFretOrNull(note) == nullptr)
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

        // What a note's neighbours make of its ring and its keyframes — the same-string bound, and
        // the head no keyframe may sit on — is normalized, never refused: every producer runs
        // normalizeSustainOverlaps and normalizeKeyframeClearances before it validates.
        previous_note = &note;
    }

    return std::expected<void, ChartError>{};
}

} // namespace rock_hero::common::core
