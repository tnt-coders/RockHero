#include "chart/chart_edits.h"

#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <iterator>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/session/session.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <utility>

namespace rock_hero::editor::core
{

namespace
{

// True when a note's harmonic node cannot follow it into `target`, so the verb changing the attack
// must send the node away with it.
//
// Two facts decide it, and nothing else. A TAP's node is a struck contact point, and a strike is a
// strike whichever hand delivers it, so re-typing a tap carries the node into any attack that can
// host it: Shift+T re-handing a tap harmonic lands on the left-hand-tap harmonic E13 names, and the
// validation gate below still refuses a strike point past the neck ceiling. (The reverse re-hand,
// left-hand tap back to Tap, still drops the node under the ownership test; no live verb sets Tap
// today, and truing that direction is the note-view unification's business, not this verb's.)
// Otherwise the node survives exactly while the same HAND still owns it: the stored number means a
// place on the neck under a fretting touch and a place the picking hand damps otherwise, so a
// change that flips the owner silently re-reads it as a different technique. On a stopped note
// nothing flips — an artificial harmonic's fretting hand is on the stop and the picking hand on
// the node under every attack — while an open-string pinch's bridge-side graze, which is not a
// strikeable place at all, strands its node and the E4 gate then refuses the form.
//
// The connection verb does not ask: a legato claim stores no direction, so it can never demand a
// node leave. A stored-direction model instead needs a rule the two verbs must agree on by hand.
[[nodiscard]] bool nodeLeavesWithAttack(
    const common::core::ChartNote& note, const common::core::NoteAttack target)
{
    if (!note.harmonic_node.has_value())
    {
        return false;
    }
    if (note.attack == common::core::NoteAttack::Tap)
    {
        return false;
    }
    common::core::ChartNote retyped = note;
    retyped.attack = target;
    return common::core::frettingFingerOnNode(note) != common::core::frettingFingerOnNode(retyped);
}

// Diffs the note stream's current values against the planned ones into removed/inserted full
// values; both inputs are sorted by the chart's slot order. The label is the caller's.
[[nodiscard]] ChartEditPlan diffNotes(
    const std::vector<common::core::ChartNote>& before,
    const std::vector<common::core::ChartNote>& after, const std::string_view label)
{
    ChartEditPlan plan{.removed = {}, .inserted = {}, .label = std::string{label}};
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    while (before_index < before.size() || after_index < after.size())
    {
        if (before_index == before.size())
        {
            plan.inserted.push_back(after[after_index++]);
            continue;
        }
        if (after_index == after.size())
        {
            plan.removed.push_back(before[before_index++]);
            continue;
        }
        const common::core::ChartNote& old_note = before[before_index];
        const common::core::ChartNote& new_note = after[after_index];
        if (chartSlotKeyOf(old_note) < chartSlotKeyOf(new_note))
        {
            plan.removed.push_back(before[before_index++]);
            continue;
        }
        if (chartSlotKeyOf(new_note) < chartSlotKeyOf(old_note))
        {
            plan.inserted.push_back(after[after_index++]);
            continue;
        }
        if (!(old_note == new_note))
        {
            plan.removed.push_back(old_note);
            plan.inserted.push_back(new_note);
        }
        ++before_index;
        ++after_index;
    }
    return plan;
}

// The note stream cut in two by a key set: the notes the keys name, and everything else. Both
// keep their slot order. The two range verbs share it — deleting is "keep the rest", moving is
// "transform the keyed half and put it back".
struct KeyedSplit
{
    std::vector<common::core::ChartNote> keyed;
    std::vector<common::core::ChartNote> rest;
};

// keys must be sorted ascending (the ChartSelection order); the lookup binary-searches it.
[[nodiscard]] KeyedSplit splitByKeys(
    const std::vector<common::core::ChartNote>& notes, const std::vector<ChartSlotKey>& keys)
{
    KeyedSplit split;
    split.rest.reserve(notes.size());
    for (const common::core::ChartNote& note : notes)
    {
        if (std::ranges::binary_search(keys, chartSlotKeyOf(note)))
        {
            split.keyed.push_back(note);
            continue;
        }
        split.rest.push_back(note);
    }
    return split;
}

// An offset along a ring — the end's, or a keyframe's — is a beat count from the onset, and a beat
// is a different note value on either side of a meter change. Stepping the INSTANT it names by a
// whole-note delta and measuring it again from `onset_after` keeps its musical distance from the
// onset and keeps it on the tick lattice; adding a beat delta to the count would do neither once
// the step crosses a denominator change.
[[nodiscard]] common::core::Fraction carriedOffset(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition onset_before,
    const common::core::Fraction offset, const common::core::Fraction whole_note_delta,
    const common::core::GridPosition onset_after)
{
    return common::core::beatDistance(
        tempo_map,
        onset_after,
        common::core::advanceGridPositionByWholeNotes(
            tempo_map,
            common::core::advanceGridPosition(tempo_map, onset_before, offset),
            whole_note_delta));
}

// A ring a verb AUTHORS is a note value carried from the onset along the whole-note axis to a
// lattice end, then measured back as the beat count the note stores. Stated as beats at the onset's
// meter it would re-read as another length past a denominator change and could end between two
// ticks there.
[[nodiscard]] common::core::Fraction authoredRing(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition onset,
    const common::core::Fraction whole_notes)
{
    return common::core::beatDistance(
        tempo_map,
        onset,
        common::core::advanceGridPositionByWholeNotes(tempo_map, onset, whole_notes));
}

// The minimum slide window is stated in beats (the import reads the same constant), so its worth
// in whole notes is taken at the onset's own meter and carried from there.
[[nodiscard]] common::core::Fraction minimumSlideWindowRing(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition onset)
{
    const int denominator = std::max(1, tempo_map.timeSignatureAt(onset.measure).denominator);
    return authoredRing(
        tempo_map, onset, g_minimum_slide_window * common::core::Fraction{1, denominator});
}

// Slides every note by the delta in place, or answers false and leaves them half-moved for the
// caller to discard. Refused, never clamped: a move that would leave the neck or the grid is
// invalid. The grid arithmetic itself clamps at the origin, so leaving the grid shows up as a move
// that fell short of the delta asked for. The ring's end and every keyframe travel as instants of
// their own, by the same whole-note delta as the onset, and are measured again from where the
// onset landed (carriedOffset): a note carried across a meter change keeps its real length.
[[nodiscard]] bool moveKeyedNotes(
    const common::core::TempoMap& tempo_map, std::vector<common::core::ChartNote>& notes,
    const common::core::Fraction whole_note_delta, const int string_delta, const int string_count)
{
    for (common::core::ChartNote& note : notes)
    {
        const common::core::GridPosition from = note.position;
        note.position =
            common::core::advanceGridPositionByWholeNotes(tempo_map, from, whole_note_delta);
        note.string += string_delta;
        if (note.string < 1 || note.string > string_count ||
            common::core::wholeNoteDistance(tempo_map, from, note.position) != whole_note_delta)
        {
            return false;
        }
        note.sustain =
            carriedOffset(tempo_map, from, note.sustain, whole_note_delta, note.position);
        for (common::core::Keyframe& keyframe : note.keyframes)
        {
            keyframe.offset =
                carriedOffset(tempo_map, from, keyframe.offset, whole_note_delta, note.position);
        }
    }
    return true;
}

// The one repair a plan carries with it rather than refusing over: an attack that STRIKES from
// nowhere needs somewhere to land (E4). It rides the entry that produced it because the truth it
// repairs is the note's OWN — retyping a tap down to the open string leaves nothing to strike — so
// refusing instead would make the edit fail for a reason the user never asked about. Every OTHER
// rule the normalizer owns stays a refusal, because its repair would discard authored data the
// user did not touch. (A dead note's tail was the second such repair until E25 became a
// presentation rule: X now leaves the ring alone, because nothing draws it.)
//
// Whether a verb's per-note ELIGIBILITY test applies the flatten before asking the rule authority.
// Only the verbs whose intent is not the attack do: for the attack verb a strike with nowhere to
// land is a note to skip, not one to quietly retype as a pick.
enum class StrandedStrikeRepair : std::uint8_t
{
    Flatten,
    Skip
};

// Finalizes a candidate chart: restores each authored array's slot order, applies the 40-Q2-B
// overlap normalization — refusing where it lost an authored statement — and the one in-plan
// repair, gates the result through the whole technique matrix, and diffs against `base`. The gate
// is what makes authoring an invalid chart impossible by construction — a plan whose candidate the
// document reader would reject refuses here, for every present and future verb, with no per-verb
// guard to forget. It validates the SAVED form, because a scrape's latent overrides are legal in
// memory and stripped by the writer.
// The two emptinesses are distinct on purpose: the gate's refusal is Invalid, an empty diff is
// NoChange — conflating them is what made every refusal in the editor silent.
//
// `base` is the stream the plan is expressed against, which is `chart.notes` for every verb that
// edits from what it finds. The duration gesture is the reason the base is a parameter rather than
// read off `chart`: its plan must describe the whole gesture, so it is diffed against the stream
// the gesture started from while the ring RULES still judge the live chart. The move gesture,
// equally a gesture, needs no such split — every bound it reads is a fact about the PRE-GESTURE
// chart that its own steps cannot change, so its caller simply hands it that chart and `base` is
// that chart's own notes. Slide-out-ness is the one that had to be earned: a point stepped onto the
// ring's end would have become the slide-out mid-run, leaving the replay reading a kind the chart
// no longer had, so the verb refuses that step instead (planMoveSelection).
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> finalizePlan(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base,
    std::vector<common::core::ChartNote> candidate, std::string_view label)
{
    std::ranges::sort(candidate, common::core::chartNoteOrderLess);
    // The one rule a note cannot obey alone, normalized exactly as a loaded chart is: a re-strike
    // stops the ring, so a note landing inside a ring truncates it and the statement at that
    // ring's end rides back with the end — onto the new head itself, which is where the store says
    // the hands left it; the spacing the mark needs to be seen is presentation's. A truncation may
    // SHORTEN a ring; it may not DELETE a statement the charter authored on a note the edit may
    // never have touched, so a plan whose truncation reports a loss is refused whole.
    const std::vector<common::core::TailTruncation> truncated =
        common::core::normalizeSustainOverlaps(candidate, tempo_map);
    if (std::ranges::any_of(truncated, &common::core::TailTruncation::statement_lost))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    // The in-plan repair (E4). Relational truths deliberately do not repair here (see
    // planSettleChart): mid-burst a claim the chart cannot justify simply plays as the pick it
    // sounds like, and the burst stays one undo step. Sweeping the whole candidate needs no record
    // of which notes the plan touched, because a note the plan left alone already passed this gate.
    for (common::core::ChartNote& note : candidate)
    {
        static_cast<void>(common::core::flattenStrandedStrike(note));
    }
    // The relational settle a plan DOES carry, and the one the cascade rests on: a claimed stop
    // that reaches no shape states nothing anywhere, so an edit that leaves one takes it in the
    // same undo entry rather than saving a statement nothing draws — the whole note where the note
    // IS the claim, the field alone where a sounding onset carries it. It rides the plan for the
    // same reason the stranded strike does — the truth it repairs is the edit's own product — and
    // it is the whole of what makes "every claimed stop in the chart states something" an invariant
    // instead of a hope. Deliberately unlike the legato settle beside it, which stays out of a
    // burst because a claim the burst broke is still visible and still the user's; a stop the edit
    // stranded is neither. The derivation's residue, taken in the same entry and for the same
    // reason the settle above is: authoring a pull-off is what makes its predecessor's stored held
    // stop a second spelling of a fact the notation now states, so the edit that created the
    // duplication is the edit that clears it (DERIVED HELD). No verb states this rule — the plan
    // gate does, once, for every present and future one.
    static_cast<void>(common::core::sweepDerivedHeldStops(candidate, tempo_map));
    static_cast<void>(common::core::sweepInertClaimedStops(candidate, tempo_map));
    // The gate judges the SAVED form: a scrape's latent overrides are legal in memory and stripped
    // by the writer, so validating the in-memory values would refuse charts the document accepts.
    std::vector<common::core::ChartNote> saved_form;
    saved_form.reserve(candidate.size());
    for (const common::core::ChartNote& note : candidate)
    {
        saved_form.push_back(common::core::savedChartNote(note));
    }
    if (!common::core::validateChartNotes(saved_form, chart.tuning, tempo_map).has_value())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    ChartEditPlan plan = diffNotes(base, candidate, label);
    if (plan.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    return plan;
}

// The one per-note write plan every property verb runs — flags, emphasis, and attack — so the law
// is written once: each selected note is built as the verb would WRITE it (`write` fills `written`
// from `note`, or returns false to leave the note alone), a write that changes nothing the document
// would record is skipped (asked of the writer's own authority, so a scrape, whose saved form
// strips its latents, never earns an undo entry for a flag no surface draws), the plan's own
// repair rides the eligibility test, and the per-note rule authority then judges the SAVED form so
// a mixed selection applies to what CAN take the write and leaves the rest alone. One skeleton
// rather than a copy inside each of the three planners, which are then free to disagree about the
// no-op test.
template <typename Write>
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> planNoteWrite(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const std::string_view label,
    const StrandedStrikeRepair stranded, const Write& write)
{
    if (keys.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    std::vector<common::core::ChartNote> candidate = chart.notes;
    bool changed = false;
    for (common::core::ChartNote& note : candidate)
    {
        if (!std::ranges::binary_search(keys, chartSlotKeyOf(note)))
        {
            continue;
        }
        common::core::ChartNote written = note;
        if (!write(std::as_const(note), written))
        {
            continue;
        }
        if (common::core::savedChartNote(written) == common::core::savedChartNote(note))
        {
            continue;
        }
        if (stranded == StrandedStrikeRepair::Flatten)
        {
            static_cast<void>(common::core::flattenStrandedStrike(written));
        }
        if (!common::core::validateChartNoteAlone(
                 common::core::savedChartNote(written), chart.tuning, tempo_map)
                 .has_value())
        {
            continue;
        }
        note = std::move(written);
        changed = true;
    }
    if (!changed)
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), label);
}

// The offsets one note carries selected keyframes at, in ascending order. Keyframe keys are sorted
// by (note slot, offset), so the run belonging to one note is contiguous and this is one
// equal_range rather than a scan — the same shape the onset-group collection uses on the two slot
// arrays, one level down.
[[nodiscard]] std::vector<common::core::Fraction> selectedOffsetsOn(
    const std::vector<ChartKeyframeKey>& keyframe_keys, const ChartSlotKey& slot)
{
    const auto run = std::ranges::equal_range(
        keyframe_keys, slot, {}, [](const ChartKeyframeKey& key) { return key.note; });
    std::vector<common::core::Fraction> offsets;
    offsets.reserve(static_cast<std::size_t>(std::ranges::distance(run)));
    for (const ChartKeyframeKey& key : run)
    {
        offsets.push_back(key.offset);
    }
    return offsets;
}

// One stop a retype addresses: the note it lives on, where inside that note, and what it currently
// reads. A head's fret and a keyframe's fret are the same kind of statement one level apart, so
// both populations walk ONE list — which is what lets the anchor, the delta and the write be
// written once instead of once per kind, and what makes a chord slide's members transpose together
// with their heads.
struct AddressedStop
{
    // Index into the retype's base snapshot.
    std::size_t base_index{};
    // The keyframe's offset, or absent for the note's own stop.
    std::optional<common::core::Fraction> keyframe_offset{};
    // The stop's current value, which the anchor reads and the transposing write shifts.
    int value{};
};

// Replays a duration gesture's steps over one note's PRE-GESTURE ring and returns the ring they
// author.
//
// Deliberately UNBOUNDED: the floor and the same-string bound judge this answer at the call site
// and are never fed back into the walk. That is the gesture's symmetry (ruling 8) — a step a bound
// absorbed would otherwise become the next step's starting value, and a chord member pinned on the
// way out would come back on a different ring than it left on. So the authored ring may sit past a
// note's bound, or at or below zero, between steps; the caller resolves both.
//
// A step moves the ring's END, an absolute position, onto the adjacent line of the step's own
// lattice strictly beyond it — the same primitive the caret step and the lane nudge walk with,
// which is what makes an end left between lines SNAP onto them instead of carrying its remainder
// forever.
//
// Two degenerate ends are harmless and deliberately unguarded: an authored ring at or below zero
// puts the end at or before the onset, where advanceGridPosition clamps at the grid origin and
// adjacentTempoGridPosition can collapse onto its input — a ring stepped back past the start of the
// song simply stops moving, and the floor holds the note's visible ring either way.
[[nodiscard]] common::core::Fraction authoredSustain(
    const common::core::TempoMap& tempo_map, const common::core::ChartNote& start,
    const std::vector<ChartSustainStep>& steps)
{
    common::core::Fraction ring = start.sustain;
    for (const ChartSustainStep& step : steps)
    {
        const common::core::GridPosition end =
            common::core::advanceGridPosition(tempo_map, start.position, ring);
        ring = common::core::beatDistance(
            tempo_map,
            start.position,
            adjacentTempoGridPosition(tempo_map, step.note_value, end, step.grow));
    }
    return ring;
}

// THE SPLIT, for one note: appends the products of cutting `note` at each instant to `products`.
//
// Each product spans one segment of the original ring — [start, end) — so the walk is lossless by
// construction rather than by a rule each caller restates: the keyframes inside a segment ride
// along rebased onto its onset, and the CHANNEL states in force at the cut (`ringStateAt`: fret,
// bend, vibrato) open the new head, so the pitch does not jump across it. Every one of the
// note's own flags rides onto both products, since each product starts life as a copy of it.
// Walking the whole ring as segments rather than special-casing "origin plus remainder" is what
// makes two instants on one note three notes without a second rule.
//
// The fret in force is the STATED one, never the interpolated travel between two stating points:
// rounding travel is invented data, and a head must sit on a fret the hand actually takes. So a cut
// mid-glide leaves the origin holding the fret it set out from while the remainder travels on to
// the arrival.
//
// Each head taking over is STRUCK and stores `Pick`: a junction with no strike is the join's one
// longer ring, so a split authors the re-attack (see the header). \ref planCutRing, the other verb
// that divides a ring, replaces that head with one at strike defaults.
//
// Instants must be strictly inside the ring and strictly ascending; one at the ring's END is not a
// split at all (the ring already stops there) and is refused, which is also the whole of the
// disconnect's "a keyframe at the ring's end has no remainder to hand over".
[[nodiscard]] std::expected<void, ChartPlanRefusal> splitNoteIntoProducts(
    const common::core::TempoMap& tempo_map, const common::core::ChartNote& note,
    const std::vector<common::core::Fraction>& instants,
    std::vector<common::core::ChartNote>& products)
{
    // A SCRAPE is one gesture of the picking hand end to end, so it has no junction to sever: every
    // product but the first would be a fretting-hand note the charter never wrote, and the JOIN
    // refuses a scrape on either side, so such a product could never be joined back.
    if (common::core::isScrape(note.attack))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    for (const common::core::Fraction& instant : instants)
    {
        if (!(common::core::Fraction{0, 1} < instant) || !(instant < note.sustain))
        {
            return std::unexpected{ChartPlanRefusal::Invalid};
        }
    }
    common::core::Fraction start{0, 1};
    for (std::size_t index = 0; index <= instants.size(); ++index)
    {
        // True when a new head takes over at this product's end — every product but the last.
        const bool re_picked = index < instants.size();
        const common::core::Fraction end = re_picked ? instants[index] : note.sustain;
        common::core::ChartNote product = note;
        product.keyframes.clear();
        if (index > 0)
        {
            const common::core::RingState carried = common::core::ringStateAt(note, start);
            product.position = common::core::advanceGridPosition(tempo_map, note.position, start);
            product.fret = carried.fret;
            product.bend = carried.bend;
            product.vibrato = carried.vibrato;
            product.attack = common::core::NoteAttack::Pick;
        }
        // The ring runs to where the string is next struck, which after a split is the next
        // product's onset: a re-strike is what stops a ring, and the drawn tail is the
        // presentation rules' business, not this walk's.
        product.sustain = end - start;
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            if (!(start < keyframe.offset) || end < keyframe.offset)
            {
                continue;
            }
            common::core::Keyframe rebased = keyframe;
            rebased.offset = keyframe.offset - start;
            product.keyframes.push_back(rebased);
        }
        // A statement standing exactly at a cut becomes the product's END statement, ON the head
        // the next product starts at, and the chart then PROVES what it is: the fret named is the
        // stop that product is struck at, so the statement is an ARRIVAL and not a slide-out
        // (\ref common::core::arrivesIntoNextHead). `ringStateAt` at the cut reads that same
        // keyframe as the new head's own statement, which is why the two can never name different
        // stops. A SLIDE-OUT reaches only the product ending where the gesture did, every earlier
        // product ending at a cut the next product is struck at.
        //
        // What the end may KEEP is the channel table's: VIBRATO there has no ring left to vibrate
        // in and goes — no loss, being the NEXT product's onset state — while the BEND stays, the
        // curve's last value completing as this product's ring does.
        static_cast<void>(common::core::shedEndStatementVibrato(product));
        products.push_back(std::move(product));
        start = end;
    }
    return {};
}

// THE JOIN, for one head: folds `head` into `predecessor` as a junction POINT on its path, and
// returns the offset the point took (which is the key the selection then carries).
//
// The exact inverse of the split walk above, written as the same authority run backward rather
// than as a second law — one segment of a ring becomes one leg of the one before it. The
// predecessor's ring grows to the two rings laid end to end, the head's keyframes rebase onto the
// predecessor's onset, and the point states the head's fret always (it is what the split's product
// head took) plus the channels whose onset value DIFFERS from the ring already running, since a
// channel restating what is in force says nothing.
//
// Nothing else of the head survives. Its attack, mutes, node, tremolo, emphasis and held stop are
// facts about a STRIKE, and the join is precisely the statement that no strike happens there.
//
// THE ARRIVAL NEEDS NO RETURN. The split leaves its product's arrival AT the cut, so the statement
// is already standing at the junction when the join reaches it and the merge below takes it over —
// which is the whole of what makes split-then-join an exact round trip, with no "did this point
// retreat?" equality test and no spelling-based distinction between a retreated arrival and a
// charter's own point at the clearance.
[[nodiscard]] std::expected<common::core::Fraction, ChartPlanRefusal> joinHeadIntoPath(
    const common::core::TempoMap& tempo_map, common::core::ChartNote& predecessor,
    const common::core::ChartNote& head)
{
    // A scrape's travel is the PICK's position on the string, so no fretting finger arrives
    // anywhere for a path to continue from.
    // A SLIDE-OUT's tail is authored exit geometry, not slack to spend: growing the ring under it
    // would rewrite the gesture (the D14 assist refuses the same reshape, planSetLegato). An
    // ARRIVAL is the opposite — the finger is already on the stop this very head takes — so it is
    // joinable, and the predicate is asked of the PAIR this function holds rather than of a
    // resolved vector it has no index into.
    // A fret-hand harmonic is a touch, and a touch holds nothing to hand over.
    const bool arrives = common::core::arrivesIntoNextHead(predecessor, head, tempo_map);
    if (common::core::isScrape(predecessor.attack) ||
        common::core::slideOutFretOrNull(predecessor, arrives) != nullptr ||
        common::core::fretHandHarmonic(predecessor))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    // A scrape is not a fretted stop, so its ring is no path a point could continue; and a point
    // states frets and channels, never a harmonic node.
    if (common::core::isScrape(head.attack) || head.harmonic_node.has_value())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

    const common::core::Fraction gap =
        common::core::beatDistance(tempo_map, predecessor.position, head.position);
    // An arrival already standing at the junction is part of what is in force there — which is what
    // makes the round trip's point restate the fret rather than change it.
    const common::core::RingState at = common::core::ringStateAt(predecessor, gap);
    const std::optional<double> point_bend =
        std::is_neq(head.bend <=> at.bend) ? std::optional<double>{head.bend} : std::nullopt;
    // The head's onset width is the width of the leg the point begins, whatever the leg before it
    // was: nothing carries in the vibrato channel, so it is stated rather than compared.
    const common::core::Keyframe point{
        .offset = gap, .fret = head.fret, .bend = point_bend, .vibrato = head.vibrato
    };
    if (!predecessor.keyframes.empty() && predecessor.keyframes.back().offset == gap)
    {
        // The arrival standing there IS the junction, so the point merges into it rather than
        // doubling its offset — a second record on one offset is a shape no chart may hold. The
        // merge is the overlay law's (common::core::overlayKeyframe): a channel the point does not
        // state is left exactly as the arrival had it, which is what `at` just read as in force.
        // What the head's own onset states — its bend, its vibrato — is meant to overwrite the
        // arrival's there: the join is the charter folding the head in, not a truncation.
        static_cast<void>(common::core::overlayKeyframe(predecessor.keyframes.back(), point));
    }
    else
    {
        predecessor.keyframes.push_back(point);
    }

    // The two rings laid end to end. The head's keyframes follow the point by construction (their
    // offsets are strictly positive), so appending keeps the array ascending, and a slide-out of
    // the head lands on the grown ring's end and is the predecessor's slide-out now.
    predecessor.sustain = gap + head.sustain;
    for (const common::core::Keyframe& keyframe : head.keyframes)
    {
        common::core::Keyframe rebased = keyframe;
        rebased.offset = keyframe.offset + gap;
        predecessor.keyframes.push_back(rebased);
    }
    return gap;
}

} // namespace

