#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero
{

class AudioEngine;

// Main application window. Owns the AudioEngine and all UI components.
class EditorWindow : public juce::DocumentWindow
{
public:
    explicit EditorWindow(juce::String title);
    ~EditorWindow() override;

    void closeButtonPressed() override;

private:
    struct ContentComponent;

    std::unique_ptr<AudioEngine> m_audio_engine;
    std::unique_ptr<ContentComponent> m_content;
};

} // namespace rock_hero
