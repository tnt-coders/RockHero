#include "rock_hero/game/core/session/gameplay_session.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <rock_hero/common/core/package/rock_song_package.h>
#include <system_error>
#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Composes the rig preload request exactly like the editor's project load does
// (rock-hero-editor/core/src/project/project_handlers.cpp): every tone the arrangement's regions
// reference, deduplicated in schedule order, empty entries skipped; the audible tone is left
// empty so the engine's first-branch fallback applies. Constraint (g): the game must reproduce
// the authored tone path byte-for-byte, so the request composition must match the editor's.
[[nodiscard]] std::vector<std::string> toneDocumentRefsForArrangement(
    const common::core::Arrangement& arrangement)
{
    std::vector<std::string> tone_document_refs;
    for (const common::core::ToneRegion& region : arrangement.tone_track.regions)
    {
        if (!region.tone_document_ref.empty() &&
            std::ranges::find(tone_document_refs, region.tone_document_ref) ==
                tone_document_refs.end())
        {
            tone_document_refs.push_back(region.tone_document_ref);
        }
    }

    return tone_document_refs;
}

} // namespace

GameplaySession::GameplaySession(
    common::audio::ISongAudio& song_audio, common::audio::ITransport& transport,
    common::audio::ILiveRig& live_rig, common::audio::IToneTimelinePlayer& tone_timeline,
    common::audio::IMixControls& mix_controls, const common::audio::IPlaybackClock& clock,
    common::audio::LiveInputMonitor& live_input_monitor)
    : m_song_audio{song_audio}
    , m_transport{transport}
    , m_live_rig{live_rig}
    , m_tone_timeline{tone_timeline}
    , m_mix_controls{mix_controls}
    , m_clock{clock}
    , m_live_input_monitor{live_input_monitor}
{
    // The session listens for the one transport transition it does not initiate itself: the
    // engine's end-of-content auto-stop, which is how Playing becomes Finished.
    m_transport.addListener(*this);
}

GameplaySession::~GameplaySession()
{
    close();
    m_transport.removeListener(*this);
}

