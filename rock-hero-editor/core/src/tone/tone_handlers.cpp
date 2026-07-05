#include "controller/editor_controller_impl.h"
#include "tone/tone_region_edits.h"

#include <algorithm>
#include <memory>
#include <rock_hero/common/core/shared/logger.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>
#include <utility>

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

void EditorController::Impl::onToneRegionSelected(std::string region_id)
{
    runAction(EditorAction::SelectToneRegion{std::move(region_id)});
}

void EditorController::Impl::onToneRegionResizeRequested(
    std::string region_id, common::core::ToneGridPosition start, common::core::ToneGridPosition end)
{
    runAction(EditorAction::ResizeToneRegion{std::move(region_id), start, end});
}

// Stores the selection when the id names an authored region; anything else clears it.
void EditorController::Impl::performActionImpl(EditorAction::SelectToneRegion action)
{
    const common::core::ToneRegion* const region =
        findToneRegion(m_session.currentToneTrack(), action.region_id);
    m_selected_tone_region_id = region != nullptr ? std::move(action.region_id) : std::string{};
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
    pushUndoEntry(
        std::make_unique<ToneRegionResizeEdit>(
            action.region_id, region->name, before_start, before_end, action.start, action.end));
    updateView();
}

} // namespace rock_hero::editor::core
