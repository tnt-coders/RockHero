#include "chart/chart_edits.h"
#include "chart/chart_hit_testing.h"
#include "chart/chart_navigation.h"
#include "chart/chart_selection.h"
#include "controller/editor_controller_impl.h"
#include "shared/editor_controller_logging.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/bend_travel.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/overloaded.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/editor/core/chart/chart_reveal.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Pointer travel past this distance turns an empty-lane press into a marquee instead of a
// click-to-seek; small enough that deliberate drags always marquee, large enough that a shaky
// click never accidentally selects.
constexpr float g_chart_click_threshold_px = 4.0f;

// The multi-digit fret entry window, shared by selection retyping and pending-insert
// composition: well above deliberate two-digit typing (inter-keystroke ~150-300ms) and below a
// thinking pause, so "12" combines and "2, pause, 3" stays two values.
constexpr std::uint32_t g_fret_entry_window_ms = 750;

// How far the paused transport may stand from the position the editor put the cursor at and still
// be standing there. Not zero: 200 ms after a seek, even while paused, Tracktion writes its
// playhead's sample-based position back over the one it was given
// (tracktion_TransportControl.cpp:1083-1096), which moves it by up to half a sample. Any deliberate
// move of the cursor is far larger.
constexpr double g_cursor_column_tolerance_seconds = 0.001;

// The face a target addresses: an object's BEND CHIP, or the mark every other target is. A press
// on a face settles it whole: it hands the caret that face, preserving a wider selection where the
// object is already in one. So the slide-out's collapse — which exists to reduce a chord selection
// to the head that was clicked — runs only for a mark, or it would take back exactly the selection
// the press preserved.
[[nodiscard]] ChartCaretFace chartTargetFace(const ChartHitTarget& target) noexcept
{
    return std::holds_alternative<ChartBendChipHit>(target) ? ChartCaretFace::BendChip
                                                            : ChartCaretFace::Mark;
}

} // namespace

// The memoized projection of the current chart, refreshed on read: pointer events resolve against
// it, which is the projection the view last drew, and a read between an edit and the next view
// push — an undo transition's focus asking whether a held stop is drawn — sees the chart it acts
// on rather than the one the edit replaced. Null while no arrangement is displayed.
const common::core::ChartViewState* EditorController::Impl::currentTabProjection() const
{
    refreshChartProjections();
    return m_tab_view_state.get();
}

// Rationale lives on the declaration in editor_controller_impl.h.
ChartEditViewState EditorController::Impl::resolvedChartEdit() const
{
    ChartEditViewState edit;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return edit;
    }
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    edit.selected_notes = selectedNoteIndices(notes, chartSelection());
    if (m_tab_view_state != nullptr)
    {
        edit.selected_keyframes =
            selectedKeyframeIndices(notes, m_tab_view_state->notes, chartSelection());
    }
    // The hand row's selection is outlined on the lane's chip: the placement's index in the chart's
    // stream is its index in the projection too, one placement projecting to one chip.
    if (const auto* const placement = std::get_if<FretHandPositionSelection>(&m_selection))
    {
        edit.selected_fret_hand_position = markerIndex(MarkerRow::Hand, placement->position);
    }
    // Armed ⟹ paused is structural (play and the transport listener demote), so no transport
    // check re-derives it here.
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && !caret->lane.has_value())
    {
        edit.caret = ChartCaretViewState{
            .seconds = secondsAtGridPosition(session().song().tempo_map, caret->position),
            .string = caret->string,
            .face = chartCaretFace(),
        };
    }
    return edit;
}

// The presence a pointer event resolves against, spelled from the grounds the lane paints by
// (chartPresence), settled where the lane eases. A mark the reveal brought in is reachable
// while it is drawn, which is the whole of "nothing undrawn is clickable", and a head stepped back
// behind the ring being edited answers after that ring's faces. The answer is consumed by the
// event's own resolution against `tab`, which outlives it.
common::ui::TabPresence EditorController::Impl::chartPresenceFor(
    const ChartPointerEvent& event, const common::core::ChartViewState& tab) const
{
    return [answers = chartPresence(tab.notes, event.modifiers.alt, resolvedChartEdit())](
               const std::size_t index) { return answers[index]; };
}

// Each authored array is sorted by (position, string) and the tab projection preserves that order
// one to one, so a projection index addresses the chart record directly — the same rule for every
// kind, which is why the hit target names its own kind rather than the caller assuming one.
//
// A keyframe hit is the one that cannot stop at the note: its identity is (note slot, offset), and
// the offset comes off the projected keyframe the pointer landed on, which carries it for exactly
// this purpose.
std::optional<ChartSelectionKey> EditorController::Impl::chartSelectionKeyAt(
    const ChartHitTarget& target) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::ChartViewState* const tab = currentTabProjection();
    return std::visit(
        [this, &chart, tab](const auto& hit) -> std::optional<ChartSelectionKey> {
            using Hit = std::remove_cvref_t<decltype(hit)>;
            // A bend chip resolves to the object it prints the bend of, exactly as that object's
            // own mark does: it is a second FACE of one object, never a second object. What the
            // hits differ in is the face the press then arms, which is the caller's question
            // rather than this one's.
            if constexpr (std::is_same_v<Hit, ChartBendChipHit>)
            {
                return std::visit(
                    [this](const auto& owner) {
                        return chartSelectionKeyAt(ChartHitTarget{owner});
                    },
                    hit.owner);
            }
            else if constexpr (!std::is_same_v<Hit, ChartKeyframeHit>)
            {
                if (hit.index >= chart.notes.size())
                {
                    return std::nullopt;
                }
                return ChartNoteKey{.slot = chartSlotKeyOf(chart.notes[hit.index])};
            }
            else
            {
                if (tab == nullptr || hit.note_index >= chart.notes.size() ||
                    hit.note_index >= tab->notes.size())
                {
                    return std::nullopt;
                }
                const std::vector<common::core::KeyframeViewState>& keyframes =
                    tab->notes[hit.note_index].keyframes;
                if (hit.keyframe_index >= keyframes.size())
                {
                    return std::nullopt;
                }
                return ChartKeyframeKey{
                    .note = chartSlotKeyOf(chart.notes[hit.note_index]),
                    .offset = keyframes[hit.keyframe_index].offset,
                };
            }
        },
        target);
}

void EditorController::Impl::clearChartEditingState()
{
    // DISCARD rather than settle: this is context teardown (a chart being replaced or closed),
    // and committing a pending value into a dying session would author into the wrong chart.
    discardChartFretEntry();
    clearSelection();
    m_chart_gesture.reset();
    disarmChartVerbWindow();
    m_chart_bend_question.clear();
    m_chart_notes_top.reset();
    // A fresh chart-editing context starts passive: the paused cursor at the transport
    // position is the position, and nothing is armed until the first click or arrow (the
    // marker model).
    m_chart_marker = ChartCursor{};
}

// Read-only view of the chart alternative; any other held kind reads as the empty selection,
// which is exactly what "no chart selection" means to every chart handler.
const ChartSelection& EditorController::Impl::chartSelection() const
{
    static const ChartSelection g_empty{};
    const auto* const selection = std::get_if<ChartSelection>(&m_selection);
    return selection != nullptr ? *selection : g_empty;
}

// Mutable access emplaces the chart alternative, so any chart-selection gesture structurally
// replaces a tone-region or automation-point selection (one selection editor-wide).
ChartSelection& EditorController::Impl::chartSelectionMutable()
{
    if (auto* const selection = std::get_if<ChartSelection>(&m_selection))
    {
        return *selection;
    }
    // The emplace is the chart funnel's counterpart to setSelection: it REPLACES another kind, so
    // the selection input the audible tone reads has changed and is re-derived here. Only this
    // branch — the early return above is reached once per marquee move, and nothing changed there.
    auto& emplaced = m_selection.emplace<ChartSelection>();
    syncAudibleTone();
    return emplaced;
}

std::string EditorController::Impl::selectedToneRegionId() const
{
    const ToneRegionSelection* const selection = std::get_if<ToneRegionSelection>(&m_selection);
    return selection != nullptr ? selection->region_id : std::string{};
}

const AutomationPointSelection* EditorController::Impl::selectedAutomationPoint() const
{
    return std::get_if<AutomationPointSelection>(&m_selection);
}

const TimeSelection* EditorController::Impl::selectedTimeSelection() const
{
    return std::get_if<TimeSelection>(&m_selection);
}

// Replaces the whole selection, and drops any in-flight multi-digit fret entry with it: the entry
// is keyed to the chart selection it retypes, so leaving it armed against a vanished selection
// could widen an undo entry for notes no longer selected.
//
// This reset is belt-and-braces, NOT the guarantee — several chart paths replace the selection
// through `chartSelectionMutable` without coming through here, so a comment promising that every
// replacement funnels through this one function would be false and would invite someone to rely on
// it. What actually protects the entry is the widen's own key check: it proceeds only while the
// entry's keys still equal the current chart selection, so any selection change of any shape
// declines the widen. The audible tone is the one thing both funnels DO owe: the sync below is
// matched by one on `chartSelectionMutable`'s emplace, because the selection is an input to it.
//
// A user-initiated selection replacement IS a settle event: the burst is over, so the claims it
// broke stop being transient. The follow-the-edit rewrites inside applyChartEditPlan deliberately
// do not come through here — settling on those would cut every burst into single edits.
void EditorController::Impl::setSelection(EditorSelection selection)
{
    // Settle BEFORE the selection moves: the pending entry's plan and its selection follow are
    // expressed against the outgoing selection, and a value you typed is a value you meant.
    settleChartFretEntry();
    m_selection = std::move(selection);
    disarmChartVerbWindow();
    static_cast<void>(settleChart());
    // The selection is one of the three inputs the audible tone is derived from (syncAudibleTone),
    // so every replacement re-derives it here: a selected region IS the active tone, and a
    // selection of any other kind hands the active tone back to the cursor.
    syncAudibleTone();
}

void EditorController::Impl::clearSelection()
{
    setSelection(std::monostate{});
}

// Stated as the kinds that SURVIVE a cursor move, not the ones that clear: a chart selection and
// the time span are the only kinds with their own lifecycle, so a kind added later follows the
// cursor without this list having to learn its name.
void EditorController::Impl::clearCursorCoupledSelection()
{
    if (!std::holds_alternative<std::monostate>(m_selection) &&
        !std::holds_alternative<ChartSelection>(m_selection) &&
        !std::holds_alternative<TimeSelection>(m_selection))
    {
        setSelection(std::monostate{});
    }
}

// THE ONE WRITER of the armed caret, and the reason it exists: where the keyboard stands is one of
// the three inputs the audible tone is derived from (syncAudibleTone's law), and arming never
// seeks, so the transport cannot report the caret's new region on its behalf. Every write of it
// therefore re-derives the tone HERE — a caret riding a nudged note across a tone-region boundary
// as much as a deliberate arming — rather than at each site that moves it. The sync is idempotent,
// so a write that lands inside the region the rig already plays costs one compare.
void EditorController::Impl::setArmedCaret(ChartCaret caret)
{
    m_chart_marker = std::move(caret);
    syncAudibleTone();
}

// Returns the armed caret, or null while the marker is passive.
const EditorController::Impl::ChartCaret* EditorController::Impl::armedChartCaret() const noexcept
{
    return std::get_if<ChartCaret>(&m_chart_marker);
}

// Returns the marker's remembered string in either state.
int EditorController::Impl::chartMarkerString() const noexcept
{
    // Both marker states carry the remembered string. Read it through get_if rather than
    // std::visit so the accessor is genuinely noexcept: std::visit is potentially-throwing
    // (bad_variant_access), which the -Werror exception-escape check rejects in a noexcept
    // function. The marker is always one of the two alternatives, so the cursor branch is total.
    if (const ChartCaret* const caret = armedChartCaret())
    {
        return caret->string;
    }
    return std::get_if<ChartCursor>(&m_chart_marker)->string;
}

// Returns the marker's lane in either state; get_if rather than std::visit for the reason
// chartMarkerString gives.
const std::optional<EditorController::Impl::AutomationLaneRow>& EditorController::Impl::
    chartMarkerLane() const noexcept
{
    if (const ChartCaret* const caret = armedChartCaret())
    {
        return caret->lane;
    }
    return std::get_if<ChartCursor>(&m_chart_marker)->lane;
}

// Demotes an armed caret to the passive cursor, leaving the transport where it is. Used by
// the transport-motion handoffs (play, external playback, paused seeks): the row survives, and
// so does the caret's exact position, which the next arming trusts only if the transport never
// left it.
void EditorController::Impl::disarmChartMarker()
{
    if (const ChartCaret* const caret = armedChartCaret())
    {
        m_chart_marker =
            ChartCursor{.string = caret->string, .lane = caret->lane, .column = caret->position};
    }
}

// Demotes an armed caret to the passive cursor "in its place": the cursor moves to the caret's
// musical time, so the cursor line appears exactly where the caret was. Used by the
// editing-gesture handoffs (Ctrl+click, double-click, marquee, Esc) and by every step off a point
// row onto a marker row. The rig follows the move like any other cursor move: the lanes and the
// panel already follow the cursor, and a caret may have walked into another tone's region.
void EditorController::Impl::dissolveChartCaretInPlace()
{
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr)
    {
        return;
    }

    moveCursorTo(caret->position);
    disarmChartMarker();
}

// One of the two cursor-move entries, and the one the EDITOR drives: a marker step, the column
// rule, a dissolving caret, a moved marker the edit must show. The selection is kept whole, because
// none of those is transport motion — moving onto the thing you have selected must not deselect it.
// Contrast activateToneAtCursor(), which the TRANSPORT moved under and which drops the
// cursor-coupled selection. Either way the rig follows: the cursor is one of the three inputs the
// audible tone is derived from (syncAudibleTone).
void EditorController::Impl::moveCursorTo(const common::core::GridPosition position)
{
    m_transport.seek(
        session().timeline().clamp(
            common::core::TimePosition{secondsAtGridPosition(
                session().song().tempo_map, position)}));
    // Only the passive cursor remembers a column; while a caret is armed the caret's own position
    // IS the remembered one, and disarmChartMarker carries it across at the demotion. That is why a
    // dissolve seeks through here and demotes afterwards.
    if (auto* const cursor = std::get_if<ChartCursor>(&m_chart_marker))
    {
        cursor->column = position;
    }
    syncAudibleTone();
}

// What a slot HOLDS — what a landing there addresses — or absent when nothing does. The note stream
// holds each slot at most once, so the note half is one binary search, and notes come first. Where
// no note stands, the only object that can is a keyframe STRICTLY INSIDE the one ring covering the
// slot — which is chartPathTailAt's question, answered there once for the typed digit and this — so
// this asks it and then only checks whether a keyframe sits at exactly that offset. Exact
// rationals, so equality is the test.
//
// A ring's END STATEMENT is not one of the slot's objects: it belongs to the ring that ENDS there
// rather than to the slot the next head starts on, and it is reached through that ring — by the
// object walk, one step below the head at a shared instant, or by clicking its chip, both of which
// carry the key itself into the landing (armChartCaret). That is what keeps a bare digit at a
// ring's end slot always the next note, whatever the end states.
//
// THE ARMING's authority, and only its: a landing that carries no object of its own re-derives one
// here. What a selection KEY names is a different question, asked of the key
// (dropChartSelectionKeysNamingNothing), because an object exists whether or not a landing reaches
// it.
std::optional<ChartSelectionKey> EditorController::Impl::chartObjectAt(
    const common::core::GridPosition position, const int string) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const auto slot_of = [](const common::core::ChartNote& note) { return chartSlotKeyOf(note); };
    const ChartSlotKey slot{.position = position, .string = string};
    if (std::ranges::binary_search(notes, slot, {}, slot_of))
    {
        return ChartNoteKey{.slot = slot};
    }
    const std::optional<ChartPathTail> tail =
        chartPathTailAt(notes, session().song().tempo_map, position, string);
    if (!tail.has_value() || tail->at_ring_end)
    {
        return std::nullopt;
    }
    const common::core::ChartNote* const carrier = chartNoteAt(notes, tail->note);
    if (carrier != nullptr && standingKeyframe(*carrier, tail->offset) != nullptr)
    {
        return ChartKeyframeKey{.note = tail->note, .offset = tail->offset};
    }
    return std::nullopt;
}

// Whether the note at this slot is IN FOCUS for the keyframe commit law: the charter's attention is
// on it — it is selected, the caret stands somewhere inside its ring, or it carries a selected
// point. The last arm is the one the caret cannot answer, because a multi-selection of points
// dissolves the caret: a point under scrutiny keeps its note in focus exactly as the caret on it
// would.
//
// Attention, which is the reveal's grounds minus the modifier (chartPresence): what a silent
// point may outlive is the charter still working on the note, never what is drawn. Spelled again
// here, in grid space, because it is asked mid-edit against the live chart, before the projection
// the reveal reads is rebuilt. It differs from the drawn focus at one seam on purpose: a caret on
// the head a ring ends on still attends that ring, so an end statement typed there survives.
bool EditorController::Impl::chartNoteInFocus(const ChartSlotKey& slot) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return false;
    }
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const auto slot_of = [](const common::core::ChartNote& candidate) {
        return chartSlotKeyOf(candidate);
    };
    if (!std::ranges::binary_search(notes, slot, {}, slot_of))
    {
        return false;
    }
    if (chartSelection().contains(ChartSelectionKey{ChartNoteKey{.slot = slot}}))
    {
        return true;
    }
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && !caret->lane.has_value())
    {
        // The caret's own slot first, since a ring's tail excludes the onset it starts at
        // (chartPathTailAt); the tail arm then covers the rest of the ring, its end included.
        if (ChartSlotKey{.position = caret->position, .string = caret->string} == slot)
        {
            return true;
        }
        const std::optional<ChartPathTail> tail =
            chartPathTailAt(notes, session().song().tempo_map, caret->position, caret->string);
        if (tail.has_value() && tail->note == slot)
        {
            return true;
        }
    }
    return std::ranges::any_of(chartSelection().keyframes(), [&slot](const ChartKeyframeKey& key) {
        return key.note == slot;
    });
}

// THE KEYFRAME COMMIT LAW's in-memory half. A point that says nothing the path does not already
// say is authoring state: the editor holds it while the charter is still on its note — a slide's
// start planted before its landing exists — and no longer. Every note `keeps` refuses loses its
// silent points here with NO history entry, because no entry ever held them (writtenChartPlan): the
// chart simply returns to the state the history already describes. That is also why the burst
// record retires when anything goes — its plan named the points, and a plan the live chart no
// longer matches would fail the next fold or continuation.
bool EditorController::Impl::dissolveSilentKeyframes(
    const std::function<bool(const ChartSlotKey&)>& keeps)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() ||
        m_undo_history.hasPendingTransition())
    {
        return false;
    }
    // Judged on the shared chart first, so the mutable access — which bumps the chart revision and
    // rebuilds every projection — is taken only when a point actually goes.
    std::vector<std::size_t> dissolving;
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        if (notes[index].keyframes.empty() || keeps(chartSlotKeyOf(notes[index])))
        {
            continue;
        }
        common::core::ChartNote stripped = notes[index];
        if (common::core::stripSilentKeyframes(stripped))
        {
            dissolving.push_back(index);
        }
    }
    if (dissolving.empty())
    {
        return false;
    }
    const bool written = m_session.writeChart([&dissolving](common::core::Chart& chart) {
        for (const std::size_t index : dissolving)
        {
            static_cast<void>(common::core::stripSilentKeyframes(chart.notes[index]));
        }
    });
    if (!written)
    {
        return false;
    }
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    updateView();
    return true;
}