std::expected<void, GameplaySessionError> GameplaySession::start(GameplaySessionRequest request)
{
    if (m_stage != GameplaySessionStage::Idle)
    {
        return std::unexpected{GameplaySessionError{
            GameplaySessionErrorCode::OperationUnavailable,
            "start() requires an idle session",
        }};
    }

    m_stage = GameplaySessionStage::Loading;

    // The scratch workspace lives under per-user app data (the caller supplies the unique
    // per-session path), never beside the package: the package file itself is read-only.
    std::error_code workspace_error;
    std::filesystem::create_directories(request.workspace_directory, workspace_error);
    if (workspace_error)
    {
        return failLoad(
            GameplaySessionError{
                GameplaySessionErrorCode::WorkspaceUnavailable,
                "Session workspace could not be created: " + workspace_error.message(),
            });
    }
    m_workspace = request.workspace_directory;

    auto song = common::core::readRockSongPackage(request.package_path, m_workspace);
    if (!song.has_value())
    {
        return failLoad(
            GameplaySessionError{
                GameplaySessionErrorCode::PackageUnreadable,
                song.error().message,
            });
    }
    m_song = std::move(*song);

    // Empty arrangement id selects the first arrangement (the dev-fixture convention until the
    // library UI supplies explicit ids).
    std::size_t arrangement_index = 0;
    if (!request.arrangement_id.empty())
    {
        const auto matches_id = [&request](const common::core::Arrangement& arrangement) {
            return arrangement.id == request.arrangement_id;
        };
        const auto found = std::ranges::find_if(m_song.arrangements, matches_id);
        if (found == m_song.arrangements.end())
        {
            return failLoad(
                GameplaySessionError{
                    GameplaySessionErrorCode::ArrangementNotFound,
                    "Arrangement id not found in song: " + request.arrangement_id,
                });
        }
        arrangement_index = static_cast<std::size_t>(found - m_song.arrangements.begin());
    }
    else if (m_song.arrangements.empty())
    {
        return failLoad(
            GameplaySessionError{
                GameplaySessionErrorCode::ArrangementNotFound,
                "Song has no arrangements",
            });
    }
    m_arrangement_index = arrangement_index;

    // prepareSong validates every arrangement's audio through the audio boundary and fills the
    // accepted durations the tone schedule's terminal clamp depends on.
    if (const auto prepared = m_song_audio.prepareSong(m_song); !prepared.has_value())
    {
        return failLoad(
            GameplaySessionError{
                GameplaySessionErrorCode::PreparationFailed,
                prepared.error().message,
            });
    }

    const common::core::Arrangement& arrangement = m_song.arrangements[m_arrangement_index];
    if (const auto activated = m_song_audio.setActiveArrangement(arrangement);
        !activated.has_value())
    {
        return failLoad(
            GameplaySessionError{
                GameplaySessionErrorCode::ActivationFailed,
                activated.error().message,
            });
    }

    // Hosted plugins read tempo from the backend, so the song's real tempo map is mirrored
    // exactly like the editor does after its loads — tone fidelity (constraint (g)) includes
    // tempo-synced effects. Best-effort by the port's contract.
    m_song_audio.mirrorTempoMap(m_song.tempo_map);

    // The seconds-resolved switch schedule is derived once per load and handed to the tone
    // timeline when the rig finishes preloading (Phase 3 implements the backend).
    m_tone_schedule = common::core::makeToneSchedule(
        arrangement.tone_track, m_song.tempo_map, arrangement.audio_duration);

    m_stage = GameplaySessionStage::PreparingRig;

    // The completion may fire after this session is superseded (close/start) or destroyed, so it
    // validates both the load generation and a liveness token before touching the session.
    const std::uint64_t generation = ++m_load_generation;
    const std::weak_ptr<char> liveness{m_liveness};
    m_live_rig.loadLiveRig(
        common::audio::LiveRigLoadRequest{
            .song_directory = m_workspace,
            .tone_document_refs = toneDocumentRefsForArrangement(arrangement),
            .audible_tone_ref = {},
            .progress_callback = {},
            .yield_callback = {},
        },
        [this, generation, liveness](
            std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result) {
            if (liveness.expired())
            {
                return;
            }
            onRigLoadCompleted(generation, std::move(result));
        });

    // A synchronous completion (fakes, empty tone sets) may already have advanced or failed the
    // session; report that failure to the caller instead of a stale success.
    if (m_stage == GameplaySessionStage::Failed && m_error.has_value())
    {
        return std::unexpected{*m_error};
    }
    return {};
}

std::expected<void, GameplaySessionError> GameplaySession::play()
{
    if (m_stage == GameplaySessionStage::Finished)
    {
        return restart();
    }
    if (m_stage != GameplaySessionStage::Ready && m_stage != GameplaySessionStage::Paused)
    {
        return std::unexpected{
            GameplaySessionError{GameplaySessionErrorCode::OperationUnavailable}
        };
    }

    // Stage moves first so the listener attributes the resulting transport transition to the
    // session's own request instead of misreading it.
    m_stage = GameplaySessionStage::Playing;
    m_transport.play();
    return {};
}

std::expected<void, GameplaySessionError> GameplaySession::pause()
{
    if (m_stage != GameplaySessionStage::Playing)
    {
        return std::unexpected{
            GameplaySessionError{GameplaySessionErrorCode::OperationUnavailable}
        };
    }

    // Stage moves first: the transport listener must never mistake this stop for song end.
    m_stage = GameplaySessionStage::Paused;
    m_transport.pause();
    return {};
}

