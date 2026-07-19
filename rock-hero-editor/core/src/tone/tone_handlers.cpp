#include "controller/editor_controller_impl.h"
#include "tone/tone_automation_edits.h"
#include "tone/tone_region_edits.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
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

// Snaps a raw normalised value onto a discrete parameter's real states (k/(count-1)); a
// continuous parameter passes through — the model-side sibling of the lanes view's own snap,
// so keyboard-authored values land exactly where pointer-authored ones do.
[[nodiscard]] float snappedLaneValue(float raw_value, bool is_discrete, int discrete_value_count)
{
    if (!is_discrete || discrete_value_count < 2)
    {
        return raw_value;
    }
    const int steps = discrete_value_count - 1;
    const int nearest =
        std::clamp(static_cast<int>(std::lround(raw_value * static_cast<float>(steps))), 0, steps);
    return static_cast<float>(nearest) / static_cast<float>(steps);
}

// One value keystroke's step: one real state on a discrete parameter, else 0.01 (0.001 fine).
[[nodiscard]] float laneValueStep(bool is_discrete, int discrete_value_count, bool fine)
{
    return is_discrete && discrete_value_count >= 2
               ? 1.0F / static_cast<float>(discrete_value_count - 1)
               : (fine ? 0.001F : 0.01F);
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
    const std::vector<common::core::ToneRegion>& regions = arrangement->tone_track.regions;
    for (std::size_t index = 0; index < regions.size(); ++index)
    {
        const common::core::ToneRegion& region = regions[index];
        // The tone schedule is gapless and spans the whole chart: the baseline (first) region owns
        // everything before the first tone change (the pre-measure-1 lead-in), and the last region
        // owns everything after its start (through the end of chart time). So every cursor position
        // resolves to exactly one region, matching how the tone track projects those spans.
        const double start =
            index == 0 ? 0.0 : tempo_map.secondsAtBeat(region.start.measure, region.start.beat);
        const bool is_last = index + 1 == regions.size();
        const double end = is_last ? std::numeric_limits<double>::infinity()
                                   : tempo_map.secondsAtBeat(region.end.measure, region.end.beat);
        if (position.seconds >= start && position.seconds < end)
        {
            return region.id;
        }
    }

    return {};
}

// Resolves the active tone region: the formally selected region if one is selected, otherwise the
// region under the cursor. The active tone is what the rig plays and the signal-chain panel edits;
// the selection is a separate, deliberate concept (the Delete target).
std::string EditorController::Impl::activeToneRegionId() const
{
    if (std::string selected = selectedToneRegionId(); !selected.empty())
    {
        return selected;
    }
    return toneRegionIdAt(m_transport.position());
}

// Names the tone document referenced by the active region, or empty when nothing resolves. Scopes
// the signal-chain and automation-lane projections to the active tone.
std::string EditorController::Impl::activeToneDocumentRef() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }
    const std::string active_region_id = activeToneRegionId();
    if (active_region_id.empty())
    {
        return {};
    }
    for (const common::core::ToneRegion& region : arrangement->tone_track.regions)
    {
        if (region.id == active_region_id)
        {
            return region.tone_document_ref;
        }
    }
    return {};
}

// Resolves the active tone's display name for undo labels, empty when no tone resolves.
std::string EditorController::Impl::activeToneName() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    const std::string tone_document_ref = activeToneDocumentRef();
    if (arrangement == nullptr || tone_document_ref.empty())
    {
        return {};
    }
    return common::core::toneNameFor(*arrangement, tone_document_ref);
}

// Formally selects a region (a deliberate click): the selection is the Delete target and draws a
// white outline, and it becomes the active tone (a preview) until the cursor moves off it.
void EditorController::Impl::applyToneSelection(std::string region_id)
{
    if (region_id.empty())
    {
        // A region deselect only releases the region alternative; it must not disturb a chart
        // or automation selection made since (one selection editor-wide, per-kind lifecycles).
        if (std::holds_alternative<ToneRegionSelection>(m_selection))
        {
            m_selection = std::monostate{};
        }
    }
    else
    {
        m_selection = ToneRegionSelection{.region_id = std::move(region_id)};
    }
    syncAudibleTone();
}

// Makes the tone under the cursor active without formally selecting it: clears any selection (so
// Delete can never fire from mere cursor movement) and points the rig at the cursor's tone.
void EditorController::Impl::activateToneAtCursor()
{
    clearCursorCoupledSelection();
    syncAudibleTone();
}

