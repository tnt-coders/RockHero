/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::audio
{
// Forward-declared so the editor window can own the engine without exposing its header here.
class Engine;
} // namespace rock_hero::audio

namespace rock_hero
{

/*!
\brief Main application window.

Owns the audio::Engine and a ContentComponent that provides the load/play controls and waveform
display. ContentComponent is a private nested struct defined in main_window.cpp.

The destructor clears the DocumentWindow's non-owning content pointer before member destruction
to avoid dangling-pointer issues during teardown.

\see rock_hero::audio::Engine
\see rock_hero::ui::WaveformDisplay
*/
class MainWindow : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the window, its audio::Engine, and the content component.
    \param title Text shown in the title bar, typically the app name.
    */
    explicit MainWindow(const juce::String& title);

    /*! \brief Clears the content component pointer before destroying owned members. */
    ~MainWindow() override;

    /*! \brief Copying is disabled because JUCE windows and owned runtime state are not copyable. */
    MainWindow(const MainWindow&) = delete;

    /*! \brief Copy assignment is disabled because JUCE windows and owned runtime state are not
     * copyable. */
    MainWindow& operator=(const MainWindow&) = delete;

    /*! \brief Moving is disabled because JUCE windows and owned runtime state are not movable. */
    MainWindow(MainWindow&&) = delete;

    /*! \brief Move assignment is disabled because JUCE windows and owned runtime state are not
     * movable. */
    MainWindow& operator=(MainWindow&&) = delete;

    /*! \brief Requests application quit when the user closes the window. */
    void closeButtonPressed() override;

private:
    // Private content component keeps JUCE child widgets out of the public header.
    struct ContentComponent;

    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<audio::Engine> m_audio_engine;

    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<ContentComponent> m_content;
};

} // namespace rock_hero
