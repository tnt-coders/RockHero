#include "chart/chart_selection.h"
#include "controller/editor_controller_impl.h"
#include "controller/refusal_light.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_shapes.h>
#include <rock_hero/common/core/chart/chart_view_state.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/common/core/timeline/tick_lattice.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

// The marker rows: the ruler's section, tempo and time-signature rows, the span and none-hold rows,
// the position row and the tone row, which the keyboard reaches by selecting a marker. Every row
// answers the same questions — where its markers start, which one holds a position, which one the
// selection names — so the walk, a chip click and Tab share one model of all seven.

std::vector<common::core::GridPosition> EditorController::Impl::markerStarts(
    const MarkerRow row) const
{
    std::vector<common::core::GridPosition> starts;
    switch (row)
    {
        case MarkerRow::Section:
        {
            for (const common::core::SongSection& section : session().song().sections)
            {
                starts.push_back(section.position);
            }
            break;
        }
        case MarkerRow::Tempo:
        {
            // The terminal anchor only closes the last span, so like the ruler it marks nothing.
            const std::vector<common::core::BeatAnchor>& anchors =
                session().song().tempo_map.anchors();
            for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
            {
                starts.push_back(anchors[index].position);
            }
            break;
        }
        case MarkerRow::TimeSignature:
        {
            // Every reign start, the derived short bar's included: the chips the ruler draws, so
            // the walk lands on a ghost chip as it lands on an authored one.
            for (const common::core::MeterReign& reign : session().song().tempo_map.reigns())
            {
                starts.push_back(common::core::measureDownbeat(reign.first_measure));
            }
            break;
        }
        case MarkerRow::NoneHold:
        {
            for (const common::core::NoneHoldViewState& hold : noneHoldMarks())
            {
                starts.push_back(hold.start);
            }
            break;
        }
        case MarkerRow::Span:
        {
            // Every span, derived and held alike: the projection's spans are the notes layer's in
            // order, so a row index IS a span's index. Read off the memoized notes layer at the
            // exact musical place, never off the projection's seconds.
            const common::core::TempoMap& tempo_map = session().song().tempo_map;
            for (const common::core::ChartShape& shape : chartResolutions().shapes)
            {
                starts.push_back(tempo_map.gridPositionAtWholeNote(shape.place));
            }
            break;
        }
        case MarkerRow::FretHandPosition:
        {
            // Every hand event, derived and authored alike: the chips the lane draws, in the
            // order it draws them, so a row index IS a chip index. Read off the memoized
            // projection, which derives them once per chart revision.
            if (const common::core::ChartViewState* const scene = currentTabProjection())
            {
                for (const common::core::FhpViewState& chip : scene->fret_hand_positions)
                {
                    starts.push_back(chip.position);
                }
            }
            break;
        }
        case MarkerRow::Tone:
        {
            if (const common::core::Arrangement* const arrangement = session().currentArrangement())
            {
                for (const common::core::ToneRegion& region : arrangement->tone_track.regions)
                {
                    starts.push_back(region.start);
                }
            }
            break;
        }
    }
    return starts;
}

// Read off the memoized projection, which publishes the chart's none holds once per revision.
const std::vector<common::core::NoneHoldViewState>& EditorController::Impl::noneHoldMarks() const
{
    static const std::vector<common::core::NoneHoldViewState> g_no_tab;
    const common::core::ChartViewState* const scene = currentTabProjection();
    return scene != nullptr ? scene->none_holds : g_no_tab;
}

// A none hold's close is its stated end, which always fits its notes; a span's is its musical
// close, read on its start's axis, which for a held span is its hold's stated end too.
std::optional<common::core::GridPosition> EditorController::Impl::markerClose(
    const MarkerRow row, const std::size_t index) const
{
    switch (row)
    {
        case MarkerRow::NoneHold:
        {
            // Read through .at(), as markerSelectionAt reads the tone row: an index outside the
            // row's markers is a broken precondition.
            return noneHoldMarks().at(index).end;
        }
        case MarkerRow::Span:
        {
            const common::core::ChartShape& span = chartResolutions().shapes.at(index);
            return session().song().tempo_map.gridPositionAtWholeNote(span.place + span.sustain);
        }
        case MarkerRow::Section:
        case MarkerRow::Tempo:
        case MarkerRow::TimeSignature:
        case MarkerRow::FretHandPosition:
        case MarkerRow::Tone:
        {
            return std::nullopt;
        }
    }
    std::unreachable();
}