// Points the rig's audible tone at the active region's tone document. Leaves the audible tone
// unchanged when nothing resolves (no content loaded, or the region has no tone yet).
void EditorController::Impl::syncAudibleTone()
{
    if (!m_project_audio_ready)
    {
        return;
    }

    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return;
    }

    const std::string active_region_id = activeToneRegionId();
    if (active_region_id.empty())
    {
        return;
    }

    const auto region = std::ranges::find_if(
        arrangement->tone_track.regions,
        [&active_region_id](const common::core::ToneRegion& candidate) {
            return candidate.id == active_region_id;
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

// The tone row reports that the playhead crossed into a new region at render cadence. This makes
// that tone active (following the cursor) without a formal selection, so playback never leaves a
// deletable selection behind. Transient view state, so it does not route through an action.
void EditorController::Impl::onToneRegionActivated()
{
    activateToneAtCursor();
    updateView();
}

void EditorController::Impl::onToneRegionResizeRequested(
    std::string region_id, common::core::GridPosition start, common::core::GridPosition end)
{
    runAction(EditorAction::ResizeToneRegion{std::move(region_id), start, end});
}

void EditorController::Impl::onToneRegionCreateRequested(
    common::core::GridPosition position, std::string new_region_id, std::string tone_document_ref)
{
    runAction(
        EditorAction::CreateToneRegion{
            position, std::move(new_region_id), std::move(tone_document_ref)
        });
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
    std::string right_region_id, common::core::GridPosition position)
{
    runAction(EditorAction::MoveToneBoundary{std::move(right_region_id), position});
}

void EditorController::Impl::onToneCreateNewRequested(
    common::core::GridPosition position, std::string name)
{
    runAction(EditorAction::CreateNewTone{position, std::move(name)});
}

// Opens a session-scoped tracking lane; nothing is authored, so this is a direct view-state
// mutation (like selection), not an undoable action.
void EditorController::Impl::onToneAutomationLaneAddRequested(
    const std::string& instance_id, std::string param_id)
{
    const auto identity = m_tone_plugin_identities.find(instance_id);
    if (identity == m_tone_plugin_identities.end())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored lane add for unknown plugin instance instance_id={:?}",
            instance_id);
        return;
    }

    // The picker only offers parameters listed from the selected tone's live chain, so this plugin
    // provably belongs to the selected tone. A plugin inserted before any region was selected
    // carries an empty tone ref on its identity and durable binding; left stale, the projection
    // filters out both this open lane and any lane authored on it later. Adopt the selected tone's
    // ref for the identity, its binding, and the new lane so all three resolve consistently.
    const std::string selected_tone_ref = activeToneDocumentRef();
    if (!selected_tone_ref.empty())
    {
        identity->second.tone_document_ref = selected_tone_ref;
        if (const auto binding = m_tone_plugin_bindings.find(identity->second.plugin_id);
            binding != m_tone_plugin_bindings.end())
        {
            binding->second.tone_document_ref = selected_tone_ref;
        }
    }

    const OpenAutomationLane open_lane{
        .tone_document_ref = selected_tone_ref,
        .plugin_id = identity->second.plugin_id,
        .param_id = std::move(param_id),
    };
    if (std::ranges::find(m_open_automation_lanes, open_lane) == m_open_automation_lanes.end())
    {
        m_open_automation_lanes.push_back(open_lane);
    }
    updateView();
}

// Closes an open tracking lane. Authored lanes are unaffected: their removal is an undoable
// points edit, and the projection subsumes any matching open entry while points exist.
void EditorController::Impl::onToneAutomationLaneRemoveRequested(
    const std::string& instance_id, const std::string& param_id)
{
    const auto identity = m_tone_plugin_identities.find(instance_id);
    if (identity == m_tone_plugin_identities.end())
    {
        return;
    }
    const std::string& plugin_id = identity->second.plugin_id;
    const auto removed = std::ranges::remove_if(
        m_open_automation_lanes, [&plugin_id, &param_id](const OpenAutomationLane& open_lane) {
            return open_lane.plugin_id == plugin_id && open_lane.param_id == param_id;
        });
    if (removed.empty())
    {
        return;
    }
    m_open_automation_lanes.erase(removed.begin(), removed.end());
    updateView();
}

