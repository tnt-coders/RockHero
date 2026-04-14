#include <rock_hero_audio/audio_engine.h>

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace rock_hero
{

AudioEngine::AudioEngine() : m_engine(std::make_unique<te::Engine>("RockHero"))
{
    // Stereo output, no input for now (ASIO guitar input comes later).
    m_engine->getDeviceManager().initialise(0, 2);

    // createSingleTrackEdit creates an Edit with one AudioTrack ready for clips.
    // createSingleTrackEdit already provides one AudioTrack.
    m_edit = te::Edit::createSingleTrackEdit(*m_engine);
}

AudioEngine::~AudioEngine()
{
    if (m_edit)
    {
        m_edit->getTransport().stop(false, false);
    }

    m_edit.reset();
    m_engine.reset();
}

bool AudioEngine::loadFile(const juce::File& file)
{
    // Stop playback before mutating clips to avoid mid-stream graph rebuilds.
    m_edit->getTransport().stop(false, false);

    if (!file.existsAsFile())
    {
        return false;
    }

    auto* track = te::getAudioTracks(*m_edit)[0];
    if (track == nullptr)
    {
        return false;
    }

    // Remove any existing clips on this track.
    for (auto* clip : track->getClips())
    {
        clip->removeFromParent();
    }

    te::AudioFile audio_file(*m_engine, file);
    if (!audio_file.isValid())
    {
        return false;
    }

    const auto length = te::TimeDuration::fromSeconds(audio_file.getLength());
    const auto start = te::TimePosition{};
    const te::ClipPosition position{.time = {start, start + length}, .offset = te::TimeDuration{}};

    const auto clip =
        track->insertWaveClip(file.getFileNameWithoutExtension(), file, position, false);
    if (clip == nullptr)
    {
        return false;
    }

    auto& transport = m_edit->getTransport();
    transport.looping = false;
    transport.setPosition(te::TimePosition{});

    m_transport_position.store(0.0, std::memory_order_relaxed);
    return true;
}

void AudioEngine::play()
{
    m_edit->getTransport().play(false);
}

void AudioEngine::stop()
{
    m_edit->getTransport().stop(false, false);
    m_edit->getTransport().setPosition(te::TimePosition{});
    m_transport_position.store(0.0, std::memory_order_relaxed);
}

void AudioEngine::pause()
{
    // stop(false, false): do not discard recording, do not clear recordings.
    // Does not reset transport position — that is the semantic difference from stop().
    m_edit->getTransport().stop(false, false);
}

void AudioEngine::seek(double seconds)
{
    m_edit->getTransport().setPosition(te::TimePosition::fromSeconds(seconds));
    m_transport_position.store(seconds, std::memory_order_relaxed);
}

bool AudioEngine::isPlaying() const
{
    return m_edit->getTransport().isPlaying();
}

double AudioEngine::getTransportPosition() const noexcept
{
    return m_transport_position.load(std::memory_order_relaxed);
}

void AudioEngine::updateTransportPositionCache()
{
    const double pos = m_edit->getTransport().getPosition().inSeconds();
    m_transport_position.store(pos, std::memory_order_relaxed);
}

tracktion::Engine& AudioEngine::getEngine() noexcept
{
    return *m_engine;
}

} // namespace rock_hero
