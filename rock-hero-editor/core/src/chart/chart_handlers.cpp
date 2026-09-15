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

// True where the target is a note's held-stop SATELLITE rather than a glyph that selects by being
// clicked. A satellite is its note's held face and nothing else (SATELLITES ARE NOTE-SCOPED), and
// the press settles it whole: it hands the caret that note's other stop, preserving a wider
// selection where the note is already in one. So the release's collapse — which exists to reduce a
// chord selection to the head that was clicked — must not run for it, or it would take back exactly
// the selection the press preserved.
[[nodiscard]] bool chartSatelliteTarget(const ChartHitTarget& target) noexcept
{
    return std::holds_alternative<ChartHeldStopHit>(target);
}

// One callable per alternative for std::visit, so a variant that gains an alternative fails to
// compile at every visit that has not said what the new one means.
template <typename... Handlers> struct Overloaded : Handlers...
{
    using Handlers::operator()...;
};

} // namespace

// The memoized PRESENTED projection deriveViewState pushed, which is what pointer events resolve
// against; null while no chart is displayed. It is not the whole of what the lane DRAWS: a
// selected note (and every note while the reveal is held) draws its longer actual ring, and that
// extra length is deliberately not hit-testable — see EditorViewState::tab_actual.
const common::core::ChartViewState* EditorController::Impl::displayedTabProjection() const
{
    return m_tab_view_state.get();
}

// Each authored array is sorted by (position, string) and the tab projection preserves that order
// one to one, so a projection index addresses the chart record directly — the same rule for every
// kind, which is why the hit target names its own kind rather than the caller assuming one.
//
// A keyframe hit is the one that cannot stop at the note: its identity is (note slot, offset), and
// the offset comes off the DRAWN keyframe the pointer landed on rather than off the chart, because
// the drawn list holds only the keyframes the lane actually shows.
std::optional<ChartSelectionKey> EditorController::Impl::chartSelectionKeyAt(
    const ChartHitTarget& target) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const common::core::ChartViewState* const tab = displayedTabProjection();
    return std::visit(
        [&chart, tab](const auto& hit) -> std::optional<ChartSelectionKey> {
            using Hit = std::remove_cvref_t<decltype(hit)>;
            // A held-stop satellite resolves to its own note, exactly as its head does: it is a
            // second MARK of one object, never a second object. What the two hits differ in is the
            // caret channel the press then arms, which is the caller's question rather than this
            // one's.
            if constexpr (!std::is_same_v<Hit, ChartKeyframeHit>)
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
                const std::vector<common::core::KeyframeViewState>& drawn =
                    tab->notes[hit.note_index].slides;
                if (hit.keyframe_index >= drawn.size())
                {
                    return std::nullopt;
                }
                return ChartKeyframeKey{
                    .note = chartSlotKeyOf(chart.notes[hit.note_index]),
                    .offset = drawn[hit.keyframe_index].offset,
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
    ChartSelection& emplaced = m_selection.emplace<ChartSelection>();
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

// What the chart holds on a slot, or absent when nothing does. The note stream holds each slot at
// most once, silently-held stops included, so the note half is one binary search. Where no note
// stands, the only object that can is a keyframe of the one ring covering the slot — which is
// chartPathTailAt's question, answered there once for the typed digit and this — so this asks
// it and then only checks whether a keyframe sits at exactly that offset. Exact rationals, so
// equality is the test. The one coincidence the chart's laws allow, a silently-held stop at a
// keyframe's instant on its own string (a hold bounds no ring), resolves to the note: the stream's
// own record, and the one the binary search finds first.
//
// Shared by caret arming (selection re-derivation) and every verb that asks what stands at a slot.
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
    if (!tail.has_value())
    {
        return std::nullopt;
    }
    // The tail names a note of this very stream, so the search lands on it.
    const auto carrier = std::ranges::lower_bound(notes, tail->note, {}, slot_of);
    if (carrier != notes.end() &&
        std::ranges::find(carrier->keyframes, tail->offset, &common::core::Keyframe::offset) !=
            carrier->keyframes.end())
    {
        return ChartKeyframeKey{.note = tail->note, .offset = tail->offset};
    }
    return std::nullopt;
}

// Drops every selection key naming an object the chart no longer holds — the undo/redo
// transition's selection repair, and the only caller it has.
//
// A key outliving its object is THE DISSOLVE LAW'S LINGER, and inside a verb window it is
// deliberate: a second press proves it is acting on the same selection, so a keyframe key stays
// after the keyframe it named dissolved. The linger is scoped to a live window, though, and
// undo/redo ENDS one (performActionImpl(Undo) disarms it before either direction replays). Past
// the transition the key is pure staleness, and it is not inert: a digit routes by the RETYPE
// OPERAND, so a selection holding a key that resolves to nothing swallows the keystroke — the
// retype finds no operand and the caret never gets to author there. Undoing an insert and typing
// again at the same slot did exactly nothing, the undo twin of the delete the empty selection
// fixed.
//
// PRUNED, not cleared: a selection the transition left whole is still the user's scope, including
// one an armed caret deliberately does not own (armChartHeldStopHandle keeps a chord selected
// while the caret reaches one member's satellite). Resolution is chartObjectAt's — the ONE
// occupancy question — asked at each key's own slot and confirmed by equality, so no second rule
// about what a key names can drift from it.
void EditorController::Impl::dropChartSelectionKeysNamingNothing()
{
    if (chartSelection().empty())
    {
        return;
    }
    const std::vector<ChartSelectionKey> selected = chartSelection().keys();
    std::vector<ChartSelectionKey> surviving;
    surviving.reserve(selected.size());
    for (const ChartSelectionKey& key : selected)
    {
        const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, key);
        if (const std::optional<ChartSelectionKey> object =
                chartObjectAt(slot.position, slot.string);
            object.has_value() && *object == key)
        {
            surviving.push_back(key);
        }
    }
    if (surviving.size() == selected.size())
    {
        return;
    }
    chartSelectionMutable().applyBox(surviving, false);
}

// Whether this PROJECTED note's whole truth is on show — the lane's own reveal predicate
// (\ref chartNoteRevealed), asked with the controller's inputs rather than the lane's. The rule is
// one function; only the way each layer holds the selection and the caret differs, and neither
// layer may spell the rule itself.
//
// The LANE REVEAL is the one input this side does not hold: the modifier is sampled per frame in
// the view and deliberately never pushed here (tab_view.h). A pointer event carries it — the press
// that reaches a mark knows whether Alt was down — so the caller passes what it knows, and the
// keyboard paths pass false: with Alt down they are not reachable, and a caret's own note is
// revealed by the two inputs below anyway.
bool EditorController::Impl::chartNoteRevealed(
    const std::size_t index, const bool lane_reveal) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const common::core::ChartViewState* const actual = m_tab_actual_view_state.get();
    if (arrangement == nullptr || !arrangement->chart.has_value() || actual == nullptr ||
        index >= actual->notes.size() || index >= arrangement->chart->notes.size())
    {
        return false;
    }
    // The caret's own two facts, resolved the way the published state resolves them, so the peek
    // measures the same instant the lane's does.
    std::optional<ChartCaretPeek> peek;
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && !caret->lane.has_value())
    {
        peek = ChartCaretPeek{
            .seconds = caretTimeBounds(session().song().tempo_map, caret->position).seconds,
            .string = caret->string,
        };
    }
    const ChartSelectionKey key{
        ChartNoteKey{.slot = chartSlotKeyOf(arrangement->chart->notes[index])}
    };
    return core::chartNoteRevealed(
        actual->notes[index], lane_reveal, chartSelection().contains(key), peek);
}

