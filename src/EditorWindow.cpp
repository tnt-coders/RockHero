#include "EditorWindow.h"

#include "AudioEngine.h"
#include "WaveformDisplay.h"

namespace rock_hero
{

struct EditorWindow::ContentComponent : public juce::Component
{
    explicit ContentComponent(AudioEngine& engine)
        : m_audio_engine(engine), m_waveform_display(engine)
    {
        addAndMakeVisible(m_load_button);
        addAndMakeVisible(m_play_stop_button);
        addAndMakeVisible(m_waveform_display);

        m_load_button.setButtonText("Load File...");
        m_load_button.onClick = [this] { onLoadClicked(); };

        m_play_stop_button.setButtonText("Play");
        m_play_stop_button.onClick = [this] { onPlayStopClicked(); };

        setSize(800, 300);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto button_row = area.removeFromTop(32);
        m_load_button.setBounds(button_row.removeFromLeft(120));
        button_row.removeFromLeft(8);
        m_play_stop_button.setBounds(button_row.removeFromLeft(80));
        area.removeFromTop(8);
        m_waveform_display.setBounds(area);
    }

private:
    void onLoadClicked()
    {
        m_file_chooser = std::make_unique<juce::FileChooser>(
            "Select an audio file", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.aiff;*.ogg;*.flac");

        m_file_chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                        juce::FileBrowserComponent::canSelectFiles,
                                    [this](const juce::FileChooser& chooser) {
                                        const auto file = chooser.getResult();
                                        if (file.existsAsFile())
                                        {
                                            if (m_audio_engine.loadFile(file))
                                                m_waveform_display.setAudioFile(file);
                                        }
                                    });
    }

    void onPlayStopClicked()
    {
        if (m_audio_engine.isPlaying())
        {
            m_audio_engine.stop();
            m_play_stop_button.setButtonText("Play");
        }
        else
        {
            m_audio_engine.play();
            m_play_stop_button.setButtonText("Stop");
        }
    }

    AudioEngine& m_audio_engine;
    WaveformDisplay m_waveform_display;
    juce::TextButton m_load_button;
    juce::TextButton m_play_stop_button;
    std::unique_ptr<juce::FileChooser> m_file_chooser;
};

EditorWindow::EditorWindow(juce::String title)
    : juce::DocumentWindow(title,
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::allButtons),
      m_audio_engine(std::make_unique<AudioEngine>()),
      m_content(std::make_unique<ContentComponent>(*m_audio_engine))
{
    setUsingNativeTitleBar(true);
    setContentOwned(m_content.release(), true);
    setResizable(true, false);
    centreWithSize(800, 300);
    setVisible(true);
}

EditorWindow::~EditorWindow() = default;

void EditorWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace rock_hero
