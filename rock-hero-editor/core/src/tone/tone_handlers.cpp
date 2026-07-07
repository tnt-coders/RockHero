#include "controller/editor_controller_impl.h"
#include "tone/tone_region_edits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// Finds a mutable authored region by stable id; synthesized default regions have no id and are
// intentionally never found here.
[[nodiscard]] common::core::ToneRegion* findToneRegion(
    common::core::ToneTrack* tone_track, const std::string& region_id)
{
    if (tone_track == nullptr || region_id.empty())
    {
        return nullptr;
    }

    const auto region = std::ranges::find_if(
        tone_track->regions, [&region_id](const common::core::ToneRegion& candidate) {
            return candidate.id == region_id;
        });
    return region == tone_track->regions.end() ? nullptr : &*region;
}

} // namespace

// Names the authored region whose span contains a timeline position, for cursor-follow
// selection. Regions are beat-bounded, so the span converts through the tempo map.
std::string EditorController::Impl::toneRegionIdAt(common::core::TimePosition position) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }

    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    for (const common::core::ToneRegion& region : arrangement->tone_track.regions)
    {
        const double start = tempo_map.secondsAtBeat(region.start.measure, region.start.beat);
        const double end = tempo_map.secondsAtBeat(region.end.measure, region.end.beat);
        if (position.seconds >= start && position.seconds < end)
        {
            return region.id;
        }
    }

    return {};
}

// Stores the tone selection and switches the audible rig tone to match. Selection and audibility
// are one concept: the selected tone is the one heard and the one the signal-chain panel edits.
void EditorController::Impl::applyToneSelection(std::string region_id)
{
    m_selected_tone_region_id = std::move(region_id);
    syncAudibleTone();
}

// Points the rig's audible tone at the selected region's tone document. An empty or unknown
// selection leaves the audible tone unchanged, so a cursor in a gap keeps the previous region's
// tone ringing until scheduled switching lands.
void EditorController::Impl::syncAudibleTone()
{
    if (!m_project_audio_ready || m_selected_tone_region_id.empty())
    {
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return;
    }

    const auto region = std::ranges::find_if(
        arrangement->tone_track.regions, [this](const common::core::ToneRegion& candidate) {
            return candidate.id == m_selected_tone_region_id;
        });
    if (region == arrangement->tone_track.regions.end() || region->tone_document_ref.empty())
    {
        return;
    }

    auto switched = m_live_rig.setAudibleTone(region->tone_document_ref);
    if (!switched.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Could not switch the audible tone tone_document_ref={:?} detail={:?}",
            region->tone_document_ref,
            switched.error().message);
        return;
    }

    // The panel binds to the audible tone, so a successful switch rebinds it to the new chain.
    m_signal_chain.replaceSnapshot(
        common::audio::PluginChainSnapshot{.plugins = std::move(switched->plugins)});
    m_output_gain_db = switched->output_gain.db;
    m_output_gain_preview_before.reset();
}

void EditorController::Impl::onToneRegionSelected(std::string region_id)
{
    runAction(EditorAction::SelectToneRegion{std::move(region_id)});
}

void EditorController::Impl::onToneRegionResizeRequested(
    std::string region_id, common::core::ToneGridPosition start, common::core::ToneGridPosition end)
{
    runAction(EditorAction::ResizeToneRegion{std::move(region_id), start, end});
}

void EditorController::Impl::onToneRegionCreateRequested(
    common::core::ToneGridPosition at, std::string new_region_id, std::string tone_document_ref)
{
    runAction(
        EditorAction::CreateToneRegion{at, std::move(new_region_id), std::move(tone_document_ref)});
}

void EditorController::Impl::onToneRegionDeleteRequested(std::string region_id)
{
    runAction(EditorAction::DeleteToneRegion{std::move(region_id)});
}

void EditorController::Impl::onToneRenameRequested(std::string tone_document_ref, std::string name)
{
    runAction(EditorAction::RenameTone{std::move(tone_document_ref), std::move(name)});
}

void EditorController::Impl::onToneBoundaryMoveRequested(
    std::string right_region_id, common::core::ToneGridPosition position)
{
    runAction(EditorAction::MoveToneBoundary{std::move(right_region_id), position});
}

void EditorController::Impl::onToneCreateNewRequested(
    common::core::ToneGridPosition at, std::string name)
{
    runAction(EditorAction::CreateNewTone{at, std::move(name)});
}

// Stores the selection when the id names an authored region; anything else clears it.
void EditorController::Impl::performActionImpl(EditorAction::SelectToneRegion action)
{
    const common::core::ToneRegion* const region =
        findToneRegion(m_session.currentToneTrack(), action.region_id);
    applyToneSelection(region != nullptr ? std::move(action.region_id) : std::string{});
    updateView();
}