// The marker holding a position on a gapless row: the last start at or before it, else the first.
std::size_t EditorController::Impl::markerHolderIndex(
    const std::vector<common::core::GridPosition>& starts,
    const common::core::GridPosition& position)
{
    const auto after = std::ranges::upper_bound(starts, position);
    if (after == starts.begin())
    {
        return 0;
    }
    return static_cast<std::size_t>(std::distance(starts.begin(), after)) - 1;
}

// A span row's markers never overlap, so the only one that can hold the column is the last one
// starting at or before it, and it holds the column exactly while its close lies past it.
std::optional<std::size_t> EditorController::Impl::markerHolderAt(
    const MarkerRow row, const common::core::GridPosition& column) const
{
    const std::vector<common::core::GridPosition> starts = markerStarts(row);
    if (starts.empty())
    {
        return std::nullopt;
    }
    const std::size_t holder = markerHolderIndex(starts, column);
    if (const std::optional<common::core::GridPosition> close = markerClose(row, holder);
        close.has_value() && (column < starts[holder] || !(column < *close)))
    {
        return std::nullopt;
    }
    return holder;
}

// The row and index of the selected marker, or nothing while the selection holds no marker.
std::optional<EditorController::Impl::SelectedMarker> EditorController::Impl::selectedMarker() const
{
    // Every kind but the tone region is identified by its start; a region by its id, whose index is
    // its place in the track the starts were read from.
    const auto marker = [this](const MarkerRow row, const common::core::GridPosition& start) {
        return SelectedMarker{.row = row, .index = markerIndex(row, start)};
    };

    if (const SongSectionSelection* const section = selectedSongSection())
    {
        return marker(MarkerRow::Section, section->position);
    }
    if (const auto* const anchor = std::get_if<TempoAnchorSelection>(&m_selection))
    {
        return marker(MarkerRow::Tempo, anchor->position);
    }
    if (const auto* const signature = std::get_if<TimeSignatureSelection>(&m_selection))
    {
        return marker(MarkerRow::TimeSignature, common::core::measureDownbeat(signature->measure));
    }
    if (const auto* const hold = std::get_if<NoneHoldSelection>(&m_selection))
    {
        return marker(MarkerRow::NoneHold, hold->start);
    }
    if (const auto* const span = std::get_if<SpanSelection>(&m_selection))
    {
        return marker(MarkerRow::Span, span->start);
    }
    if (const auto* const placement = std::get_if<FretHandPositionSelection>(&m_selection))
    {
        return marker(MarkerRow::FretHandPosition, placement->position);
    }
    if (const auto* const region = std::get_if<ToneRegionSelection>(&m_selection))
    {
        SelectedMarker selected{.row = MarkerRow::Tone, .index = std::nullopt};
        if (const common::core::Arrangement* const arrangement = session().currentArrangement())
        {
            const std::vector<common::core::ToneRegion>& regions = arrangement->tone_track.regions;
            const auto found =
                std::ranges::find(regions, region->region_id, &common::core::ToneRegion::id);
            if (found != regions.end())
            {
                selected.index = static_cast<std::size_t>(std::distance(regions.begin(), found));
            }
        }
        return selected;
    }
    return std::nullopt;
}

// The selection naming a row's marker by index, selectedMarker()'s inverse.
MarkerSelection EditorController::Impl::markerSelectionAt(
    const MarkerRow row, const std::size_t index) const
{
    const common::core::GridPosition start = markerStarts(row).at(index);
    switch (row)
    {
        case MarkerRow::Section:
        {
            return SongSectionSelection{.position = start};
        }
        case MarkerRow::Tempo:
        {
            return TempoAnchorSelection{.position = start};
        }
        case MarkerRow::TimeSignature:
        {
            return TimeSignatureSelection{.measure = start.measure};
        }
        case MarkerRow::NoneHold:
        {
            return NoneHoldSelection{.start = start};
        }
        case MarkerRow::Span:
        {
            return SpanSelection{.start = start};
        }
        case MarkerRow::FretHandPosition:
        {
            return FretHandPositionSelection{.position = start};
        }
        case MarkerRow::Tone:
        {
            // The one row whose markers live on the ARRANGEMENT rather than the song, and reaching
            // this arm proves one is loaded: markerStarts reads the tone row's starts from that
            // same arrangement, so without it the starts are empty and the .at above has already
            // thrown. Read through .at for the same reason the start is — an index out of range is
            // a broken precondition, not a walk off the end.
            const common::core::Arrangement* const arrangement = session().currentArrangement();
            return ToneRegionSelection{.region_id = arrangement->tone_track.regions.at(index).id};
        }
    }
    std::unreachable();
}