std::expected<ChartEditPlan, ChartPlanRefusal> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note, const common::core::Fraction grid_note_value)
{
    // Every note rings, so a placement authors a duration whether the user thought about one or
    // not, and the session's grid is what they were looking at when they placed it: the ring
    // reaches its next line, a lattice position by construction where one exact step out from an
    // onset need not be (a septuplet grid's step is not a whole number of ticks). The finalize
    // gate's same-string normalization does the clamping: a ring that would run through the next
    // onset on the string ends exactly on it.
    note.sustain = common::core::beatDistance(
        tempo_map,
        note.position,
        adjacentTempoGridPosition(tempo_map, grid_note_value, note.position, true));
    // An occupied slot is the gate's refusal (two notes on one slot), never a replace: the entry
    // keys address a head that stands where they land, so a placement never meets one.
    std::vector<common::core::ChartNote> candidate = chart.notes;
    candidate.push_back(std::move(note));
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), "Insert Note");
}

std::optional<ChartPathTail> chartPathTailAt(
    const std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map,
    const common::core::GridPosition position, const int string)
{
    for (const common::core::ChartNote& note : notes)
    {
        if (note.string != string)
        {
            continue;
        }
        const common::core::Fraction offset =
            common::core::beatDistance(tempo_map, note.position, position);
        if (!(common::core::Fraction{0} < offset) || note.sustain < offset)
        {
            continue;
        }
        return ChartPathTail{
            .note = chartSlotKeyOf(note),
            .offset = offset,
            .at_ring_end = !(offset < note.sustain),
        };
    }
    return std::nullopt;
}