// Applies a snapped resize to the session model and records its inverse in the undo history.
void EditorController::Impl::performActionImpl(const EditorAction::ResizeToneRegion& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    common::core::ToneRegion* const region = findToneRegion(tone_track, action.region_id);
    if (region == nullptr)
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored resize for unknown tone region region_id={:?}",
            action.region_id);
        return;
    }

    if (region->start == action.start && region->end == action.end)
    {
        return;
    }

    common::core::ToneTrack candidate = *tone_track;
    common::core::ToneRegion* const candidate_region = findToneRegion(&candidate, action.region_id);
    candidate_region->start = action.start;
    candidate_region->end = action.end;
    if (const auto valid =
            common::core::validateToneTrackRules(candidate, session().song().tempo_map);
        !valid.has_value())
    {
        // The view snaps and clamps before emitting the intent, so a violation here means the
        // request went stale (for example an undo landed between drag and release); refresh the
        // view so the row snaps back to the authoritative model.
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone region resize region_id={:?} detail={:?}",
            action.region_id,
            valid.error().message);
        updateView();
        return;
    }

    const common::core::ToneGridPosition before_start = region->start;
    const common::core::ToneGridPosition before_end = region->end;
    region->start = action.start;
    region->end = action.end;

    // The region's undo label names its tone, which now lives in the arrangement's tone catalog.
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::string tone_name =
        arrangement != nullptr ? common::core::toneNameFor(*arrangement, region->tone_document_ref)
                               : std::string{};
    pushUndoEntry(
        std::make_unique<ToneRegionResizeEdit>(
            action.region_id, tone_name, before_start, before_end, action.start, action.end));
    updateView();
}

// Splits the region under the marker into a new tone-change region and records its inverse. The new
// region references an existing catalog tone; minting a fresh tone is a caller-side precondition.
void EditorController::Impl::performActionImpl(const EditorAction::CreateToneRegion& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return;
    }

    // Apply the split to a candidate first: createToneRegion enforces that the marker falls inside a
    // region, and validateToneTrackRules catches a duplicate id or bad grid, so a stale request
    // refreshes the view instead of corrupting gap-free coverage.
    common::core::ToneTrack candidate = *tone_track;
    if (const auto created = common::core::createToneRegion(
            candidate, action.at, action.new_region_id, action.tone_document_ref);
        !created.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone region create measure={} beat={} detail={:?}",
            action.at.measure,
            action.at.beat,
            created.error().message);
        updateView();
        return;
    }

    if (const auto valid =
            common::core::validateToneTrackRules(candidate, session().song().tempo_map);
        !valid.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone", "Rejected tone region create detail={:?}", valid.error().message);
        updateView();
        return;
    }

    *tone_track = std::move(candidate);

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::string tone_name =
        arrangement != nullptr ? common::core::toneNameFor(*arrangement, action.tone_document_ref)
                               : std::string{};
    pushUndoEntry(
        std::make_unique<ToneRegionCreateEdit>(
            action.at, action.new_region_id, action.tone_document_ref, tone_name));
    updateView();
}

// Deletes a tone region, merging its span into a neighbor, and records its inverse. Deleting the
// only region is the reset case (clear the tone's chain and rename it "default"); that needs the
// audio boundary and is handled separately, so here it is rejected and left to the caller.
void EditorController::Impl::performActionImpl(const EditorAction::DeleteToneRegion& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return;
    }

    const auto region = std::ranges::find_if(
        tone_track->regions, [&action](const common::core::ToneRegion& candidate) {
            return candidate.id == action.region_id;
        });
    if (region == tone_track->regions.end())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored delete for unknown tone region region_id={:?}",
            action.region_id);
        return;
    }

    if (tone_track->regions.size() == 1)
    {
        // Deleting the only region is a reset, not a merge: repoint it to a fresh empty "Default"
        // tone rather than removing the song's coverage. That path mints and reloads the rig, so it
        // is handled separately from the coverage-preserving merge below.
        resetSoleToneRegion(action.region_id);
        return;
    }

    // Capture what the inverse needs before the merge erases it: the full region, its index, and
    // which neighbor absorbs its span (the previous region unless the removed region is first). The
    // region is copied out (not referenced) because deleteToneRegion erases it from the vector.
    const auto removed_index =
        static_cast<std::size_t>(std::distance(tone_track->regions.begin(), region));
    common::core::ToneRegion removed_region = *region;
    const bool absorbed_by_prev = removed_index > 0;

    if (const auto deleted = common::core::deleteToneRegion(*tone_track, action.region_id);
        !deleted.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Tone region delete not applied region_id={:?} detail={:?}",
            action.region_id,
            deleted.error().message);
        updateView();
        return;
    }

    // Selection follows the span: if the deleted region was selected, select the neighbor that now
    // covers it so the signal-chain panel stays bound to an existing region.
    if (m_selected_tone_region_id == action.region_id)
    {
        const std::size_t neighbor_index = absorbed_by_prev ? removed_index - 1 : removed_index;
        applyToneSelection(
            neighbor_index < tone_track->regions.size() ? tone_track->regions[neighbor_index].id
                                                        : std::string{});
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::string tone_name =
        arrangement != nullptr
            ? common::core::toneNameFor(*arrangement, removed_region.tone_document_ref)
            : std::string{};
    pushUndoEntry(
        std::make_unique<ToneRegionDeleteEdit>(
            removed_index, std::move(removed_region), tone_name, absorbed_by_prev));
    updateView();
}

