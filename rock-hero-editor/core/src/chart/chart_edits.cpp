#include "chart/chart_edits.h"

#include "chart/pick_slide_defaults.h"

#include <algorithm>
#include <array>
#include <cstddef>
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
// The connection verb no longer asks: a legato claim stores no direction, so it can never demand a
// node leave. That is the shape difference the stored-direction model paid for with a rule the two
// verbs had to agree on by hand — and disagreed on.
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

// Diffs one authored array's current values against the planned ones into removed/inserted full
// values; both inputs are sorted by the chart's slot order. Written once over the record type
// because a note stream and a hold-marker array are the same walk — a per-array copy would be the
// same merge stated twice, free to disagree about what "changed in place" means.
template <typename Record>
[[nodiscard]] ChartArrayChange<Record> diffSlotArray(
    const std::vector<Record>& before, const std::vector<Record>& after)
{
    ChartArrayChange<Record> change;
    std::size_t before_index = 0;
    std::size_t after_index = 0;
    while (before_index < before.size() || after_index < after.size())
    {
        if (before_index == before.size())
        {
            change.inserted.push_back(after[after_index++]);
            continue;
        }
        if (after_index == after.size())
        {
            change.removed.push_back(before[before_index++]);
            continue;
        }
        const Record& old_record = before[before_index];
        const Record& new_record = after[after_index];
        if (chartSlotKeyOf(old_record) < chartSlotKeyOf(new_record))
        {
            change.removed.push_back(before[before_index++]);
            continue;
        }
        if (chartSlotKeyOf(new_record) < chartSlotKeyOf(old_record))
        {
            change.inserted.push_back(after[after_index++]);
            continue;
        }
        if (!(old_record == new_record))
        {
            change.removed.push_back(old_record);
            change.inserted.push_back(new_record);
        }
        ++before_index;
        ++after_index;
    }
    return change;
}

// One authored array cut in two by a key set: the records the keys name, and everything else.
// Both keep their slot order. The two range verbs share it — deleting is "keep the rest", moving
// is "transform the keyed half and put it back" — and both run it over each authored array, so
// neither verb states per-array handling of its own.
template <typename Record> struct KeyedSplit
{
    std::vector<Record> keyed;
    std::vector<Record> rest;
};

// keys must be sorted ascending (the ChartSelection order); the lookup binary-searches it.
template <typename Record>
[[nodiscard]] KeyedSplit<Record> splitByKeys(
    const std::vector<Record>& records, const std::vector<ChartSlotKey>& keys)
{
    KeyedSplit<Record> split;
    split.rest.reserve(records.size());
    for (const Record& record : records)
    {
        if (std::ranges::binary_search(keys, chartSlotKeyOf(record)))
        {
            split.keyed.push_back(record);
            continue;
        }
        split.rest.push_back(record);
    }
    return split;
}

