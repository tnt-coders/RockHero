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
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/fraction.h>
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

} // namespace

// The memoized projection deriveViewState pushed is exactly what the lane painted, so pointer
// events resolve against it; null while no chart is displayed.
const common::core::ChartViewState* EditorController::Impl::displayedTabProjection() const
{
    return m_tab_view_state.get();
}

// Chart notes are sorted by (position, string) and the tab projection preserves that order one
// to one, so a projection index addresses the chart note directly.
std::optional<ChartNoteKey> EditorController::Impl::chartNoteKeyAt(
    std::size_t projection_index) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() ||
        projection_index >= arrangement->chart->notes.size())
    {
        return std::nullopt;
    }

    const common::core::ChartNote& note = arrangement->chart->notes[projection_index];
    return ChartNoteKey{.position = note.position, .string = note.string};
}

void EditorController::Impl::clearChartEditingState()
{
    // DISCARD rather than settle: this is context teardown (a chart being replaced or closed),
    // and committing a pending value into a dying session would author into the wrong chart.
    discardChartFretEntry();
    clearSelection();
    m_chart_gesture.reset();
    disarmChartVerbWindow();
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
    return m_selection.emplace<ChartSelection>();
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
// declines the widen.
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
    static_cast<void>(settleChartLegato());
}

void EditorController::Impl::clearSelection()
{
    setSelection(std::monostate{});
}

void EditorController::Impl::clearCursorCoupledSelection()
{
    if (std::holds_alternative<ToneRegionSelection>(m_selection) ||
        std::holds_alternative<AutomationPointSelection>(m_selection))
    {
        setSelection(std::monostate{});
    }
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

// Demotes an armed caret to the passive cursor, leaving the transport where it is. Used by
// the transport-motion handoffs (play, external playback, paused seeks): the transport
// already states the position, so only the string memory survives.
void EditorController::Impl::disarmChartMarker()
{
    if (const ChartCaret* const caret = armedChartCaret())
    {
        m_chart_marker = ChartCursor{.string = caret->string};
    }
}

// Demotes an armed caret to the passive cursor "in its place": a paused seek carries the
// transport to the caret's musical time, so the cursor line appears exactly where the caret
// was. Used by the editing-gesture handoffs (Ctrl+click, double-click, marquee, Esc); the
// seek deliberately skips tone activation — dissolving a caret is a display handoff, not a
// listening move.
void EditorController::Impl::dissolveChartCaretInPlace()
{
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr)
    {
        return;
    }

    m_transport.seek(
        session().timeline().clamp(
            common::core::TimePosition{secondsAtGridPosition(
                session().song().tempo_map, caret->position)}));
    m_chart_marker = ChartCursor{.string = caret->string};
}

// Arms the caret at a slot and re-derives the selection from what sits under it: a note
// becomes the selection (the highlight IS the caret display there), an empty slot clears it
// (the white square shows where typing will insert). Chart notes are sorted by
// (position, string), so slot occupancy is one binary search.
// True when a note already sits on the slot: the chart notes are sorted by (position, string),
// so occupancy is one binary search. Shared by caret arming (selection re-derivation), the
// Alt+click insert (refusal), and the insert ghost's honesty gate.
bool EditorController::Impl::chartSlotOccupied(
    common::core::GridPosition position, int string) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return false;
    }
    const ChartNoteKey key{.position = position, .string = string};
    return std::ranges::binary_search(
        arrangement->chart->notes, key, {}, [](const common::core::ChartNote& note) {
            return ChartNoteKey{.position = note.position, .string = note.string};
        });
}

void EditorController::Impl::armChartCaret(common::core::GridPosition position, int string)
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
    m_chart_marker = ChartCaret{.position = position, .string = string};
    const ChartNoteKey key{.position = position, .string = string};
    if (chartSlotOccupied(position, string))
    {
        chartSelectionMutable().replaceWith(key);
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
    static_cast<void>(settleChartLegato());
}

// Plants a note at an empty slot, makes it the selection, and arms the caret on it — the mouse
// form of the Insert verb (§9b) behind Alt+click neutral-create. The caller guarantees the slot
// is empty (planInsertNote would otherwise REPLACE the occupant with this fret-0 note). The
// insert is one undo entry; a following retype (the note lands selected) is its own — "place,
// then correct the value".
void EditorController::Impl::insertChartNoteAt(
    common::core::GridPosition position, int string, int fret)
{
    // The pending fret entry settles first (the uniform prologue).
    settleChartFretEntry();
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    common::core::ChartNote note;
    note.position = position;
    note.string = string;
    note.fret = fret;
    std::expected<ChartNotesEditPlan, ChartPlanRefusal> plan = planInsertNote(
        *arrangement->chart, session().song().tempo_map, note, chartGridStepBeats(position));
    if (!plan.has_value())
    {
        return;
    }
    if (!applyChartEditPlan(
            std::move(plan),
            std::vector<ChartNoteKey>{ChartNoteKey{.position = position, .string = string}}))
    {
        return;
    }
    // Arm the caret on the freshly placed note so the state reads exactly like a plain click
    // that landed on a note (armed ⟹ the selection is what sits under the caret, §9a) and the
    // next typed digit retypes it.
    armChartCaret(position, string);
}

// Resolves the Alt-hover insert ghost: published only while paused with Alt held over an
// insertable empty slot, so the ring never advertises an insert that would no-op (§7). Snapping
// and occupancy match the click exactly (chartPlacementAt + chartSlotOccupied). Dirty-checked
// against the current ghost — a hover that stays within one grid slot leaves it unchanged and
// pushes no view rebuild, so per-pixel hover stays cheap.
void EditorController::Impl::publishChartInsertGhost(const ChartPointerEvent& event)
{
    std::optional<ChartSlotViewState> ghost;
    if (event.modifiers.alt && !isBusy() && !m_transport.state().playing)
    {
        if (const auto placement = chartPlacementAt(event);
            placement.has_value() && !chartSlotOccupied(placement->first, placement->second))
        {
            const common::core::TempoMap& tempo_map = session().song().tempo_map;
            ghost = ChartSlotViewState{
                .seconds = tempo_map.secondsAtNote(
                    placement->first.measure, placement->first.beat, placement->first.offset),
                .string = placement->second,
            };
        }
    }
    if (ghost == m_chart_insert_ghost)
    {
        return;
    }
    m_chart_insert_ghost = ghost;
    updateView();
}

// Arms the caret on an automation lane row and re-derives the selection from what sits under
// it — armChartCaret's row-axis sibling (§9b): a point at the slot becomes the editor-wide
// selection, an empty slot clears it. The remembered string survives so crossing back up into
// the tab lane returns where the caret left.
void EditorController::Impl::armLaneCaret(
    common::core::GridPosition position, AutomationLaneRow row)
{
    bool on_point = false;
    if (const std::vector<common::core::ToneAutomationPoint>* const points =
            lanePointsFor(row.instance_id, row.param_id))
    {
        on_point =
            std::ranges::any_of(*points, [&](const common::core::ToneAutomationPoint& point) {
                return point.position == position;
            });
    }

    if (on_point)
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
    m_chart_marker =
        ChartCaret{.position = position, .string = chartMarkerString(), .lane = std::move(row)};
}