// Whether the note at this slot is IN FOCUS for the keyframe commit law: revealed by the lane's own
// predicate — selected, or the caret standing inside its ring — or carrying a selected point. The
// last arm is the one the reveal does not ask, because a multi-selection of points dissolves the
// caret: a point under scrutiny keeps its note in focus exactly as the caret on it would.
bool EditorController::Impl::chartNoteInFocus(const ChartSlotKey& slot) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return false;
    }
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const auto note =
        std::ranges::lower_bound(notes, slot, {}, [](const common::core::ChartNote& candidate) {
            return chartSlotKeyOf(candidate);
        });
    if (note == notes.end() || chartSlotKeyOf(*note) != slot)
    {
        return false;
    }
    if (chartNoteRevealed(static_cast<std::size_t>(note - notes.begin()), false))
    {
        return true;
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
    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr)
    {
        return false;
    }
    for (const std::size_t index : dissolving)
    {
        static_cast<void>(common::core::stripSilentKeyframes(chart->notes[index]));
    }
    m_chart_notes_top.reset();
    disarmChartVerbWindow();
    updateView();
    return true;
}

// True when the note at this slot SHOWS a satellite digit for its held stop. Read from the
// PROJECTION and never re-derived: the mark says whether a face is drawn and on what terms, so
// asking the drawn picture is what keeps the caret's second stop, the click target and the mark
// itself from ever disagreeing about whether there is one.
//
// THE CARET IS ITSELF A REVEAL, which is why the terms below are met rather than computed. Every
// reader of this predicate is a caret standing on the note or moving onto it, and arming a caret
// SELECTS what sits under it — one of the reveal's own grounds — so a reveal-only satellite is
// drawn exactly because of the act that asks. Asking the note's current reveal instead would demote
// a caret off the mark the press itself brings in, since the press resolves this before the
// selection it derives. The presence rule is still spelled through its one authority rather than
// short-circuited here, so a face with different terms would be answered rather than assumed.
bool EditorController::Impl::chartSlotShowsHeldStop(const ChartSlotKey& slot) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (arrangement == nullptr || !arrangement->chart.has_value() || tab == nullptr)
    {
        return false;
    }
    // The projection preserves the chart's note order one to one, so the chart index addresses the
    // projected note directly — the same rule every hit target and selection resolution here uses.
    const std::vector<common::core::ChartNote>& notes = arrangement->chart->notes;
    const auto found = std::ranges::lower_bound(
        notes, slot, {}, [](const auto& note) { return chartSlotKeyOf(note); });
    if (found == notes.end() || chartSlotKeyOf(*found) != slot)
    {
        return false;
    }
    const auto index = static_cast<std::size_t>(found - notes.begin());
    if (index >= tab->notes.size())
    {
        return false;
    }
    // Bound to a local so the optional check and the access are provably the same object.
    const common::core::NoteViewState& projected = tab->notes[index];
    const std::optional<common::core::StopMarkViewState>& mark = projected.stop_mark;
    return projected.held.has_value() && mark.has_value() &&
           common::core::stopMarkShown(*mark, /*revealed=*/true);
}

// THE caret's stop, and the one place the held channel's precondition is applied at READ time: the
// stored value is what the last arming asked for, and a Held request is worth only what the drawn
// picture still says. An edit can take the satellite out from under a stationary caret — clearing
// the stop is exactly what Delete on it does — and a caret left claiming a mark that is gone would
// point the next digit at nothing. Asked through the same predicate the arming asks, so the rule
// is one predicate applied at two moments rather than two rules.
common::core::ChartStopChannel EditorController::Impl::chartCaretChannel() const
{
    const ChartCaret* const caret = armedChartCaret();
    if (caret == nullptr || caret->lane.has_value() ||
        caret->channel != common::core::ChartStopChannel::Held)
    {
        return common::core::ChartStopChannel::Sounding;
    }
    const ChartSlotKey slot{.position = caret->position, .string = caret->string};
    return chartSlotShowsHeldStop(slot) ? common::core::ChartStopChannel::Held
                                        : common::core::ChartStopChannel::Sounding;
}

