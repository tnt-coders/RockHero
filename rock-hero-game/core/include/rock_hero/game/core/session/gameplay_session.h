/*!
\file gameplay_session.h
\brief Headless "play a song" orchestration: load a package, prepare the rig, drive playback.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <rock_hero/common/audio/clock/i_playback_clock.h>
#include <rock_hero/common/audio/input/live_input_monitor.h>
#include <rock_hero/common/audio/live_rig/i_live_rig.h>
#include <rock_hero/common/audio/mix/i_mix_controls.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/song/i_song_audio.h>
#include <rock_hero/common/audio/tone_timeline/i_tone_timeline_player.h>
#include <rock_hero/common/audio/transport/i_transport.h>
#include <rock_hero/common/audio/transport/transport_error.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/common/core/tone/tone_schedule.h>
#include <rock_hero/game/core/session/gameplay_session_error.h>
#include <string>
#include <vector>

namespace rock_hero::game::core
{

/*! \brief Observable lifecycle stage of a gameplay session. */
enum class GameplaySessionStage : std::uint8_t
{
    /*! \brief No song is loaded; start() is the only legal operation. */
    Idle,

    /*! \brief Package extraction, parsing, and audio preparation are in progress. */
    Loading,

    /*! \brief Tone preloading is in progress; playback is not yet available. */
    PreparingRig,

    /*! \brief Fully loaded and preloaded; playback can start instantly. */
    Ready,

    /*! \brief The transport is running. */
    Playing,

    /*! \brief Playback is paused at the current position. */
    Paused,

    /*! \brief The song played through to the end of its content. */
    Finished,

    /*! \brief A load stage failed; error() carries the typed reason. */
    Failed,
};

/*! \brief Everything start() needs to load one song for play. */
struct GameplaySessionRequest
{
    /*! \brief The `.rock` package to play. The file itself is never written. */
    std::filesystem::path package_path;

    /*! \brief Arrangement id to play; empty selects the song's first arrangement. */
    std::string arrangement_id;

    /*!
    \brief Session-private scratch directory for the extracted package.

    Must be an unused path under per-user app data (never beside the package). The session
    creates it during start() and deletes it on close; the composing caller owns uniqueness
    (one directory per session).
    */
    std::filesystem::path workspace_directory;
};

/*!
\brief Headless state machine that orchestrates playing one song end to end.

Consumes only project-owned ports, so it is fully testable with fakes: package loading and audio
preparation run through ISongAudio, tone preloading through ILiveRig (the pre-song preload
guarantee: the session never reports Ready until the rig completion fires, so no plugin
instantiation can happen after Ready), scheduled tone switching through IToneTimelinePlayer, and
playback through ITransport with song time exposed via the wait-free IPlaybackClock.

All methods are message-thread operations (the ports it drives require it). Loading work inside
start() is synchronous on the calling thread; composition decides where that runs.
*/
class GameplaySession final : private common::audio::ITransport::Listener
{
public:
    /*!
    \brief Creates an idle session over the composed audio ports.

    \param song_audio Song preparation and arrangement activation boundary.
    \param transport Playback transport the session drives.
    \param live_rig Tone preloading boundary; also owns the player-monitor gain.
    \param tone_timeline Scheduled tone switching boundary.
    \param mix_controls Master and backing-track gain boundary.
    \param clock Wait-free playback clock handed to gameplay consumers.
    \param live_input_monitor Shared calibrate-first live-input monitoring gate the session drives
    at its Ready and close edges.
    */
    GameplaySession(
        common::audio::ISongAudio& song_audio, common::audio::ITransport& transport,
        common::audio::ILiveRig& live_rig, common::audio::IToneTimelinePlayer& tone_timeline,
        common::audio::IMixControls& mix_controls, const common::audio::IPlaybackClock& clock,
        common::audio::LiveInputMonitor& live_input_monitor);

    /*! \brief Closes the session, releasing the arrangement and deleting the scratch workspace. */
    ~GameplaySession() override;

    GameplaySession(const GameplaySession&) = delete;
    GameplaySession(GameplaySession&&) = delete;
    GameplaySession& operator=(const GameplaySession&) = delete;
    GameplaySession& operator=(GameplaySession&&) = delete;

    /*!
    \brief Loads the requested package and begins tone preloading.

    Legal only while Idle. Runs the Loading stage synchronously (workspace creation, package
    extraction, arrangement selection, audio preparation and activation), then issues the async
    rig preload and returns with the session in PreparingRig. The rig completion callback later
    moves the session to Ready (or Failed). Every failure also transitions the session to Failed
    with the same typed error this method returns.

    \param request Package, arrangement selection, and session workspace.
    \return Nothing on success, or the typed reason the load failed.
    */
    [[nodiscard]] std::expected<void, GameplaySessionError> start(GameplaySessionRequest request);

    /*!
    \brief Starts or resumes playback.

    Legal from Ready, Paused, and Finished (Finished restarts from the top).

    \return Nothing on success, or OperationUnavailable outside those stages.
    */
    [[nodiscard]] std::expected<void, GameplaySessionError> play();

    /*!
    \brief Pauses playback, keeping the current position.
    \return Nothing on success, or OperationUnavailable when not Playing.
    */
    [[nodiscard]] std::expected<void, GameplaySessionError> pause();

    /*!
    \brief Moves the playhead. Legal in Ready, Playing, Paused, and Finished.
    \param position Target position on the song timeline.
    \return Nothing on success, or OperationUnavailable outside those stages.
    */
    [[nodiscard]] std::expected<void, GameplaySessionError> seek(
        common::core::TimePosition position);