std::expected<ChartEditPlan, ChartPlanRefusal> planInsertKeyframe(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& note, const common::core::Fraction offset, const int fret)
{
    std::vector<common::core::ChartNote> candidate = chart.notes;
    const auto target =
        std::ranges::find_if(candidate, [&note](const common::core::ChartNote& existing) {
            return chartSlotKeyOf(existing) == note;
        });
    if (target == candidate.end())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    // Inserted at its sorted place and never merged onto a record already there: a second keyframe
    // on one offset leaves the note's offsets no longer strictly ascending, which is the rule
    // authority's refusal and not one this planner restates. The point carries the width of the
    // leg it divides, so planting it never ends a vibrato.
    const auto at = std::ranges::upper_bound(
        target->keyframes, offset, {}, [](const common::core::Keyframe& keyframe) {
            return keyframe.offset;
        });
    common::core::Keyframe point = common::core::keyframeInLeg(*target, offset);
    point.fret = fret;
    target->keyframes.insert(at, point);

    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), "Insert Keyframe");
}

std::expected<ChartEditPlan, ChartPlanRefusal> planCutRing(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& note, const common::core::Fraction offset, const int fret)
{
    const auto origin =
        std::ranges::find_if(chart.notes, [&note](const common::core::ChartNote& existing) {
            return chartSlotKeyOf(existing) == note;
        });
    if (origin == chart.notes.end())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    std::vector<common::core::ChartNote> products;
    const std::expected<void, ChartPlanRefusal> walked =
        splitNoteIntoProducts(tempo_map, *origin, {offset}, products);
    if (!walked.has_value())
    {
        return std::unexpected{walked.error()};
    }
    // THE FRESH HEAD. The walk's second product is the origin picked again — the remainder, the
    // keyframes past the cut, the channel states in force there — and every other fact of the
    // origin's strike besides. The cut's head is its own strike: the ring's facts ride
    // over, the strike's do not, and the fret is the one typed. The ring's facts are the walk's
    // own channel list (fret, bend, vibrato, the keyframes), so a channel added there is copied
    // here as well.
    const common::core::ChartNote& severed = products[1];
    common::core::ChartNote head;
    head.position = severed.position;
    head.string = severed.string;
    head.fret = fret;
    head.sustain = severed.sustain;
    head.vibrato = severed.vibrato;
    head.bend = severed.bend;
    head.keyframes = severed.keyframes;

    std::vector<common::core::ChartNote> candidate = chart.notes;
    candidate[static_cast<std::size_t>(origin - chart.notes.begin())] = products[0];
    candidate.push_back(std::move(head));
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), "Cut Ring");
}

int chartFretInForceAt(
    const std::vector<common::core::ChartNote>& notes, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& slot)
{
    if (const std::optional<ChartPathTail> tail =
            chartPathTailAt(notes, tempo_map, slot.position, slot.string);
        tail.has_value() && !tail->at_ring_end)
    {
        const auto carrier =
            std::ranges::find_if(notes, [&tail](const common::core::ChartNote& note) {
                return chartSlotKeyOf(note) == tail->note;
            });
        if (carrier != notes.end())
        {
            return common::core::ringStateAt(*carrier, tail->offset).fret;
        }
    }
    // Past a ring's end, the end slot included: the string's latest note before the slot. The
    // stream is in chart order, so the scan stops at the first note not before the slot.
    const common::core::ChartNote* latest = nullptr;
    for (const common::core::ChartNote& candidate : notes)
    {
        if (!(common::core::Fraction{0} <
              common::core::beatDistance(tempo_map, candidate.position, slot.position)))
        {
            break;
        }
        if (candidate.string == slot.string)
        {
            latest = &candidate;
        }
    }
    return latest != nullptr ? common::core::fretBeforeEnd(*latest) : 0;
}

std::expected<ChartEditPlan, ChartPlanRefusal> planDeleteSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys)
{
    KeyedSplit notes = splitByKeys(chart.notes, note_keys);
    const std::size_t deleted_notes = notes.keyed.size();
    // Keyframes go from the notes that SURVIVE: one whose note this call deletes needs no removal
    // of its own. On a point Delete takes its FRET — the stop it states is the point's own thing,
    // as a head is the note's — and every other technique there stays for its own verb to clear
    // (the bend picker's "No bend", `V`). The point itself goes where nothing new is left on it
    // (the commit law's own question), and a point stating no fret is its other techniques alone,
    // so it goes whole: an erase, never a strip to a bare boundary, which where the leg before it
    // vibrates would still end the vibrato. A fret that says nothing new is no fret
    // (shedSilentStatements): withdrawing it would change nothing the charter can see.
    std::size_t deleted_keyframes = 0;
    std::size_t stripped_frets = 0;
    for (common::core::ChartNote& note : notes.rest)
    {
        const std::vector<common::core::Fraction> offsets =
            selectedOffsetsOn(keyframe_keys, chartSlotKeyOf(note));
        if (offsets.empty())
        {
            continue;
        }
        // A point is judged against the note WITHOUT it, the commit law's own terms. Every keyed
        // fret that says something is withdrawn first, each asked of the note as the selection
        // found it and ascending like the points, so each survivor below is judged with its
        // neighbours' withdrawals already made.
        std::vector<common::core::Fraction> withdrawn;
        const common::core::ChartNote as_found = note;
        for (common::core::Keyframe& keyframe : note.keyframes)
        {
            if (std::ranges::binary_search(offsets, keyframe.offset) &&
                common::core::shedSilentStatements(
                    common::core::noteWithoutKeyframe(as_found, keyframe.offset), keyframe)
                    .fret.has_value())
            {
                keyframe.fret.reset();
                withdrawn.push_back(keyframe.offset);
            }
        }
        // Only the keyed points are judged: a silent point elsewhere on the note, planted and not
        // yet landed, is the commit law's to take when focus leaves, never this Delete's.
        const common::core::ChartNote before_sweep = note;
        const auto goes =
            [&offsets, &withdrawn, &before_sweep](const common::core::Keyframe& point) {
                if (!std::ranges::binary_search(offsets, point.offset))
                {
                    return false;
                }
                if (!std::ranges::binary_search(withdrawn, point.offset))
                {
                    return true;
                }
                return common::core::keyframeSaysNothingNew(
                    common::core::noteWithoutKeyframe(before_sweep, point.offset), point);
            };
        deleted_keyframes += std::erase_if(note.keyframes, goes);
        // A point that lost its fret and still stands kept a technique of its own.
        stripped_frets += static_cast<std::size_t>(
            std::ranges::count_if(withdrawn, [&note](const common::core::Fraction& offset) {
                return std::ranges::contains(
                    note.keyframes, offset, &common::core::Keyframe::offset);
            }));
    }
    if (deleted_notes == 0 && deleted_keyframes == 0 && stripped_frets == 0)
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // The label names what was actually deleted rather than counting one kind for both: a mixed
    // burst has no honest noun, so it names the selection instead of claiming a count of notes.
    const auto count_label = [](const std::size_t count,
                                const std::string_view singular,
                                const std::string_view plural) {
        return count == 1 ? std::string{singular}
                          : std::to_string(count) + " " + std::string{plural};
    };
    const int kinds = static_cast<int>(deleted_notes > 0) +
                      static_cast<int>(deleted_keyframes > 0) +
                      static_cast<int>(stripped_frets > 0);
    std::string label;
    if (kinds > 1)
    {
        label = "Delete Selection";
    }
    else if (deleted_notes > 0)
    {
        label = "Delete " + count_label(deleted_notes, "Note", "Notes");
    }
    else if (deleted_keyframes > 0)
    {
        label = "Delete " + count_label(deleted_keyframes, "Keyframe", "Keyframes");
    }
    else
    {
        label = "Remove " + count_label(stripped_frets, "Fret", "Frets");
    }
    return finalizePlan(chart, tempo_map, chart.notes, std::move(notes.rest), label);
}

ChartMoveDelta chartMoveGestureDelta(
    const common::core::TempoMap& tempo_map, const std::vector<ChartSlotKey>& note_keys,
    const std::vector<ChartKeyframeKey>& keyframe_keys, const std::vector<ChartMoveStep>& steps)
{
    ChartMoveDelta delta{};
    if (note_keys.empty() && keyframe_keys.empty())
    {
        return delta;
    }
    // The ANCHOR the presses are measured at. Any selected object serves — the step is uniform over
    // the whole selection — so the front of whichever kind is present does; a keyframe anchors at
    // the instant it states, since that is what its step lands on a line.
    const common::core::GridPosition anchor =
        !note_keys.empty()
            ? note_keys.front().position
            : common::core::advanceGridPosition(
                  tempo_map, keyframe_keys.front().note.position, keyframe_keys.front().offset);
    for (const ChartMoveStep& step : steps)
    {
        switch (step.direction)
        {
            case ChartStepDirection::Left:
            case ChartStepDirection::Right:
            {
                // Each press carries the anchor from where the run has REACHED — not from where it
                // started, which is the whole reason the presses are kept in order — onto the
                // adjacent line of the press's own lattice, and is worth that distance in whole
                // notes: a whole number of ticks, so every object the delta then moves lands on the
                // tick lattice in whichever meter it arrives in.
                const common::core::GridPosition reached =
                    common::core::advanceGridPositionByWholeNotes(
                        tempo_map, anchor, delta.whole_notes);
                const common::core::GridPosition line = adjacentTempoGridPosition(
                    tempo_map,
                    step.note_value,
                    reached,
                    step.direction == ChartStepDirection::Right);
                delta.whole_notes =
                    delta.whole_notes + common::core::wholeNoteDistance(tempo_map, reached, line);
                break;
            }
            case ChartStepDirection::Up:
            {
                ++delta.strings;
                break;
            }
            case ChartStepDirection::Down:
            {
                --delta.strings;
                break;
            }
        }
    }
    return delta;
}

