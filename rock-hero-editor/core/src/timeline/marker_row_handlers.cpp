#include "chart/chart_selection.h"
#include "controller/editor_controller_impl.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

// The marker rows (docs/plans/completed/keyboard-focus-rows.md): the ruler's section, tempo and
// time-signature rows and the tone row, which the keyboard reaches by selecting a marker. Every row
// answers the same questions — where its markers start, which one holds a position, which one the
// selection names — so the walk, a chip click and Tab share one model of all four.

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
                starts.push_back(
                    common::core::GridPosition{
                        .measure = anchors[index].measure,
                        .beat = anchors[index].beat,
                        .offset = {},
                    });
            }
            break;
        }
        case MarkerRow::TimeSignature:
        {
            for (const common::core::TimeSignatureChange& change :
                 session().song().tempo_map.timeSignatures())
            {
                starts.push_back(
                    common::core::GridPosition{.measure = change.measure, .beat = 1, .offset = {}});
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

std::optional<EditorController::Impl::SelectedMarker> EditorController::Impl::selectedMarker() const
{
    // Every kind but the tone region is identified by its start; a region by its id, whose index is
    // its place in the track the starts were read from.
    const auto marker = [this](const MarkerRow row, const common::core::GridPosition& start) {
        const std::vector<common::core::GridPosition> starts = markerStarts(row);
        const auto found = std::ranges::find(starts, start);
        return SelectedMarker{
            .row = row,
            .index =
                found == starts.end()
                    ? std::nullopt
                    : std::optional{static_cast<std::size_t>(std::distance(starts.begin(), found))},
        };
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
        return marker(
            MarkerRow::TimeSignature,
            common::core::GridPosition{.measure = signature->measure, .beat = 1, .offset = {}});
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

EditorController::Impl::MarkerSelection EditorController::Impl::markerSelectionAt(
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

void EditorController::Impl::selectMarker(const MarkerSelection& marker)
{
    // The marker becomes the whole selection, and the caret goes with it: an armed caret is where
    // the next keystroke would author, so one left standing beside a selected marker would be a
    // second answer to what the next press reaches. Demoted in place, as every selecting gesture
    // demotes it, so the cursor line stays where the caret was.
    dissolveChartCaretInPlace();
    setSelection(std::visit([](const auto& kind) -> EditorSelection { return kind; }, marker));
}

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
    const std::vector<common::core::GridPosition> starts = markerStarts(selected->row);
    if (markerHolderIndex(starts, pausedCursorPosition(g_tick_quantum_note_value)) != *index)
    {
        moveCursorTo(starts[*index]);
    }
}

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
    return pausedCursorPosition(g_tick_quantum_note_value);
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
        return common::core::TimePosition{secondsAtGridPosition(
            session().song().tempo_map, caret->position)};
    }
    return m_transport.position();
}

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
// moved or taken back by an undo — nothing is left to select, exactly as Delete leaves nothing
// behind.
void EditorController::Impl::releaseMarkerSelectionNamingNothing()
{
    if (const std::optional<SelectedMarker> selected = selectedMarker();
        selected.has_value() && !selected->index.has_value())
    {
        setSelection(std::monostate{});
    }
}

void EditorController::Impl::onTempoAnchorSelected(const common::core::GridPosition position)
{
    runAction(EditorAction::SelectTempoAnchor{.position = position});
}

void EditorController::Impl::onTimeSignatureSelected(const int measure)
{
    runAction(EditorAction::SelectTimeSignature{.measure = measure});
}

void EditorController::Impl::selectMarkerStartingAt(
    const MarkerRow row, const common::core::GridPosition& start)
{
    const std::vector<common::core::GridPosition> starts = markerStarts(row);
    if (const auto found = std::ranges::find(starts, start); found != starts.end())
    {
        selectMarker(
            markerSelectionAt(row, static_cast<std::size_t>(std::distance(starts.begin(), found))));
    }
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
    selectMarkerStartingAt(
        MarkerRow::TimeSignature,
        common::core::GridPosition{.measure = action.measure, .beat = 1, .offset = {}});
    updateView();
}

// THE marker rule, for every marker kind: a marker verb authors at the cursor — the armed caret
// where one is armed (a caret riding an automation lane included, since that is an armed caret
// naming its lane), else the paused cursor read at the quantum the kind places on: the tick for a
// section, which snaps to its measure's downbeat from there, the placement quantum for a tone
// change, which lands where an arrow press would arm. Never the selection: the chords are
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

// Enter's verb, dispatched on the selection's kind here so the view opens what it names and
// decides nothing: a section restates on its name, a tone region on its tone (until the signal
// chain has a keyboard model to drill into, plan 53 Phase 5), the "+" row opens the parameter
// picker, and every other kind has no restate.
RestateTarget EditorController::Impl::restateTarget() const
{
    if (const std::optional<RenameSectionTarget> section = selectedSectionTarget();
        section.has_value())
    {
        return *section;
    }
    if (const std::optional<RetoneRegionTarget> region = selectedRegionTarget(); region.has_value())
    {
        return *region;
    }
    if (std::holds_alternative<AddAutomationLaneRowSelection>(m_selection))
    {
        return OpenAutomationPickerTarget{};
    }
    return {};
}

// Ctrl+R's verb: rename the selection where its kind has a name — a section's own, a tone region's
// TONE, shared by every region on it — and nothing elsewhere, the synthesized default tone
// included, which has no document to name. Read from the selection directly rather than from
// Enter's verb, so the day Enter's meaning on a region changes this one does not move with it.
RenameTarget EditorController::Impl::renameTarget() const
{
    if (const std::optional<RenameSectionTarget> section = selectedSectionTarget();
        section.has_value())
    {
        return *section;
    }
    if (const std::optional<RetoneRegionTarget> region = selectedRegionTarget();
        region.has_value() && !region->tone_document_ref.empty())
    {
        return RenameToneTarget{
            .tone_document_ref = region->tone_document_ref,
            .name = toneNameForRef(region->tone_document_ref),
        };
    }
    return {};
}

} // namespace rock_hero::editor::core