void EditorController::Impl::armChartCaret(
    common::core::GridPosition position, int string, common::core::ChartStopChannel channel)
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
    // The held channel exists only where the satellite that states it is DRAWN, so a request the
    // slot cannot honour lands on the stop every note has instead of parking the caret on a mark
    // that is not there. Enforced here rather than at each caller of this funnel — but this is not
    // the only writer of the channel: armChartHeldStopHandle below writes it too, and deliberately
    // outside this arm, because its whole point is to reach a satellite the pointer has just hit
    // without re-deriving the selection the way this arm does. What makes a stale Held claim
    // harmless whichever writer left it is the READ (chartCaretChannel), which asks this same
    // predicate again at the moment the channel is spent. One predicate at three sites, rather
    // than one gate every path must pass.
    if (channel == common::core::ChartStopChannel::Held && !chartSlotShowsHeldStop(key))
    {
        channel = common::core::ChartStopChannel::Sounding;
    }
    m_chart_marker = ChartCaret{.position = position, .string = string, .channel = channel};
    if (const std::optional<ChartSelectionKey> object = chartObjectAt(position, string);
        object.has_value())
    {
        // Whatever the slot holds becomes the selection — a sounding note, a silently-held stop or
        // a keyframe alike, so the armed-caret invariant reads the same for every kind and a verb
        // finds its own object selected after it authors one.
        chartSelectionMutable().replaceWith(*object);
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

// THE SELECTION HANDLE: a selected note's satellite belongs to the selection, so reaching for it
// moves the caret onto that note's held stop and PRESERVES what is selected. Deliberately not
// armChartCaret, whose whole job is to re-derive the selection from the slot under it: collapsing a
// chord to one member because the charter aimed at that member's held stop would take the scope
// away in the very act of naming a stop within it.
//
// The SECOND writer of the Held channel, and it applies no precondition of its own: the caller
// reached here by hitting a satellite that is drawn, which is the very thing armChartCaret's
// demotion tests for, and chartCaretChannel asks it again at the read whatever this leaves behind.
void EditorController::Impl::armChartHeldStopHandle(const ChartSlotKey& slot)
{
    settleChartFretEntry();
    disarmChartVerbWindow();
    m_chart_marker = ChartCaret{
        .position = slot.position,
        .string = slot.string,
        .channel = common::core::ChartStopChannel::Held,
        .lane = {},
    };
    // A caret move is a settle point whatever else it does, which is the one thing this shares with
    // the arm it deliberately is not: the press that reached this stop is where a claim the chart
    // no longer justifies gets written down as the pick it plays as.
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
    m_chart_marker =
        ChartCaret{.position = position, .string = chartMarkerString(), .lane = std::move(row)};
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

// The caret row's next authored object strictly beyond the caret in the step direction: notes and
// their keyframes on the caret's string (the notes alone with notes_only), points on its lane.
// Linear scans are fine at keypress cadence.
std::optional<common::core::GridPosition> EditorController::Impl::nextRowObjectStop(
    const ChartCaret& caret, const bool later, const bool notes_only)
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
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    for (const common::core::ChartNote& note : arrangement->chart->notes)
    {
        if (note.string != caret.string)
        {
            continue;
        }
        consider(note.position);
        if (notes_only)
        {
            continue;
        }
        // A keyframe is an authored object on this string as much as the note it rides, standing
        // on its own slot along the ring, so the walk stops on it exactly as it stops on a note.
        for (const common::core::Keyframe& keyframe : note.keyframes)
        {
            consider(common::core::advanceGridPosition(tempo_map, note.position, keyframe.offset));
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
    const common::core::ChartViewState* const tab = displayedTabProjection();
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

// One GRID step in beats at a position: the session's grid note value scaled by the local meter.
// This is the musical UNIT the user is authoring in, so it answers duration questions — the ring a
// placement gives a new note is the standing one. It deliberately ignores grid snap: snap decides
// where things go, never how long they are, and a tick-long default ring would be absurd.
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
    std::expected<ChartEditPlan, ChartPlanRefusal> plan,
    std::optional<std::vector<ChartSelectionKey>> select_exactly)
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

    if (const auto applied = applyChartChange(*chart, *plan); !applied.has_value())
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

    // The selection follows the edit: retyped/moved/inserted records stay selected under their
    // new keys, deleted ones drop out (their keys no longer resolve).
    if (select_exactly.has_value())
    {
        chartSelectionMutable().applyBox(*select_exactly, false);
    }
    else
    {
        // (selection - removed keys) + inserted keys: retyped/resized notes stay selected even
        // when the edit left some of them unchanged, moved notes follow to their new keys, and
        // deleted ones drop out.
        std::vector<ChartSelectionKey> next_selection;
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
        // verb there is — and that is what carries the dissolve law's linger: the key stays
        // selected after the keyframe it named dissolved, so a second press inside the verb
        // window still proves it is acting on the same selection and reverses exactly. A note the
        // plan MOVED or DELETED takes its keyframes' keys with it, because the key names the old
        // slot and the plan carries no map from an old slot to a new one.
        for (const ChartKeyframeKey& keyframe : chartSelection().keyframes())
        {
            if (!in_side(plan->removed, keyframe.note) || in_side(plan->inserted, keyframe.note))
            {
                next_selection.emplace_back(keyframe);
            }
        }
        chartSelectionMutable().applyBox(next_selection, false);
    }

    // The history takes the WRITTEN form of the transition and the burst record the whole of it
    // (writtenChartPlan): a transition that only planted or moved a silent point writes as nothing,
    // so the point stands in the chart with no entry and no record at all, and the next edit on its
    // note diffs from the written state before it. The record is what the settle sweep folds its
    // flatten into, so the edit and the claim it broke undo together, and what the technique
    // toggle windows reverse; recorded only for an entry the history actually took — a refused
    // push resets the history, and a record claiming to own its top would then be a false proof.
    ChartEditPlan written = writtenChartPlan(*plan);
    if (!written.empty() && pushUndoEntry(std::make_unique<ChartEdit>(std::move(written))))
    {
        m_chart_notes_top = ChartNotesTopEntry{
            .plan = std::move(*plan),
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
    // The press knows whether the lane reveal is up, because the modifier that holds it is the one
    // this event carries: a satellite the reveal brought in is reachable while it is drawn, which
    // is the whole of "nothing undrawn is clickable" for it.
    gesture.hit_target = chartHitTarget(
        *tab, event.geometry, event.x, event.y, [this, &event](const std::size_t index) {
            return chartNoteRevealed(index, event.modifiers.alt);
        });
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

    // THE SATELLITE IS ITS NOTE'S HELD FACE, always: a press on one addresses that note's held
    // stop, so the digits that follow state it — the pointer twin of stepping the caret onto that
    // stop, and the whole of what makes the satellite an independent target rather than a second
    // selection kind. Ctrl and the double click keep their own meanings below: they are selection
    // gestures, and a satellite selects its note like any other mark.
    const bool satellite = chartSatelliteTarget(*gesture.hit_target);
    // A press that CHANGES the channel re-arms even on an already-selected note, and it is the one
    // reason to: clicking the head of a note whose caret sits on its satellite changes which stop
    // the next digit states, and a press that left the caret alone would silently point it at the
    // other one. Every press that does not change it keeps the standing selection and marker
    // untouched — the gap a future drag-move gesture lives in. Asked against Sounding because every
    // target reaching the branches below addresses a note's own head.
    const bool channel_changes = chartCaretChannel() != common::core::ChartStopChannel::Sounding;

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
    else if (event.clicks >= 2)
    {
        chartSelectionMutable().replaceWith(group_at());
        dissolveChartCaretInPlace();
        static_cast<void>(settleChart());
    }
    else if (satellite)
    {
        const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
        // Already-selected takes the HANDLE, which is the whole difference between the two: a
        // chord's member whose satellite the charter aimed at must not lose the rest of the chord
        // in the act of naming a stop within it. Where nothing selected it, arming is the ordinary
        // press — the note becomes the selection, with the caret on the stop that was clicked.
        if (chartSelection().contains(*key))
        {
            armChartHeldStopHandle(slot);
        }
        else
        {
            armChartCaret(slot.position, slot.string, common::core::ChartStopChannel::Held);
        }
    }
    else if (!chartSelection().contains(*key) || channel_changes)
    {
        // Arming re-derives the singleton selection from the object under the caret. A press on
        // an already-selected one keeps the standing selection (and marker) untouched until
        // the release collapses it — the gap a future drag-move gesture lives in — unless it
        // moves the caret to the note's OTHER stop, which is a change the next digit depends on.
        // The stop every object has: the satellite branch above took every target that addresses
        // another one.
        const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
        armChartCaret(slot.position, slot.string);
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

    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        updateView();
        return;
    }

    if (gesture.hit_target.has_value())
    {
        const bool clicked = std::abs(event.x - gesture.anchor_x) <= g_chart_click_threshold_px &&
                             std::abs(event.y - gesture.anchor_y) <= g_chart_click_threshold_px;
        // A completed plain click on a selected note collapses the selection to that note and
        // arms the caret there (the press deferred both while a drag was still possible); the
        // second release of a double click leaves the group selection standing.
        // A SATELLITE is settled entirely by the press, so the collapse skips it: re-arming here
        // would take back the selection the handle preserved.
        if (clicked && !gesture.modifiers.ctrl && event.clicks < 2 &&
            !chartSatelliteTarget(*gesture.hit_target))
        {
            if (const std::optional<ChartSelectionKey> key =
                    chartSelectionKeyAt(*gesture.hit_target);
                key.has_value())
            {
                // Every target reaching here addresses the object's own head, which is the stop
                // every object has.
                const ChartSlotKey slot = chartCaretSlotFor(session().song().tempo_map, *key);
                armChartCaret(slot.position, slot.string);
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
        const std::vector<ChartHitTarget> boxed =
            chartTargetsInBox(*tab, gesture.geometry, left, top, right, bottom);
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
            stack.push_back(MarkerFocusRow{.row = row});
        }
    };
    // The ruler draws its rows top down as sections, tempo, time signature.
    push_marker_row(MarkerRow::Section);
    push_marker_row(MarkerRow::Tempo);
    push_marker_row(MarkerRow::TimeSignature);
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

// The vertical walk (docs/plans/in-progress/keyboard-focus-rows.md): rows where nothing is typed —
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
    // A marker selected with the pointer need not hold the cursor. Stepping off it brings the
    // cursor inside first, so the next row's holder and the lanes the stack lists are the ones
    // found at that marker, and a lane caret armed below a tone region sits inside its own tone.
    moveCursorIntoSelectedMarker();

    const ChartCaret* const armed = armedChartCaret();
    const std::optional<common::core::GridPosition> column =
        armed != nullptr ? std::optional{armed->position} : std::nullopt;
    const std::vector<FocusRow> stack = focusRowStack(string_count);
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
    const common::core::ChartStopChannel channel)
{
    std::visit(
        Overloaded{
            [&](const StringFocusRow& string_row) {
                armChartCaret(
                    column.has_value() ? *column : pausedCursorPosition(placementQuantum()),
                    string_row.string,
                    channel);
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
                        pausedCursorPosition(g_tick_quantum_note_value))));
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
// without stepping; while armed they step the union stop set on the caret's row, or jump measures
// under the reach modifier (the Guitar Pro jump). Every move re-derives the selection from what
// sits under the caret. Inert while playing: arming requires a paused transport (armed ⟹ paused
// is structural).
void EditorController::Impl::performActionImpl(const EditorAction::StepChartCaret& action)
{
    const ChartStepDirection direction = action.direction;
    const common::core::ChartViewState* const tab = displayedTabProjection();
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

    const ChartCaret* const armed = armedChartCaret();
    if (armed == nullptr)
    {
        armMarkerInPlace(tab->stringCount());
        return;
    }

    // Along the time axis, reach is a measure.
    const bool measure = action.reach;
    const ChartCaret caret = *armed;
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const int sign = direction == ChartStepDirection::Right ? 1 : -1;
    // The WITHIN-SLOT stop, taken before the grid: a note carrying a held stop wears two marks in
    // one column — its head, then the satellite outboard of the bracket — so the caret visits two
    // stops there in DISPLAY order, rightward and reversed leftward. Only a plain step visits it:
    // the measure jump is a big move by definition, and a stop half a glyph away is not what it
    // means.
    if (!measure && !caret.lane.has_value())
    {
        const ChartSlotKey slot{.position = caret.position, .string = caret.string};
        const bool on_head = chartCaretChannel() == common::core::ChartStopChannel::Sounding;
        if (on_head == (sign > 0) && chartSlotShowsHeldStop(slot))
        {
            armChartCaret(
                caret.position,
                caret.string,
                on_head ? common::core::ChartStopChannel::Held
                        : common::core::ChartStopChannel::Sounding);
            updateView();
            return;
        }
    }
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
        stepped =
            adjacentTempoGridPosition(tempo_map, placementQuantum(), caret.position, sign > 0);
        if (const std::optional<common::core::GridPosition> object_stop =
                nextRowObjectStop(caret, sign > 0, false);
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
    // Time stepping is row-agnostic: a lane caret steps the same grid and keeps its row, the one
    // row rule every horizontal landing shares. On a string the channel is display order,
    // reversed: the satellite sits to the RIGHT of its head, so a caret arriving from the right
    // meets it first and a caret arriving from the left meets the head first. A measure jump is not
    // traversal — it is a big move by definition — so it lands on the stop every note has.
    // armChartCaret drops a Held request the destination cannot draw, so this needs no second test
    // of its own.
    landOnRow(
        prepareLandingRow(tab->stringCount()),
        stepped,
        !measure && sign < 0 ? common::core::ChartStopChannel::Held
                             : common::core::ChartStopChannel::Sounding);
    updateView();
}

// Tab (docs/plans/in-progress/keyboard-focus-rows.md, Phase 2): the next or previous OBJECT on the
// row focus stands on, the grid ignored. A string's objects are its notes and their keyframes (its
// notes alone under notes_only), a lane's its points, and a marker row's its markers, where the
// step moves from the SELECTED marker — not from the cursor, which a pointer selection may have
// left elsewhere — selecting its neighbour and bringing the cursor to that marker's start. A held
// stop's satellite is part of its note rather than an object of its own, so a string step always
// lands on a note's head. Past either end, and on the "+" row, which holds no objects, the press is
// inert; from the passive marker it arms in place, as the arrows' first press does.
void EditorController::Impl::performActionImpl(const EditorAction::StepToRowObject& action)
{
    const common::core::ChartViewState* const tab = displayedTabProjection();
    if (tab == nullptr || tab->stringCount() <= 0)
    {
        return;
    }

    if (const std::optional<SelectedMarker> selected = selectedMarker(); selected.has_value())
    {
        const std::optional<std::size_t> index = selected->index;
        const std::vector<common::core::GridPosition> starts = markerStarts(selected->row);
        if (!index.has_value() || (action.later ? *index + 1 >= starts.size() : *index == 0))
        {
            return;
        }
        const std::size_t neighbour = action.later ? *index + 1 : *index - 1;
        moveCursorTo(starts[neighbour]);
        selectMarker(markerSelectionAt(selected->row, neighbour));
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
    if (const std::optional<common::core::GridPosition> stop =
            nextRowObjectStop(*armed, action.later, action.notes_only);
        stop.has_value())
    {
        landOnRow(prepareLandingRow(tab->stringCount()), stop);
        updateView();
    }
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
    const common::core::ChartViewState* const tab = displayedTabProjection();
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
    const common::core::ChartViewState* const tab = displayedTabProjection();
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
// everywhere else. A silently-held stop moves with the chord it belongs to and needs no rule of its
// own: it is a note on a slot like any other.
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
    const auto moved_slot = [this, &delta](const ChartSlotKey& slot) {
        return ChartSlotKey{
            .position = common::core::advanceGridPosition(
                session().song().tempo_map, slot.position, delta.beats),
            .string = slot.string + delta.strings,
        };
    };
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
                .offset = note_moved ? key.offset : key.offset + delta.beats,
            });
    }

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
    // The caret rides its STOP, not just its slot: a charter typing into the held stop who nudges
    // the note would otherwise find the next digit stating the sounding fret instead. Read before
    // the edit and copied by value, because the marker below is what the reference points into;
    // a stop the moved note no longer draws is dropped where every other read drops it
    // (chartCaretChannel), so this needs no test of the destination.
    const common::core::ChartStopChannel rides_channel =
        caret_rides ? chartCaretChannel() : common::core::ChartStopChannel::Sounding;
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
                delta.beats,
                delta.strings,
                label);
        },
        gesture,
        moved);
    if (committed && caret_rides)
    {
        // Re-read after the edit: the selection followed the move, so it names where the caret
        // landed.
        const std::vector<ChartSelectionKey> landed_keys = chartSelection().keys();
        if (landed_keys.size() == 1)
        {
            const ChartSlotKey landed =
                chartCaretSlotFor(session().song().tempo_map, landed_keys.front());
            m_chart_marker = ChartCaret{
                .position = landed.position, .string = landed.string, .channel = rides_channel
            };
            updateView();
        }
    }
}

// Deletes the selected notes and keyframes as one compound undo entry; the selection empties with
// them.
void EditorController::Impl::deleteChartSelection()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }

    // Delete takes what the caret is ON, and on a held stop that is the STATEMENT rather than the
    // note: the charter never asked for the onset under it to go. A clearing planner of its own
    // (planClearHeldStops) rather than the hold verb's releasing direction: that verb infers its
    // direction from the CLAIM column, which a bare tap's DEFAULT and a fretting-hand source's
    // PLANT never enter, so routed there a Delete would author a held 0 on the one and convert the
    // other into a silent hold (THE PLANT'S FACE). Clearing withdraws the charter's statement and
    // nothing else; what the notation states it refuses, off the one ownership table the retype
    // reads.
    const ChartVerbScope scope = chartVerbSlots();
    if (scope.channel == common::core::ChartStopChannel::Held && !scope.slots.empty())
    {
        static_cast<void>(applyChartEditPlan(
            planClearHeldStops(*arrangement->chart, session().song().tempo_map, scope.slots)));
        return;
    }

    // Delete leaves no selection, including the keyframe keys that normally linger through
    // in-place edits for technique-toggle reversal. The empty caret can then accept a new point.
    static_cast<void>(applyChartEditPlan(
        planDeleteSelection(
            *arrangement->chart,
            session().song().tempo_map,
            chartSelection().notes(),
            chartSelection().keyframes()),
        std::vector<ChartSelectionKey>{}));
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
    if (const SongSectionSelection* const section = selectedSongSection())
    {
        const SongSectionSelection selected = *section;
        deleteSelectedSongSection(selected);
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
    // THE FIRST DIGIT'S MODIFIER DECIDES what the entry creates, and every digit after it — bare or
    // under `Alt` — simply widens that value. So "1" then "Alt+2" at a ring's end is the fret-12
    // HEAD the bare digit opened, and "Alt+1" then "2" there is the fret-12 SLIDE-OUT. The two
    // verbs differ in one cell only, and re-deriving the target on every keystroke to catch a verb
    // switch inside one 750 ms window bought that cell at the price of the whole entry's cost.
    if (m_chart_fret_entry.has_value() && combineChartFretEntry(digit, now_ms))
    {
        return;
    }
    // Which flow a digit takes is decided by the RETYPE operand, not by whether the selection is
    // empty — and that operand is BOTH kinds, because a selected keyframe states a fret exactly as
    // a head does. A selection holding neither falls through to the insert flow, where the caret
    // decides; with a keyframe selected the marker is a cursor, so nothing there could have
    // authored anyway.
    if (chartSelection().notes().empty() && chartSelection().keyframes().empty())
    {
        insertChartFretAtCaret(digit, action.path, now_ms);
        return;
    }
    retypeChartSelectionFret(digit, now_ms);
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
    // A HARMONIC entry states a node, and no digit widens one. The digit is a fresh statement
    // about the same notes, so the node settles first — a value you stated is a value you meant —
    // and the digit falls through to the retype flow, exactly as an expired entry's does.
    if (std::holds_alternative<ChartFretEntry::HarmonicNodes>(m_chart_fret_entry->target))
    {
        settleChartFretEntry();
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
// the combined value at its slot (undo removes the note), a split entry plans the cut carrying it
// as the new head's fret, and a retype entry replans the whole selection from the pre-entry base,
// so a widened value can never compound on its own earlier digit.
std::expected<ChartEditPlan, ChartPlanRefusal> EditorController::Impl::replanChartFretEntry(
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
    // A typed point on a tail plans the keyframe it states — planted for real at the settle and
    // selected. The commit law is not asked here: a typed value the path already passes through is
    // a point that says nothing, authoring state like any such point — no entry, gone when the note
    // leaves focus. One law, one place.
    if (const auto* const create = std::get_if<ChartFretEntry::CreateKeyframe>(&entry.target))
    {
        return planInsertKeyframe(
            *arrangement->chart,
            session().song().tempo_map,
            create->note,
            create->offset,
            entry.value);
    }
    // The harmonic entry plans the SAME function its technique row does, handed the candidate the
    // charter has cycled to: one planner, two callers differing only in whether a choice was
    // stated. An empty ladder states none, which is what an unambiguous press means.
    if (const auto* const harmonic = std::get_if<ChartFretEntry::HarmonicNodes>(&entry.target))
    {
        const std::optional<int> chosen_partial =
            harmonic->ladder.empty() ? std::nullopt
                                     : std::optional{harmonic->ladder[harmonic->chosen].partial};
        return planSetHarmonic(
            *arrangement->chart,
            session().song().tempo_map,
            harmonic->keys,
            chosen_partial,
            std::string{chartTechniqueLaw(ChartTechnique::Harmonic).noun});
    }
    // No guard for an empty operand here: the planner answers NoChange for one, and calling that
    // Invalid is what armed a red pending box — the display of a REFUSAL — over a press that had
    // simply found nothing to retype. The two emptinesses stay distinct, as everywhere else.
    const auto& retype = std::get<ChartFretEntry::Retype>(entry.target);
    return planRetypeFrets(
        *arrangement->chart,
        session().song().tempo_map,
        retype.base_notes,
        retype.keys,
        retype.keyframe_keys,
        ChartFretSet{.fret = entry.value},
        retype.channel);
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
        // retypes it — and a committed GHOST selects the point it made, for the same reason. A
        // retype rides the default selection follow. Bound before the call so the move and the
        // sibling read never share one argument list.
        std::optional<std::vector<ChartSelectionKey>> select_exactly;
        if (const auto* const insert = std::get_if<ChartFretEntry::InsertAt>(&entry.target))
        {
            select_exactly = std::vector<ChartSelectionKey>{ChartNoteKey{.slot = insert->slot}};
        }
        else if (
            const auto* const create = std::get_if<ChartFretEntry::CreateKeyframe>(&entry.target)
        )
        {
            select_exactly = std::vector<ChartSelectionKey>{
                ChartKeyframeKey{.note = create->note, .offset = create->offset}
            };
        }
        // Read before the move for the same reason the bind above is: the two must never share
        // one argument list.
        const bool stated_harmonic =
            std::holds_alternative<ChartFretEntry::HarmonicNodes>(entry.target);
        if (applyChartEditPlan(std::move(*entry.plan), std::move(select_exactly)))
        {
            if (stated_harmonic)
            {
                // The harmonic entry commits the TECHNIQUE verb's edit, so it arms that verb's
                // window here exactly as an immediate press would: the next `H` must be able to
                // reverse this entry and give back what the touch could not carry — a bend, a
                // shake, a slide — which the clear's own arithmetic, giving back only the fret,
                // cannot. Every other technique arms it at its apply; this one's apply is here.
                m_chart_verb_window = ChartVerbWindow{
                    .keys = chartSelection().keys(),
                    .verb = ChartTechniqueToggle{.technique = ChartTechnique::Harmonic},
                };
            }
        }
    }
    updateView();
}

// The one disposition rule for a freshly planned entry — a fresh digit, a combination, or a
// harmonic verb stating a node: an INVALID value goes pending whatever it holds, because the red
// box must be SEEN, and it persists until a further digit, Esc, or any other intent settles it,
// never a timer; a value a further press could still CHANGE waits out its window; every other
// valid value settles in the same keystroke.
//
// What "could still change" means is the entry's own answer: another digit could widen a leading 1
// or 2 at the 24-fret cap, and another `H` could cycle a harmonic entry whose typed fret named more
// than one node. So the picker arms on ambiguity and the other fifteen labels settle in one press,
// through this rule rather than beside it.
void EditorController::Impl::armOrSettleChartFretEntry(ChartFretEntry entry)
{
    const bool invalid = !entry.plan.has_value() && entry.plan.error() == ChartPlanRefusal::Invalid;
    const auto* const harmonic = std::get_if<ChartFretEntry::HarmonicNodes>(&entry.target);
    const bool extendable =
        harmonic != nullptr ? harmonic->ladder.size() > 1 : chartFretValueExtendable(entry.value);
    if (invalid || extendable)
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
            // An INVALID value outlives its window: the red box IS the refusal display, and a
            // display that vanishes on a timer is barely a display. It stays until a further digit
            // extends it or Esc / any other intent discards it.
            if (!m_chart_fret_entry->plan.has_value() &&
                m_chart_fret_entry->plan.error() == ChartPlanRefusal::Invalid)
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
std::optional<decltype(EditorController::Impl::ChartFretEntry::target)> EditorController::Impl::
    chartCaretDigitTarget(const bool path) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const ChartCaret* const caret = armedChartCaret();
    if (arrangement == nullptr || !arrangement->chart.has_value() || caret == nullptr ||
        caret->lane.has_value())
    {
        // No caret, or the caret rides an automation lane row — lane typing is the
        // typed-value editor (routed in the view), never a fret insert.
        return std::nullopt;
    }
    const ChartSlotKey slot{.position = caret->position, .string = caret->string};
    const std::optional<ChartPathTail> tail = chartPathTailAt(
        arrangement->chart->notes, session().song().tempo_map, caret->position, caret->string);
    if (!tail.has_value())
    {
        // Nothing rings here, so both verbs state the same head.
        return ChartFretEntry::InsertAt{.slot = slot};
    }
    // A digit states a POINT wherever the path can take one: STRICTLY INSIDE the ring always, where
    // the only thing a fret can mean at an instant the string is already sounding is a stop the
    // hand takes, and at the ring's exact END under `Alt` alone, where it is the slide-out the
    // release names. Nothing single-press cuts a ring — the disconnect verb (Shift+L) is the
    // split's only door — and a release already standing on the end slot is unreachable from here,
    // since arming the caret selected its chip and the digit went to the retype flow instead.
    if (!tail->at_ring_end || path)
    {
        return ChartFretEntry::CreateKeyframe{.note = tail->note, .offset = tail->offset};
    }
    // A BARE digit at the exact end places the adjacent head instead: the ring already stops where
    // that head starts, so sequential entry never trips over a grid-step note's tail.
    return ChartFretEntry::InsertAt{.slot = slot};
}

// Fresh insert: with no selection, the typed digit states an object at the armed caret — a head on
// a slot no ring covers, a POINT on the path of one that does, or at a ring's exact end the head
// bare and the slide-out under Alt. Each rides the same pending entry: the box at the slot, red
// where the gate refuses the fret, and nothing authored until the window settles. While the marker
// is passive, digits are inert by design (the marker model) — a stray keystroke after listening
// authors nothing.
void EditorController::Impl::insertChartFretAtCaret(
    const int digit, const bool path, const std::uint32_t now_ms)
{
    std::optional<decltype(ChartFretEntry::target)> target = chartCaretDigitTarget(path);
    if (!target.has_value())
    {
        return;
    }
    ChartFretEntry entry{.value = digit, .target = std::move(*target), .armed_ms = now_ms};
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
}

// Fresh retype: capture the selection's pre-entry values as the replan base, plan the typed
// digit in FULL — the pending box and its red state read the outcome, so even a refused digit
// visibly does something — then arm the window for a digit a second digit could extend, or
// settle in the same keystroke for one it could not. An Invalid provisional digit still arms:
// under a capo every playable fret's first digit alone refuses, and the window is what keeps
// the two-digit target reachable.
//
// WHICH stop the digits state comes from the verb scope's channel, so the two ways to reach the
// held one — clicking its satellite, or stepping the caret onto it — open the same entry rather
// than two. Bare digits on a selected note keep stating its own sounding fret, which is what makes
// the held channel something the charter enters deliberately.
//
// The snapshot covers every note the entry writes THROUGH rather than only the notes it addresses,
// because a keyframe is stored inside its note: a selection naming only a point on a slide still
// needs that slide's pre-entry record to replan from, with its head's own fret left alone.
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
                .keyframe_keys = chartSelection().keyframes(),
                .base_notes = chartNotesForKeys(
                    notesTouchedBy(chartSelection().notes(), chartSelection().keyframes())),
                .channel = chartVerbSlots().channel,
            },
        .armed_ms = now_ms,
    };
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
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

