#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// One struck string's contribution to a span's articulation identity: the whole note with its
// position and duration neutralized, so ChartNote equality decides "same chord" and a technique
// field added to the note later can never silently drop out of the comparison. Empty where the
// string takes no part in the posture.
using StringArticulation = std::optional<ChartNote>;

// What one string SOUNDS at a slot on the fretting-hand axis. Two onsets fill it and they fill it
// with the same fact: a fretting-hand onset sounds the stop it presses, and a right-hand onset
// sounds the stop the OTHER hand holds under it (\ref claimedStop), because a tap harmonic's pitch
// derives from the STOPPED length — tapping above a stop sounds that stop and never the point the
// tapping finger is on.
struct SoundedStop
{
    int fret{0};

    // The record whose own CLAIM is what sounds this stop, where one does: a right-hand onset plays
    // the stop the other hand holds under it, and that hold is a statement the settle beside this
    // walk judges. Empty for a fretting-hand onset, which sounds the fret it presses and claims
    // nothing — so answering with it publishes no reach, because there is no claim to publish it
    // for.
    std::optional<std::size_t> claim_note{};
};

// The sounded stops of one slot, indexed by string. Empty where nothing sounds a fretting-hand stop
// at all: a silently-held member, and a tap the hand states nothing under, whose only pitch is its
// own.
//
// ONE array rather than one per hand, because the fret-match law asks one question of it — is the
// claimed stop sounding here — and two arms asked separately would be two laws free to drift.
using SoundedStops = std::vector<std::optional<SoundedStop>>;

// Reduces a presented note to the identity two strums are compared by. Read from the PRESENTED
// note deliberately: two strums that DRAW identically are one box, so a gesture the presentation
// rules compressed is compared in its compressed form.
[[nodiscard]] StringArticulation articulationOf(const ChartNote& presented)
{
    ChartNote key = presented;
    key.position = GridPosition{};
    key.sustain = Fraction{};
    return key;
}

// One CLAIMED stop (\ref claimedStop) resolved against the span it fell inside — a silently-held
// member, or the fretting-hand stop riding a right-hand onset. The note itself names no span, so
// this is the whole of the relationship: which note it came from, which string it claims, the beat
// it claims it at (which the span's own end then judges), and the stop it states. The note index
// rides along so the close can publish which span each claim ended up in, which is what the
// editor's face and its hit box are placed from — and what the inert sweep reads to decide whether
// the record stated anything at all.
struct StopClaim
{
    std::size_t note_index{0};
    std::size_t string_index{0};
    Fraction beat{};
    int fret{0};
};

// The span being held open: the articulation a following onset must repeat to join it, the silent
// claims inside it so far, and the beat marks its end is chosen from — how far its members ring
// (`end_beat`) and where its final restrike sits (`last_strum_beat`), which is the floor the
// closing trim can never cut below. The posture is NOT here: the vector is only complete once the
// span is, which is what lets one span key one posture instead of every strum re-keying it.
struct OpenSpan
{
    std::vector<StringArticulation> articulation;
    std::vector<StopClaim> claims;
    GridPosition position;
    Fraction start_beat{};
    Fraction end_beat{};
    Fraction last_strum_beat{};

    // True when NOTHING sounds in this span — its articulation is empty, so every member is a
    // silently-held stop. Fixed at open: a later onset can join such a span but never fills its
    // articulation, so this stays the question "was this shape stated by the hand alone". Asked of
    // the articulation rather than of the opening slot's strike count, because a span that GREW out
    // of a sounding one inherits that shape's strings without striking any of them here.
    bool silent_only{false};

    // True once the content a silent-only span was authored in front of has ARRIVED, which is one
    // thing and not two (user ruling 2026-08-27): one of this span's claimed stops SOUNDED inside
    // the span — the standing fret-match law, \ref answersClaim. A silent-only span that closes
    // without one is unjustified, because nothing it could be fronting exists, and it dissolves
    // exactly as a lone member states nothing. Meaningless on a span that opened with sound, which
    // is always emitted.
    //
    //     "a tap should not be able to justify a span on its own, the span should require one of
    //      its HELD frets to be played at some point during the span for it to be justified"
    //     — user, 2026-08-27
    //
    // What that overturned is the picking-hand COVERAGE half: taps on the shape's posture strings
    // used to justify it and carry its extent, and a tap sounds where the TAPPING finger lands, so
    // by itself it is evidence about the other hand and says nothing about whether the stated stops
    // are still down. What answers the quoted rule is not the tap but the STOP UNDER it: a tap
    // harmonic's pitch derives from the stopped length, so a right-hand onset whose held fret is a
    // claimed stop plays that stop as surely as a finger fretting it does (user ruling 2026-08-27,
    // the tap-harmonic arm). That is one law with two ways to sound a stop, not two justifications
    // — \ref SoundedStops carries both and \ref answersClaim compares once — and a tap holding
    // nothing, or holding a stop the shape never claimed, still justifies nothing at all.
    //
    // Arrival is not the same question as EXTENT, and the two are deliberately separate fields: a
    // chord can answer a claim without ever joining the span, which justifies the statement (it is
    // emitted at its own instant) while leaving the hand no longer demonstrably down.
    bool justified{false};
};

