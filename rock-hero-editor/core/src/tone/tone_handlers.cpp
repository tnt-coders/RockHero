#include "controller/editor_controller_impl.h"
#include "tone/tone_automation_edits.h"
#include "tone/tone_model_edit.h"
#include "tone/tone_track_projection.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <rock_hero/editor/core/timeline/tempo_grid_geometry.h>
#include <rock_hero/editor/core/timeline/timeline_geometry.h>
#include <rock_hero/editor/core/tone/tone_automation_pointer.h>
#include <string>
#include <string_view>
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

// One value keystroke's step: one real state on a discrete parameter, else 0.01. A single step,
// because the value axis is not a position axis — grid snap decides where things go in TIME and
// says nothing about a knob, so it has no second tier to offer here.
[[nodiscard]] float laneValueStep(bool is_discrete, int discrete_value_count)
{
    return is_discrete && discrete_value_count >= 2
               ? 1.0F / static_cast<float>(discrete_value_count - 1)
               : 0.01F;
}

// The lanes view's ÷width forward pixel map (xForSeconds), replicated so the ported move/insert
// drag hit-tests and window-clamps against the same pixels the view did. This is a forward map for
// resolving which point a press falls on and where the editable window's edges sit — NOT a snap
// path: every position SNAP on the lane (caret arm, insert ghost, Alt placement, drag) runs through
// the one ÷(width - 1) placement seam, laneSnapPositionForX, so the lane has a single horizontal
// snap authority as the chart does.
[[nodiscard]] std::optional<float> laneXForSeconds(
    double seconds, common::core::TimeRange visible_timeline, int content_width)
{
    const double duration = visible_timeline.duration().seconds;
    if (content_width <= 0 || duration <= 0.0)
    {
        return std::nullopt;
    }
    return static_cast<float>(
        (seconds - visible_timeline.start.seconds) / duration * static_cast<double>(content_width));
}

// Maps a lane-local pixel y onto a normalised value inside one lane's value band, clamped to the
// band, matching the lanes view's valueForY so the delta-based pull moves bit-for-bit as the view
// did. The band geometry is view-computed and carried on the event, so no view layout constant
// reaches editor-core.
[[nodiscard]] float laneValueForY(float y, const ToneAutomationLaneExtent& extent)
{
    const float band_height = std::max(1.0F, extent.value_band_height);
    const float relative = (y - extent.value_band_top) / band_height;
    return std::clamp(1.0F - relative, 0.0F, 1.0F);
}

// The pixel y a point at a normalised value draws at inside one lane's value band; the point
// hit-test's vertical coordinate, matching the lanes view's hitAt.
[[nodiscard]] float laneValueBandY(float norm_value, const ToneAutomationLaneExtent& extent)
{
    return extent.value_band_top + ((1.0F - norm_value) * extent.value_band_height);
}

// Snaps a lane-local pixel x through the exact placement seam an Alt+click / drag commit uses:
// timelinePositionForX inverts the pixel to a click time (÷ (width - 1), clamped), then
// nearestTempoGridPosition on the placement quantum. That is musicalGridPositionForX bit-for-bit,
// the one snap authority the ghost already rides.
[[nodiscard]] std::optional<common::core::GridPosition> laneSnapPositionForX(
    const common::core::TempoMap& tempo_map, common::core::Fraction placement_quantum,
    common::core::TimeRange visible_timeline, int content_width, float content_x)
{
    const std::optional<common::core::TimePosition> clicked =
        timelinePositionForX(content_x, visible_timeline, content_width);
    if (!clicked.has_value())
    {
        return std::nullopt;
    }
    return nearestTempoGridPosition(tempo_map, placement_quantum, *clicked);
}

// Drops every catalog tone no region references any more. A phantom entry would keep owning its
// name while nothing could reach it: the picker offers only tones some region references, so an
// unreferenced entry is already unofferable. Run on every tone commit, so the two verbs that can
// take a tone's last reference (delete, retone) can never disagree about it.
void pruneUnreferencedTones(
    const common::core::ToneTrack& tone_track, std::vector<common::core::Tone>& catalog)
{
    std::erase_if(catalog, [&tone_track](const common::core::Tone& tone) {
        return std::ranges::none_of(
            tone_track.regions, [&tone](const common::core::ToneRegion& region) {
                return region.tone_document_ref == tone.tone_document_ref;
            });
    });
}

// The one duplicate a catalog may not hold: two tones with one name, which would draw two regions
// with the same label and no way to tell them apart in the picker.
[[nodiscard]] std::optional<std::string> duplicateToneName(
    const std::vector<common::core::Tone>& catalog)
{
    std::vector<std::string_view> names;
    names.reserve(catalog.size());
    for (const common::core::Tone& tone : catalog)
    {
        names.push_back(tone.name);
    }
    std::ranges::sort(names);
    const auto duplicate = std::ranges::adjacent_find(names);
    if (duplicate == names.end())
    {
        return std::nullopt;
    }
    return std::string{*duplicate};
}

} // namespace