// The caret row's next authored object strictly beyond the caret in the step direction: notes
// on the caret's string, points on its lane. Linear scans are fine at keypress cadence.
std::optional<common::core::GridPosition> EditorController::Impl::nextRowObjectStop(
    const ChartCaret& caret, bool later)
{
    std::optional<common::core::GridPosition> best;
    const auto consider = [&](const common::core::GridPosition& position) {
        if (later ? !(caret.position < position) : !(position < caret.position))
        {
            return;
        }
        if (!best.has_value() || (later ? position < *best : *best < position))
        {
            best = position;
        }
    };
    if (caret.lane.has_value())
    {
        if (const std::vector<common::core::ToneAutomationPoint>* const points =
                lanePointsFor(caret.lane->instance_id, caret.lane->param_id))
        {
            for (const common::core::ToneAutomationPoint& point : *points)
            {
                consider(point.position);
            }
        }
        return best;
    }
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return best;
    }
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (note.string == caret.string)
        {
            consider(note.position);
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
// chart's single placement seam (arm, Alt insert, and ghost all snap through it, mirroring
// the lane's laneSnapPositionForX). Placement snaps to the displayed grid's exact rational by
// default; Ctrl composes the 1/960-beat fine tier, uniform with the lane arm and placement
// (the off-grid unification — snap default follows the data, the capability is universal).
std::optional<std::pair<common::core::GridPosition, int>> EditorController::Impl::chartPlacementAt(
    const ChartPointerEvent& event) const
{
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || event.geometry.lane_height <= 0.0f)
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
        event.modifiers.ctrl
            ? fineGridPositionForBeat(tempo_map, tempo_map.beatPositionAtSeconds(clicked->seconds))
            : nearestTempoGridPosition(tempo_map, m_grid_note_value, *clicked);

    // Lanes stack highest string on top; extra user lanes pad below the chart's strings.
    const float lane = (event.y - event.geometry.bounds_y) / event.geometry.lane_height;
    const int lane_index =
        std::clamp(static_cast<int>(lane), 0, event.geometry.displayed_count - 1);
    const int displayed_string = event.geometry.displayed_count - lane_index;
    const int string =
        std::clamp(displayed_string - event.geometry.extra_lanes, 1, tab->string_count);
    return std::pair{position, string};
}

// One grid step in beats at a position (the shared gridStepBeats seam under the session's
// current grid). The grid step is the default move; the Ctrl fine tier (1/960 beat) is the uniform
// precision escape hatch on both surfaces.
common::core::Fraction EditorController::Impl::chartGridStepBeats(
    common::core::GridPosition at) const
{
    return gridStepBeats(session().song().tempo_map, m_grid_note_value, at.measure);
}