// THE scope of a typed chart verb, written once here for every verb that asks. The typing family's
// own gate is "act on the selection (or the armed marker), and no-op when there is none"
// (`docs/plans/in-progress/keymap-matrix.md`), so the selection comes first and the armed caret is
// what an empty selection falls back to — which is also the only way a verb can reach a slot that
// holds nothing at all, the empty-slot authoring case.
//
// A caret riding an automation lane row names no chart slot, so it contributes nothing: lane typing
// is the lane's own typed-value editor, routed in the view.
//
// The CHANNEL is the caret's in either shape of scope, and that is not an inconsistency: arming re-
// derives the singleton selection from what sits under the caret, so a caret parked on a held stop
// is a caret on the one note the selection then holds. Every gesture that builds a wider selection
// dissolves the caret in place, which leaves the channel at the stop every note has — so a
// multi-note scope can never carry a stop only one of its notes states.
EditorController::Impl::ChartVerbScope EditorController::Impl::chartVerbSlots() const
{
    const ChartCaret* const caret = armedChartCaret();
    const common::core::ChartStopChannel channel = chartCaretChannel();
    const std::vector<ChartSlotKey>& selected = chartSelection().notes();
    if (!selected.empty())
    {
        return ChartVerbScope{.slots = selected, .channel = channel};
    }
    if (caret == nullptr || caret->lane.has_value())
    {
        return ChartVerbScope{.slots = {}, .channel = channel};
    }
    return ChartVerbScope{
        .slots = {ChartSlotKey{.position = caret->position, .string = caret->string}},
        .channel = channel,
    };
}

