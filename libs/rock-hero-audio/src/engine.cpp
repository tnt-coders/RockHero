#include "engine.h"

#include <atomic>
#include <rock_hero/audio/thumbnail.h>
#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::audio
{

struct Engine::Impl
{
    std::unique_ptr<tracktion::Engine> engine;
    std::unique_ptr<tracktion::Edit> edit;
    std::atomic<double> transport_position{0.0};
};

// Creates the Tracktion engine and a minimal single-track edit for early playback support.
Engine::Engine() : m_impl(std::make_unique<Impl>())
{
    m_impl->engine = std::make_unique<tracktion::Engine>("RockHero");

    // Stereo output, no input for now (ASIO guitar input comes later).
    m_impl->engine->getDeviceManager().initialise(0, 2);

    // createSingleTrackEdit already provides one AudioTrack ready for clips.
    m_impl->edit = tracktion::Edit::createSingleTrackEdit(*m_impl->engine);
}

// Stops transport activity before destroying Tracktion objects in dependency order.
Engine::~Engine()
{
    if (m_impl->edit)
    {
        m_impl->edit->getTransport().stop(false, false);
    }

    m_impl->edit.reset();
    m_impl->engine.reset();
}

// Replaces the single backing-track clip while keeping graph mutation on the message thread.
bool Engine::loadFile(const std::filesystem::path& file)
{
    // Stop playback before mutating clips to avoid mid-stream graph rebuilds.
    m_impl->edit->getTransport().stop(false, false);

    const juce::File juce_file(file.string());

    if (!juce_file.existsAsFile())
    {
        return false;
    }

    auto* track = tracktion::getAudioTracks(*m_impl->edit)[0];
    if (track == nullptr)
    {
        return false;
    }

    // Remove any existing clips on this track.
    for (auto* clip : track->getClips())
    {
        clip->removeFromParent();
    }

    tracktion::AudioFile audio_file(*m_impl->engine, juce_file);
    if (!audio_file.isValid())
    {
        return false;
    }

    const auto length = tracktion::TimeDuration::fromSeconds(audio_file.getLength());
    const auto start = tracktion::TimePosition{};
    const tracktion::ClipPosition position{
        .time = {start, start + length}, .offset = tracktion::TimeDuration{}
    };

    const auto clip =
        track->insertWaveClip(juce_file.getFileNameWithoutExtension(), juce_file, position, false);
    if (clip == nullptr)
    {
        return false;
    }

    auto& transport = m_impl->edit->getTransport();
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});

    m_impl->transport_position.store(0.0, std::memory_order_relaxed);
    return true;
}

// Starts Tracktion transport playback from the current edit position.
void Engine::play()
{
    m_impl->edit->getTransport().play(false);
}

// Stops playback and resets both Tracktion and cached transport positions to the start.
void Engine::stop()
{
    m_impl->edit->getTransport().stop(false, false);
    m_impl->edit->getTransport().setPosition(tracktion::TimePosition{});
    m_impl->transport_position.store(0.0, std::memory_order_relaxed);
}

// Pauses playback without resetting position so the user can resume from the same point.
void Engine::pause()
{
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position — that is the semantic difference from stop().
    m_impl->edit->getTransport().stop(false, false);
}

// Moves Tracktion transport and the UI-readable cache to the requested song time.
void Engine::seek(double seconds)
{
    m_impl->edit->getTransport().setPosition(tracktion::TimePosition::fromSeconds(seconds));
    m_impl->transport_position.store(seconds, std::memory_order_relaxed);
}

// Reads Tracktion transport state for UI controls that need current playback status.
bool Engine::isPlaying() const
{
    return m_impl->edit->getTransport().isPlaying();
}

// Returns the lock-free cached transport position for UI painting and future realtime readers.
double Engine::getTransportPosition() const noexcept
{
    return m_impl->transport_position.load(std::memory_order_relaxed);
}

// Mirrors Tracktion's message-thread position into the atomic cache used by UI components.
void Engine::updateTransportPositionCache()
{
    const double pos = m_impl->edit->getTransport().getPosition().inSeconds();
    m_impl->transport_position.store(pos, std::memory_order_relaxed);
}

// Creates a thumbnail wrapper without exposing Tracktion types through public UI-facing headers.
std::unique_ptr<Thumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<Thumbnail>(*m_impl->engine, owner);
}

} // namespace rock_hero::audio