// THE one test of whether a face can be stood on: the mark always, a bend chip where the object
// states a bend. The arming and the read both ask it, so a
// face is one predicate applied at two moments rather than two rules.
bool EditorController::Impl::chartFaceShown(
    const ChartCaretFace face, const std::optional<ChartSelectionKey>& object) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    switch (face)
    {
        case ChartCaretFace::Mark:
            return true;
        case ChartCaretFace::BendChip:
            return arrangement != nullptr && arrangement->chart.has_value() && object.has_value() &&
                   chartObjectStatesBend(arrangement->chart->notes, *object);
    }
    return false;
}

// The object the armed caret stands on. Armed means the selection is exactly the object under the
// caret, so it is the selection's one key; a face is therefore always one object's.
std::optional<ChartSelectionKey> EditorController::Impl::chartCaretObject() const
{
    const std::vector<ChartSelectionKey> keys = chartSelection().keys();
    if (armedChartStringCaret() == nullptr || keys.size() != 1)
    {
        return std::nullopt;
    }
    return keys.front();
}

// THE caret's face, and the one place a face's precondition is applied at READ time: the stored
// value is what the last arming asked for, and a face is worth only what the drawn picture still
// says. An edit can take a face out from under a stationary caret — clearing the bend is exactly
// what Delete on it does — and a caret left claiming a face that is gone would point
// the next key at nothing.
ChartCaretFace EditorController::Impl::chartCaretFace() const
{
    const ChartCaret* const caret = armedChartStringCaret();
    if (caret == nullptr)
    {
        return ChartCaretFace::Mark;
    }
    return chartFaceShown(caret->face, chartCaretObject()) ? caret->face : ChartCaretFace::Mark;
}

void EditorController::Impl::armChartCaret(
    common::core::GridPosition position, int string, ChartCaretFace face,
    const std::optional<ChartSelectionKey>& object)
{
    // The pending fret entry settles BEFORE the marker moves: the settle selects the note it
    // committed at the OLD slot, and the arming below then replaces that selection for the new
    // slot, so "armed implies the selection is what sits under the caret" holds through every
    // caret move. This is the one funnel behind pointer, arrow, jump, and row stepping; a verb
    // that settled only after moving the marker (the End key once did) left the caret at the
    // destination with the selection on the slot it left.
    settleChartFretEntry();
    // A caret move is a commit point for the chart verbs' coalescing window: a press after it
    // means the verb's ordinary law — never a reversal of the entry the window remembers, and
    // never a continuation of the duration gesture it was accumulating.
    disarmChartVerbWindow();
    const ChartSlotKey key{.position = position, .string = string};
    // THE LANDING'S OWN OBJECT when it carries one, and what the slot holds otherwise: the walk and
    // the pointer both know which object they reached, and at a shared instant the slot cannot say
    // — a head and the previous ring's end statement stand on one slot, and re-deriving would take
    // the head every time (chartObjectAt).
    const std::optional<ChartSelectionKey> landed =
        object.has_value() ? object : chartObjectAt(position, string);
    // A face exists only where it is DRAWN, so a request the landing cannot honour lands on the
    // mark instead of parking the caret on a face that is not there. This funnel is the face's one
    // writer, and the READ (chartCaretFace) asks the same predicate again at the moment the face is
    // spent, so an edit that takes a face away leaves the caret on the mark.
    if (!chartFaceShown(face, landed))
    {
        face = ChartCaretFace::Mark;
    }
    // The write re-derives the audible tone before the selection below is replaced, which is the
    // same answer either way: the only selection that outranks the caret is a selected tone region,
    // and both branches below replace it through chartSelectionMutable's emplace — itself a
    // re-derivation — so the last word always comes after this new caret is in place.
    setArmedCaret(ChartCaret{.position = position, .string = string, .face = face});
    if (landed.has_value())
    {
        // Whatever the landing addresses becomes the selection — a note or a keyframe alike, so the
        // armed-caret invariant reads the same for every kind and a verb finds its own object
        // selected after it authors one.
        chartSelectionMutable().replaceWith(*landed);
    }
    else
    {
        // Arming onto an empty slot empties the selection — through the chart alternative, so
        // a tone-region or automation-point selection is replaced too (the caret is now the
        // typing scope).
        chartSelectionMutable().clear();
    }
    // Arming replaces the selection without passing setSelection, so the settle event lands here
    // too: this is the funnel behind every caret move — pointer, arrow, jump — and the marker
    // restore at project open, where the loaded chart is already settled and the sweep finds
    // nothing.
    static_cast<void>(settleChart());
}

// Arms the caret on an automation lane row and re-derives the selection from what sits under
// it — armChartCaret's row-axis sibling (§9b): a point at the slot becomes the editor-wide
// selection, an empty slot clears it. The string survives as the fallback an arming takes once
// this lane is no longer visible.
void EditorController::Impl::armLaneCaret(
    common::core::GridPosition position, AutomationLaneRow row)
{
    if (lanePointAt(row, position))
    {
        setSelection(
            AutomationPointSelection{
                .instance_id = row.instance_id,
                .param_id = row.param_id,
                .position = position,
            });
    }
    else
    {
        setSelection(std::monostate{});
    }
    // The marker write is what moved the keyboard position, so the tone re-derives with it and not
    // in the setSelection call ahead of it, which still saw the caret's old slot.
    setArmedCaret(
        ChartCaret{.position = position, .string = chartMarkerString(), .lane = std::move(row)});
}

bool EditorController::Impl::lanePointAt(
    const AutomationLaneRow& row, const common::core::GridPosition& position)
{
    const std::vector<common::core::ToneAutomationPoint>* const points =
        lanePointsFor(row.instance_id, row.param_id);
    return points != nullptr &&
           std::ranges::any_of(*points, [&](const common::core::ToneAutomationPoint& point) {
               return point.position == position;
           });
}

// The caret row's next authored object strictly beyond the caret in the step direction, as the
// object itself: notes and their keyframes on the caret's string (the notes alone with notes_only),
// points on its lane. Linear scans are fine at keypress cadence. The shared instant's order is
// RowObjectStop's own (editor_controller_impl.h), so this walk only says which stops exist.
std::optional<EditorController::Impl::RowObjectStop> EditorController::Impl::nextRowObjectStop(
    const ChartCaret& caret, const bool later, const bool notes_only)
{
    // WHERE THE WALK STANDS, which the caret's slot alone cannot say once two objects share it: the
    // statement when the selection names one there — the walk's own landing selects it — and the
    // head otherwise, so a second press leaves the slot instead of stepping back onto the head.
    const ChartSlotKey caret_slot{.position = caret.position, .string = caret.string};
    const RowObjectStop from{
        .position = caret.position,
        .is_head = !std::ranges::any_of(
            chartSelection().keyframes(),
            [this, &caret_slot](const ChartKeyframeKey& key) {
                return chartCaretSlotFor(session().song().tempo_map, key) == caret_slot;
            }),
        .object = {},
    };
    std::optional<RowObjectStop> best;
    const auto consider = [&](const RowObjectStop& stop) {
        const bool beyond = later ? from < stop : stop < from;
        if (!beyond)
        {
            return;
        }
        if (!best.has_value() || (later ? stop < *best : *best < stop))
        {
            best = stop;
        }
    };
    if (caret.lane.has_value())
    {
        if (const std::vector<common::core::ToneAutomationPoint>* const points =
                lanePointsFor(caret.lane->instance_id, caret.lane->param_id))
        {
            for (const common::core::ToneAutomationPoint& point : *points)
            {
                // A lane point has no instant-mate, so which side of one it takes never decides
                // anything.
                consider(RowObjectStop{.position = point.position, .is_head = true, .object = {}});
            }
        }
        return best;
    }
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return best;
    }
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (note.string != caret.string)
        {
            continue;
        }
        const ChartSlotKey slot = chartSlotKeyOf(note);
        consider(
            RowObjectStop{
                .position = note.position,
                .is_head = true,
                .object = ChartSelectionKey{ChartNoteKey{.slot = slot}},
            });
        if (notes_only)
        {
            continue;
        }
        // A keyframe is an authored object on this string as much as the note it rides, standing
        // on its own slot along the ring, so the walk stops on it exactly as it stops on a note.
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            consider(
                RowObjectStop{
                    .position = common::core::advanceGridPosition(
                        tempo_map, note.position, keyframe.offset),
                    .is_head = false,
                    .object = ChartSelectionKey{
                        ChartKeyframeKey{.note = slot, .offset = keyframe.offset}
                    },
                });
        }
    }
    return best;
}

// The visible automation lane rows in display order, derived from the same lane-source
// enumeration the full projection builds lanes from — so traversal and display can never
// disagree about which rows exist, without paying the full projection (port parameter listing,
// per-point seconds) on every keystroke.
std::vector<EditorController::Impl::AutomationLaneRow> EditorController::Impl::
    visibleAutomationLaneRows() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }
    const std::vector<ToneAutomationLaneSource> sources = toneAutomationLaneSources(
        *arrangement, activeToneDocumentRef(), m_tone_plugin_bindings, m_open_automation_lanes);
    std::vector<AutomationLaneRow> rows;
    rows.reserve(sources.size());
    for (const ToneAutomationLaneSource& source : sources)
    {
        rows.push_back(
            AutomationLaneRow{.instance_id = source.instance_id, .param_id = source.param_id});
    }
    return rows;
}

// Resolves the event's snapped musical position and the string lane under the pointer — the
// chart's single placement seam (every press snaps through it, mirroring the lane's
// laneSnapPositionForX). It snaps to the placement quantum's exact rational, which is
// the displayed grid while snap is on and the tick lattice while it is off; no modifier composes
// a second answer.
std::optional<std::pair<common::core::GridPosition, int>> EditorController::Impl::chartPlacementAt(
    const ChartPointerEvent& event) const
{
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0 || event.geometry.lane_height <= 0.0f)
    {
        return std::nullopt;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::optional<common::core::TimePosition> clicked = timelinePositionForX(
        event.x, event.geometry.visible_timeline, static_cast<int>(event.geometry.bounds_width));
    if (!clicked.has_value())
    {
        return std::nullopt;
    }

    const common::core::GridPosition position =
        nearestTempoGridPosition(tempo_map, placementQuantum(), *clicked);

    // Lanes stack highest string on top; extra user lanes pad below the chart's strings.
    const float lane = (event.y - event.geometry.bounds_y) / event.geometry.lane_height;
    const int lane_index =
        std::clamp(static_cast<int>(lane), 0, event.geometry.displayed_count - 1);
    const int displayed_string = event.geometry.displayed_count - lane_index;
    const int string =
        std::clamp(displayed_string - event.geometry.extra_lanes, 1, tab->stringCount());
    return std::pair{position, string};
}

// Applies a planned chart-note change through the session's mutable chart (bumping the revision
// so every projection rebuilds) and records it as one undo entry. Takes the planners' own return
// shape; the refusal kind is not consumed here — a caller that wants to distinguish NoChange from
// Invalid branches before handing the plan over.
bool EditorController::Impl::applyChartEditPlan(
    std::expected<ChartEditPlan, ChartPlanRefusal> plan,
    std::optional<std::vector<ChartSelectionKey>> select_exactly)
{
    if (!plan.has_value())
    {
        return false;
    }

    // Where the charter stands before anything moves, which is exactly where undo returns them.
    std::optional<ChartEditFocus> before = chartEditFocusOf(chartSelection().keys());
    const std::optional<std::expected<void, EditorUndoFailureCode>> applied = m_session.writeChart(
        [&plan](common::core::Chart& chart) { return applyChartChange(chart, *plan); });
    if (!applied.has_value())
    {
        return false;
    }
    if (!applied->has_value())
    {
        // The plan was computed against this exact chart, so a precondition failure means a
        // logic error rather than user input; surface it instead of silently dropping the edit.
        reportError("Could not apply chart edit: " + plan->label);
        return false;
    }

    // Any chart edit closes the technique toggle windows unless the caller re-arms them. The
    // pending fret entry is normally ALREADY settled by the caller's prologue when another verb
    // reaches here (and taken out by settleChartFretEntry when this apply IS the settle); an
    // entry still present means a verb path missed its prologue, and the last resort is to
    // discard rather than commit — this plan was computed without knowledge of the pending one,
    // so committing both here could preflight-collide.
    discardChartFretEntry();
    disarmChartVerbWindow();

    // The selection follows the edit: a verb that knows where its objects landed says so, and
    // otherwise retyped/moved/inserted records stay selected under their new keys.
    std::vector<ChartSelectionKey> next_selection;
    if (select_exactly.has_value())
    {
        next_selection = *select_exactly;
    }
    else
    {
        // (selection - removed keys) + inserted keys: retyped/resized notes stay selected even
        // when the edit left some of them unchanged, and moved notes follow to their new keys.
        const auto in_side = [](const auto& side, const ChartSlotKey& key) {
            return std::ranges::any_of(
                side, [&key](const auto& record) { return chartSlotKeyOf(record) == key; });
        };
        const auto follow =
            [&next_selection, &in_side, &plan](const std::vector<ChartSlotKey>& selected) {
                for (const ChartSlotKey& key : selected)
                {
                    if (!in_side(plan->removed, key))
                    {
                        next_selection.emplace_back(ChartNoteKey{.slot = key});
                    }
                }
                for (const common::core::ChartNote& note : plan->inserted)
                {
                    const ChartSlotKey key = chartSlotKeyOf(note);
                    // A note rewritten IN PLACE that the user had not selected is something
                    // the plan carried, not the edit's subject — the H assist grows a
                    // predecessor's tail inside the same plan, and the finalize's overlap pass
                    // can retrim a same-string neighbour — so it must not join the selection.
                    // Selecting it would break the armed-caret invariant (armed means the
                    // selection is exactly what sits under the caret) and would silently widen
                    // the next keystroke's scope. A note inserted at a NEW key is the edit's own
                    // product (a moved or created object) and follows as before.
                    if (in_side(plan->removed, key) && !std::ranges::binary_search(selected, key))
                    {
                        continue;
                    }
                    next_selection.emplace_back(ChartNoteKey{.slot = key});
                }
            };
        follow(chartSelection().notes());
        // A keyframe key rides an edit that rewrote its note IN PLACE, which is every keyframe
        // verb there is. A note the plan MOVED or DELETED takes its keyframes' keys with it,
        // because the key names the old slot and the plan carries no map from an old slot to a
        // new one.
        for (const ChartKeyframeKey& keyframe : chartSelection().keyframes())
        {
            if (!in_side(plan->removed, keyframe.note) || in_side(plan->inserted, keyframe.note))
            {
                next_selection.emplace_back(keyframe);
            }
        }
    }
    landChartSelection(std::move(next_selection));

    // The history takes the WRITTEN form of the transition and the burst record the whole of it
    // (writtenChartPlan): a transition that only planted or moved a silent point writes as nothing,
    // so the point stands in the chart with no entry and no record at all, and the next edit on its
    // note diffs from the written state before it. The record is what the settle sweep folds its
    // flatten into, so the edit and the claim it broke undo together, and what the technique
    // toggle windows reverse.
    //
    // EVERY path through here assigns it, which is what makes this the record's one creator: the
    // entry when the history took one, and nothing when it did not. A record that outlived the edit
    // it names would hand the next burst someone else's entry — an entry-less edit moves the
    // history position not at all, so the position proof every reader rests on would still pass,
    // and a gesture press would reverse a stranger's plan and retire a stranger's entry. (A refused
    // push resets the history, which is the same reason.) The accepted consequence: a run whose
    // early steps write nothing and whose later step writes does not coalesce across that boundary,
    // which is correct — the early steps are authoring state no entry may hold.
    ChartEditPlan written = writtenChartPlan(*plan);
    std::optional<ChartEditFocus> after = chartEditFocusOf(chartSelection().keys());
    if (!written.empty() &&
        pushUndoEntry(std::make_unique<ChartEdit>(std::move(written), before, after)))
    {
        m_chart_notes_top = ChartNotesTopEntry{
            .plan = std::move(*plan),
            .history_position = m_undo_history.snapshot().position,
            .before = std::move(before),
            .after = std::move(after),
        };
    }
    else
    {
        m_chart_notes_top.reset();
    }
    updateView();
    return true;
}

// A key the edit took away names nothing, and would leave the next digit retyping no operand, so
// the selection keeps only what the written chart holds — the same rule the undo repair asks.
void EditorController::Impl::landChartSelection(std::vector<ChartSelectionKey> keys)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const common::core::Chart* written = nullptr;
    if (arrangement != nullptr)
    {
        // Bound to a local so the presence test and the read are provably one object.
        const std::optional<common::core::Chart>& chart = arrangement->chart;
        if (chart.has_value())
        {
            written = &*chart;
        }
    }
    std::erase_if(keys, [written](const ChartSelectionKey& key) {
        return written == nullptr || !chartHoldsKey(written->notes, key);
    });
    chartSelectionMutable().applyBox(keys, false);
}

std::optional<ChartEditFocus> EditorController::Impl::chartEditFocusOf(
    std::vector<ChartSelectionKey> selected) const
{
    // The slot comes from the first selected object rather than from the caret whenever there is
    // one: a verb that re-keys its objects moves the caret onto them only after its edit applies,
    // and the object's own slot is where the caret will stand.
    const ChartCaret* const caret = armedChartStringCaret();
    const ChartCursor* const cursor = std::get_if<ChartCursor>(&m_chart_marker);
    std::optional<ChartSlotKey> slot;
    if (!selected.empty())
    {
        slot = chartCaretSlotFor(session().song().tempo_map, selected.front());
    }
    else if (caret != nullptr)
    {
        slot = ChartSlotKey{.position = caret->position, .string = caret->string};
    }
    else if (cursor != nullptr && cursor->column.has_value() && !cursor->lane.has_value())
    {
        slot = ChartSlotKey{.position = *cursor->column, .string = cursor->string};
    }
    if (!slot.has_value())
    {
        return std::nullopt;
    }
    // The caret's stop is recorded only where the caret stands on that very slot.
    const bool caret_on_slot =
        caret != nullptr &&
        ChartSlotKey{.position = caret->position, .string = caret->string} == *slot;
    return ChartEditFocus{
        .selected = std::move(selected),
        .slot = *slot,
        .face = caret_on_slot ? chartCaretFace() : ChartCaretFace::Mark,
    };
}