void EditorController::Impl::onSetToneAutomationPoints(
    std::string instance_id, std::string param_id,
    std::vector<common::core::ToneAutomationPoint> points)
{
    runAction(
        EditorAction::SetToneAutomationPoints{
            std::move(instance_id), std::move(param_id), std::move(points)
        });
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

    const common::core::GridPosition before_start = region->start;
    const common::core::GridPosition before_end = region->end;
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
            candidate, action.position, action.new_region_id, action.tone_document_ref);
        !created.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone region create measure={} beat={} detail={:?}",
            action.position.measure,
            action.position.beat,
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
            action.position, action.new_region_id, action.tone_document_ref, tone_name));
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
    if (selectedToneRegionId() == action.region_id)
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

    // When the deleted region was the tone's last reference, the tone leaves the catalog too:
    // a phantom entry would keep owning its name (blocking reuse) while nothing can reach it.
    // The document stays on disk as an orphan, collected at publish, like every removed tone.
    std::optional<common::core::Tone> removed_catalog_tone;
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    const bool still_referenced = std::ranges::any_of(
        tone_track->regions, [&removed_region](const common::core::ToneRegion& candidate) {
            return candidate.tone_document_ref == removed_region.tone_document_ref;
        });
    if (catalog != nullptr && !still_referenced)
    {
        const auto tone =
            std::ranges::find_if(*catalog, [&removed_region](const common::core::Tone& candidate) {
                return candidate.tone_document_ref == removed_region.tone_document_ref;
            });
        if (tone != catalog->end())
        {
            removed_catalog_tone = *tone;
            catalog->erase(tone);
        }
    }

    pushUndoEntry(
        std::make_unique<ToneRegionDeleteEdit>(
            removed_index,
            std::move(removed_region),
            tone_name,
            absorbed_by_prev,
            std::move(removed_catalog_tone)));
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

    const common::core::GridPosition before = right->start;
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
            candidate, action.position, new_region_id, new_tone_document_ref);
        !created.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected new-tone create measure={} beat={} detail={:?}",
            action.position.measure,
            action.position.beat,
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
            action.position, new_region_id, new_tone_document_ref, action.name));

    if (!activateEmptyToneBranch(new_tone_document_ref, new_region_id))
    {
        reloadLiveRigForToneSet(new_region_id);
    }
}