// Renames a catalog tone and records the inverse. Region labels derive from the catalog, so every
// region referencing the tone relabels together on the next view refresh.
void EditorController::Impl::performActionImpl(const EditorAction::RenameTone& action)
{
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (catalog == nullptr)
    {
        return;
    }

    const auto tone =
        std::ranges::find_if(*catalog, [&action](const common::core::Tone& candidate) {
            return candidate.tone_document_ref == action.tone_document_ref;
        });
    if (tone == catalog->end() || tone->name == action.name)
    {
        return;
    }

    // Tone names are unique within an arrangement so region labels stay unambiguous.
    if (std::ranges::any_of(*catalog, [&action](const common::core::Tone& candidate) {
            return candidate.tone_document_ref != action.tone_document_ref &&
                   candidate.name == action.name;
        }))
    {
        reportError("A tone named \"" + action.name + "\" already exists in this arrangement.");
        return;
    }

    std::string before_name = tone->name;
    tone->name = action.name;
    pushUndoEntry(
        std::make_unique<ToneRenameEdit>(
            action.tone_document_ref, std::move(before_name), action.name));
    updateView();
}

// Moves the shared boundary between two adjacent regions so both neighbors meet at the new position
// (gap-free), and records the inverse. Grid endpoints are audio-inert: the rig switches tones by
// document ref, so moving a boundary never touches the audio graph.
void EditorController::Impl::performActionImpl(const EditorAction::MoveToneBoundary& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    if (tone_track == nullptr)
    {
        return;
    }

    const auto right = std::ranges::find_if(
        tone_track->regions, [&action](const common::core::ToneRegion& candidate) {
            return candidate.id == action.right_region_id;
        });
    if (right == tone_track->regions.end() || right == tone_track->regions.begin())
    {
        // The first region's start is the pinned song boundary; only interior boundaries move.
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored boundary move for unknown or first tone region region_id={:?}",
            action.right_region_id);
        return;
    }

    if (std::prev(right)->end == action.position && right->start == action.position)
    {
        return;
    }

    // Validate on a candidate first: validateToneTrackRules rejects a position that empties or
    // reverses either neighbor, so a stale request refreshes the view instead of committing.
    common::core::ToneTrack candidate = *tone_track;
    const auto index = static_cast<std::size_t>(std::distance(tone_track->regions.begin(), right));
    candidate.regions[index - 1].end = action.position;
    candidate.regions[index].start = action.position;
    if (const auto valid =
            common::core::validateToneTrackRules(candidate, session().song().tempo_map);
        !valid.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone boundary move region_id={:?} detail={:?}",
            action.right_region_id,
            valid.error().message);
        updateView();
        return;
    }

    const common::core::ToneGridPosition before = right->start;
    std::prev(right)->end = action.position;
    right->start = action.position;
    pushUndoEntry(
        std::make_unique<ToneBoundaryMoveEdit>(action.right_region_id, before, action.position));
    updateView();
}