// Whether a span has any SOUND in it at all — the question \ref OpenSpan::silent_only stores,
// asked at each open so the two opens cannot answer it differently.
[[nodiscard]] bool nothingSounds(const std::vector<StringArticulation>& articulation)
{
    return std::ranges::none_of(
        articulation, [](const StringArticulation& slot) { return slot.has_value(); });
}

// The stop a shape states on one string: the one it SOUNDS there, else the one it CLAIMS there,
// else nothing at all. One reader over both ways a shape can state where a finger is, so a claim
// landing on that string is judged against the shape by one comparison — equal is the same hand
// restating itself, and anything else, an absent stop included, is a stop the shape does not state.
[[nodiscard]] std::optional<int> statedStop(const OpenSpan& span, const std::size_t string_index)
{
    // Bound to a local so the optional check and the access are provably the same object.
    const StringArticulation& slot = span.articulation[string_index];
    if (slot.has_value())
    {
        return slot->fret;
    }
    const auto stated = std::ranges::find(span.claims, string_index, &StopClaim::string_index);
    return stated == span.claims.end() ? std::nullopt : std::optional<int>{stated->fret};
}

// Whether a slot ANSWERS one claim: it sounds that claim's stop, on that claim's string. THE
// fret-match law, spelled once for the three questions that ask it — the lone re-pick that keeps a
// sounding span (side ruling (ii)), the arrival that justifies a span the hand alone stated, and
// the tap harmonic that answers the very claim its own record makes — so they can never drift into
// different tests of the same thing. A different stop is a different hand: it answers nothing.
//
// SOUNDING is the whole of what it reads, and \ref SoundedStops is where the two ways a stop can
// sound are folded into one fact, so there is one comparison here and no arm to drift.
//
// Every claim's string index is in range by construction (the walk bounds it before pushing one),
// and the sounded array is always the model's full width, so the lookup needs no second bound.
[[nodiscard]] bool answersClaim(const StopClaim& claim, const SoundedStops& sounded)
{
    // Bound to a local so the optional check and the access are provably the same object.
    const std::optional<SoundedStop>& stop = sounded[claim.string_index];
    return stop.has_value() && stop->fret == claim.fret;
}

// The record whose own claim SOUNDS a string's stop here, where one does. Total, so a caller that
// has just matched a claim through \ref answersClaim never re-opens the optional it matched
// through — which is also what keeps the guard and the access one expression.
[[nodiscard]] std::optional<std::size_t> soundingClaimNote(
    const SoundedStops& sounded, const std::size_t string_index)
{
    const std::optional<SoundedStop>& stop = sounded[string_index];
    return stop.has_value() ? stop->claim_note : std::nullopt;
}

// Whether a span is still being ASSEMBLED: stated by the hand alone, and still waiting for the
// content it was authored in front of. Nothing dates such a statement yet, so fingers landing
// inside it are part of the one statement being made rather than a shape change — which is why the
// growth split exempts it. Asked of \ref OpenSpan::justified rather than of \ref
// OpenSpan::silent_only, which is fixed at the open and never clears: once the content has arrived
// the statement is dated exactly as a sounding shape is, and a stop it does not state means the
// same thing there as anywhere else.
[[nodiscard]] bool stillAssembling(const OpenSpan& span)
{
    return span.silent_only && !span.justified;
}

// Whether a shape is still standing at `now` — asked by the slot that would replace it and by the
// lone re-pick that would rejoin it, so the two can never answer differently.
//
// For a span with sound in it, standing means still RINGING, not merely still open in the walk: a
// span stays open across the silence after its members stop, because rule 11 lets a later identical
// strum rejoin it, so "open" and "held" are different questions.
//
// A silent-only span with no EXTENT yet is standing however long it waits. It has no ring to
// measure, and it was authored in FRONT of the content it describes — the whole reason the record
// exists — so ending it at its own instant would refuse the case it is for. Once something has
// attached, its own extent answers like any other span's. Asked of the extent rather than of
// `justified` because the two part company exactly once: a chord answering a claim justifies the
// statement without attaching to it, and a span still sitting at its own instant is still waiting
// whatever has been answered.
[[nodiscard]] bool stillHeld(const OpenSpan& span, const Fraction now)
{
    if (span.silent_only && span.end_beat == span.start_beat)
    {
        return true;
    }
    return now < span.end_beat;
}

} // namespace