// The one marker select every pointer and keyboard path shares.
void EditorController::Impl::selectMarker(const MarkerSelection& marker)
{
    // The marker becomes the whole selection, and the caret goes with it: an armed caret is where
    // the next keystroke would author, so one left standing beside a selected marker would be a
    // second answer to what the next press reaches. Demoted in place, as every selecting gesture
    // demotes it, so the cursor line stays where the caret was.
    dissolveChartCaretInPlace();
    setSelection(std::visit([](const auto& kind) -> EditorSelection { return kind; }, marker));
}

// Moves the cursor to the selected marker's start when a pointer select left it outside the span.
void EditorController::Impl::moveCursorIntoSelectedMarker()
{
    const std::optional<SelectedMarker> selected = selectedMarker();
    if (!selected.has_value())
    {
        return;
    }
    const std::optional<std::size_t> index = selected->index;
    if (!index.has_value())
    {
        return;
    }
    // Asked through the row's own holder rule, so a cursor in the gap after a selected span counts
    // as outside it.
    if (markerHolderAt(
            selected->row, pausedCursorPosition(common::core::g_tick_quantum_note_value)) != index)
    {
        moveCursorTo(markerStarts(selected->row).at(*index));
    }
}

// Where the keyboard stands, for the view to keep in sight: the caret, a time selection's moving
// edge, or the paused cursor.
std::optional<common::core::GridPosition> EditorController::Impl::keyboardPosition() const
{
    if (const ChartCaret* const caret = armedChartCaret(); caret != nullptr)
    {
        return caret->position;
    }
    // A time selection's focus is the end the extend moves — the one walking off the view — not
    // the anchor the paused cursor rests on.
    if (const auto* const range = std::get_if<TimeSelection>(&m_selection))
    {
        return range->focus;
    }
    return pausedCursorPosition(common::core::g_tick_quantum_note_value);
}

// keyboardPosition's seconds-space sibling, for the rules that resolve in seconds — the tone
// regions' containment rule above all. A caret riding an automation lane answers here too, since
// that is an armed caret naming its lane. The conversion reads the caret's own GridPosition rather
// than the quantised keyboardPosition(), so the answer is the instant the caret is drawn at and a
// containment test can never put it on the wrong side of a region boundary. With no caret armed the
// transport's clock is the answer, which is also what a playing transport always gives: arming is
// paused-only.
common::core::TimePosition EditorController::Impl::keyboardTimePosition() const
{
    if (const ChartCaret* const caret = armedChartCaret(); caret != nullptr)
    {
        return common::core::TimePosition{session().song().tempo_map.secondsAt(caret->position)};
    }
    return m_transport.position();
}

// The first member of what a selection verb acts on, for the view to centre when it is off screen.
std::optional<common::core::GridPosition> EditorController::Impl::selectionStart() const
{
    // A chart selection made without a caret (a marquee, a plain click on a note) may stand far
    // from the cursor the dissolved caret left behind; its earliest object is what the verbs act
    // on. An automation point likewise.
    const std::vector<ChartSelectionKey> keys = chartSelection().keys();
    if (!keys.empty())
    {
        const common::core::TempoMap& tempo_map = session().song().tempo_map;
        return std::ranges::min(
            keys | std::views::transform([&tempo_map](const ChartSelectionKey& key) {
                return chartCaretSlotFor(tempo_map, key).position;
            }));
    }
    if (const auto* const point = std::get_if<AutomationPointSelection>(&m_selection))
    {
        return point->position;
    }
    if (const std::optional<SelectedMarker> selected = selectedMarker();
        selected.has_value() && selected->index.has_value())
    {
        return markerStarts(selected->row).at(*selected->index);
    }
    if (const ChartCaret* const caret = armedChartCaret(); caret != nullptr)
    {
        return caret->position;
    }
    return std::nullopt;
}

// Only the edit's visibility asks for this. Keeping a selected marker holding the cursor for the
// walk and Tab is the column rule's job (moveCursorIntoSelectedMarker), done before each step. No
// transport test is needed: a marker move is paused-only, so the cursor is always the caret's.
void EditorController::Impl::followMovedMarker(const common::core::GridPosition start)
{
    moveCursorTo(start);
    updateView();
}

