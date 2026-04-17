#include <rock_hero_audio_engine/audio_engine.h>

#include <rock_hero_audio_engine/audio_thumbnail.h>

#include <tracktion_engine/tracktion_engine.h>

namespace rock_hero
{

AudioEngine::AudioEngine() : m_engine(std::make_unique<tracktion::Engine>("RockHero"))
{
    // Stereo output, no input for now (ASIO guitar input comes later).
    m_engine->getDeviceManager().initialise(0, 2);

    // createSingleTrackEdit creates an Edit with one AudioTrack ready for clips.
    // createSingleTrackEdit already provides one AudioTrack.
    m_edit = tracktion::Edit::createSingleTrackEdit(*m_engine);
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

bool AudioEngine::loadFile(const std::filesystem::path& file)
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

void AudioEngine::play()
{
    m_edit->getTransport().play(false);
}

void AudioEngine::stop()
{
    m_edit->getTransport().stop(false, false);
    m_edit->getTransport().setPosition(tracktion::TimePosition{});
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
    m_edit->getTransport().setPosition(tracktion::TimePosition::fromSeconds(seconds));
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

std::unique_ptr<AudioThumbnail> AudioEngine::createThumbnail(juce::Component& owner)
{
    return std::make_unique<AudioThumbnail>(*m_engine, owner);
}

} // namespace rock_hero