// Applies a planned chart-note change through the session's mutable chart (bumping the revision
// so every projection rebuilds) and records it as one undo entry. Takes the planners' own return
// shape; the refusal kind is not consumed here — a caller that wants to distinguish NoChange from
// Invalid branches before handing the plan over.
bool EditorController::Impl::applyChartEditPlan(
    std::expected<ChartNotesEditPlan, ChartPlanRefusal> plan,
    std::optional<std::vector<ChartNoteKey>> select_exactly)
{
    if (!plan.has_value())
    {
        return false;
    }

    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr)
    {
        return false;
    }

    if (const auto applied = applyChartNotesChange(*chart, plan->removed, plan->inserted);
        !applied.has_value())
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

    // The selection follows the edit: retyped/moved/inserted notes stay selected under their
    // new keys, deleted notes drop out (their keys no longer resolve).
    if (select_exactly.has_value())
    {
        chartSelectionMutable().applyBox(*select_exactly, false);
    }
    else
    {
        // (selection - removed keys) + inserted keys: retyped/resized notes stay selected even
        // when the edit left some of them unchanged, moved notes follow to their new keys, and
        // deleted notes drop out.
        const auto in_plan = [](const std::vector<common::core::ChartNote>& side,
                                const ChartNoteKey& key) {
            return std::ranges::any_of(side, [&key](const common::core::ChartNote& note) {
                return ChartNoteKey{.position = note.position, .string = note.string} == key;
            });
        };
        std::vector<ChartNoteKey> next_selection;
        next_selection.reserve(chartSelection().notes().size() + plan->inserted.size());
        for (const ChartNoteKey& key : chartSelection().notes())
        {
            if (!in_plan(plan->removed, key))
            {
                next_selection.push_back(key);
            }
        }
        for (const common::core::ChartNote& note : plan->inserted)
        {
            const ChartNoteKey key{.position = note.position, .string = note.string};
            // A note rewritten IN PLACE that the user had not selected is something the plan
            // carried, not the edit's subject — the H assist grows a predecessor's tail inside
            // the same plan, and the finalize's overlap pass can retrim a same-string neighbour —
            // so it must not join the selection. Selecting it would break the armed-caret
            // invariant (armed means the selection is exactly what sits under the caret) and
            // would silently widen the next keystroke's scope. A note inserted at a NEW key is
            // the edit's own product (a moved or created note) and follows as before.
            if (in_plan(plan->removed, key) &&
                !std::ranges::binary_search(chartSelection().notes(), key))
            {
                continue;
            }
            next_selection.push_back(key);
        }
        chartSelectionMutable().applyBox(next_selection, false);
    }

    // Recorded before the plan moves into the entry: the settle sweep folds its flatten into this
    // entry so the edit and the claim it broke undo together, and the technique toggle windows
    // reverse it. Recorded only for an entry the history actually took — a refused push resets
    // the history, and a record claiming to own its top would then be a false proof.
    ChartNotesEditPlan recorded = *plan;
    if (pushUndoEntry(std::make_unique<ChartNotesEdit>(std::move(*plan))))
    {
        m_chart_notes_top = ChartNotesTopEntry{
            .plan = std::move(recorded),
            .history_position = m_undo_history.snapshot().position,
        };
    }
    updateView();
    return true;
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
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0 || isBusy())
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
            m_grid_note_value,
            event.geometry.visible_timeline,
            static_cast<int>(event.geometry.bounds_width),
            event.x,
            event.modifiers.ctrl ? TimelineCursorPlacementMode::Free
                                 : TimelineCursorPlacementMode::SnapToGrid);
        if (clicked.has_value())
        {
            runAction(EditorAction::SeekTimeline{*clicked});
        }
        return;
    }

    // A press ends any Alt-hover preview: a live gesture owns the lane now, and an Alt-drag
    // re-shows the ring as it follows. Refresh only when a ghost was actually showing.
    const bool had_insert_ghost = m_chart_insert_ghost.has_value();
    m_chart_insert_ghost.reset();

    ChartPointerGesture gesture;
    gesture.geometry = event.geometry;
    gesture.modifiers = event.modifiers;
    gesture.anchor_x = event.x;
    gesture.anchor_y = event.y;
    gesture.current_x = event.x;
    gesture.current_y = event.y;
    gesture.hit_note = chartNoteHitIndex(*tab, event.geometry, event.x, event.y);
    m_chart_gesture = gesture;

    if (!gesture.hit_note.has_value())
    {
        if (had_insert_ghost)
        {
            updateView();
        }
        return;
    }

    const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
    if (!key.has_value())
    {
        return;
    }

    if (event.modifiers.ctrl)
    {
        chartSelectionMutable().toggle(*key);
        dissolveChartCaretInPlace();
        // Both multi-select gestures change the selection without passing setSelection or
        // armChartCaret, so the settle event lands here too.
        static_cast<void>(settleChartLegato());
    }
    else if (event.clicks >= 2)
    {
        const common::core::Arrangement* const arrangement = session().currentArrangement();
        if (arrangement != nullptr && arrangement->chart.has_value())
        {
            chartSelectionMutable().replaceWith(
                chartOnsetGroupKeys(arrangement->chart->notes, key->position));
        }
        dissolveChartCaretInPlace();
        static_cast<void>(settleChartLegato());
    }
    else if (!chartSelection().contains(*key))
    {
        // Arming re-derives the singleton selection from the note under the caret. A press on
        // an already-selected note keeps the standing selection (and marker) untouched until
        // the release collapses it — the gap a future drag-move gesture lives in.
        armChartCaret(key->position, key->string);
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
    if (gesture.hit_note.has_value())
    {
        return;
    }

    if (gesture.modifiers.alt)
    {
        // Alt on an empty slot is the neutral-create gesture, never a marquee: the ring follows
        // the pointer and the release plants the note, so press-drag-release places in one
        // gesture just as the automation lane's Alt-drag does.
        publishChartInsertGhost(event);
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
    // A release ends the hover preview; every path below refreshes the view, so a ghost left
    // following an Alt-drag clears here.
    m_chart_insert_ghost.reset();

    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
    {
        updateView();
        return;
    }

    if (gesture.hit_note.has_value())
    {
        const bool clicked = std::abs(event.x - gesture.anchor_x) <= g_chart_click_threshold_px &&
                             std::abs(event.y - gesture.anchor_y) <= g_chart_click_threshold_px;
        // A completed plain click on a selected note collapses the selection to that note and
        // arms the caret there (the press deferred both while a drag was still possible); the
        // second release of a double click leaves the group selection standing.
        if (clicked && !gesture.modifiers.ctrl && event.clicks < 2)
        {
            if (const std::optional<ChartNoteKey> key = chartNoteKeyAt(*gesture.hit_note);
                key.has_value())
            {
                armChartCaret(key->position, key->string);
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
        const std::vector<std::size_t> boxed =
            chartNoteIndicesInBox(*tab, gesture.geometry, left, top, right, bottom);
        std::vector<ChartNoteKey> keys;
        keys.reserve(boxed.size());
        for (const std::size_t index : boxed)
        {
            if (const std::optional<ChartNoteKey> key = chartNoteKeyAt(index); key.has_value())
            {
                keys.push_back(*key);
            }
        }
        // Dissolution is a rule over outcomes (the marker model): a box that caught notes is
        // a multi-select outcome and demotes the caret to a cursor in its place; an empty box
        // has no selection outcome, so an armed caret survives untouched.
        if (!keys.empty())
        {
            chartSelectionMutable().applyBox(keys, gesture.modifiers.shift);
            dissolveChartCaretInPlace();
            static_cast<void>(settleChartLegato());
        }
        updateView();
        return;
    }

    // Empty release: Alt plants a fret-0 note here and selects it for an immediate retype (the
    // neutral-create verb's mouse form, §9b — the chart sibling of the lane's on-curve Alt+click),
    // but only on an EMPTY slot: planInsertNote replaces an occupant, so an Alt+click onto an
    // existing note would clobber it to fret 0. On an occupied slot Alt falls through to arming
    // the caret (selecting that note), matching the insert ghost's occupancy gate — no lying
    // affordance (§7). A plain release always arms the caret at the snapped slot — with
    // play-from-the-marker this IS the seek, the selection clearing via the caret's re-derivation.
    if (const auto placement = chartPlacementAt(event); placement.has_value())
    {
        if (gesture.modifiers.alt && !chartSlotOccupied(placement->first, placement->second))
        {
            insertChartNoteAt(placement->first, placement->second, 0);
        }
        else
        {
            armChartCaret(placement->first, placement->second);
        }
    }
    updateView();
}

// A button-less hover: publish the Alt insert ghost when Alt is held over an insertable empty
// slot, else clear it. The controller resolves snap + occupancy so the ring can only appear
// where an Alt+click would actually plant a note (§7, no lying affordance).
void EditorController::Impl::onChartPointerMove(const ChartPointerEvent& event)
{
    publishChartInsertGhost(event);
}

// The pointer left the lane: no hover, so no ghost.
void EditorController::Impl::onChartPointerExit()
{
    if (!m_chart_insert_ghost.has_value())
    {
        return;
    }
    m_chart_insert_ghost.reset();
    updateView();
}

// The vertical half of caret stepping — the row axis (§9b): strings render top-to-bottom with
// string 1 at the visual bottom, and the visible automation lanes continue the stack below
// it, so Down from string 1 crosses into the first lane and Up from the first lane returns to
// string 1. Edges clamp (re-arm in place) exactly like the string edges always have, and a
// caret whose lane left the visible set (tone switch, lane removal) falls back onto the
// remembered string (§9b demotion posture).
void EditorController::Impl::stepCaretRow(const ChartCaret& caret, bool up, int string_count)
{
    const std::vector<AutomationLaneRow> lanes = visibleAutomationLaneRows();
    if (caret.lane.has_value())
    {
        const auto row = std::ranges::find(lanes, *caret.lane);
        if (row == lanes.end())
        {
            armChartCaret(caret.position, std::clamp(caret.string, 1, string_count));
            return;
        }
        const std::size_t row_index = static_cast<std::size_t>(row - lanes.begin());
        if (up)
        {
            if (row_index == 0)
            {
                armChartCaret(caret.position, 1);
            }
            else
            {
                armLaneCaret(caret.position, lanes[row_index - 1]);
            }
        }
        else if (row_index + 1 < lanes.size())
        {
            armLaneCaret(caret.position, lanes[row_index + 1]);
        }
        else
        {
            armLaneCaret(caret.position, lanes[row_index]);
        }
        return;
    }
    if (!up && caret.string == 1 && !lanes.empty())
    {
        armLaneCaret(caret.position, lanes.front());
        return;
    }
    armChartCaret(caret.position, std::clamp(caret.string + (up ? 1 : -1), 1, string_count));
}

// Arrow keys on the marker (the marker model): while passive, the first press arms the caret
// at the paused cursor — nearest grid line, remembered string — without stepping; while
// armed, Left/Right step one grid line on the caret's string — or jump measures under the
// modifier (the Guitar Pro jump) — and Up/Down move across strings. Every move re-derives
// the selection from what sits under the caret. Inert while playing: arming requires a
// paused transport (armed ⟹ paused is structural).
void EditorController::Impl::performActionImpl(const EditorAction::StepChartCaret& action)
{
    const ChartStepDirection direction = action.direction;
    const bool measure = action.measure;
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
    {
        return;
    }

    const ChartCaret* const armed = armedChartCaret();
    if (armed == nullptr)
    {
        armChartCaret(
            nearestTempoGridPosition(
                session().song().tempo_map, m_grid_note_value, m_transport.position()),
            std::clamp(chartMarkerString(), 1, tab->string_count));
        updateView();
        return;
    }

    const ChartCaret caret = *armed;
    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        stepCaretRow(caret, direction == ChartStepDirection::Up, tab->string_count);
        updateView();
        return;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const int sign = direction == ChartStepDirection::Right ? 1 : -1;
    common::core::GridPosition stepped;
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
        stepped = adjacentTempoGridPosition(tempo_map, m_grid_note_value, caret.position, sign > 0);
        if (const std::optional<common::core::GridPosition> object_stop =
                nextRowObjectStop(caret, sign > 0);
            object_stop.has_value())
        {
            const bool grid_advanced =
                sign > 0 ? caret.position < stepped : stepped < caret.position;
            const bool object_nearer = sign > 0 ? *object_stop < stepped : stepped < *object_stop;
            if (!grid_advanced || object_nearer)
            {
                stepped = *object_stop;
            }
        }
    }
    // Time stepping is row-agnostic: a lane caret steps the same grid and keeps its row.
    if (caret.lane.has_value())
    {
        armLaneCaret(stepped, *caret.lane);
    }
    else
    {
        armChartCaret(stepped, caret.string);
    }
    updateView();
}

// Caret leap to a derived musical position (Home/End, PageUp/Down). Each jump resolves an absolute
// or section-relative destination and arms the caret there on the first press — no arm-at-cursor
// step first, because the whole point of these keys is the big move. The row is preserved
// (horizontal reach), so a lane caret keeps its lane and a string caret its string; without an
// armed caret yet the jump measures from the paused cursor and lands on the remembered string. A
// section jump with no section in that direction is refused, not clamped, in line with every other
// refused move. Inert while playing — arming requires a paused transport.
void EditorController::Impl::performActionImpl(const EditorAction::JumpChartCaret& action)
{
    const ChartCaretJump target = action.target;
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
    {
        return;
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const ChartCaret* const armed = armedChartCaret();
    const common::core::GridPosition reference =
        armed != nullptr
            ? armed->position
            : nearestTempoGridPosition(tempo_map, m_grid_note_value, m_transport.position());

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
            destination = adjacentSectionPosition(session().song().sections, reference, false);
            break;
        }
        case ChartCaretJump::NextSection:
        {
            destination = adjacentSectionPosition(session().song().sections, reference, true);
            break;
        }
    }

    if (!destination.has_value())
    {
        // A refused section jump keeps an armed caret exactly where it was; a first press with
        // nothing armed still arms at the reference, so the key always yields a caret to work from
        // (matching the arrow keys' arm-first press).
        if (armed == nullptr)
        {
            armChartCaret(reference, std::clamp(chartMarkerString(), 1, tab->string_count));
            updateView();
        }
        return;
    }

    // Preserve the row: a lane caret keeps its lane, a string caret its string, and a fresh caret
    // lands on the remembered string (bounds/sections are horizontal reach).
    if (armed != nullptr && armed->lane.has_value())
    {
        armLaneCaret(*destination, *armed->lane);
    }
    else
    {
        armChartCaret(
            *destination,
            armed != nullptr ? armed->string
                             : std::clamp(chartMarkerString(), 1, tab->string_count));
    }
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
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->string_count <= 0)
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
        anchor = common::core::snapGridPosition(tempo_map, caret->position, m_grid_note_value);
        focus = anchor;
    }
    else
    {
        anchor = nearestTempoGridPosition(tempo_map, m_grid_note_value, m_transport.position());
        focus = anchor;
    }

    // Move the focus one unit through the shared caret-navigation destinations, so the range edge
    // and the caret land on the same slot for the same motion.
    common::core::GridPosition next_focus = focus;
    switch (extent)
    {
        case TimeSelectionExtent::Grid:
        {
            next_focus = adjacentTempoGridPosition(tempo_map, m_grid_note_value, focus, later);
            break;
        }
        case TimeSelectionExtent::Measure:
        {
            next_focus = measureJumpPosition(focus, later);
            break;
        }
        case TimeSelectionExtent::Section:
        {
            next_focus =
                adjacentSectionPosition(session().song().sections, focus, later).value_or(focus);
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
    const bool fine = action.fine;
    // The handlers below run full action dispatches that may reassign the selection variant or
    // the marker; the dispatched value is copied here so no handler ever holds a reference into
    // the object it (or a reentrant view callback) might replace. The lane branches are
    // deliberately reachable while playing — live automation editing during playback is a
    // supported workflow (the points port edits safely mid-play) — while chart branches stay
    // structurally paused-only because play clears the chart selection.
    if (const AutomationPointSelection* const point = selectedAutomationPoint())
    {
        const AutomationPointSelection selected = *point;
        moveSelectedAutomationPoint(selected, direction, fine);
        return;
    }
    if (!chartSelection().empty())
    {
        moveChartSelection(direction, fine);
        return;
    }
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && caret->lane.has_value())
    {
        const ChartCaret armed = *caret;
        createAndNudgeLanePointAtCaret(armed, direction, fine);
    }
}

