/*!
\file waveform_display.h
\brief Waveform rendering component with a scrolling playhead cursor.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/audio/engine.h>

namespace rock_hero::audio
{
// Forward-declared thumbnail port keeps this header independent of Tracktion implementation types.
class Thumbnail;
} // namespace rock_hero::audio

namespace rock_hero::ui
{

/*!
\brief Displays the waveform of the loaded audio file with a scrolling playhead.

Delegates thumbnail proxy generation and drawing to audio::Thumbnail, which keeps all Tracktion
types out of this library. Transport cursor updates arrive through audio::Engine::Listener;
thumbnail loading repaints are requested by the thumbnail adapter.

\see rock_hero::audio::Engine
\see rock_hero::audio::Thumbnail
*/
class WaveformDisplay : public juce::Component, private audio::Engine::Listener
{
public:
    /*!
    \brief Creates the waveform display and attaches a listener for transport position changes.
    \param engine The audio engine whose transport state drives the cursor.
    */
    explicit WaveformDisplay(audio::Engine& engine);

    /*! \brief Detaches the listener and releases resources. */
    ~WaveformDisplay() override;

    /*! \brief Copying is disabled because JUCE components and listener state are not copyable. */
    WaveformDisplay(const WaveformDisplay&) = delete;

    /*! \brief Copy assignment is disabled because JUCE components and listener state are not
     * copyable. */
    WaveformDisplay& operator=(const WaveformDisplay&) = delete;

    /*! \brief Moving is disabled because JUCE components and listener state are not movable. */
    WaveformDisplay(WaveformDisplay&&) = delete;

    /*! \brief Move assignment is disabled because JUCE components and listener state are not
     * movable. */
    WaveformDisplay& operator=(WaveformDisplay&&) = delete;

    /*!
    \brief Updates the thumbnail source after a new file has been loaded.
    \param file The audio file that was successfully loaded into the engine.
    */
    void setAudioFile(const juce::File& file);

    /*!
    \brief Stores the callback fired with the target position in seconds when the waveform is
    clicked.
    \param on_seek Callback invoked for seek intent.
    */
    void setOnSeek(std::function<void(double)> on_seek);

    /*!
    \brief Seeks to the clicked position and fires the stored seek callback.
    \param event The mouse event from the click.
    */
    void mouseDown(const juce::MouseEvent& event) override;

    /*!
    \brief Draws the waveform thumbnail and the playhead cursor line.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

    /*! \brief Responds to component size changes. */
    void resized() override;

private:
    // Receives transport events and converts them into cursor repaint requests.
    void engineTransportPositionChanged(double seconds) override;

    // Callback fired when the user clicks the waveform and a valid seek target can be derived.
    std::function<void(double)> m_on_seek{};

    // Tracktion-free thumbnail interface created by the audio engine adapter.
    std::unique_ptr<audio::Thumbnail> m_thumbnail;

    // Normalised cursor position in the range [0, 1].
    double m_cursor_proportion{0.0};

    // Declared last so its destructor detaches the listener before other members are destroyed.
    audio::ScopedListener<audio::Engine, audio::Engine::Listener> m_engine_listener;
};

} // namespace rock_hero::ui
