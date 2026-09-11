#include "chart/chart_edits.h"

#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// Slides every note by the delta in place, or answers false and leaves them half-moved for the
// caller to discard. Refused, never clamped: a move that would leave the neck or the grid is
// invalid. The grid arithmetic itself clamps at the origin, so leaving the grid shows up as a move
// that fell short of the delta asked for.
[[nodiscard]] bool moveKeyedNotes(
    const common::core::TempoMap& tempo_map, std::vector<common::core::ChartNote>& notes,
    const common::core::Fraction beat_delta, const int string_delta, const int string_count)
{
    for (common::core::ChartNote& note : notes)
    {
        const common::core::GridPosition from = note.position;
        note.position = common::core::advanceGridPosition(tempo_map, from, beat_delta);
        note.string += string_delta;
        if (note.string < 1 || note.string > string_count ||
            common::core::beatDistance(tempo_map, from, note.position) != beat_delta)
        {
            return false;
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
// overlap normalization and the one in-plan repair, gates the result through the whole technique
// matrix, and diffs against `base`. The gate is what makes authoring an invalid chart impossible by
// construction — a plan whose candidate the document reader would reject refuses here, for every
// present and future verb, with no per-verb guard to forget. It validates the SAVED form, because a
// scrape's latent overrides are legal in memory and stripped by the writer.
// The two emptinesses are distinct on purpose: the gate's refusal is Invalid, an empty diff is
// NoChange — conflating them is what made every refusal in the editor silent.
//
// `base` is the stream the plan is expressed against, which is `chart.notes` for every verb that
// edits from what it finds. The duration gesture is the reason the base is a parameter rather than
// read off `chart`: its plan must describe the whole gesture, so it is diffed against the stream
// the gesture started from while the ring RULES still judge the live chart. The move gesture,
// equally a gesture, needs no such split — it judges nothing against the live chart, so its caller
// simply hands it the pre-gesture chart and `base` is that chart's own notes.
//
// Silently-held stops need no arm of their own here: they are notes, so the slot-uniqueness rule
// the gate already runs covers them, with no disjointness test between two arrays to write.
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> finalizePlan(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base,
    std::vector<common::core::ChartNote> candidate, std::string_view label)
{
    std::ranges::sort(candidate, common::core::chartNoteOrderLess);
    // The two rules a note cannot obey alone, normalized exactly as a loaded chart is: a re-strike
    // stops the ring, and no keyframe sits on a head of its own string. So a note inserted into a
    // ring truncates it, a release the truncation carried onto the new head rides back to its
    // clearance, and a keyframe stepped onto the next head lands at the clearance instead — while
    // a keyframe placed INSIDE the margin, short of the head, is the charter's deliberate act and
    // stands. The repaired indices are the load path's business (it names what it changed); a
    // producer that only needs the invariant ignores them, which is why neither rule is
    // [[nodiscard]].
    common::core::normalizeSustainOverlaps(candidate, tempo_map);
    common::core::normalizeKeyframeClearances(candidate, tempo_map);
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
    // The keyframe's offset, or absent for the note's own stop on the entry's channel.
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

} // namespace

std::expected<ChartEditPlan, ChartPlanRefusal> planInsertNote(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    common::core::ChartNote note, const common::core::Fraction default_sustain)
{
    // Every note rings, so a placement authors a duration whether the user thought about one or
    // not, and the session's grid step is what they were looking at when they placed it. The
    // finalize gate's same-string normalization does the clamping: a step that would ring through
    // the next onset on the string ends exactly on it.
    note.sustain = default_sustain;
    std::vector<common::core::ChartNote> candidate = chart.notes;
    // Placing on an occupied slot replaces the note there. Still reachable: undo and redo never
    // move the caret, so undoing a delete can put a note back under an armed caret with an empty
    // selection — the next typed digit inserts onto that occupied slot and must replace, not
    // collide.
    std::erase_if(candidate, [&note](const common::core::ChartNote& existing) {
        return chartSlotKeyOf(existing) == chartSlotKeyOf(note);
    });
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
        int stated_fret = note.fret;
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            if (offset < keyframe.offset)
            {
                break;
            }
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = keyframe.fret;
            if (fret.has_value())
            {
                stated_fret = *fret;
            }
        }
        return ChartPathTail{
            .note = chartSlotKeyOf(note), .offset = offset, .stated_fret = stated_fret
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
    // authority's refusal and not one this planner restates.
    const auto at = std::ranges::upper_bound(
        target->keyframes, offset, {}, [](const common::core::Keyframe& keyframe) {
            return keyframe.offset;
        });
    target->keyframes.insert(at, common::core::Keyframe{.offset = offset, .fret = fret});

    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), "Insert Keyframe");
}

std::expected<ChartEditPlan, ChartPlanRefusal> planToggleSilentHold(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& slots, const common::core::Fraction default_sustain)
{
    if (slots.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // Every sorted-by-slot sequence below is searched through this one projection.
    const auto slot_of = [](const common::core::ChartNote& note) { return chartSlotKeyOf(note); };
    const std::vector<common::core::ChartNote> named = notesForKeys(chart.notes, slots);
    // WHO STATES EACH STOP (DERIVED HELD), resolved against the LIVE chart because the relation
    // lives BETWEEN notes — a snapshot of the addressed notes says nothing about their neighbours.
    // Both readings come off one walk, exactly as planRetypeFrets takes them: the RESOLVED claim is
    // what this verb asks instead of the stored field, like every other consumer, and the
    // DERIVATION beside it answers who states it.
    const common::core::ChartConnections connections =
        common::core::chartConnections(chart.notes, tempo_map);
    const std::vector<std::optional<int>> derived_stops =
        common::core::chartDerivedStops(connections);
    const std::vector<std::optional<int>> claimed_stops =
        common::core::chartClaimedStops(connections);
    // Where an addressed note sits in the live chart, which is what both vectors are parallel to.
    const auto live_index =
        [&chart, &slot_of](const common::core::ChartNote& note) -> std::optional<std::size_t> {
        const ChartSlotKey slot = slot_of(note);
        const auto found = std::ranges::lower_bound(chart.notes, slot, {}, slot_of);
        if (found == chart.notes.end() || slot_of(*found) != slot)
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - chart.notes.begin());
    };
    // THE DERIVATION OWNS IT, so the verb REFUSES rather than acting — the same refusal
    // planRetypeFrets makes on the held channel, and it binds in BOTH directions: where a pull-off
    // states the stop there is no field to write and none to clear, and the only way to withdraw
    // the statement is to unwrite the pull-off, which is not this verb's act. Whole-scope like
    // every other refusal here, so one owned stop rejects the press rather than leaving a chord
    // half toggled — and the press SAYS so, where reading the raw field left the entry diffing
    // empty and the refusal silent.
    const auto derivation_owns_it = [&derived_stops,
                                     &live_index](const common::core::ChartNote& note) {
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<std::size_t> index = live_index(note);
        return index.has_value() && derived_stops[*index].has_value();
    };
    if (std::ranges::any_of(named, derivation_owns_it))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    // Whether a note ALREADY states the fretting hand's stop, in whichever shape its onset allows:
    // a silent hold IS that statement, and a right-hand onset carries it as a held fret because its
    // own fret belongs to the other hand. One predicate, so the direction test and the per-slot
    // work cannot disagree about what "already holding" means — and it reads the RESOLVED claim,
    // never \ref ChartNote::held, which is the law every consumer of a claimed stop is under.
    const auto states_a_stop = [&claimed_stops, &live_index](const common::core::ChartNote& note) {
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<std::size_t> index = live_index(note);
        return index.has_value() && claimed_stops[*index].has_value();
    };
    // The toggle direction, asked of the scope as a whole exactly as the technique verbs ask it:
    // only a scope whose every occupied slot ALREADY holds a stop means "stop holding". An empty
    // slot names no note here, so it never argues for the releasing direction — there is nothing at
    // it to release.
    const bool release_them = !named.empty() && std::ranges::all_of(named, states_a_stop);
    // Which words are true of that direction, which is a question about the CONTENT rather than
    // about the direction: releasing a silent hold gives a note back its sound, while releasing a
    // held stop leaves the onset that carried it sounding exactly as before. Asked as two counts of
    // the same predicate rather than one, because a MIXED scope is a third answer and not the
    // absence of the second.
    const auto is_silent = [](const common::core::ChartNote& note) {
        return common::core::silentHold(note.attack);
    };
    const bool all_silent = std::ranges::all_of(named, is_silent);
    const bool any_silent = std::ranges::any_of(named, is_silent);

    std::vector<common::core::ChartNote> candidate = chart.notes;
    for (common::core::ChartNote& toggled : candidate)
    {
        if (!std::ranges::binary_search(slots, chartSlotKeyOf(toggled)))
        {
            continue;
        }
        if (release_them)
        {
            if (common::core::silentHold(toggled.attack))
            {
                // Back to a sounding note. The ring is the caller's session step, like any
                // placement's, because a hold stored none to restore; what the conversion stripped
                // comes back through the reversal window and undo, which carry the whole note,
                // never by being reinvented.
                toggled.attack = common::core::NoteAttack::Pick;
                toggled.sustain = default_sustain;
            }
            else
            {
                // A right-hand onset stops holding: only the statement goes. The note itself is
                // untouched, because its onset was never the fretting hand's to convert.
                toggled.held.reset();
            }
        }
        else if (common::core::rightHandOnset(toggled.attack))
        {
            // The FOURTH case. The verb's meaning is the same — state the fretting hand's stop at
            // this slot — and only WHERE that statement can live differs: this onset belongs to the
            // picking hand, so converting the note would delete a sound the charter wrote, while
            // the stop under it is exactly what the held fret is for. Open string, like the
            // empty-slot case below and for the same reason: the editor's one fret-stating flow is
            // typing a digit, and the caller arms the caret on this stop so the charter states it
            // next. A slot already stating one is left alone — asked of the RESOLVED claim like the
            // direction above, so the seed can never write a second spelling of a stop the chart
            // already states; the whole-scope direction is what decides between stating and
            // releasing.
            if (!states_a_stop(toggled))
            {
                toggled.held = 0;
            }
        }
        else
        {
            // Converting a sounding note: the attack changes, and the fixpoint the saved form
            // already defines takes everything the new attack cannot state with it — the ring, the
            // mutes, the node, the payload. Written through savedChartNote rather than by clearing
            // fields here, so this verb and the rule that judges its result can never disagree
            // about what a silent hold may carry, and a technique added to ChartNote later needs no
            // line in this function. A slot already holding a stop is left exactly as it is, which
            // is what savedChartNote answers for it too.
            toggled.attack = common::core::NoteAttack::None;
            toggled = common::core::savedChartNote(toggled);
        }
    }
    // The scope's EMPTY slots, which only the caret's fallback can name: each gains a hold at the
    // open string, exactly as the neutral-create placement plants a note at fret 0 — the editor's
    // one fret-stating flow is typing a digit at the armed caret, and the caller arms it here, so
    // the charter states the stop next. Appended in whatever order the scope lists them, because
    // the shared finalize is the one authority on the stream's order.
    for (const ChartSlotKey& slot : slots)
    {
        if (release_them || std::ranges::binary_search(named, slot, {}, slot_of))
        {
            continue;
        }
        candidate.push_back(
            common::core::ChartNote{
                .position = slot.position,
                .string = slot.string,
                .fret = 0,
                .sustain = {},
                .attack = common::core::NoteAttack::None,
                .palm_mute = false,
                .dead = false,
                .harmonic_node = {},
                .vibrato = common::core::VibratoState::Off,
                .tremolo = false,
                .emphasis = common::core::NoteEmphasis::Normal,
                .bend = 0.0,
                .keyframes = {},
            });
    }

    std::expected<ChartEditPlan, ChartPlanRefusal> plan = finalizePlan(
        chart,
        tempo_map,
        chart.notes,
        std::move(candidate),
        // Four labels for two directions, because the undo entry has to say what it did: releasing
        // a silent hold gives the note its sound back, releasing a held stop leaves the onset that
        // carried it sounding exactly as before.
        //
        // The fourth is the MIXED releasing scope, and it is a plural rather than a fourth verb
        // because both kinds ARE held-stop releases — a silent hold is a held stop the fretting
        // hand wrote as a note of its own. "Sound Note" would lie about the onsets it leaves
        // untouched and "Release Held Stop" would lie about the notes it sounds, so the honest word
        // is the one true of every slot in the press.
        !release_them ? "Hold Stop"
        : all_silent  ? "Sound Note"
        : any_silent  ? "Release Held Stops"
                      : "Release Held Stop");
    if (!plan.has_value())
    {
        return plan;
    }
    // Whole-plan atomicity, asked as the ONE question the settle can answer for either shape of
    // claim: does every slot this press stated at still state a stop? The finalize's settle takes
    // a claimed stop that reaches no shape, and WHAT it takes differs by shape — the whole note
    // where the note IS the claim, the field alone where a sounding onset carries it, which leaves
    // that note byte-identical to what it was and therefore invisible to any diff of removed
    // against inserted. Such a press states nothing at that slot, so it is refused whole rather
    // than applied in part — which is also what lets a chord convert together and a lone member
    // refuse, without this function knowing that spans exist. Bound once so the checked value and
    // the reads are provably one object.
    const ChartEditPlan& settled = *plan;
    // Asked of the RECORD a note keeps, not of the resolution above, because what the settle TAKES
    // is a record: "did this press's statement survive it" is a question about records, and the
    // notes this reads are the plan's own written ones, which the live chart's resolution knows
    // nothing about. The two readings cannot part on an addressed slot in any case — the refusal
    // above already rejected every onset whose stop the derivation owns.
    const auto keeps_a_record = [](const common::core::ChartNote& note) {
        return common::core::claimedStop(note).has_value();
    };
    // What the settled plan leaves at a slot: the note it writes there, the note already there
    // where it writes none, and nothing where it took the note outright. Both halves of a diff are
    // in slot order, like every other stream here.
    const auto states_a_stop_after =
        [&settled, &chart, &slot_of, &keeps_a_record](const ChartSlotKey& slot) {
            const auto written = std::ranges::lower_bound(settled.inserted, slot, {}, slot_of);
            if (written != settled.inserted.end() && slot_of(*written) == slot)
            {
                return keeps_a_record(*written);
            }
            if (std::ranges::binary_search(settled.removed, slot, {}, slot_of))
            {
                return false;
            }
            const auto standing = std::ranges::lower_bound(chart.notes, slot, {}, slot_of);
            return standing != chart.notes.end() && slot_of(*standing) == slot &&
                   keeps_a_record(*standing);
        };
    // Only in the stating direction: releasing asks for exactly the absence this refuses.
    if (!release_them && !std::ranges::all_of(slots, states_a_stop_after))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    return plan;
}

std::expected<ChartEditPlan, ChartPlanRefusal> planDeleteSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys)
{
    KeyedSplit notes = splitByKeys(chart.notes, note_keys);
    const std::size_t deleted_notes = notes.keyed.size();
    // Keyframes go from the notes that SURVIVE: one whose note this call deletes needs no removal
    // of its own, and the strip runs over `rest` for exactly that reason. Delete takes every
    // statement a keyframe makes, so the keyframe always empties and always goes — which is the
    // strip authority's own removal rule rather than a second one written here.
    std::size_t deleted_keyframes = 0;
    for (common::core::ChartNote& note : notes.rest)
    {
        const std::vector<common::core::Fraction> offsets =
            selectedOffsetsOn(keyframe_keys, chartSlotKeyOf(note));
        if (offsets.empty())
        {
            continue;
        }
        const std::size_t before = note.keyframes.size();
        static_cast<void>(common::core::stripKeyframeChannels(
            note.keyframes, [&offsets](common::core::Keyframe& keyframe) {
                if (!std::ranges::binary_search(offsets, keyframe.offset))
                {
                    return false;
                }
                keyframe.fret.reset();
                keyframe.bend.reset();
                keyframe.vibrato.reset();
                return true;
            }));
        deleted_keyframes += before - note.keyframes.size();
    }
    if (deleted_notes == 0 && deleted_keyframes == 0)
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
    std::string label;
    if (deleted_keyframes == 0)
    {
        label = "Delete " + count_label(deleted_notes, "Note", "Notes");
    }
    else if (deleted_notes == 0)
    {
        label = "Delete " + count_label(deleted_keyframes, "Keyframe", "Keyframes");
    }
    else
    {
        label = "Delete Selection";
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
    // Any selected object's onset answers the meter question — the step is uniform over the whole
    // selection either way — so the front of whichever kind is present serves. A keyframe's meter
    // is its note's, since the offset it steps is measured from there.
    const common::core::GridPosition origin =
        !note_keys.empty() ? note_keys.front().position : keyframe_keys.front().note.position;
    for (const ChartMoveStep& step : steps)
    {
        switch (step.direction)
        {
            case ChartStepDirection::Left:
            case ChartStepDirection::Right:
            {
                // The meter this press was taken under is the one where the run has REACHED, not
                // the one it started in, which is the whole reason the presses are kept in order.
                const common::core::GridPosition reference =
                    common::core::advanceGridPosition(tempo_map, origin, delta.beats);
                const common::core::Fraction beats =
                    gridStepBeats(tempo_map, step.note_value, reference.measure);
                delta.beats = step.direction == ChartStepDirection::Right ? delta.beats + beats
                                                                          : delta.beats - beats;
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

std::expected<ChartEditPlan, ChartPlanRefusal> planMoveSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartKeyframeKey>& keyframe_keys,
    common::core::Fraction beat_delta, int string_delta, std::string_view label)
{
    if ((note_keys.empty() && keyframe_keys.empty()) ||
        (beat_delta.numerator == 0 && string_delta == 0))
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
    // The RELEASE is the ring's end, so stepping it steps the end with it: the fall's length is
    // the point's to change, and this is the verb that changes it — outward for a longer fall,
    // inward for a shorter one, never onto or across the last sounded fret (the order refusal
    // above), and past the string's next onset only as far as the same-string clamp lets a ring
    // reach, where the release parks. Read before any offset moves, because the release is
    // recognised by sitting exactly at the end.
    bool stepped_keyframe = false;
    if (beat_delta.numerator != 0)
    {
        for (common::core::ChartNote& note : notes.rest)
        {
            const std::vector<common::core::Fraction> offsets =
                selectedOffsetsOn(keyframe_keys, chartSlotKeyOf(note));
            if (offsets.empty())
            {
                continue;
            }
            const common::core::Keyframe* const release = common::core::releaseKeyframe(note);
            for (common::core::Keyframe& keyframe : note.keyframes)
            {
                if (std::ranges::binary_search(offsets, keyframe.offset))
                {
                    keyframe.offset = keyframe.offset + beat_delta;
                    if (&keyframe == release)
                    {
                        note.sustain = keyframe.offset;
                    }
                    stepped_keyframe = true;
                }
            }
            // A point stepped onto the ring's end is the release now, and a release states its
            // fret and nothing else.
            static_cast<void>(common::core::stripReleaseChannels(note));
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
    if (!moveKeyedNotes(tempo_map, notes.keyed, beat_delta, string_delta, string_count))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

    // Converging moves that stack two notes on one slot are refused, as is landing on a slot an
    // unmoved note occupies. One test covers silently-held stops too, because they are notes on
    // the same slot space — the collision the two-array model had to state as disjointness.
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
    const std::vector<ChartKeyframeKey>& keyframe_keys, const ChartFretWrite write,
    common::core::ChartStopChannel channel)
{
    if (base.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // Null for a shift, which names no value of its own.
    const ChartFretSet* const set = std::get_if<ChartFretSet>(&write);

    // EVERY stop this plan addresses, collected once so the anchor and the write can never read
    // different fields. The two key lists say WHICH: a note's own stop on `channel` where the
    // selection named the note, a keyframe's fret where it named the keyframe. A note in the
    // snapshot that neither list names is written through and not addressed — the shape a mixed
    // selection takes, and the fret-verb law's other half (a head's digit never moves its path).
    std::vector<AddressedStop> addressed;
    addressed.reserve(base.size() + keyframe_keys.size());
    if (channel == common::core::ChartStopChannel::Held)
    {
        // ONE walk of the LIVE chart answers both questions the held channel asks, because both
        // are facts about a note's NEIGHBOURS that the snapshot — one loose string of notes —
        // states nothing about: WHO states each stop, and WHAT the channel addresses.
        //
        // The held stop it addresses is THE COMPLETE ONE (\ref common::core::chartHeldStops): the
        // channel exists on a note exactly where the satellite that states it is drawn, and a bare
        // tap wears one carrying its DEFAULT — the grip the covering span holds. So typing there
        // AUTHORS a real held stop, where a gate on the stored field would let the digit fall
        // through and change nothing. The walk runs only on this channel: it is a whole-chart pass
        // on a per-keystroke path, and the sounding channel asks the chart nothing.
        const common::core::ChartResolutions resolutions =
            common::core::chartResolutions(chart.notes, tempo_map);
        // Where an addressed note sits in the live chart, which is what both vectors are parallel
        // to.
        const auto live_index =
            [&chart](const common::core::ChartNote& note) -> std::optional<std::size_t> {
            const ChartSlotKey slot = chartSlotKeyOf(note);
            const auto found = std::ranges::lower_bound(
                chart.notes, slot, {}, [](const common::core::ChartNote& stored) {
                    return chartSlotKeyOf(stored);
                });
            if (found == chart.notes.end() || chartSlotKeyOf(*found) != slot)
            {
                return std::nullopt;
            }
            return static_cast<std::size_t>(found - chart.notes.begin());
        };
        for (std::size_t base_index = 0; base_index < base.size(); ++base_index)
        {
            const common::core::ChartNote& note = base[base_index];
            if (!std::ranges::binary_search(note_keys, chartSlotKeyOf(note)))
            {
                continue;
            }
            // Bound to a local so the presence test and every read are provably one object.
            const std::optional<std::size_t> index = live_index(note);
            if (!index.has_value())
            {
                continue;
            }
            // THE DERIVATION OWNS IT, so the held channel is REFUSED there rather than quietly
            // skipped: the charter typed at a stop the notation already states, and the pending box
            // has to say the value cannot land (DERIVED HELD). A DEFAULT is owned by nobody and is
            // deliberately NOT refused — it is exactly the satellite this verb is for. Whole-plan,
            // like every other refusal here: one member the derivation owns rejects the entry
            // rather than leaving a chord half retyped, so the first one found ends it.
            //
            // UNLESS THE DIGIT AGREES WITH IT (SAME-FRET SETTLE). Typing the value the satellite
            // already shows is not an authoring attempt the derivation has to fend off — it asks
            // for the state the chart is already in, so it settles as the no-op it is: nothing
            // authored, nothing refused, no undo entry. The note contributes NOTHING to the plan
            // rather than a write of the same value, because a write here would author the field
            // the derivation's own residue sweep exists to clear. Only an EXACT entry can agree: a
            // shift names a delta rather than a value, and so says nothing about this one.
            //
            // Asked of the WIDE planted table (\ref common::core::chartPlantedStops): a right-hand
            // entry there IS the derived claim, and a fretting-hand entry is the PLANT the note
            // wears as its own satellite on the reveal's terms (THE PLANT'S FACE) — the notation
            // owns both, so typing at either is refused alike, and a fretting-hand note can never
            // be handed a held field its attack forbids. A plant never sits on a note the channel
            // does not reach: the resolver refuses a silent hold as a pull-off source, so every
            // planted note sounds and carries a held stop. Bound to a local so the presence test
            // and the read are provably one object.
            if (const std::optional<int>& planted = resolutions.planted_stops[*index];
                planted.has_value())
            {
                if (set == nullptr || *planted != set->fret)
                {
                    return std::unexpected{ChartPlanRefusal::Invalid};
                }
                continue;
            }
            // Bound to a local so the presence test and the read are provably one object.
            if (const std::optional<int>& held = resolutions.held_stops[*index]; held.has_value())
            {
                addressed.push_back(
                    AddressedStop{.base_index = base_index, .keyframe_offset = {}, .value = *held});
            }
        }
    }
    else
    {
        for (std::size_t base_index = 0; base_index < base.size(); ++base_index)
        {
            const common::core::ChartNote& note = base[base_index];
            if (std::ranges::binary_search(note_keys, chartSlotKeyOf(note)))
            {
                // A FRET-HAND HARMONIC HAS NO STOP TO RETYPE. Its finger stands on the node and
                // presses nothing, so the fret this channel addresses is not a value the charter
                // can restate — landing the digit would author `fret 5 + node 4.98`, a stop and a
                // touch naming two different places, which no rule catches because 4.98 is not
                // beyond nothing. Refused whole rather than skipped, the shape of the
                // derived-held refusal above and for its reason: the pending box has to say the
                // value cannot land instead of leaving it looking typed. Restating a node is
                // press `H`, type, press `H`.
                if (common::core::fretHandHarmonic(note))
                {
                    return std::unexpected{ChartPlanRefusal::Invalid};
                }
                addressed.push_back(
                    AddressedStop{
                        .base_index = base_index, .keyframe_offset = {}, .value = note.fret
                    });
            }
        }
    }
    // The keyframe half, collected on EITHER channel: a keyframe has one position channel and wears
    // no satellite, so nothing about it asks which stop of a note the digit meant — the selection
    // kind already said. A keyframe stating no fret states nothing about position, so it offers no
    // stop to transpose and takes none: authoring a fret there would state a channel the charter
    // never pointed at.
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
            if (!fret.has_value() || !std::ranges::binary_search(offsets, keyframe.offset))
            {
                continue;
            }
            addressed.push_back(
                AddressedStop{
                    .base_index = base_index, .keyframe_offset = keyframe.offset, .value = *fret
                });
        }
    }

    // One delta over every addressed stop — a silently-held member and a selected keyframe alike,
    // which is what makes a transposed chord carry its held frets and a chord slide's points move
    // together. The shift NAMES that delta; nothing here anchors it.
    const int delta = set != nullptr ? 0 : std::get<ChartFretShift>(write).delta;
    const std::string label =
        set != nullptr
            ? (channel == common::core::ChartStopChannel::Held ? "Set Held Stop " : "Set Fret ") +
                  std::to_string(set->fret)
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
            if (channel == common::core::ChartStopChannel::Held)
            {
                retyped.held = value;
            }
            else
            {
                // A NODE TRAVELS WITH ITS STOP. A node is `stop + offset` and fret positions are
                // logarithmic, so the offset the harmonic names survives a move only if the node
                // moves by the same amount; leaving it behind authors a touch that is no node of
                // the string as newly stopped. The fret-hand form never reaches here — it was
                // refused above — so what this moves is the artificial family (a real stop under a
                // node), a tap harmonic's own landing point, and a pinch's graze. Whether the
                // moved node is still legal is the finalize gate's answer, like every other bound
                // this planner leaves to it.
                //
                // Bound to a local so the presence test and the write are provably one object.
                std::optional<double>& node = retyped.harmonic_node;
                if (node.has_value())
                {
                    node = *node + static_cast<double>(value - stop.value);
                }
                retyped.fret = value;
            }
            continue;
        }
        for (common::core::Keyframe& keyframe : retyped.keyframes)
        {
            if (keyframe.offset == *at)
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

std::expected<ChartEditPlan, ChartPlanRefusal> planClearHeldStops(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& slots)
{
    if (slots.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    // THE ONE OWNERSHIP AUTHORITY, the same table planRetypeFrets asks: a stop the notation states
    // — a tap's derived held stop, a fretting-hand source's PLANT — is not the charter's to
    // withdraw, so the press is refused whole rather than clearing what it may around it. A
    // DEFAULT is owned by nobody and carried by no field, so clearing it is the no-op the finalize
    // reports. Resolved against the live stream the candidate copies, so the two are
    // index-parallel.
    const common::core::ChartResolutions resolutions =
        common::core::chartResolutions(chart.notes, tempo_map);
    std::vector<common::core::ChartNote> candidate = chart.notes;
    for (const ChartSlotKey& slot : slots)
    {
        const auto found =
            std::ranges::lower_bound(candidate, slot, {}, [](const common::core::ChartNote& note) {
                return chartSlotKeyOf(note);
            });
        if (found == candidate.end() || chartSlotKeyOf(*found) != slot)
        {
            continue; // An empty slot carries nothing to withdraw.
        }
        const auto index = static_cast<std::size_t>(found - candidate.begin());
        if (resolutions.planted_stops[index].has_value())
        {
            return std::unexpected{ChartPlanRefusal::Invalid};
        }
        found->held.reset();
    }
    // The hold verb's own word for this act, so the undo entry reads the same whichever key made
    // it: nothing gains or loses a sound.
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), "Release Held Stop");
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
        // on a release drags the end with it), is the verb for going further. The floor is
        // INCLUSIVE where landing on it makes the point the RELEASE: pulling the end exactly onto
        // the last stated fret of a ring that simply ends is how a glide becomes an unpitched
        // slide-out. On a ring already released the floor IS the release and stays exclusive —
        // the ribbon cannot pass its own end point, and the fall's length is the point's to
        // change. The onset itself is never a legal end, so the empty ring's floor stays
        // exclusive too. A scrape's path is DERIVED and re-terminates onto whatever tail it has,
        // so it floors at the minimum gesture window instead, clamped — always positive, so it
        // never reaches the hold below.
        common::core::Fraction floor{};
        bool floor_ends_the_ring = false;
        if (common::core::isScrape(stepped.attack))
        {
            if (target < common::core::g_minimum_slide_window)
            {
                target = common::core::g_minimum_slide_window;
            }
        }
        else if (!stepped.keyframes.empty())
        {
            const common::core::Keyframe& last = stepped.keyframes.back();
            floor = last.offset;
            floor_ends_the_ring =
                common::core::releaseKeyframe(stepped) == nullptr && last.fret.has_value();
        }
        if (floor < target || (floor_ends_the_ring && floor == target))
        {
            // The one bound on a ring (40-Q2-B): a tail may reach exact adjacency with the next
            // onset on its OWN string and no further, because a re-strike stops the ring. The
            // margin that binds growth against ANY string is the DRAWN tail's spacing rule, which
            // presentation owns rather than this clamp. Clamping the replayed value needs no
            // direction test and no memory of the previous step — a note pinned at its bound
            // reports the bound for every step past it, and leaves it the moment the replayed ring
            // falls back inside. The clamp can never SHORTEN a note below where the gesture found
            // it: normalizeSustainOverlaps holds every stored ring inside this same bound, so
            // `start` is already at most the bound.
            if (const std::optional<common::core::Fraction> bound =
                    common::core::sustainBoundOf(chart.notes, note, tempo_map);
                bound.has_value() && *bound < target)
            {
                target = *bound;
            }
            // The one way a ring changes length once it carries a payload: a pitched note's
            // keyframes all lie above its floor, so nothing clips there — the resize leaves a
            // release behind a lengthening ring as the pitched stop it has become, and re-aims a
            // scrape's compressed path.
            common::core::clipPayloadsToSustain(stepped, target);
            note = std::move(stepped);
        }
        // A held note counts too: the ring it keeps is still what the entry writes over `base`.
        net = net + (note.sustain - start->sustain);
    }

    // A running gesture's step that moves NO ring — every keyed note held at its floor or pinned
    // at its bound — is refused rather than recorded, exactly as the move verb refuses a step it
    // cannot apply. Recording it would bank an overshoot the charter cannot see and would have to
    // pay back, click by click, before the next visible step. A chord member held while another
    // member moves is not this case: that step happened, and replaying the held member from its
    // start is what brings it back in shape with the others. A FIRST step that moves nothing is
    // the ordinary no-op the finalize below answers, which arms no gesture at all.
    if (candidate == chart.notes && chart.notes != base)
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

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
    // Counted by reason so the caller can say WHY an all-skipped press did nothing; index 0 (None)
    // stays zero and makes the dominant-reason scan below a plain maximum. Sized off the enum, so
    // a new reason is a compile-time widening rather than a throw out of a keystroke handler.
    std::array<int, static_cast<std::size_t>(ChartLegatoSkip::Count)> skips{};
    if (keys.empty())
    {
        return ChartLegatoPlan{.plan = std::nullopt, .skipped = 0, .reason = ChartLegatoSkip::None};
    }

    // Connections of the ORIGINAL stream, in the SAVED form the gate validates: the original so
    // that claiming one note's connection cannot change what the next note is asked about, and the
    // saved form because a pick slide's latent mute can make an onset group read as all-muted
    // (choked, no span extension) in memory where the saved chart reads it as held — which would
    // have the verb deny a connection the gate, the sweep, and both surfaces all agree exists.
    // Connections rather than the whole resolutions: the hypothetical below reads stored fields
    // only, so the presented stream, the spans and the holds would all be derived and discarded.
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
            ++skips.at(static_cast<std::size_t>(ChartLegatoSkip::PickingHandOnset));
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
        // the same plan, so pressing H authors the connection instead of demanding the drag first
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
            // A trail-off's tail is its authored exit window, not slack to spend: reshaping it to
            // buy a connection would rewrite the gesture. The connection itself stays legal — the
            // resolver reads the RELEASED fret — it just has to be authored by dragging that tail.
            // (A scrape never reaches here: the resolver disqualifies it outright, so its hold is
            // never the only blocker.)
            if (hold_was_the_only_blocker &&
                common::core::slideOutFretOrNull(*predecessor) == nullptr)
            {
                common::core::clipPayloadsToSustain(
                    candidate[predecessor_index], still_ringing.sustain);
                resolved = if_held;
                changed = true;
            }
        }
        if (resolved == common::core::LegatoMotion::Unjustified)
        {
            ++skips.at(
                static_cast<std::size_t>(
                    predecessor == nullptr      ? ChartLegatoSkip::NoPredecessor
                    : hold_was_the_only_blocker ? ChartLegatoSkip::PredecessorReleased
                                                : ChartLegatoSkip::NoConnection));
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

    ChartLegatoPlan outcome{.plan = std::nullopt, .skipped = 0, .reason = ChartLegatoSkip::None};
    for (std::size_t reason = 1; reason < skips.size(); ++reason)
    {
        outcome.skipped += skips.at(reason);
        if (skips.at(reason) > skips.at(static_cast<std::size_t>(outcome.reason)))
        {
            outcome.reason = static_cast<ChartLegatoSkip>(reason);
        }
    }
    if (changed)
    {
        // The refusal kind is deliberately not forwarded: an Invalid finalize leaves the plan
        // empty exactly like an all-skipped press, so the press falls through to its clear
        // meaning — the behavior this verb always had. The skip channel, not the plan's absence,
        // is this planner's feedback payload.
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
                // The path was gesture geometry; as a pitched glide or an ordinary trail-off it
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
            // Asked of the RETYPED note, which is the one the node will describe: the stop a string
            // speaks from depends on the attack (a right-hand onset holds its stop beside its own
            // fret), so asking the note as it stood would measure a pinch's node from the stop the
            // tap it just stopped being was holding.
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
                if (retyped.sustain < common::core::g_minimum_slide_window)
                {
                    const common::core::TimeSignatureChange signature =
                        tempo_map.timeSignatureAt(note.position.measure);
                    common::core::Fraction wanted =
                        pickSlideDefaultSustainBeats(signature.denominator);
                    const std::optional<common::core::Fraction> bound =
                        common::core::sustainBoundOf(chart.notes, note, tempo_map);
                    if (bound.has_value() && *bound < wanted)
                    {
                        wanted = *bound;
                    }
                    retyped.sustain = wanted > common::core::g_minimum_slide_window
                                          ? wanted
                                          : common::core::g_minimum_slide_window;
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

std::expected<ChartEditPlan, ChartPlanRefusal> planDisconnectKeyframes(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartKeyframeKey>& keyframe_keys, const std::string_view label)
{
    std::vector<common::core::ChartNote> candidate;
    candidate.reserve(chart.notes.size() + keyframe_keys.size());
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
            if (!keyframe.fret.has_value() || !(keyframe.offset < note.sustain))
            {
                // A head must sit on a stated fret, and it needs a remainder to take.
                return std::unexpected{ChartPlanRefusal::Invalid};
            }
            splits.push_back(keyframe.offset);
        }
        if (splits.empty())
        {
            candidate.push_back(note);
            continue;
        }
        split_any = true;
        // The glide-into-a-landing margin, read at the note's own measure exactly as the
        // presentation trim reads it (`trimToMargin`), so the stored arrival lands where the drawn
        // tail would have been trimmed to anyway.
        const common::core::Fraction margin = common::core::minimumSustainDistanceBeats(
            tempo_map.timeSignatureAt(note.position.measure).denominator);
        // Each product spans one segment of the original ring: [start, end), with the segment's
        // own arrival keyframe carried on its END so the leg the user split at survives intact.
        // Walking the whole ring as segments rather than special-casing "origin plus remainder"
        // is what makes two selected junctions on one note three notes without a second rule.
        splits.push_back(note.sustain);
        common::core::Fraction start{0, 1};
        for (const common::core::Fraction& end : splits)
        {
            common::core::ChartNote product = note;
            product.keyframes.clear();
            if (std::is_neq(start <=> common::core::Fraction{0, 1}))
            {
                const common::core::RingState carried = common::core::ringStateAt(note, start);
                product.position =
                    common::core::advanceGridPosition(tempo_map, note.position, start);
                product.fret = carried.fret;
                product.bend = carried.bend;
                product.vibrato = carried.vibrato;
                // W10's signed store for a split head. Its motion is the resolver's to derive,
                // and today an equal-fret junction resolves to Unjustified — see the header: the
                // unstruck-tie default the addendum PROPOSES needs LegatoMotion::Continuation,
                // which is unbuilt, so the settle sweep flattens this claim to a plain pick.
                product.attack = common::core::NoteAttack::Legato;
            }
            // The ring runs to where the string is next struck, which after a split is the next
            // product's onset: a re-strike is what stops a ring, and the drawn tail is the
            // presentation rules' business, not this plan's.
            product.sustain = end - start;
            // True when a new head takes over at this product's end — every product but the last.
            const bool re_picked = std::is_neq(end <=> note.sustain);
            for (const common::core::Keyframe& keyframe : note.keyframes)
            {
                if (!(start < keyframe.offset) || end < keyframe.offset)
                {
                    continue;
                }
                common::core::Keyframe rebased = keyframe;
                rebased.offset = keyframe.offset - start;
                if (re_picked && !(keyframe.offset < end))
                {
                    // The arrival of a glide into a RE-PICKED head lands the margin before it —
                    // the format's own shift-slide shape (`ChartNote::keyframes`, the importer's
                    // policy rule 13), and the clearance a repaired statement takes
                    // (`keyframeClearanceOf`). Left ON the head it would be the product's release
                    // by position, and the gate's clearance repair would then shorten the ring
                    // under it into a slide-out; retreated, the arrival ends the gesture's
                    // INFORMATION a margin early while the ring below still runs to the head. A
                    // retreat landing on or before the statement before it is the ordering
                    // refusal's.
                    rebased.offset = rebased.offset - margin;
                }
                product.keyframes.push_back(rebased);
            }
            // The release is the keyframe at the ring's END, so it reaches only the product that
            // ends where the gesture did: every earlier product ends at a split point, which is a
            // sounded fret and never the release.
            candidate.push_back(std::move(product));
            start = end;
        }
    }
    if (!split_any)
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    return finalizePlan(chart, tempo_map, chart.notes, std::move(candidate), label);
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
            // solely because one of its keyframes is selected keeps the shake it opens with.
            if (std::ranges::binary_search(note_keys, slot))
            {
                written.vibrato = set;
            }
            const std::vector<common::core::Fraction> offsets =
                selectedOffsetsOn(keyframe_keys, slot);
            for (common::core::Keyframe& keyframe : written.keyframes)
            {
                if (std::ranges::binary_search(offsets, keyframe.offset))
                {
                    keyframe.vibrato = set;
                }
            }
            // The dissolve law's static half, run over the statements this press wrote: one that
            // restates the state already in force where it stands changes neither the path nor the
            // state, so it is no statement at all. Dropping it through the strip authority is what
            // dissolves a keyframe whose only job was the technique just cleared — the point
            // lingers as a selection key (which is what a second press inside the verb window
            // reverses through) while the chart, which may never hold a keyframe stating nothing,
            // simply does not have it.
            common::core::VibratoState shaking = written.vibrato;
            static_cast<void>(common::core::stripKeyframeChannels(
                written.keyframes, [&shaking, &offsets](common::core::Keyframe& keyframe) {
                    const std::optional<common::core::VibratoState>& stated = keyframe.vibrato;
                    if (!stated.has_value())
                    {
                        return false;
                    }
                    const bool redundant = *stated == shaking;
                    shaking = *stated;
                    if (!redundant || !std::ranges::binary_search(offsets, keyframe.offset))
                    {
                        return false;
                    }
                    keyframe.vibrato.reset();
                    return true;
                }));
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
// produces. The fret is zeroed BEFORE the stop is asked, because the number being resolved is a
// touch and not a press: a note holding nothing must read its stop from the capo rather than from
// the fret the charter just typed. The normalizer then strips what a touch cannot carry — a bend,
// a shake, the travel of a finger that presses nothing — so the note takes the harmonic instead of
// being skipped for a payload it never needed. Safe here in a way it would not be for a pinch: no
// repair can undo an on-neck node, so the normalizer can only take payloads, never the harmonic.
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

// The label the note's own fret states, in fret units above the stop the string SPEAKS from. Zero
// for an open string, which names no touch at all and is why such a note has no candidates.
[[nodiscard]] double harmonicLabelOf(const common::core::ChartNote& note, const int capo)
{
    common::core::ChartNote unpressed = note;
    unpressed.fret = 0;
    return static_cast<double>(note.fret - common::core::physicalStopFret(unpressed, capo));
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
            harmonicLabelOf(note, tuning.capo), common::core::g_max_snapped_partial);
    // REACHABILITY IS THE RULE AUTHORITY'S ANSWER, never a bound restated here: a node past the
    // neck, at or behind the stop, or on a note whose saved form records no node at all (a scrape,
    // a silently-held stop) is dropped because the write it would produce is one the chart rules
    // refuse — the same judgement planNoteWrite makes per note, asked one candidate earlier so the
    // picker never offers a row the settle would skip. Only the last of those can fire today; the
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
                // The typed fret names no node this note can reach — frets 1, 11 and 13, an open
                // string (whose zero offset names no touch at all), and a pinch, whose node is the
                // other hand's. Skipped, never repaired: moving the finger to the nearest node
                // would author a position the charter never typed.
                return false;
            }
            // THE CHOICE BINDS ONLY WHAT IT NAMES. A note whose label reaches one node has that
            // node whatever partial was chosen, and a press that stated no choice means the
            // nearest — which is the same answer for a single candidate, so the two rules agree
            // everywhere except at the one ambiguous label the picker exists for.
            const auto named = std::ranges::find_if(
                candidates, [chosen_partial](const common::core::HarmonicNodeCandidate& candidate) {
                    return chosen_partial.has_value() && candidate.partial == *chosen_partial;
                });
            const std::size_t chosen =
                named != candidates.end()
                    ? static_cast<std::size_t>(std::ranges::distance(candidates.begin(), named))
                    : common::core::nearestHarmonicNode(
                          candidates, harmonicLabelOf(note, chart.tuning.capo));
            touched = harmonicTouchNote(note, candidates[chosen].position, chart.tuning);
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
            // Bound to a local so the presence test and the read are provably one object.
            const std::optional<double>& node = note.harmonic_node;
            if (!node.has_value())
            {
                return false;
            }
            if (note.attack == common::core::NoteAttack::Pinch)
            {
                cleared.attack = common::core::NoteAttack::Pick;
            }
            // The finger presses where it was touching. Guarded on the node being ON THE NECK so a
            // pinch's bridge-side graze is never pressed as a fret: only a fretting finger was
            // ever standing somewhere a fret number can name.
            if (note.fret == 0 && common::core::nodeIsOnNeck(note.attack))
            {
                cleared.fret = static_cast<int>(std::lround(*node));
            }
            cleared.harmonic_node.reset();
            return true;
        });
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

// The WIDTH the vibrato channel is in force at where one selected keyframe stands — its OWN
// statement included, which is what `ringStateAt` reads and what makes "which tier does this point
// carry" the same question at a point as at an onset. Returns the width rather than a flag so each
// tier's verb compares against its own value: a wide point must read as NOT carrying the ordinary
// tier, or `V` over it would clear instead of replacing.
//
// A key naming no note, or naming a keyframe an earlier press dissolved, reads the state the ring
// actually holds there — after a clearing press, not shaking — so the next press means SET. What
// that press can then do is bounded by `planSetVibrato`, which states the channel on keyframes the
// chart HOLDS and never authors one: a key whose point dissolved therefore plans to NoChange. The
// dissolved point returns through the verb window's exact reversal (the second press of the pair),
// which is what the lingering key exists for; once that window closes the key is inert until the
// selection next changes. Restating a dissolved point is authoring a keyframe at an offset, which
// is the `B` verb's business and not this one's.
[[nodiscard]] common::core::VibratoState selectedKeyframeVibrato(
    const common::core::Chart& chart, const ChartKeyframeKey& key)
{
    const auto found = std::ranges::lower_bound(
        chart.notes, key.note, {}, [](const common::core::ChartNote& note) {
            return chartSlotKeyOf(note);
        });
    if (found == chart.notes.end() || !(chartSlotKeyOf(*found) == key.note))
    {
        return common::core::VibratoState::Off;
    }
    return common::core::ringStateAt(*found, key.offset).vibrato;
}

// One tier's whole row of the technique law. The two vibrato verbs differ ONLY in the width they
// name, so stating the row once and handing it that width is what keeps "toggle my tier, replace
// the other one" a single rule rather than two copies free to disagree: `carried` asks whether
// every anchor already stands at THIS width — a scope at the other tier answers no, which makes
// the press an ordinary set that replaces it in one entry — and `plan` writes this width or clears
// to `Off`. A mixed selection follows the same convention every other row does: anything short of
// "all of them already" means set, so the press levels the whole scope onto this tier.
template <common::core::VibratoState Tier>
[[nodiscard]] ChartTechniqueLaw vibratoTierLaw(const std::string_view noun)
{
    return ChartTechniqueLaw{
        .noun = noun,
        // The one row family with two scopes, because vibrato is the one technique here that is
        // interval STATE: a selected note carries the tier when its onset opens at it, and a
        // selected keyframe when the state in force where it stands is at it. Both are read for
        // the same uniform-scope answer, so a press over a mixed selection clears only when every
        // anchor in it already stands at this tier.
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
                               return selectedKeyframeVibrato(chart, key) == Tier;
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
                    set ? Tier : common::core::VibratoState::Off,
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
    // two harmonic rows set through their own hand's planner and clear through one shared plan.
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
        case ChartTechnique::Harmonic:
        {
            return ChartTechniqueLaw{
                .noun = "Harmonic",
                // Deliberately WIDER than fretHandHarmonic: any node the fretting side of the
                // instrument owns counts, so the clear also reaches a tap harmonic and an imported
                // artificial one, which nothing else in the editor can un-harmonic. The pinch is
                // the one node it excludes, because the row below owns that hand.
                .carried =
                    [](const common::core::Chart& chart, const ChartSelection& selection) {
                        return everySelectedNoteCarries(
                            chart, selection, [](const common::core::ChartNote& note) {
                                return note.harmonic_node.has_value() &&
                                       common::core::nodeIsOnNeck(note.attack);
                            });
                    },
                // The SET states no choice, which means the node nearest the typed fret. Where the
                // fret names two nodes the verb arms the picker instead and settles this same
                // planner with the candidate the charter chose, so the two are one function and
                // this row is what a choiceless press does.
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const ChartSelection& selection,
                       const bool set,
                       const std::string_view label) {
                        return set ? planSetHarmonic(
                                         chart, tempo_map, selection.notes(), std::nullopt, label)
                                   : planClearHarmonic(chart, tempo_map, selection.notes(), label);
                    },
            };
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
                                   : planClearHarmonic(chart, tempo_map, selection.notes(), label);
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
    common::core::Chart* const chart = context.session.currentChart();
    if (chart == nullptr)
    {
        return std::unexpected{EditorUndoFailureCode::PreflightRejected};
    }
    return applyChartChange(*chart, plan);
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

} // namespace rock_hero::editor::core