// Slides every record by the delta in place, or answers false and leaves them half-moved for the
// caller to discard. Refused, never clamped: a move that would leave the neck or the grid is
// invalid. The grid arithmetic itself clamps at the origin, so leaving the grid shows up as a move
// that fell short of the delta asked for.
//
// One template over the record type because a note and a hold marker move identically — they are
// the same slot arithmetic, and the marker riding along with its notes is exactly what keeps a
// moved chord's silent member from being left behind.
template <typename Record>
[[nodiscard]] bool moveKeyedRecords(
    const common::core::TempoMap& tempo_map, std::vector<Record>& records,
    const common::core::Fraction beat_delta, const int string_delta, const int string_count)
{
    for (Record& record : records)
    {
        const common::core::GridPosition from = record.position;
        record.position = common::core::advanceGridPosition(tempo_map, from, beat_delta);
        record.string += string_delta;
        if (record.string < 1 || record.string > string_count ||
            common::core::beatDistance(tempo_map, from, record.position) != beat_delta)
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
// edits from what it finds. The sustain gesture is the exception, and the reason the base is a
// parameter rather than read off `chart`: its plan must describe the whole gesture, so it is diffed
// against the stream the gesture started from while the ring rules still judge the live chart. The
// MARKER base is always the live `chart.hold_markers`, because no verb replans a marker gesture
// across presses — so it is read here rather than passed, and cannot be passed wrong.
//
// The hold markers pass through the SAME gate rather than a rule of their own, and that is what
// keeps disjointness true by construction: a note planted where a marker sits, or a marker planted
// where a note sounds, refuses here even though neither planner mentions the other array. Markers
// carry no in-plan repair — every marker rule is a refusal, so a candidate needing one is refused
// whole rather than quietly clamped onto a stop the charter never typed.
[[nodiscard]] std::expected<ChartEditPlan, ChartPlanRefusal> finalizePlan(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base,
    std::vector<common::core::ChartNote> candidate,
    std::vector<common::core::ChartHoldMarker> candidate_markers, std::string_view label)
{
    std::ranges::sort(candidate, common::core::chartNoteOrderLess);
    std::ranges::sort(candidate_markers, common::core::chartHoldMarkerOrderLess);
    // The truncated indices are the load path's business (it names what it changed); a producer
    // that only needs the invariant ignores them, which is why the rule is not [[nodiscard]].
    common::core::normalizeSustainOverlaps(candidate, tempo_map);
    // The in-plan repair (E4). Relational truths deliberately do not repair here (see
    // planSettleLegato): mid-burst a claim the chart cannot justify simply plays as the pick it
    // sounds like, and the burst stays one undo step. Sweeping the whole candidate needs no record
    // of which notes the plan touched, because a note the plan left alone already passed this gate.
    for (common::core::ChartNote& note : candidate)
    {
        static_cast<void>(common::core::flattenStrandedStrike(note));
    }
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
    // Judged against the candidate's own saved notes, never the chart's: the slot rule has to see
    // the stream the plan produces, or a conversion would be refused for colliding with the very
    // note it removes.
    if (!common::core::validateChartHoldMarkers(
             candidate_markers, saved_form, chart.tuning, tempo_map)
             .has_value())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    ChartEditPlan plan{
        .notes = diffSlotArray(base, candidate),
        .hold_markers = diffSlotArray(chart.hold_markers, candidate_markers),
        .label = std::string{label},
    };
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
// a mixed selection applies to what CAN take the write and leaves the rest alone. The three
// planners used to carry this skeleton each, and two of them disagreed about the no-op test.
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
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(candidate), chart.hold_markers, label);
}

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
    // Placing on an occupied slot replaces the note there. Still reachable under the marker
    // model: undo/redo never move the marker, so undoing a delete can put a note back under
    // an armed caret with an empty selection — the next typed digit inserts onto that
    // occupied slot and must replace, not collide.
    std::erase_if(candidate, [&note](const common::core::ChartNote& existing) {
        return chartSlotKeyOf(existing) == chartSlotKeyOf(note);
    });
    candidate.push_back(std::move(note));
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(candidate), chart.hold_markers, "Insert Note");
}

std::expected<ChartEditPlan, ChartPlanRefusal> planToggleHoldMarker(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const ChartSlotKey& slot)
{
    const auto marker_at = std::ranges::lower_bound(
        chart.hold_markers, slot, {}, [](const common::core::ChartHoldMarker& marker) {
            return chartSlotKeyOf(marker);
        });
    if (marker_at != chart.hold_markers.end() && chartSlotKeyOf(*marker_at) == slot)
    {
        std::vector<common::core::ChartHoldMarker> markers = chart.hold_markers;
        markers.erase(
            markers.begin() + std::ranges::distance(chart.hold_markers.begin(), marker_at));
        return finalizePlan(
            chart, tempo_map, chart.notes, chart.notes, std::move(markers), "Remove Hold Marker");
    }

    const auto note_at =
        std::ranges::lower_bound(chart.notes, slot, {}, [](const common::core::ChartNote& note) {
            return chartSlotKeyOf(note);
        });
    const bool convert = note_at != chart.notes.end() && chartSlotKeyOf(*note_at) == slot;
    // Convert carries the note's fret; authoring on an empty slot states none, because a later
    // in-span note on that string is what supplies it. The two cases are the whole fret story: a
    // fret is authored exactly where no note could ever state it.
    const common::core::ChartHoldMarker marker{
        .position = slot.position,
        .string = slot.string,
        .fret = convert ? std::optional{note_at->fret} : std::optional<int>{},
    };
    std::vector<common::core::ChartNote> notes = chart.notes;
    if (convert)
    {
        notes.erase(notes.begin() + std::ranges::distance(chart.notes.begin(), note_at));
    }
    std::vector<common::core::ChartHoldMarker> markers = chart.hold_markers;
    markers.push_back(marker);
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(notes), std::move(markers), "Hold Marker");
}