// The slot walk. Groups are contiguous runs of one grid position, which is the same partition
// presentation and the hold engine use — the stream is sorted by (position, string), so a group is
// an adjacency question and never a search. One stream carries every member, sounding or silent,
// so a slot whose members are all held fingers is simply a step of the walk like any other.
ChartShapes deriveChartShapes(
    const std::vector<ChartNote>& saved_notes, const std::vector<ChartNote>& presented_notes,
    const TempoMap& tempo_map)
{
    ChartShapes derived;
    derived.claim_shapes.assign(saved_notes.size(), std::nullopt);

    // A posture array is indexed by string number, and the model bounds those at
    // g_max_chart_strings — so that constant IS the width, never a quantity read off the input.
    // The derivation runs inside `normalizeChart`, BEFORE `validateChartRules` has refused an
    // out-of-range string, so sizing from the stream (or from the tuning, equally unvalidated
    // there) would let a corrupt document's `"string"` decide an allocation. Every posture from
    // one stream is the same length either way, which is all the dedup below needs to compare
    // held frets alone; a string the tuning does not have simply never fills its slot.
    constexpr auto string_count = static_cast<std::size_t>(g_max_chart_strings);

    // Every onset's exact global beat, once. The whole rule is beat arithmetic — how far members
    // ring, the closing margin, the span's own length — and it stays rational end to end because a
    // span's sustain is a stored-shaped fraction, not a rounded one.
    std::vector<Fraction> onset_beat;
    onset_beat.reserve(saved_notes.size());
    for (const ChartNote& note : saved_notes)
    {
        onset_beat.push_back(beatDistance(tempo_map, GridPosition{}, note.position));
    }
    const auto ring_end_of = [&onset_beat, &saved_notes](const std::size_t index) {
        return onset_beat[index] + saved_notes[index].sustain;
    };

    std::map<std::vector<std::optional<int>>, std::size_t> posture_indices;
    std::optional<OpenSpan> open;

    // The closing onset reduced by the minimum-sustain-distance margin (at the closing onset's
    // measure) — where a span it closes must end.
    const auto margin_limit = [&onset_beat, &saved_notes, &tempo_map](const std::size_t closing) {
        return onset_beat[closing] -
               minimumSustainDistanceBeats(
                   tempo_map.timeSignatureAt(saved_notes[closing].position.measure).denominator);
    };

    // Closes the held span, and keys its posture. A span closed by a following event trims to the
    // margin before it (rule 12a — spans keep the same minimum sustain distance as every other
    // element), floored at the last strum so the box always reaches its final restrike. A span that
    // would lose all length (a single strum crowded closer than the margin) falls back to exact
    // adjacency, mirroring the sustain rules' protected-adjacency precedent.
    //
    // The posture is built HERE rather than at each onset because the span owns extent and a
    // silent claim is judged against it: the vector is only complete once the span is. Keying it
    // once per span rather than once per strum is what that buys back.
    const auto close_span = [&derived, &posture_indices, &open](
                                const std::optional<Fraction> closing_limit,
                                const std::optional<Fraction>
                                    closing_beat) {
        if (!open.has_value())
        {
            return;
        }
        // A shape the hand alone stated, with nothing arriving to justify it, is EVIDENCE of
        // nothing: no sounding note ever played one of the frets it claims, so the passage it was
        // authored in front of does not exist. It dissolves rather than printing a posture over
        // silence, which is the same nothing a lone member states. A run of taps over it dissolves
        // it too, unless one of them HOLDS a stop the shape claims — the shape's own held frets are
        // what have to be heard, and the stop under a tap is heard through it (the tap-harmonic
        // arm). What happens to the NOTES is not decided here: this walk only declines to emit
        // the span, and the settle beside it (\ref sweepInertClaimedStops) is what then removes
        // every hold this answer left reaching nothing.
        if (open->silent_only && !open->justified)
        {
            open.reset();
            return;
        }
        Fraction end = open->end_beat;
        if (closing_limit.has_value() && *closing_limit < end)
        {
            end = std::max(*closing_limit, open->last_strum_beat);
        }
        if (!(open->start_beat < end) && closing_beat.has_value())
        {
            // Exact adjacency: the crowded span ends at the earlier of its own ring and the
            // closing onset — both sit strictly after the span start, so the span keeps positive
            // length even when the closer lands exactly on the ring's end (a dense run of short
            // strums).
            end = std::min(open->end_beat, *closing_beat);
        }
        std::vector<std::optional<int>> frets(open->articulation.size());
        for (std::size_t string_index = 0; string_index < open->articulation.size(); ++string_index)
        {
            // Bound to a local so the optional check and the access are provably the same
            // object (bugprone-unchecked-optional-access cannot track repeated indexing).
            const StringArticulation& slot = open->articulation[string_index];
            if (slot.has_value())
            {
                frets[string_index] = slot->fret;
            }
        }
        // What the notes could not say. A claim authored past the span's own end is inert — that
        // gap is after the shape stopped sounding, and the bracket the fret would print under
        // never reaches it. A string the sound already states is left alone: the hold adds nothing
        // there, so it makes no silent member either.
        //
        // "Past the end" is asked as "not strictly inside, and not AT the start". The second half
        // is not an exception carved out for a degenerate case: a span opened by held fingers
        // alone starts and ends together until sound attaches to it, and its own opening members
        // are exactly the claims sitting at that instant. Excluding them would let a posture open
        // and then state nothing.
        bool silent_member = false;
        for (const StopClaim& claim : open->claims)
        {
            std::optional<int>& fret = frets[claim.string_index];
            const bool inside = claim.beat < end || claim.beat == open->start_beat;
            if (!inside || fret.has_value())
            {
                continue;
            }
            fret = claim.fret;
            silent_member = true;
            // Which span this stop reached, published for the surfaces: the bracket that prints it
            // draws at this span's start, and a hold with no entry here draws nowhere. The FIRST
            // span a claim reaches keeps its face: a stop the hand goes on holding across a growth
            // split is a member of every span it reaches, but it was authored once, at one slot,
            // and its face must stay where the charter put it.
            if (!derived.claim_shapes[claim.note_index].has_value())
            {
                derived.claim_shapes[claim.note_index] = derived.shapes.size();
            }
        }
        const auto [entry, inserted] = posture_indices.try_emplace(frets, derived.postures.size());
        if (inserted)
        {
            derived.postures.push_back(ChartPosture{.frets = std::move(frets)});
        }
        derived.shapes.push_back(
            ChartShape{
                .position = open->position,
                .sustain = end - open->start_beat,
                .posture = entry->second,
                .silent_member = silent_member,
            });
        open.reset();
    };

    // Answers a waiting span with this slot's sounded stops, and publishes what the answering
    // record thereby REACHED. The two are one act: a claim whose answer is the only thing keeping a
    // span alive states exactly as much as a member does — take that record away and the span
    // dissolves — so the settle beside this walk (\ref sweepInertClaimedStops) can go on asking its
    // single question, "does the derived face exist", instead of growing a second rule about
    // justification that would then have to be kept in step with this one (user ruling
    // 2026-08-27).
    //
    // Only a record whose OWN claim sounded the stop has a face to publish: a fretting-hand arrival
    // presses the fret it names and claims nothing. The span is the one being justified, at the
    // index the count names right now — a justified span is always emitted, and nothing can push a
    // shape while it is open, because \ref close_span is the only push and it consumes the span.
    const auto justify = [&derived](OpenSpan& span, const SoundedStops& sounded) {
        for (const StopClaim& claim : span.claims)
        {
            if (!answersClaim(claim, sounded))
            {
                continue;
            }
            span.justified = true;
            if (const std::optional<std::size_t> answering =
                    soundingClaimNote(sounded, claim.string_index);
                answering.has_value())
            {
                // The FIRST span a claim reaches keeps its face, exactly as the close's members do.
                std::optional<std::size_t>& reach = derived.claim_shapes[*answering];
                if (!reach.has_value())
                {
                    reach = derived.shapes.size();
                }
            }
        }
    };

    // A string number the posture array can actually carry. Bounded here because this runs before
    // validation has refused an impossible one, so a corrupt document's `"string"` must never
    // decide an index — and a note naming one is not a member of anything either, which is why
    // every count and every attach below asks the same question.
    const auto note_string_index = [](const ChartNote& note) -> std::optional<std::size_t> {
        if (note.string < 1 || note.string > g_max_chart_strings)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(note.string - 1);
    };

    // The last note sounded per string, for the ring-through rule: a note whose tail crosses a
    // chord's onset on an un-struck string is still sounding, so its held fret joins the derived
    // posture — and the arrival rule then renders the partly-struck span as an arpeggio. Indexes
    // rather than pointers, because the posture it folds in comes from the presented stream while
    // the ring it tests comes from the stored one.
    std::vector<std::optional<std::size_t>> ringing(string_count);

    // SIDE RULING (ii): a lone re-pick of a string the open span already holds does not leave the
    // shape. Any single-string onset used to close the span, which killed a held chord at the exact
    // moment a broken figure re-picked one of its own members — and every fact needed to know
    // better was already in the stream. Nothing here is authored: it is the derivation reading what
    // it had. Three conditions, and the third is what keeps it honest.
    const auto lone_repick_continues = [&ringing, &onset_beat, &presented_notes](
                                           const OpenSpan& span,
                                           const std::vector<StringArticulation>& articulation,
                                           const SoundedStops& sounded,
                                           const Fraction now) {
        // A `struck == 1` onset fills exactly one slot, which this finds without the walk having to
        // carry it out of the group loop.
        std::size_t struck_index = articulation.size();
        for (std::size_t string_index = 0; string_index < articulation.size(); ++string_index)
        {
            if (articulation[string_index].has_value())
            {
                struck_index = string_index;
            }
        }
        if (struck_index == articulation.size())
        {
            return false;
        }
        // 1. The string is a member of the open shape. A member the SOUND states must be re-picked
        //    identically — rule 11 splits a chord on any articulation change, and a lone re-pick is
        //    that same question asked of one string. A member the chart HOLDS silently has no
        //    articulation to match, because nothing ever sounded it, so the authored claim is the
        //    whole test — and this is where a silent hold and this ruling compose into the case
        //    the whole record exists for.
        //
        //    The claim IS the whole test, which means all of it: a claim states where the finger
        //    is, so a re-pick at a DIFFERENT stop is a different hand and splits, exactly as the
        //    sound branch splits on a changed articulation. Passing it would let an authored stop
        //    outlive the note that contradicts it — the bracket printing the claim's fret at the
        //    span start while the note inside it sounds another.
        const StringArticulation& held = span.articulation[struck_index];
        const StringArticulation& repick = articulation[struck_index];
        bool member = false;
        if (held.has_value() && repick.has_value())
        {
            member = *held == *repick;
        }
        else if (!held.has_value())
        {
            // The claim law, asked of the RE-PICKED string alone. Scoped to it rather than run over
            // the whole slot, because the sounded stops now carry the other hand too: a
            // held-carrying tap elsewhere in this slot answers claims of its own (the tap-harmonic
            // arm), and this question is only ever about whether the string being re-picked is a
            // member. Same law, one string.
            member =
                std::ranges::any_of(span.claims, [&sounded, struck_index](const StopClaim& claim) {
                    return claim.string_index == struck_index && answersClaim(claim, sounded);
                });
        }
        if (!member)
        {
            return false;
        }
        // 2. The shape is still standing. A span the hand alone stated has no ring to witness, so
        //    the question is \ref stillHeld's — it waits, unended, for the content it was authored
        //    in front of, and once sound has attached its own extent answers. This is the matching
        //    note ARRIVING, which is exactly the justification such a span was waiting for.
        if (span.silent_only)
        {
            return stillHeld(span, now);
        }
        // 3. For a span with sound in it, some OTHER member must still be sounding, so the hand
        //    demonstrably has not left the shape; without this a lone re-pick would resurrect a
        //    span across any amount of silence. The witness is the PRESENTED ring, for the arrival
        //    rule's own reason: a tail no surface draws is not a string the shape is heard to be
        //    holding.
        bool another_rings = false;
        for (std::size_t string_index = 0; string_index < span.articulation.size(); ++string_index)
        {
            const std::optional<std::size_t>& ring = ringing[string_index];
            if (string_index == struck_index || !span.articulation[string_index].has_value() ||
                !ring.has_value())
            {
                continue;
            }
            another_rings =
                another_rings || now < onset_beat[*ring] + presented_notes[*ring].sustain;
        }
        return another_rings;
    };

    std::size_t index = 0;
    while (index < saved_notes.size())
    {
        const GridPosition position = saved_notes[index].position;
        const Fraction position_beat = onset_beat[index];

        std::size_t onset_end = index;
        // The span reaches at least its own start. For every sounding onset that is what the first
        // ring already says; for a slot whose members are all silent it is the whole of what the
        // slot says, and starting the accumulator here is what keeps that from reading as beat
        // zero — an absolute beat, which would make the span's length negative.
        Fraction ring_end = position_beat;
        std::vector<StringArticulation> articulation(string_count);
        // Rule 10 counts MEMBERS (user ruling 2026-08-27): a member is a sounding fretting-hand
        // onset here or a stop the hand CLAIMS here, so one sound plus one held finger states a
        // shape, two held fingers state one, and a lone member of either kind states none. A
        // silent hold was never a strike and still is not — what changed is that a shape is not
        // made of strikes, it is made of stops.
        //
        // A claimed stop is a claimed stop however it is written down: a tap carrying a held fret
        // is one record stating two facts at one slot, and it counts here exactly as the silent
        // hold it would otherwise have to be written as (user ruling 2026-08-27). That is also
        // what makes the same-instant justification below reachable at all — the tap and the claim
        // it articulates cannot collide, because they ARE one note.
        //
        // The slot's claimed stops are collected here as the claims they will become, rather than
        // counted here and re-found later: the branch below asks what they claim, and whatever span
        // this slot leaves open then takes them.
        std::size_t struck = 0;
        std::vector<StopClaim> slot_claims;
        SoundedStops sounded(string_count);
        while (onset_end < saved_notes.size() && saved_notes[onset_end].position == position)
        {
            const ChartNote& member = saved_notes[onset_end];
            const std::optional<std::size_t> string_index = note_string_index(member);
            // The fretting hand's stop, in whichever shape the record states it: a silently-held
            // member IS the statement, and a stop the hand holds under a right-hand onset rides
            // that note as its held fret. One query for both (\ref claimedStop), so the membership
            // count, the justification and the fret-match can never read them differently.
            const std::optional<int> claim = claimedStop(member);
            if (claim.has_value() && string_index.has_value())
            {
                slot_claims.push_back(
                    StopClaim{
                        .note_index = onset_end,
                        .string_index = *string_index,
                        .beat = position_beat,
                        .fret = *claim,
                    });
            }
            if (!silentHold(member.attack) && !rightHandOnset(member.attack))
            {
                // Right-hand onsets are invisible to span derivation: they join no posture and
                // extend no ring, so a mixed onset is judged by its fretting-hand members alone.
                if (string_index.has_value())
                {
                    articulation[*string_index] = articulationOf(presented_notes[onset_end]);
                    // What this string sounds: the fretting hand's own stop, read from the
                    // PRESENTED note for the same reason the articulation is — what a strum sounds
                    // is what it draws. It carries no claim: this note's fret IS its stop.
                    sounded[*string_index] = SoundedStop{
                        .fret = presented_notes[onset_end].fret, .claim_note = std::nullopt
                    };
                    ++struck;
                }
                ring_end = std::max(ring_end, ring_end_of(onset_end));
            }
            else if (rightHandOnset(member.attack) && string_index.has_value())
            {
                // Invisible to the posture, audible at the STOP: what a right-hand onset sounds on
                // the fret axis is the stop the other hand holds under it, which is why the claim
                // it makes is also the answer to one (the tap-harmonic arm). A tap holding nothing
                // sounds no fretting-hand stop at all, and the empty slot says exactly that. The
                // record rides along, because answering with a stop the record CLAIMS is that
                // claim doing something, which is what the settle beside this walk judges.
                if (claim.has_value())
                {
                    sounded[*string_index] = SoundedStop{.fret = *claim, .claim_note = onset_end};
                }
            }
            ++onset_end;
        }
        const bool posture_slot = struck + slot_claims.size() >= 2;

        // THE justification, and now the only one (user ruling 2026-08-27): this slot sounding one
        // of the open shape's claimed stops is the content that shape was authored in front of,
        // whatever ELSE the slot holds. One of the span's own held frets has been played inside the
        // span, which is the whole of what the ruling asks.
        //
        // Asked here, before the branch, for a reason the branch itself cannot serve: the answer
        // must not depend on whether the slot goes on to continue the span or to replace it — a
        // chord carrying the claimed stop justifies the statement it fronts and then opens its own
        // shape, where routing the test through the lone re-pick alone would dissolve the statement
        // unseen, which is the vanishing the member rule was re-ruled to stop.
        //
        // What it cannot see is a claim this same slot MAKES, since the claims attach below; that
        // is the tap harmonic's own case and it is asked there.
        //
        // "Inside the span" needs no test of its own: an unjustified silent-only span has no
        // extent, so it is still standing at every later slot (\ref stillHeld) and there is no
        // instant this could fire at that is outside it. Once an arrival attaches, the span's own
        // extent governs like any other's.
        if (open.has_value() && open->silent_only)
        {
            justify(*open, sounded);
        }

        // Opening a span is one statement however the slot got here, sounding or silent: the
        // articulation it holds, starting and reaching at least this instant, with no claims yet
        // (this slot's holds attach below). Written once so the two branches that open a span
        // cannot drift into two different spans.
        const auto open_span_here = [&open, &articulation, &position, position_beat, ring_end] {
            // Asked before the aggregate, which leaves `articulation` moved-from.
            const bool silent = nothingSounds(articulation);
            open = OpenSpan{
                .articulation = std::move(articulation),
                .claims = {},
                .position = position,
                .start_beat = position_beat,
                .end_beat = ring_end,
                .last_strum_beat = position_beat,
                .silent_only = silent,
                .justified = false,
            };
        };

        if (struck == 0)
        {
            // Nothing sounds here, and a slot that sounds nothing never ENDS a held posture. Two
            // cases share that: tap-only onsets are transparent to the GROUPING (a chord ringing
            // under taps on other strings keeps its span, which the arrival rule then renders as a
            // held arpeggio — the corpus's held-shape-under-tapping case), and a slot of held
            // fingers is a hand coming down, which adds to a shape rather than replacing one.
            // Transparent to the grouping is not transparent to the ring: a tap is a real onset on
            // its own string, so the same-string clamp has already ended any ring there.
            //
            // Where no shape is STANDING, though, held fingers alone can state one — that is the
            // whole of what a silent-only span is. Its articulation is empty because nothing
            // sounds in it, and its claims (attached below) are its entire posture.
            //
            // Bound once as a pointer so every read below is provably behind the has_value check,
            // the shape this file uses wherever an optional's guarantee has to survive intervening
            // calls. It dangles the moment the close consumes the span, which is why the growth
            // below builds its successor first.
            const OpenSpan* const standing =
                open.has_value() && stillHeld(*open, position_beat) ? &*open : nullptr;

            // GROWTH (user ruling 2026-08-27, which overturns rule 12b's join clause). A stop the
            // hand takes that the standing shape does not already state is the hand in a DIFFERENT
            // shape from here on, and that is the same answer the derivation already gives a strum
            // that grows by a string: growth splits. What the authored hold changes is WHERE the
            // charter puts the statement — one written at the shape's own onset states the shape
            // whole from its start, which is the case the whole record exists for, while one
            // written later says the finger came down later, because that is what it says.
            //
            // ONE comparison covers both ways a shape can fail to state what a claim says, because
            // they are one question asked of \ref statedStop: a string the shape states nothing on
            // is the hand growing into a new shape, and a string it states ANOTHER stop on is the
            // finger moved, which is a different shape however the old stop was stated — by sound
            // or by claim (user ruling 2026-08-27: same fret continues the span, a different one
            // splits it). Only a claim restating the shape's own stop leaves the shape alone, which
            // is the redundancy the settle then takes.
            //
            // A span still ASSEMBLING is deliberately exempt: nothing dates it yet, so later
            // fingers join the one statement being made rather than splitting a statement that is
            // still waiting for the content it fronts.
            const bool takes_new_stop =
                standing != nullptr && !stillAssembling(*standing) &&
                std::ranges::any_of(slot_claims, [standing](const StopClaim& claim) {
                    return statedStop(*standing, claim.string_index) != claim.fret;
                });
            if (takes_new_stop)
            {
                // The shape does not change identity where this slot says nothing about it — those
                // strings are still stated and still ringing — so the new span INHERITS them,
                // articulation and stated stops alike, and the split divides the old span's extent
                // at the instant the hand moved.
                //
                // A string this slot states a DIFFERENT stop on is one the hand has just LEFT, so
                // what the shape said there is superseded: the successor drops it, and this slot's
                // own claims (attached below) state those strings instead. Without that the grown
                // shape would print the stop the hand moved off — the older statement wins the
                // posture slot either way — and the claim that split the span would state nothing
                // anywhere, its own evidence swept away by the settle. A claim RESTATING the
                // shape's stop supersedes nothing, for the same reason it splits nothing: it
                // changes nothing.
                //
                // Built before the close, which consumes the span it reads from.
                const auto superseded = [&slot_claims, standing](const std::size_t string_index) {
                    return std::ranges::any_of(
                        slot_claims, [standing, string_index](const StopClaim& claim) {
                            return claim.string_index == string_index &&
                                   statedStop(*standing, string_index) != claim.fret;
                        });
                };
                std::vector<StringArticulation> inherited = standing->articulation;
                for (std::size_t string_index = 0; string_index < inherited.size(); ++string_index)
                {
                    if (superseded(string_index))
                    {
                        inherited[string_index].reset();
                    }
                }
                std::vector<StopClaim> carried = standing->claims;
                std::erase_if(carried, [&superseded](const StopClaim& claim) {
                    return superseded(claim.string_index);
                });
                // Asked before the aggregate, which leaves `inherited` moved-from.
                const bool silent = nothingSounds(inherited);
                OpenSpan grown{
                    .articulation = std::move(inherited),
                    .claims = std::move(carried),
                    .position = position,
                    .start_beat = position_beat,
                    // A span reaches at least its own start; beyond that it runs where the shape it
                    // inherited was already running, so the two spans cover that ring with no gap
                    // and no overlap.
                    .end_beat = std::max(standing->end_beat, position_beat),
                    .last_strum_beat = position_beat,
                    .silent_only = silent,
                    .justified = false,
                };
                // No margin trim: nothing SOUNDS here to keep a distance from, so the shape being
                // replaced ends exactly where the new one starts.
                close_span(position_beat, position_beat);
                open = std::move(grown);
            }
            else if (standing == nullptr && posture_slot)
            {
                // No margin trim: nothing SOUNDS here to keep a distance from, and the span being
                // replaced has already stopped standing at or before this slot.
                close_span(std::nullopt, std::nullopt);
                open_span_here();
            }
        }
        else if (
            struck == 1 && open.has_value() &&
            lone_repick_continues(*open, articulation, sounded, position_beat)
        )
        {
            // Side ruling (ii): the shape survives one of its own members being re-picked, and the
            // span grows to cover the re-pick — the last-strum floor included, so the closing trim
            // can never cut back past it. Asked BEFORE the posture test below, because a re-pick
            // that happens to carry a held finger beside it is still the shape continuing; letting
            // the member count open a fresh span there would break exactly the broken-chord figure
            // this ruling exists for.
            //
            // For a shape the hand alone stated, the arrival that justified it was recorded above,
            // by the one claim law; what this adds is the EXTENT — from here the normal ring rule
            // takes over, exactly as it would had the span always been sounding.
            open->end_beat = std::max(open->end_beat, ring_end);
            open->last_strum_beat = position_beat;
        }
        else if (posture_slot)
        {
            // Ring-through strings join the posture (they never count as struck): the held note's
            // articulation folds in so span merging still compares whole notes.
            for (std::size_t string_index = 0; string_index < string_count; ++string_index)
            {
                const std::optional<std::size_t>& ring = ringing[string_index];
                if (!articulation[string_index].has_value() && ring.has_value() &&
                    position_beat < ring_end_of(*ring))
                {
                    articulation[string_index] = articulationOf(presented_notes[*ring]);
                }
            }
            if (open.has_value() && open->articulation == articulation)
            {
                open->end_beat = std::max(open->end_beat, ring_end);
                open->last_strum_beat = position_beat;
            }
            else
            {
                close_span(margin_limit(index), position_beat);
                open_span_here();
            }
        }
        else
        {
            // Any other intervening non-chord onset ends the held posture.
            close_span(margin_limit(index), position_beat);
        }

        // This slot's held fingers join whatever span is open here. A hold outside every span is
        // refused nowhere — it simply attaches to nothing and states nothing, the same degrade an
        // unjustified connection claim takes. Asked after the branch so it reads the span this
        // slot actually left open, which for a growth split is the span the stop itself opened.
        if (open.has_value())
        {
            open->claims.insert(open->claims.end(), slot_claims.begin(), slot_claims.end());
            // The tap harmonic's own case: a claim and the sound that answers it in ONE record, so
            // the ask before the branch ran while the claim did not yet exist. One note states the
            // stop and sounds it, which is the whole figure and needs nothing else to arrive.
            //
            // At the span's OWN instant and nowhere else, because that is where the claims a shape
            // STATES sit while it waits: a stop the record adds a beat later is not one the shape
            // was authored making, and a statement the shape does not make cannot be evidence for
            // it — otherwise every held-carrying tap would justify whatever span happened to be
            // open by claiming into it. A tap answering a claim the span already HELD is the ask
            // above's business, at any instant it lands on.
            if (open->silent_only && open->start_beat == position_beat)
            {
                justify(*open, sounded);
            }
        }

        // This onset's sounding fretting-hand notes become the ring candidates for later onsets
        // (updated after use: a note starting at an onset is struck there, not ringing through
        // it). Taps stay invisible here too — a ringing tap never folds into a later posture — and
        // so does a silent hold, which has no ring to fold in at all.
        for (std::size_t member = index; member < onset_end; ++member)
        {
            const ChartNote& note = saved_notes[member];
            if (const std::optional<std::size_t> string_index = note_string_index(note);
                string_index.has_value() && !rightHandOnset(note.attack) &&
                !silentHold(note.attack))
            {
                ringing[*string_index] = member;
            }
        }
        index = onset_end;
    }
    close_span(std::nullopt, std::nullopt);

    return derived;
}