// Moves the selected chart notes: Left/Right by one grid step — or one 1/960-beat fine step, the
// uniform precision tier; the move is relative either way, so an off-grid note keeps its offset
// under grid steps — and Up/Down across strings (fine has no meaning on the discrete string axis).
// A refused move (edge of the neck, occupied slot, grid origin collision) is a silent no-op — the
// selection stays put, matching refuse-not-clamp everywhere else.
void EditorController::Impl::moveChartSelection(ChartStepDirection direction, bool fine)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    common::core::Fraction beat_delta{};
    int string_delta = 0;
    switch (direction)
    {
        case ChartStepDirection::Left:
        case ChartStepDirection::Right:
        {
            const common::core::GridPosition reference = chartSelection().notes().front().position;
            const common::core::Fraction step =
                fine ? common::core::Fraction{1, 960} : chartGridStepBeats(reference);
            beat_delta = direction == ChartStepDirection::Right
                             ? step
                             : common::core::Fraction{-step.numerator, step.denominator};
            break;
        }
        case ChartStepDirection::Up:
        {
            string_delta = 1;
            break;
        }
        case ChartStepDirection::Down:
        {
            string_delta = -1;
            break;
        }
    }
    // A caret sitting exactly on the single moved note rides along (an object stop stays under
    // the caret through its own nudge); the marker moves directly — no re-arm — so the derived
    // selection cannot widen to a chord unit mid-nudge.
    const ChartCaret* const caret = armedChartCaret();
    const bool caret_rides = caret != nullptr && !caret->lane.has_value() &&
                             chartSelection().notes().size() == 1 &&
                             caret->position == chartSelection().notes().front().position &&
                             caret->string == chartSelection().notes().front().string;
    if (applyChartEditPlan(planMoveNotes(
            *arrangement->chart,
            session().song().tempo_map,
            chartSelection().notes(),
            beat_delta,
            string_delta,
            chartSelection().notes().size() == 1 ? "Move Note" : "Move Notes")) &&
        caret_rides && !chartSelection().empty())
    {
        m_chart_marker = ChartCaret{
            .position = chartSelection().notes().front().position,
            .string = chartSelection().notes().front().string,
        };
        updateView();
    }
}