// Shifts every selected stop's fret by one (Alt+Shift+wheel), shape-preserving by
// construction: the verb names its delta and nothing else, and the planner moves every stop the
// selection addresses by it — silently-held stops included, so a transposed chord carries its
// held members, and selected KEYFRAMES too, since a point on a slide states a fret exactly as a
// head does (W13's ruling). A shift pushing any stop below zero or past the cap is refused by the
// planner, never clamped.
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
        ChartFretShift{.delta = direction > 0 ? 1 : -1},
        // The shape-preserving shift keeps the SOUNDING channel on the notes it names, per the
        // ruling that every verb but the two typing ones keeps note scope: it moves the stops the
        // notes sound, and whether a transpose should carry a shape's silently-held members along
        // is the open question recorded with the verb's design, not something to settle by reading
        // the caret here. A selected keyframe needs no channel at all.
        common::core::ChartStopChannel::Sounding)));
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
// select_exactly: where the run's objects have LANDED, for a verb whose steps re-key them; absent
// leaves the plan's default follow to it, which is right for every verb that rewrites in place.
bool EditorController::Impl::commitChartGestureStep(
    const bool continues, const ChartGestureReplan& replan, ChartVerbWindowVerb verb,
    const std::optional<std::vector<ChartSelectionKey>>& select_exactly)
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
            // The selection goes back with the chart. A verb whose steps re-key its objects has
            // been pointing at where the run had reached, and a replay describing nothing is
            // exactly the case where that landing IS the start.
            if (select_exactly.has_value())
            {
                chartSelectionMutable().applyBox(*select_exactly, false);
            }
            retireChartGesture(burst->plan);
            // The chart MOVED — back to where the run began — so this is a step the caller must
            // follow exactly as it follows any other: a caret riding the lone object rides it home.
            return true;
        }
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
        // The history entry is swapped BEFORE the model moves, the settle fold's discipline: the
        // two states must never disagree, and the live-gesture proofs above are exactly
        // replaceTop's own preconditions, so a refusal here is a logic error reported with the
        // chart untouched rather than left between two entries.
        if (m_undo_history.replaceTop(std::make_unique<ChartEdit>(std::move(written))).status !=
            EditorUndoTransitionStatus::Applied)
        {
            reportError("Could not apply chart edit: " + plan->label);
            return false;
        }
        common::core::Chart* const chart = m_session.currentChart();
        // Walk the live chart back to the pre-gesture stream and then to the re-planned one, so
        // the state the top entry describes is exactly the state the chart holds.
        if (chart == nullptr || !applyChartChange(*chart, burst->plan.reversed()).has_value() ||
            !applyChartChange(*chart, *plan).has_value())
        {
            reportError("Could not apply chart edit: " + plan->label);
            return false;
        }
        // The burst record follows the entry it names, or the next step would reverse a plan the
        // history no longer holds.
        burst->plan = std::move(*plan);
        // The replace path has no plan-driven selection follow of its own (that lives in
        // applyChartEditPlan, which only the first step runs), so a verb whose step re-keys its
        // objects states the landing here or leaves the next press holding keys naming nothing.
        if (select_exactly.has_value())
        {
            chartSelectionMutable().applyBox(*select_exactly, false);
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
    // The history moves BEFORE the model, this file's discipline everywhere: the two states must
    // never disagree, and a live gesture is exactly dropTop's precondition, so a refusal here is a
    // logic error reported with the chart untouched.
    if (m_undo_history.dropTop().status != EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + applied.label);
        return;
    }
    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr || !applyChartChange(*chart, applied.reversed()).has_value())
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