std::expected<ChartEditPlan, ChartPlanRefusal> planDeleteSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartSlotKey>& marker_keys)
{
    KeyedSplit<common::core::ChartNote> notes = splitByKeys(chart.notes, note_keys);
    KeyedSplit<common::core::ChartHoldMarker> markers =
        splitByKeys(chart.hold_markers, marker_keys);
    const std::size_t deleted_notes = notes.keyed.size();
    const std::size_t deleted_markers = markers.keyed.size();
    if (deleted_notes == 0 && deleted_markers == 0)
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
    if (deleted_markers == 0)
    {
        label = "Delete " + count_label(deleted_notes, "Note", "Notes");
    }
    else if (deleted_notes == 0)
    {
        label = "Delete " + count_label(deleted_markers, "Hold Marker", "Hold Markers");
    }
    else
    {
        label = "Delete Selection";
    }
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(notes.rest), std::move(markers.rest), label);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planMoveSelection(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<ChartSlotKey>& note_keys, const std::vector<ChartSlotKey>& marker_keys,
    common::core::Fraction beat_delta, int string_delta, std::string_view label)
{
    if ((note_keys.empty() && marker_keys.empty()) ||
        (beat_delta.numerator == 0 && string_delta == 0))
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }

    const int string_count = static_cast<int>(chart.tuning.strings.size());
    KeyedSplit<common::core::ChartNote> notes = splitByKeys(chart.notes, note_keys);
    KeyedSplit<common::core::ChartHoldMarker> markers =
        splitByKeys(chart.hold_markers, marker_keys);
    if (notes.keyed.empty() && markers.keyed.empty())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }
    if (!moveKeyedRecords(tempo_map, notes.keyed, beat_delta, string_delta, string_count) ||
        !moveKeyedRecords(tempo_map, markers.keyed, beat_delta, string_delta, string_count))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

    // Converging moves that stack two records on one slot are refused, as is landing on a slot an
    // unmoved record occupies — and both tests span BOTH arrays, because the arrays share one slot
    // space and a note landing on a marker's slot is exactly the collision disjointness forbids.
    std::vector<ChartSlotKey> target_keys;
    target_keys.reserve(notes.keyed.size() + markers.keyed.size());
    for (const common::core::ChartNote& note : notes.keyed)
    {
        target_keys.push_back(chartSlotKeyOf(note));
    }
    for (const common::core::ChartHoldMarker& marker : markers.keyed)
    {
        target_keys.push_back(chartSlotKeyOf(marker));
    }
    std::ranges::sort(target_keys);
    if (std::ranges::adjacent_find(target_keys) != target_keys.end())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    const auto lands_on_unmoved = [&target_keys](const auto& rest) {
        return std::ranges::any_of(rest, [&target_keys](const auto& record) {
            return std::ranges::binary_search(target_keys, chartSlotKeyOf(record));
        });
    };
    if (lands_on_unmoved(notes.rest) || lands_on_unmoved(markers.rest))
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }

    notes.rest.insert(notes.rest.end(), notes.keyed.begin(), notes.keyed.end());
    markers.rest.insert(markers.rest.end(), markers.keyed.begin(), markers.keyed.end());
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(notes.rest), std::move(markers.rest), label);
}