// Deletes the selected notes as one compound undo entry; the selection empties with them.
void EditorController::Impl::deleteChartSelection()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    static_cast<void>(applyChartEditPlan(planDeleteNotes(
        *arrangement->chart, session().song().tempo_map, chartSelection().notes())));
}

// The Insert key's neutral create: the surface's neutral object appears at an armed EMPTY caret
// slot — a fret-0 note on a string row, an on-curve point on a lane row — and nothing else ever
// happens: occupied slots, selections, and the passive state are no-ops, so Insert never mutates
// existing objects.
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

    // String row. The action gate's prologue has already settled the pending fret entry, which
    // may have planted the very slot this verb would — in which case the typed value IS the insert
    // and this press has nothing left to do. So only an armed EMPTY slot inserts, asked of the
    // chart now rather than of the selection before the settle. Then the keyboard form of the same
    // verb Alt+click performs, through the same planting function.
    if (chartSlotOccupied(caret->position, caret->string))
    {
        return;
    }
    const ChartCaret armed = *caret;
    insertChartNoteAt(armed.position, armed.string, 0);
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
    if (std::string region_id = selectedToneRegionId(); !region_id.empty())
    {
        onToneRegionDeleteRequested(std::move(region_id));
    }
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
    if (m_chart_fret_entry.has_value() && combineChartFretEntry(digit, now_ms))
    {
        return;
    }
    if (chartSelection().empty())
    {
        insertChartFretAtCaret(digit, now_ms);
        return;
    }
    retypeChartSelectionFret(digit, now_ms);
}

// A digit while an entry is LIVE combines into it: the pending value widens to value*10+digit,
// replanned in full from the pre-entry base, and — at the current fret cap, where a second
// digit always exhausts the entry — settles immediately. An entry past its window settles
// first (a value you typed is a value you meant) and the digit falls through to a fresh flow,
// as does a combination past the fret cap: refused, never clamped, so the old value commits
// alone and the digit starts over.
bool EditorController::Impl::combineChartFretEntry(const int digit, const std::uint32_t now_ms)
{
    if (!m_chart_fret_entry.has_value())
    {
        return false;
    }
    // An INVALID entry is an open error state: its red box is visibly live however long it has
    // sat, so a digit always extends it. The window expiry binds valid entries only — and there
    // it is a belt, because the wake should already have settled an expired one.
    const bool invalid = !m_chart_fret_entry->plan.has_value() &&
                         m_chart_fret_entry->plan.error() == ChartPlanRefusal::Invalid;
    if (!invalid && now_ms - m_chart_fret_entry->armed_ms > g_fret_entry_window_ms)
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
// the combined value at its slot (undo removes the note), and a retype entry replans the whole
// selection from the pre-entry base, so a widened value can never compound on its own earlier
// digit.
std::expected<ChartNotesEditPlan, ChartPlanRefusal> EditorController::Impl::replanChartFretEntry(
    const ChartFretEntry& entry) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    if (const auto* const insert = std::get_if<ChartFretEntry::InsertAt>(&entry.target))
    {
        common::core::ChartNote note;
        note.position = insert->slot.position;
        note.string = insert->slot.string;
        note.fret = entry.value;
        return planInsertNote(
            *arrangement->chart,
            session().song().tempo_map,
            std::move(note),
            chartGridStepBeats(insert->slot.position));
    }
    const auto& retype = std::get<ChartFretEntry::Retype>(entry.target);
    if (retype.keys.empty())
    {
        return std::unexpected{ChartPlanRefusal::Invalid};
    }
    return planRetypeFrets(
        *arrangement->chart,
        session().song().tempo_map,
        retype.base_notes,
        entry.value,
        /*set_exact=*/true);
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
    if (entry.plan.has_value())
    {
        // An insert selects the planted note — the caret stays armed on it, so the next digit
        // retypes it, the same post-state the old immediate insert left. A retype rides the
        // default selection follow. Bound before the call so the move and the sibling read never
        // share one argument list.
        std::optional<std::vector<ChartNoteKey>> select_exactly;
        if (const auto* const insert = std::get_if<ChartFretEntry::InsertAt>(&entry.target))
        {
            select_exactly = std::vector<ChartNoteKey>{insert->slot};
        }
        static_cast<void>(applyChartEditPlan(std::move(*entry.plan), std::move(select_exactly)));
    }
    updateView();
}

// The one disposition rule for a freshly planned entry — a fresh digit or a combination: an
// INVALID value goes pending whatever its digits, because the red box must be SEEN, and it
// persists until a further digit, Esc, or any other intent settles it, never a timer (user
// re-ruling 2026-08-20); an extendable valid value waits out its window; every other valid
// value settles in the same keystroke.
void EditorController::Impl::armOrSettleChartFretEntry(ChartFretEntry entry)
{
    const bool invalid = !entry.plan.has_value() && entry.plan.error() == ChartPlanRefusal::Invalid;
    if (invalid || chartFretValueExtendable(entry.value))
    {
        armChartFretEntry(std::move(entry));
        return;
    }
    // An immediate digit is a pending entry that settles in the same keystroke.
    m_chart_fret_entry = std::move(entry);
    settleChartFretEntry();
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
            // An INVALID value outlives its window (user re-ruling 2026-08-20): the red box IS
            // the refusal display, and a display that vanishes on a timer is barely a display.
            // It stays until a further digit extends it or Esc / any other intent discards it.
            if (!m_chart_fret_entry->plan.has_value() &&
                m_chart_fret_entry->plan.error() == ChartPlanRefusal::Invalid)
            {
                return;
            }
            settleChartFretEntry();
        })));
}