// The chart verbs' toggle window (D14 ruling 4), shared by every verb that has one rather than
// copied into each: while the selection and the burst record still prove the previous press was
// this verb's own entry, this press REVERSES that entry exactly, so the pair leaves no trace —
// including tails an assist grew and the note an arpeggio-hold conversion took, neither of which a
// verb's own clear law could ever restore.
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
    // The history moves BEFORE the model, the settle sweep's own discipline: the two states must
    // never disagree, and the guards above are exactly the history's preconditions, so a refusal
    // here is a logic error reported with the chart untouched rather than left reversed under an
    // entry that still describes the edit.
    //
    // A save mid-window makes the entry the file's clean state, so erasing it would make "return
    // to clean" a lie. The reversal still happens — the toggle stays genuine and the grown tail
    // comes back — but as its own inverse entry, which leaves the session correctly dirty.
    const bool clean_entry = m_undo_history.isAtCleanState();
    m_chart_notes_top.reset();
    ChartEditPlan reversal = applied.reversed();
    reversal.label = std::string{revert_label};
    if (clean_entry)
    {
        // The written form, like every entry; a record exists only for a plan that wrote as
        // something, and the reversal of such a plan writes as its reverse.
        pushUndoEntry(std::make_unique<ChartEdit>(writtenChartPlan(reversal)));
    }
    else if (m_undo_history.dropTop().status != EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + applied.label);
        return true;
    }
    common::core::Chart* const chart = m_session.currentChart();
    if (chart == nullptr || !applyChartChange(*chart, reversal).has_value())
    {
        reportError("Could not apply chart edit: " + applied.label);
        return true;
    }
    updateView();
    return true;
}

