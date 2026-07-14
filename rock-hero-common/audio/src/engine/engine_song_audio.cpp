#include "engine_impl.h"
#include "shared/audio_path_util.h"
#include "tracktion/tempo_mirror.h"

#include <rock_hero/common/core/shared/juce_path.h>

namespace rock_hero::common::audio
{

namespace
{

// Maps monitoring rebuild failures into song-audio activation errors.
[[nodiscard]] SongAudioError songAudioErrorFromLiveInputError(const LiveInputError& error)
{
    return SongAudioError{SongAudioErrorCode::MonitoringRouteFailed, error.message};
}

// Opens an asset through Tracktion only long enough to validate it and read its duration.
[[nodiscard]] std::expected<common::core::TimeDuration, SongAudioError> readAudioDuration(
    tracktion::Engine& engine, const common::core::AudioAsset& audio_asset)
{
    const juce::File file = common::core::juceFileFromPath(audio_asset.path);
    if (!file.existsAsFile())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file does not exist: " + pathToUtf8String(audio_asset.path)
        }};
    }

    const tracktion::AudioFile audio_file(engine, file);
    if (!audio_file.isValid())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file could not be decoded: " + pathToUtf8String(audio_asset.path)
        }};
    }

    const common::core::TimeDuration asset_duration{audio_file.getLength()};
    if (asset_duration.seconds <= 0.0)
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::InvalidAudioDuration,
            "Backing audio file has no positive duration: " + pathToUtf8String(audio_asset.path)
        }};
    }

    return asset_duration;
}

} // namespace

// Validates every arrangement audio file and records the accepted backend durations.
std::expected<void, SongAudioError> Engine::prepareSong(common::core::Song& song)
{
    for (common::core::Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.audio_asset.path.empty())
        {
            return std::unexpected{SongAudioError{
                SongAudioErrorCode::MissingAudioAssetPath,
                "Arrangement is missing a backing audio asset path: " + arrangement.id
            }};
        }

        const auto audio_duration = readAudioDuration(*m_impl->m_engine, arrangement.audio_asset);
        if (!audio_duration.has_value())
        {
            return std::unexpected{audio_duration.error()};
        }

        arrangement.audio_duration = *audio_duration;
    }

    return {};
}