common::core::Fraction chartSteppedKeyframeOffset(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartKeyframeKey& keyframe, const common::core::Fraction whole_note_delta)
{
    const common::core::Fraction stepped = carriedOffset(
        tempo_map,
        keyframe.note.position,
        keyframe.offset,
        whole_note_delta,
        keyframe.note.position);
    const common::core::ChartNote* const note = chartNoteAt(chart.notes, keyframe.note);
    if (note == nullptr)
    {
        return stepped;
    }
    // Only the statement AT the ring's end carries that end with it, and the end is the one thing
    // 40-Q2-B bounds. Every other point is bounded by that end instead, which planMoveSelection
    // refuses a step past rather than clamping — so a key naming an interior point, or naming
    // nothing, answers with the plain step. NOTE-LOCAL, and the relation is no part of it: a
    // slide-out and a shift slide's arrival are the same point at the same moment, and moving
    // either moves the end, so asking which gesture it proves would change nothing this verb does.
    const common::core::Keyframe* const end = common::core::endFretStatement(*note);
    if (end == nullptr || end->offset != keyframe.offset)
    {
        return stepped;
    }
    return common::core::ringEndWithinBound(chart.notes, *note, tempo_map, stepped);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planMoveSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    common::core::Fraction whole_note_delta, int string_delta, std::string_view label)
{
    if ((note_keys.empty() && keyframe_keys.empty()) ||
        (whole_note_delta.numerator == 0 && string_delta == 0))
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }

    const int string_count = static_cast<int>(chart.tuning.strings.size());
    KeyedSplit notes = splitByKeys(chart.notes, note_keys);

    // The keyframe half of the same step, taken on the notes the selection LEFT STANDING: a
    // selected note carries its own path along by moving whole, so stepping its keyframes too
    // would move them twice. The offsets stay in their stored order rather than being re-sorted,
    // which is what makes a step onto or across a neighbour show up as offsets that no longer
    // ascend — a refusal from the one rule authority, never a swap this planner had to forbid.
    //
    // The SLIDE-OUT is the ring's end, so stepping it steps the end with it: the slide-out's length
    // is the point's to change, and this is the verb that changes it — outward for a longer
    // slide-out, inward for a shorter one, never onto or across the last sounded fret (the order
    // refusal above), and outward only as far as a ring's end may reach, where the slide-out parks.
    // Read before any offset moves, because the slide-out is recognised by sitting exactly at the
    // end.
    //
    // KIND IS NOT THIS VERB'S TO CHANGE: a point that already IS the slide-out drags the end, and
    // every other point lives STRICTLY inside the ring at both ends — the onset below (the
    // validator's strictly-positive offsets) and the end above (the bound below). So the slide-out
    // a step reads off the note it was handed is still the slide-out after it, which is what lets a
    // held run replay from its pre-gesture chart at all.
    bool stepped_keyframe = false;
    if (whole_note_delta.numerator != 0)
    {
        for (common::core::ChartNote& note : notes.rest)
        {
            const ChartSlotKey slot = chartSlotKeyOf(note);
            const std::vector<common::core::Fraction> offsets =
                selectedOffsetsOn(keyframe_keys, slot);
            if (offsets.empty())
            {
                continue;
            }
            const common::core::Keyframe* const slide_out = common::core::endFretStatement(note);
            // The ring's end AFTER this step, which every INTERIOR point must stay STRICTLY below:
            // the move verb never changes what a point IS, so a step that would reach the end is
            // refused exactly like one that reaches the onset below or the neighbour beside. Read
            // off the slide-out when the slide-out is stepping too — one uniform delta moves both,
            // so a figure selected whole keeps its shape and the end travels with it — and asked of
            // the one authority that holds a ring's end at the next head on its string
            // (\ref chartSteppedKeyframeOffset), so a slide-out stepped onto that head lands there
            // and one stepped past it parks on it, exactly as the duration verb's clamp does.
            //
            // Without the bound the step authored a slide-out the burst could not then drag: the
            // gesture replays from the PRE-GESTURE chart, where the point is still interior, so
            // the end never followed the next press and the run stuck until re-selection. And what
            // it left behind was a slide-out nothing draws (a repeated fret) or one shed of its
            // vibrato (shedEndStatementVibrato) — a point that lost its meaning to a move.
            const common::core::Fraction end =
                slide_out != nullptr && std::ranges::binary_search(offsets, slide_out->offset)
                    ? chartSteppedKeyframeOffset(
                          chart,
                          tempo_map,
                          ChartKeyframeKey{.note = slot, .offset = slide_out->offset},
                          whole_note_delta)
                    : note.sustain;
            for (common::core::Keyframe& keyframe : note.keyframes)
            {
                if (!std::ranges::binary_search(offsets, keyframe.offset))
                {
                    continue;
                }
                // The SLIDE-OUT is the ring's end, so stepping it steps the end with it — as far as
                // that end may reach and no further, the clamp being where the whole step lands
                // rather than a second answer applied after it. The lower bound both kinds share
                // is the validator's (offsets are strictly positive).
                if (&keyframe == slide_out)
                {
                    keyframe.offset = end;
                    note.sustain = end;
                }
                else
                {
                    keyframe.offset = carriedOffset(
                        tempo_map, note.position, keyframe.offset, whole_note_delta, note.position);
                    // Against the CLAMPED end: a step that would strand an interior point on or
                    // past the head its own ring stops at is refused, never clamped — clamping it
                    // would stack it on the slide-out.
                    if (!(keyframe.offset < end))
                    {
                        return std::unexpected{ChartPlanRefusal::Invalid};
                    }
                }
                stepped_keyframe = true;
            }
        }
    }

    if (notes.keyed.empty())
    {
        if (!stepped_keyframe)
        {
            return std::unexpected{ChartPlanRefusal::NoChange};
        }
        return finalizePlan(chart, tempo_map, chart.notes, std::move(notes.rest), label);
    }
    if (!moveKeyedNotes(tempo_map, notes.keyed, whole_note_delta, string_delta, string_count))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

    // Converging moves that stack two notes on one slot are refused, as is landing on a slot an
    // unmoved note occupies.
    std::vector<ChartSlotKey> target_keys;
    target_keys.reserve(notes.keyed.size());
    for (const common::core::ChartNote& note : notes.keyed)
    {
        target_keys.push_back(chartSlotKeyOf(note));
    }
    std::ranges::sort(target_keys);
    if (std::ranges::adjacent_find(target_keys) != target_keys.end())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    const bool lands_on_unmoved =
        std::ranges::any_of(notes.rest, [&target_keys](const common::core::ChartNote& note) {
            return std::ranges::binary_search(target_keys, chartSlotKeyOf(note));
        });
    if (lands_on_unmoved)
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    notes.rest.insert(notes.rest.end(), notes.keyed.begin(), notes.keyed.end());
    return finalizePlan(chart, tempo_map, chart.notes, std::move(notes.rest), label);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const std::vector<ChartSlotKey>& note_keys,
    const std::vector<ChartKeyframeKey>& keyframe_keys, const ChartFretWrite write)
{
    if (base.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // Null for a shift, which names no value of its own.
    const ChartFretSet* const set = std::get_if<ChartFretSet>(&write);

    // EVERY stop this plan addresses, collected once so the anchor and the write can never read
    // different fields. The two key lists say WHICH: a note's own stop where the selection named
    // the note, a keyframe's fret where it named the keyframe. A note in the
    // snapshot that neither list names is written through and not addressed — the shape a mixed
    // selection takes, and the fret-verb law's other half (a head's digit never moves its path).
    std::vector<AddressedStop> addressed;
    addressed.reserve(base.size() + keyframe_keys.size());
    for (std::size_t base_index = 0; base_index < base.size(); ++base_index)
    {
        const common::core::ChartNote& note = base[base_index];
        if (std::ranges::binary_search(note_keys, chartSlotKeyOf(note)))
        {
            // A FRET-HAND HARMONIC HAS NO STOP TO RETYPE. Its finger stands on the node and
            // presses nothing, so its fret is not a value the charter can restate — landing the
            // digit would author `fret 5 + node 4.98`, a stop and a touch naming two different
            // places, which no rule catches because 4.98 is not beyond nothing. Refused whole
            // rather than skipped: the pending box has to say the value cannot land instead of
            // leaving it looking typed. Restating a node is press `H`, type, press `H`.
            if (common::core::fretHandHarmonic(note))
            {
                return std::unexpected{ChartPlanRefusal::Invalid};
            }
            addressed.push_back(
                AddressedStop{.base_index = base_index, .keyframe_offset = {}, .value = note.fret});
        }
    }
    // The keyframe half: the selection kind already said which stop the digit meant. A keyframe
    // stating no fret INHERITS the fret in force and is drawn and
    // selected like any other point (ruled 2026-09-27), so a typed value pointed at it STATES its
    // fret there, beside whatever else the point states. A SHIFT moves stops, and such a point has
    // no stop of its own — it rides the path it inherits — so a shift takes none.
    for (std::size_t base_index = 0; base_index < base.size(); ++base_index)
    {
        const common::core::ChartNote& note = base[base_index];
        const std::vector<common::core::Fraction> offsets =
            selectedOffsetsOn(keyframe_keys, chartSlotKeyOf(note));
        if (offsets.empty())
        {
            continue;
        }
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            // Bound to a local so the presence test and the read are provably one object.
            const std::optional<int>& fret = keyframe.fret;
            if (!std::ranges::binary_search(offsets, keyframe.offset) ||
                (!fret.has_value() && set == nullptr))
            {
                continue;
            }
            // A set writes its own value to every addressed stop, so a point stating no fret
            // carries the typed one; only a shift reads the stop's value, and it never gets here
            // without one.
            addressed.push_back(
                AddressedStop{
                    .base_index = base_index,
                    .keyframe_offset = keyframe.offset,
                    .value = fret.has_value() ? *fret : set->fret,
                });
        }
    }

    // One delta over every addressed stop — a selected head and a selected keyframe alike, which is
    // what makes a chord slide's points move together with the heads. The shift NAMES that delta;
    // nothing here anchors it.
    const int delta = set != nullptr ? 0 : std::get<ChartFretShift>(write).delta;
    const std::string label =
        set != nullptr ? "Set Fret " + std::to_string(set->fret)
                       : "Shift Frets " + std::string{delta > 0 ? "+" : ""} + std::to_string(delta);
    // Retyped values compute from the SNAPSHOT (the multi-digit window replans the whole entry from
    // the pre-entry originals) and swap into the live stream for the shared finalize, whose
    // whole-matrix gate stands in place of local fret caps here: any out-of-range or rule-violating
    // result refuses the plan outright.
    //
    // The snapshot is COPIED WHOLE and only the addressed stops are written over, which is the
    // fret-verb law by construction: a stop nothing addressed keeps the value the charter gave it —
    // a head's path when the digit named the head, a head's own fret when it named a point on that
    // head's path. A scrape is no exception and its path must not translate with its start: a
    // scrape start retyped onto its first path position is refused downstream by the
    // always-traveling rule in the finalize gate, and a pitched slide's equal-fret start is the
    // legal hold encoding and passes.
    std::vector<common::core::ChartNote> retyped_notes = base;
    for (const AddressedStop& stop : addressed)
    {
        const int value = set != nullptr ? set->fret : stop.value + delta;
        common::core::ChartNote& retyped = retyped_notes[stop.base_index];
        // Bound to a local so the presence test and the read are provably one object.
        const std::optional<common::core::Fraction>& at = stop.keyframe_offset;
        if (!at.has_value())
        {
            // A NODE TRAVELS WITH ITS STOP. A node is `stop + offset` and fret positions are
            // logarithmic, so the offset the harmonic names survives a move only if the node moves
            // by the same amount; leaving it behind authors a touch that is no node of the string
            // as newly stopped. The fret-hand form never reaches here — it was refused above — so
            // what this moves is the pressed stop under a node: the artificial family, a tapped
            // harmonic's stop, and a pinch's graze. Whether the moved node is still legal is the
            // finalize gate's answer, like every other bound this planner leaves to it.
            //
            // Bound to a local so the presence test and the write are provably one object.
            std::optional<double>& node = retyped.harmonic_node;
            if (node.has_value())
            {
                node = *node + static_cast<double>(value - stop.value);
            }
            retyped.fret = value;
            continue;
        }
        // Read once, outside the scan: the offset is constant across it, and a read inside the
        // loop is one the optional-access analysis cannot tie back to the guard above.
        const common::core::Fraction offset = *at;
        for (common::core::Keyframe& keyframe : retyped.keyframes)
        {
            if (keyframe.offset == offset)
            {
                keyframe.fret = value;
                break;
            }
        }
    }

    std::vector<common::core::ChartNote> candidate = chart.notes;
    for (common::core::ChartNote& note : candidate)
    {
        for (const common::core::ChartNote& retyped : retyped_notes)
        {
            if (chartSlotKeyOf(retyped) == chartSlotKeyOf(note))
            {
                note = retyped;
                break;
            }
        }
    }
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), label);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planAdjustSustain(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, const std::vector<ChartSlotKey>& keys,
    const std::vector<ChartSustainStep>& steps)
{
    // A gesture that has recorded nothing describes nothing; the finalize below would answer
    // NoChange anyway, so say it up front.
    if (steps.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // The candidate starts from the LIVE stream because a floored note keeps the ring it currently
    // has; every note the replay does reach is then rebuilt WHOLE from its pre-gesture value, which
    // is also what restores payload an earlier step's shrink clipped away.
    std::vector<common::core::ChartNote> candidate = chart.notes;
    // What the entry changes the selection's rings by in total, start → now, which is what names
    // it below.
    common::core::Fraction net{};
    for (common::core::ChartNote& note : candidate)
    {
        if (!std::ranges::binary_search(keys, chartSlotKeyOf(note)))
        {
            continue;
        }
        const auto start = std::ranges::lower_bound(
            base, chartSlotKeyOf(note), {}, [](const common::core::ChartNote& based) {
                return chartSlotKeyOf(based);
            });
        if (start == base.end() || chartSlotKeyOf(*start) != chartSlotKeyOf(note))
        {
            continue;
        }
        common::core::ChartNote stepped = *start;
        common::core::Fraction target = authoredSustain(tempo_map, *start, steps);
        // The ring's FLOOR: the last keyframe's offset where the note carries one, the onset
        // otherwise — a point never leaves the ring, and this verb moves the ribbon, never a point.
        // A replay that reaches past the floor has nowhere legal to put the end: the note keeps
        // the ring it currently has — the live value the candidate was seeded with — rather than
        // being clamped to some invented value, and rejoins the gesture the moment the replayed
        // ring clears the floor again. Deleting the keyframe, or dragging it (the move verb, which
        // on a slide-out drags the end with it), is the verb for going further. The floor is
        // INCLUSIVE where landing on it makes the point a REAL slide-out and costs it nothing:
        // pulling the end exactly onto a keyframe that states a fret, nothing else, and a fret the
        // path does not already hold there, on a ring that simply ends, is how a glide becomes an
        // unpitched slide-out. On a ring that already slides out the floor IS it and stays
        // exclusive — the ribbon cannot pass its own end point, and the slide-out's length is the
        // point's to change. The onset itself is never a legal end,
        // so the empty ring's floor stays exclusive too. A scrape's path is DERIVED and
        // re-terminates onto whatever tail it has, so it floors at the minimum gesture window
        // instead, clamped — always positive, so it never reaches the hold below.
        common::core::Fraction floor{};
        bool floor_may_end_the_ring = false;
        if (common::core::isScrape(stepped.attack))
        {
            const common::core::Fraction window =
                minimumSlideWindowRing(tempo_map, stepped.position);
            if (target < window)
            {
                target = window;
            }
        }
        else if (!stepped.keyframes.empty())
        {
            floor = stepped.keyframes.back().offset;
            // Whether the landing is legal at all is the model's own question, asked here rather
            // than restated: this verb may shorten a ring, but it may neither delete a statement
            // nor author one nothing shows (ringEndMayLandOnLastKeyframe, the one spelling the move
            // verb can ask the same way).
            floor_may_end_the_ring = common::core::ringEndMayLandOnLastKeyframe(stepped);
        }
        if (floor < target || (floor_may_end_the_ring && floor == target))
        {
            // The one bound on a ring (40-Q2-B), asked of the one authority that applies it — the
            // same call the move verb's stepped slide-out makes, so both verbs give one answer for
            // a ring's end reaching the next head on its string. The margin that binds growth
            // against ANY string is the DRAWN tail's spacing rule, which presentation owns rather
            // than this clamp. The clamp can never SHORTEN a note below where the gesture found it:
            // normalizeSustainOverlaps holds every stored ring inside this same bound, so `start`
            // is already at most the bound.
            target = common::core::ringEndWithinBound(chart.notes, note, tempo_map, target);
            // The one way a ring changes length once it carries a payload: a pitched note's
            // keyframes all lie above its floor, so nothing clips there — the resize leaves a
            // slide-out behind a lengthening ring as the pitched stop it has become, and re-aims a
            // scrape's compressed path. A scrape needs no clause of its own: its terminal rides
            // the end, so a gesture grown onto the next head ends exactly there with its terminal
            // on it, which is what the store holds and what presentation then spaces.
            static_cast<void>(common::core::clipPayloadsToSustain(stepped, target));
            note = std::move(stepped);
        }
        else if (floor == target && common::core::endStatedFretOrNull(stepped) != nullptr)
        {
            // A running gesture can grow a slide-out into an ordinary pitched keyframe, then step
            // straight back to the slide-out it started from. That replay describes no edit
            // relative to `base`, but the live chart still holds the grown note the candidate was
            // seeded with; restore the replayed slide-out so finalizePlan reports NoChange and the
            // controller retires the grow entry instead of treating the shrink as refused.
            note = std::move(stepped);
        }
        // A held note counts too: the ring it keeps is still what the entry writes over `base`.
        net = net + (note.sustain - start->sustain);
    }

    // A step that moves NO ring — every keyed note held at its floor or pinned at its bound —
    // needs no clause here. The replay is a pure function of the step list, so it answers the plan
    // it answered last time, and dropping that press so the run banks no overshoot is the shared
    // gesture authority's (commitChartGestureStep, which compares the replay against the plan its
    // entry already holds). A chord member held while another member moves is not this case at all:
    // that step happened, and replaying the held member from its start is what brings it back in
    // shape with the others.

    // The label states the gesture's NET direction, because the entry it goes on describes the
    // whole gesture (start → now) rather than the step just pressed: grow, grow, shrink is a growth
    // of one step, and "Undo Shrink Sustain" over an entry that shortens the ring would lie. The
    // steps themselves have no sign to sum — a grid step's size is whatever reaches the next line —
    // but the entry's own change does, totalled over the selection so a member the bound or the
    // floor held still leaves the others to name it. A run that replays every ring back to `base`
    // needs no name at all: the finalize below refuses it as NoChange and the caller retires the
    // gesture's entry instead of labelling one that describes nothing.
    const std::string_view label = net.numerator > 0 ? "Grow Sustain" : "Shrink Sustain";
    return finalizePlan(chart, tempo_map, base, std::move(candidate), label);
}

// Claims a connection for every selected note the resolver justifies one for. Which note to connect
// FROM is not decided here: `chartResolutions` already established every note's same-string
// predecessor to answer its own resolutions, and hands the relation over — so the rule lives in one
// place and a whole-selection press costs no per-note backward scan.
//
// Deliberately unbounded in time: a hammer-on from a note eight bars back is musically odd, but a
// predecessor still holding is a predecessor, and the author asserting the connection is the
// authority on whether the notes connect. Refusing on distance would be second-guessing them.
ChartLegatoPlan planSetLegato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const std::string_view label)
{
    // Each refused note is recorded as the walk reaches it, with the reason that walk established:
    // the refusal flash glows those very notes, so naming them here is what keeps a consumer from
    // re-deriving a judgement this loop already made.
    std::vector<ChartLegatoRefusal> refused;
    if (keys.empty())
    {
        return ChartLegatoPlan{.plan = std::nullopt, .refused = {}};
    }

    // Connections of the ORIGINAL stream, in the SAVED form the gate validates: the original so
    // that claiming one note's connection cannot change what the next note is asked about, and the
    // saved form because a pick slide's latent mute can make an onset group read as all-muted
    // (choked, no span extension) in memory where the saved chart reads it as held — which would
    // have the verb deny a connection the gate, the sweep, and both surfaces all agree exists.
    // Connections rather than the whole resolutions: the hypothetical below reads stored fields
    // only, so the ink ends, the spans and the holds would all be derived and discarded.
    const common::core::ChartConnections connections =
        common::core::chartConnections(chart.notes, tempo_map);
    std::vector<common::core::ChartNote> candidate = chart.notes;
    bool changed = false;
    for (std::size_t index = 0; index < candidate.size(); ++index)
    {
        common::core::ChartNote& note = candidate[index];
        if (!std::ranges::binary_search(keys, chartSlotKeyOf(note)))
        {
            continue;
        }
        // Picking-hand riders (tap, pinch, scrape) skip in both directions: their onset is already
        // fully described, so a connection claim would say nothing about it.
        if (!common::core::legatoClaimable(note.attack))
        {
            refused.push_back(
                ChartLegatoRefusal{
                    .note = chartSlotKeyOf(note),
                    .reason = ChartLegatoSkip::PickingHandOnset,
                });
            continue;
        }
        const std::size_t predecessor_index = connections.predecessors[index];
        const common::core::ChartNote* const predecessor =
            predecessor_index == common::core::g_no_chart_predecessor
                ? nullptr
                : &connections.saved_notes[predecessor_index];
        // The hypothetical the press asks about: this note AS A CLAIM. The claim attack has to be
        // in place because the resolver answers a `LeftTap` locally — it reports the hammer motion
        // for a tap no predecessor could justify, and asking in that form would write a claim the
        // chart cannot keep. Everything else the resolver reads is the note's own stored data, so
        // no rule it applies is restated here: a fret-hand harmonic, for instance, skips itself,
        // because its node vetoes the pull clause and its open string leaves nothing to hammer on.
        common::core::ChartNote asked = connections.saved_notes[index];
        asked.attack = common::core::NoteAttack::Legato;
        common::core::LegatoMotion resolved =
            common::core::resolveLegato(asked, predecessor, tempo_map);
        // The D14 assist: when the HOLD is the only thing missing — the claim would resolve if the
        // predecessor were still ringing — the verb grows that ring to the successor's ONSET in
        // the same plan, so pressing L authors the connection instead of demanding the drag first
        // (the ring IS the held-ness datum; the verb writes it rather than requiring it). The
        // hypothetical is asked by handing the resolver a predecessor carrying that ring, which IS
        // the only-blocker test: an equal fret, a missing predecessor, or a fret-hand-harmonic
        // predecessor still refuses.
        //
        // No growth pre-check, and none is possible to disagree with: `note` is the next onset on
        // the predecessor's string by construction (that is what makes it the predecessor), so the
        // onset the assist grows to IS the predecessor's sustainBoundOf — exactly what a manual
        // drag could reach, and exactly what the finalize gate would clamp to.
        bool hold_was_the_only_blocker = false;
        if (resolved == common::core::LegatoMotion::Unjustified && predecessor != nullptr)
        {
            common::core::ChartNote still_ringing = *predecessor;
            still_ringing.sustain =
                common::core::beatDistance(tempo_map, predecessor->position, note.position);
            const common::core::LegatoMotion if_held =
                common::core::resolveLegato(asked, &still_ringing, tempo_map);
            hold_was_the_only_blocker = if_held != common::core::LegatoMotion::Unjustified;
            // A slide-out's tail is its authored exit window, not slack to spend: reshaping it to
            // buy a connection would rewrite the gesture. The connection itself stays legal — the
            // resolver reads the FRET AT THE RING'S END — it just has to be authored by dragging
            // that tail. (A scrape never reaches here: the resolver disqualifies it outright, so
            // its hold is never the only blocker.)
            if (hold_was_the_only_blocker &&
                common::core::slideOutFretOrNull(
                    *predecessor, connections.arrives_into[predecessor_index]) == nullptr)
            {
                static_cast<void>(common::core::clipPayloadsToSustain(
                    candidate[predecessor_index], still_ringing.sustain));
                resolved = if_held;
                changed = true;
            }
        }
        if (resolved == common::core::LegatoMotion::Unjustified)
        {
            refused.push_back(
                ChartLegatoRefusal{
                    .note = chartSlotKeyOf(note),
                    .reason = predecessor == nullptr      ? ChartLegatoSkip::NoPredecessor
                              : hold_was_the_only_blocker ? ChartLegatoSkip::PredecessorReleased
                                                          : ChartLegatoSkip::NoConnection,
                });
            continue;
        }
        // Which motion it resolved to is not recorded — that is the whole point of the model. A
        // note already carrying the claim is left alone, and counts as neither a change nor a skip.
        if (note.attack != common::core::NoteAttack::Legato)
        {
            note.attack = common::core::NoteAttack::Legato;
            changed = true;
        }
    }

    ChartLegatoPlan outcome{.plan = std::nullopt, .refused = std::move(refused)};
    if (changed)
    {
        // The refusal kind is deliberately not forwarded: an Invalid finalize leaves the plan
        // empty exactly like an all-skipped press, so the press falls through to its clear
        // meaning — the behavior this verb always had. The refusal channel, not the plan's
        // absence, is this planner's feedback payload.
        if (auto plan = finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), label);
            plan.has_value())
        {
            outcome.plan = std::move(*plan);
        }
    }
    return outcome;
}