// Fresh insert: with no selection, the typed digit becomes a note at the armed empty caret.
// While the marker is passive, digits are inert by design (the marker model) — a stray
// keystroke after listening authors nothing.
void EditorController::Impl::insertChartFretAtCaret(int digit, std::uint32_t now_ms)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const ChartCaret* const caret = armedChartCaret();
    if (arrangement == nullptr || !arrangement->chart.has_value() || caret == nullptr ||
        caret->lane.has_value())
    {
        // No caret, or the caret rides an automation lane row — lane typing is the
        // typed-value editor (routed in the view), never a fret insert.
        return;
    }
    ChartFretEntry entry{
        .value = digit,
        .target =
            ChartFretEntry::InsertAt{
                .slot = ChartNoteKey{.position = caret->position, .string = caret->string},
            },
        .armed_ms = now_ms,
    };
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
}

// Fresh retype: capture the selection's pre-entry values as the replan base, plan the typed
// digit in FULL — the pending box and its red state read the outcome, so even a refused digit
// visibly does something — then arm the window for a digit a second digit could extend, or
// settle in the same keystroke for one it could not. An Invalid provisional digit still arms:
// under a capo every playable fret's first digit alone refuses, and the window is what keeps
// the two-digit target reachable.
void EditorController::Impl::retypeChartSelectionFret(int digit, std::uint32_t now_ms)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    ChartFretEntry entry{
        .value = digit,
        .target =
            ChartFretEntry::Retype{
                .keys = chartSelection().notes(),
                .base_notes = chartNotesForKeys(chartSelection().notes()),
            },
        .armed_ms = now_ms,
    };
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
}

// The full note values behind a sorted key set, in chart order — the one selection-snapshot
// loop the typing and fret-shift verbs share.
std::vector<common::core::ChartNote> EditorController::Impl::chartNotesForKeys(
    const std::vector<ChartNoteKey>& keys) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return {};
    }
    return notesForKeys(arrangement->chart->notes, keys);
}

// Shifts every selected note's fret by one (Alt+Shift+wheel), shape-preserving by
// construction; a shift pushing the lowest fret below zero or the highest past the cap is
// refused by the planner, never clamped.
void EditorController::Impl::performActionImpl(const EditorAction::ShiftChartFrets& action)
{
    const int direction = action.direction;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || direction == 0)
    {
        return;
    }

    const std::vector<common::core::ChartNote> selected =
        chartNotesForKeys(chartSelection().notes());
    if (selected.empty())
    {
        return;
    }
    const int lowest = std::ranges::min(selected, {}, &common::core::ChartNote::fret).fret;

    static_cast<void>(applyChartEditPlan(planRetypeFrets(
        *arrangement->chart,
        session().song().tempo_map,
        selected,
        lowest + (direction > 0 ? 1 : -1),
        /*set_exact=*/false)));
}

// Grows or shrinks the selection's rings by one grid step — moving each ring's END onto the
// adjacent grid line — or by one 1/960-beat fine step, the uniform Ctrl precision tier on the
// extent verbs, as ONE GESTURE (user ruling 2026-08-22): every press APPENDS its step to the run's
// list, the whole selection is re-planned by replaying that list over the rings the gesture STARTED
// at, and the run stays one undo entry that always describes start → now. That is what makes the
// verb symmetric: a chord member pinned at its own bound on the way out rejoins its neighbours
// exactly where it left them on the way back, instead of each step baking the clamp into the next
// step's starting value.
//
// The list replaced a single accumulated delta (user bug 2026-08-23): a grid step has no size to
// sum, because what it adds is whatever reaches the next line from where the ring's end currently
// sits — and a summed delta therefore carried a Ctrl fine-tuned remainder through every later grid
// step, leaving the ring permanently off-grid.
//
// The gesture is live while the shared window proof holds (the same selection, and the burst record
// still owning the history top), so it ends at every commit point the technique toggle ends at —
// and a press after any of them starts a new gesture from the current values. The step arithmetic
// and the ring rules are the planner's alone (planAdjustSustain).
void EditorController::Impl::performActionImpl(const EditorAction::AdjustChartSustain& action)
{
    const int direction = action.direction;
    const bool fine = action.fine;
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty() ||
        direction == 0)
    {
        return;
    }

    // A grid step records the session's NOTE VALUE rather than a beat amount, because the planner
    // snaps the ring's end onto that grid's own lines: the meter at whatever measure the end lands
    // in scales the value there, so nothing here needs to know where any ring ends.
    const ChartSustainStep step{
        .grid_note_value =
            fine ? std::optional<common::core::Fraction>{} : std::optional{m_grid_note_value},
        .grow = direction > 0,
    };
    // Bound once as a pointer so every read below is provably behind the null check, the shape this
    // file uses wherever an optional's guarantee has to survive intervening calls. A live gesture
    // and the burst record are present together — the gesture's own proofs demand the record — so
    // this pointer is exactly "a gesture is running". It dies with any reassignment of the window,
    // so the steps are copied out of it here, before anything below can touch that field.
    const std::vector<ChartSustainStep>* const live = liveChartSustainGestureSteps();
    // Grid and fine steps mix freely inside one gesture; the list keeps them in the order they were
    // pressed, which is the only order that replays what the user did.
    std::vector<ChartSustainStep> steps;
    if (live != nullptr)
    {
        steps = *live;
    }
    steps.push_back(step);
    ChartNotesTopEntry* const burst =
        live != nullptr && m_chart_notes_top.has_value() ? &*m_chart_notes_top : nullptr;

    // The stream the gesture started from. Mid-gesture it is reconstructed by reversing exactly
    // what the top entry applied — the settle fold's own method, and the reason the gesture keeps
    // no snapshot of its own: that entry already holds the pre-gesture values, so a second copy
    // could only disagree with it.
    std::vector<common::core::ChartNote> base = arrangement->chart->notes;
    if (burst != nullptr)
    {
        common::core::Chart pre_gesture = *arrangement->chart;
        if (!applyChartNotesChange(pre_gesture, burst->plan.inserted, burst->plan.removed)
                 .has_value())
        {
            reportError("Could not apply chart edit: " + burst->plan.label);
            return;
        }
        base = std::move(pre_gesture.notes);
    }
    std::expected<ChartNotesEditPlan, ChartPlanRefusal> plan = planAdjustSustain(
        *arrangement->chart, session().song().tempo_map, base, chartSelection().notes(), steps);
    if (!plan.has_value())
    {
        // Invalid is the gate refusing the result, so this step never happened: it is not recorded
        // (the appended list is local until the window is armed below), and a running gesture keeps
        // the entry and the steps it had.
        //
        // NoChange is the gesture standing exactly where it started — every ring already at its
        // bound on a first step, or a run that replayed back to its start — so it describes no edit
        // at all. A first step then arms nothing, and the next press in the other direction starts
        // from the current rings rather than paying back steps that never moved anything; a
        // running gesture RETIRES the entry it pushed, because an entry describing nothing is a
        // dead Ctrl+Z on a document reported modified that is byte-identical to the saved file.
        if (plan.error() == ChartPlanRefusal::NoChange && burst != nullptr)
        {
            retireChartSustainGesture(burst->plan);
        }
        return;
    }

    if (burst == nullptr)
    {
        if (!applyChartEditPlan(std::move(plan)))
        {
            return;
        }
    }
    else
    {
        // The history entry is swapped BEFORE the model moves, the settle fold's discipline: the
        // two states must never disagree, and the live-gesture proofs above are exactly
        // replaceTop's own preconditions, so a refusal here is a logic error reported with the
        // chart untouched rather than left between two entries.
        if (m_undo_history.replaceTop(std::make_unique<ChartNotesEdit>(*plan)).status !=
            EditorUndoTransitionStatus::Applied)
        {
            reportError("Could not apply chart edit: " + plan->label);
            return;
        }
        common::core::Chart* const chart = m_session.currentChart();
        // Walk the live chart back to the pre-gesture stream and then to the re-planned one, so
        // the state the top entry describes is exactly the state the chart holds.
        if (chart == nullptr ||
            !applyChartNotesChange(*chart, burst->plan.inserted, burst->plan.removed).has_value() ||
            !applyChartNotesChange(*chart, plan->removed, plan->inserted).has_value())
        {
            reportError("Could not apply chart edit: " + plan->label);
            return;
        }
        // The burst record follows the entry it names, or the next step would reverse a plan the
        // history no longer holds.
        burst->plan = std::move(*plan);
        updateView();
    }

    // Both paths arm the same window: the live selection, and the step list the next press appends
    // to. Read back from the selection rather than carried across the apply, so the keys are
    // always the ones the next press will compare against.
    m_chart_verb_window = ChartVerbWindow{
        .keys = chartSelection().notes(),
        .verb = ChartSustainGesture{.steps = std::move(steps)},
    };
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
bool EditorController::Impl::chartVerbWindowHolds(const std::vector<ChartNoteKey>& armed_keys) const
{
    return m_chart_notes_top.has_value() && armed_keys == chartSelection().notes() &&
           m_undo_history.snapshot().position == m_chart_notes_top->history_position;
}