// Arms the gesture and applies glyph-press selection per the containment hierarchy: a plain single
// press selects the individual note — keeping an existing multi-selection intact so a future drag
// can move it — a double press selects the note's whole onset group (its chord), and Ctrl toggles
// individual membership. Shift is reassigned to plan 52's time-range selection and behaves as
// plain until that lands. Marker handoffs (the marker model): a plain press on an unselected note
// arms the caret there; every multi-select gesture — Ctrl, double-click — dissolves the caret into
// a cursor in its place, so the visible glyph always states whether typing inserts or acts on the
// selection.
void EditorController::Impl::onChartPointerDown(const ChartPointerEvent& event)
{
    // The pending fret entry settles first (the uniform prologue): a click that starts a drag
    // on the very note being retyped must not race a half-typed value.
    settleChartFretEntry();
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0 || isBusy())
    {
        return;
    }

    // While playing there is no caret to place and no selection to build (playback dissolves
    // both), so the lane is a plain seek surface exactly like the waveform around it; routing
    // through SeekTimeline gives the click the same gating, snapping, and tone-follow as the
    // overlay's own click-to-seek.
    if (m_transport.state().playing)
    {
        const std::optional<common::core::TimePosition> clicked = timelineCursorPlacementTime(
            session().song().tempo_map,
            placementQuantum(),
            event.geometry.visible_timeline,
            static_cast<int>(event.geometry.bounds_width),
            event.x);
        if (clicked.has_value())
        {
            runAction(EditorAction::SeekTimeline{*clicked});
        }
        return;
    }

    ChartPointerGesture gesture;
    gesture.geometry = event.geometry;
    gesture.modifiers = event.modifiers;
    gesture.anchor_x = event.x;
    gesture.anchor_y = event.y;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    gesture.hit_target =
        chartHitTarget(*tab, event.geometry, event.x, event.y, chartPresenceFor(event, *tab));
    m_chart_gesture = gesture;

    if (!gesture.hit_target.has_value())
    {
        return;
    }

    const std::optional<ChartSelectionKey> key = chartSelectionKeyAt(*gesture.hit_target);
    if (!key.has_value())
    {
        return;
    }

    // A SATELLITE IS ITS NOTE'S HELD FACE and A BEND CHIP ITS OBJECT'S BEND, always: a click on
    // one selects its object alone with the caret on that face — the digits then state the held
    // stop, `Delete` takes the bend — the pointer twin of stepping the caret onto it. A face is
    // one object's, since the caret stands on one object; Ctrl and the double click on a head are
    // selection gestures, and a face selects its object like any other mark.
    const ChartCaretFace face = chartTargetFace(*gesture.hit_target);

    // The group a double click reaches — the notes at a head's onset, the keyframes at a
    // junction's instant — resolved the same way for the plain form and the Ctrl form.
    const auto group_at = [this, &key]() -> std::vector<ChartSelectionKey> {
        const common::core::Arrangement* const arrangement = session().currentArrangement();
        if (arrangement == nullptr || !arrangement->chart.has_value())
        {
            return {};
        }
        return chartOnsetGroupKeys(session().song().tempo_map, arrangement->chart->notes, *key);
    };

    if (event.modifiers.ctrl)
    {
        if (event.clicks >= 2)
        {
            // A double click arrives as two presses, and the first already toggled this one
            // object. The second RETRACTS that and toggles the whole group instead: the group
            // joins the selection, or leaves it when every member was already in — so
            // Ctrl+double-click on a selected chord takes the chord out and on an unselected one
            // brings it all in, rather than flipping one member twice.
            chartSelectionMutable().toggle(*key);
            chartSelectionMutable().toggleAll(group_at());
        }
        else
        {
            chartSelectionMutable().toggle(*key);
        }
        dissolveChartCaretInPlace();
        // Both multi-select gestures change the selection without passing setSelection or
        // armChartCaret, so the settle event lands here too.
        static_cast<void>(settleChart());
    }
    else if (event.clicks >= 2 && face == ChartCaretFace::BendChip)
    {
        // A double click on a bend chip RESTATES the bend it prints, the chip's own `Enter`: its
        // object alone on its chip, with the bend picker open over it (user ruling 2026-09-29).
        // The chord is still a double click away on the heads.
        const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
        armChartCaret(slot.position, slot.string, face, key);
        runAction(EditorAction::ChooseChartBend{.plane = ChartEntryPlane::Note});
    }
    else if (event.clicks >= 2)
    {
        chartSelectionMutable().replaceWith(group_at());
        dissolveChartCaretInPlace();
        static_cast<void>(settleChart());
    }
    else if (!chartSelection().contains(*key))
    {
        // Arming takes the object the press HIT as the singleton selection, on the face it hit —
        // the press knows which mark it reached, and at a shared instant the slot cannot say.
        const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
        armChartCaret(slot.position, slot.string, face, key);
    }
    else
    {
        // A press on an already-selected one keeps the standing selection (and marker) untouched
        // until the release collapses it onto what it hit — the gap a future drag-move gesture
        // lives in. Guarded rather than assumed: the gesture was set above, but the calls since
        // leave nothing a checker can tie back to that assignment.
        if (m_chart_gesture.has_value())
        {
            m_chart_gesture->collapse_on_release = true;
        }
    }
    updateView();
}

// Disambiguates an empty-lane press into a marquee once the pointer travels past the click
// threshold and republishes the marquee rectangle while it grows. Glyph-press drags are the
// future move gesture and do nothing yet.
void EditorController::Impl::onChartPointerDrag(const ChartPointerEvent& event)
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    ChartPointerGesture& gesture = *m_chart_gesture;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    if (gesture.hit_target.has_value())
    {
        return;
    }

    const bool beyond_threshold =
        std::abs(event.x - gesture.anchor_x) > g_chart_click_threshold_px ||
        std::abs(event.y - gesture.anchor_y) > g_chart_click_threshold_px;
    if (!gesture.marquee && !beyond_threshold)
    {
        return;
    }

    // The in-flight marquee leaves the marker alone: dissolution is a rule over OUTCOMES (the
    // marker model), and the outcome is unknown until release — an empty box must leave an
    // armed caret exactly where it was.
    gesture.marquee = true;
    updateView();
}

// Resolves the gesture: a marquee release selects the boxed notes (Shift extends), an
// empty-lane click arms the caret at the snapped slot, and a plain click-release on an
// already-selected note collapses the selection to it and arms the caret there.
void EditorController::Impl::onChartPointerUp(const ChartPointerEvent& event)
{
    if (!m_chart_gesture.has_value())
    {
        return;
    }

    const ChartPointerGesture gesture = *m_chart_gesture;
    m_chart_gesture.reset();

    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        updateView();
        return;
    }

    if (gesture.hit_target.has_value())
    {
        const bool clicked = std::abs(event.x - gesture.anchor_x) <= g_chart_click_threshold_px &&
                             std::abs(event.y - gesture.anchor_y) <= g_chart_click_threshold_px;
        // A completed plain click on a selected object collapses the selection to that object and
        // arms the caret there, on the face the click hit (the press deferred both while a drag
        // was still possible) — a chord member's chip as much as its head. The
        // second release of a double click leaves what the second press made standing.
        if (clicked && gesture.collapse_on_release && !gesture.modifiers.ctrl && event.clicks < 2)
        {
            if (const std::optional<ChartSelectionKey> key =
                    chartSelectionKeyAt(*gesture.hit_target);
                key.has_value())
            {
                // The collapse names that object rather than letting the slot answer, so a click
                // on a mark a head shares its instant with keeps the mark.
                const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
                armChartCaret(
                    slot.position, slot.string, chartTargetFace(*gesture.hit_target), key);
            }
        }
        updateView();
        return;
    }

    if (gesture.marquee)
    {
        const float left = std::min(gesture.anchor_x, event.x);
        const float right = std::max(gesture.anchor_x, event.x);
        const float top = std::min(gesture.anchor_y, event.y);
        const float bottom = std::max(gesture.anchor_y, event.y);
        const std::vector<ChartHitTarget> boxed = chartTargetsInBox(
            *tab, gesture.geometry, left, top, right, bottom, chartPresenceFor(event, *tab));
        std::vector<ChartSelectionKey> keys;
        keys.reserve(boxed.size());
        for (const ChartHitTarget& target : boxed)
        {
            if (const std::optional<ChartSelectionKey> key = chartSelectionKeyAt(target);
                key.has_value())
            {
                keys.push_back(*key);
            }
        }
        // Dissolution is a rule over outcomes (the marker model): a box that caught objects is
        // a multi-select outcome and demotes the caret to a cursor in its place; an empty box
        // has no selection outcome, so an armed caret survives untouched.
        //
        // The box takes the modifiers' one vocabulary: plain REPLACES, as a plain click does;
        // Shift EXTENDS; and Ctrl, the membership modifier, toggles what it boxed as one unit —
        // the box form of Ctrl+click, exactly as Ctrl+double-click is its group form — so a
        // selection built under Ctrl is never wiped by the drag that meant to grow it.
        if (!keys.empty())
        {
            if (gesture.modifiers.ctrl)
            {
                chartSelectionMutable().toggleAll(keys);
            }
            else
            {
                chartSelectionMutable().applyBox(keys, gesture.modifiers.shift);
            }
            dissolveChartCaretInPlace();
            static_cast<void>(settleChart());
        }
        updateView();
        return;
    }

    // Empty release: the caret arms at the snapped slot, whatever modifiers were held. A CLICK
    // NEVER CREATES — every object on this lane is typed, so the pointer's whole job here is to
    // say where the next digit lands — and with play-from-the-marker this arm IS the seek, the
    // selection clearing via the caret's re-derivation.
    //
    // A Ctrl release on nothing does NOTHING. Ctrl is the membership modifier — it toggles the
    // object under the pointer — and an empty slot has no member to toggle, so the press is inert
    // rather than an arm that would re-derive the selection to nothing: a large, carefully picked
    // selection must survive a misclick made while holding the very key that built it.
    if (gesture.modifiers.ctrl)
    {
        updateView();
        return;
    }
    if (const auto placement = chartPlacementAt(event); placement.has_value())
    {
        armChartCaret(placement->first, placement->second);
    }
    updateView();
}

std::optional<EditorController::Impl::FocusRow> EditorController::Impl::currentFocusRow() const
{
    if (const ChartCaret* const caret = armedChartCaret())
    {
        if (caret->lane.has_value())
        {
            return *caret->lane;
        }
        return StringFocusRow{.string = caret->string};
    }
    if (const std::optional<SelectedMarker> selected = selectedMarker(); selected.has_value())
    {
        return MarkerFocusRow{.row = selected->row};
    }
    if (std::holds_alternative<AddAutomationLaneRowSelection>(m_selection))
    {
        return AddLaneFocusRow{};
    }
    return std::nullopt;
}

bool EditorController::Impl::sameReachGroup(const FocusRow& lhs, const FocusRow& rhs)
{
    if (lhs.index() != rhs.index())
    {
        return false;
    }
    // Same kind, so only the marker rows are left to tell apart: they share one alternative and
    // each is its own group, while the strings share theirs and the lanes share theirs.
    const auto* const marker = std::get_if<MarkerFocusRow>(&lhs);
    return marker == nullptr || marker->row == std::get<MarkerFocusRow>(rhs).row;
}

std::vector<EditorController::Impl::FocusRow> EditorController::Impl::focusRowStack(
    const int string_count) const
{
    std::vector<FocusRow> stack;
    const auto push_marker_row = [this, &stack](const MarkerRow row) {
        if (!markerStarts(row).empty())
        {
            stack.emplace_back(MarkerFocusRow{.row = row});
        }
    };
    // The ruler draws its rows top down as sections, tempo, time signature.
    push_marker_row(MarkerRow::Section);
    push_marker_row(MarkerRow::Tempo);
    push_marker_row(MarkerRow::TimeSignature);
    // The hand row sits over the strings, where the lane draws its fret-hand chips along the top.
    push_marker_row(MarkerRow::Hand);
    // Strings draw with string 1 at the visual bottom, so the stack runs from the top string down.
    for (int string = string_count; string >= 1; --string)
    {
        stack.emplace_back(StringFocusRow{.string = string});
    }
    push_marker_row(MarkerRow::Tone);
    for (AutomationLaneRow& lane : visibleAutomationLaneRows())
    {
        stack.emplace_back(std::move(lane));
    }
    if (!activeToneDocumentRef().empty())
    {
        stack.emplace_back(AddLaneFocusRow{});
    }
    return stack;
}

// The vertical walk (docs/plans/completed/keyboard-focus-rows.md): rows where nothing is typed —
// the ruler's marker rows, the tone row and the "+" row — are reached by SELECTION, and rows where
// a keystroke authors a point by the caret, so the caret only ever arms on a string or a lane.
// Vertical keys keep the column; the landing decides what the destination row holds there.
void EditorController::Impl::stepFocusRow(const bool up, const bool reach, const int string_count)
{
    const std::optional<FocusRow> current = currentFocusRow();
    if (!current.has_value())
    {
        landOnRow(prepareLandingRow(string_count), std::nullopt);
        return;
    }
    // A mark and the bend chip it wears share one slot, the chip drawn above: `Up` from a mark
    // whose object prints one stands on the chip, and `Down` from the chip returns to the mark,
    // the caret re-arming on the same object so that only the face changes. `Up` from the chip
    // leaves the column like any step, and every arrival lands on a mark. Reach skips faces: it
    // jumps between groups.
    if (const ChartCaret* const caret = armedChartStringCaret(); caret != nullptr && !reach)
    {
        const ChartSlotKey slot{.position = caret->position, .string = caret->string};
        const ChartCaretFace face = chartCaretFace();
        const std::optional<ChartSelectionKey> object = chartCaretObject();
        const bool onto_chip =
            up && face == ChartCaretFace::Mark && chartFaceShown(ChartCaretFace::BendChip, object);
        if (onto_chip || (!up && face == ChartCaretFace::BendChip))
        {
            armChartCaret(
                slot.position,
                slot.string,
                onto_chip ? ChartCaretFace::BendChip : ChartCaretFace::Mark,
                object);
            return;
        }
    }
    const std::vector<FocusRow> stack = rowsFromFocus(string_count);
    const ChartCaret* const armed = armedChartCaret();
    const std::optional<common::core::GridPosition> column =
        armed != nullptr ? std::optional{armed->position} : std::nullopt;
    const auto here = std::ranges::find(stack, *current);
    if (here == stack.end())
    {
        // A row that has left the stack — a caret's lane hidden by a tone switch or a lane removal,
        // or a tone or "+" row whose track lost its regions or its tone — lands on the marker's
        // point row instead (§9b demotion posture).
        landOnRow(prepareLandingRow(string_count), column);
        return;
    }

    // The next row in the step direction, or with reach the first one past the current group.
    // Past either end nothing qualifies and the press is inert.
    for (auto target = here; up ? target != stack.begin() : std::next(target) != stack.end();)
    {
        target = up ? std::prev(target) : std::next(target);
        if (!reach || !sameReachGroup(*target, *current))
        {
            landOnRow(*target, column);
            return;
        }
    }
}

// The selection release below is the same reconciliation the column rule
// (moveCursorIntoSelectedMarker) performs for a walk OFF a marker: both establish that the CURSOR's
// tone owns the lanes before the row is judged. The walk brings the cursor to the marker; a landing
// that keeps the marker's row drops the selection instead.
EditorController::Impl::FocusRow EditorController::Impl::prepareLandingRow(const int string_count)
{
    if (armedChartCaret() == nullptr)
    {
        activateToneAtCursor();
    }
    if (const std::optional<AutomationLaneRow>& lane = chartMarkerLane(); lane.has_value())
    {
        const std::vector<AutomationLaneRow> lanes = visibleAutomationLaneRows();
        if (std::ranges::find(lanes, *lane) != lanes.end())
        {
            return *lane;
        }
    }
    return StringFocusRow{.string = std::clamp(chartMarkerString(), 1, string_count)};
}

// The trust test runs in SECONDS because that is the only currency the transport and the remembered
// musical column share — the write-back the tolerance forgives is a sample rounding, which has no
// musical spelling. Once the column is untrusted the answer is the caller's own quantum, not a
// canonical one: an arming and a holder lookup want different roundings of the same instant, and
// resolving that here rather than at the call sites is what keeps the trust rule single.
common::core::GridPosition EditorController::Impl::pausedCursorPosition(
    const common::core::Fraction quantum) const
{
    if (const auto* const passive = std::get_if<ChartCursor>(&m_chart_marker); passive != nullptr)
    {
        // Bound once so the guard and both reads are provably the same optional: the CI
        // unchecked-optional-access check cannot tie two separate member accesses together.
        const std::optional<common::core::GridPosition>& column = passive->column;
        if (column.has_value() &&
            std::abs(
                secondsAtGridPosition(session().song().tempo_map, *column) -
                m_transport.position().seconds) <= g_cursor_column_tolerance_seconds)
        {
            return *column;
        }
    }
    return nearestTempoGridPosition(session().song().tempo_map, quantum, m_transport.position());
}

void EditorController::Impl::landOnRow(
    const FocusRow& row, const std::optional<common::core::GridPosition> column,
    const ChartCaretFace face, const std::optional<ChartSelectionKey>& object)
{
    std::visit(
        common::core::Overloaded{
            [&](const StringFocusRow& string_row) {
                armChartCaret(
                    column.has_value() ? *column : pausedCursorPosition(placementQuantum()),
                    string_row.string,
                    face,
                    object);
            },
            [&](const AutomationLaneRow& lane) {
                armLaneCaret(
                    column.has_value() ? *column : pausedCursorPosition(placementQuantum()), lane);
            },
            [&](const MarkerFocusRow& marker) {
                // The caret dissolves first, so the holder found is the one under its column. The
                // stack lists only rows with markers, so a holder always exists.
                dissolveChartCaretInPlace();
                selectMarker(markerSelectionAt(
                    marker.row,
                    markerHolderIndex(
                        markerStarts(marker.row),
                        pausedCursorPosition(common::core::g_tick_quantum_note_value))));
            },
            [&](const AddLaneFocusRow&) {
                dissolveChartCaretInPlace();
                setSelection(AddAutomationLaneRowSelection{});
            },
        },
        row);
}

// The landing row is prepared here rather than by the caller, so the two arrow sites share one
// spelling of the law rather than repeating it.
void EditorController::Impl::armMarkerInPlace(const int string_count)
{
    landOnRow(prepareLandingRow(string_count), std::nullopt);
    updateView();
}

// Arrow keys on the marker (the marker model): Up/Down walk the focus rows (stepFocusRow).
// Left/Right from the passive marker — a marker row included — arm in place on the remembered row
// without stepping, except under a time selection, which they leave past its edge in their
// direction; while armed they step the union stop set on the caret's row, or jump measures under
// the reach modifier (the Guitar Pro jump). A step that takes the WALK's stop names the object the
// walk reached, at any slot, so the arrow honours the walk's order everywhere — which is how a
// ring's end statement sharing a head's instant is reachable and leavable from either side. A grid
// stop lands on the slot alone and lets it answer what stands there. Inert while playing:
// arming requires a paused transport (armed ⟹ paused is structural).
void EditorController::Impl::performActionImpl(const EditorAction::StepChartCaret& action)
{
    const ChartStepDirection direction = action.direction;
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }

    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        stepFocusRow(direction == ChartStepDirection::Up, action.reach, tab->stringCount());
        updateView();
        return;
    }

    const ChartCaret* armed = armedChartCaret();
    if (armed == nullptr)
    {
        // A standing time selection is left the way a caret leaves the span it stands on: Left
        // continues one step past the selection's start, Right one step past its end. The caret
        // arms at that edge on the remembered row, and the ordinary step below takes the one step.
        // The edge is copied first: landing releases the selection it was read from.
        const TimeSelection* const range = selectedTimeSelection();
        if (range == nullptr)
        {
            armMarkerInPlace(tab->stringCount());
            return;
        }
        const common::core::GridPosition edge =
            direction == ChartStepDirection::Left ? range->start() : range->end();
        landOnRow(prepareLandingRow(tab->stringCount()), edge);
        armed = armedChartCaret();
        if (armed == nullptr)
        {
            updateView();
            return;
        }
    }

    // Along the time axis, reach is a measure.
    const bool measure = action.reach;
    const ChartCaret caret = *armed;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const int sign = direction == ChartStepDirection::Right ? 1 : -1;
    common::core::GridPosition stepped;
    // The object a step lands ON, carried whenever the walk's stop is the one taken: at a shared
    // instant the slot holds the head too, so re-deriving would take the head every time
    // (chartObjectAt) and the caret could never reach the ring's end statement beside it.
    std::optional<ChartSelectionKey> stepped_object;
    if (measure)
    {
        // The Guitar Pro measure jump, shared with the time-selection extend so the two never
        // drift on the same motion.
        stepped = measureJumpPosition(caret.position, sign > 0);
    }
    else
    {
        // The caret steps the union stop set: the adjacent grid line OR the row's next authored
        // object — a note on this string, a point on this lane — whichever is nearer. Off-grid
        // objects are first-class stops, so a fine-placed note stays reachable from plain arrows;
        // landing on one arms onto it (selecting it) exactly like landing on an occupied grid
        // slot. The grid stop comes from the shared adjacent-line primitive the lane point nudge
        // steps with, so the two surfaces can never land on different slots for the same verb.
        stepped =
            adjacentTempoGridPosition(tempo_map, placementQuantum(), caret.position, sign > 0);
        if (const std::optional<RowObjectStop> object_stop =
                nextRowObjectStop(caret, sign > 0, false);
            object_stop.has_value())
        {
            // The object wins a TIE, so the walk's order is the arrow's order at every slot: only
            // a grid line strictly nearer than the walk's stop lands on a slot alone, which is
            // what keeps a bare digit at an empty ring's end the next note.
            const common::core::GridPosition position = object_stop->position;
            const bool grid_advanced =
                sign > 0 ? caret.position < stepped : stepped < caret.position;
            const bool grid_nearer =
                grid_advanced && (sign > 0 ? stepped < position : position < stepped);
            if (!grid_nearer)
            {
                stepped = position;
                stepped_object = object_stop->object;
            }
        }
    }
    // Time stepping is row-agnostic: a lane caret steps the same grid and keeps its row, the one
    // row rule every horizontal landing shares. EVERY ARRIVAL LANDS ON THE MARK (user ruling
    // 2026-09-29): a walk between slots is a walk between objects, and the head is the object; a
    // face — the held stop to the right, the bend chip above — is stepped onto from its own mark.
    landOnRow(prepareLandingRow(tab->stringCount()), stepped, ChartCaretFace::Mark, stepped_object);
    updateView();
}