std::optional<ChartEditPlan> planSettleChart(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const common::core::Chart& base, const std::string_view label)
{
    std::vector<common::core::ChartNote> settled = chart.notes;
    if (common::core::sweepUnjustifiedLegato(settled, tempo_map).empty())
    {
        // The ONE emptiness this planner reports: the sweep found nothing to flatten. Returning
        // the diff's emptiness instead would conflate that with a flatten that exactly cancelled
        // the burst it is diffed against, and the caller would leave its coalescing windows armed
        // over a claim the sweep had rejected.
        return std::nullopt;
    }
    // Deliberately not through finalizePlan: the sweep only ever turns a `Legato` into a `Pick`, so
    // order, the 40-Q2-B overlap bound, and every intra-note rule are exactly as the stream already
    // satisfied them — a plain pick demands nothing. Passing through the finalize would also diff
    // against the current stream rather than `base`, which is the one thing this planner needs to
    // control.
    //
    // The diff itself may come out EMPTY, and that is a real plan rather than a refusal: it means
    // the flatten put the stream back exactly where `base` had it, so the caller still has to
    // commit — walking the chart back to `base` is what removes the claim — and the entry it
    // replaces correctly describes nothing.
    return diffNotes(base.notes, settled, label);
}

ChartEditPlan writtenChartPlan(const ChartEditPlan& plan)
{
    // Both sides are slot-ordered subsequences of slot-ordered streams, which is all diffNotes
    // asks of its inputs, and stripping a point moves no slot.
    std::vector<common::core::ChartNote> removed = plan.removed;
    std::vector<common::core::ChartNote> inserted = plan.inserted;
    for (common::core::ChartNote& note : removed)
    {
        static_cast<void>(common::core::stripSilentKeyframes(note));
    }
    for (common::core::ChartNote& note : inserted)
    {
        static_cast<void>(common::core::stripSilentKeyframes(note));
    }
    return diffNotes(removed, inserted, plan.label);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetAttack(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const common::core::NoteAttack attack,
    const std::string_view label)
{
    // The strike flatten never rides THIS verb's eligibility: here the attack IS what the user
    // asked for, so a strike with nowhere to land (an open-string pinch re-handed to the fretting
    // hand) is a note to skip, not one to quietly retype as a pick.
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Skip,
        [&](const common::core::ChartNote& note, common::core::ChartNote& retyped) {
            if (note.attack == attack)
            {
                return false;
            }
            retyped.attack = attack;
            if (nodeLeavesWithAttack(note, attack))
            {
                retyped.harmonic_node.reset();
            }
            const bool was_scrape = common::core::isScrape(note.attack);
            if (was_scrape && !common::core::isScrape(attack))
            {
                // The path was gesture geometry; as a pitched glide or an ordinary slide-out it
                // would be a fiction. The overridden techniques were never touched, so they
                // simply resurface — including a bend or vibrato statement authored ON one of
                // the path's own keyframes, which is why the drop is per channel.
                common::core::dropNotePath(retyped);
            }
            // A pinch is picking while damping a node, so the verb authors one when none exists:
            // the octave at the stop — the lowest-order harmonic available at any fret and the
            // commonest squeal — matching the import default. An existing node keeps its
            // position; it names the same physical point under either picking-hand reading.
            //
            // Asked of the RETYPED note because that is the note the node will describe. The stop a
            // string speaks from is the note's own fret whichever hand made the onset, and this
            // verb moves no fret, so the number is the same either way.
            if (attack == common::core::NoteAttack::Pinch && !retyped.harmonic_node.has_value())
            {
                retyped.harmonic_node = static_cast<double>(common::core::physicalStopFret(
                                            retyped, chart.tuning.capo)) +
                                        12.0;
            }
            if (common::core::isScrape(attack))
            {
                // A scrape needs room to travel, so a ring too short to hold a gesture at all
                // grows first: the signed quarter-note default, clamped by the model's ONE bound
                // (40-Q2-B) so an authored default can never ring through the string's next
                // onset. A ring that can hold the gesture is left exactly as authored — the ring
                // is the note's own truth, and this verb changes the attack, not the duration.
                const common::core::Fraction window =
                    minimumSlideWindowRing(tempo_map, note.position);
                if (retyped.sustain < window)
                {
                    common::core::Fraction wanted = authoredRing(
                        tempo_map, note.position, g_pick_slide_default_sustain_whole_note);
                    const std::optional<common::core::Fraction> bound =
                        common::core::sustainBoundOf(chart.notes, note, tempo_map);
                    if (bound.has_value() && *bound < wanted)
                    {
                        wanted = *bound;
                    }
                    retyped.sustain = wanted > window ? wanted : window;
                }
                // An existing slide IS the gesture's path, so converting keeps the frets and the
                // direction the charter already drew; only a note with no slide at all takes the
                // synthesized default. The note's own fret is always the start (unlike imported
                // carriers, whose dead strings carry no meaningful fret). A converted path that
                // HOLDS a fret is no scrape — a pick cannot rest and still be scraping — and the
                // gate skips the note on exactly that rule (the normalizer's demotion), so no
                // travel test is restated here.
                if (!convertSlideToScrapePath(retyped))
                {
                    applyDefaultPickSlidePath(
                        retyped,
                        pickSlideDefaultUpward(retyped.fret, chart.tuning.capo),
                        chart.tuning.capo);
                }
            }
            return true;
        });
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetNoteFlag(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const ChartNoteFlag which, const bool value,
    const std::string_view label)
{
    // The write is one bool, and the rule authority is what refuses `dead` wherever a technique
    // needs the pitch it removes — a bend, a vibrato, a pinch's squeal — and what a palm mute
    // always passes. The eligibility asks the plan's own repair first (E4's strike flatten), as
    // every verb whose intent is not the attack does: a write that leaves a strike with nowhere to
    // land retypes to a plain pick and applies, rather than skipping the note for a reason the
    // user never asked about. The ring is not this verb's business at all — E25 is a presentation
    // rule, so X takes a dead note's DRAWN tail away and leaves its stored duration standing.
    bool common::core::ChartNote::* const field = chartNoteFlagField(which);
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Flatten,
        [field, value](const common::core::ChartNote&, common::core::ChartNote& written) {
            written.*field = value;
            return true;
        });
}