// Fast path for a freshly minted EMPTY tone: appends a passthrough branch to the live rig
// (Tracktion reuses every existing plugin instance across the coalesced graph rebuild, so nothing
// is torn down and playback never stops), then selects the region. No capture is needed because
// nothing on disk is replaced, and no identities merge because an empty branch has no plugins.
// Returns false when no rig is loaded or the add fails; the caller falls back to a full reload.
bool EditorController::Impl::activateEmptyToneBranch(
    const std::string& tone_document_ref, const std::string& select_region_id)
{
    if (!m_project.has_value() || !m_project_audio_ready)
    {
        return false;
    }
    if (const auto added = m_live_rig.addEmptyToneBranch(tone_document_ref); !added.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Empty tone branch add failed; falling back to a full rig reload "
            "tone_document_ref={:?} detail={:?}",
            tone_document_ref,
            added.error().message);
        return false;
    }

    // Keep the loaded-tone bookkeeping coherent so undo/redo coverage checks see the branch; an
    // empty set means coverage is unknowable (port under test reports no chains) and stays so.
    if (!m_loaded_tone_refs.empty() &&
        std::ranges::find(m_loaded_tone_refs, tone_document_ref) == m_loaded_tone_refs.end())
    {
        m_loaded_tone_refs.push_back(tone_document_ref);
    }

    applyToneSelection(select_region_id);
    updateView();
    return true;
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
        applyToneSelection(select_region_id);
        updateView();
        return;
    }

    // The reload replaces every branch from the documents on disk, so unsaved branch drift must
    // be captured first or it is silently lost. On capture failure the reload is skipped: the
    // drifted state stays alive in the current rack, and the new tone gains its branch on the
    // next successful load.
    if (const auto captured = captureLiveRigToDisk(*m_project); !captured.has_value())
    {
        reportError(
            std::string{"Could not capture the current tones before reloading: "} +
            captured.error().message);
        applyToneSelection(select_region_id);
        updateView();
        return;
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    updateView();
    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
            .song_directory = currentSongDirectory(),
            // The by-value parameter is consumed here: moving it into the capture keeps the
            // capture construction non-throwing and gives the completion its own copy for free.
            .finish = [this, owned_region_id = std::move(select_region_id)](
                          std::expected<void, common::audio::LiveRigError> rig_result) {
                if (!rig_result.has_value())
                {
                    finishBusyOperation();
                    reportError(
                        std::string{"Could not load the new tone: "} + rig_result.error().message);
                    updateView();
                    return;
                }
                applyToneSelection(owned_region_id);
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
    // The reset's fresh tone is empty, so the fast path applies; the previous tone's branch
    // lingers silently (the same cost as any inaudible tone) until the next full load.
    if (!activateEmptyToneBranch(after_ref, region_id))
    {
        reloadLiveRigForToneSet(region_id);
    }
}

// Looks up a tone-chain parameter's user-facing name for the undo label, falling back to its id.
std::string EditorController::Impl::automationParameterName(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id) const
{
    const auto parameters = m_tone_automation.listAutomatableParameters(tone_document_ref);
    if (parameters.has_value())
    {
        for (const common::audio::AutomatableParamInfo& parameter : *parameters)
        {
            if (parameter.instance_id == instance_id && parameter.param_id == param_id)
            {
                return parameter.name;
            }
        }
    }
    return param_id;
}

// Supplies the audible chain's durable plugin ids for capture, in chain order, minting ids for
// instances the association does not know yet (first save of a chain built before ids existed).
std::vector<std::string> EditorController::Impl::captureStableIds()
{
    std::vector<std::string> stable_ids;
    const std::vector<PluginViewState>& plugins = m_signal_chain.plugins();
    stable_ids.reserve(plugins.size());
    for (const PluginViewState& plugin : plugins)
    {
        auto identity = m_tone_plugin_identities.find(plugin.instance_id);
        if (identity == m_tone_plugin_identities.end())
        {
            identity = m_tone_plugin_identities
                           .emplace(
                               plugin.instance_id,
                               ToneAutomationIdentity{
                                   .plugin_id = common::core::generatePackageId(),
                                   .tone_document_ref = activeToneDocumentRef(),
                               })
                           .first;
        }
        stable_ids.push_back(identity->second.plugin_id);
    }
    return stable_ids;
}

// Merges load-reported plugin identities into the runtime association, minting durable ids for
// records the documents did not carry yet. Upsert-only: id-preserving undo can revive an instance
// id, so entries are never erased within a session.
void EditorController::Impl::mergeToneChainIdentities(
    const std::vector<common::audio::LoadedToneChainIdentities>& tone_chains)
{
    // The load recreated every plugin instance, so the durable-id-to-live-instance bindings are
    // rebuilt from scratch; keeping older entries would let a dead instance shadow the live one.
    m_tone_plugin_bindings.clear();
    for (const common::audio::LoadedToneChainIdentities& chain : tone_chains)
    {
        for (const common::audio::LoadedTonePluginIdentity& plugin : chain.plugins)
        {
            const std::string plugin_id =
                plugin.stable_id.empty() ? common::core::generatePackageId() : plugin.stable_id;
            m_tone_plugin_identities.insert_or_assign(
                plugin.instance_id,
                ToneAutomationIdentity{
                    .plugin_id = plugin_id,
                    .tone_document_ref = chain.tone_document_ref,
                });
            m_tone_plugin_bindings.insert_or_assign(
                plugin_id,
                common::audio::ToneAutomationBinding{
                    .instance_id = plugin.instance_id,
                    .tone_document_ref = chain.tone_document_ref,
                });
        }
    }
}

// Rewrites every derived playback curve from the arrangement's musical automation. Runs after a
// rig load (the curves were stripped from persisted plugin state) and would run after any future
// tempo-map edit; the shared rebuild skips entries whose plugin is not currently loaded, which
// reconcile on the next load that resolves them.
void EditorController::Impl::rebuildToneAutomationCurves()
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return;
    }

    common::audio::rebuildToneAutomationCurves(
        editContext().tone_automation,
        arrangement->tone_automation,
        session().song().tempo_map,
        m_tone_plugin_bindings);
}