std::expected<void, GameplaySessionError> GameplaySession::seek(common::core::TimePosition position)
{
    if (m_stage != GameplaySessionStage::Ready && m_stage != GameplaySessionStage::Playing &&
        m_stage != GameplaySessionStage::Paused && m_stage != GameplaySessionStage::Finished)
    {
        return std::unexpected{
            GameplaySessionError{GameplaySessionErrorCode::OperationUnavailable}
        };
    }

    // Seeking a finished session revives it to a paused position (the player scrubbed back).
    if (m_stage == GameplaySessionStage::Finished)
    {
        m_stage = GameplaySessionStage::Paused;
    }
    m_transport.seek(position);
    return {};
}

std::expected<void, GameplaySessionError> GameplaySession::restart()
{
    if (m_stage != GameplaySessionStage::Ready && m_stage != GameplaySessionStage::Playing &&
        m_stage != GameplaySessionStage::Paused && m_stage != GameplaySessionStage::Finished)
    {
        return std::unexpected{
            GameplaySessionError{GameplaySessionErrorCode::OperationUnavailable}
        };
    }

    // Instant restart is a seek plus play: no rig teardown, no re-preload (the pre-song preload
    // guarantee makes this instant).
    m_stage = GameplaySessionStage::Playing;
    m_transport.seek(common::core::TimePosition{});
    m_transport.play();
    return {};
}

std::expected<void, common::audio::TransportError> GameplaySession::setPlaybackSpeed(double factor)
{
    return m_transport.setPlaybackSpeed(factor);
}

std::expected<void, common::audio::TransportError> GameplaySession::setLoopRegion(
    common::core::TimeRange region)
{
    return m_transport.setLoopRegion(region);
}

void GameplaySession::clearLoopRegion()
{
    m_transport.clearLoopRegion();
}

std::expected<void, common::audio::MixControlsError> GameplaySession::setMasterVolume(
    common::audio::Gain gain)
{
    return m_mix_controls.setMasterGain(gain);
}

common::audio::Gain GameplaySession::masterVolume() const
{
    return m_mix_controls.masterGain();
}

std::expected<void, common::audio::MixControlsError> GameplaySession::setBackingVolume(
    common::audio::Gain gain)
{
    return m_mix_controls.setBackingGain(gain);
}

common::audio::Gain GameplaySession::backingVolume() const
{
    return m_mix_controls.backingGain();
}

// The monitor gain's single owner is the live rig's output stage; the session only forwards so
// the game has one mixing surface (21-Q3: three volumes, each with exactly one backend owner).
std::expected<void, common::audio::LiveRigError> GameplaySession::setMonitorVolume(
    common::audio::Gain gain)
{
    return m_live_rig.setOutputGain(gain);
}

common::audio::Gain GameplaySession::monitorVolume() const
{
    return m_live_rig.outputGain();
}

GameplaySessionStage GameplaySession::stage() const noexcept
{
    return m_stage;
}

std::optional<GameplaySessionError> GameplaySession::error() const
{
    return m_stage == GameplaySessionStage::Failed ? m_error : std::nullopt;
}

const common::audio::IPlaybackClock& GameplaySession::clock() const noexcept
{
    return m_clock;
}

const common::core::Arrangement* GameplaySession::activeArrangement() const noexcept
{
    const bool loaded =
        m_stage == GameplaySessionStage::PreparingRig || m_stage == GameplaySessionStage::Ready ||
        m_stage == GameplaySessionStage::Playing || m_stage == GameplaySessionStage::Paused ||
        m_stage == GameplaySessionStage::Finished;
    if (!loaded || m_arrangement_index >= m_song.arrangements.size())
    {
        return nullptr;
    }

    return &m_song.arrangements[m_arrangement_index];
}