// Names the authored region whose span contains a timeline position, for cursor-follow
// selection. Spans resolve through the one region-span rule (toneRegionSpanSeconds — sub-beat
// exact, baseline lead-in), so cursor-follow can never disagree with the drawn tone row or the
// automation editable window about where a region begins.
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
        // The tone schedule is gapless and spans the whole chart, and the last region owns
        // everything after its start (through the end of chart time), so every cursor position
        // resolves to exactly one region.
        const common::core::TimeRange span =
            toneRegionSpanSeconds(tempo_map, arrangement->tone_track, index);
        const bool is_last = index + 1 == regions.size();
        const double end = is_last ? std::numeric_limits<double>::infinity() : span.end.seconds;
        if (position.seconds >= span.start.seconds && position.seconds < end)
        {
            return regions[index].id;
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
std::string EditorController::Impl::toneNameForRef(const std::string& tone_document_ref) const
{
    const common::core::Arrangement* const arrangement = session().currentArrangement();
    if (arrangement == nullptr || tone_document_ref.empty())
    {
        return {};
    }
    return common::core::toneNameFor(*arrangement, tone_document_ref);
}

std::string EditorController::Impl::activeToneName() const
{
    return toneNameForRef(activeToneDocumentRef());
}

// Formally selects a region (a deliberate click): the selection is the Delete target and draws a
// white outline, and it becomes the active tone (a preview) until the cursor moves off it.
void EditorController::Impl::applyToneSelection(std::string region_id)
{
    if (!region_id.empty())
    {
        selectMarker(ToneRegionSelection{.region_id = std::move(region_id)});
        return;
    }
    // A region deselect only releases the region alternative; it must not disturb a chart or
    // automation selection made since (one selection editor-wide, per-kind lifecycles).
    if (std::holds_alternative<ToneRegionSelection>(m_selection))
    {
        setSelection(std::monostate{});
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

void EditorController::Impl::onToneRegionToneRequested(
    std::string region_id, std::string tone_document_ref)
{
    runAction(
        EditorAction::SetToneRegionTone{
            std::move(region_id), EditorAction::ExistingTone{std::move(tone_document_ref)}
        });
}

// The same retone, pointed at a tone that does not exist yet. One action, so minting the tone and
// repointing the region are one undo entry rather than two.
void EditorController::Impl::onToneRegionNewToneRequested(std::string region_id, std::string name)
{
    runAction(
        EditorAction::SetToneRegionTone{
            std::move(region_id), EditorAction::NewTone{std::move(name)}
        });
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

// Opens a session-scoped open lane; nothing is authored, so this is a direct view-state
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
        .param_id = param_id,
    };
    if (std::ranges::find(m_open_automation_lanes, open_lane) == m_open_automation_lanes.end())
    {
        m_open_automation_lanes.push_back(open_lane);
    }
    // While the "+" row holds the selection (Enter on it, or a click on its chip while it is
    // selected), the new lane is where the keyboard goes next: the caret arms on it at the cursor,
    // as walking onto any lane would. Opened under any other selection, the lane moves nothing.
    if (std::holds_alternative<AddAutomationLaneRowSelection>(m_selection))
    {
        const FocusRow lane =
            AutomationLaneRow{.instance_id = instance_id, .param_id = std::move(param_id)};
        landOnRow(lane, std::nullopt);
    }
    updateView();
}

// Closes a session-scoped open lane. Authored lanes are unaffected: their removal is an undoable
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

void EditorController::Impl::onToneAutomationPointsEditRequested(
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

// Commits the tone model as it now stands as one undo entry and republishes it. Every tone verb
// ends here, so the invariants live here once: the catalog holds exactly the tones regions
// reference (unreferenced entries are pruned, a reference to nothing is refused), tone names are
// unique, and the track satisfies the structural rules persistence enforces. A refused commit
// restores the model whole, so a verb that failed leaves no trace; a verb that changed nothing
// records nothing. Returns whether an entry was pushed.
bool EditorController::Impl::commitToneModel(ToneModelSnapshot before, std::string label)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return false;
    }

    const auto restore = [&] {
        *catalog = before.tones;
        *tone_track = before.tone_track;
    };
    const auto refuse = [&](std::string_view detail) {
        restore();
        RH_LOG_WARNING("editor.tone", "Rejected tone edit label={:?} detail={:?}", label, detail);
        updateView();
        return false;
    };

    pruneUnreferencedTones(*tone_track, *catalog);

    if (const auto valid =
            common::core::validateToneTrackRules(*tone_track, session().song().tempo_map);
        !valid.has_value())
    {
        return refuse(valid.error().message);
    }
    for (const common::core::ToneRegion& region : tone_track->regions)
    {
        if (std::ranges::none_of(*catalog, [&region](const common::core::Tone& tone) {
                return tone.tone_document_ref == region.tone_document_ref;
            }))
        {
            return refuse("tone is not in the arrangement catalog");
        }
    }
    if (const std::optional<std::string> duplicate = duplicateToneName(*catalog);
        duplicate.has_value())
    {
        restore();
        reportError("A tone named \"" + *duplicate + "\" already exists in this arrangement.");
        return false;
    }

    ToneModelSnapshot after{.tones = *catalog, .tone_track = *tone_track};
    if (after == before)
    {
        updateView();
        return false;
    }

    pushUndoEntry(
        std::make_unique<ToneModelEdit>(std::move(before), std::move(after), std::move(label)));
    // The audible tone follows the active region, and any verb here can change which region that
    // is or what it references.
    releaseMarkerSelectionNamingNothing();
    syncAudibleTone();
    updateView();
    return true;
}

// Splits the region under the marker into a new tone-change region referencing an existing
// catalog tone; minting a fresh tone is CreateNewTone's job. Like every insert it leaves what it
// made SELECTED: the region holding the inserted start, which is the new one even when the next
// region's tone was pulled back into it.
void EditorController::Impl::performActionImpl(const EditorAction::CreateToneRegion& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
    if (const auto created = common::core::createToneRegion(
            *tone_track, action.position, action.new_region_id, action.tone_document_ref);
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

    const std::string tone_name = toneNameForRef(action.tone_document_ref);
    if (commitToneModel(
            std::move(before),
            "Insert " + (tone_name.empty() ? std::string{"Tone Change"} : tone_name)))
    {
        applyToneSelection(common::core::toneRegionAt(*tone_track, action.position)->id);
        updateView();
    }
}

// Deletes a tone region: the previous region runs on over its span, and merges with the next one
// when the two share a tone. Deleting the ONLY region cannot merge — the track must cover the
// whole song — so that case resets instead, stated below as a retone onto a fresh empty tone.
// Either way nothing stays selected.
void EditorController::Impl::performActionImpl(const EditorAction::DeleteToneRegion& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    const common::core::ToneRegion* const region = findToneRegion(tone_track, action.region_id);
    if (region == nullptr)
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Ignored delete for unknown tone region region_id={:?}",
            action.region_id);
        return;
    }

    if (tone_track->regions.size() == 1)
    {
        // Deleting the only region is a reset, not a merge: the track must cover the whole song, so
        // there is nowhere to merge into. Resetting IS repointing that region at a fresh empty tone
        // named "Default", which the retone already does — including pruning the tone it replaces —
        // so this states the reset in terms of that one authority instead of keeping a second
        // mint-and-repoint path beside it. Nothing stays selected, as with every delete.
        applyToneSelection({});
        runAction(
            EditorAction::SetToneRegionTone{
                action.region_id, EditorAction::NewTone{std::string{"Default"}}
            });
        return;
    }

    const std::string tone_name = toneNameForRef(region->tone_document_ref);
    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
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

    // Delete leaves NOTHING selected: the commit releases a selection naming a region that is gone.
    // The absorbing neighbour used to inherit it, on the reasoning that the signal-chain panel had
    // to stay bound to a region — but the panel follows the ACTIVE tone, which tracks the cursor,
    // while "selected" is only the Delete target and its outline. Inheriting it just armed Delete
    // at a region the charter never pointed at.
    commitToneModel(
        std::move(before),
        "Delete " + (tone_name.empty() ? std::string{"Tone Region"} : tone_name));
}

