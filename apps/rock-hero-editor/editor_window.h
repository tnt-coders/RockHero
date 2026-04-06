/** @file editor_window.h
    @brief Main application window for Rock Hero.
*/

#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class AudioEngine;

/** Main application window.

    Owns the AudioEngine and a ContentComponent that provides the load/play controls and waveform
    display. ContentComponent is a private nested struct defined in editor_window.cpp.

    The destructor clears the DocumentWindow's non-owning content pointer before member destruction
    to avoid dangling-pointer issues during teardown.

    @see AudioEngine, WaveformDisplay
*/
class EditorWindow : public juce::DocumentWindow
{
public:
    /** Creates the window, its AudioEngine, and the content component.
        @param title  text shown in the title bar (typically the app name)
    */
    explicit EditorWindow(const juce::String& title);

    /** Clears the content component pointer before destroying owned members. */
    ~EditorWindow() override;

    /** Requests application quit when the user closes the window. */
    void closeButtonPressed() override;

private:
    struct ContentComponent;

    std::unique_ptr<AudioEngine> m_audio_engine;
    std::unique_ptr<ContentComponent> m_content;
};

} // namespace rock_hero
