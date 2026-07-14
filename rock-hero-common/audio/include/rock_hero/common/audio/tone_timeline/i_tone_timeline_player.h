/*!
\file i_tone_timeline_player.h
\brief Port for scheduled, transport-driven tone switching over the multi-tone rig.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/audio/live_rig/live_rig_error.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <span>

namespace rock_hero::common::audio
{

/*!
\brief Project-owned boundary for scheduled tone switching against the transport timeline.

prepareToneTimeline does the real work up front: it builds the multi-tone graph, preloads every
referenced tone, and bakes the region schedule into transport-evaluated branch-gain automation.
After that the audio thread drives every switch by evaluating the baked automation against the
transport position — there are no per-frame calls and nothing outside the audio thread pushes
position to trigger a switch. setToneTimelinePosition exists only so a seek or scrub can resync
the audible tone to a new playhead position; it is not the playback switch path.

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
    \brief Builds the rig graph, preloads referenced tones, and bakes the switch schedule.

    \param song_directory Native song workspace directory that owns package-relative tone files.
    \param regions Seconds-resolved, contiguous switch regions (see makeToneSchedule).
    \return Nothing on success, or a typed live-rig failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> prepareToneTimeline(
        const std::filesystem::path& song_directory,
        std::span<const common::core::ToneSwitchRegion> regions) = 0;

    /*!
    \brief Resyncs the audible tone after a seek or scrub.

    Not used during continuous playback — the audio thread's automation evaluation owns every
    playback-driven switch, including loop wraps.

    \param position New playhead position to resync the audible tone against.
    \return Nothing on success, or a typed live-rig failure.
    */
    [[nodiscard]] virtual std::expected<void, LiveRigError> setToneTimelinePosition(
        common::core::TimePosition position) = 0;

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