std::vector<bool> chartShapeArrivals(
    const std::vector<ChartNote>& presented_notes, const std::vector<ChartShape>& shapes,
    const std::vector<ChartPosture>& postures, const TempoMap& tempo_map)
{
    std::vector<bool> arpeggio;
    arpeggio.reserve(shapes.size());
    // Both streams ascend, so one note cursor serves every shape. It carries the one thing the rule
    // needs from the past — the most recent note on each string — which is what turns the whole
    // classification into a single forward pass. Answering it per shape instead meant walking BACK
    // through the note stream from each span, all the way to the first note whenever a posture
    // string had none, and both projections do this for every shape on every chart revision.
    constexpr std::size_t no_note = std::numeric_limits<std::size_t>::max();
    std::array<std::size_t, static_cast<std::size_t>(g_max_chart_strings) + 1> last_per_string{};
    last_per_string.fill(no_note);
    std::size_t next_note = 0;
    for (const ChartShape& shape : shapes)
    {
        while (next_note < presented_notes.size() &&
               presented_notes[next_note].position < shape.position)
        {
            const int string = presented_notes[next_note].string;
            // A silent hold is not a note this rule can read: it makes no sound to cross a span
            // start, and remembering one would shadow the earlier ring that does.
            if (string >= 1 && string <= g_max_chart_strings &&
                !silentHold(presented_notes[next_note].attack))
            {
                last_per_string.at(static_cast<std::size_t>(string)) = next_note;
            }
            ++next_note;
        }
        // The cursor now sits on the first note AT the span start, and the notes sharing that onset
        // are the contiguous run from there.
        std::size_t after_start = next_note;
        std::size_t sounding_at_start = 0;
        while (after_start < presented_notes.size() &&
               presented_notes[after_start].position == shape.position)
        {
            sounding_at_start += silentHold(presented_notes[after_start].attack) ? 0 : 1;
            ++after_start;
        }
        if (sounding_at_start < 2)
        {
            // Fewer than two SOUNDS at the span start is a sequential arrival, whatever else is
            // ringing — and a span whose start is held fingers alone is the extreme of that.
            arpeggio.push_back(true);
            continue;
        }
        if (shape.silent_member)
        {
            // The hand holds a member it never sounds here, which only the bracket can state: a
            // chord box prints the notes' own heads and has nowhere to put a fret nothing struck.
            // Carried by the derivation rather than re-derived, because nothing in the note stream
            // can tell a silently-held string from an absent one — that is the whole reason a
            // NoteAttack::None note is authored at all.
            arpeggio.push_back(true);
            continue;
        }

        // A held chord played under a right-hand onset reads as a held arpeggio, not a strummed
        // box: the fretting hand holds the shape while the other hand sounds above it — taps and
        // pick slides alike. Any such note sounding within the span flips the box.
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
        bool held_under_right_hand = false;
        for (std::size_t scan = next_note;
             scan < presented_notes.size() && presented_notes[scan].position < span_end;
             ++scan)
        {
            held_under_right_hand =
                held_under_right_hand || rightHandOnset(presented_notes[scan].attack);
        }
        if (held_under_right_hand || shape.posture >= postures.size())
        {
            arpeggio.push_back(held_under_right_hand);
            continue;
        }

        // A posture string still ringing at the start without an onset there was not re-struck —
        // the strum picks around the held note, so the span cannot be one full strum. Only that
        // string's most recent earlier note can still be ringing, which the cursor already knows.
        // "Ringing" is the PRESENTED tail: a dead string makes no sound to pick around, and
        // presentation is where a dead note's tail goes (E25).
        const ChartPosture& posture = postures[shape.posture];
        bool rings_unstruck = false;
        for (std::size_t index = 0; index < posture.frets.size(); ++index)
        {
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = posture.frets[index];
            const int string = static_cast<int>(index) + 1;
            if (!fret.has_value() || string > g_max_chart_strings)
            {
                continue;
            }
            bool struck = false;
            for (std::size_t scan = next_note; scan < after_start; ++scan)
            {
                struck = struck || (presented_notes[scan].string == string &&
                                    !silentHold(presented_notes[scan].attack));
            }
            const std::size_t earlier = last_per_string.at(static_cast<std::size_t>(string));
            rings_unstruck =
                rings_unstruck ||
                (!struck && earlier != no_note &&
                 shape.position < sustainEndPosition(tempo_map, presented_notes[earlier]));
        }
        arpeggio.push_back(rings_unstruck);
    }
    return arpeggio;
}

} // namespace rock_hero::common::core
