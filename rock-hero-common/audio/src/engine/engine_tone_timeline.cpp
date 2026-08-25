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

// A rack is only ever assigned fully built, so a present optional holding a null rack type would
// be a half-torn-down rig; returning null for it keeps that state harmless rather than fatal.
tracktion::RackType* Engine::Impl::loadedToneRack() noexcept
{
    return m_tone_rack.has_value() ? m_tone_rack->rack_type.get() : nullptr;
}

// Pushes a playhead jump onto the rig's tone automation. Tracktion evaluates automation only while
// the graph renders blocks (Plugin::applyToBufferWithAutomation), so a position change made with
// the playback context released leaves every tone parameter reading its pre-jump value.
// RackType::updateAutomatableParamPositions is the engine's public hook for exactly this: it walks
// the rack's plugins and modifiers into AutomatableParameter::updateToFollowCurve, which reads the
// curve directly rather than the audio-thread parameter stream, so it needs no running graph and
// honours a single-point curve the stream discards. It deliberately bypasses
// setAutomatableParamPosition's lastTime dedupe and its isReadingAutomation() gate;
// prepareToneTimeline pins that flag on, so there is nothing left to gate against. One call covers
// the whole rig because every tone plugin and every branch gain lives inside the single rack.
//
// This is the only place a position reaches the rack, and publishClockBoundary is its only caller:
// every playhead discontinuity runs through that boundary, so no port surface pushes position.
void Engine::Impl::resyncToneAutomation(common::core::TimePosition position)
{
    if (tracktion::RackType* const rack = loadedToneRack(); rack != nullptr)
    {
        rack->updateAutomatableParamPositions(
            tracktion::TimePosition::fromSeconds(position.seconds));
    }
}

} // namespace rock_hero::common::audio