void GameplaySession::close()
{
    // Invalidate any in-flight rig completion before touching state it would read.
    ++m_load_generation;

    // Tear down the live-input gate the Ready edge may have armed: monitoring must not outlive the
    // session, and disabling is best-effort by the monitor's contract. restart()/replay do no rig
    // work, so monitoring stays armed across pause/finish/restart -- only close() disables it.
    m_live_input_monitor.disableMonitoring();

    if (m_stage == GameplaySessionStage::Playing)
    {
        m_transport.pause();
    }

    if (m_stage != GameplaySessionStage::Idle)
    {
        // Best-effort teardown: close has no caller-visible error channel, and a failed clear
        // cannot leave the session in a worse state than the reload that follows it.
        (void)m_song_audio.clearActiveArrangement();
    }

    if (!m_workspace.empty())
    {
        // Best-effort: a straggling handle only strands a temp directory the next session's
        // unique path never collides with.
        std::error_code remove_error;
        std::filesystem::remove_all(m_workspace, remove_error);
        m_workspace.clear();
    }

    m_song = {};
    m_tone_schedule.clear();
    m_arrangement_index = 0;
    m_error.reset();
    m_stage = GameplaySessionStage::Idle;
}

void GameplaySession::onTransportStateChanged(common::audio::TransportState state)
{
    // The session initiates every transport change except one: the engine's end-of-content
    // auto-stop. Its own play/pause/restart calls move m_stage BEFORE touching the transport,
    // so a stop observed while still Playing can only be the song finishing.
    if (m_stage == GameplaySessionStage::Playing && !state.playing)
    {
        m_stage = GameplaySessionStage::Finished;
    }
}

std::expected<void, GameplaySessionError> GameplaySession::failLoad(GameplaySessionError error)
{
    m_stage = GameplaySessionStage::Failed;
    m_error = error;
    return std::unexpected{std::move(error)};
}

void GameplaySession::onRigLoadCompleted(
    std::uint64_t generation,
    std::expected<common::audio::LiveRigLoadResult, common::audio::LiveRigError> result)
{
    // Stale completions from superseded loads must not touch the current session state.
    if (generation != m_load_generation)
    {
        return;
    }

    if (!result.has_value())
    {
        // Missing plugins get their own code so UI can present an "install these" flow
        // (21-Q1: refuse to start, listing the missing plugins) distinct from generic failure.
        const GameplaySessionErrorCode code =
            result.error().code == common::audio::LiveRigErrorCode::MissingPlugins
                ? GameplaySessionErrorCode::MissingPlugins
                : GameplaySessionErrorCode::RigLoadFailed;
        m_stage = GameplaySessionStage::Failed;
        m_error = GameplaySessionError{code, result.error().message};
        return;
    }

    // Hand the region schedule to the tone timeline; scheduled switching is baked once per load
    // and evaluated by the audio thread from then on (no per-frame calls).
    if (const auto prepared = m_tone_timeline.prepareToneTimeline(m_workspace, m_tone_schedule);
        !prepared.has_value())
    {
        m_stage = GameplaySessionStage::Failed;
        m_error = GameplaySessionError{
            GameplaySessionErrorCode::ToneTimelineFailed,
            prepared.error().message,
        };
        return;
    }

    // The pre-song preload guarantee: Ready is reported only now, after the completion fired,
    // so no plugin instantiation can happen once gameplay is allowed to start.
    m_stage = GameplaySessionStage::Ready;

    // Arm the calibrate-first live-input gate now that arrangement audio and the tone rig are
    // committed -- the game's readiness edge, the exact analogue of the editor's project-ready
    // gate. This completion runs on the JUCE message thread (loadLiveRig refuses off-thread and
    // marshals its continuations back via callAsync), so driving the message-thread-only live-input
    // port from here is contract-correct. The gate stays silent unless this app's own store holds a
    // calibration matching the active input route; a disabled result is non-fatal and never blocks
    // readiness. The status.reason is retained for a future SDL "monitoring off because X" surface
    // (plan 26).
    [[maybe_unused]] const common::audio::LiveInputMonitoringStatus monitoring_status =
        m_live_input_monitor.refresh(
            common::audio::LiveInputMonitoringContext{
                .session_audio_ready = true,
                .arrangement_loaded = true,
            });
}

} // namespace rock_hero::game::core