// Rationale lives on the declaration in editor_controller_impl.h.
bool EditorController::Impl::chartFretEntryContinuedBy(const EditorAction::Action& action) const
{
    if (std::holds_alternative<EditorAction::TypeChartFretDigit>(action))
    {
        return true;
    }
    const auto* const toggle = std::get_if<EditorAction::ToggleChartTechnique>(&action);
    return toggle != nullptr && toggle->technique == ChartTechnique::Harmonic &&
           m_chart_fret_entry.has_value() &&
           std::holds_alternative<ChartFretEntry::HarmonicNodes>(m_chart_fret_entry->target);
}

// The ladder the picker offers over one scope. Ambiguity occurs at ONE offset in the whole node
// ladder — a typed fret three above its stop, which names both the 7th partial's 2.669 and the
// 6th's 3.156 — so every ambiguous member of a selection offers this same pair whatever string or
// stop it sits on, and the first one found speaks for all of them. That is a consequence of the
// physics rather than a policy: it is what makes one picker per press, one plan and one undo entry
// the honest shape for a chord, where per-member pickers would break the toggle law with N windows.
//
// Empty means nothing in the scope is ambiguous, which the disposition rule reads as "settles in
// the same keystroke".
EditorController::Impl::ChartHarmonicLadder EditorController::Impl::chartHarmonicNodeLadder(
    const std::vector<ChartSlotKey>& keys) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value())
    {
        return {};
    }
    const common::core::Chart& chart = *arrangement->chart;
    // By INDEX rather than through `notesForKeys`, which copies each note whole: this walk runs on
    // every view-state push for the mouse rows, and a large selection would pay a note copy —
    // keyframe vectors and all — per member to answer a question about two numbers.
    for (const std::size_t index : slotIndicesForKeys(chart.notes, keys))
    {
        const common::core::ChartNote& note = chart.notes[index];
        std::vector<common::core::HarmonicNodeCandidate> candidates =
            chartHarmonicNodeCandidates(note, chart.tuning, session().song().tempo_map);
        if (candidates.size() <= 1)
        {
            continue;
        }
        // The label this note's fret states, asked of the same stop the candidates were resolved
        // against, so "nearest" means nearest to the number the charter actually typed.
        common::core::ChartNote unpressed = note;
        unpressed.fret = 0;
        const auto stop =
            static_cast<double>(common::core::physicalStopFret(unpressed, chart.tuning.capo));
        const std::size_t nearest =
            common::core::nearestHarmonicNode(candidates, static_cast<double>(note.fret) - stop);
        return ChartHarmonicLadder{
            .candidates = std::move(candidates),
            .nearest = nearest,
            .stop = stop,
        };
    }
    return {};
}

// What the live picker DRAWS: for every note the entry would actually write, the nodes its own
// fret names and which of them is armed. The nodes are absolute — the note's own stop plus each
// candidate's offset — because that is the number the head will print once it commits.
//
// Absent unless the entry is a picker with a ladder to show. A harmonic entry with no ladder
// settles in its own keystroke, so it never draws; publishing an empty overlay for it would put a
// box over heads with nothing to choose.
std::optional<ChartPendingHarmonicViewState> EditorController::Impl::chartPendingHarmonicViewState(
    const ChartFretEntry& entry) const
{
    const auto* const harmonic = std::get_if<ChartFretEntry::HarmonicNodes>(&entry.target);
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (harmonic == nullptr || harmonic->ladder.size() <= 1 || arrangement == nullptr ||
        !arrangement->chart.has_value())
    {
        return std::nullopt;
    }
    const common::core::Chart& chart = *arrangement->chart;
    const int chosen_partial = harmonic->ladder[harmonic->chosen].partial;
    ChartPendingHarmonicViewState published;
    for (const std::size_t index : slotIndicesForKeys(chart.notes, harmonic->keys))
    {
        const common::core::ChartNote& note = chart.notes[index];
        const std::vector<common::core::HarmonicNodeCandidate> candidates =
            chartHarmonicNodeCandidates(note, chart.tuning, session().song().tempo_map);
        if (candidates.empty())
        {
            // A note the settle would skip draws nothing: the picker must never show a value over
            // a head that is not going to take one.
            continue;
        }
        // The stop each candidate is measured from, asked exactly as the planner asks it.
        common::core::ChartNote unpressed = note;
        unpressed.fret = 0;
        const auto stop =
            static_cast<double>(common::core::physicalStopFret(unpressed, chart.tuning.capo));
        std::vector<double> nodes;
        nodes.reserve(candidates.size());
        std::size_t chosen = 0;
        for (const common::core::HarmonicNodeCandidate& candidate : candidates)
        {
            if (candidate.partial == chosen_partial)
            {
                chosen = nodes.size();
            }
            nodes.push_back(stop + candidate.position);
        }
        published.notes.push_back(
            ChartPendingHarmonicNode{
                .note = index,
                .nodes = std::move(nodes),
                .chosen = chosen,
            });
    }
    if (published.notes.empty())
    {
        return std::nullopt;
    }
    return published;
}

// The picker's mouse rows over the CURRENT selection, offered whenever that selection holds an
// ambiguous member — the same test the keyboard picker arms on, so the two forms can never
// disagree about when a choice exists. Read off the ladder's own note, whose stop is what makes
// the printed value absolute.
std::vector<ChartHarmonicNodeChoice> EditorController::Impl::chartHarmonicNodeChoices() const
{
    const ChartHarmonicLadder ladder = chartHarmonicNodeLadder(chartSelection().notes());
    if (ladder.candidates.empty())
    {
        return {};
    }
    std::vector<ChartHarmonicNodeChoice> choices;
    choices.reserve(ladder.candidates.size());
    for (const common::core::HarmonicNodeCandidate& candidate : ladder.candidates)
    {
        choices.push_back(
            ChartHarmonicNodeChoice{
                .node = ladder.stop + candidate.position,
                .partial = candidate.partial,
            });
    }
    return choices;
}

// Arms the harmonic verb's pending entry over the current selection.
void EditorController::Impl::armChartHarmonicNodeEntry()
{
    ChartHarmonicLadder ladder = chartHarmonicNodeLadder(chartSelection().notes());
    // Read before the move, so the two never share one initializer with a moved-from operand.
    const std::size_t chosen = ladder.nearest;
    ChartFretEntry entry{
        .target =
            ChartFretEntry::HarmonicNodes{
                .keys = chartSelection().notes(),
                .ladder = std::move(ladder.candidates),
                .chosen = chosen,
            },
        .armed_ms = m_now_milliseconds(),
    };
    entry.plan = replanChartFretEntry(entry);
    armOrSettleChartFretEntry(std::move(entry));
}

// A second `H` while the picker is live: the next candidate becomes the armed one and the window
// starts over, so a charter can keep cycling. Nothing commits — the entry is replanned in full,
// exactly as a widened digit is — which is what keeps the picker and the toggle window exclusive.
bool EditorController::Impl::cycleChartHarmonicNodeEntry()
{
    if (!m_chart_fret_entry.has_value() ||
        !std::holds_alternative<ChartFretEntry::HarmonicNodes>(m_chart_fret_entry->target))
    {
        return false;
    }
    ChartFretEntry entry = std::move(*m_chart_fret_entry);
    m_chart_fret_entry.reset();
    auto& harmonic = std::get<ChartFretEntry::HarmonicNodes>(entry.target);
    if (harmonic.ladder.empty())
    {
        // An entry with no ladder states no choice, so there is nothing to cycle — and it would
        // have settled in its own keystroke, so this is unreachable in practice. Settled rather
        // than dropped, because the entry still holds a plan the charter asked for.
        m_chart_fret_entry = std::move(entry);
        settleChartFretEntry();
        return true;
    }
    harmonic.chosen = (harmonic.chosen + 1) % harmonic.ladder.size();
    entry.armed_ms = m_now_milliseconds();
    entry.plan = replanChartFretEntry(entry);
    armChartFretEntry(std::move(entry));
    return true;
}

