#pragma once

#include <atomic>
#include <memory>

#include <juce_core/juce_core.h>

// Forward declarations — callers need not include Tracktion headers.
namespace tracktion
{
inline namespace engine
{
class Engine;
class Edit;
} // namespace engine
} // namespace tracktion

namespace rock_hero
{

// Isolation layer between Tracktion Engine and the rest of the application.
// All other code depends on this interface, not on Tracktion directly.
// This boundary enables the fallback-to-raw-JUCE strategy: only this file
// and AudioEngine.cpp (plus WaveformDisplay.cpp) ever include Tracktion headers.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    // Load an audio file onto track 0. Replaces any existing clip.
    // Must be called from the UI/message thread.
    bool loadFile(const juce::File& file);

    void play();
    void stop();
    [[nodiscard]] bool isPlaying() const;

    // Transport position in seconds. Lock-free read from any thread.
    // Currently written by the 60 Hz UI timer shim; will be moved to the
    // audio thread callback once ASIO input is wired.
    [[nodiscard]] double getTransportPosition() const noexcept;

    // Called by WaveformDisplay's 60 Hz timer to mirror Tracktion's transport
    // position into the atomic. Delete this once the audio thread owns the write.
    void updateTransportPositionCache();

    // Intentional bounded Tracktion type leak.
    // Only AudioEngine.cpp and WaveformDisplay.cpp may call this.
    [[nodiscard]] tracktion::Engine& getEngine() noexcept;

private:
    std::unique_ptr<tracktion::Engine> m_engine;
    std::unique_ptr<tracktion::Edit> m_edit;
    std::atomic<double> m_transport_position{0.0};
};

} // namespace rock_hero