// Renames a catalog tone. Region labels derive from the catalog, so every region referencing the
// tone relabels together on the next view refresh; a name that changes nothing pushes nothing.
void EditorController::Impl::performActionImpl(const EditorAction::RenameTone& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    const auto tone = std::ranges::find(
        *catalog, action.tone_document_ref, &common::core::Tone::tone_document_ref);
    if (tone == catalog->end())
    {
        return;
    }

    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
    const std::string label =
        "Rename Tone " + (tone->name.empty() ? std::string{"<unknown>"} : tone->name) + " to " +
        (action.name.empty() ? std::string{"<unknown>"} : action.name);
    tone->name = action.name;
    commitToneModel(std::move(before), label);
}

// Points one tone region at a tone. The target is either a tone already in the catalog or one to
// MINT, which is why this is the only retone: the two differ solely in whether the tone exists
// yet, and both are one change to one region and so one undo entry. A region that comes to share
// a neighbour's tone merges with it, and the selection follows the region that survives, so the
// next verb acts on what the charter just made.
void EditorController::Impl::performActionImpl(const EditorAction::SetToneRegionTone& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    const common::core::ToneRegion* const region = findToneRegion(tone_track, action.region_id);
    if (region == nullptr)
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone region retone region={:?} detail={:?}",
            action.region_id,
            std::string_view{"unknown region"});
        updateView();
        return;
    }
    const common::core::GridPosition start = region->start;
    const bool was_selected = selectedToneRegionId() == action.region_id;

    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
    std::string after_ref;
    std::optional<common::core::Tone> minted_tone;
    if (const auto* const existing = std::get_if<EditorAction::ExistingTone>(&action.target))
    {
        after_ref = existing->tone_document_ref;
    }
    else
    {
        auto minted = m_live_rig.mintEmptyTone(currentSongDirectory());
        if (!minted.has_value())
        {
            reportError(std::string{"Could not create a new tone: "} + minted.error().message);
            return;
        }
        after_ref = std::move(*minted);
        minted_tone = common::core::Tone{
            .tone_document_ref = after_ref,
            .name = std::get<EditorAction::NewTone>(action.target).name
        };
    }

    if (const auto retoned =
            common::core::retoneToneRegion(*tone_track, action.region_id, after_ref);
        !retoned.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone region retone region={:?} detail={:?}",
            action.region_id,
            retoned.error().message);
        updateView();
        return;
    }
    if (minted_tone.has_value())
    {
        catalog->push_back(*minted_tone);
    }

    const std::string after_name = toneNameForRef(after_ref);
    if (!commitToneModel(
            std::move(before),
            "Change Tone of Region to " + (after_name.empty() ? std::string{"tone"} : after_name)))
    {
        return;
    }

    // The region the charter pointed at may have merged into its predecessor; the region now
    // holding its start is the one the retone produced either way.
    if (was_selected)
    {
        applyToneSelection(common::core::toneRegionAt(*tone_track, start)->id);
        updateView();
    }

    // An existing tone already has its branch; a freshly minted one needs one.
    if (minted_tone.has_value() && !activateEmptyToneBranch(after_ref))
    {
        reloadLiveRigForToneSet();
    }
}

// Moves the boundary a region opens. Grid positions are audio-inert: the rig switches tones by
// document ref, so moving a boundary never touches the audio graph.
void EditorController::Impl::performActionImpl(const EditorAction::MoveToneBoundary& action)
{
    common::core::ToneTrack* const tone_track = m_session.currentToneTrack();
    std::vector<common::core::Tone>* const catalog = m_session.currentToneCatalog();
    if (tone_track == nullptr || catalog == nullptr)
    {
        return;
    }

    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
    if (const auto moved =
            common::core::moveToneBoundary(*tone_track, action.right_region_id, action.position);
        !moved.has_value())
    {
        RH_LOG_WARNING(
            "editor.tone",
            "Rejected tone boundary move region_id={:?} detail={:?}",
            action.right_region_id,
            moved.error().message);
        updateView();
        return;
    }
    commitToneModel(std::move(before), "Move Tone Boundary");
}

// Creates a new empty tone: mints its document, splits the region under the marker to reference it,
// commits catalog tone and region as one entry, gives the tone a rig branch, and — like every
// insert — leaves the new region SELECTED. The selection comes after the branch, so the rig can
// switch to the tone the moment it is selected; on the full-reload fallback the reload's own
// completion points the rig at it. The document is minted first because loadLiveRig fails on a
// missing file; a refused split or commit leaves the file as an orphan, kept and collected at
// publish like every removed tone.
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

    auto minted = m_live_rig.mintEmptyTone(currentSongDirectory());
    if (!minted.has_value())
    {
        reportError(std::string{"Could not create a new tone: "} + minted.error().message);
        return;
    }
    const std::string new_tone_document_ref = std::move(*minted);
    const std::string new_region_id = common::core::generatePackageId();

    ToneModelSnapshot before{.tones = *catalog, .tone_track = *tone_track};
    if (const auto created = common::core::createToneRegion(
            *tone_track, action.position, new_region_id, new_tone_document_ref);
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
    catalog->push_back(
        common::core::Tone{.tone_document_ref = new_tone_document_ref, .name = action.name});

    if (!commitToneModel(
            std::move(before), "Add " + (action.name.empty() ? std::string{"Tone"} : action.name)))
    {
        return;
    }
    if (!activateEmptyToneBranch(new_tone_document_ref))
    {
        reloadLiveRigForToneSet();
    }
    applyToneSelection(common::core::toneRegionAt(*tone_track, action.position)->id);
    updateView();
}