// Tab (docs/plans/completed/keyboard-focus-rows.md, Phase 2, re-ruled in Phase 3): the next or
// previous OBJECT on the row focus stands on, the grid ignored, read from the CURSOR on every row.
// A string's objects are its notes and their keyframes (its notes alone under notes_only), a
// lane's its points, and a marker row's its marker starts: the column rule first brings the cursor
// into a marker the pointer selected elsewhere, then Tab reaches the start strictly after the
// cursor and Shift+Tab the start strictly before it — from inside a marker past its start, that is
// the marker's OWN start, the media player's "previous" — and the marker starting there is
// selected with the cursor on it. A string step always lands on the mark every object has. Where
// a ring's end
// statement and a head share an instant the statement is stepped first, so Shift+Tab from the head
// selects it and a second press leaves. Past either end, and on the "+" row,
// which holds no objects, the press is inert; from the passive marker it arms in place, as the
// arrows' first press does.
void EditorController::Impl::performActionImpl(const EditorAction::StepToRowObject& action)
{
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }

    if (const std::optional<SelectedMarker> selected = selectedMarker(); selected.has_value())
    {
        // Published even when the step is refused: the column rule may already have moved the
        // cursor, and a refusal must not leave the view showing where it stood before.
        moveCursorIntoSelectedMarker();
        if (const std::optional<common::core::GridPosition> start = adjacentPosition(
                markerStarts(selected->row),
                pausedCursorPosition(common::core::g_tick_quantum_note_value),
                action.later);
            start.has_value())
        {
            moveCursorTo(*start);
            selectMarkerStartingAt(selected->row, *start);
        }
        updateView();
        return;
    }
    if (std::holds_alternative<AddAutomationLaneRowSelection>(m_selection))
    {
        return;
    }

    const ChartCaret* const armed = armedChartCaret();
    if (armed == nullptr)
    {
        armMarkerInPlace(tab->stringCount());
        return;
    }
    if (const std::optional<RowObjectStop> stop =
            nextRowObjectStop(*armed, action.later, action.notes_only);
        stop.has_value())
    {
        // Lands on the OBJECT the walk named, never on the slot alone: at a shared instant the slot
        // holds the head too, and re-deriving would take it every time (chartObjectAt).
        landOnRow(
            prepareLandingRow(tab->stringCount()),
            stop->position,
            ChartCaretFace::Mark,
            stop->object);
        updateView();
    }
}

// The rows as the walk and the jumps see them: a marker selected with the pointer need not hold the
// cursor, so the cursor is brought inside it FIRST, and only then is the stack listed — the next
// row's holder and the lanes the stack lists are then the ones found at that marker, and a lane
// caret armed below a tone region sits inside its own tone. One call for both callers, so neither
// can list before reconciling.
std::vector<EditorController::Impl::FocusRow> EditorController::Impl::rowsFromFocus(
    const int string_count)
{
    moveCursorIntoSelectedMarker();
    return focusRowStack(string_count);
}

EditorController::Impl::FocusRow EditorController::Impl::focusRowFor(const FocusRowJump jump)
{
    switch (jump)
    {
        case FocusRowJump::Section:
            return MarkerFocusRow{.row = MarkerRow::Section};
        case FocusRowJump::Tempo:
            return MarkerFocusRow{.row = MarkerRow::Tempo};
        case FocusRowJump::TimeSignature:
            return MarkerFocusRow{.row = MarkerRow::TimeSignature};
        case FocusRowJump::Hand:
            return MarkerFocusRow{.row = MarkerRow::Hand};
        case FocusRowJump::Tone:
            return MarkerFocusRow{.row = MarkerRow::Tone};
        case FocusRowJump::AddAutomationLane:
            return AddLaneFocusRow{};
    }
    std::unreachable();
}

// A jump (Ctrl+Shift+letter) lands exactly as the walk does, through the same landing, on the row
// its letter names — and only if the stack lists that row. Stack membership is the one silence
// rule: a song with no sections lists no section row (a loaded arrangement always has a region and
// an active tone, so the tone and "+" rows are always listed), and then the press selects nothing
// and leaves an armed caret armed, where indexing the row's markers would have thrown. It is not
// entirely inert: the column rule has already brought the cursor inside a marker selected
// elsewhere, as it does before every walk step, which is why the publish below is unconditional.
// The keep-the-row landing (prepareLandingRow) is not for this: a jump changes rows.
void EditorController::Impl::performActionImpl(const EditorAction::JumpToFocusRow& action)
{
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }
    const std::vector<FocusRow> stack = rowsFromFocus(tab->stringCount());
    const FocusRow target = focusRowFor(action.row);
    if (std::ranges::find(stack, target) != stack.end())
    {
        landOnRow(target, std::nullopt);
    }
    updateView();
}

// Caret leap to a derived musical position (Home/End, PageUp/Down). Each jump resolves an absolute
// or section-relative destination and arms the caret there on the first press — no arm-at-cursor
// step first, because the whole point of these keys is the big move. The row is preserved
// (horizontal reach), so a lane caret keeps its lane and a string caret its string; without an
// armed caret yet — a marker row included — the jump measures from the paused cursor and lands on
// the remembered row. A section jump with no section in that direction is refused, not clamped, in
// line with every other refused move. Inert while playing — arming requires a paused transport.
void EditorController::Impl::performActionImpl(const EditorAction::JumpChartCaret& action)
{
    const ChartCaretJump target = action.target;
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const ChartCaret* const armed = armedChartCaret();
    const FocusRow row = prepareLandingRow(tab->stringCount());
    const common::core::GridPosition reference =
        armed != nullptr ? armed->position : pausedCursorPosition(placementQuantum());

    std::optional<common::core::GridPosition> destination;
    switch (target)
    {
        case ChartCaretJump::ChartStart:
        {
            destination = chartStartPosition();
            break;
        }
        case ChartCaretJump::ChartEnd:
        {
            destination = common::core::terminalGridPosition(tempo_map);
            break;
        }
        case ChartCaretJump::PreviousSection:
        {
            destination = adjacentSectionStop(
                session().song().sections,
                common::core::terminalGridPosition(tempo_map),
                reference,
                false);
            break;
        }
        case ChartCaretJump::NextSection:
        {
            destination = adjacentSectionStop(
                session().song().sections,
                common::core::terminalGridPosition(tempo_map),
                reference,
                true);
            break;
        }
    }

    if (!destination.has_value())
    {
        // A refused section jump keeps an armed caret exactly where it was; a first press with
        // nothing armed still arms at the reference — the paused cursor, on the row prepared
        // above — so the key always yields a caret to work from (the arrow keys' arm-first press).
        if (armed == nullptr)
        {
            landOnRow(row, reference);
            updateView();
        }
        return;
    }

    // Preserve the row: bounds and sections are horizontal reach.
    landOnRow(row, destination);
    updateView();
}

// Extends or creates the grid-locked time selection (Shift+arrows). The range is a
// mutually-exclusive selection kind, so making or extending it demotes the marker to passive and
// evicts any object selection (decision D). With a range held, the focus edge moves one `extent`
// in `direction` from the fixed anchor; with none held, the first press anchors on the marker —
// the armed caret's slot snapped to the grid (an off-grid caret's note stays inside the range), or
// the nearest grid line to the paused cursor while passive (52-Q9's recommendation) — then
// extends from there. Every endpoint is a grid position, so a boundary is never off-grid (decision
// B). A Grid or Section extend with nothing further that way refuses (the focus stays), and a
// refused first press creates no range. Inert while playing; Up/Down are ignored (the span is
// full-height).
void EditorController::Impl::performActionImpl(const EditorAction::ExtendTimeSelection& action)
{
    const TimeSelectionExtent extent = action.extent;
    const ChartStepDirection direction = action.direction;
    const common::core::ChartViewState* const tab = currentTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }
    if (direction != ChartStepDirection::Left && direction != ChartStepDirection::Right)
    {
        return;
    }
    const bool later = direction == ChartStepDirection::Right;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;

    // Anchor (fixed) + current focus (moving). An existing range keeps both; the first press seeds
    // them from the marker, grid-snapped even from an off-grid caret (the caret's note then sits
    // inside the range) or from the paused cursor while passive.
    const TimeSelection* const existing = selectedTimeSelection();
    common::core::GridPosition anchor;
    common::core::GridPosition focus;
    if (existing != nullptr)
    {
        anchor = existing->anchor;
        focus = existing->focus;
    }
    else if (const ChartCaret* const caret = armedChartCaret())
    {
        anchor = common::core::snapGridPosition(tempo_map, caret->position, placementQuantum());
        focus = anchor;
    }
    else
    {
        anchor = nearestTempoGridPosition(tempo_map, placementQuantum(), m_transport.position());
        focus = anchor;
    }

    // Move the focus one unit through the shared caret-navigation destinations, so the range edge
    // and the caret land on the same slot for the same motion.
    common::core::GridPosition next_focus = focus;
    switch (extent)
    {
        case TimeSelectionExtent::Grid:
        {
            next_focus = adjacentTempoGridPosition(tempo_map, placementQuantum(), focus, later);
            break;
        }
        case TimeSelectionExtent::Measure:
        {
            next_focus = measureJumpPosition(focus, later);
            break;
        }
        case TimeSelectionExtent::Section:
        {
            next_focus = adjacentSectionStop(
                             session().song().sections,
                             common::core::terminalGridPosition(tempo_map),
                             focus,
                             later)
                             .value_or(focus);
            break;
        }
        case TimeSelectionExtent::ChartBound:
        {
            next_focus =
                later ? common::core::terminalGridPosition(tempo_map) : chartStartPosition();
            break;
        }
    }

    // Refused (grid edge, or no section that way): a held range stays; a first press makes none.
    if (next_focus == focus)
    {
        return;
    }

    // The range dissolves the caret and evicts any object selection (decision D). The dissolution
    // seeks the transport to the caret's spot (not disarmChartMarker, which deliberately does not
    // seek), so play-from-here and a later plain arrow resume at the range rather than a stale
    // transport position; it no-ops when the marker is already passive (an existing range, or the
    // passive-anchor path). The seek leaves the transport at the anchor, which the collapse case
    // below relies on for seamless re-extension.
    dissolveChartCaretInPlace();

    // A focus that stepped exactly back onto the anchor is a shrink to zero width: clear the range
    // rather than hold an empty span (which would read as present and paint a stray edge). The
    // transport now rests at the anchor, so a further Shift+extend re-anchors here and continues in
    // the same place.
    if (next_focus == anchor)
    {
        clearSelection();
    }
    else
    {
        setSelection(TimeSelection{.anchor = anchor, .focus = next_focus});
    }
    updateView();
}

// The one selection-move intent (Alt+arrows): one editor-wide selection exists, so the move
// dispatches on its kind exactly like the Delete dispatch. With no selection at all, an armed
// caret on an empty lane slot turns the arrow into create-then-nudge (grab the curve and pull
// in one keystroke); every other combination is a silent no-op.
void EditorController::Impl::performActionImpl(const EditorAction::MoveSelection& action)
{
    const ChartStepDirection direction = action.direction;
    // The handlers below run full action dispatches that may reassign the selection variant or
    // the marker; the dispatched value is copied here so no handler ever holds a reference into
    // the object it (or a reentrant view callback) might replace. Every branch is paused-only: the
    // verb itself is refused while the transport plays, alongside the rest of the marker plane.
    //
    // A marker of any kind lives on ONE timeline row, so it moves horizontally only: the vertical
    // refusal is stated here, once, rather than by each kind's own mover. The point, chart and lane
    // branches below all give Up/Down a meaning of their own, and none of them is a marker.
    if (direction != ChartStepDirection::Left && direction != ChartStepDirection::Right &&
        selectedMarker().has_value())
    {
        return;
    }
    if (const AutomationPointSelection* const point = selectedAutomationPoint())
    {
        const AutomationPointSelection selected = *point;
        moveSelectedAutomationPoint(selected, direction);
        return;
    }
    if (const SongSectionSelection* const section = selectedSongSection())
    {
        // One measure per press, not one grid step: a section starts on a downbeat and nowhere
        // else, so the measure IS the section's step.
        const SongSectionSelection selected = *section;
        moveSelectedSongSection(selected, direction);
        return;
    }
    if (const auto* const placement = std::get_if<FretHandPositionSelection>(&m_selection))
    {
        const FretHandPositionSelection selected = *placement;
        moveSelectedFretHandPosition(selected, direction);
        return;
    }
    if (!chartSelection().empty())
    {
        moveChartSelection(direction);
        return;
    }
    if (const std::string region_id = selectedToneRegionId(); !region_id.empty())
    {
        moveSelectedToneRegionStart(region_id, direction);
        return;
    }
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && caret->lane.has_value())
    {
        const ChartCaret armed = *caret;
        createAndNudgeLanePointAtCaret(armed, direction);
    }
}

// Moves the selected chart objects: Left/Right by one placement-quantum step (a grid step while
// snap is on, a tick while it is off), and Up/Down across strings. The time move is RELATIVE, so an
// object sitting between lines keeps its offset rather than being pulled onto one — a move is not a
// snap. A refused move (edge of the neck, occupied slot, a keyframe stepped onto its neighbour or
// out of its ring) is a silent no-op — the selection stays put, matching refuse-not-clamp
// everywhere else.
//
// BOTH selection kinds are operands of the time step (W13's ruling): a note's place is its slot and
// a keyframe's is an offset along the ring it rides, so one press steps each where it lives, in one
// plan and one undo entry. The STRING step reaches notes only — a keyframe has no string of its
// own, and a selected head carries its path across by construction — so Alt+Up/Down over keyframes
// alone moves nothing: a first press plans nothing at all, and a press inside a live run records a
// step that adds no delta, which leaves the run exactly where it was.
//
// A held or repeated run is ONE GESTURE and ONE UNDO ENTRY (ruling 8, extended from the duration
// verb to this one): every press APPENDS its step to the run's list, the whole run is re-planned by
// replaying that list over the objects it STARTED on, and the entry always describes start → now.
// The entry bookkeeping is the shared gesture authority's (commitChartGestureStep), so all that is
// written here is what a MOVE step means.
//
// A step list rather than a summed delta, for the reason the duration verb keeps one: a time step
// is the placement quantum scaled by the meter where the run has REACHED, so a run crossing a meter
// change steps by two different amounts and only an ordered list replays them
// (\ref chartMoveGestureDelta). The run ends where a duration run ends — a selection change, a
// caret move, any other verb, undo/redo, a save, a committing settle — because both rest on the one
// window proof; and a reversal that replays back to the origin RETIRES its entry rather than
// leaving a Ctrl+Z that changes nothing.
void EditorController::Impl::moveChartSelection(ChartStepDirection direction)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    // The gesture this press continues, or a fresh one over what is selected NOW. Its keys are read
    // once, here, and never again: every step re-points the selection, so the live keys name where
    // the run has REACHED while the run itself always replays from the keys it started on.
    const ChartVerbWindowVerb* const live_verb = liveChartGestureVerb();
    const ChartMoveGesture* const live =
        live_verb != nullptr ? std::get_if<ChartMoveGesture>(live_verb) : nullptr;
    ChartMoveGesture gesture;
    if (live != nullptr)
    {
        gesture = *live;
    }
    else
    {
        gesture.note_keys = chartSelection().notes();
        gesture.keyframe_keys = chartSelection().keyframes();
    }
    // The step records the NOTE VALUE in force rather than a beat amount, because the meter where
    // the run has reached scales it — the same reason a duration step stores one.
    gesture.steps.push_back(
        ChartMoveStep{.note_value = placementQuantum(), .direction = direction});
    const ChartMoveDelta delta = chartMoveGestureDelta(
        session().song().tempo_map, gesture.note_keys, gesture.keyframe_keys, gesture.steps);

    // WHERE THE SELECTION LANDS, which this verb states for BOTH kinds: a note's key is its slot
    // and a keyframe's identity IS its offset, so every step re-keys what it moves, and the plan
    // carries no old-key-to-new-key map to derive that from. Saying it is also what keeps the run
    // alive — the window's proof compares the armed keys against the live selection, so the
    // re-pointing is part of the gesture rather than a courtesy after it. A keyframe on a SELECTED
    // note keeps its offset and follows that note's slot, exactly as the plan moves it.
    //
    // A stepping keyframe's landing is the PLANNER's own answer rather than the delta arithmetic,
    // because a slide-out stepped past the next head on its string parks ON that head
    // (chartSteppedKeyframeOffset): naming the unclamped offset would leave the selection — and
    // with it the next press's window proof — pointing at a keyframe nothing holds. Asked of the
    // pre-gesture chart the run replays over, which is what the landing callback is handed.
    const auto moved_slot = [this, &delta](const ChartSlotKey& slot) {
        return ChartSlotKey{
            .position = common::core::advanceGridPositionByWholeNotes(
                session().song().tempo_map, slot.position, delta.whole_notes),
            .string = slot.string + delta.strings,
        };
    };
    const auto landing = [this, &delta, &gesture, &moved_slot](
                             const common::core::Chart& pre_gesture) {
        std::vector<ChartSelectionKey> moved;
        moved.reserve(gesture.note_keys.size() + gesture.keyframe_keys.size());
        for (const ChartSlotKey& slot : gesture.note_keys)
        {
            moved.emplace_back(ChartNoteKey{.slot = moved_slot(slot)});
        }
        for (const ChartKeyframeKey& key : gesture.keyframe_keys)
        {
            const bool note_moved = std::ranges::binary_search(gesture.note_keys, key.note);
            moved.emplace_back(
                ChartKeyframeKey{
                    .note = note_moved ? moved_slot(key.note) : key.note,
                    .offset =
                        note_moved
                            ? key.offset
                            : chartSteppedKeyframeOffset(
                                  pre_gesture, session().song().tempo_map, key, delta.whole_notes),
                });
        }
        return moved;
    };

    // A caret sitting exactly on the single moved object rides along (an object stop stays under
    // the caret through its own nudge); the caret moves directly — no re-arm — so the derived
    // selection cannot widen to a chord unit mid-nudge. Measured against the LIVE selection, which
    // is where the run has reached: mid-gesture the caret sits on the step before this one, not on
    // the slot the run started from. One rule for a note and a keyframe, through the one function
    // that says where either sits.
    const bool one_note = gesture.note_keys.size() == 1 && gesture.keyframe_keys.empty();
    const bool one_keyframe = gesture.note_keys.empty() && gesture.keyframe_keys.size() == 1;
    const std::vector<ChartSelectionKey> live_keys = chartSelection().keys();
    const ChartCaret* const caret = armedChartCaret();
    bool caret_rides = false;
    if ((one_note || one_keyframe) && live_keys.size() == 1 && caret != nullptr &&
        !caret->lane.has_value())
    {
        const ChartSlotKey under = chartCaretSlotFor(session().song().tempo_map, live_keys.front());
        caret_rides = caret->position == under.position && caret->string == under.string;
    }
    // The caret rides its FACE, not just its slot: a charter on a bend chip who nudges the note
    // would otherwise find the caret back on the mark. Read before
    // the edit and copied by value, because the marker below is what the reference points into;
    // a face the moved object no longer draws is dropped where every other read drops it
    // (chartCaretFace), so this needs no test of the destination.
    const ChartCaretFace rides_face = caret_rides ? chartCaretFace() : ChartCaretFace::Mark;
    // The entry names what the run actually moves, so a lone object of either kind reads as itself
    // and anything wider reads as the selection it was.
    std::string_view label = "Move Selection";
    if (one_note)
    {
        label = "Move Note";
    }
    else if (one_keyframe)
    {
        label = "Move Keyframe";
    }
    // The gesture is COPIED into the window rather than moved: the replan reads it while the arming
    // argument is being evaluated, and a moved-from list would replay nothing.
    const bool committed = commitChartGestureStep(
        live != nullptr,
        [this, &delta, &gesture, label](const common::core::Chart& pre_gesture) {
            return planMoveSelection(
                pre_gesture,
                session().song().tempo_map,
                gesture.note_keys,
                gesture.keyframe_keys,
                delta.whole_notes,
                delta.strings,
                label);
        },
        gesture,
        landing);
    if (committed && caret_rides)
    {
        // Re-read after the edit: the selection followed the move, so it names where the caret
        // landed.
        const std::vector<ChartSelectionKey> landed_keys = chartSelection().keys();
        if (landed_keys.size() == 1)
        {
            const ChartSlotKey landed =
                chartCaretSlotFor(session().song().tempo_map, landed_keys.front());
            setArmedCaret(
                ChartCaret{
                    .position = landed.position,
                    .string = landed.string,
                    .face = rides_face,
                });
            updateView();
        }
    }
}