// A marker selection names its marker; when the marker is gone — deleted, merged away by a retone,
// moved or taken back by an undo — nothing is left to select. (Delete itself then selects the
// previous marker, as a click would; this release only clears a selection naming nothing.)
void EditorController::Impl::releaseMarkerSelectionNamingNothing()
{
    if (markerSelectionNamesNothing())
    {
        setSelection(std::monostate{});
    }
}

// A marker selection names nothing where its row holds no marker at its identity.
bool EditorController::Impl::markerSelectionNamesNothing() const
{
    const std::optional<SelectedMarker> selected = selectedMarker();
    return selected.has_value() && !selected->index.has_value();
}

// The tail both marker funnels share once a model lands: drop a stale selection, re-derive the
// tone and tempo caches, publish.
void EditorController::Impl::finishMarkerCommit()
{
    releaseMarkerSelectionNamingNothing();
    syncAudibleTone();
    syncTempoDerivedAudio();
    updateView();
}

// The record goes to the log and the light with its sentence to the screen, so the painter never
// learns which target was the subject and which stood in its way.
void EditorController::Impl::refuseMarkerVerb(
    const std::string_view label, const std::string_view reason, const RefusalLight& lit)
{
    m_refusal_log.markerEdit(label, reason);
    flashRefusalLight(lit, reason);
}

// Refuses a marker verb aimed at or past the song's end, lighting nothing: the closing barline
// draws no chip.
void EditorController::Impl::refuseAtSongEnd(const MarkerVerb verb)
{
    refuseMarkerVerb(
        markerVerbLabel(verb), "nothing can start at or past the end of the song", RefusalLight{});
}

// Hands a marker verb's prompt to the view, dropping it while no view is attached.
void EditorController::Impl::showMarkerPrompt(const MarkerPrompt& prompt)
{
    if (m_view != nullptr)
    {
        m_view->showMarkerPrompt(prompt);
    }
}

// Selects a tempo chip through the action gate.
void EditorController::Impl::onTempoAnchorSelected(const common::core::GridPosition position)
{
    runAction(EditorAction::SelectTempoAnchor{.position = position});
}

// Selects a time-signature chip through the action gate.
void EditorController::Impl::onTimeSignatureSelected(const int measure)
{
    runAction(EditorAction::SelectTimeSignature{.measure = measure});
}

// Selects the row's marker starting exactly at a position, the select a chip click names; reports
// whether one stood there.
bool EditorController::Impl::selectMarkerStartingAt(
    const MarkerRow row, const common::core::GridPosition& start)
{
    const std::optional<std::size_t> index = markerIndex(row, start);
    if (!index.has_value())
    {
        return false;
    }
    selectMarker(markerSelectionAt(row, *index));
    return true;
}

// Selects the row's marker by projection index, the select a lane press names, then publishes.
void EditorController::Impl::selectMarkerAt(const MarkerRow row, const std::size_t index)
{
    if (index < markerStarts(row).size())
    {
        selectMarker(markerSelectionAt(row, index));
    }
    updateView();
}

// A marker kind's identity is its start, so this is the one lookup every identity-to-index question
// asks — the selection's own, a chip click's, a pending entry's box.
std::optional<std::size_t> EditorController::Impl::markerIndex(
    const MarkerRow row, const common::core::GridPosition& start) const
{
    const std::vector<common::core::GridPosition> starts = markerStarts(row);
    const auto found = std::ranges::find(starts, start);
    if (found == starts.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(starts.begin(), found));
}

// A tempo chip click selects the anchor it marks and seeks nothing, exactly as a section chip does.
void EditorController::Impl::performActionImpl(const EditorAction::SelectTempoAnchor& action)
{
    selectMarkerStartingAt(MarkerRow::Tempo, action.position);
    updateView();
}

// A time-signature chip click selects the change it marks and seeks nothing.
void EditorController::Impl::performActionImpl(const EditorAction::SelectTimeSignature& action)
{
    selectMarkerStartingAt(MarkerRow::TimeSignature, common::core::measureDownbeat(action.measure));
    updateView();
}