// Fast path for a freshly minted EMPTY tone: appends a passthrough branch to the live rig
// (Tracktion reuses every existing plugin instance across the coalesced graph rebuild, so nothing
// is torn down and playback never stops), then points the rig at the active tone, which the commit
// could not do while the branch was missing. No capture is needed because nothing on disk is
// replaced, and no identities merge because an empty branch has no plugins. Returns false when no
// rig is loaded or the add fails; the caller falls back to a full reload.
bool EditorController::Impl::activateEmptyToneBranch(const std::string& tone_document_ref)
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

    syncAudibleTone();
    updateView();
    return true;
}

// Reloads the live rig from the current model so a newly referenced tone gains its own branch, then
// points the rig at the active tone. Runs behind the loading busy overlay like the
// arrangement-switch load. The selection is never the reload's business: no verb that reloads
// selects what it made, and the asynchronous completion below lands long after the verb returned.
// Undo/redo intentionally skip this: the model is the source of truth and the rig re-derives on the
// next full load, so a branch left behind by an undone create is harmless.
void EditorController::Impl::reloadLiveRigForToneSet()
{
    if (!m_project.has_value() || !m_project_audio_ready)
    {
        // No live rig to reload yet; the model already holds the tone.
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
        updateView();
        return;
    }

    const std::uint64_t token = beginBusy(BusyOperation::LoadingLiveRig);
    updateView();
    runLiveRigLoadStage(
        ProjectLoadLiveRigStage{
            .token = token,
            .song_directory = currentSongDirectory(),
            .finish = [this](std::expected<void, common::audio::LiveRigError> rig_result) {
                if (!rig_result.has_value())
                {
                    finishBusyOperation();
                    reportError(
                        std::string{"Could not load the new tone: "} + rig_result.error().message);
                    updateView();
                    return;
                }
                syncAudibleTone();
                finishBusyOperation();
                updateView();
            },
        });
}

// The one port lookup of a parameter's metadata by (instance, param): every consumer of
// AutomatableParamInfo — undo labels, landing-value shape, value stepping — resolves through
// this so the identity resolution can never drift between them.
std::optional<common::audio::AutomatableParamInfo> EditorController::Impl::paramInfoFor(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id) const
{
    auto parameters = m_tone_automation.listAutomatableParameters(tone_document_ref);
    if (!parameters.has_value())
    {
        return std::nullopt;
    }
    for (common::audio::AutomatableParamInfo& parameter : *parameters)
    {
        if (parameter.instance_id == instance_id && parameter.param_id == param_id)
        {
            return std::move(parameter);
        }
    }
    return std::nullopt;
}