// Replaces a tone-chain plugin parameter's automation and records its inverse. The arrangement's
// musical points are the persisted truth; the derived playback curve is rewritten best-effort, and
// an edit whose points match the current model records nothing.
void EditorController::Impl::performActionImpl(const EditorAction::SetToneAutomationPoints& action)
{
    const auto identity = m_tone_plugin_identities.find(action.instance_id);
    if (identity == m_tone_plugin_identities.end())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored automation edit for unknown plugin instance instance_id={:?}",
            action.instance_id);
        return;
    }

    std::vector<common::core::ToneParameterAutomation>* const automation =
        m_session.currentToneAutomation();
    if (automation == nullptr)
    {
        return;
    }

    if (!action.points.empty())
    {
        const common::core::ToneParameterAutomation candidate{
            .plugin_id = identity->second.plugin_id,
            .param_id = action.param_id,
            .points = action.points,
        };
        if (!isValidToneParameterAutomation(candidate, session().song().tempo_map))
        {
            // The view snaps and clamps before emitting the intent, so a violation means the
            // request went stale; refresh the view so lanes snap back to the model.
            RH_LOG_WARNING(
                "editor.tone",
                "Rejected invalid automation points instance_id={:?} param={:?}",
                action.instance_id,
                action.param_id);
            updateView();
            return;
        }
    }

    const auto existing = std::ranges::find_if(
        *automation, [&](const common::core::ToneParameterAutomation& candidate) {
            return candidate.plugin_id == identity->second.plugin_id &&
                   candidate.param_id == action.param_id;
        });
    std::vector<common::core::ToneAutomationPoint> before;
    if (existing != automation->end())
    {
        before = existing->points;
    }
    if (before == action.points)
    {
        return;
    }

    const EditorEditContext context = editContext();
    if (!applyToneAutomationModel(
            m_session, identity->second.plugin_id, action.param_id, action.points))
    {
        return;
    }
    rewriteDerivedToneCurve(
        context,
        identity->second.tone_document_ref,
        action.instance_id,
        action.param_id,
        action.points);

    pushUndoEntry(
        std::make_unique<ToneAutomationPointsEdit>(
            identity->second.plugin_id,
            action.instance_id,
            action.param_id,
            identity->second.tone_document_ref,
            automationParameterName(
                identity->second.tone_document_ref, action.instance_id, action.param_id),
            std::move(before),
            action.points));
    updateView();
}

// A lane click's seek-and-arm (the row-axis form of the chart lane's empty click, §9b): the
// caret arms at the nearest grid slot on the clicked lane and the transport rests there, so
// play-from-here IS the caret slot. While playing, the click only seeks (armed implies paused).
void EditorController::Impl::onToneAutomationLaneCaretRequested(
    std::string instance_id, std::string param_id, common::core::TimePosition time)
{
    if (isBusy())
    {
        return;
    }
    const common::core::GridPosition position = nearestTempoGridPosition(
        session().song().tempo_map, m_grid_note_value, session().timeline().clamp(time));
    m_transport.seek(
        session().timeline().clamp(
            common::core::TimePosition{secondsAtGridPosition(
                session().song().tempo_map, position)}));
    // The seek re-points the audible tone at the cursor; arming then owns the selection state,
    // exactly like the chart lane's empty click (seek + caret + selection re-derivation).
    activateToneAtCursor();
    if (!m_transport.state().playing)
    {
        armLaneCaret(
            position,
            AutomationLaneRow{
                .instance_id = std::move(instance_id), .param_id = std::move(param_id)
            });
    }
    updateView();
}

// The Insert dispatch for lane rows: plants an on-curve point at the armed caret's slot — the
// keyboard mirror of the on-curve Alt+click landing — and selects it. A slot that already
// carries a point is a no-op (Insert never mutates existing objects), as is an unresolved
// parameter (there is no live line to land on).
void EditorController::Impl::insertLanePointAtCaret(const ChartCaret& caret)
{
    if (!caret.lane.has_value())
    {
        return;
    }
    if (std::optional<LanePointPlan> plan = planLanePointAtCaret(caret); plan.has_value())
    {
        const float landing_value = plan->value;
        plantLanePoint(*caret.lane, std::move(*plan), caret.position, landing_value);
    }
}

// The authored points of one lane row, resolved through the durable plugin identity: the one
// lookup every lane handler shares (caret arming, stepping, planting, moving, deleting).
const std::vector<common::core::ToneAutomationPoint>* EditorController::Impl::lanePointsFor(
    const std::string& instance_id, const std::string& param_id)
{
    const auto identity = m_tone_plugin_identities.find(instance_id);
    const std::vector<common::core::ToneParameterAutomation>* const automation =
        m_session.currentToneAutomation();
    if (identity == m_tone_plugin_identities.end() || automation == nullptr)
    {
        return nullptr;
    }
    const auto entry = std::ranges::find_if(
        *automation, [&](const common::core::ToneParameterAutomation& candidate) {
            return candidate.plugin_id == identity->second.plugin_id &&
                   candidate.param_id == param_id;
        });
    return entry == automation->end() ? nullptr : &entry->points;
}