    /*!
    \brief Instant restart: seek to the top and play, with no rig teardown or re-preload.
    \return Nothing on success, or OperationUnavailable before the rig is ready.
    */
    [[nodiscard]] std::expected<void, GameplaySessionError> restart();

    /*!
    \brief Forwards a playback speed request to the transport (plan 28 drives this later).
    \param factor Requested playback speed multiplier, where 1.0 is normal speed.
    \return The transport's typed result.
    */
    [[nodiscard]] std::expected<void, common::audio::TransportError> setPlaybackSpeed(
        double factor);

    /*!
    \brief Forwards a loop-region request to the transport (plan 28 drives this later).
    \param region Loop region in song-timeline seconds.
    \return The transport's typed result.
    */
    [[nodiscard]] std::expected<void, common::audio::TransportError> setLoopRegion(
        common::core::TimeRange region);

    /*! \brief Disengages transport looping. */
    void clearLoopRegion();

    /*!
    \brief Sets the edit-wide master volume (21-Q3: global, session-local until plan 27 lands).
    \param gain Desired master gain.
    \return The mix boundary's typed result.
    */
    [[nodiscard]] std::expected<void, common::audio::MixControlsError> setMasterVolume(
        common::audio::Gain gain);

    /*! \brief Reads the edit-wide master volume. */
    [[nodiscard]] common::audio::Gain masterVolume() const;

    /*!
    \brief Sets the backing-track volume; composes with the clip normalization gain.
    \param gain Desired backing gain.
    \return The mix boundary's typed result.
    */
    [[nodiscard]] std::expected<void, common::audio::MixControlsError> setBackingVolume(
        common::audio::Gain gain);

    /*! \brief Reads the backing-track volume. */
    [[nodiscard]] common::audio::Gain backingVolume() const;

    /*!
    \brief Sets the player-monitor volume (forwards to the live rig's output gain).
    \param gain Desired monitor gain.
    \return The live rig's typed result.
    */
    [[nodiscard]] std::expected<void, common::audio::LiveRigError> setMonitorVolume(
        common::audio::Gain gain);

    /*! \brief Reads the player-monitor volume. */
    [[nodiscard]] common::audio::Gain monitorVolume() const;

    /*! \brief Reads the session's current lifecycle stage. */
    [[nodiscard]] GameplaySessionStage stage() const noexcept;

    /*!
    \brief Reads the failure that moved the session to Failed.
    \return The typed error while Failed; std::nullopt in every other stage.
    */
    [[nodiscard]] std::optional<GameplaySessionError> error() const;

    /*!
    \brief The wait-free playback clock gameplay consumers sample for song time.
    \return The clock port supplied at construction.
    */
    [[nodiscard]] const common::audio::IPlaybackClock& clock() const noexcept;

    /*!
    \brief Reads the arrangement selected by the last successful load.
    \return The active arrangement, or nullptr before Ready-track stages.
    */
    [[nodiscard]] const common::core::Arrangement* activeArrangement() const noexcept;

    /*!
    \brief Releases playback, clears the active arrangement, and deletes the scratch workspace.

    Safe to call in any stage; the session returns to Idle. Called by the destructor.
    */
    void close();

private:
    // Detects unsolicited playback stops: the engine's end-of-content auto-stop is the only
    // transport state change the session does not initiate itself, so an unexpected
    // playing -> stopped transition while Playing means the song finished.
    void onTransportStateChanged(common::audio::TransportState state) override;

    // Fails the load pipeline: records the error, transitions to Failed, and returns the same
    // error so start() can propagate it.
    [[nodiscard]] std::expected<void, GameplaySessionError> failLoad(GameplaySessionError error);

    // Handles the async rig-load completion for the generation that issued it.
    void onRigLoadCompleted(
        std::uint64_t generation,
        std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result);

    // Composed audio ports; the session owns orchestration, never the implementations.
    common::audio::ISongAudio& m_song_audio;
    common::audio::ITransport& m_transport;
    common::audio::ILiveRig& m_live_rig;
    common::audio::IToneTimelinePlayer& m_tone_timeline;
    common::audio::IMixControls& m_mix_controls;
    const common::audio::IPlaybackClock& m_clock;

    // Shared calibrate-first live-input monitoring gate. The session is a thin driver: it arms the
    // gate at Ready and disables it on close; the gate stays silent unless this app's own store
    // holds a calibration matching the active input route.
    common::audio::LiveInputMonitor& m_live_input_monitor;

    // Current lifecycle stage; every transition happens on the message thread.
    GameplaySessionStage m_stage{GameplaySessionStage::Idle};

    // Typed failure captured when m_stage is Failed.
    std::optional<GameplaySessionError> m_error;

    // Song loaded by start(); owns the arrangement activeArrangement() points into.
    common::core::Song m_song;

    // Index of the selected arrangement inside m_song.arrangements.
    std::size_t m_arrangement_index{0};

    // Session-private scratch directory; created by start(), deleted by close().
    std::filesystem::path m_workspace;

    // Seconds-resolved tone switch schedule handed to the tone timeline at rig completion.
    std::vector<common::core::ToneSwitchRegion> m_tone_schedule;

    // Invalidates in-flight rig completions from superseded loads (close/start bumps it).
    std::uint64_t m_load_generation{0};

    // Liveness token for the async rig completion (non-component owner -> weak_ptr guard).
    std::shared_ptr<char> m_liveness{std::make_shared<char>('\0')};
};

} // namespace rock_hero::game::core
