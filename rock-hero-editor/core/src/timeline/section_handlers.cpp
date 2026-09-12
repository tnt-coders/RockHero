#include "controller/editor_controller_impl.h"
#include "timeline/song_section_edits.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The one snap the section verbs share: a section starts on a measure downbeat, and nothing else
// is representable through them. Enforced by the verb rather than by SongSection, which keeps the
// stored type (and the format) a plain GridPosition validated against the tempo map.
[[nodiscard]] common::core::GridPosition measureDownbeat(
    const common::core::GridPosition& position) noexcept
{
    return common::core::GridPosition{.measure = position.measure, .beat = 1, .offset = {}};
}

// True when a downbeat can carry a section: on the grid and strictly before the closing barline,
// since a section starting at the song's end would name a passage of no length.
[[nodiscard]] bool downbeatCanCarrySection(
    const common::core::GridPosition& downbeat, const common::core::TempoMap& tempo_map)
{
    return downbeat.measure >= 1 && downbeat < common::core::terminalGridPosition(tempo_map);
}

// Finds the section starting exactly at a position; a section's position is its identity.
[[nodiscard]] const common::core::SongSection* findSection(
    const std::vector<common::core::SongSection>& sections,
    const common::core::GridPosition& position)
{
    const auto match = std::ranges::find(sections, position, &common::core::SongSection::position);
    return match != sections.end() ? &*match : nullptr;
}

// Restores the strictly-ascending order the package reader enforces, after an insert or a move.
void sortSections(std::vector<common::core::SongSection>& sections)
{
    std::ranges::sort(sections, std::ranges::less{}, &common::core::SongSection::position);
}

// Trims surrounding whitespace so an all-blank name refuses like an empty one: the reader forbids
// an empty name, and a name of spaces would draw as a blank chip nobody could click accurately.
[[nodiscard]] std::string trimmedName(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return {};
    }
    return std::string{name.substr(first, name.find_last_not_of(" \t") - first + 1)};
}

} // namespace

// Returns the formally selected section, or null when another kind (or nothing) is selected.
const SongSectionSelection* EditorController::Impl::selectedSongSection() const
{
    return std::get_if<SongSectionSelection>(&m_selection);
}

// Selects a section chip, or releases the section alternative when no position is named. A
// deselect must not disturb a selection made on another surface since, exactly as the tone
// region's deselect does not.
void EditorController::Impl::applySongSectionSelection(
    const std::optional<common::core::GridPosition> position)
{
    if (position.has_value())
    {
        setSelection(SongSectionSelection{.position = *position});
        return;
    }
    if (selectedSongSection() != nullptr)
    {
        setSelection(std::monostate{});
    }
}

// The measure a section verb lands in: the marker rule the tone-change insert already follows —
// the armed caret when one exists, else the transport position — snapped to that measure's
// downbeat. One position concept, so an insert lands where play would pick up.
common::core::GridPosition EditorController::Impl::markerSongSectionDownbeat() const
{
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const ChartCaret* const armed = armedChartCaret();
    if (armed != nullptr)
    {
        return measureDownbeat(armed->position);
    }
    return measureDownbeat(
        nearestTempoGridPosition(tempo_map, placementQuantum(), m_transport.position()));
}

// Commits a new section list as one undo entry and republishes. The list is assigned rather than
// diffed: SongSectionsEdit carries both sides whole, so every verb reaching here shares one apply.
void EditorController::Impl::commitSongSections(
    std::vector<common::core::SongSection> after, std::string label)
{
    std::vector<common::core::SongSection> before = session().song().sections;
    m_session.songSections() = after;
    pushUndoEntry(
        std::make_unique<SongSectionsEdit>(std::move(before), std::move(after), std::move(label)));
    updateView();
}

void EditorController::Impl::onSongSectionSelected(
    std::optional<common::core::GridPosition> position)
{
    runAction(EditorAction::SelectSongSection{.position = position});
}

void EditorController::Impl::onSongSectionInsertRequested(std::string name)
{
    runAction(EditorAction::InsertSongSection{.name = std::move(name)});
}

void EditorController::Impl::onSongSectionRenameRequested(
    common::core::GridPosition position, std::string name)
{
    runAction(EditorAction::RenameSongSection{.position = position, .name = std::move(name)});
}

// Selecting a chip only moves the editor-wide selection; it seeks nothing, which is what keeps a
// section selection alive under the cursor-coupled clear that a ruler-body click would trigger.
void EditorController::Impl::performActionImpl(const EditorAction::SelectSongSection& action)
{
    applySongSectionSelection(action.position);
    updateView();
}