// The steps of the gesture a duration press continues, or nullptr when the press starts one. The
// pointer aliases m_chart_verb_window, so a caller must copy what it needs before anything can
// reassign that field.
//
// Beyond the shared proof it asks the fold's own precondition: a save mid-gesture makes the entry
// the file's clean state, and replaceTop refuses to rewrite that (widening it would make "return to
// clean" restore different content than the file holds). So a save ENDS the gesture, exactly like
// any other commit point, and the next step opens a fresh one from the saved rings.
const std::vector<ChartSustainStep>* EditorController::Impl::liveChartSustainGestureSteps() const
{
    if (!m_chart_verb_window.has_value() || !chartVerbWindowHolds(m_chart_verb_window->keys))
    {
        return nullptr;
    }
    const auto* const gesture = std::get_if<ChartSustainGesture>(&m_chart_verb_window->verb);
    if (gesture == nullptr || m_undo_history.isAtCleanState())
    {
        return nullptr;
    }
    return &gesture->steps;
}

// Ends a duration gesture that describes nothing, by taking back the entry its first step pushed
// and walking the chart back to the stream that entry was applied to. What a run replaying back to
// its start has to leave behind: an entry describing nothing is a dead Ctrl+Z, and it would report
// the document modified while it is byte-identical to the saved file.
//
// This is the technique toggle's own "the pair leaves no trace" mechanism (dropTop). It needs no
// clean-state alternative, which that verb does need, because a save ends the gesture BEFORE a step
// can reach here — liveChartSustainGestureSteps refuses at the clean state, so a live gesture and
// dropTop's preconditions are the same thing.
//
// applied: the plan the entry holds, which is why the record naming it is retired last.
void EditorController::Impl::retireChartSustainGesture(const ChartNotesEditPlan& applied)
{
    // The history moves BEFORE the model, this file's discipline everywhere: the two states must
    // never disagree, and a live gesture is exactly dropTop's precondition, so a refusal here is a
    // logic error reported with the chart untouched.
    if (m_undo_history.dropTop().status != EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + applied.label);
        return;
    }
    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr ||
        !applyChartNotesChange(*chart, applied.inserted, applied.removed).has_value())
    {
        reportError("Could not apply chart edit: " + applied.label);
        return;
    }
    // The entry both the record and the window name is gone, so both go with it: the next press
    // opens a fresh gesture from rings that ARE the pre-gesture rings. (Between the drop and here
    // the record is already inert — every reader proves ownership by the history position first.)
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    updateView();
}

