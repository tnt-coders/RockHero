/*!
\file i_tone_timeline_player.h
\brief Port for scheduled, transport-driven tone switching over the multi-tone rig.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/audio/live_rig/live_rig_error.h>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <span>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned boundary for scheduled tone switching against the transport timeline.

prepareToneTimeline bakes the region schedule onto the preloaded rig's branch-gain automation.
After that the audio thread drives every switch by evaluating the baked automation against the
transport position — there are no per-frame calls and nothing outside the audio thread pushes
position to trigger a switch during playback.

OWNERSHIP, the one rule both products obey: a baked schedule owns the branch gains, and
ILiveRig::setAudibleTone must not be called while one exists. Baking an empty schedule clears the
curves and hands the gains back to that direct write. ILiveRig::describeLoadedTone is exempt: it
writes no gain, which is how the editor's signal-chain panel follows a scheduled switch. The game
bakes once per rig load and keeps the schedule for the whole session; the editor bakes at Play and
clears at every stop, because while it is paused the audible tone follows the caret rather than the
timeline.

A playhead jump made while the graph renders no blocks is the exception: automation is only
evaluated per block, so the rig would keep its pre-jump values. That is handled entirely inside the
implementation — every playhead discontinuity (load, clear, seek, play, pause, stop) resyncs the
rig's automated parameters, the switch branch gains and the authored tone parameter lanes alike —
so this port carries no position-pushing call at all. The play boundary's resync is what applies a
schedule baked immediately before the transport starts, ahead of the first audio block.

All methods are message-thread operations, like ILiveRig. One port serves both products: the
editor bakes the same schedule for authoring playback that the game bakes for gameplay, so tone
reproduction cannot diverge between them.
*/
class IToneTimelinePlayer
{
public:
    /*! \brief Destroys the tone timeline player interface. */
    virtual ~IToneTimelinePlayer() = default;

    /*!
    \brief Bakes the switch schedule onto the preloaded rig, or clears it for an empty schedule.

    Every referenced tone must already be loaded into the rig; an unknown reference is refused.
    An empty schedule clears the baked curves, which is how a caller hands the branch gains back to
    ILiveRig::setAudibleTone.

    \param song_directory Native song workspace directory that owns package-relative tone files.
    \param regions Seconds-resolved, contiguous switch regions (see makeToneSchedule); empty clears.
    \return Nothing on success, or a typed live-rig failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> prepareToneTimeline(
        const std::filesystem::path& song_directory,
        std::span<const common::core::ToneSwitchRegion> regions) = 0;

protected:
    /*! \brief Creates the tone timeline player interface. */
    IToneTimelinePlayer() = default;

    /*! \brief Copies the tone timeline player interface. */
    IToneTimelinePlayer(const IToneTimelinePlayer&) = default;

    /*! \brief Moves the tone timeline player interface. */
    IToneTimelinePlayer(IToneTimelinePlayer&&) = default;

    /*!
    \brief Assigns the tone timeline player interface from another instance.
    \return Reference to this tone timeline player interface.
    */
    IToneTimelinePlayer& operator=(const IToneTimelinePlayer&) = default;

    /*!
    \brief Move-assigns the tone timeline player interface from another instance.
    \return Reference to this tone timeline player interface.
    */
    IToneTimelinePlayer& operator=(IToneTimelinePlayer&&) = default;
};

} // namespace rock_hero::common::audio
