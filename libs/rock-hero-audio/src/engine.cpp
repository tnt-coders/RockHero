#include <rock_hero/audio/engine.h>

#include <rock_hero/audio/thumbnail.h>

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero::audio
{

Engine::Engine() : m_engine(std::make_unique<tracktion::Engine>("RockHero"))
{
    // Stereo output, no input for now (ASIO guitar input comes later).
    m_engine->getDeviceManager().initialise(0, 2);

    // createSingleTrackEdit creates an Edit with one AudioTrack ready for clips.
    // createSingleTrackEdit already provides one AudioTrack.
    m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
}

Engine::~Engine()
{
    if (m_edit)
    {
        m_edit->getTransport().stop(false, false);
    }

    m_edit.reset();
    m_engine.reset();
}

bool Engine::loadFile(const std::filesystem::path& file)
{
    // Stop playback before mutating clips to avoid mid-stream graph rebuilds.
    m_edit->getTransport().stop(false, false);

    const juce::File juce_file(file.string());

    if (!juce_file.existsAsFile())
    {
        return false;
    }

    auto* track = tracktion::getAudioTracks(*m_edit)[0];
    if (track == nullptr)
    {
        return false;
    }

    // Remove any existing clips on this track.
    for (auto* clip : track->getClips())
    {
        clip->removeFromParent();
    }

    tracktion::AudioFile audio_file(*m_engine, juce_file);
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

    auto& transport = m_edit->getTransport();
    transport.looping = false;
    transport.setPosition(tracktion::TimePosition{});

    m_transport_position.store(0.0, std::memory_order_relaxed);
    return true;
}

void Engine::play()
{
    m_edit->getTransport().play(false);
}

void Engine::stop()
{
    m_edit->getTransport().stop(false, false);
    m_edit->getTransport().setPosition(tracktion::TimePosition{});
    m_transport_position.store(0.0, std::memory_order_relaxed);
}

void Engine::pause()
{
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position — that is the semantic difference from stop().
    m_edit->getTransport().stop(false, false);
}

void Engine::seek(double seconds)
{
    m_edit->getTransport().setPosition(tracktion::TimePosition::fromSeconds(seconds));
    m_transport_position.store(seconds, std::memory_order_relaxed);
}

bool Engine::isPlaying() const
{
    return m_edit->getTransport().isPlaying();
}

double Engine::getTransportPosition() const noexcept
{
    return m_transport_position.load(std::memory_order_relaxed);
}

void Engine::updateTransportPositionCache()
{
    const double pos = m_edit->getTransport().getPosition().inSeconds();
    m_transport_position.store(pos, std::memory_order_relaxed);
}

std::unique_ptr<Thumbnail> Engine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<Thumbnail>(*m_engine, owner);
}

} // namespace rock_hero::audio
