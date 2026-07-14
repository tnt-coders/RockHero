#include "engine_impl.h"

#include <algorithm>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::audio
{

// Bakes the switch schedule onto the loaded rig's branch-gain curves. One-time message-thread
// ValueTree work: curve edits under the rack state trigger ONE coalesced, lock-free graph
// rebuild (old graph keeps playing until the new one is ready; live plugins take the
// initialiseWithoutStopping path), which is why baking belongs in preparation, never mid-song.
std::expected<void, LiveRigError> Engine::prepareToneTimeline(
    const std::filesystem::path& /*song_directory*/,
    std::span<const common::core::ToneSwitchRegion> regions)
{
    if (!m_impl->m_tone_rack.has_value())
    {
        return std::unexpected{LiveRigError{
            LiveRigErrorCode::InvalidRequest,
            "Tone timeline requires a loaded live rig",
        }};
    }

    // Every scheduled tone must already be a rack branch: the rig preload is the single moment
    // plugins may instantiate, so an unknown reference here is a caller sequencing bug.
    for (const common::core::ToneSwitchRegion& region : regions)
    {
        const bool known = std::ranges::any_of(
            m_impl->m_tone_rack->branches, [&region](const ToneRackBranch& branch) {
                return branch.tone_document_ref == region.tone_document_ref;
            });
        if (!known)
        {
            return std::unexpected{LiveRigError{
                LiveRigErrorCode::InvalidRequest,
                "Scheduled tone is not loaded in the live rig: " + region.tone_document_ref,
            }};
        }
    }

    // Playback follows curves only while the edit's automation-read gate is on. It defaults on
    // and persists per edit, but a stray toggle would silently freeze tone switching — enforce
    // it at the one place schedules are baked.
    m_impl->m_edit->getAutomationRecordManager().setReadingAutomation(true);

    // An empty schedule leaves selection-driven switching in charge (tone-less arrangements).
    // Curves are still cleared so a previous song's schedule can never leak into this one.
    for (const ToneRackBranch& branch : m_impl->m_tone_rack->branches)
    {
        const tracktion::AutomatableParameter::Ptr parameter =
            branch.branch_gain->branchGainParameter();
        auto& curve = parameter->getCurve();
        curve.clear(nullptr);
        if (regions.empty())
        {
            continue;
        }

        // The envelope math is pure, shared policy (common/core); this loop is deliberately a
        // thin point-writing adapter so scheduling behavior stays headless-testable.
        const std::vector<common::core::ToneGainPoint> envelope =
            common::core::makeToneGainEnvelope(
                regions, branch.tone_document_ref, common::core::g_tone_switch_ramp_seconds);
        for (const common::core::ToneGainPoint& point : envelope)
        {
            // Curve shape 0.0f = linear segment; undo manager nullptr keeps schedule baking out
            // of any undo stack.
            curve.addPoint(
                tracktion::TimePosition::fromSeconds(point.seconds), point.gain, 0.0F, nullptr);
        }
    }

    return {};
}

// Verified against the vendored engine (in-progress tone plan mechanism notes): parameter
// streams follow the transport position while stopped or scrubbing, so branch gains snap to the
// playhead after any seek or loop wrap without an explicit push. This hook stays a documented
// no-op until a real resync gap shows up under test.
std::expected<void, LiveRigError> Engine::setToneTimelinePosition(
    common::core::TimePosition /*position*/)
{
    return {};
}

} // namespace rock_hero::common::audio