// Resolves everything a point creation at an armed lane caret needs: the lane's authored
// points, the on-curve landing value at the caret slot, and the parameter's value shape.
std::optional<EditorController::Impl::LanePointPlan> EditorController::Impl::planLanePointAtCaret(
    const ChartCaret& caret)
{
    if (!caret.lane.has_value())
    {
        return std::nullopt;
    }
    const auto identity = m_tone_plugin_identities.find(caret.lane->instance_id);
    if (identity == m_tone_plugin_identities.end() || m_session.currentToneAutomation() == nullptr)
    {
        return std::nullopt;
    }

    LanePointPlan plan;
    if (const std::vector<common::core::ToneAutomationPoint>* const points =
            lanePointsFor(caret.lane->instance_id, caret.lane->param_id))
    {
        plan.points = *points;
    }
    const bool occupied =
        std::ranges::any_of(plan.points, [&](const common::core::ToneAutomationPoint& point) {
            return point.position == caret.position;
        });
    if (occupied)
    {
        return std::nullopt;
    }

    // The landing value comes from the drawn curve; an unauthored lane lands on the live
    // tracking line, which needs the parameter's current value from the port. The value shape
    // rides along so the evaluation holds steps exactly as the lane draws them.
    std::optional<float> fallback;
    if (auto listed = m_tone_automation.listAutomatableParameters(activeToneDocumentRef());
        listed.has_value())
    {
        for (const common::audio::AutomatableParamInfo& parameter : *listed)
        {
            if (parameter.instance_id == caret.lane->instance_id &&
                parameter.param_id == caret.lane->param_id)
            {
                fallback = parameter.current_norm_value;
                plan.is_discrete = parameter.is_discrete;
                plan.discrete_value_count = parameter.discrete_value_count;
                break;
            }
        }
    }
    if (plan.points.empty() && !fallback.has_value())
    {
        return std::nullopt;
    }
    plan.value = toneAutomationCurveValueAt(
        plan.points,
        session().song().tempo_map,
        caret.position,
        plan.is_discrete,
        fallback.value_or(0.0F));
    return plan;
}

// Plants a planned point (sorted insert, points-edit intent, select) — the shared creation
// tail of the Insert verb and the Alt+arrow create-then-nudge.
void EditorController::Impl::plantLanePoint(
    const AutomationLaneRow& row, LanePointPlan plan, common::core::GridPosition position,
    float value)
{
    // The final slot may differ from the planned caret slot (a baked-in time step); creating
    // nothing is the answer if a point sits there after all (a state race).
    if (std::ranges::any_of(plan.points, [&](const common::core::ToneAutomationPoint& point) {
            return point.position == position;
        }))
    {
        return;
    }
    const auto insert_at =
        std::ranges::find_if(plan.points, [&](const common::core::ToneAutomationPoint& candidate) {
            return position < candidate.position;
        });
    plan.points.insert(
        insert_at, common::core::ToneAutomationPoint{.position = position, .norm_value = value});
    onSetToneAutomationPoints(row.instance_id, row.param_id, std::move(plan.points));
    m_selection = AutomationPointSelection{
        .instance_id = row.instance_id,
        .param_id = row.param_id,
        .position = position,
    };
    updateView();
}

// The move-intent fallback on an armed empty lane slot (§9b): the point lands ON the curve at
// the caret and the arrow's step is baked into the creation, so "grab the curve here and pull"
// is one keystroke and ONE undo entry. A caret over an existing point always publishes it as
// the selection (arming re-derives), so reaching here means the slot is empty.
void EditorController::Impl::createAndNudgeLanePointAtCaret(
    const ChartCaret& caret, ChartStepDirection direction, bool fine)
{
    std::optional<LanePointPlan> plan = planLanePointAtCaret(caret);
    if (!plan.has_value())
    {
        return;
    }
    float value = plan->value;
    common::core::GridPosition position = caret.position;
    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        const float step = laneValueStep(plan->is_discrete, plan->discrete_value_count, fine);
        value = snappedLaneValue(
            std::clamp(value + (direction == ChartStepDirection::Up ? step : -step), 0.0F, 1.0F),
            plan->is_discrete,
            plan->discrete_value_count);
    }
    else
    {
        // A refused time step (map edge, neighbor collision, window edge) still creates at the
        // caret itself: the grab succeeded, only the pull refused. The step clamps strictly
        // between the caret slot's neighboring points, like every point nudge.
        const bool later = direction == ChartStepDirection::Right;
        const common::core::GridPosition stepped =
            steppedLaneNudgePosition(caret.position, later, fine);
        const double stepped_seconds = secondsAtGridPosition(session().song().tempo_map, stepped);
        const bool direction_ok = later ? caret.position < stepped : stepped < caret.position;
        const auto next_neighbor =
            std::ranges::find_if(plan->points, [&](const common::core::ToneAutomationPoint& point) {
                return caret.position < point.position;
            });
        const bool inside_neighbors =
            (next_neighbor == plan->points.end() || stepped < next_neighbor->position) &&
            (next_neighbor == plan->points.begin() || std::prev(next_neighbor)->position < stepped);
        const common::core::TimeRange window = activeToneRegionWindow();
        if (direction_ok && inside_neighbors && stepped_seconds >= window.start.seconds &&
            stepped_seconds < window.end.seconds)
        {
            position = stepped;
        }
    }
    plantLanePoint(*caret.lane, std::move(*plan), position, value);
}