std::expected<ChartEditPlan, ChartPlanRefusal> planRetypeFrets(
    const common::core::Chart& chart, const common::core::TempoMap& tempo_map,
    const std::vector<common::core::ChartNote>& base, int target, bool set_exact)
{
    // The transposition anchor: the shared delta comes from the snapshot's lowest fret.
    std::optional<int> lowest;
    for (const common::core::ChartNote& note : base)
    {
        if (!lowest.has_value() || note.fret < *lowest)
        {
            lowest = note.fret;
        }
    }
    if (!lowest.has_value())
    {
        return std::unexpected{ChartPlanRefusal::NoChange};
    }

    const int delta = set_exact ? 0 : target - *lowest;
    const std::string label =
        (set_exact ? "Set Fret " : "Transpose to Fret ") + std::to_string(target);
    // Retyped values compute from the SNAPSHOT (the multi-digit window replans the whole entry
    // from the pre-entry originals) and swap into the live stream for the shared finalize, whose
    // whole-matrix gate replaces the local fret caps this once carried: any out-of-range or
    // rule-violating result refuses the plan outright.
    std::vector<common::core::ChartNote> retyped_notes;
    retyped_notes.reserve(base.size());
    for (const common::core::ChartNote& note : base)
    {
        // The fret-verb law (user-ruled 2026-08-13): a fret verb edits exactly the selected
        // notes' own frets — a slide's path never rides along, in either mode, because every
        // waypoint was placed on its fret on purpose. Do not restore the old scrape special case
        // that translated the path with the start; it was ruled a bug. A scrape start retyped
        // onto its first path position is refused downstream by the always-traveling rule in the
        // finalize gate; a pitched slide's equal-fret start is the legal hold encoding and passes.
        common::core::ChartNote retyped = note;
        retyped.fret = set_exact ? target : note.fret + delta;
        retyped_notes.push_back(std::move(retyped));
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
    return finalizePlan(
        chart, tempo_map, chart.notes, std::move(candidate), chart.hold_markers, label);
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
        stepped.sustain = authoredSustain(tempo_map, *start, steps);
        // A scrape needs somewhere to travel: its sustain floors at the minimum gesture window
        // (the path re-terminates onto the shrunk tail via the payload clip). That floor is always
        // positive, so a scrape never reaches the hold below.
        if (common::core::isScrape(stepped.attack) &&
            stepped.sustain < common::core::g_minimum_slide_window)
        {
            stepped.sustain = common::core::g_minimum_slide_window;
        }
        // Every note rings for some length, so there is no empty ring to shrink to: a note the
        // replay takes to zero or below keeps the ring it currently has — the live value the
        // candidate was seeded with — rather than being clamped to some invented floor, and
        // rejoins the gesture the moment the replayed ring is positive again. Deleting the note is
        // the verb for removing it.
        if (stepped.sustain.numerator > 0)
        {
            // The one bound on a ring (40-Q2-B): a tail may reach exact adjacency with the next
            // onset on its OWN string and no further, because a re-strike stops the ring. The
            // margin that used to bind growth against ANY string was the DRAWN tail's spacing
            // rule, which presentation now owns. Clamping the replayed value needs no direction
            // test and no memory of the previous step — a note pinned at its bound reports the
            // bound for every step past it, and leaves it the moment the replayed ring falls back
            // inside. The clamp can never SHORTEN a note below where the gesture found it:
            // normalizeSustainOverlaps holds every stored ring inside this same bound, so `start`
            // is already at most the bound.
            if (const std::optional<common::core::Fraction> bound =
                    common::core::sustainBoundOf(chart.notes, note, tempo_map);
                bound.has_value() && *bound < stepped.sustain)
            {
                stepped.sustain = *bound;
            }
            common::core::clipPayloadsToSustain(stepped);
            note = std::move(stepped);
        }
        // A held note counts too: the ring it keeps is still what the entry writes over `base`.
        net = net + (note.sustain - start->sustain);
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
    return finalizePlan(chart, tempo_map, base, std::move(candidate), chart.hold_markers, label);
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
            if (hold_was_the_only_blocker && !predecessor->slide_out.has_value())
            {
                candidate[predecessor_index].sustain = still_ringing.sustain;
                common::core::clipPayloadsToSustain(candidate[predecessor_index]);
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
        if (auto plan = finalizePlan(
                chart, tempo_map, chart.notes, std::move(candidate), chart.hold_markers, label);
            plan.has_value())
        {
            outcome.plan = std::move(*plan);
        }
    }
    return outcome;
}

std::optional<ChartEditPlan> planSettleLegato(
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
    return ChartEditPlan{
        .notes = diffSlotArray(base.notes, settled),
        // The sweep touches no marker, so this half is exactly what the entry being folded did to
        // the marker array — DERIVED from the two chart states rather than carried across by hand.
        // It cannot be left empty: the caller walks the live chart back through the burst's own
        // reversal, which takes both arrays with it, so an empty half here would drop the markers
        // that burst authored (and resurrect the note a conversion took) the moment a settle lands
        // on one.
        .hold_markers = diffSlotArray(base.hold_markers, chart.hold_markers),
        .label = std::string{label},
    };
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
                // simply resurface — except a latent slide-out, which the scrape's own terminal
                // occupied.
                retyped.slides.clear();
                retyped.slide_out.reset();
            }
            // A pinch is picking while damping a node, so the verb authors one when none exists:
            // the octave at the stop — the lowest-order harmonic available at any fret and the
            // commonest squeal — matching the import default. An existing node keeps its
            // position; it names the same physical point under either picking-hand reading.
            if (attack == common::core::NoteAttack::Pinch && !retyped.harmonic_node.has_value())
            {
                retyped.harmonic_node =
                    static_cast<double>(common::core::physicalStopFret(note, chart.tuning.capo)) +
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

ChartTechniqueLaw chartTechniqueLaw(const ChartTechnique technique)
{
    // Each row binds a noun, the "already carries it" test, and the planner. The flag rows ask
    // the one flag-to-field mapping; the emphasis rows compare against the axis's value; the
    // scrape row is the attack planner in both directions.
    switch (technique)
    {
        case ChartTechnique::PalmMute:
        {
            return ChartTechniqueLaw{
                .noun = "Palm Mute",
                .carries = [](const common::core::ChartNote& note) { return note.palm_mute; },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart, tempo_map, keys, ChartNoteFlag::PalmMute, set, label);
                    },
            };
        }
        case ChartTechnique::Dead:
        {
            return ChartTechniqueLaw{
                .noun = "Dead Note",
                .carries = [](const common::core::ChartNote& note) { return note.dead; },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart, tempo_map, keys, ChartNoteFlag::Dead, set, label);
                    },
            };
        }
        case ChartTechnique::Tremolo:
        {
            return ChartTechniqueLaw{
                .noun = "Tremolo",
                .carries = [](const common::core::ChartNote& note) { return note.tremolo; },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart, tempo_map, keys, ChartNoteFlag::Tremolo, set, label);
                    },
            };
        }
        case ChartTechnique::Vibrato:
        {
            return ChartTechniqueLaw{
                .noun = "Vibrato",
                .carries = [](const common::core::ChartNote& note) { return note.vibrato; },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetNoteFlag(
                            chart, tempo_map, keys, ChartNoteFlag::Vibrato, set, label);
                    },
            };
        }
        case ChartTechnique::Accent:
        {
            return ChartTechniqueLaw{
                .noun = "Accent",
                .carries =
                    [](const common::core::ChartNote& note) {
                        return note.emphasis == common::core::NoteEmphasis::Accent;
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetEmphasis(
                            chart,
                            tempo_map,
                            keys,
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
                .carries =
                    [](const common::core::ChartNote& note) {
                        return note.emphasis == common::core::NoteEmphasis::Ghost;
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetEmphasis(
                            chart,
                            tempo_map,
                            keys,
                            set ? common::core::NoteEmphasis::Ghost
                                : common::core::NoteEmphasis::Normal,
                            label);
                    },
            };
        }
        case ChartTechnique::PickSlide:
        {
            return ChartTechniqueLaw{
                .noun = "Pick Slide",
                .carries =
                    [](const common::core::ChartNote& note) {
                        return common::core::isScrape(note.attack);
                    },
                .plan =
                    [](const common::core::Chart& chart,
                       const common::core::TempoMap& tempo_map,
                       const std::vector<ChartSlotKey>& keys,
                       const bool set,
                       const std::string_view label) {
                        return planSetAttack(
                            chart,
                            tempo_map,
                            keys,
                            set ? common::core::NoteAttack::PickSlide
                                : common::core::NoteAttack::Pick,
                            label);
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

// Applies one array's change to a copy of that array, or refuses. Written over the record type so
// the notes and the hold markers share one preflight: every removal must still match by full value
// and every insertion must land on a free slot.
template <typename Record>
[[nodiscard]] std::expected<std::vector<Record>, EditorUndoFailureCode> withArrayChange(
    const std::vector<Record>& current, const ChartArrayChange<Record>& change)
{
    std::vector<Record> records = current;
    for (const Record& record : change.removed)
    {
        const auto found =
            std::ranges::lower_bound(records, chartSlotKeyOf(record), {}, [](const Record& held) {
                return chartSlotKeyOf(held);
            });
        if (found == records.end() || !(*found == record))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        records.erase(found);
    }
    for (const Record& record : change.inserted)
    {
        const auto insert_at =
            std::ranges::lower_bound(records, chartSlotKeyOf(record), {}, [](const Record& held) {
                return chartSlotKeyOf(held);
            });
        if (insert_at != records.end() && chartSlotKeyOf(*insert_at) == chartSlotKeyOf(record))
        {
            return std::unexpected{EditorUndoFailureCode::PreflightRejected};
        }
        records.insert(insert_at, record);
    }
    return records;
}

} // namespace

std::expected<void, EditorUndoFailureCode> applyChartChange(
    common::core::Chart& chart, const ChartEditPlan& plan)
{
    // Both arrays are rebuilt on copies before either is swapped in, so a failed precondition in
    // the second one cannot leave the first half applied — which is what makes a plan crossing the
    // two arrays one atomic gesture.
    auto notes = withArrayChange(chart.notes, plan.notes);
    if (!notes.has_value())
    {
        return std::unexpected{notes.error()};
    }
    auto markers = withArrayChange(chart.hold_markers, plan.hold_markers);
    if (!markers.has_value())
    {
        return std::unexpected{markers.error()};
    }

    chart.notes = std::move(*notes);
    chart.hold_markers = std::move(*markers);
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