// Adds a section at the marker's measure downbeat. Every failure is a refusal, not a clamp and not
// a silent overwrite: an empty name, a downbeat outside the song, and a downbeat another section
// already holds each leave the list exactly as it was (rename is the verb for the last of those).
void EditorController::Impl::performActionImpl(const EditorAction::InsertSongSection& action)
{
    const std::string name = trimmedName(action.name);
    if (name.empty())
    {
        RH_LOG_WARNING(
            "editor.section",
            "Rejected section insert detail={:?}",
            std::string_view{"empty name"});
        updateView();
        return;
    }

    const common::core::GridPosition downbeat = markerSongSectionDownbeat();
    if (!downbeatCanCarrySection(downbeat, session().song().tempo_map))
    {
        RH_LOG_WARNING(
            "editor.section",
            "Rejected section insert measure={} detail={:?}",
            downbeat.measure,
            std::string_view{"outside the song"});
        updateView();
        return;
    }

    std::vector<common::core::SongSection> sections = session().song().sections;
    if (findSection(sections, downbeat) != nullptr)
    {
        RH_LOG_WARNING(
            "editor.section",
            "Rejected section insert measure={} detail={:?}",
            downbeat.measure,
            std::string_view{"downbeat occupied"});
        updateView();
        return;
    }

    sections.push_back(common::core::SongSection{.position = downbeat, .name = name});
    sortSections(sections);
    applySongSectionSelection(downbeat);
    commitSongSections(std::move(sections), "Add " + name);
}

// Renames the section at a position. Position-anchored like the tone rename beside it, so Ctrl+M
// on a selected chip and the chip double-click reach a section the same way. A name that changes
// nothing pushes nothing.
void EditorController::Impl::performActionImpl(const EditorAction::RenameSongSection& action)
{
    const std::string name = trimmedName(action.name);
    if (name.empty())
    {
        RH_LOG_WARNING(
            "editor.section",
            "Rejected section rename detail={:?}",
            std::string_view{"empty name"});
        updateView();
        return;
    }

    std::vector<common::core::SongSection> sections = session().song().sections;
    const auto match =
        std::ranges::find(sections, action.position, &common::core::SongSection::position);
    if (match == sections.end() || match->name == name)
    {
        updateView();
        return;
    }

    const std::string before_name = match->name;
    match->name = name;
    commitSongSections(std::move(sections), "Rename Section " + before_name + " to " + name);
}

// Moves the selected section one measure (the Alt+arrow dispatch for the section alternative).
// Refused rather than clamped when the target leaves the song or another section already holds
// that downbeat, in line with every other refused move.
void EditorController::Impl::moveSelectedSongSection(
    const SongSectionSelection& selection, const ChartStepDirection direction)
{
    const int delta = direction == ChartStepDirection::Left    ? -1
                      : direction == ChartStepDirection::Right ? 1
                                                               : 0;
    if (delta == 0)
    {
        // Up/Down has no meaning for a marker on one timeline row; a silent no-op, like a string
        // step over a keyframe that has no string.
        return;
    }

    const common::core::GridPosition target = common::core::GridPosition{
        .measure = selection.position.measure + delta, .beat = 1, .offset = {}
    };
    std::vector<common::core::SongSection> sections = session().song().sections;
    const auto match =
        std::ranges::find(sections, selection.position, &common::core::SongSection::position);
    if (match == sections.end() || !downbeatCanCarrySection(target, session().song().tempo_map) ||
        findSection(sections, target) != nullptr)
    {
        return;
    }

    match->position = target;
    sortSections(sections);
    // The position IS the section's identity, so the selection is re-keyed with the move or it
    // would name a section that no longer exists there.
    applySongSectionSelection(target);
    commitSongSections(std::move(sections), "Move Section");
}

// Deletes the selected section (the Delete-key dispatch for the section alternative).
void EditorController::Impl::deleteSelectedSongSection(const SongSectionSelection& selection)
{
    std::vector<common::core::SongSection> sections = session().song().sections;
    const auto match =
        std::ranges::find(sections, selection.position, &common::core::SongSection::position);
    if (match == sections.end())
    {
        return;
    }

    const std::string name = match->name;
    sections.erase(match);
    applySongSectionSelection(std::nullopt);
    commitSongSections(std::move(sections), "Delete " + name);
}

} // namespace rock_hero::editor::core