// Makes the prepared arrangement active on the Tracktion backing audio track.
std::expected<void, SongAudioError> Engine::setActiveArrangement(
    const common::core::Arrangement& arrangement)
{
    auto* track = m_impl->backingTrack();
    if (track == nullptr)
    {
        return std::unexpected{SongAudioError{SongAudioErrorCode::MissingBackingTrack}};
    }

    if (arrangement.audio_asset.path.empty())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::MissingAudioAssetPath,
            "Arrangement is missing a backing audio asset path: " + arrangement.id
        }};
    }

    if (arrangement.audio_duration.seconds <= 0.0)
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::InvalidAudioDuration,
            "Arrangement has no accepted backing audio duration: " + arrangement.id
        }};
    }

    const juce::File file = common::core::juceFileFromPath(arrangement.audio_asset.path);
    if (!file.existsAsFile())
    {
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::UnreadableAudioFile,
            "Backing audio file does not exist: " + pathToUtf8String(arrangement.audio_asset.path)
        }};
    }

    // Candidate is valid; stop playback and clear nodes before replacing Tracktion's edit graph.
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();

    // A positive asset start offset delays the clip so a backing recording whose content begins
    // after the score's first beat still lines up; the gap before it plays as silence. Almost
    // always zero, in which case the clip sits at the timeline origin as before.
    const auto start =
        tracktion::TimePosition::fromSeconds(arrangement.audio_asset.start_offset.seconds);
    const auto length = tracktion::TimeDuration::fromSeconds(arrangement.audio_duration.seconds);
    const tracktion::ClipPosition wave_clip_position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    // Final trailing argument asks Tracktion to replace any existing media on the track.
    const auto wave_clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, wave_clip_position, true);
    if (wave_clip == nullptr)
    {
        m_impl->rebuildInstrumentMonitoringGraphBestEffort(
            "backing clip insertion rollback failed");
        m_impl->updateTransportState();
        return std::unexpected{SongAudioError{
            SongAudioErrorCode::BackendClipInsertionFailed,
            "Could not insert backing audio clip: " + pathToUtf8String(arrangement.audio_asset.path)
        }};
    }

    // Play the user's backing file directly instead of through Tracktion's cached/offline proxy.
    // Two reasons, both load-bearing:
    //  - Compressed sources (.ogg) default to proxy-enabled, so getPlaybackFile() returns a proxy
    //    that is still being rendered asynchronously when the user presses Space right after open.
    //    WaveNodeRealTime then has no reader and plays silence until the proxy lands and Tracktion
    //    calls restartPlayback() -- the observed silent-scroll-then-freeze regression.
    //  - Practice-speed playback time-stretches this clip live. With a proxy, every speed change
    //    invalidates the proxy hash and schedules a fresh offline render + restartPlayback, stalling
    //    the slider. Proxy-off routes through WaveNodeRealTime's elastique reader, which stretches
    //    the original source in realtime and responds to speed changes immediately.
    // WaveNodeRealTime streams compressed sources via BufferedAudioFileManager, so a single backing
    // track stays cheap at 1x and only pays elastique cost while actually slowed or sped up.
    wave_clip->setUsesProxy(false);

    // The backing recording is pinned to absolute seconds: its placement comes from the audio
    // asset, never from the edit's beat grid. The one-way tempo mirror's writes never run
    // Tracktion's remap snapshot, but the default bars/beats sync would let any future
    // remap-enabled tempo path slide this clip; syncAbsolute makes remapEdit() skip it outright.
    wave_clip->setSyncType(tracktion::Clip::syncAbsolute);

    // Apply persisted normalization gain so playback volume matches the analyzed loudness target.
    if (arrangement.audio_asset.normalization.has_value())
    {
        wave_clip->setGainDB(static_cast<float>(arrangement.audio_asset.normalization->gain_db));
    }

    m_impl->m_loaded_length_seconds = arrangement.audio_duration.seconds;

    // Arrangement activation clears any engaged loop through the shared helper so the looping
    // flag and the stored loop points can never diverge; callers that want a loop across a load
    // re-apply it through the transport port afterwards.
    m_impl->disengageLoop();
    transport.setPosition(tracktion::TimePosition{});
    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{songAudioErrorFromLiveInputError(route_result.error())};
    }
    m_impl->updateTransportState();
    m_impl->publishClockBoundary(common::core::TimePosition{});
    return {};
}

// Clears the backing track so closed projects do not leave stale media in Tracktion.
std::expected<void, SongAudioError> Engine::clearActiveArrangement()
{
    auto& transport = m_impl->m_edit->getTransport();
    m_impl->stopTransportAndReleaseContext();
    transport.setPosition(tracktion::TimePosition{});

    if (auto* track = m_impl->backingTrack(); track != nullptr)
    {
        const juce::Array<tracktion::Clip*> clips = track->getClips();
        for (tracktion::Clip* clip : clips)
        {
            if (clip != nullptr)
            {
                clip->removeFromParent();
            }
        }
    }

    m_impl->m_loaded_length_seconds = 0.0;
    auto route_result = m_impl->rebuildInstrumentMonitoringGraph();
    if (!route_result.has_value())
    {
        return std::unexpected{songAudioErrorFromLiveInputError(route_result.error())};
    }
    m_impl->updateTransportState();
    m_impl->publishClockBoundary(common::core::TimePosition{});
    return {};
}

void Engine::mirrorTempoMap(const common::core::TempoMap& tempo_map)
{
    // Best-effort derived write: the mirror only feeds hosted plugins' host-tempo view, so guards
    // return silently rather than surfacing errors nothing can act on.
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread() || m_impl->m_edit == nullptr)
    {
        return;
    }

    mirrorTempoMapIntoSequence(m_impl->m_edit->tempoSequence, tempo_map);
}

} // namespace rock_hero::common::audio