// Deletes the selected notes and keyframes' frets (planDeleteSelection) as one compound undo entry;
// the selection keeps only the points left standing.
void EditorController::Impl::deleteChartSelection()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    // Delete takes what the caret is ON. On a bend chip that is the BEND rather than the object:
    // the picker's "No bend" through its own planner, the notes staying at rest. On the mark, what
    // Delete took leaves no selection behind, so the empty caret can accept a new point; a point
    // Delete only took the fret from still stands with a technique of its own, and stays selected
    // and ringed like the anchors `B` and `V` leave, so its next technique is one key away. Either
    // way the selection keeps what the written chart still holds.
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    switch (chartCaretFace())
    {
        case ChartCaretFace::BendChip:
            static_cast<void>(applyChartEditPlan(
                planSetBend(
                    chart,
                    tempo_map,
                    chartSelection().notes(),
                    chartSelection().keyframes(),
                    std::nullopt,
                    "Remove Bend"),
                chartSelection().keys()));
            return;
        case ChartCaretFace::Mark:
            static_cast<void>(applyChartEditPlan(
                planDeleteSelection(
                    chart, tempo_map, chartSelection().notes(), chartSelection().keyframes()),
                chartSelection().keys()));
            return;
    }
}

// The Delete key's one dispatch: exactly one selection exists editor-wide, so Delete deletes
// whatever kind it holds. This is dispatch on the variant's alternative, not the retired
// automation-point → chart → tone-region precedence ladder — once two live selections became
// unrepresentable, there is nothing to disambiguate.
void EditorController::Impl::performActionImpl(const EditorAction::DeleteSelection&)
{
    // Copied for the same aliasing reason as the move dispatch: the delete replays a points
    // edit through a full action dispatch, which must never read back through the variant.
    if (const AutomationPointSelection* const point = selectedAutomationPoint())
    {
        const AutomationPointSelection selected = *point;
        deleteSelectedAutomationPoint(selected);
        return;
    }
    if (!chartSelection().empty())
    {
        deleteChartSelection();
        return;
    }
    deleteSelectedMarker();
}

// Deletes the selected MARKER, whatever its row, and then selects the one before it on that row —
// exactly what Shift+Tab from the deleted marker's start would land on, cursor included — so a
// run of deletes walks back along the row instead of dropping the keyboard off it. Where the
// deleted marker was the row's first, nothing is left selected, as Shift+Tab is inert there. A
// note's delete is not this rule: it leaves the caret on the emptied slot, which is the entry
// plane's own continuation. A row with no delete verb (the tempo anchor and the time signature,
// until their verbs ship) deletes nothing, and the selection stays.
void EditorController::Impl::deleteSelectedMarker()
{
    const std::optional<SelectedMarker> marker = selectedMarker();
    if (!marker.has_value() || !marker->index.has_value())
    {
        return;
    }
    const MarkerRow row = marker->row;
    const std::vector<common::core::GridPosition> before = markerStarts(row);
    if (*marker->index >= before.size())
    {
        return;
    }
    const common::core::GridPosition start = before[*marker->index];

    if (const SongSectionSelection* const section = selectedSongSection())
    {
        const SongSectionSelection selected = *section;
        deleteSelectedSongSection(selected);
    }
    else if (const auto* const placement = std::get_if<FretHandPositionSelection>(&m_selection))
    {
        const FretHandPositionSelection selected = *placement;
        deleteSelectedFretHandPosition(selected);
    }
    else if (std::string region_id = selectedToneRegionId(); !region_id.empty())
    {
        onToneRegionDeleteRequested(std::move(region_id));
    }

    const std::vector<common::core::GridPosition> after = markerStarts(row);
    if (std::ranges::contains(after, start))
    {
        return; // nothing was deleted: no verb on this row, or the commit refused
    }
    if (const std::optional<common::core::GridPosition> previous =
            adjacentPosition(after, start, false);
        previous.has_value())
    {
        moveCursorTo(*previous);
        selectMarkerStartingAt(row, *previous);
    }
    updateView();
}

// Typed digits are PROVISIONAL (the W3 pending model): the value being typed lives in the
// pending entry — drawn on the head(s), red when it cannot apply — and the chart holds nothing
// of it until the entry settles (a second digit, the window elapsing, or any other action's
// settle prologue). A first digit no second digit could extend within the fret cap needs no
// window and settles in the same keystroke, so only a leading 1 or 2 waits at the 24-fret cap.
// The flows live in their own helpers below; this dispatcher only orders them.
void EditorController::Impl::performActionImpl(const EditorAction::TypeChartFretDigit& action)
{
    const int digit = action.digit;
    if (digit < 0 || digit > 9)
    {
        return;
    }
    const std::uint32_t now_ms = m_now_milliseconds();
    // THE FIRST DIGIT DECIDES what the entry creates, and every digit after it simply widens that
    // value — re-deriving the target on every keystroke would buy nothing and cost the whole
    // entry's replan.
    if (m_chart_fret_entry.has_value() && combineChartFretEntry(digit, now_ms))
    {
        return;
    }
    // A fresh entry over what the key addresses (chartEntryTarget), planned in FULL — the pending
    // box and its red state read the outcome, so even a refused digit visibly does something —
    // then armed for a digit a second digit could extend, or settled in the same keystroke for
    // one it could not. An Invalid provisional digit still arms: under a capo every playable
    // fret's first digit alone refuses, and the window is what keeps the two-digit target
    // reachable. While the marker is passive with no selection, digits are inert by design (the
    // marker model) — a stray keystroke after listening authors nothing.
    if (std::optional<decltype(ChartFretEntry::target)> target = chartEntryTarget(action.plane);
        target.has_value())
    {
        armOrSettleChartFretEntry(plannedChartFretEntry(digit, std::move(*target), now_ms));
    }
}

// A digit while an entry is LIVE combines into it: the pending value widens to value*10+digit,
// replanned in full from the pre-entry base, and — at the current fret cap, where a second
// digit always exhausts the entry — settles immediately. An entry past its window settles
// first (a value you typed is a value you meant) and the digit falls through to a fresh flow,
// as does a combination past the fret cap: refused, never clamped, so the value already typed
// commits alone and the digit starts over.
bool EditorController::Impl::combineChartFretEntry(const int digit, const std::uint32_t now_ms)
{
    if (!m_chart_fret_entry.has_value())
    {
        return false;
    }
    // An INVALID entry is an open error state: its red box is visibly live however long it has
    // sat, so a digit always extends it. The window expiry binds valid entries only — and there
    // it is a belt, because the wake should already have settled an expired one.
    if (!m_chart_fret_entry->refused() &&
        now_ms - m_chart_fret_entry->armed_ms > g_fret_entry_window_ms)
    {
        settleChartFretEntry();
        return false;
    }
    const int combined = m_chart_fret_entry->value * 10 + digit;
    if (combined > common::core::g_max_fret)
    {
        settleChartFretEntry();
        return false;
    }
    ChartFretEntry entry = std::move(*m_chart_fret_entry);
    m_chart_fret_entry.reset();
    entry.value = combined;
    entry.armed_ms = now_ms;
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
    return true;
}

// One authority for what a pending entry would apply: an insert entry plans ONE insert carrying
// the combined value at its slot (undo removes the note), a cut entry plans the ring's division
// with the value as the new head's fret, a point entry plans the keyframe it states, and a retype
// entry replans the whole selection from the pre-entry base, so a widened value can never
// compound on its own earlier digit. Each note-stream plan carries what its settle selects — the
// head it planted, the ring site it struck or pointed, the objects it retyped — so the settle
// never reads the target again. A hand placement's retype is the one whose plan is not a
// note-stream change: its stream with the placement's fret replaced (planFretHandFret).
std::expected<EditorController::Impl::ChartFretEntryPlan, ChartPlanRefusal> EditorController::Impl::
    replanChartFretEntry(const ChartFretEntry& entry) const
{
    using Planned = std::expected<ChartFretEntryPlan, ChartPlanRefusal>;
    // Pairs a note-stream plan, or its refusal, with the selection its settle leaves.
    const auto selecting = [](std::expected<ChartEditPlan, ChartPlanRefusal> plan,
                              std::vector<ChartSelectionKey>
                                  select) -> Planned {
        if (!plan.has_value())
        {
            return std::unexpected{plan.error()};
        }
        return ChartFretNotePlan{.plan = std::move(*plan), .select = std::move(select)};
    };
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    return std::visit(
        common::core::Overloaded{
            [&](const ChartFretEntry::InsertAt& insert) -> Planned {
                common::core::ChartNote note;
                note.position = insert.slot.position;
                note.string = insert.slot.string;
                note.fret = entry.value;
                return selecting(
                    planInsertNote(chart, tempo_map, std::move(note), m_grid_note_value),
                    {ChartNoteKey{.slot = insert.slot}});
            },
            [&](const ChartFretEntry::Cut& cut) -> Planned {
                return selecting(
                    planCutRing(chart, tempo_map, cut.note, cut.offset, entry.value),
                    {ChartNoteKey{.slot = chartRingSiteSlot(cut.note, cut.offset)}});
            },
            // A point on a ring plans the keyframe it states — planted for real at the settle
            // and selected. The commit law is not asked here: a typed value the path already
            // passes through is a point that says nothing, authoring state like any such point —
            // no entry, gone when the note leaves focus. One law, one place.
            [&](const ChartFretEntry::CreateKeyframe& create) -> Planned {
                return selecting(
                    planInsertKeyframe(chart, tempo_map, create.note, create.offset, entry.value),
                    {ChartKeyframeKey{.note = create.note, .offset = create.offset}});
            },
            // No guard for an empty operand here: the planner answers NoChange for one, and
            // calling that Invalid is what armed a red pending box — the display of a REFUSAL —
            // over a press that had simply found nothing to retype. The two emptinesses stay
            // distinct, as everywhere else.
            [&](const ChartFretEntry::Retype& retype) -> Planned {
                return selecting(
                    planRetypeFrets(
                        chart,
                        tempo_map,
                        retype.base_notes,
                        retype.keys,
                        retype.keyframe_keys,
                        ChartFretSet{.fret = entry.value}),
                    chartRetypeKeys(retype));
            },
            [&](const ChartFretEntry::RetypeHandFret& hand) -> Planned {
                return planFretHandFret(hand.position, entry.value);
            },
        },
        entry.target);
}

// The uniform settle: commit the pending entry's plan when it holds one (ONE undo entry for the
// whole typed value), apply nothing on NoChange (a valid no-op), discard on Invalid (the
// previous values were never touched, so there is nothing to restore). Every action and intent
// runs this first — the prologue is what keeps the stored plan from ever going stale — and the
// window wake runs it at timeout. Undo is deliberately not special: settling first means Ctrl+Z
// on a valid pending value commits it and then undoes it, which is honest, because the value
// really was a valid edit.
void EditorController::Impl::settleChartFretEntry()
{
    if (!m_chart_fret_entry.has_value())
    {
        return;
    }
    ChartFretEntry entry = std::move(*m_chart_fret_entry);
    m_chart_fret_entry.reset();
    ++m_chart_fret_entry_wake;
    settleChartFretEntry(std::move(entry));
}

// The settle applies the plan to the store it names: a note-stream plan selects what the entry
// addressed or made (the plan carries it), so the caret stays armed on it and the next digit
// retypes it; a placement's fret commits through the marker funnel, and the placement simply stays
// selected, since its position, which names it, is not what the digits changed.
void EditorController::Impl::settleChartFretEntry(ChartFretEntry entry)
{
    if (entry.plan.has_value())
    {
        std::visit(
            common::core::Overloaded{
                [this](ChartFretNotePlan& notes) {
                    static_cast<void>(
                        applyChartEditPlan(std::move(notes.plan), std::move(notes.select)));
                },
                [this](FretHandPositionsSnapshot& placements) {
                    commitFretHandFret(std::move(placements));
                },
            },
            *entry.plan);
    }
    updateView();
}

// The one disposition rule for a freshly planned entry — a fresh digit or a combination: an
// INVALID value goes pending whatever it holds, because the red box must be SEEN, and it persists
// until a further digit, Esc, or any other intent settles it, never a timer; a value a further
// digit could still WIDEN (a leading 1 or 2 at the 24-fret cap) waits out its window; every other
// valid value settles in the same keystroke.
void EditorController::Impl::armOrSettleChartFretEntry(ChartFretEntry entry)
{
    if (entry.refused() || chartFretValueExtendable(entry.value))
    {
        armChartFretEntry(std::move(entry));
        return;
    }
    // An immediate digit settles in the same keystroke.
    settleChartFretEntry(std::move(entry));
}

// Drops the pending entry without committing — context teardown and the Esc invalid rung,
// where committing would author into a dying session or keep exactly the value Esc rejects.
void EditorController::Impl::discardChartFretEntry()
{
    if (!m_chart_fret_entry.has_value())
    {
        return;
    }
    m_chart_fret_entry.reset();
    ++m_chart_fret_entry_wake;
    updateView();
}

// Stores the entry as the live pending state and schedules its window wake.
void EditorController::Impl::armChartFretEntry(ChartFretEntry entry)
{
    m_chart_fret_entry = std::move(entry);
    ++m_chart_fret_entry_wake;
    scheduleChartFretEntryWake();
    updateView();
}

// Schedules the settle at the window's end. The stamp is the ONLY guard: a settle, discard, or
// re-arm since scheduling makes the wake stale, and a live stamp means this wake is the live
// entry's own timer, so it settles unconditionally. Deliberately NO clock re-check: an earlier
// version second-guessed the scheduler against the injected clock and no-oped without
// rescheduling, which left a marginally-early wake as a pending entry nothing would ever
// settle — a stuck state a correctness check must not be able to create. Under the tests'
// synchronous scheduler the wake therefore settles inside the arming keystroke, which is why
// every test that needs the pending state to persist uses the deferring scheduler instead.
void EditorController::Impl::scheduleChartFretEntryWake()
{
    const std::uint64_t stamp = m_chart_fret_entry_wake;
    static_cast<void>(m_message_thread_scheduler.callAfterDelay(
        std::chrono::milliseconds{g_fret_entry_window_ms}, safeCallback([this, stamp] {
            if (!m_chart_fret_entry.has_value() || stamp != m_chart_fret_entry_wake)
            {
                return;
            }
            // An INVALID value outlives its window: the red box IS the refusal display, and a
            // display that vanishes on a timer is barely a display. It stays until a further digit
            // extends it or Esc / any other intent discards it.
            if (m_chart_fret_entry->refused())
            {
                return;
            }
            settleChartFretEntry();
        })));
}

// Rationale lives on the declaration in editor_controller_impl.h.
ChartSlotKey EditorController::Impl::chartRingSiteSlot(
    const ChartSlotKey& note, const common::core::Fraction offset) const
{
    return ChartSlotKey{
        .position =
            common::core::advanceGridPosition(session().song().tempo_map, note.position, offset),
        .string = note.string,
    };
}

// Rationale lives on the declaration in editor_controller_impl.h.
const EditorController::Impl::ChartCaret* EditorController::Impl::armedChartStringCaret()
    const noexcept
{
    const ChartCaret* const caret = armedChartCaret();
    return caret != nullptr && !caret->lane.has_value() ? caret : nullptr;
}