// The picker's MOUSE form: a menu row is already a deliberate choice, so it applies at once rather
// than arming a window that a click has no second press to cycle. Same planner, same uniform scope,
// same single undo entry — only the way the choice arrived differs.
void EditorController::Impl::performActionImpl(const EditorAction::SetChartHarmonicNode& action)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || !arrangement->chart.has_value() || chartSelection().empty())
    {
        return;
    }
    const std::string_view noun = chartTechniqueLaw(ChartTechnique::Harmonic).noun;
    if (applyChartEditPlan(planSetHarmonic(
            *arrangement->chart,
            session().song().tempo_map,
            chartSelection().notes(),
            action.partial,
            noun)))
    {
        // The same window the keyboard path arms, and for the same reason: the next `H` reverses
        // this entry exactly, giving back what the touch could not carry.
        m_chart_verb_window = ChartVerbWindow{
            .keys = chartSelection().keys(),
            .verb = ChartTechniqueToggle{.technique = ChartTechnique::Harmonic},
        };
    }
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
    // A second `H` over a live picker CYCLES it: the entry has committed nothing, so there is no
    // edit to reverse yet and the toggle window below cannot be armed for it. Checked first for
    // that reason — the two windows are exclusive by construction, and the press means the cycle
    // before the settle and the reversal after it.
    if (technique == ChartTechnique::Harmonic && cycleChartHarmonicNodeEntry())
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
        toggleChartLegato(chartSelection().notes());
        return;
    }

    // The law reads the whole SELECTION for both halves of the toggle: which objects a technique
    // has a meaning for is the row's own business, so a press over a selection this technique
    // reaches nothing in simply plans to NoChange instead of being filtered out here.
    const ChartTechniqueLaw law = chartTechniqueLaw(technique);
    const bool all_carry = law.carried(*arrangement->chart, chartSelection());
    if (technique == ChartTechnique::Harmonic && !all_carry)
    {
        // THE ONE ROW WHOSE SET STATES A VALUE. Which node the finger touches is a quantity, not a
        // flag, so it goes through the pending-entry machinery every stated value in this editor
        // goes through — arming the picker where the typed fret names two nodes, settling in the
        // same keystroke where it names one, and committing the very same planner this row's own
        // plan would have called. The CLEAR states nothing and stays an ordinary row below.
        armChartHarmonicNodeEntry();
        return;
    }
    const std::string label = all_carry ? "Remove " + std::string{law.noun} : std::string{law.noun};
    if (applyChartEditPlan(law.plan(
            *arrangement->chart, session().song().tempo_map, chartSelection(), !all_carry, label)))
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
void EditorController::Impl::toggleChartLegato(const std::vector<ChartSlotKey>& keys)
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
        if (applyChartEditPlan(std::move(clear_plan)))
        {
            m_chart_verb_window = ChartVerbWindow{
                .keys = chartSelection().keys(),
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

// The arpeggio hold verb (`N`), selection-scoped like every other chart verb with the typing
// family's caret fallback behind it (\ref chartVerbSlots): a chord converts in one press and one
// undo entry, and a caret on an empty slot is what authors a hand fact where no note is — the case
// a selection cannot reach, since there is nothing there to select
// (`docs/plans/todo/arpeggio-authoring.md`). The uniform-scope law therefore has no exception here
// any more; what it had was a verb whose only reachable operand was the caret's.
//
// Under the toggle window this is a true two-press toggle in every case, conversions included: the
// second press REVERSES the first entry, which is the only thing that can put back the techniques a
// conversion stripped — a silent hold carries a stop and nothing else, so no forward law could
// rebuild the mutes, node and payload the note had. Once the window is dead (any other edit, a
// caret move, undo/redo), pressing `N` on a hold sounds it again as a plain pick at the session's
// grid step, which is the honest forward inverse and the one this verb can state.
//
// A press whose product would state nothing is refused by the planner and is SILENT here, exactly
// as a technique toggle that applies to nothing is: no window is armed, so the next press is an
// ordinary first press. The counted feedback both want is W5's channel.
void EditorController::Impl::performActionImpl(const EditorAction::ToggleChartSilentHold&)
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::vector<ChartSlotKey> slots = chartVerbSlots().slots;
    if (arrangement == nullptr || !arrangement->chart.has_value() || slots.empty())
    {
        return;
    }
    if (reverseChartVerbWindow(ChartSilentHoldToggle{}, "Revert Hold Stop"))
    {
        return;
    }
    // The ring a sounded note is given is the session's grid step, read at the scope's first slot
    // like any placement's: the step is a session fact scaled by the local meter, and the charter
    // is working at the position they are looking at.
    if (applyChartEditPlan(planToggleSilentHold(
            *arrangement->chart,
            session().song().tempo_map,
            slots,
            chartGridStepBeats(slots.front().position))))
    {
        // The fourth case's own follow-through: where the press stated a HELD stop, the caret moves
        // onto that stop so the digits that follow state it — the keyboard twin of clicking the
        // satellite, and the same "the caller arms it here" the empty-slot case has always used.
        // Asked of the settled chart through the one satellite query, so a press whose statement
        // the settle then took leaves the caret on the head it started from.
        armHeldStopCaretAfterToggle(slots);
        m_chart_verb_window = ChartVerbWindow{
            .keys = chartSelection().keys(),
            .verb = ChartSilentHoldToggle{},
        };
    }
}

// Moves the caret onto the held stop the toggle just stated, when it stated exactly one. A press
// over a whole chord leaves the caret where it was: a pending entry has ONE channel, and a scope
// whose notes do not all state a held stop has no single stop for the digits to mean.
void EditorController::Impl::armHeldStopCaretAfterToggle(const std::vector<ChartSlotKey>& slots)
{
    if (slots.size() != 1 || !chartSlotShowsHeldStop(slots.front()))
    {
        return;
    }
    armChartCaret(
        slots.front().position, slots.front().string, common::core::ChartStopChannel::Held);
    // The apply published before this ran, so the moved caret needs its own push: arming is not a
    // publishing operation anywhere, and every other caller pushes for the same reason.
    updateView();
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
    const ChartEditPlan* const burst =
        m_chart_notes_top.has_value() && m_chart_notes_top->history_position == history.position &&
                !m_undo_history.isAtCleanState()
            ? &m_chart_notes_top->plan
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
        if (!applyChartChange(base, burst->reversed()).has_value())
        {
            return false;
        }
        label = burst->label;
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
        retireChartGesture(*burst);
        return true;
    }

    // The history entry is swapped BEFORE the model moves, because the two states must never
    // disagree: the guards above are exactly replaceTop's own preconditions, so a refusal is a
    // logic error, and reporting it leaves the chart untouched instead of stranded between entries.
    if (burst != nullptr &&
        m_undo_history.replaceTop(std::make_unique<ChartEdit>(written)).status !=
            EditorUndoTransitionStatus::Applied)
    {
        reportError("Could not apply chart edit: " + settled->label);
        return false;
    }
    common::core::Chart* const chart = m_session.currentChart();
    // Walk the live chart back to pre-burst and then to the settled state, so the state the history
    // top describes is exactly the state the chart holds (the fret-entry widen's own discipline).
    if (chart == nullptr ||
        (burst != nullptr && !applyChartChange(*chart, burst->reversed()).has_value()) ||
        !applyChartChange(*chart, *settled).has_value())
    {
        reportError("Could not apply chart edit: " + settled->label);
        return false;
    }
    if (burst == nullptr)
    {
        // At the top of the stack a push truncates nothing, so the flatten simply becomes its own
        // undo step.
        pushUndoEntry(std::make_unique<ChartEdit>(std::move(written)));
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
