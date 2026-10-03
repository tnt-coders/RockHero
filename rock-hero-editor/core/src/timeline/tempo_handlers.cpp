#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "timeline/tempo_map_edits.h"
#include "timeline/tempo_map_snapshot.h"
#include "tone/tone_automation_edits.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/decimal_text.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/tempo_span_text.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The beat a position falls in: the only place a tempo anchor can stand, since an anchor pins a
// whole beat. Positions carry their offset within the beat, so dropping it floors.
[[nodiscard]] common::core::GridPosition tempoAnchorBeat(const common::core::GridPosition& position)
{
    return common::core::GridPosition{
        .measure = position.measure,
        .beat = position.beat,
        .offset = {},
    };
}

// The tempo text of the span the anchor standing exactly on a beat opens, as its chip prints it, or
// nothing where no anchor opening a span stands there (the terminal opens none). The one lookup
// the chord, Enter's verb and the typed tempo's undo label all print through.
[[nodiscard]] std::optional<std::string> spanBpmTextAt(
    const common::core::TempoMap& tempo_map, const common::core::GridPosition& beat)
{
    const std::vector<common::core::BeatAnchor>& anchors = tempo_map.anchors();
    for (std::size_t span = 0; span + 1 < anchors.size(); ++span)
    {
        if (anchors[span].measure == beat.measure && anchors[span].beat == beat.beat)
        {
            return tempoSpanBpmText(tempo_map, span);
        }
    }
    return std::nullopt;
}

// The time a rippled map must still reach: the latest audio end over every arrangement, since the
// one map places them all and a faster tempo must not leave any of them playing past the terminal.
[[nodiscard]] double latestAudioEndSeconds(const common::core::Song& song)
{
    double latest = 0.0;
    for (const common::core::Arrangement& arrangement : song.arrangements)
    {
        latest = std::max(latest, arrangement.audioTimelineRange().end.seconds);
    }
    return latest;
}

} // namespace

// The tempo chord authors at the cursor and never reads the selection: the beat the cursor is IN,
// read from the tick so a cursor just short of a beat is not rounded into the next one, floored to
// that beat, the only place an anchor can stand. An anchor already standing there is restated —
// the BPM prompt for the span it opens — which is what makes the double press work: the first
// inserts and selects, the second finds that anchor on the cursor's beat. Where none stands and a
// marker may start, the verb is an insert; at or past the terminal it is nothing, since the
// terminal opens no span and nothing may start after it.
TempoChordTarget EditorController::Impl::tempoChordTarget() const
{
    const std::optional<common::core::GridPosition> position =
        cursorPosition(common::core::g_tick_quantum_note_value);
    if (!position.has_value())
    {
        return {};
    }
    const common::core::GridPosition beat = tempoAnchorBeat(*position);
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    if (std::optional<std::string> bpm_text = spanBpmTextAt(tempo_map, beat); bpm_text.has_value())
    {
        return SetTempoTarget{.position = beat, .bpm_text = std::move(*bpm_text)};
    }
    if (!common::core::markerCanStartAt(beat, tempo_map))
    {
        return {};
    }
    return InsertTempoAnchorTarget{.position = beat};
}

// A selection naming an anchor that is gone answers nothing.
std::optional<SetTempoTarget> EditorController::Impl::selectedTempoTarget() const
{
    const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection);
    if (anchor == nullptr)
    {
        return std::nullopt;
    }
    std::optional<std::string> bpm_text =
        spanBpmTextAt(session().song().tempo_map, anchor->position);
    if (!bpm_text.has_value())
    {
        return std::nullopt;
    }
    return SetTempoTarget{.position = anchor->position, .bpm_text = std::move(*bpm_text)};
}

// The beat is floored here, at the boundary, so the verbs below address whole beats only.
void EditorController::Impl::onTempoAnchorInsertRequested(const common::core::GridPosition position)
{
    runAction(EditorAction::InsertTempoAnchor{.position = tempoAnchorBeat(position)});
}

// The prompt hands back raw text, so the parse is the core's: text that is not one number changes
// nothing and records nothing, exactly as a refused commit does. Whether the number is a usable
// tempo is the tempo edit's rule.
void EditorController::Impl::onTempoSpanBpmRequested(
    const common::core::GridPosition position, const std::string& text)
{
    const std::optional<double> quarter_bpm = common::core::parseFiniteDecimal(text);
    if (!quarter_bpm.has_value())
    {
        logMarkerRefusal("Set Tempo", "not a number: " + text);
        return;
    }
    runAction(
        EditorAction::SetTempoSpanBpm{
            .position = tempoAnchorBeat(position),
            .quarter_bpm = *quarter_bpm,
        });
}

// The one commit of a whole tempo map. The grid is truth (decision 4): content keeps its musical
// place and moves in time with the map, and so does the cursor — passive or armed — so it is read
// at the tick BEFORE the map changes and put back on that place after, under the new map. The
// anchor a verb leaves selected is selected first, because selecting dissolves an armed caret by
// seeking to it, and that seek would otherwise be the last word.
void EditorController::Impl::commitTempoMap(
    TempoMapSnapshot before, common::core::TempoMap after, std::string label,
    const std::optional<TempoAnchorSelection> landed)
{
    const std::optional<common::core::GridPosition> place =
        cursorPosition(common::core::g_tick_quantum_note_value);
    const MarkerCommit committed = commitMarkerModel(
        std::move(before), TempoMapSnapshot{.tempo_map = std::move(after)}, std::move(label));
    if (committed == MarkerCommit::Refused)
    {
        return;
    }
    if (landed.has_value())
    {
        selectMarker(*landed);
    }
    if (committed == MarkerCommit::Landed && place.has_value())
    {
        moveCursorTo(*place);
    }
    if (landed.has_value() || committed == MarkerCommit::Landed)
    {
        updateView();
    }
}