// THE marker rule, for every marker kind: a marker verb authors at the cursor — the armed caret
// where one is armed (a caret riding an automation lane included, since that is an armed caret
// naming its lane), else the paused cursor read at the quantum the kind places on: the placement
// quantum for a section, a tone change and a tempo anchor, which land where an arrow press would
// arm, the tick for a meter, which names the beat it is in. Never the selection: the chords are
// authoring verbs, and Enter and Ctrl+R are the selection's. Nothing while the transport plays —
// the marker plane is paused-only, and a chord must not land a beat late off a moving transport —
// and nothing with no song to hold a marker; those two gates live here, once, rather than in each
// projection or in the view.
std::optional<common::core::GridPosition> EditorController::Impl::cursorPosition(
    const common::core::Fraction quantum) const
{
    if (!m_project.has_value() || m_transport.state().playing)
    {
        return std::nullopt;
    }
    if (const ChartCaret* const armed = armedChartCaret(); armed != nullptr)
    {
        return armed->position;
    }
    return pausedCursorPosition(quantum);
}

// A selection naming a section that is gone answers nothing, as the release that follows every
// commit and undo will confirm.
std::optional<RenameSectionTarget> EditorController::Impl::selectedSectionTarget() const
{
    const auto* const section = std::get_if<SongSectionSelection>(&m_selection);
    if (section == nullptr)
    {
        return std::nullopt;
    }
    const std::vector<common::core::SongSection>& sections = session().song().sections;
    const auto standing =
        std::ranges::find(sections, section->position, &common::core::SongSection::position);
    if (standing == sections.end())
    {
        return std::nullopt;
    }
    return RenameSectionTarget{.position = section->position, .name = standing->name};
}

// The selected tone region and the tone it sounds, or nothing when none is selected or it is gone.
std::optional<RetoneRegionTarget> EditorController::Impl::selectedRegionTarget() const
{
    const auto* const selected = std::get_if<ToneRegionSelection>(&m_selection);
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (selected == nullptr || arrangement == nullptr)
    {
        return std::nullopt;
    }
    const std::vector<common::core::ToneRegion>& regions = arrangement->tone_track.regions;
    const auto region =
        std::ranges::find(regions, selected->region_id, &common::core::ToneRegion::id);
    if (region == regions.end())
    {
        return std::nullopt;
    }
    return RetoneRegionTarget{
        .region_id = region->id, .tone_document_ref = region->tone_document_ref
    };
}

// Enter, decided at the press on the selection's kind, so the view opens what it is handed and
// decides nothing: a section restates on its name, a tempo anchor on the tempo of the span it
// opens, a tone region on its tone (the signal chain has no keyboard model to drill into), the "+"
// row opens the parameter picker, a bend chip the caret stands on restates its bend through the
// picker `B` opens, and every other kind has no restate — a fret-hand position included, since its
// fret has no entry to re-open — so the press does nothing.
void EditorController::Impl::performActionImpl(EditorAction::RestateSelection /*action*/)
{
    if (chartCaretFace() == ChartCaretFace::BendChip)
    {
        runAction(EditorAction::ChooseChartBend{.plane = ChartEntryPlane::Note});
        return;
    }
    if (const std::optional<RenameSectionTarget> section = selectedSectionTarget();
        section.has_value())
    {
        showMarkerPrompt(*section);
    }
    else if (const std::optional<SetTempoTarget> tempo = selectedTempoTarget(); tempo.has_value())
    {
        showMarkerPrompt(*tempo);
    }
    else if (
        const std::optional<SetTimeSignatureTarget> meter = selectedTimeSignatureTarget();
        meter.has_value()
    )
    {
        showMarkerPrompt(*meter);
    }
    else if (
        const std::optional<RetoneRegionTarget> region = selectedRegionTarget(); region.has_value()
    )
    {
        showMarkerPrompt(*region);
    }
    else if (std::holds_alternative<AddAutomationLaneRowSelection>(m_selection))
    {
        showMarkerPrompt(OpenAutomationPickerTarget{});
    }
}

// Ctrl+R, decided at the press: rename the selection where its kind has a name — a section's own,
// a tone region's TONE, shared by every region on it — and do nothing elsewhere, the synthesized
// default tone included, which has no document to name. Read from the selection directly rather
// than from Enter's verb, so the day Enter's meaning on a region changes this one does not move
// with it.
void EditorController::Impl::performActionImpl(EditorAction::RenameSelection /*action*/)
{
    if (const std::optional<RenameSectionTarget> section = selectedSectionTarget();
        section.has_value())
    {
        showMarkerPrompt(*section);
    }
    else if (
        const std::optional<RetoneRegionTarget> region = selectedRegionTarget(); region.has_value()
    )
    {
        // The tone row's own rename, so the two cannot open on different names.
        performActionImpl(
            EditorAction::ToneRenamePrompt{.tone_document_ref = region->tone_document_ref});
    }
}

} // namespace rock_hero::editor::core