std::expected<ChartJunctionPlan, ChartPlanRefusal> planToggleJunctions(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& head_keys, const std::vector<ChartKeyframeKey>& keyframe_keys)
{
    // THE SPLIT PASS. Every selected keyframe becomes a head.
    std::vector<common::core::ChartNote> split;
    split.reserve(chart.notes.size() + keyframe_keys.size());
    std::vector<ChartSelectionKey> selection;
    bool split_any = false;
    for (const common::core::ChartNote& note : chart.notes)
    {
        const std::vector<common::core::Fraction> offsets =
            selectedOffsetsOn(keyframe_keys, chartSlotKeyOf(note));
        // The offsets that actually name one of this note's keyframes; a key naming none is a
        // selection the chart has moved past and is simply skipped, exactly as every other key
        // resolution here skips one.
        std::vector<common::core::Fraction> splits;
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            if (!std::ranges::binary_search(offsets, keyframe.offset))
            {
                continue;
            }
            if (!keyframe.fret.has_value())
            {
                // W10's ruling 2: a head must sit on a STATED fret, and the value between two
                // stating points is interpolated travel. The other half of that ruling — a
                // keyframe at the ring's end, with no remainder to hand over — is the walk's own
                // range refusal below and is not restated here.
                return std::unexpected{ChartPlanRefusal::Invalid};
            }
            // The instant is all this verb supplies: the keyframe the cut consumes states its own
            // fret, which is exactly the fret in force there and therefore the walk's own value,
            // and the new head's `Pick` attack is the walk's too.
            splits.push_back(keyframe.offset);
        }
        if (splits.empty())
        {
            split.push_back(note);
            continue;
        }
        split_any = true;
        const std::size_t first_product = split.size();
        const std::expected<void, ChartPlanRefusal> walked =
            splitNoteIntoProducts(tempo_map, note, splits, split);
        if (!walked.has_value())
        {
            return std::unexpected{walked.error()};
        }
        // Every product but the first lands on a NEW slot key, which is what the apply's own
        // follow rule calls the edit's own product. Stated here rather than left to that rule
        // because this press also inserts junction POINTS, and only this walk knows which record
        // is which.
        for (std::size_t index = first_product + 1; index < split.size(); ++index)
        {
            selection.emplace_back(ChartNoteKey{.slot = chartSlotKeyOf(split[index])});
        }
    }

    // The join pass needs chart order to find each head's predecessor in one forward walk, and the
    // split pass can break it: a product sits later in time than the note that followed its
    // origin, so a same-position neighbour on another string ends up out of place.
    std::ranges::sort(split, common::core::chartNoteOrderLess);

    // THE JOIN PASS. Every selected head becomes a point on its predecessor's path.
    //
    // The last note seen per string, indexed into the OUTPUT: a joined head is never pushed, so
    // what a later head on that string finds is the grown predecessor and not the record that just
    // dissolved into it.
    std::array<std::size_t, static_cast<std::size_t>(common::core::g_max_chart_strings) + 1>
        last_per_string{};
    last_per_string.fill(common::core::g_no_chart_predecessor);
    std::vector<common::core::ChartNote> joined;
    joined.reserve(split.size());
    bool join_any = false;
    for (const common::core::ChartNote& note : split)
    {
        const bool string_in_range =
            note.string >= 1 && note.string <= common::core::g_max_chart_strings;
        if (std::ranges::binary_search(head_keys, chartSlotKeyOf(note)))
        {
            const std::size_t predecessor =
                string_in_range ? last_per_string.at(static_cast<std::size_t>(note.string))
                                : common::core::g_no_chart_predecessor;
            if (predecessor == common::core::g_no_chart_predecessor)
            {
                // Nothing holds this string, so there is no path for the point to join.
                return std::unexpected{ChartPlanRefusal::Invalid};
            }
            const std::expected<common::core::Fraction, ChartPlanRefusal> at =
                joinHeadIntoPath(tempo_map, joined[predecessor], note);
            if (!at.has_value())
            {
                return std::unexpected{at.error()};
            }
            // The point takes the selection, so the next press splits it straight back — which is
            // the whole of what makes this verb a toggle.
            selection.emplace_back(
                ChartKeyframeKey{.note = chartSlotKeyOf(joined[predecessor]), .offset = *at});
            join_any = true;
            continue;
        }
        joined.push_back(note);
        if (string_in_range)
        {
            last_per_string.at(static_cast<std::size_t>(note.string)) = joined.size() - 1;
        }
    }

    if (!split_any && !join_any)
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // The label is the walk's answer, not the caller's: only this pass knows which halves a mixed
    // selection actually ran.
    const std::string_view label =
        split_any ? (join_any ? "Split and Join" : "Split Note") : "Join Notes";
    std::expected<ChartEditPlan, ChartPlanRefusal> plan =
        finalizePlan(chart, tempo_map, chart.notes, std::move(joined), label);
    if (!plan.has_value())
    {
        return std::unexpected{plan.error()};
    }
    return ChartJunctionPlan{.plan = std::move(*plan), .selection = std::move(selection)};
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetVibrato(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    const common::core::VibratoState set, const std::string_view label)
{
    return planNoteWrite(
        chart,
        tempo_map,
        notesTouchedBy(note_keys, keyframe_keys),
        label,
        StrandedStrikeRepair::Flatten,
        [&note_keys, &keyframe_keys, set](
            const common::core::ChartNote& note, common::core::ChartNote& written) {
            const ChartSlotKey slot = chartSlotKeyOf(note);
            // The onset statement, written only when the NOTE itself is selected: a note reached
            // solely because one of its keyframes is selected keeps the vibrato it opens with.
            if (std::ranges::binary_search(note_keys, slot))
            {
                written.vibrato = set;
            }
            // A key names an instant, a point standing there or not; setting plants one.
            for (const common::core::Fraction& offset : selectedOffsetsOn(keyframe_keys, slot))
            {
                if (common::core::hasVibrato(set))
                {
                    common::core::keyframeAt(written, offset).vibrato = set;
                }
                else
                {
                    common::core::endVibratoAt(written, offset);
                }
            }
            return true;
        });
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetBend(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    const std::optional<double> semitones, const std::string_view label)
{
    return planNoteWrite(
        chart,
        tempo_map,
        notesTouchedBy(note_keys, keyframe_keys),
        label,
        StrandedStrikeRepair::Flatten,
        [&note_keys, &keyframe_keys, semitones](
            const common::core::ChartNote& note, common::core::ChartNote& written) {
            const ChartSlotKey slot = chartSlotKeyOf(note);
            // The onset statement, written only when the NOTE itself is named — a note reached
            // solely through one of its instants keeps the pre-bend it opens with. An onset always
            // states its bend, so taking the statement away there leaves it at rest.
            if (std::ranges::binary_search(note_keys, slot))
            {
                written.bend = semitones.value_or(0.0);
            }
            const std::vector<common::core::Fraction> offsets =
                selectedOffsetsOn(keyframe_keys, slot);
            for (const common::core::Fraction& offset : offsets)
            {
                if (semitones.has_value())
                {
                    common::core::keyframeAt(written, offset).bend = semitones;
                }
                else if (common::core::standingKeyframe(written, offset) != nullptr)
                {
                    common::core::keyframeAt(written, offset).bend.reset();
                }
            }
            // Taking the bend away from a point that stated nothing else takes the point, as
            // `Delete` takes one whose fret was all it said: a point left saying nothing is not a
            // statement the charter asked to keep. Only the named points are judged, each against
            // the note WITHOUT it, the commit law's own terms; a planted point is the amount
            // path's, and keeps its linger.
            if (!semitones.has_value())
            {
                const common::core::ChartNote cleared = written;
                std::erase_if(
                    written.keyframes, [&offsets, &cleared](const common::core::Keyframe& point) {
                        return std::ranges::binary_search(offsets, point.offset) &&
                               common::core::keyframeSaysNothingNew(
                                   common::core::noteWithoutKeyframe(cleared, point.offset), point);
                    });
            }
            return true;
        });
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetEmphasis(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const common::core::NoteEmphasis value,
    const std::string_view label)
{
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Flatten,
        [value](const common::core::ChartNote&, common::core::ChartNote& struck) {
            struck.emphasis = value;
            return true;
        });
}

namespace
{

// The note the harmonic verb would WRITE at one candidate node — the ONE spelling of that write,
// so the candidate probe below and the plan itself can never disagree about what a chosen node
// produces. THE TYPED FRET IS A TOUCH ON THE OPEN STRING, which is the whole of `H`'s law: the fret
// is zeroed and the node then measured from the stop an unpressed string speaks from — the nut, or
// the capo — so the number the charter typed names a position the finger touches rather than one it
// presses. On a tap that authors an OPEN-STRING tapped harmonic, the tapping finger on a node of
// the whole string. A harmonic touched above a PRESSED stop is a different statement — one hand
// holds a fret while the other touches the node, which the note states as a positive `fret` beside
// its node — and the verb that authors one is not this one. The normalizer then strips what a touch
// cannot carry — a bend, vibrato, the travel of a finger that presses nothing — so the note takes
// the harmonic instead of being skipped for a payload it never needed. Safe here in a way it would
// not be for a pinch: no repair can undo an on-neck node, so the normalizer can only take payloads,
// never the harmonic. A planted finger the note was holding stays where the charter put it, as a
// latent the saved form strips exactly as an attack change's latents are stripped, so clearing the
// harmonic presses the fret back down with that finger under it again.
[[nodiscard]] common::core::ChartNote harmonicTouchNote(
    const common::core::ChartNote& note, const double position,
    const common::core::ChartTuning& tuning)
{
    common::core::ChartNote touched = note;
    touched.fret = 0;
    const int stop = common::core::physicalStopFret(touched, tuning.capo);
    // Fret positions are logarithmic, so the stop and the offset simply add.
    touched.harmonic_node = static_cast<double>(stop) + position;
    static_cast<void>(common::core::normalizeChartNote(touched, tuning));
    return touched;
}

// The fret the harmonic verb reads as the note's LABEL: the fret the charter typed, or — on a note
// already touching an on-neck node from the OPEN string, whose fret is zero because nothing is
// pressed — the fret that node lies at, the same number the clear presses back down. One spelling
// for both, so the rows a carrier is offered and the fret its clear restores can never name
// different places. A PRESSED-STOP harmonic — a positive fret beside a node, artificial or tapped —
// takes the plain reading of its own fret, which is the verb's law applied without an exception:
// that number names the ladder of the OPEN string, so `H` offers it the nodes a touch there would
// reach, and the clear leaves the note pressed exactly where it already was.
[[nodiscard]] int harmonicLabelFret(const common::core::ChartNote& note)
{
    // Bound to a local so the presence test and the read are provably one object.
    const std::optional<double>& node = note.harmonic_node;
    if (note.fret == 0 && node.has_value() && carriesNeckHarmonic(note))
    {
        return static_cast<int>(std::lround(*node));
    }
    return note.fret;
}

// The label the note states, in fret units above the stop an UNPRESSED string speaks from — the
// nut, or the capo — because the touch this verb authors stands on the open string. Zero for an
// open string, which names no touch at all and is why such a note has no candidates.
[[nodiscard]] double harmonicLabelOf(const common::core::ChartNote& note, const int capo)
{
    common::core::ChartNote unpressed = note;
    unpressed.fret = 0;
    return static_cast<double>(
        harmonicLabelFret(note) - common::core::physicalStopFret(unpressed, capo));
}

} // namespace

std::vector<common::core::HarmonicNodeCandidate> chartHarmonicNodeCandidates(
    const common::core::ChartNote& note, const common::core::ChartTuning& tuning,
    const common::core::TempoMap& tempo_map)
{
    // A pinch's node is the picking thumb's, and ChartTechnique::PinchHarmonic owns it. The one
    // eligibility stated by hand, because it is the only one about which HAND the number belongs
    // to; everything below is the rule authority's answer.
    if (!common::core::nodeIsOnNeck(note.attack))
    {
        return {};
    }
    std::vector<common::core::HarmonicNodeCandidate> candidates =
        common::core::harmonicNodeCandidates(
            harmonicLabelOf(note, tuning.capo), common::core::g_max_harmonic_partial);
    // REACHABILITY IS THE RULE AUTHORITY'S ANSWER, never a bound restated here: a node past the
    // neck, at or behind the stop, or on a note whose saved form records no node at all (a scrape)
    // is dropped because the write it would produce is one the chart rules refuse — the same
    // judgement planNoteWrite makes per note, asked one candidate earlier so the picker never
    // offers a row the settle would skip. Only the last of those can fire today; the
    // header states why the two positional bounds are unreachable from a label, and asking the
    // authority is what keeps this tracking them if they move.
    std::erase_if(
        candidates,
        [&note, &tuning, &tempo_map](const common::core::HarmonicNodeCandidate& candidate) {
            const common::core::ChartNote written =
                common::core::savedChartNote(harmonicTouchNote(note, candidate.position, tuning));
            return !written.harmonic_node.has_value() ||
                   !common::core::validateChartNoteAlone(written, tuning, tempo_map).has_value();
        });
    // LOWEST PARTIAL FIRST: the order the picker lists and the row a choiceless press takes. Not
    // the nearest node, which import takes — under a bound of 16 the 13th partial's 2.892 sits
    // nearer a typed 3 than the 6th's 3.156 does, and a charter typing 3 means the 6th.
    std::ranges::sort(
        candidates,
        [](const common::core::HarmonicNodeCandidate& lhs,
           const common::core::HarmonicNodeCandidate& rhs) { return lhs.partial < rhs.partial; });
    return candidates;
}

std::expected<ChartEditPlan, ChartPlanRefusal> planSetHarmonic(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const std::optional<int> chosen_partial,
    const std::string_view label)
{
    // The strike flatten never rides this verb's eligibility, for planSetAttack's reason: the touch
    // IS what the user asked for, and the write always leaves a node for a strike to land on, so
    // this verb strands nothing and must not quietly retype an attack it did not touch.
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Skip,
        [&chart, &tempo_map, chosen_partial](
            const common::core::ChartNote& note, common::core::ChartNote& touched) {
            const std::vector<common::core::HarmonicNodeCandidate> candidates =
                chartHarmonicNodeCandidates(note, chart.tuning, tempo_map);
            if (candidates.empty())
            {
                // The typed fret names no node this note can reach — an open string, whose zero
                // offset names no touch at all, and a pinch, whose node is the other hand's.
                // Skipped, never repaired: moving the finger to the nearest node would author a
                // position the charter never typed.
                return false;
            }
            // THE CHOICE BINDS ONLY WHAT IT NAMES. A chosen partial takes the node of that partial
            // on every note whose label offers it; a note whose label does not, and a press that
            // stated no choice, take the operand's FIRST row — the lowest partial, the one the
            // picker preselects — so the keyboard's default and the menu's first row agree.
            const auto named = std::ranges::find_if(
                candidates, [chosen_partial](const common::core::HarmonicNodeCandidate& candidate) {
                    return chosen_partial.has_value() && candidate.partial == *chosen_partial;
                });
            const common::core::HarmonicNodeCandidate& chosen =
                named != candidates.end() ? *named : candidates.front();
            touched = harmonicTouchNote(note, chosen.position, chart.tuning);
            return true;
        });
}

