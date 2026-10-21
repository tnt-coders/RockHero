#include "controller/editor_controller_impl.h"
#include "controller/marker_model_commit.h"
#include "timeline/song_sections_snapshot.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/song/song_section_rules.h>
#include <rock_hero/editor/core/timeline/marker_verb.h>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Finds the section starting exactly at a position; a section's position is its identity.
[[nodiscard]] std::vector<common::core::SongSection>::iterator findSection(
    std::vector<common::core::SongSection>& sections, const common::core::GridPosition& position)
{
    return std::ranges::find(sections, position, &common::core::SongSection::position);
}

} // namespace

// Returns the formally selected section, or null when another kind (or nothing) is selected.
const SongSectionSelection* EditorController::Impl::selectedSongSection() const
{
    return std::get_if<SongSectionSelection>(&m_selection);
}

// The section chord authors at the cursor and never reads the selection: the cursor's placement
// slot, the tone and tempo chords' read — the grid line with snap on, the tick with snap off. A
// section already standing exactly there is restated (renamed) — which is what makes the double
// press work: the first inserts and selects, and since the commit keeps the cursor on its slot, the
// second finds that section there. Where none stands and a section may start, the verb is an
// insert; at or past the terminal anchor it is nothing, so the chord is inert exactly where the
// commit would refuse it rather than prompting for a name it cannot use.
SectionChordTarget EditorController::Impl::sectionChordTarget() const
{
    const std::optional<common::core::GridPosition> position = cursorPosition(placementQuantum());
    if (!position.has_value())
    {
        return {};
    }
    const std::vector<common::core::SongSection>& sections = session().song().sections;
    if (const auto standing =
            std::ranges::find(sections, *position, &common::core::SongSection::position);
        standing != sections.end())
    {
        return RenameSectionTarget{.position = *position, .name = standing->name};
    }
    if (!common::core::markerCanStartAt(*position, session().song().tempo_map))
    {
        return {};
    }
    return InsertSectionTarget{.position = *position};
}

void EditorController::Impl::onSongSectionSelected(
    std::optional<common::core::GridPosition> position)
{
    runAction(EditorAction::SelectSongSection{.position = position});
}

void EditorController::Impl::onSongSectionInsertRequested(
    const common::core::GridPosition position, std::string name)
{
    runAction(EditorAction::InsertSongSection{.position = position, .name = std::move(name)});
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
    // Bound once so the guard and the read are provably the same optional (the CI
    // unchecked-optional-access check cannot tie two separate member accesses together).
    if (const std::optional<common::core::GridPosition>& position = action.position;
        position.has_value())
    {
        selectMarker(SongSectionSelection{.position = *position});
    }
    else
    {
        releaseSelectionIfHeld<SongSectionSelection>();
    }
    updateView();
}

// Adds a section at the position the press captured. Every failure is a refusal, not a clamp and
// not a silent overwrite: an empty name, a position off the grid or at or past the terminal, and a
// position another section already holds each leave the list exactly as it was (rename is the verb
// for the last of those) — all three are section rules, so the commit refuses them rather than this
// verb restating them. Like every insert, it leaves what it made SELECTED — the caret it was typed
// from demotes in place — so Enter, Delete and Alt+arrows act on the new section next, and an
// arrow re-arms the caret where it stood.
void EditorController::Impl::performActionImpl(const EditorAction::InsertSongSection& action)
{
    const std::string name = common::core::trimmedSongSectionName(action.name);
    SongSectionsSnapshot before = SongSectionsSnapshot::capture(session());
    SongSectionsSnapshot after = before;
    after.sections.push_back(common::core::SongSection{.position = action.position, .name = name});

    if (commitMarkerModel(std::move(before), std::move(after), "Add " + name) !=
        MarkerCommit::Refused)
    {
        // The insert selects what it made, and a selection made after the commit publishes with
        // this refresh: the commit's own publish ran before it existed.
        selectMarker(SongSectionSelection{.position = action.position});
        updateView();
    }
}