// One lane keyboard time-step through the beat axis so the result stays an exact rational: the
// adjacent tempo-grid line, or one 1/960-beat fine step. Landing via nearest-grid-line keeps
// odd grids exact and respects the measure-anchored walk.
common::core::GridPosition EditorController::Impl::steppedLaneNudgePosition(
    const common::core::GridPosition& from, bool later, bool fine) const
{
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const double global_beat =
        static_cast<double>(tempo_map.globalBeatIndex(from.measure, from.beat)) +
        from.offset.toDouble();
    if (fine)
    {
        return fineGridPositionForBeat(tempo_map, global_beat + (later ? 1.0 : -1.0) / 960.0);
    }
    // One grid step is the grid note value scaled by the local meter's beat unit.
    const common::core::TimeSignatureChange signature = tempo_map.timeSignatureAt(from.measure);
    const double step_beats =
        m_grid_note_value.toDouble() * static_cast<double>(signature.denominator);
    const double target_seconds =
        tempo_map.secondsAtGlobalBeatPosition(global_beat + (later ? step_beats : -step_beats));
    return nearestTempoGridPosition(
        tempo_map, m_grid_note_value, common::core::TimePosition{target_seconds});
}

// The active tone region's time window, matching the tone-track projection's span rule (the
// baseline region owns the pre-measure-1 lead-in) so keyboard clamps agree with the pointer's
// editable window.
common::core::TimeRange EditorController::Impl::activeToneRegionWindow() const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }
    const std::string active_region_id = activeToneRegionId();
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::vector<common::core::ToneRegion>& regions = arrangement->tone_track.regions;
    for (std::size_t index = 0; index < regions.size(); ++index)
    {
        const common::core::ToneRegion& region = regions[index];
        if (region.id != active_region_id)
        {
            continue;
        }
        const double start =
            index == 0 ? 0.0
                       : tempo_map.secondsAtNote(
                             region.start.measure, region.start.beat, region.start.offset);
        return common::core::TimeRange{
            .start = common::core::TimePosition{start},
            .end = common::core::TimePosition{tempo_map.secondsAtNote(
                region.end.measure, region.end.beat, region.end.offset)},
        };
    }
    return {};
}

// A deliberate point click: the point becomes THE editor-wide selection, replacing whatever any
// other surface had selected (one selection, structurally). Direct like the chart pointer
// intents rather than an action: selection is display policy, not an undoable edit. The caret
// arms at the clicked slot (2026-07-18 fix — it used to stay behind), so keyboard verbs
// continue from the object just touched; arming re-derives the selection from the slot, which
// is the clicked point. Armed implies paused, so while playing the click only selects.
void EditorController::Impl::onToneAutomationPointSelected(
    std::string instance_id, std::string param_id, common::core::GridPosition position)
{
    if (isBusy())
    {
        return;
    }
    if (m_transport.state().playing)
    {
        m_selection = AutomationPointSelection{
            .instance_id = std::move(instance_id),
            .param_id = std::move(param_id),
            .position = position,
        };
    }
    else
    {
        armLaneCaret(
            position,
            AutomationLaneRow{
                .instance_id = std::move(instance_id), .param_id = std::move(param_id)
            });
    }
    updateView();
}

// Deletes the selected point by replaying its lane's point list without it through the one
// points-edit intent, so removal rides the same undo/curve-rewrite machinery as every lane
// gesture. The durable selection stays put: it publishes as nothing while the point is gone
// and lights up again if undo restores it.
void EditorController::Impl::deleteSelectedAutomationPoint(
    const AutomationPointSelection& selection)
{
    const std::vector<common::core::ToneAutomationPoint>* const points =
        lanePointsFor(selection.instance_id, selection.param_id);
    if (points == nullptr)
    {
        return;
    }

    std::vector<common::core::ToneAutomationPoint> remaining;
    remaining.reserve(points->size());
    for (const common::core::ToneAutomationPoint& point : *points)
    {
        if (!(point.position == selection.position))
        {
            remaining.push_back(point);
        }
    }
    if (remaining.size() == points->size())
    {
        // The selection went stale (the point is already gone); deleting nothing is the answer.
        return;
    }
    onSetToneAutomationPoints(selection.instance_id, selection.param_id, std::move(remaining));
}