std::expected<ChartEditPlan, ChartPlanRefusal> planClearHarmonic(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const std::string_view label)
{
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Flatten,
        [](const common::core::ChartNote& note, common::core::ChartNote& cleared) {
            // The fretting hand's node only: a pinch's is the picking thumb's, and the row that
            // owns that hand clears it (planClearPinchHarmonic). A shared clear once reached both,
            // which let this verb's "No harmonic" strip a pinch selected beside a
            // fret-hand carrier.
            if (!carriesNeckHarmonic(note))
            {
                return false;
            }
            // The finger presses where it was touching — the label the verb reads off a carrier,
            // which is what makes the clear the exact inverse of the set. On a PRESSED-STOP
            // harmonic that label is the note's own fret: the touch goes and a plain note is left
            // stopped exactly where it already was.
            cleared.fret = harmonicLabelFret(note);
            cleared.harmonic_node.reset();
            return true;
        });
}

std::expected<ChartEditPlan, ChartPlanRefusal> planClearPinchHarmonic(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& keys, const std::string_view label)
{
    return planNoteWrite(
        chart,
        tempo_map,
        keys,
        label,
        StrandedStrikeRepair::Flatten,
        [](const common::core::ChartNote& note, common::core::ChartNote& cleared) {
            if (note.attack != common::core::NoteAttack::Pinch || !note.harmonic_node.has_value())
            {
                return false;
            }
            // The note goes back to the plain pick it was picked as, and the thumb's node goes with
            // it; the fret is untouched, because a pinch's fret was pressed all along.
            cleared.attack = common::core::NoteAttack::Pick;
            cleared.harmonic_node.reset();
            return true;
        });
}

bool carriesNeckHarmonic(const common::core::ChartNote& note) noexcept
{
    return note.harmonic_node.has_value() && common::core::nodeIsOnNeck(note.attack);
}