// Rationale lives on the declaration in editor_controller_impl.h.
std::optional<decltype(EditorController::Impl::ChartFretEntry::target)> EditorController::Impl::
    chartEntryTarget(const ChartEntryPlane plane) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    // A SELECTED fret-hand placement is a selection like any other, and the digits retype what is
    // selected: here, the placement's fret. Selecting it demoted the caret, so no slot competes.
    // Both planes land here: no ring reaches a placement, so `Alt`+digit is the bare digit.
    if (const auto* const placement = std::get_if<FretHandPositionSelection>(&m_selection))
    {
        return ChartFretEntry::RetypeHandFret{.position = placement->position};
    }
    const std::vector<ChartSlotKey>& notes = chartSelection().notes();
    const std::vector<ChartKeyframeKey>& keyframes = chartSelection().keyframes();
    const bool nothing_selected = notes.empty() && keyframes.empty();
    const std::optional<ChartSlotKey> slot = chartOperandSlot();
    // THE RING PLANE FIRST, falling through to the bare key's meaning where no ring reaches the
    // slot: at a slot holding both a head and a previous ring's end, the bare key is the head and
    // the `Alt` key is the ring, with nothing between.
    const std::vector<common::core::ChartNote>& stream = arrangement->chart->notes;
    if (slot.has_value() && plane == ChartEntryPlane::Ring)
    {
        if (std::optional<decltype(ChartFretEntry::target)> ring =
                chartRingEntryTarget(stream, *slot);
            ring.has_value())
        {
            return ring;
        }
    }
    // THE NOTE PLANE ADDRESSES STOPS, and a point stating no fret has none: it is an instant on the
    // ring carrying a bend or a vibrato change. So the bare key on one alone means what it means on
    // the bare ring — a note here, cutting the ring there (its new head carries the bend in force)
    // or, at the ring's end, a head placed after it. The ring plane states the point's fret
    // instead, through the retype below.
    if (plane == ChartEntryPlane::Note && notes.empty() && keyframes.size() == 1)
    {
        const ChartKeyframeKey& point = keyframes.front();
        const common::core::ChartNote* const carrier = chartNoteAt(stream, point.note);
        const common::core::Keyframe* const standing =
            carrier == nullptr ? nullptr : common::core::standingKeyframe(*carrier, point.offset);
        if (standing != nullptr && !standing->fret.has_value())
        {
            return chartBareRingEntryTarget(
                stream, chartCaretSlotFor(session().song().tempo_map, ChartSelectionKey{point}));
        }
    }
    if (!nothing_selected)
    {
        return chartRetypeTarget(notes, keyframes);
    }
    if (!slot.has_value())
    {
        return std::nullopt;
    }
    return chartCaretEntryTarget(stream, *slot);
}

// Rationale lives on the declaration in editor_controller_impl.h.
std::optional<ChartSlotKey> EditorController::Impl::chartOperandSlot() const
{
    const ChartCaret* const caret = armedChartStringCaret();
    if (caret == nullptr)
    {
        return std::nullopt;
    }
    const std::vector<ChartSlotKey>& notes = chartSelection().notes();
    const ChartSlotKey at{.position = caret->position, .string = caret->string};
    const bool on_nothing = notes.empty() && chartSelection().keyframes().empty();
    const bool on_one_head =
        chartSelection().keyframes().empty() && notes.size() == 1 && notes.front() == at;
    if (on_nothing || on_one_head)
    {
        return at;
    }
    return std::nullopt;
}

// Rationale lives on the declaration in editor_controller_impl.h. The ring at a slot is the one
// chartPathTailAt names, the same the ring plane of the digits reaches: its instant is where the
// statement stands or will stand, whether a point is there yet or not.
ChartSelection EditorController::Impl::chartModifierAnchors(const ChartEntryPlane plane) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::optional<ChartSlotKey> slot = chartOperandSlot();
    if (arrangement == nullptr || !arrangement->chart.has_value() || !slot.has_value())
    {
        return chartSelection();
    }
    // The ring covering or ending at the slot, and the instant on it the key names.
    const std::optional<ChartPathTail> tail = chartPathTailAt(
        arrangement->chart->notes, session().song().tempo_map, slot->position, slot->string);
    ChartSelection anchors;
    // The ring plane asks the ring first: over the one selected head, the ring ENDING there.
    if (plane == ChartEntryPlane::Ring && tail.has_value())
    {
        anchors.replaceWith(ChartKeyframeKey{.note = tail->note, .offset = tail->offset});
        return anchors;
    }
    // Otherwise a selection is the operand itself.
    if (!chartSelection().empty())
    {
        return chartSelection();
    }
    // With nothing selected the note plane asks what STANDS at the slot — met only through a
    // transition that put it back under an armed caret, arming on an object selecting it — and
    // then the ring, a covered slot's only meaning for a key that modifies; an empty slot holds
    // nothing to modify.
    if (const std::optional<ChartSelectionKey> standing =
            chartObjectAt(slot->position, slot->string);
        standing.has_value())
    {
        anchors.replaceWith(*standing);
    }
    else if (tail.has_value())
    {
        anchors.replaceWith(ChartKeyframeKey{.note = tail->note, .offset = tail->offset});
    }
    return anchors;
}

// Rationale lives on the declaration in editor_controller_impl.h. An object standing at the slot
// is met with nothing selected only through a transition that put it back under an armed caret —
// arming on an object selects it.
decltype(EditorController::Impl::ChartFretEntry::target) EditorController::Impl::
    chartCaretEntryTarget(
        const std::vector<common::core::ChartNote>& notes, const ChartSlotKey& slot) const
{
    if (const std::optional<ChartSelectionKey> standing = chartObjectAt(slot.position, slot.string);
        standing.has_value())
    {
        return std::visit(
            common::core::Overloaded{
                [this](const ChartNoteKey& note) { return chartRetypeTarget({note.slot}, {}); },
                [this](const ChartKeyframeKey& keyframe) {
                    return chartRetypeTarget({}, {keyframe});
                },
            },
            *standing);
    }
    return chartBareRingEntryTarget(notes, slot);
}

// Rationale lives on the declaration in editor_controller_impl.h.
decltype(EditorController::Impl::ChartFretEntry::target) EditorController::Impl::
    chartBareRingEntryTarget(
        const std::vector<common::core::ChartNote>& notes, const ChartSlotKey& slot) const
{
    const std::optional<ChartPathTail> tail =
        chartPathTailAt(notes, session().song().tempo_map, slot.position, slot.string);
    // "A note here." On a slot a ring rings THROUGH, the head that divides it, taking the ring's
    // remainder (planCutRing). On an empty slot, or one where a ring merely stops, the head placed
    // there, whatever that ring's end states — so sequential entry never trips over a grid-step
    // note's tail. The end's own statement is the ring plane's.
    if (tail.has_value() && !tail->at_ring_end)
    {
        return ChartFretEntry::Cut{.note = tail->note, .offset = tail->offset};
    }
    return ChartFretEntry::InsertAt{.slot = slot};
}

// Rationale lives on the declaration in editor_controller_impl.h. A statement is addressed rather
// than doubled because two records on one offset is a shape no chart may hold. From the keyboard
// this is how the END's own statement is reached at all: no landing addresses it (chartObjectAt).
std::optional<decltype(EditorController::Impl::ChartFretEntry::target)> EditorController::Impl::
    chartRingEntryTarget(
        const std::vector<common::core::ChartNote>& notes, const ChartSlotKey& slot) const
{
    const std::optional<ChartPathTail> tail =
        chartPathTailAt(notes, session().song().tempo_map, slot.position, slot.string);
    if (!tail.has_value())
    {
        return std::nullopt;
    }
    const common::core::ChartNote* const carrier = chartNoteAt(notes, tail->note);
    if (carrier == nullptr)
    {
        return std::nullopt;
    }
    if (standingKeyframe(*carrier, tail->offset) != nullptr)
    {
        return chartRetypeTarget(
            {}, {ChartKeyframeKey{.note = tail->note, .offset = tail->offset}});
    }
    return ChartFretEntry::CreateKeyframe{.note = tail->note, .offset = tail->offset};
}

// The snapshot covers every note the entry writes THROUGH rather than only the notes it addresses,
// because a keyframe is stored inside its note: a selection naming only a point on a slide still
// needs that slide's pre-entry record to replan from, with its head's own fret left alone.
EditorController::Impl::ChartFretEntry::Retype EditorController::Impl::chartRetypeTarget(
    std::vector<ChartSlotKey> keys, std::vector<ChartKeyframeKey> keyframe_keys) const
{
    std::vector<common::core::ChartNote> base_notes =
        chartNotesForKeys(notesTouchedBy(keys, keyframe_keys));
    return ChartFretEntry::Retype{
        .keys = std::move(keys),
        .keyframe_keys = std::move(keyframe_keys),
        .base_notes = std::move(base_notes),
    };
}

std::vector<ChartSelectionKey> EditorController::Impl::chartRetypeKeys(
    const ChartFretEntry::Retype& retype)
{
    std::vector<ChartSelectionKey> keys;
    keys.reserve(retype.keys.size() + retype.keyframe_keys.size());
    for (const ChartSlotKey& note : retype.keys)
    {
        keys.emplace_back(ChartNoteKey{.slot = note});
    }
    for (const ChartKeyframeKey& keyframe : retype.keyframe_keys)
    {
        keys.emplace_back(keyframe);
    }
    return keys;
}

// Planned in FULL at once — the pending box and its red state read the outcome, so even a refused
// value visibly does something.
EditorController::Impl::ChartFretEntry EditorController::Impl::plannedChartFretEntry(
    const int value, decltype(ChartFretEntry::target) target, const std::uint32_t now_ms) const
{
    ChartFretEntry entry{.value = value, .target = std::move(target), .armed_ms = now_ms};
    entry.plan = replanChartFretEntry(entry);
    return entry;
}

// Rationale lives on the declaration in editor_controller_impl.h.
std::optional<ChartSlotKey> EditorController::Impl::chartFretEntryCreationSlot(
    const decltype(ChartFretEntry::target)& target) const
{
    return std::visit(
        common::core::Overloaded{
            [](const ChartFretEntry::InsertAt& insert) {
                return std::optional<ChartSlotKey>{insert.slot};
            },
            [this](const ChartFretEntry::Cut& cut) {
                return std::optional<ChartSlotKey>{chartRingSiteSlot(cut.note, cut.offset)};
            },
            [this](const ChartFretEntry::CreateKeyframe& create) {
                return std::optional<ChartSlotKey>{chartRingSiteSlot(create.note, create.offset)};
            },
            [](const ChartFretEntry::Retype&) { return std::optional<ChartSlotKey>{}; },
            [](const ChartFretEntry::RetypeHandFret&) { return std::optional<ChartSlotKey>{}; },
        },
        target);
}

// Rationale lives on the declaration in editor_controller_impl.h.
//
// Settled in this keystroke rather than armed: the value arrives WHOLE, so there is no digit left
// to widen it and no provisional value to show — which is also why a refusal shows no red box
// here, the key having typed nothing the charter can see undone. A retype target is where the
// digit would ADDRESS what stands — the selection, the object under the caret, a statement already
// on the ring — and `Insert` selects it instead.
void EditorController::Impl::insertAtChartCaret(const ChartEntryPlane plane)
{
    std::optional<decltype(ChartFretEntry::target)> target = chartEntryTarget(plane);
    if (!target.has_value())
    {
        return;
    }
    if (const auto* const retype = std::get_if<ChartFretEntry::Retype>(&*target))
    {
        chartSelectionMutable().replaceWith(chartRetypeKeys(*retype));
        updateView();
        return;
    }
    const std::optional<ChartSlotKey> slot = chartFretEntryCreationSlot(*target);
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (!slot.has_value() || arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    const int fret =
        chartFretInForceAt(arrangement->chart->notes, session().song().tempo_map, *slot);
    settleChartFretEntry(plannedChartFretEntry(fret, std::move(*target), m_now_milliseconds()));
}

// `Insert`: the note plane at the fret in force — a head on an empty slot or at a ring's end, the
// cut inside a ring, the head under the caret selected — or the lane's own point.
void EditorController::Impl::performActionImpl(const EditorAction::InsertAtCaret&)
{
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr)
    {
        return;
    }
    if (caret->lane.has_value())
    {
        // Copied so the planting (a full action dispatch that re-points the selection and may
        // touch the marker) never reads back through the marker variant it aliases.
        const ChartCaret armed = *caret;
        insertLanePointAtCaret(armed);
        return;
    }
    insertAtChartCaret(ChartEntryPlane::Note);
}

// `Alt+Insert`: the ring plane at the fret in force — strictly inside a ring the silent point
// typing the note's own fret makes, at its END the end statement at that fret (a slide-out toward
// the fret in force, or the ARRIVAL the chart then proves where a head at that stop abuts —
// common::core::arrivesIntoNextHead — a shift slide in one key), a statement already standing
// there selected, and where no ring reaches the slot exactly what `Insert` does.
//
// THE REVEAL IS IN THE CHORD because the slot before a head can look blank while lying inside a
// ring's ending zone, past its ink end, and this key states a point on exactly that stretch: with
// `Alt` held the ring is drawn to its end, so the charter sees what they are inserting onto.
void EditorController::Impl::performActionImpl(const EditorAction::InsertRingPoint&)
{
    insertAtChartCaret(ChartEntryPlane::Ring);
}

// The full note values behind a sorted key set, in chart order — the one selection-snapshot
// loop the typing and fret-shift verbs share.
std::vector<common::core::ChartNote> EditorController::Impl::chartNotesForKeys(
    const std::vector<ChartSlotKey>& keys) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return {};
    }
    return notesForKeys(arrangement->chart->notes, keys);
}

// Shifts every selected stop's fret by one (Alt+Shift+wheel), shape-preserving by
// construction: the verb names its delta and nothing else, and the planner moves every stop the
// selection addresses by it — selected KEYFRAMES included, since a point on a slide states a fret
// exactly as a head does (W13's ruling). A shift pushing any stop below zero or past the cap is
// refused by the planner, never clamped.
void EditorController::Impl::performActionImpl(const EditorAction::ShiftChartFrets& action)
{
    const int direction = action.direction;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || direction == 0)
    {
        return;
    }

    const std::vector<ChartSlotKey>& note_keys = chartSelection().notes();
    const std::vector<ChartKeyframeKey>& keyframe_keys = chartSelection().keyframes();
    static_cast<void>(applyChartEditPlan(planRetypeFrets(
        *arrangement->chart,
        session().song().tempo_map,
        chartNotesForKeys(notesTouchedBy(note_keys, keyframe_keys)),
        note_keys,
        keyframe_keys,
        ChartFretShift{.delta = direction > 0 ? 1 : -1})));
}

// Grows or shrinks the selection's rings by one step — moving each ring's END onto the adjacent
// line of the placement quantum's lattice — as ONE GESTURE: every press APPENDS its step to the
// run's list, the whole selection is re-planned by replaying that list over the rings the gesture
// STARTED at, and the run stays one undo entry that always describes start → now. That is what
// makes the verb symmetric: a chord member pinned at its own bound on the way out rejoins its
// neighbours exactly where it left them on the way back, instead of each step baking the clamp into
// the next step's starting value.
//
// A list rather than a single accumulated delta: a step has no size to sum, because what it adds is
// whatever reaches the next line from where the ring's end currently sits — a summed delta carries
// a remainder through every later step, leaving the ring permanently between lines.
//
// The gesture is live while the shared window proof holds (the same selection, and the burst record
// still owning the history top), so it ends at every commit point the technique toggle ends at —
// and a press after any of them starts a new gesture from the current values. The step arithmetic
// and the ring rules are the planner's alone (planAdjustSustain), and the entry bookkeeping is the
// shared gesture authority's (commitChartGestureStep), which the move verb runs through too.
void EditorController::Impl::performActionImpl(const EditorAction::AdjustChartSustain& action)
{
    const int direction = action.direction;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty() ||
        direction == 0)
    {
        return;
    }
    // Bound once, right behind the guard, so every read below is provably behind it — the shape
    // this file uses wherever an optional's guarantee has to survive intervening calls.
    const common::core::Chart& live_chart = *arrangement->chart;

    // The gesture this press continues, or a fresh one. The pointer dies with any reassignment of
    // the window, so the steps are copied out of it here, before anything below can touch it.
    const ChartVerbWindowVerb* const live_verb = liveChartGestureVerb();
    const ChartSustainGesture* const live =
        live_verb != nullptr ? std::get_if<ChartSustainGesture>(live_verb) : nullptr;
    // Steps made against different lattices mix freely inside one gesture (a grid change or a snap
    // toggle mid-run); the list keeps them in the order they were pressed, which is the only order
    // that replays what the user did.
    ChartSustainGesture gesture;
    if (live != nullptr)
    {
        gesture = *live;
    }
    // The step records the NOTE VALUE it snapped by rather than a beat amount, because the planner
    // snaps the ring's end onto that lattice's own lines: the meter at whatever measure the end
    // lands in scales the value there, so nothing here needs to know where any ring ends.
    gesture.steps.push_back(
        ChartSustainStep{.note_value = placementQuantum(), .grow = direction > 0});

    // The operand is every note the selection reaches, through either kind: a keyframe sits on the
    // TAIL, and this is the verb that acts on the tail, so a selected junction grows or shrinks the
    // ring it rides — the end of a slide is exactly where a charter stands when the tail wants
    // pulling out. The head verbs (mute, accent, the techniques) deliberately do not reach through
    // a keyframe; they act on the onset, which a point on the tail says nothing about.
    //
    // No select_exactly: a duration step rewrites its notes IN PLACE, so every key stays where it
    // was and the plan's own follow is exactly right.
    static_cast<void>(commitChartGestureStep(
        live != nullptr,
        [this, &live_chart, &gesture](const common::core::Chart& pre_gesture) {
            return planAdjustSustain(
                live_chart,
                session().song().tempo_map,
                pre_gesture.notes,
                notesTouchedBy(chartSelection().notes(), chartSelection().keyframes()),
                gesture.steps);
        },
        gesture));
}