// Adds an anchor on a beat, pinning the time the map already gives it, so the insert moves nothing
// audible. Like every insert, it leaves what it made SELECTED, so Enter, Delete and Alt+arrows act
// on the new anchor next. An occupied beat, or one at or past the terminal, is the edit's refusal.
void EditorController::Impl::performActionImpl(const EditorAction::InsertTempoAnchor& action)
{
    constexpr std::string_view label{"Add Tempo Anchor"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult inserted =
        insertTempoAnchor(before.tempo_map, action.position.measure, action.position.beat);
    if (!inserted.has_value())
    {
        logMarkerRefusal(label, inserted.error().message);
        return;
    }
    commitTempoMap(
        std::move(before),
        std::move(*inserted),
        std::string{label},
        TempoAnchorSelection{.position = action.position});
}

// Types the tempo of the span an anchor opens: the anchor stays put and every later anchor ripples
// by the same whole-millisecond amount, so every later span keeps its tempo, and the terminal grows
// to keep covering the song's audio. Like the section rename, a restate SELECTS its target whether
// the tempo changed or not, and only a refusal leaves the selection where it was.
void EditorController::Impl::performActionImpl(const EditorAction::SetTempoSpanBpm& action)
{
    constexpr std::string_view verb{"Set Tempo"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult rippled = setTempoSpanBpm(
        before.tempo_map,
        action.position.measure,
        action.position.beat,
        action.quarter_bpm,
        latestAudioEndSeconds(session().song()));
    if (!rippled.has_value())
    {
        logMarkerRefusal(verb, rippled.error().message);
        return;
    }

    // The edit refuses a beat no span-opening anchor holds, so the result always prints one.
    std::string label =
        std::string{verb} + " to " + spanBpmTextAt(*rippled, action.position).value_or("");
    commitTempoMap(
        std::move(before),
        std::move(*rippled),
        std::move(label),
        TempoAnchorSelection{.position = action.position});
}

// Moves the selected anchor one on-screen pixel at the reported zoom (at least a millisecond), in
// free time: the anchor defines the grid, so no grid line can be its step. Every other anchor stays
// pinned and the two spans beside it change tempo. A step onto a neighbour, or the first anchor
// below zero, is refused rather than clamped. A move never changes the anchor's beat, so the
// selection it lands is the one it started from, and a held run of presses is one undo entry.
void EditorController::Impl::moveSelectedTempoAnchor(
    const TempoAnchorSelection& selection, const ChartStepDirection direction)
{
    constexpr std::string_view label{"Move Tempo Anchor"};
    // The tempo row's markers are the anchors that open a span, in order, so a marker's index is
    // its anchor's.
    const std::optional<std::size_t> index = markerIndex(MarkerRow::Tempo, selection.position);
    if (!index.has_value())
    {
        // The selection went stale; moving nothing is the answer.
        return;
    }
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    const double step = tempoAnchorNudgeSeconds(m_timeline_zoom_pixels_per_second);
    const double seconds = before.tempo_map.anchors().at(*index).seconds +
                           (direction == ChartStepDirection::Left ? -step : step);

    TempoMapEditResult moved = moveTempoAnchor(
        before.tempo_map, selection.position.measure, selection.position.beat, seconds);
    if (!moved.has_value())
    {
        logMarkerRefusal(label, moved.error().message);
        return;
    }

    if (commitMarkerMove(
            std::move(before),
            TempoMapSnapshot{.tempo_map = std::move(*moved)},
            std::string{label},
            selection))
    {
        followMovedMarker(selection.position);
    }
}

// Deletes the selected anchor (the Delete-key dispatch for the tempo alternative): the two spans
// beside it merge, their outer anchors staying pinned, so it is the exact inverse of an insert. The
// first anchor and the terminal are refused; every map keeps both.
void EditorController::Impl::deleteSelectedTempoAnchor(const TempoAnchorSelection& selection)
{
    constexpr std::string_view label{"Delete Tempo Anchor"};
    TempoMapSnapshot before = TempoMapSnapshot::capture(session());
    TempoMapEditResult merged =
        deleteTempoAnchor(before.tempo_map, selection.position.measure, selection.position.beat);
    if (!merged.has_value())
    {
        logMarkerRefusal(label, merged.error().message);
        return;
    }
    commitTempoMap(std::move(before), std::move(*merged), std::string{label}, std::nullopt);
}

// THE one authority for keeping the engine's tempo-derived caches current: the seconds-domain tone
// automation curves, which are the arrangement's musical automation baked through the map, and the
// plugins' one-way host-tempo mirror. Every path that can change the map — the marker funnel, an
// undo or a redo — asks here, and it compares the session's map with the one it last pushed, so
// asking costs one comparison when the map did not change. Nothing to do without a ready rig: the
// rig load pushes both from the map it finds.
void EditorController::Impl::syncTempoDerivedAudio()
{
    if (!m_project_audio_ready)
    {
        return;
    }
    if (m_engine_tempo_map == session().song().tempo_map)
    {
        return;
    }
    pushTempoDerivedAudio();
}

void EditorController::Impl::pushTempoDerivedAudio()
{
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    rebuildDerivedToneCurves(editContext());
    m_song_audio.mirrorTempoMap(tempo_map);
    m_engine_tempo_map = tempo_map;
}

} // namespace rock_hero::editor::core