namespace
{

// The uniform-scope read for a technique that lives on the NOTE alone: every selected note already
// carries it. An empty note operand answers false, which makes such a press mean SET — and a set
// with nothing to write plans to NoChange, which is the inert outcome an empty selection has always
// had. Written once so six of the seven row shapes below state only their own field.
template <typename Carries>
[[nodiscard]] bool everySelectedNoteCarries(
    const common::core::Chart& chart, const ChartSelection& selection, const Carries& carries)
{
    const std::vector<common::core::ChartNote> selected =
        notesForKeys(chart.notes, selection.notes());
    return !selected.empty() && std::ranges::all_of(selected, carries);
}

// The WIDTH the vibrato channel is in force at a keyframe key's instant — a point's OWN
// statement included, which is what `ringStateAt` reads and what makes "which tier does this point
// carry" the same question at a point as at an onset. Returns the width rather than a flag so each
// tier's verb compares against its own value: a wide point must read as NOT carrying the ordinary
// tier, or `V` over it would clear instead of replacing.
//
// A key naming an instant with no point — `V` on a covered slot, or a point an earlier press
// dissolved — reads the state the ring actually holds there, the leg that instant lies in, so the
// press flips THAT leg's width from the instant on (`planSetVibrato` plants the point). A dissolved
// point's own return is the verb window's exact reversal, the second press of the pair.
[[nodiscard]] common::core::VibratoState vibratoAtKey(
    const common::core::Chart& chart, const ChartKeyframeKey& key)
{
    const common::core::ChartNote* const note = chartNoteAt(chart.notes, key.note);
    return note != nullptr ? common::core::ringStateAt(*note, key.offset).vibrato
                           : common::core::VibratoState::None;
}

// One tier's whole row of the technique law. The two vibrato verbs differ ONLY in the width they
// name, so stating the row once and handing it that width is what keeps "toggle my tier, replace
// the other one" a single rule rather than two copies free to disagree: `carried` asks whether
// every anchor already stands at THIS width — a scope at the other tier answers no, which makes
// the press an ordinary set that replaces it in one entry — and `plan` writes this width or clears
// to `None`. A mixed selection follows the same convention every other row does: anything short of
// "all of them already" means set, so the press levels the whole scope onto this tier.
template <common::core::VibratoState Tier>
[[nodiscard]] ChartTechniqueLaw vibratoTierLaw(const std::string_view noun)
{
    return ChartTechniqueLaw{
        .noun = noun,
        // The one row family with two scopes, because vibrato is the one technique here that is a
        // fact about a LEG: a note carries the tier when its first leg is at it, and a keyframe
        // key when the leg its instant lies in is at it. Both are read for the same uniform-scope
        // answer, so a press over a mixed operand clears only when every anchor in it already
        // stands at this tier.
        .carried =
            [](const common::core::Chart& chart, const ChartSelection& selection) {
                // Asked of the RESOLVED anchors, like every other row's everySelectedNoteCarries: a
                // key naming a note the chart no longer holds is not an anchor that carries
                // anything, and reading the KEYS instead would make this row answer "already
                // carries it" where the flag rows answer "set it" for the same selection.
                const std::vector<common::core::ChartNote> notes =
                    notesForKeys(chart.notes, selection.notes());
                if (notes.empty() && selection.keyframes().empty())
                {
                    return false;
                }
                return std::ranges::all_of(
                           notes,
                           [](const common::core::ChartNote& note) {
                               return note.vibrato == Tier;
                           }) &&
                       std::ranges::all_of(
                           selection.keyframes(), [&chart](const ChartKeyframeKey& key) {
                               return vibratoAtKey(chart, key) == Tier;
                           });
            },
        .plan =
            [](const common::core::Chart& chart,
               const common::core::TempoMap& tempo_map,
               const ChartSelection& selection,
               const bool set,
               const std::string_view label) {
                return planSetVibrato(
                    chart,
                    tempo_map,
                    selection.notes(),
                    selection.keyframes(),
                    set ? Tier : common::core::VibratoState::None,
                    label);
            },
    };
}

// One attack's whole row of the technique law. The four attack verbs differ ONLY in the value they
// name, so stating the row once and handing it that value is what keeps "toggle my attack, replace
// whatever else was there" a single rule rather than four copies free to disagree — the vibrato
// pair's argument one level up, on a field with four claimants instead of two. `carried` asks
// whether every selected note already stands at THIS attack, so a scope at another one answers no
// and the press is an ordinary set that replaces it in one entry; `plan` writes this attack or
// clears back to the plain pick.
//
// Every compatibility consequence a conversion owes — the node a re-handed strike strands, the
// scrape's path and terminal when the note stops scraping, the ring a scrape needs to travel, the
// pinch's authored node — is planSetAttack's in BOTH directions, and the per-note gate behind it is
// the one rule authority. So a row here inherits all of it and states none of it: a tap with
// nothing to strike is skipped by that gate rather than by a guard written again in this file.
template <common::core::NoteAttack Attack>
[[nodiscard]] ChartTechniqueLaw attackLaw(const std::string_view noun)
{
    return ChartTechniqueLaw{
        .noun = noun,
        .carried =
            [](const common::core::Chart& chart, const ChartSelection& selection) {
                return everySelectedNoteCarries(
                    chart, selection, [](const common::core::ChartNote& note) {
                        return note.attack == Attack;
                    });
            },
        .plan =
            [](const common::core::Chart& chart,
               const common::core::TempoMap& tempo_map,
               const ChartSelection& selection,
               const bool set,
               const std::string_view label) {
                return planSetAttack(
                    chart,
                    tempo_map,
                    selection.notes(),
                    set ? Attack : common::core::NoteAttack::Pick,
                    label);
            },
    };
}

} // namespace

ChartTechniqueLaw chartTechniqueLaw(const ChartTechnique technique)
{
    // Each row binds a noun, the "already carries it" test, and the planner. The flag rows ask
    // the one flag-to-field mapping; the emphasis rows compare against the axis's value; the two
    // vibrato rows are one shared row shape handed their own width, and the four attack rows are
    // another handed their own attack value, the plain pick being what each of them clears to; the
    // pinch row sets through the attack verb and clears the thumb's node with it.
    // Every row but the vibrato pair reads `selection.notes()` alone, which is the empty-operand
    // rule doing the work a per-kind guard would otherwise do.
    switch (technique)
    {
        case ChartTechnique::PalmMute:
        {
            return ChartTechniqueLaw{
                .noun = "Palm Mute",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.palm_mute;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart,
                            tempo_map,
                            selection.notes(),
                            ChartNoteFlag::PalmMute,
                            set,
                            label);
                    },
            };
        }
        case ChartTechnique::Dead:
        {
            return ChartTechniqueLaw{
                .noun = "Dead Note",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.dead;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart, tempo_map, selection.notes(), ChartNoteFlag::Dead, set, label);
                    },
            };
        }
        case ChartTechnique::Tremolo:
        {
            return ChartTechniqueLaw{
                .noun = "Tremolo",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.tremolo;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart,
                            tempo_map,
                            selection.notes(),
                            ChartNoteFlag::Tremolo,
                            set,
                            label);
                    },
            };
        }
        case ChartTechnique::Vibrato:
        {
            return vibratoTierLaw<common::core::VibratoState::Narrow>("Vibrato");
        }
        case ChartTechnique::WideVibrato:
        {
            return vibratoTierLaw<common::core::VibratoState::Wide>("Wide Vibrato");
        }
        case ChartTechnique::Accent:
        {
            return ChartTechniqueLaw{
                .noun = "Accent",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.emphasis == common::core::NoteEmphasis::Accent;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return planSetEmphasis(
                            chart,
                            tempo_map,
                            selection.notes(),
                            set ? common::core::NoteEmphasis::Accent
                                : common::core::NoteEmphasis::Normal,
                            label);
                    },
            };
        }
        case ChartTechnique::Ghost:
        {
            return ChartTechniqueLaw{
                .noun = "Ghost Note",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.emphasis == common::core::NoteEmphasis::Ghost;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return planSetEmphasis(
                            chart,
                            tempo_map,
                            selection.notes(),
                            set ? common::core::NoteEmphasis::Ghost
                                : common::core::NoteEmphasis::Normal,
                            label);
                    },
            };
        }
        case ChartTechnique::PickSlide:
        {
            return attackLaw<common::core::NoteAttack::PickSlide>("Pick Slide");
        }
        case ChartTechnique::Tap:
        {
            // "Right-Hand Tap" rather than "Tap" because the undo history and the discovery menu
            // show it beside "Left-Hand Tap": the plates already name these two by hand rather
            // than by letter, so the words do too.
            return attackLaw<common::core::NoteAttack::Tap>("Right-Hand Tap");
        }
        case ChartTechnique::Slap:
        {
            return attackLaw<common::core::NoteAttack::Slap>("Slap");
        }
        case ChartTechnique::Pop:
        {
            return attackLaw<common::core::NoteAttack::Pop>("Pop");
        }
        case ChartTechnique::PinchHarmonic:
        {
            // NOT attackLaw<Pinch>, and the difference is the clear: the row's noun is a HARMONIC,
            // so its clear must remove one, where clearing to the plain pick alone would leave a
            // stop and a node standing — an artificial harmonic nobody authored. The SET is the
            // attack verb unchanged, which already re-asks a node whose owning hand flips and
            // authors the octave at the stop when none exists.
            return ChartTechniqueLaw{
                .noun = "Pinch Harmonic",
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.attack == common::core::NoteAttack::Pinch;
                            });
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return set ? planSetAttack(
                                         chart,
                                         tempo_map,
                                         selection.notes(),
                                         common::core::NoteAttack::Pinch,
                                         label)
                                   : planClearPinchHarmonic(
                                         chart, tempo_map, selection.notes(), label);
                    },
            };
        }
        case ChartTechnique::Legato:
        {
            break;
        }
    }
    // Legato's plan is planSetLegato, which decides set-or-clear itself; reaching here is a caller
    // bug, and inventing a row would be a verb that looks like it works.
    std::unreachable();
}

namespace
{

// Applies the plan to a copy of the note stream, or refuses: every removal must still match by
// full value and every insertion must land on a free slot.
[[nodiscard]] std::expected<std::vector<common::core::ChartNote>, EditorUndoFailureCode>
withPlanApplied(const std::vector<common::core::ChartNote>& current, const ChartEditPlan& plan)
{
    std::vector<common::core::ChartNote> notes = current;
    for (const common::core::ChartNote& note : plan.removed)
    {
        const auto found = std::ranges::lower_bound(
            notes, chartSlotKeyOf(note), {}, [](const common::core::ChartNote& held) {
                return chartSlotKeyOf(held);
            });
        if (found == notes.end() || !(*found == note))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        notes.erase(found);
    }
    for (const common::core::ChartNote& note : plan.inserted)
    {
        const auto insert_at = std::ranges::lower_bound(
            notes, chartSlotKeyOf(note), {}, [](const common::core::ChartNote& held) {
                return chartSlotKeyOf(held);
            });
        if (insert_at != notes.end() && chartSlotKeyOf(*insert_at) == chartSlotKeyOf(note))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        notes.insert(insert_at, note);
    }
    return notes;
}

} // namespace

std::expected<void, EditorUndoFailureCode> applyChartChange(
    common::core::Chart& chart, const ChartEditPlan& plan)
{
    // Rebuilt on a copy before it is swapped in, so a failed precondition partway through cannot
    // leave half the plan applied.
    auto notes = withPlanApplied(chart.notes, plan);
    if (!notes.has_value())
    {
        return std::unexpected{notes.error()};
    }
    chart.notes = std::move(*notes);
    return {};
}

namespace
{

// Both undo and redo replay the plan against the session's mutable chart, so the chart revision
// bumps and every projection rebuilds exactly like a fresh edit.
[[nodiscard]] std::expected<void, EditorUndoFailureCode> applyToSessionChart(
    EditorEditContext& context, const ChartEditPlan& plan)
{
    const std::optional<std::expected<void, EditorUndoFailureCode>> applied =
        context.session.writeChart(
            [&plan](common::core::Chart& chart) { return applyChartChange(chart, plan); });
    if (!applied.has_value())
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return *applied;
}

} // namespace

std::expected<void, EditorUndoFailureCode> ChartEdit::undo(EditorEditContext& context) const
{
    return applyToSessionChart(context, plan.reversed());
}

std::expected<void, EditorUndoFailureCode> ChartEdit::redo(EditorEditContext& context) const
{
    return applyToSessionChart(context, plan);
}

std::string ChartEdit::label() const
{
    return plan.label;
}

EditFocus ChartEdit::focus(const EditorUndoDirection direction) const
{
    const std::optional<ChartEditFocus>& side =
        direction == EditorUndoDirection::Undo ? before : after;
    if (!side.has_value())
    {
        return {};
    }
    return *side;
}

} // namespace rock_hero::editor::core