// ONE step of a coalescing gesture, whichever verb's — the duration run, the move run, and whatever
// joins them. The caller has already appended this press to the run's step list; this replays the
// whole list over the state the run STARTED at and folds the answer into the single entry the run
// owns: the first step PUSHES it, every later step REPLACES it, so one Ctrl+Z always undoes the
// whole run and the entry always describes start → now.
//
// The start state needs no snapshot of its own. The entry the first step pushed already holds the
// pre-gesture values, so reversing it IS the state the gesture began at — the settle fold's own
// method, and the reason a second copy could only disagree with it. The whole CHART rather than its
// notes alone, because a plan is reversed against a chart and a planner may want more of it.
//
// continues: whether this press proved it continues a live run of its OWN verb (the shared window
// proof, asked by the caller through liveChartGestureVerb). replan: the plan describing the whole
// run, given the state it started from. verb: what the next press must match to continue this run.
// landing: where the run's objects have LANDED, for a verb whose steps re-key them; absent leaves
// the plan's default follow to it, which is right for every verb that rewrites in place. Asked of
// the pre-gesture chart, exactly like the replan, because a landing a bound HELD is a fact about
// that same state.
bool EditorController::Impl::commitChartGestureStep(
    const bool continues, const ChartGestureReplan& replan, ChartVerbWindowVerb verb,
    const ChartGestureLanding& landing)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return false;
    }
    const common::core::Chart& live_chart = *arrangement->chart;
    // A live gesture and the burst record are present together — the window proof demands the
    // record — so this pointer is exactly "a run is in progress". Bound once so every read below is
    // provably behind the check.
    ChartNotesTopEntry* const burst =
        continues && m_chart_notes_top.has_value() ? &*m_chart_notes_top : nullptr;

    // A press that STARTS a run needs no reconstruction and takes the live chart itself.
    common::core::Chart reconstructed;
    const common::core::Chart* pre_gesture = &live_chart;
    if (burst != nullptr)
    {
        reconstructed = live_chart;
        if (!applyChartChange(reconstructed, burst->plan.reversed()).has_value())
        {
            reportError("Could not apply chart edit: " + burst->plan.label);
            return false;
        }
        pre_gesture = &reconstructed;
    }

    std::optional<std::vector<ChartSelectionKey>> select_exactly;
    if (landing)
    {
        select_exactly = landing(*pre_gesture);
    }
    std::expected<ChartEditPlan, ChartPlanRefusal> plan = replan(*pre_gesture);
    if (!plan.has_value())
    {
        // Invalid is the gate refusing the result, so this step never happened: it is not recorded
        // (the appended list is the caller's local until the window is armed below), and a running
        // gesture keeps the entry and the steps it had.
        //
        // NoChange is the gesture standing exactly where it started — every object already at its
        // bound on a first step, or a run that replayed back to its start — so it describes no edit
        // at all. A first step then arms nothing, and the next press in the other direction starts
        // from the current values rather than paying back steps that never moved anything; a
        // running gesture RETIRES the entry it pushed, because an entry describing nothing is a
        // dead Ctrl+Z on a document reported modified that is byte-identical to the saved file.
        if (plan.error() == ChartPlanRefusal::NoChange && burst != nullptr)
        {
            // The selection goes back with the chart, landed once it is written. A verb whose
            // steps re-key its objects has been pointing at where the run had reached, and a
            // replay describing nothing is exactly the case where that landing IS the start.
            retireChartGesture(burst->plan);
            landChartSelection(select_exactly.value_or(chartSelection().keys()));
            updateView();
            // The chart MOVED — back to where the run began — so this is a step the caller must
            // follow exactly as it follows any other: a caret riding the lone object rides it home.
            return true;
        }
        return false;
    }

    // A replayed run that describes EXACTLY the plan its entry already holds moved nothing — every
    // object pinned at a bound or held at a floor — so the press is not recorded: the caller's step
    // list is still its own local until the window is armed below, and a running gesture keeps the
    // entry and the steps it had. Recording it would bank an overshoot the charter cannot see and
    // would have to pay back, press by press, before the next visible step. The label is
    // deliberately not compared: it names the run's NET direction, and a run standing still has no
    // new direction to name.
    //
    // Stated here rather than per verb because it is a fact about the GESTURE, not about any verb's
    // rules: the entry's plan is the only record of where the run has reached, and the shared
    // authority is the one place holding it. It is what makes a clamp safe to coalesce — the move
    // verb's slide-out parks on the next head on its string, so every further press that way is a
    // no-op that must cost nothing to come back from.
    if (burst != nullptr && plan->removed == burst->plan.removed &&
        plan->inserted == burst->plan.inserted)
    {
        return false;
    }

    // A run whose whole replay writes as nothing — a silent point stepped along its tail, or a
    // stated one stepped back onto the path — has no entry to keep: a first step applies with none
    // (applyChartEditPlan), and a running one RETIRES its entry and then applies the same way, so
    // the chart moves while the history stays where the run found it. The record goes with the
    // retire, so `burst` is not read past it.
    ChartEditPlan written = writtenChartPlan(*plan);
    const bool retired = burst != nullptr && written.empty();
    if (retired)
    {
        retireChartGesture(burst->plan);
    }
    if (burst == nullptr || retired)
    {
        if (!applyChartEditPlan(std::move(plan), select_exactly))
        {
            return false;
        }
    }
    else
    {
        // The model moves first and the history follows, the order every chart edit takes
        // (applyChartEditPlan), so the entry records where the step LEFT the charter. Walk the
        // live chart back to the pre-gesture stream and then to the re-planned one, so the state
        // the top entry describes is exactly the state the chart holds.
        const std::optional<bool> walked =
            m_session.writeChart([&burst, &plan](common::core::Chart& chart) {
                return applyChartChange(chart, burst->plan.reversed()).has_value() &&
                       applyChartChange(chart, *plan).has_value();
            });
        if (!walked.value_or(false))
        {
            reportError("Could not apply chart edit: " + plan->label);
            return false;
        }
        // The replace path has no plan-driven selection follow of its own (that lives in
        // applyChartEditPlan, which only the first step runs), so a verb whose step re-keys its
        // objects states the landing here or leaves the next press holding keys naming nothing.
        landChartSelection(select_exactly.value_or(chartSelection().keys()));
        // The run began where the burst record says; it now leaves the charter on its landing.
        std::optional<ChartEditFocus> after = chartEditFocusOf(chartSelection().keys());
        if (replaceUndoTop(std::make_unique<ChartEdit>(std::move(written), burst->before, after)))
        {
            // The burst record follows the entry it names, or the next step would reverse a plan
            // the history no longer holds.
            burst->plan = std::move(*plan);
            burst->after = std::move(after);
        }
        else
        {
            m_chart_notes_top.reset();
        }
        updateView();
    }

    // Both paths arm the same window: the live selection, and the gesture the next press continues.
    // Read back from the selection rather than carried across the apply, so the keys are always the
    // ones the next press will compare against.
    m_chart_verb_window = ChartVerbWindow{
        .keys = chartSelection().keys(),
        .verb = std::move(verb),
    };
    return true;
}

// Disarms whichever verb's coalescing window is armed. Called from each COMMIT point — a selection
// change, a caret move, an edit, undo/redo, a settling sweep — so a press after any of them means
// the verb's ordinary law: a technique toggle that sets or clears rather than reversing, and a
// duration step that starts a new gesture from the current rings.
void EditorController::Impl::disarmChartVerbWindow() noexcept
{
    m_chart_verb_window.reset();
}

// The proof both windows rest on, so neither verb restates it: the armed selection is still the
// live one, and the burst record still owns the history top. Any other push, undo, or redo moves
// the cursor and retires the record, which is why no verb keeps a plan of its own to agree with
// that record by hand.
bool EditorController::Impl::chartVerbWindowHolds(
    const std::vector<ChartSelectionKey>& armed_keys) const
{
    return m_chart_notes_top.has_value() && armed_keys == chartSelection().keys() &&
           m_undo_history.snapshot().position == m_chart_notes_top->history_position;
}

// The gesture a press may continue, or nullptr when it must start a new one. The pointer aliases
// m_chart_verb_window, so a caller must copy what it needs before anything can reassign that field.
//
// Beyond the shared proof it asks the fold's own precondition: a save mid-gesture makes the entry
// the file's clean state, and replaceTop refuses to rewrite that (widening it would make "return to
// clean" restore different content than the file holds). So a save ENDS the gesture, exactly like
// any other commit point, and the next step opens a fresh one from the saved values.
//
// WHICH verb's gesture is the caller's own question, asked of the returned variant. A window this
// press does not own is a window this press ENDS, which the caller does by arming its own over it —
// the same "this press is a different verb, so whatever it interrupts is over" the toggle reversal
// states one function down.
const EditorController::Impl::ChartVerbWindowVerb* EditorController::Impl::liveChartGestureVerb()
    const
{
    if (!m_chart_verb_window.has_value() || !chartVerbWindowHolds(m_chart_verb_window->keys) ||
        m_undo_history.isAtCleanState())
    {
        return nullptr;
    }
    return &m_chart_verb_window->verb;
}

// Ends a gesture that describes nothing, by taking back the entry its first step pushed and walking
// the chart back to the stream that entry was applied to. What a run replaying back to its start
// has to leave behind: an entry describing nothing is a dead Ctrl+Z, and it would report the
// document modified while it is byte-identical to the saved file.
//
// This is the technique toggle's own "the pair leaves no trace" mechanism (dropTop). It needs no
// clean-state alternative, which that verb does need, because a save ends the gesture BEFORE a step
// can reach here — liveChartGestureVerb refuses at the clean state, so a live gesture and dropTop's
// preconditions are the same thing.
//
// applied: the plan the entry holds, which is why the record naming it is retired last.
void EditorController::Impl::retireChartGesture(const ChartEditPlan& applied)
{
    // The model moves first and the history follows, the order every chart edit takes
    // (applyChartEditPlan).
    const std::optional<bool> reversed =
        m_session.writeChart([&applied](common::core::Chart& chart) {
            return applyChartChange(chart, applied.reversed()).has_value();
        });
    if (!reversed.value_or(false))
    {
        reportError("Could not apply chart edit: " + applied.label);
        return;
    }
    static_cast<void>(dropUndoTop());
    // The entry both the record and the window name is gone, so both go with it: the next press
    // opens a fresh gesture from rings that ARE the pre-gesture rings. (Between the drop and here
    // the record is already inert — every reader proves ownership by the history position first.)
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    updateView();
}

// The chart verbs' toggle window (D14 ruling 4), shared by every verb that has one rather than
// copied into each: while the selection and the burst record still prove the previous press was
// this verb's own entry, this press REVERSES that entry exactly, so the pair leaves no trace —
// including tails an assist grew, which a verb's own clear law could never restore.
//
// ALWAYS disarms, reversal or not: a press whose proofs fail commits the previous entry, which is
// what makes the window end at the next selection change or caret move. A window ANOTHER verb
// armed disarms here too, for the same reason — this press is a different verb, so whatever it
// interrupts is over.
//
// pressed: the verb pressed now, compared whole; only a press of the verb that armed the window
// reverses. revert_label: what the reversal entry is called when the save-mid-window path needs
// one. Returns true when this press was consumed by a reversal, so the caller must not plan.
bool EditorController::Impl::reverseChartVerbWindow(
    const ChartVerbWindowVerb& pressed, const std::string_view revert_label)
{
    if (!m_chart_verb_window.has_value())
    {
        return false;
    }
    const ChartVerbWindow window = std::move(*m_chart_verb_window);
    m_chart_verb_window.reset();
    if (window.verb != pressed)
    {
        return false;
    }
    const std::vector<ChartSelectionKey>& armed_keys = window.keys;
    // Bound once so every read below is provably behind the has_value check, the shape this file
    // uses wherever an optional's guarantee has to survive intervening calls.
    const ChartNotesTopEntry* const burst =
        m_chart_notes_top.has_value() ? &*m_chart_notes_top : nullptr;
    if (burst == nullptr || !chartVerbWindowHolds(armed_keys))
    {
        return false;
    }
    const ChartEditPlan applied = burst->plan;
    // The model moves first and the history follows, the order every chart edit takes
    // (applyChartEditPlan).
    //
    // A save mid-window makes the entry the file's clean state, so erasing it would make "return
    // to clean" a lie. The reversal still happens — the toggle stays genuine and the grown tail
    // comes back — but as its own inverse entry, which leaves the session correctly dirty.
    const bool clean_entry = m_undo_history.isAtCleanState();
    m_chart_notes_top.reset();
    ChartEditPlan reversal = applied.reversed();
    reversal.label = std::string{revert_label};
    const std::optional<bool> reversed =
        m_session.writeChart([&reversal](common::core::Chart& chart) {
            return applyChartChange(chart, reversal).has_value();
        });
    if (!reversed.value_or(false))
    {
        reportError("Could not apply chart edit: " + applied.label);
        return true;
    }
    if (clean_entry)
    {
        // The written form, like every entry; a record exists only for a plan that wrote as
        // something, and the reversal of such a plan writes as its reverse. The reversal keeps
        // the selection, so the charter stands in one place on both sides of it.
        const std::optional<ChartEditFocus> here = chartEditFocusOf(chartSelection().keys());
        pushUndoEntry(std::make_unique<ChartEdit>(writtenChartPlan(reversal), here, here));
    }
    else
    {
        static_cast<void>(dropUndoTop());
    }
    updateView();
    return true;
}

// Rationale lives on the declaration in editor_controller_impl.h.
bool EditorController::Impl::chartFretEntryContinuedBy(const EditorAction::Action& action) const
{
    return std::holds_alternative<EditorAction::TypeChartFretDigit>(action);
}

// The node rows over the CURRENT selection, read off the member whose label names the most nodes
// (the first such in chart order) and named so the view can anchor on that head. One member speaks
// for the scope because the rows carry PARTIALS and the planner binds a chosen partial on every
// member whose own label offers it: a chord of 5s and 7s offers the 5's rows, and choosing the 4th
// partial touches each 5 at 4.98 while each 7 takes its single node. The printed value is that
// member's stop plus each candidate's offset, so a row and the head it will produce print the same
// number, and the row that member is touching now is marked current.
//
// A carrier's label is the fret its node lies at (chartHarmonicNodeCandidates reads it so), which
// is how a note already touching 4.98 is offered the 13th and 15th partials of a 5. Which rows
// CHANGE anything, and whether a clear is on offer, is not asked here: the verb asks the planner.
//
// By INDEX rather than through `notesForKeys`, which copies each note whole: a large selection
// would otherwise pay a note copy per member to answer a question about a few numbers.
std::optional<ChartHarmonicNodePicker> EditorController::Impl::chartHarmonicNodePicker() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::Chart& chart = *arrangement->chart;
    std::optional<ChartHarmonicNodePicker> picker;
    for (const std::size_t index : slotIndicesForKeys(chart.notes, chartSelection().notes()))
    {
        const common::core::ChartNote& note = chart.notes[index];
        const std::vector<common::core::HarmonicNodeCandidate> candidates =
            chartHarmonicNodeCandidates(note, chart.tuning, session().song().tempo_map);
        if (candidates.empty() ||
            (picker.has_value() && candidates.size() <= picker->choices.size()))
        {
            continue;
        }
        // The stop each candidate is measured from, asked exactly as the planner asks it.
        common::core::ChartNote unpressed = note;
        unpressed.fret = 0;
        const auto stop =
            static_cast<double>(common::core::physicalStopFret(unpressed, chart.tuning.capo));
        // Bound to a local so the presence test and the read are provably one object.
        const std::optional<double>& touching = note.harmonic_node;
        picker = ChartHarmonicNodePicker{.note = index, .choices = {}, .preselected = 0};
        picker->choices.reserve(candidates.size() + 1);
        for (const common::core::HarmonicNodeCandidate& candidate : candidates)
        {
            const double node = stop + candidate.position;
            picker->choices.emplace_back(
                ChartHarmonicNodeChoice{
                    .node = node,
                    .partial = candidate.partial,
                    // Exact, the form coding-conventions states: the set writes exactly this sum,
                    // and import snaps onto the same table, so a touched node either IS a row's
                    // value or names no row at all.
                    .current = touching.has_value() && std::is_eq(*touching <=> node),
                });
        }
    }
    return picker;
}

// The fret-hand harmonic verb (`H`). Not a toggle: its set states a VALUE — which node the finger
// touches — and a label usually names several, so the verb offers every CHANGE the selection allows
// and asks only when there is more than one. WHAT CHANGES ANYTHING IS THE PLANNER'S ANSWER: each
// node row and the clear are planned over the live chart, and a plan of NoChange is a row that
// would do nothing — the node the anchor already touches with no other member to move, or a clear
// with nothing carried. Counting rows by hand instead was a second model of the same question, and
// it disagreed with the first. One change applies at once — a 12 writes its single node, a 12
// already touching it clears — and several open the picker through the view port, committing
// nothing; the chosen row returns through SetChartHarmonicNode. Every node row is shown so the tick
// can say where the finger is, and Return takes what a toggle would have done: the clear when every
// member carries a harmonic, else the lowest partial that changes something. A selection naming
// nothing (open strings, pinches) is inert.
//
// Nothing here disarms another verb's window: opening a menu is a question, not an edit, so a
// gesture some other verb has staged ends when a CHOICE writes (applyChartEditPlan's disarm), not
// when the rows open — an escaped menu leaves the chart, and that gesture, exactly as it found
// them.
void EditorController::Impl::performActionImpl(const EditorAction::ChooseChartHarmonic&)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    std::optional<ChartHarmonicNodePicker> picker = chartHarmonicNodePicker();
    if (!picker.has_value())
    {
        // No member's label names a node — open strings, pinches — so nothing is on offer. No
        // carrier lands here: a touching note's label is the fret its node lies at, which names
        // that node among its rows (the candidate window is the rounding window), and a pressed
        // note carrying an imported artificial node keeps the rows its pressed fret names.
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::vector<ChartSlotKey> keys = chartSelection().notes();
    // Bound once above the loop, the shape this file uses wherever an optional's guarantee has to
    // survive a loop the checker cannot see through.
    ChartHarmonicNodePicker& rows = *picker;
    // The answers that would write, in row order, each spelled as the choice that returns it.
    std::vector<std::optional<int>> changes;
    std::optional<std::size_t> first_changing_row;
    // Every row is a node row here: the clear, if offered, is inserted AFTER this loop.
    for (std::size_t row = 0; row < rows.choices.size(); ++row)
    {
        const int partial = std::get<ChartHarmonicNodeChoice>(rows.choices[row]).partial;
        if (planSetHarmonic(chart, tempo_map, keys, partial, "Harmonic").has_value())
        {
            changes.emplace_back(partial);
            if (!first_changing_row.has_value())
            {
                first_changing_row = row;
            }
        }
    }
    // The clear leads the list wherever it is offered, on every selection alike, so the row never
    // moves between states: where it opens selected, Down walks the partials from the lowest, the
    // likeliest next choice, instead of Up landing on the highest.
    const bool clear_offered =
        planClearHarmonic(chart, tempo_map, keys, "Remove Harmonic").has_value();
    if (clear_offered)
    {
        changes.emplace_back(std::nullopt);
        rows.choices.emplace(rows.choices.begin(), ChartHarmonicClearChoice{});
        if (first_changing_row.has_value())
        {
            ++*first_changing_row;
        }
    }
    if (changes.empty())
    {
        return;
    }
    if (changes.size() == 1)
    {
        commitChartHarmonic(changes.front());
        return;
    }
    // Return's row: the clear when it is offered and every member carries a harmonic — what a
    // toggle would have done — else the first changing node row, the lowest partial that does
    // anything, which skips a ticked row the anchor is already standing on. Asked of the clear row
    // that EXISTS rather than of the notes alone, so a clear the gate refused can never leave
    // Return on a row that is not there.
    const bool all_carry = std::ranges::all_of(
        slotIndicesForKeys(chart.notes, keys),
        [&chart](const std::size_t index) { return carriesNeckHarmonic(chart.notes[index]); });
    if (clear_offered && all_carry)
    {
        rows.preselected = 0;
    }
    else if (first_changing_row.has_value())
    {
        // Always the case here: two or more changes with at most one clear among them means some
        // node row changes something.
        rows.preselected = *first_changing_row;
    }
    requestChartHarmonicNodePicker(std::move(*picker));
}

// The row chosen in the picker the harmonic verb asked the view to open: a partial, or no harmonic.
// A row is already a deliberate choice, so it applies at once, as the step of the run a choiceless
// press would have taken.
void EditorController::Impl::performActionImpl(const EditorAction::SetChartHarmonicNode& action)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    commitChartHarmonic(action.partial);
}

// The harmonic run's step, in the gesture family rather than the toggle's: the latest choice IS the
// gesture, so the replan reads it from this press and the window carries only the verb's name. A
// second choice on the same selection replaces the first step's entry; choosing the way back to the
// pre-gesture state plans NoChange against it and retires the entry, which is how `H` Return `H`
// Return still leaves no trace without a reversal of its own.
void EditorController::Impl::commitChartHarmonic(const std::optional<int> partial)
{
    const ChartVerbWindowVerb* const live_verb = liveChartGestureVerb();
    const bool continues =
        live_verb != nullptr && std::holds_alternative<ChartHarmonicGesture>(*live_verb);
    // Bound before the step so the replan reads the keys the proof was made against.
    const std::vector<ChartSlotKey> keys = chartSelection().notes();
    static_cast<void>(commitChartGestureStep(
        continues,
        [this, &keys, partial](const common::core::Chart& pre_gesture) {
            return partial.has_value()
                       ? planSetHarmonic(
                             pre_gesture, session().song().tempo_map, keys, partial, "Harmonic")
                       : planClearHarmonic(
                             pre_gesture, session().song().tempo_map, keys, "Remove Harmonic");
        },
        ChartHarmonicGesture{}));
}

namespace
{

// The bend picker's amounts, rest to the ceiling the curve is scaled to, one row per quarter step;
// the rows are numbered by quarter steps, so a stated amount names its row directly.
constexpr long g_bend_picker_top_row = static_cast<long>(
    common::core::g_bend_ceiling_semitones / common::core::g_bend_quarter_step_semitones);

} // namespace