// Moves the selected automation point (the move-intent dispatch for the automation
// alternative), replaying its lane's points with the change through the one points-edit
// intent. Up/Down steps the value — one real state on a discrete parameter, else 0.01 (0.001
// fine); Left/Right steps the time axis to the adjacent grid line (1/960 beat fine), refusing
// steps that collapse (map edge) or reverse direction (nearest-line bounce-back) and clamping
// strictly between the neighbors and inside the active region's window. Every refusal — stale
// selection included — is a silent no-op.
void EditorController::Impl::moveSelectedAutomationPoint(
    const AutomationPointSelection& selection, ChartStepDirection direction, bool fine)
{
    const std::vector<common::core::ToneAutomationPoint>* const lane_points =
        lanePointsFor(selection.instance_id, selection.param_id);
    if (lane_points == nullptr)
    {
        return;
    }
    const auto point =
        std::ranges::find_if(*lane_points, [&](const common::core::ToneAutomationPoint& candidate) {
            return candidate.position == selection.position;
        });
    if (point == lane_points->end())
    {
        return;
    }
    const std::size_t point_index = static_cast<std::size_t>(point - lane_points->begin());

    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        // The parameter's value shape comes from the port at move time, exactly like the
        // landing value at creation.
        bool is_discrete = false;
        int discrete_value_count = 0;
        if (auto listed = m_tone_automation.listAutomatableParameters(activeToneDocumentRef());
            listed.has_value())
        {
            for (const common::audio::AutomatableParamInfo& parameter : *listed)
            {
                if (parameter.instance_id == selection.instance_id &&
                    parameter.param_id == selection.param_id)
                {
                    is_discrete = parameter.is_discrete;
                    discrete_value_count = parameter.discrete_value_count;
                    break;
                }
            }
        }
        const float step = laneValueStep(is_discrete, discrete_value_count, fine);
        const float raw = point->norm_value + (direction == ChartStepDirection::Up ? step : -step);
        const float new_value =
            snappedLaneValue(std::clamp(raw, 0.0F, 1.0F), is_discrete, discrete_value_count);
        // Exact inequality via is_neq keeps -Wfloat-equal builds clean; snapped-value
        // no-change detection is deliberately exact.
        if (std::is_neq(new_value <=> point->norm_value))
        {
            std::vector<common::core::ToneAutomationPoint> points = *lane_points;
            points[point_index].norm_value = new_value;
            onSetToneAutomationPoints(selection.instance_id, selection.param_id, std::move(points));
            updateView();
        }
        return;
    }

    const bool later = direction == ChartStepDirection::Right;
    const common::core::GridPosition new_position =
        steppedLaneNudgePosition(selection.position, later, fine);
    if (new_position == selection.position || (later && new_position < selection.position) ||
        (!later && selection.position < new_position))
    {
        return;
    }
    if (point != lane_points->begin() && !(std::prev(point)->position < new_position))
    {
        return;
    }
    if (std::next(point) != lane_points->end() && !(new_position < std::next(point)->position))
    {
        return;
    }
    const common::core::TimeRange window = activeToneRegionWindow();
    const double new_seconds = secondsAtGridPosition(session().song().tempo_map, new_position);
    if (new_seconds < window.start.seconds || new_seconds >= window.end.seconds)
    {
        return;
    }
    std::vector<common::core::ToneAutomationPoint> points = *lane_points;
    // The step cannot cross a neighbor (clamped strictly between them), so order survives
    // without a re-sort.
    points[point_index].position = new_position;
    onSetToneAutomationPoints(selection.instance_id, selection.param_id, std::move(points));
    // A lane caret sitting on the moved point rides along (the caret stays on its object
    // through a nudge). Checked against the OLD position before the selection re-points, and
    // moved directly — the row and remembered string are unchanged, so no re-arm is needed.
    if (const ChartCaret* const caret = armedChartCaret();
        caret != nullptr && caret->lane.has_value() &&
        caret->lane->instance_id == selection.instance_id &&
        caret->lane->param_id == selection.param_id && caret->position == selection.position)
    {
        m_chart_marker =
            ChartCaret{.position = new_position, .string = caret->string, .lane = caret->lane};
    }
    m_selection = AutomationPointSelection{
        .instance_id = selection.instance_id,
        .param_id = selection.param_id,
        .position = new_position,
    };
    updateView();
}

} // namespace rock_hero::editor::core