// Looks up a tone-chain parameter's user-facing name for the undo label, falling back to its id.
std::string EditorController::Impl::automationParameterName(
    const std::string& tone_document_ref, const std::string& instance_id,
    const std::string& param_id) const
{
    std::optional<common::audio::AutomatableParamInfo> parameter =
        paramInfoFor(tone_document_ref, instance_id, param_id);
    if (parameter.has_value())
    {
        return std::move(parameter->name);
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

// A time-addressed lane caret arm (the row-axis form of the chart lane's empty click, §9b): the
// caret arms at the grid slot nearest the given time. This is the time-input entry point (exercised
// by tests); the pixel-input click path arms through the one placement snap in
// onToneAutomationPointerDown. Both converge on seekAndArmLaneCaret, so a lane caret always rests
// on one slot however it was addressed.
void EditorController::Impl::onToneAutomationLaneCaretRequested(
    std::string instance_id, std::string param_id, common::core::TimePosition time)
{
    const common::core::GridPosition position = nearestTempoGridPosition(
        session().song().tempo_map, placementQuantum(), session().timeline().clamp(time));
    seekAndArmLaneCaret(
        position,
        AutomationLaneRow{.instance_id = std::move(instance_id), .param_id = std::move(param_id)});
}

// Seeks the transport to a resolved lane slot and arms the caret there — the seek-and-arm tail the
// lane click (onToneAutomationPointerDown's plain-empty branch, snapping the pixel through
// laneSnapPositionForX) and the time-addressed caret request share, so the transport rests on the
// caret slot and play-from-here IS that slot. The seek re-points the audible tone at the cursor;
// arming then owns the selection state, exactly like the chart lane's empty click (seek + caret +
// selection re-derivation). While playing the click only seeks (armed implies paused).
void EditorController::Impl::seekAndArmLaneCaret(
    common::core::GridPosition position, AutomationLaneRow row)
{
    if (isBusy())
    {
        return;
    }
    // The seek routes through the one SeekTimeline action, so a lane-caret seek gets the same
    // gating, clamping, marker demotion, and tone-follow every other seek gets (arming below
    // immediately supersedes the demotion while paused).
    runAction(
        EditorAction::SeekTimeline{common::core::TimePosition{secondsAtGridPosition(
            session().song().tempo_map, position)}});
    if (!m_transport.state().playing)
    {
        armLaneCaret(position, std::move(row));
        updateView();
    }
}

// A button-less lane hover: resolve the Alt insert ghost and publish it only where an Alt+click
// would actually land — Alt held, not busy, and paused (armed-create is a paused-only gesture).
// The event carries the raw lane-local pixel x plus the geometry it was mapped against, and the
// snap runs through laneSnapPositionForX — the exact placement seam an Alt+click and the drag
// commit share (timelinePositionForX ÷ (width - 1) then the placement quantum's lattice),
// so the ring lands on the identical slot the click would with no sub-pixel drift. The occupancy
// gate that keeps the ring honest lives at publish time (deriveViewState, against the published
// lanes): now that mouse placement refuses an occupied slot (onToneAutomationPointerDown's Alt
// branch), the ring is hidden there too so it never previews an insert that would no-op (§7).
// Dirty-checked against the current ghost: a hover that stays within one grid slot leaves it
// unchanged and pushes no view rebuild.
void EditorController::Impl::onToneAutomationPointerMove(const ToneAutomationPointerEvent& event)
{
    std::optional<ToneInsertGhost> ghost;
    if (event.modifiers.alt && !isBusy() && !m_transport.state().playing)
    {
        if (const std::optional<common::core::GridPosition> position = laneSnapPositionForX(
                session().song().tempo_map,
                placementQuantum(),
                event.geometry.visible_timeline,
                event.geometry.content_width,
                event.x);
            position.has_value())
        {
            ghost = ToneInsertGhost{
                .instance_id = event.instance_id,
                .param_id = event.param_id,
                .position = *position,
            };
        }
    }
    if (ghost == m_tone_insert_ghost)
    {
        return;
    }
    m_tone_insert_ghost = std::move(ghost);
    updateView();
}

// The pointer left the lane row: no hover, so no ghost. Refresh only when one was actually showing.
void EditorController::Impl::onToneAutomationPointerExit()
{
    if (!m_tone_insert_ghost.has_value())
    {
        return;
    }
    m_tone_insert_ghost.reset();
    updateView();
}

// A primary-button press inside a lane, re-resolving the point-vs-anchor-vs-empty-area hit the view
// forwarded (the view already peeled off the zones it owns outright — name chips, resize bands, the
// "+" picker, and right-clicks — so a forwarded press is a point handle, the lane's derived anchor,
// or empty editable area, the last already gated inside the window). The controller owns the
// gesture from here: a point grab begins a move drag (a click that never moves selects on release);
// an anchor press begins an insert drag at the lane start that stays a click until the drag
// threshold, so a bare click there authors nothing; Alt on empty area begins an on-curve insert
// that authors from the press, refused on an occupied slot; plain empty area arms the lane caret.
// A double-click's second press belongs to the view's value editor, so it never arms a stray drag
// here.
void EditorController::Impl::onToneAutomationPointerDown(const ToneAutomationPointerEvent& event)
{
    if (isBusy() || event.clicks >= 2 || event.lane_index >= event.lane_extents.size())
    {
        return;
    }
    const ToneAutomationLaneExtent& extent = event.lane_extents[event.lane_index];

    // A press ends any Alt-hover preview: a live gesture owns the lane now and its own preview
    // drives every later repaint. Refresh only when a ghost was actually showing.
    const bool had_insert_ghost = m_tone_insert_ghost.has_value();
    m_tone_insert_ghost.reset();
    const auto refresh_dismissed_ghost = [&] {
        if (had_insert_ghost)
        {
            updateView();
        }
    };

    // Re-resolve the point-vs-empty-area hit against the lane's points with the same forgiving
    // radius and ÷width geometry the view's hitAt filtered on, so the two never disagree: a hit
    // grabs that point, a miss is empty editable area.
    const common::core::TempoMap& tempo_map = session().song().tempo_map;
    const std::vector<common::core::ToneAutomationPoint>* const points =
        lanePointsFor(event.instance_id, event.param_id);
    std::optional<std::size_t> grabbed;
    if (points != nullptr)
    {
        for (std::size_t index = 0; index < points->size(); ++index)
        {
            const std::optional<float> point_x = laneXForSeconds(
                secondsAtGridPosition(tempo_map, (*points)[index].position),
                event.geometry.visible_timeline,
                event.geometry.content_width);
            if (!point_x.has_value())
            {
                continue;
            }
            const float dx = event.x - *point_x;
            const float dy = event.y - laneValueBandY((*points)[index].norm_value, extent);
            if (((dx * dx) + (dy * dy)) <=
                (g_tone_lane_handle_grab_radius * g_tone_lane_handle_grab_radius))
            {
                grabbed = index;
                break;
            }
        }
    }

    if (grabbed.has_value())
    {
        // A point grab begins a move drag but stays a click until the pointer crosses the drag
        // threshold, so a plain click selects the point without an
        // accidental move (resolved on Up).
        const common::core::ToneAutomationPoint& point = (*points)[*grabbed];
        m_tone_automation_drag = ToneAutomationDrag{
            .instance_id = event.instance_id,
            .param_id = event.param_id,
            .points = *points,
            .point_index = *grabbed,
            .visible_timeline = event.geometry.visible_timeline,
            .content_width = event.geometry.content_width,
            .value_band = extent,
            .preview_position = point.position,
            .preview_value = point.norm_value,
            .start_position = point.position,
            .start_value = point.norm_value,
            .press_x = event.x,
            .press_y = event.y,
            .is_discrete = event.lane_is_discrete,
            .discrete_value_count = event.lane_discrete_value_count,
            .moved = false,
            .origin = ToneLaneDragOrigin::PointHandle,
        };
        refresh_dismissed_ghost();
        return;
    }

    // The anchor is the lane's derived start value, drawn read-only at the timeline origin. It is
    // a handle like a point handle, not empty area — the one place the Alt-authors law does not
    // reach, so no Alt is needed here — and like a point handle it stays a CLICK until the pointer
    // crosses the drag threshold. Crossing it authors a real point at the lane start carrying the
    // anchor's own value, which the drag then pulls, because wanting a different start value is
    // authored data, not a change to what the tone state says. A press that never crosses authors
    // nothing at all and falls through on release to the plain lane-area click (see the Up
    // handler): a point that only restates the tone state's own value is still an edit the user
    // did not ask for, and the lane already says that value without it. The plan runs through the
    // same creation seam every other placement uses, whose on-curve landing at the lane start IS
    // the anchor's own value, so the anchor adds no second creation rule. The parameter lookup is
    // a port call, so the cheap column test prunes it first; a press that misses (or a plan that
    // refuses) falls through to the empty-area verbs below.
    if (const std::optional<float> anchor_x = laneXForSeconds(
            toneAutomationAnchorSeconds(),
            event.geometry.visible_timeline,
            event.geometry.content_width);
        anchor_x.has_value() && std::abs(event.x - *anchor_x) <= g_tone_lane_handle_grab_radius)
    {
        if (const std::optional<common::audio::AutomatableParamInfo> parameter =
                paramInfoFor(activeToneDocumentRef(), event.instance_id, event.param_id);
            parameter.has_value())
        {
            const float dx = event.x - *anchor_x;
            const float dy = event.y - laneValueBandY(parameter->baseline_norm_value, extent);
            if (((dx * dx) + (dy * dy)) <=
                    (g_tone_lane_handle_grab_radius * g_tone_lane_handle_grab_radius) &&
                beginLanePointInsertDrag(
                    event, extent, toneAutomationAnchorPosition(), ToneLaneDragOrigin::Anchor))
            {
                return;
            }
        }
    }

    // Empty editable lane area. Both the plain caret arm and the Alt on-curve insert snap the pixel
    // through the one placement seam — laneSnapPositionForX (timelinePositionForX ÷ (width - 1)
    // then the placement quantum's lattice) — mirroring the chart's single chartPlacementAt
    // consumed by both its caret arm and its Alt insert. So the caret lands on the identical slot
    // an Alt+click or the insert ghost would at the same pixel, erasing the ÷width slot-boundary
    // drift the shipped view armed the caret with. A degenerate geometry that maps no slot only
    // refreshes the dismissed ghost.
    const std::optional<common::core::GridPosition> position = laneSnapPositionForX(
        tempo_map,
        placementQuantum(),
        event.geometry.visible_timeline,
        event.geometry.content_width,
        event.x);
    if (!position.has_value())
    {
        refresh_dismissed_ghost();
        return;
    }

    // Without Alt, arm the lane caret at the slot — the row-axis empty click (§9b): seek there and
    // arm (paused), re-deriving the selection from what sits under the caret.
    if (!event.modifiers.alt)
    {
        seekAndArmLaneCaret(
            *position,
            AutomationLaneRow{.instance_id = event.instance_id, .param_id = event.param_id});
        return;
    }

    // Alt on empty area begins an on-curve insert placement — the same neutral-create plan the
    // keyboard Insert runs (occupied slot, window edge, and unresolved parameter all refuse), so
    // mouse placement can never plant a duplicate. The point lands ON the curve (silent until
    // pulled) and the drag phase pulls the value by the pointer's delta.
    if (!beginLanePointInsertDrag(event, extent, *position, ToneLaneDragOrigin::EmptyArea))
    {
        refresh_dismissed_ghost();
    }
}

bool EditorController::Impl::beginLanePointInsertDrag(
    const ToneAutomationPointerEvent& event, const ToneAutomationLaneExtent& extent,
    common::core::GridPosition position, ToneLaneDragOrigin origin)
{
    std::optional<LanePointPlan> plan = planLanePointAtCaret(
        ChartCaret{
            .position = position,
            .lane = AutomationLaneRow{.instance_id = event.instance_id, .param_id = event.param_id},
        });
    if (!plan.has_value())
    {
        return false;
    }
    std::size_t insert_index = 0;
    while (insert_index < plan->points.size() && plan->points[insert_index].position < position)
    {
        ++insert_index;
    }
    // The on-curve landing snaps to a discrete parameter's states, matching the view's
    // curveValueAt: a continuous value passes through unchanged, but a point on a discrete lane
    // lands on a real state rather than a raw interpolated one.
    const float landing_value =
        snappedLaneValue(plan->value, event.lane_is_discrete, event.lane_discrete_value_count);
    m_tone_automation_drag = ToneAutomationDrag{
        .instance_id = event.instance_id,
        .param_id = event.param_id,
        .points = std::move(plan->points),
        .point_index = insert_index,
        .visible_timeline = event.geometry.visible_timeline,
        .content_width = event.geometry.content_width,
        .value_band = extent,
        .preview_position = position,
        .preview_value = landing_value,
        .start_position = position,
        .start_value = landing_value,
        .press_x = event.x,
        .press_y = event.y,
        .is_discrete = event.lane_is_discrete,
        .discrete_value_count = event.lane_discrete_value_count,
        .moved = false,
        .origin = origin,
    };
    // One refresh for both effects a press has on the lane's overlays: the Alt insert's on-curve
    // preview point publishes from the press (that gesture has its edit in hand at once), and any
    // Alt-hover ghost the press dismissed disappears with it. An anchor press publishes no preview
    // until its drag begins — the anchor's mark simply stays as it is under a click that authors
    // nothing.
    updateView();
    return true;
}

// Advances the in-flight move/insert drag preview, ported verbatim from the lanes view's mouseDrag:
// snap the position through the placement seam, neighbor-clamp it so the committed list stays
// strictly ascending, clamp x inside the editable window, and pull the value by the pointer's
// vertical delta from the press (Shift locks the dominant axis). Everything read here is frozen at
// Down (the lane's points, the geometry, the value band, the press point), so a mid-drag engine
// rebuild republishes this preview rather than resetting the edit.
void EditorController::Impl::onToneAutomationPointerDrag(const ToneAutomationPointerEvent& event)
{
    if (!m_tone_automation_drag.has_value())
    {
        return;
    }
    ToneAutomationDrag& drag = *m_tone_automation_drag;

    // A gesture that arrived with no edit in hand — a grabbed existing point, or a press on the
    // lane's anchor — stays a click until the pointer crosses the framework's click→drag threshold,
    // so the micro-jiggle inside a click can never commit an accidental move or author a stray
    // point. The Alt insert authored on its press and moves with the pointer from there. The signal
    // is JUCE's own mouseWasDraggedSinceMouseDown, carried on the event, so the timing component (a
    // long press) is honored exactly as the shipped view did.
    if (!drag.hasLiveEdit() && !event.dragged_since_down)
    {
        return;
    }

    // Shift constrains the drag to its dominant axis, anchored at the gesture start: a horizontal
    // move keeps the starting value and a vertical move keeps the starting position.
    const bool horizontal_dominant =
        std::abs(event.x - drag.press_x) >= std::abs(event.y - drag.press_y);
    const bool lock_value = event.modifiers.shift && horizontal_dominant;
    const bool lock_position = event.modifiers.shift && !horizontal_dominant;

    // Clamp x inside the editable window (÷width, the view's own xForSeconds), then snap and clamp
    // musically between the temporal neighbors so the committed list stays strictly ascending.
    const common::core::TimeRange window = activeToneRegionWindow();
    const std::optional<float> window_start =
        laneXForSeconds(window.start.seconds, drag.visible_timeline, drag.content_width);
    const std::optional<float> window_end =
        laneXForSeconds(window.end.seconds, drag.visible_timeline, drag.content_width);
    float clamped_x = event.x;
    if (window_start.has_value() && window_end.has_value())
    {
        clamped_x =
            std::clamp(clamped_x, *window_start, std::max(*window_start, *window_end - 1.0F));
    }

    if (lock_position)
    {
        drag.preview_position = drag.start_position;
    }
    else if (
        const std::optional<common::core::GridPosition> position = laneSnapPositionForX(
            session().song().tempo_map,
            placementQuantum(),
            drag.visible_timeline,
            drag.content_width,
            clamped_x);
        position.has_value()
    )
    {
        bool blocked = false;
        // Neighbor indices skip the dragged point itself for existing points; a new point's
        // insertion index already partitions the neighbors.
        if (drag.point_index > 0)
        {
            const std::size_t previous_index = drag.point_index - 1;
            if (previous_index < drag.points.size() &&
                !(drag.points[previous_index].position < *position))
            {
                blocked = true;
            }
        }
        const std::size_t next_index =
            drag.createsPoint() ? drag.point_index : drag.point_index + 1;
        if (next_index < drag.points.size() && !(*position < drag.points[next_index].position))
        {
            blocked = true;
        }
        if (!blocked)
        {
            drag.preview_position = *position;
        }
    }

    if (lock_value)
    {
        drag.preview_value = drag.start_value;
    }
    else
    {
        // Delta-based: the value moves by the pointer's vertical travel from the press, so an
        // on-curve insert landing (or an off-center point grab) never jumps to the raw pointer y.
        const float delta =
            laneValueForY(event.y, drag.value_band) - laneValueForY(drag.press_y, drag.value_band);
        drag.preview_value = snappedLaneValue(
            std::clamp(drag.start_value + delta, 0.0F, 1.0F),
            drag.is_discrete,
            drag.discrete_value_count);
    }
    drag.moved = true;
    updateView();
}

// Ends the in-flight move/insert drag, ported from the lanes view's mouseUp: a gesture holding a
// live edit commits its replacement list (one undoable edit) and selects the landed point, and a
// press that never produced one runs the click verb of whatever it grabbed. Clearing the gesture
// before the commit lets its state push apply immediately rather than rebuilding against a stale
// preview.
void EditorController::Impl::onToneAutomationPointerUp(const ToneAutomationPointerEvent& /*event*/)
{
    if (!m_tone_automation_drag.has_value())
    {
        return;
    }
    const ToneAutomationDrag drag = *m_tone_automation_drag;
    m_tone_automation_drag.reset();
    // A release ends any hover preview too, matching the tab lane's release.
    m_tone_insert_ghost.reset();

    if (drag.hasLiveEdit())
    {
        // The commit runs synchronously and pushes fresh state; with the gesture already cleared
        // that push applies immediately rather than deferring. The follow-up selection arms the
        // caret on the landed point (paused) exactly as the shipped view's release did.
        onToneAutomationPointsEditRequested(
            drag.instance_id, drag.param_id, toneAutomationDragCommitPoints(drag));
        onToneAutomationPointSelectRequested(
            drag.instance_id, drag.param_id, drag.preview_position);
        return;
    }

    // No live edit: the press never crossed the drag threshold, so the release runs the click verb
    // of what was grabbed.
    if (drag.origin == ToneLaneDragOrigin::Anchor)
    {
        // An anchor click authors nothing (see the Down handler) and falls through to the plain
        // lane-area click at the pressed pixel — §9b's seek and caret arm. The slot is re-derived
        // through the one placement seam from the geometry frozen at Down, so it is the identical
        // slot a plain click at that pixel would have armed. A degenerate geometry maps no slot,
        // and the click simply does nothing.
        if (const std::optional<common::core::GridPosition> position = laneSnapPositionForX(
                session().song().tempo_map,
                placementQuantum(),
                drag.visible_timeline,
                drag.content_width,
                drag.press_x);
            position.has_value())
        {
            seekAndArmLaneCaret(
                *position,
                AutomationLaneRow{.instance_id = drag.instance_id, .param_id = drag.param_id});
        }
        return;
    }

    // A plain click on a point (no move) selects it — the row-axis point select (§9b).
    onToneAutomationPointSelectRequested(
        drag.instance_id, drag.param_id, drag.points[drag.point_index].position);
}

// Builds the replacement point list an active move/insert drag commits: every frozen point echoed
// bit-identically except the moved one, with the preview point inserted in sorted order — the
// controller-owned sibling of the lanes view's pointsForCommit.
std::vector<common::core::ToneAutomationPoint> EditorController::Impl::
    toneAutomationDragCommitPoints(const ToneAutomationDrag& drag) const
{
    std::vector<common::core::ToneAutomationPoint> points;
    points.reserve(drag.points.size() + 1);
    for (std::size_t index = 0; index < drag.points.size(); ++index)
    {
        if (!drag.createsPoint() && index == drag.point_index)
        {
            continue;
        }
        points.push_back(drag.points[index]);
    }
    const common::core::ToneAutomationPoint edited{
        .position = drag.preview_position,
        .norm_value = drag.preview_value,
    };
    const auto insert_at =
        std::ranges::find_if(points, [&edited](const common::core::ToneAutomationPoint& candidate) {
            return edited.position < candidate.position;
        });
    points.insert(insert_at, edited);
    return points;
}

// The Insert key's whole remaining create: an on-curve point at an armed LANE caret's slot — the
// keyboard mirror of the on-curve Alt+click landing — selected once planted. The chart lane has no
// share in this verb, because every object on it is TYPED: a digit states the note or the point,
// and a key with no value to carry has nothing to place there. A slot that already holds a point is
// a no-op (Insert never mutates existing objects), as are an unresolved parameter (no live line to
// land on) and a marker that is not armed.
void EditorController::Impl::performActionImpl(const EditorAction::InsertLanePoint&)
{
    const ChartCaret* const armed_caret = armedChartCaret();
    if (armed_caret == nullptr || !armed_caret->lane.has_value())
    {
        return;
    }
    // Copied so the planting (a full action dispatch that re-points the selection and may touch
    // the marker) never reads back through the marker variant it aliases.
    const ChartCaret caret = *armed_caret;
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
    if (lanePointAt(*caret.lane, caret.position))
    {
        return std::nullopt;
    }

    // Creation clamps inside the active region's window exactly as moves and drags do. Mouse
    // placement is already window-gated view-side, but the keyboard can arm a caret past the
    // window edge — without this refusal it could plant a point the move verb would thereafter
    // refuse to touch (creatable but immovable).
    const common::core::TimeRange window = activeToneRegionWindow();
    const double slot_seconds = secondsAtGridPosition(session().song().tempo_map, caret.position);
    if (slot_seconds < window.start.seconds || slot_seconds >= window.end.seconds)
    {
        return std::nullopt;
    }

    // The landing value comes from the drawn curve, which begins at the lane's derived anchor —
    // the parameter's pre-automation value from the tone state — so an unauthored lane lands on
    // the anchor's flat line. The value shape rides along so the evaluation holds steps exactly as
    // the lane draws them. A parameter that no longer resolves has neither an anchor nor a live
    // chain slot to write into, so creation refuses there instead of inventing a landing value.
    const std::optional<common::audio::AutomatableParamInfo> parameter =
        paramInfoFor(activeToneDocumentRef(), caret.lane->instance_id, caret.lane->param_id);
    if (!parameter.has_value())
    {
        return std::nullopt;
    }
    plan.is_discrete = parameter->is_discrete;
    plan.discrete_value_count = parameter->discrete_value_count;

    // Snap the on-curve landing to a real discrete state so every creation path (keyboard Insert,
    // mouse Alt-insert, create-and-nudge) plants a legal value on a stepped parameter; snapping is
    // a no-op for continuous parameters.
    plan.value = snappedLaneValue(
        toneAutomationCurveValueAt(
            plan.points,
            session().song().tempo_map,
            caret.position,
            plan.is_discrete,
            parameter->baseline_norm_value),
        plan.is_discrete,
        plan.discrete_value_count);
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
    onToneAutomationPointsEditRequested(row.instance_id, row.param_id, std::move(plan.points));
    setSelection(
        AutomationPointSelection{
            .instance_id = row.instance_id,
            .param_id = row.param_id,
            .position = position,
        });
    // Re-point the armed caret to the planted slot so a nudged create keeps the marker on its new
    // point — the marker rides created points as it does moved ones (moveSelectedAutomationPoint /
    // moveChartSelection). plantLanePoint only runs from a lane-armed caret, so the marker holds a
    // caret; preserving its string and lane, only the position moves.
    if (auto* const caret = std::get_if<ChartCaret>(&m_chart_marker))
    {
        caret->position = position;
    }
    updateView();
}

// The move-intent fallback on an armed empty lane slot (§9b): the point lands ON the curve at
// the caret and the arrow's step is baked into the creation, so "grab the curve here and pull"
// is one keystroke and ONE undo entry. A caret over an existing point always publishes it as
// the selection (arming re-derives), so reaching here means the slot is empty.
void EditorController::Impl::createAndNudgeLanePointAtCaret(
    const ChartCaret& caret, ChartStepDirection direction)
{
    if (!caret.lane.has_value())
    {
        return;
    }
    std::optional<LanePointPlan> plan = planLanePointAtCaret(caret);
    if (!plan.has_value())
    {
        return;
    }
    float value = plan->value;
    common::core::GridPosition position = caret.position;
    if (direction == ChartStepDirection::Up || direction == ChartStepDirection::Down)
    {
        const float step = laneValueStep(plan->is_discrete, plan->discrete_value_count);
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
        const common::core::GridPosition stepped = steppedLaneNudgePosition(caret.position, later);
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

// One lane keyboard time-step, exact rational end to end: the adjacent line of the placement
// quantum's lattice through the shared adjacentTempoGridPosition primitive (the same walk the caret
// steps with), so the two surfaces can never land on different slots for the same motion.
common::core::GridPosition EditorController::Impl::steppedLaneNudgePosition(
    const common::core::GridPosition& from, bool later) const
{
    return adjacentTempoGridPosition(session().song().tempo_map, placementQuantum(), from, later);
}

// The active tone region's time window — the span automation edits clamp inside. Resolved
// through the one region-span rule (toneRegionSpanSeconds), so keyboard clamps, the pointer's
// editable window, and the drawn tone row always agree.
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
        if (regions[index].id == active_region_id)
        {
            return toneRegionSpanSeconds(tempo_map, arrangement->tone_track, index);
        }
    }
    return {};
}

// A deliberate point click: the point becomes THE editor-wide selection, replacing whatever any
// other surface had selected (one selection, structurally). Direct like the chart pointer
// intents rather than an action: selection is display policy, not an undoable edit. The caret
// arms at the clicked slot, so keyboard verbs continue from the object just touched; arming
// re-derives the selection from the slot, which is the clicked point. Armed implies paused, so
// while playing the click only selects.
void EditorController::Impl::onToneAutomationPointSelectRequested(
    std::string instance_id, std::string param_id, common::core::GridPosition position)
{
    if (isBusy())
    {
        return;
    }
    if (m_transport.state().playing)
    {
        setSelection(
            AutomationPointSelection{
                .instance_id = std::move(instance_id),
                .param_id = std::move(param_id),
                .position = position,
            });
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
    onToneAutomationPointsEditRequested(
        selection.instance_id, selection.param_id, std::move(remaining));
}

// Moves the selected automation point (the move-intent dispatch for the automation
// alternative), replaying its lane's points with the change through the one points-edit
// intent. Up/Down steps the value — one real state on a discrete parameter, else 0.01; Left/Right
// steps the time axis to the adjacent line of the placement quantum's lattice, refusing steps that
// collapse (map edge) or reverse direction (nearest-line bounce-back) and clamping strictly between
// the neighbors and inside the active region's window. Every refusal — stale selection included —
// is a silent no-op.
void EditorController::Impl::moveSelectedAutomationPoint(
    const AutomationPointSelection& selection, ChartStepDirection direction)
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
        if (const std::optional<common::audio::AutomatableParamInfo> parameter =
                paramInfoFor(activeToneDocumentRef(), selection.instance_id, selection.param_id);
            parameter.has_value())
        {
            is_discrete = parameter->is_discrete;
            discrete_value_count = parameter->discrete_value_count;
        }
        const float step = laneValueStep(is_discrete, discrete_value_count);
        const float raw = point->norm_value + (direction == ChartStepDirection::Up ? step : -step);
        const float new_value =
            snappedLaneValue(std::clamp(raw, 0.0F, 1.0F), is_discrete, discrete_value_count);
        // Exact inequality via is_neq keeps -Wfloat-equal builds clean; snapped-value
        // no-change detection is deliberately exact.
        if (std::is_neq(new_value <=> point->norm_value))
        {
            std::vector<common::core::ToneAutomationPoint> points = *lane_points;
            points[point_index].norm_value = new_value;
            onToneAutomationPointsEditRequested(
                selection.instance_id, selection.param_id, std::move(points));
            updateView();
        }
        return;
    }

    const bool later = direction == ChartStepDirection::Right;
    const common::core::GridPosition new_position =
        steppedLaneNudgePosition(selection.position, later);
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
    onToneAutomationPointsEditRequested(
        selection.instance_id, selection.param_id, std::move(points));
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
    setSelection(
        AutomationPointSelection{
            .instance_id = selection.instance_id,
            .param_id = selection.param_id,
            .position = new_position,
        });
    updateView();
}

} // namespace rock_hero::editor::core