// The bend verb's QUESTION (`B`, `Alt+B`): which anchors the key addresses (chartModifierAnchors),
// and what amount they should state, asked through the picker; nothing is committed until the
// answer returns through SetChartBend. Every amount is offered, ticked where every anchor already
// states it; the clear leads, offered only where a point states a bend to take away.
void EditorController::Impl::performActionImpl(const EditorAction::ChooseChartBend& action)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    ChartSelection anchors = chartModifierAnchors(action.plane);
    if (arrangement == nullptr || !arrangement->chart.has_value() || anchors.empty())
    {
        return;
    }
    const common::core::Chart& chart = *arrangement->chart;

    // What each anchor states now: an onset always states its pre-bend, a point states a bend only
    // where one stands with the channel stated.
    std::vector<std::optional<double>> stated;
    for (const std::size_t index : slotIndicesForKeys(chart.notes, anchors.notes()))
    {
        stated.emplace_back(chart.notes[index].bend);
    }
    bool clear_offered = false;
    for (const ChartKeyframeKey& key : anchors.keyframes())
    {
        std::optional<double> bend;
        if (const common::core::ChartNote* const carrier = chartNoteAt(chart.notes, key.note))
        {
            if (const common::core::Keyframe* const point =
                    common::core::standingKeyframe(*carrier, key.offset))
            {
                bend = point->bend;
            }
        }
        clear_offered = clear_offered || bend.has_value();
        stated.push_back(bend);
    }
    // The one amount every anchor states, when they agree — compared as optionals, so an unstated
    // anchor agrees with none.
    const std::optional<double> uniform =
        !stated.empty() && std::ranges::all_of(
                               stated,
                               [&stated](const std::optional<double>& bend) {
                                   return std::is_eq(bend <=> stated.front());
                               })
            ? stated.front()
            : std::nullopt;

    ChartBendPicker picker{
        .anchor = chartSlotViewState(
            session().song().tempo_map,
            chartCaretSlotFor(session().song().tempo_map, anchors.keys().front())),
        .choices = {},
    };
    if (clear_offered)
    {
        picker.choices.emplace_back(ChartBendClearChoice{});
    }
    for (long row = 0; row <= g_bend_picker_top_row; ++row)
    {
        const double semitones =
            static_cast<double>(row) * common::core::g_bend_quarter_step_semitones;
        picker.choices.emplace_back(
            ChartBendAmountChoice{
                .semitones = semitones,
                .current = uniform.has_value() && std::is_eq(semitones <=> *uniform),
            });
    }
    m_chart_bend_question = std::move(anchors);
    requestChartBendPicker(std::move(picker));
}

// The bend picker's ANSWER: the amount written at every anchor the question named, planting a point
// where none stands, as one entry; the anchors are then the selection, so a planted point is
// selected and wears its ring. Taking the statement away leaves each point's other channels, and a
// point it leaves saying nothing goes and leaves the selection with it.
void EditorController::Impl::performActionImpl(const EditorAction::SetChartBend& action)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || m_chart_bend_question.empty())
    {
        return;
    }
    const ChartSelection anchors = std::exchange(m_chart_bend_question, ChartSelection{});
    const std::string_view label = action.semitones.has_value() ? "Bend" : "Remove Bend";
    static_cast<void>(applyChartEditPlan(
        planSetBend(
            *arrangement->chart,
            session().song().tempo_map,
            anchors.notes(),
            anchors.keyframes(),
            action.semitones,
            label),
        anchors.keys()));
}

// The one technique toggle verb. Uniform scope, one compound undo entry: a selection where every
// note already carries the technique clears it, anything else sets it on all of them; the planner
// owns eligibility, so a selection the technique is only partly legal on applies to the notes that
// can take it rather than refusing as a whole. Both directions arm the toggle window, because
// either press is what a second press must be able to reverse exactly — which for a scrape means
// the sustain the default grew and the glide a conversion consumed, and for an emphasis the ghost
// an accent overwrote, neither of which the plain apply-or-clear law could put back. Every
// technique is one row of chartTechniqueLaw; the legato claim shares the window and the contract
// but plans through the resolver, so it branches to its own law once the shared prologue has run.
void EditorController::Impl::performActionImpl(const EditorAction::ToggleChartTechnique& action)
{
    const ChartTechnique technique = action.technique;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    // The OPERAND a modifying key addresses: the selection, else what the caret's slot holds —
    // the ring on a covered slot, where only a channel along the ring (the vibrato) has a meaning
    // and every other technique plans to NoChange.
    const ChartSelection operand = chartModifierAnchors(ChartEntryPlane::Note);
    if (arrangement == nullptr || !arrangement->chart.has_value() || operand.empty())
    {
        return;
    }
    const std::string revert_label =
        "Revert " + (technique == ChartTechnique::Legato
                         ? std::string{"Legato"}
                         : std::string{chartTechniqueLaw(technique).noun});
    if (reverseChartVerbWindow(ChartTechniqueToggle{.technique = technique}, revert_label))
    {
        return;
    }
    if (technique == ChartTechnique::Legato)
    {
        toggleChartLegato(operand);
        return;
    }

    // The law reads the whole OPERAND for both halves of the toggle: which objects a technique
    // has a meaning for is the row's own business, so a press over an operand this technique
    // reaches nothing in simply plans to NoChange instead of being filtered out here. The operand
    // becomes the selection, as the bend's anchors do, so a point the caret's slot planted is
    // selected and a second press reverses it through the window.
    const ChartTechniqueLaw law = chartTechniqueLaw(technique);
    const bool all_carry = law.carried(*arrangement->chart, operand);
    const std::string label = all_carry ? "Remove " + std::string{law.noun} : std::string{law.noun};
    if (applyChartEditPlan(
            law.plan(*arrangement->chart, session().song().tempo_map, operand, !all_carry, label),
            operand.keys()))
    {
        m_chart_verb_window = ChartVerbWindow{
            .keys = chartSelection().keys(),
            .verb = ChartTechniqueToggle{.technique = technique},
        };
    }
}

// The legato claim's own law under the shared toggle contract. planSetLegato is the oracle and the
// resolver its only authority, so eligibility is never restated here: applying is always the first
// answer — every selected note whose claim the chart justifies gets it, including the assist
// growing a predecessor's tail when the hold was the only thing missing — and only when applying
// would change nothing does the press mean clear. Measuring the press by what the PLAN does rather
// than by what the selection already holds is what keeps a rider note from stranding the toggle in
// apply mode forever. The clear flattens only the stored claims: a left-hand tap riding the
// selection keeps its attack, since Ctrl+H is its sole author.
void EditorController::Impl::toggleChartLegato(const ChartSelection& operand)
{
    const std::vector<ChartSlotKey>& keys = operand.notes();
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    ChartLegatoPlan planned =
        planSetLegato(*arrangement->chart, session().song().tempo_map, keys, "Legato");
    if (planned.plan.has_value())
    {
        if (applyChartEditPlan(std::move(*planned.plan), operand.keys()))
        {
            m_chart_verb_window = ChartVerbWindow{
                .keys = chartSelection().keys(),
                .verb = ChartTechniqueToggle{.technique = ChartTechnique::Legato},
            };
        }
        return;
    }
    // Nothing left to claim, so this press clears — the stored claims only.
    std::vector<ChartSlotKey> legato_keys;
    for (const common::core::ChartNote& note : chartNotesForKeys(keys))
    {
        if (note.attack == common::core::NoteAttack::Legato)
        {
            legato_keys.push_back(ChartSlotKey{.position = note.position, .string = note.string});
        }
    }
    std::expected<ChartEditPlan, ChartPlanRefusal> clear_plan =
        legato_keys.empty() ? std::unexpected{ChartPlanRefusal::NoChange}
                            : planSetAttack(
                                  *arrangement->chart,
                                  session().song().tempo_map,
                                  legato_keys,
                                  common::core::NoteAttack::Pick,
                                  "Remove Legato");
    if (clear_plan.has_value())
    {
        // The clear press arms the window too: reversing it restores the exact previous mix.
        if (applyChartEditPlan(std::move(clear_plan), operand.keys()))
        {
            m_chart_verb_window = ChartVerbWindow{
                .keys = chartSelection().keys(),
                .verb = ChartTechniqueToggle{.technique = ChartTechnique::Legato},
            };
        }
        return;
    }
    // A press that changed nothing is SILENT, exactly like every other technique verb that applies
    // nothing: selecting a phrase's first note and pressing L is the commonest press there is, and
    // it is not an error. `planned.refused` still names every note the resolver turned down and why
    // — that IS the feedback payload — but the only reporting seam the view offers today is a modal
    // "Could not complete request" box, which interrupts a keystroke to say nothing failed. The
    // refusals surface once the refusal flash exists
    // (docs/plans/in-progress/refusal-flash.md); until then they are deferred rather than
    // mis-routed.
}

// Sets the selection to the left-hand tap attack as one compound undo entry, uniform scope. The
// stating verb beside the inferring toggle (Shift+letter is the sibling technique, so Shift+T sits
// beside plain T on the letter map): the fretting hand striking a
// note from nowhere is a LOCAL statement, so no predecessor can justify it and plain H can never
// produce it. planSetAttack already IS the mixed-validity policy: each note is retyped and asked of
// the rule authority, so the open string with no node (E4's boundary) is skipped, a pinch's
// bridge-side graze refuses to re-hand, and a tap harmonic's strike point carries into the form E13
// names.
void EditorController::Impl::performActionImpl(const EditorAction::SetChartLeftTap&)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    static_cast<void>(applyChartEditPlan(planSetAttack(
        *arrangement->chart,
        session().song().tempo_map,
        chartSelection().notes(),
        common::core::NoteAttack::LeftTap,
        "Left-Hand Tap")));
}

// The junction toggle (`Shift+L`): at every selected junction the press moves it to its other
// state — a selected keyframe becomes a head, a selected head becomes a point on its same-string
// predecessor's path. Both halves run in one press and land in ONE compound undo entry, however
// many notes the splits produce and however many the joins dissolve. Inert with nothing selected
// at all: the empty-operand rule every verb here follows.
//
// No verb window is armed, and none is needed: the toggle rides the SELECTION instead. Each press
// leaves exactly what it made selected — a split's new heads, a join's new point — so pressing
// again reverses it for as long as the user leaves that selection alone, with no window to expire
// and no second press to arm. Refusals stay silent as the disconnect's always did (W5's feedback
// channel is deferred): the press simply does nothing, which is what an operand that cannot be
// joined reads as.
void EditorController::Impl::performActionImpl(const EditorAction::ToggleChartJunction&)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    std::expected<ChartJunctionPlan, ChartPlanRefusal> toggled = planToggleJunctions(
        *arrangement->chart,
        session().song().tempo_map,
        chartSelection().notes(),
        chartSelection().keyframes());
    if (!toggled.has_value())
    {
        return;
    }
    // The planner's own selection, never the apply's default follow: only the walk that made them
    // knows which inserted record is a new head and which is a grown path carrying a new point.
    std::vector<ChartSelectionKey> selection = std::move(toggled->selection);
    static_cast<void>(applyChartEditPlan(std::move(toggled->plan), std::move(selection)));
}

// Esc is a settle event whichever rung consumes it, so the ladder itself is the helper below and
// the press always ends with the sweep: stepping out of an editing context is exactly the moment a
// claim the burst broke stops being transient.
//
// The view push is the RUNG's, deliberately: a committing sweep publishes its own state, so a press
// that fell through every rung with nothing to settle changed nothing and publishes nothing — the
// shape the ladder had before the sweep joined it.
void EditorController::Impl::onChartEscapePressed()
{
    const bool consumed = consumeChartEscapeRung();
    static_cast<void>(settleChart());
    if (consumed)
    {
        updateView();
    }
}

// The Esc ladder (the marker model): an in-flight pointer gesture is abandoned without
// mutating; else an armed caret — on any row, lane carets included — dissolves to the passive
// cursor in its place, keeping the selection; else THE selection clears, whatever its kind (one
// selection editor-wide, so Esc's last rung is kind-agnostic like Delete's dispatch — a selected
// tone region clears through it like any other kind, and the clear itself hands the rig back to the
// cursor's tone, because the selection is one of the audible tone's inputs). The marker rungs also
// end the multi-digit fret-entry window — after a cancel, the next digit must not widen a dead
// entry.
bool EditorController::Impl::consumeChartEscapeRung()
{
    // Gesture cancels outrank the marker/selection ladder: an in-flight pointer drag simply never
    // commits. A move/insert lane drag drops without touching the model; the next rebuild paints
    // the lane back without the preview.
    if (m_tone_automation_drag.has_value())
    {
        m_tone_automation_drag.reset();
        return true;
    }

    if (m_chart_gesture.has_value())
    {
        m_chart_gesture.reset();
        return true;
    }

    // An INVALID pending fret value claims its own rung: Esc cancels the PROBLEM, so the value
    // discards and the caret survives for an immediate retype. A VALID pending value is not a
    // cancellable thing — it falls through to the caret rung below and commits on the way through
    // the uniform settle, because a value you typed is a value you meant.
    if (m_chart_fret_entry.has_value() && m_chart_fret_entry->refused())
    {
        discardChartFretEntry();
        return true;
    }

    if (armedChartCaret() != nullptr)
    {
        settleChartFretEntry();
        dissolveChartCaretInPlace();
        disarmChartVerbWindow();
        return true;
    }

    if (!std::holds_alternative<std::monostate>(m_selection))
    {
        clearSelection();
        return true;
    }
    return false;
}

// The settle sweep: flattens every connection claim the chart no longer justifies, in one batch.
//
// The whole relational half of the legato model lives here, and it is stateless — it judges only
// the stream it finds, so there is no window to keep, no flagged note, and no proof to carry.
// Mid-burst a broken claim simply displays and scores as the plain pick it plays as; this is where
// that stops being true.
//
// The commit shape is what keeps undo exact. On top of history the flatten folds into the burst's
// own entry, so one Ctrl+Z restores the edit and the claim together; with no such entry it pushes
// its own. At a mid-stack resting point — reachable only through undo — it DEFERS: touching bytes
// there would either truncate a live redo branch or rewrite an entry the cursor is not on, and the
// claim displays as its resolution anyway. The invariant is therefore exactly "Unjustified cannot
// survive a settle at the top of history"; every FILE is unconditionally clean because the document
// writer resolves as it serializes.
//
// Only the CURRENT arrangement is swept. Every load settles every chart it reads and an arrangement
// switch settles the one being departed, so in practice no other chart holds a broken claim — but
// that is not an induction, and the exception is worth naming: the switch's settle DEFERS at a
// mid-stack cursor, so breaking a claim, undoing once, and then switching leaves the departed chart
// holding an `Unjustified` claim in memory that no later event reaches (the sweep only ever visits
// the current arrangement). Consequence is display-only, and accepted: every FILE is clean
// regardless, because the document writer serializes the resolved form.
bool EditorController::Impl::settleChart()
{
    const bool settled = settleChartClaims();
    // The keyframe commit law's resting point, AFTER the claims: the fold reverses the burst record
    // against the live chart, so the points that record names must still be there while it runs.
    // Unlike the fold this half never defers, because it touches no history — which is what lets
    // a point planted before an unrelated edit on its note still go when the note leaves focus.
    // Focus is the note's, not the point's: a slide is authored as two points on one tail, and the
    // start says nothing until the landing exists.
    static_cast<void>(dissolveSilentKeyframes(
        [this](const ChartSlotKey& slot) { return chartNoteInFocus(slot); }));
    return settled;
}

bool EditorController::Impl::settleChartClaims()
{
    // The fret entry settles FIRST at every settle point the sweep owns: commit the typed value,
    // then flatten the claims it broke. Riding the sweep's own call sites is what makes the
    // pending entry's prologue complete without a second site list to keep in step.
    settleChartFretEntry();
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() ||
        m_undo_history.hasPendingTransition())
    {
        return false;
    }
    const EditorUndoHistorySnapshot history = m_undo_history.snapshot();
    if (history.position != history.labels.size())
    {
        return false;
    }

    // The entry this settle folds into, or null to push its own: the burst record must still own
    // the history top AND the file must not hold the state that entry produced — rewriting the
    // clean entry would make "return to clean" a lie about it. Bound once as a pointer rather than
    // re-asked per branch, which also keeps the guarantee visible to clang-tidy's optional
    // tracking (a `bool` carrying it is not).
    const ChartNotesTopEntry* const burst =
        m_chart_notes_top.has_value() && m_chart_notes_top->history_position == history.position &&
                !m_undo_history.isAtCleanState()
            ? &*m_chart_notes_top
            : nullptr;

    // The folded entry has to describe the WHOLE burst, so it is diffed against the pre-burst
    // CHART, reconstructed by reversing exactly what the top entry applied. (Re-planning forward
    // from the current values could not produce it: the burst's own plan is what carries the edit.)
    // The whole chart and not just its notes, because the walk-back below reverses a plan against
    // it and the base a fold diffs against is the chart state that entry was applied to.
    common::core::Chart base = *arrangement->chart;
    std::string label{"Settle Legato"};
    if (burst != nullptr)
    {
        if (!applyChartChange(base, burst->plan.reversed()).has_value())
        {
            return false;
        }
        label = burst->plan.label;
    }
    std::optional<ChartEditPlan> settled =
        planSettleChart(*arrangement->chart, session().song().tempo_map, base, label);
    if (!settled.has_value())
    {
        // Nothing to settle, so both coalescing windows stay armed exactly as they were.
        return false;
    }
    // A fold that exactly cancels the burst — a claim made and flattened — describes nothing, and
    // an entry describing nothing is a dead Ctrl+Z: the burst RETIRES instead, exactly as a
    // gesture replayed to its origin does. Judged on the written form, which is what the entry
    // would hold.
    ChartEditPlan written = writtenChartPlan(*settled);
    if (burst != nullptr && written.empty())
    {
        retireChartGesture(burst->plan);
        return true;
    }

    // The model moves first and the history follows, the order every chart edit takes
    // (applyChartEditPlan). Walk the live chart back to pre-burst and then to the settled state,
    // so the state the history top describes is exactly the state the chart holds.
    const ChartEditPlan& settled_plan = *settled;
    const std::optional<bool> walked = m_session.writeChart([&burst, &settled_plan](
                                                                common::core::Chart& chart) {
        return (burst == nullptr || applyChartChange(chart, burst->plan.reversed()).has_value()) &&
               applyChartChange(chart, settled_plan).has_value();
    });
    if (!walked.value_or(false))
    {
        reportError("Could not apply chart edit: " + settled->label);
        return false;
    }
    if (burst != nullptr)
    {
        // A flatten changes how a claim resolves, not where the edit left the charter, so the
        // fold keeps the burst's recorded focuses.
        replaceUndoTop(
            std::make_unique<ChartEdit>(std::move(written), burst->before, burst->after));
    }
    else
    {
        // At the top of the stack a push truncates nothing, so the flatten simply becomes its own
        // undo step, standing wherever the charter stands now on both sides.
        const std::optional<ChartEditFocus> here = chartEditFocusOf(chartSelection().keys());
        pushUndoEntry(std::make_unique<ChartEdit>(std::move(written), here, here));
    }
    // A sweep that commits anything closes the chart verbs' window: a fold changes the top entry's
    // content without moving the history position, so an armed window's proof would otherwise
    // still pass — and the next press would reverse a plan that no longer exists, or re-plan a
    // duration gesture against a stream the flatten has moved. The pending fret entry needs no
    // closing here — it settled at this function's head, before the sweep judged the chart.
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    updateView();
    return true;
}

} // namespace rock_hero::editor::core