// Creates a new empty tone: mints its document, splits the region under the marker to reference it,
// records one atomic memento (catalog tone + region), then reloads the rig so the tone gains a
// branch and becomes the selection. Undo/redo are pure model; the minted branch lingers harmlessly.
void EditorController::Impl::performActionImpl(const EditorAction::CreateNewTone& action)
{
    if (!m_project.has_value())
    {
        return;
    }
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    // Tone names are unique within an arrangement; reject a duplicate before minting anything.
    if (std::ranges::any_of(*catalog, [&action](const common::core::Tone& candidate) {
            return candidate.name == action.name;
        }))
    {
        reportError("A tone named \"" + action.name + "\" already exists in this arrangement.");
        return;
    }

    // Mint the empty tone document first: loadLiveRig fails on a missing file, so the reference must
    // exist before a region points at it. A rejected split below leaves the file as an orphan (kept
    // and collected at publish), consistent with the tone-model design.
    auto minted = m_live_rig.mintEmptyTone(currentSongDirectory());
    if (!minted.has_value())
    {
        reportError(std::string{"Could not create a new tone: "} + minted.error().message);
        return;
    }
    const std::string new_tone_document_ref = std::move(*minted);
    const std::string new_region_id = common::core::generatePackageId();

    // Validate the split on a candidate (position inside a region, canonical ids and refs) before
    // committing the catalog tone and the region together.
    common::core::ToneTrack candidate = *tone_track;
    if (const auto created = common::core::createToneRegion(
            candidate, action.at, new_region_id, new_tone_document_ref);
        !created.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected new-tone create measure={} beat={} detail={:?}",
            action.at.measure,
            action.at.beat,
            created.error().message);
        updateView();
        return;
    }
    if (const auto valid =
            common::core::validateToneTrackRules(candidate, session().song().tempo_map);
        !valid.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone", "Rejected new-tone create detail={:?}", valid.error().message);
        updateView();
        return;
    }

    catalog->push_back(
        common::core::Tone{.tone_document_ref = new_tone_document_ref, .name = action.name});
    *tone_track = std::move(candidate);
    pushUndoEntry(
        std::make_unique<ToneCreateWithNewToneEdit>(
            action.at, new_region_id, new_tone_document_ref, action.name));

    reloadLiveRigForToneSet(new_region_id);
}

// Reloads the live rig from the current model so a newly referenced tone gains its own branch, then
// selects the given region. Runs behind the loading busy overlay like the arrangement-switch load.
// Undo/redo intentionally skip this: the model is the source of truth and the rig re-derives on the
// next full load, so a branch left behind by an undone create is harmless.
void EditorController::Impl::reloadLiveRigForToneSet(std::string select_region_id)
{
    if (!m_project.has_value() || !m_project_audio_ready)
    {
        // No live rig to reload yet; the model already holds the tone. Select it and refresh.
        applyToneSelection(std::move(select_region_id));
        updateView();
        return;
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    updateView();
    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
            .song_directory = currentSongDirectory(),
            .finish = [this, select_region_id](
                          std::expected<void, common::audio::LiveRigError> rig_result) {
                if (!rig_result.has_value())
                {
                    finishBusyOperation();
                    reportError(
                        std::string{"Could not load the new tone: "} + rig_result.error().message);
                    updateView();
                    return;
                }
                applyToneSelection(select_region_id);
                finishBusyOperation();
                updateView();
            },
        });
}

// Resets the sole tone region to a fresh empty "Default" tone: mints a new empty document, repoints
// the region and its catalog entry to it, records the inverse, then reloads the rig. The previous
// tone's document is left on disk as an orphan (collected at publish), per the tone-model design.
void EditorController::Impl::resetSoleToneRegion(const std::string& region_id)
{
    if (!m_project.has_value())
    {
        return;
    }
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (tone_track == nullptr || catalog == nullptr || arrangement == nullptr)
    {
        return;
    }

    const auto region = std::ranges::find_if(
        tone_track->regions, [&region_id](const common::core::ToneRegion& candidate) {
            return candidate.id == region_id;
        });
    if (region == tone_track->regions.end())
    {
        return;
    }
    const std::string before_ref = region->tone_document_ref;
    const std::string before_name = common::core::toneNameFor(*arrangement, before_ref);

    auto minted = m_live_rig.mintEmptyTone(currentSongDirectory());
    if (!minted.has_value())
    {
        reportError(std::string{"Could not reset the tone: "} + minted.error().message);
        return;
    }
    const std::string after_ref = std::move(*minted);

    // Repoint the region and its catalog entry to the fresh empty tone named "Default".
    region->tone_document_ref = after_ref;
    const auto tone =
        std::ranges::find_if(*catalog, [&before_ref](const common::core::Tone& candidate) {
            return candidate.tone_document_ref == before_ref;
        });
    if (tone != catalog->end())
    {
        tone->tone_document_ref = after_ref;
        tone->name = "Default";
    }
    else
    {
        catalog->push_back(common::core::Tone{.tone_document_ref = after_ref, .name = "Default"});
    }

    pushUndoEntry(std::make_unique<ToneResetEdit>(region_id, before_ref, before_name, after_ref));
    reloadLiveRigForToneSet(region_id);
}

} // namespace rock_hero::editor::core