// Renames the section at a position. Position-anchored like the tone rename beside it, so Ctrl+M at
// the cursor, Enter and Ctrl+R on a selected chip, and the chip double-click reach a section the
// same way. A name that changes nothing pushes nothing, and a blank one is refused by the section
// rules at the commit. A restate SELECTS its target — whether the name changed or not, so the
// chord typed at the cursor leaves the section it addressed under Enter, Delete and Alt+arrows —
// and only a refused rename leaves the selection where it was.
void EditorController::Impl::performActionImpl(const EditorAction::RenameSongSection& action)
{
    SongSectionsSnapshot before = SongSectionsSnapshot::capture(session());
    SongSectionsSnapshot after = before;
    const auto match = findSection(after.sections, action.position);
    if (match == after.sections.end())
    {
        // No section starts there, so there is nothing to rename and nothing to publish.
        return;
    }

    const std::string before_name = match->name;
    const std::string name = common::core::trimmedSongSectionName(action.name);
    match->name = name;
    const SongSectionSelection section{.position = action.position};
    if (commitMarkerModel(
            std::move(before),
            std::move(after),
            markerVerbLabel(MarkerVerb::RenameSection) + " " + before_name + " to " + name) ==
        MarkerCommit::Refused)
    {
        flashSongSectionChip(section);
        return;
    }
    selectMarker(section);
    updateView();
}

// Moves the selected section one placement-quantum line, the step every marker start takes (the
// Alt+arrow dispatch for the section alternative). Refused rather than clamped, with the chip
// glowing red: onto the terminal or a position another section already holds — both section rules,
// so the commit is what refuses them — and here where the grid step has nowhere to go, off either
// end of the song (steppedNudgePosition). A landed move brings the paused cursor to the new
// position, as every selection move of a marker does.
void EditorController::Impl::moveSelectedSongSection(
    const SongSectionSelection& selection, const ChartStepDirection direction)
{
    const std::string label = markerVerbLabel(MarkerVerb::MoveSection);
    SongSectionsSnapshot before = SongSectionsSnapshot::capture(session());
    SongSectionsSnapshot after = before;
    const auto match = findSection(after.sections, selection.position);
    if (match == after.sections.end())
    {
        // The selection went stale; moving nothing is the answer.
        return;
    }
    // Left or Right, always: a marker moves horizontally only, and the move dispatch refuses a
    // vertical direction for every marker kind before it reaches here.
    const std::optional<common::core::GridPosition> step =
        steppedNudgePosition(selection.position, direction == ChartStepDirection::Right);
    if (!step.has_value())
    {
        m_refusal_log.markerEdit(label, "section would leave the song");
        flashSongSectionChip(selection);
        return;
    }
    const common::core::GridPosition target = *step;

    match->position = target;
    if (!commitMarkerMove(
            std::move(before), std::move(after), label, SongSectionSelection{.position = target}))
    {
        flashSongSectionChip(selection);
        return;
    }
    // The position IS the section's identity, so the commit selects the moved section under its new
    // one; followMovedMarker publishes it together with the cursor it brings along.
    followMovedMarker(target);
}

// Deletes the selected section (the Delete-key dispatch for the section alternative). Nothing
// stays selected: the commit releases a selection naming the section that is gone.
void EditorController::Impl::deleteSelectedSongSection(const SongSectionSelection& selection)
{
    SongSectionsSnapshot before = SongSectionsSnapshot::capture(session());
    SongSectionsSnapshot after = before;
    const auto match = findSection(after.sections, selection.position);
    if (match == after.sections.end())
    {
        return;
    }

    const std::string name = match->name;
    after.sections.erase(match);
    if (commitMarkerModel(std::move(before), std::move(after), "Delete " + name) ==
        MarkerCommit::Refused)
    {
        flashSongSectionChip(selection);
    }
}

// The commit has already logged the refusal; the chip glows red to say it was this one.
void EditorController::Impl::flashSongSectionChip(const SongSectionSelection& selection)
{
    flashRefusalLight(lightOf(selection));
}

} // namespace rock_hero::editor::core