// The technique verbs' toggle window (D14 ruling 4), shared by every verb that has one rather than
// copied into each: while the selection and the burst record still prove the previous press was
// this verb's own entry, this press REVERSES that entry exactly, so the pair leaves no trace —
// including tails an assist grew, which a verb's own clear law could never restore.
//
// ALWAYS disarms, reversal or not: a press whose proofs fail commits the previous entry, which is
// what makes the window end at the next selection change or caret move. A window the DURATION verb
// armed disarms here too, for the same reason — a technique press is another verb, so the gesture
// it interrupts is over.
//
// technique: the verb pressed now; only a press of the technique that armed the window reverses.
// Returns true when this press was consumed by a reversal, so the caller must not plan.
bool EditorController::Impl::reverseTechniqueToggleWindow(const ChartTechnique technique)
{
    if (!m_chart_verb_window.has_value())
    {
        return false;
    }
    const ChartVerbWindow window = std::move(*m_chart_verb_window);
    m_chart_verb_window.reset();
    const auto* const toggle = std::get_if<ChartTechniqueToggle>(&window.verb);
    if (toggle == nullptr || toggle->technique != technique)
    {
        return false;
    }
    const std::vector<ChartNoteKey>& armed_keys = window.keys;
    const std::string revert_label =
        "Revert " + (technique == ChartTechnique::Legato
                         ? std::string{"Legato"}
                         : std::string{chartTechniqueLaw(technique).noun});
    // Bound once so every read below is provably behind the has_value check, the shape this file
    // uses wherever an optional's guarantee has to survive intervening calls.
    const ChartNotesTopEntry* const burst =
        m_chart_notes_top.has_value() ? &*m_chart_notes_top : nullptr;
    if (burst == nullptr || !chartVerbWindowHolds(armed_keys))
    {
        return false;
    }
    const ChartNotesEditPlan applied = burst->plan;
    // The history moves BEFORE the model, the settle sweep's own discipline: the two states must
    // never disagree, and the guards above are exactly the history's preconditions, so a refusal
    // here is a logic error reported with the chart untouched rather than left reversed under an
    // entry that still describes the edit.
    //
    // A save mid-window makes the entry the file's clean state, so erasing it would make "return
    // to clean" a lie. The reversal still happens — the toggle stays genuine and the grown tail
    // comes back — but as its own inverse entry, which leaves the session correctly dirty
    // (ruled 2026-08-11).
    const bool clean_entry = m_undo_history.isAtCleanState();
    m_chart_notes_top.reset();
    if (clean_entry)
    {
        pushUndoEntry(
            std::make_unique<ChartNotesEdit>(ChartNotesEditPlan{
                .removed = applied.inserted,
                .inserted = applied.removed,
                .label = std::string{revert_label},
            }));
    }
    else if (m_undo_history.dropTop().status != EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + applied.label);
        return true;
    }
    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr ||
        !applyChartNotesChange(*chart, applied.inserted, applied.removed).has_value())
    {
        reportError("Could not apply chart edit: " + applied.label);
        return true;
    }
    updateView();
    return true;
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
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    if (reverseTechniqueToggleWindow(technique))
    {
        return;
    }
    const std::vector<ChartNoteKey> keys = chartSelection().notes();
    if (technique == ChartTechnique::Legato)
    {
        toggleChartLegato(keys);
        return;
    }

    const ChartTechniqueLaw law = chartTechniqueLaw(technique);
    const std::vector<common::core::ChartNote> selected = chartNotesForKeys(keys);
    if (selected.empty())
    {
        return;
    }
    const bool all_carry = std::ranges::all_of(selected, law.carries);
    const std::string label = all_carry ? "Remove " + std::string{law.noun} : std::string{law.noun};
    if (applyChartEditPlan(
            law.plan(*arrangement->chart, session().song().tempo_map, keys, !all_carry, label)))
    {
        m_chart_verb_window = ChartVerbWindow{
            .keys = keys,
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
void EditorController::Impl::toggleChartLegato(const std::vector<ChartNoteKey>& keys)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return;
    }
    ChartLegatoPlan planned =
        planSetLegato(*arrangement->chart, session().song().tempo_map, keys, "Legato");
    if (planned.plan.has_value())
    {
        if (applyChartEditPlan(std::move(*planned.plan)))
        {
            m_chart_verb_window = ChartVerbWindow{
                .keys = keys,
                .verb = ChartTechniqueToggle{.technique = ChartTechnique::Legato},
            };
        }
        return;
    }
    // Nothing left to claim, so this press clears — the stored claims only.
    std::vector<ChartNoteKey> legato_keys;
    for (const common::core::ChartNote& note : chartNotesForKeys(keys))
    {
        if (note.attack == common::core::NoteAttack::Legato)
        {
            legato_keys.push_back(ChartNoteKey{.position = note.position, .string = note.string});
        }
    }
    std::expected<ChartNotesEditPlan, ChartPlanRefusal> clear_plan =
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
        if (applyChartEditPlan(std::move(clear_plan)))
        {
            m_chart_verb_window = ChartVerbWindow{
                .keys = keys,
                .verb = ChartTechniqueToggle{.technique = ChartTechnique::Legato},
            };
        }
        return;
    }
    // A press that changed nothing is SILENT, exactly like every other technique verb that applies
    // nothing: selecting a phrase's first note and pressing H is the commonest press there is, and
    // it is not an error. `planned` still carries the count and the dominant reason — that IS the
    // feedback payload — but the only reporting seam the view offers today is a modal "Could not
    // complete request" box, which interrupts a keystroke to say nothing failed. The count surfaces
    // once W5's non-modal refusal channel exists; until then the spec's counted-skip half is
    // deferred rather than mis-routed.
}

// Sets the selection to the left-hand tap attack as one compound undo entry, uniform scope. The
// stating verb beside the inferring toggle (Ctrl means precision): the fretting hand striking a
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
    static_cast<void>(settleChartLegato());
    if (consumed)
    {
        updateView();
    }
}

// The Esc ladder (the marker model): an in-flight pointer gesture is abandoned without
// mutating; else an armed caret — on any row, lane carets included — dissolves to the passive
// cursor in its place, keeping the selection; else THE selection clears, whatever its kind
// (one selection editor-wide, so Esc's last rung is kind-agnostic like Delete's dispatch; a
// region deselect routes through applyToneSelection so the audible tone re-syncs). The marker
// rungs also end the multi-digit fret-entry window — after a cancel, the next digit must not
// widen a dead entry.
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

    // An INVALID pending fret value claims its own rung (user ruling): Esc cancels the PROBLEM,
    // so the value discards and the caret survives for an immediate retype. A VALID pending
    // value is not a cancellable thing — it falls through to the caret rung below and commits
    // on the way through the uniform settle, because a value you typed is a value you meant.
    if (m_chart_fret_entry.has_value() && !m_chart_fret_entry->plan.has_value() &&
        m_chart_fret_entry->plan.error() == ChartPlanRefusal::Invalid)
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

    if (!selectedToneRegionId().empty())
    {
        applyToneSelection({});
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
bool EditorController::Impl::settleChartLegato()
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
    const ChartNotesEditPlan* const burst =
        m_chart_notes_top.has_value() && m_chart_notes_top->history_position == history.position &&
                !m_undo_history.isAtCleanState()
            ? &m_chart_notes_top->plan
            : nullptr;

    // The folded entry has to describe the WHOLE burst, so it is diffed against the pre-burst
    // stream, reconstructed by reversing exactly what the top entry applied. (Re-planning forward
    // from the current values could not produce it: the burst's own plan is what carries the edit.)
    std::vector<common::core::ChartNote> base = arrangement->chart->notes;
    std::string label{"Settle Legato"};
    if (burst != nullptr)
    {
        common::core::Chart pre_burst = *arrangement->chart;
        if (!applyChartNotesChange(pre_burst, burst->inserted, burst->removed).has_value())
        {
            return false;
        }
        base = std::move(pre_burst.notes);
        label = burst->label;
    }
    std::optional<ChartNotesEditPlan> settled =
        planSettleLegato(*arrangement->chart, session().song().tempo_map, base, label);
    if (!settled.has_value())
    {
        // Nothing to settle, so both coalescing windows stay armed exactly as they were.
        return false;
    }

    // The history entry is swapped BEFORE the model moves, because the two states must never
    // disagree: the guards above are exactly replaceTop's own preconditions, so a refusal is a
    // logic error, and reporting it leaves the chart untouched instead of stranded between entries.
    if (burst != nullptr &&
        m_undo_history.replaceTop(std::make_unique<ChartNotesEdit>(*settled)).status !=
            EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + settled->label);
        return false;
    }
    common::core::Chart* const chart = m_session.currentChart();
    // Walk the live chart back to pre-burst and then to the settled state, so the state the history
    // top describes is exactly the state the chart holds (the fret-entry widen's own discipline).
    if (chart == nullptr ||
        (burst != nullptr &&
         !applyChartNotesChange(*chart, burst->inserted, burst->removed).has_value()) ||
        !applyChartNotesChange(*chart, settled->removed, settled->inserted).has_value())
    {
        reportError("Could not apply chart edit: " + settled->label);
        return false;
    }
    if (burst == nullptr)
    {
        // At the top of the stack a push truncates nothing, so the flatten simply becomes its own
        // undo step.
        pushUndoEntry(std::make_unique<ChartNotesEdit>(*settled));
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
