#include "main_window.h"

#include <rock_hero_ui/transport_controls.h>
#include <rock_hero_ui/waveform_display.h>

#include <rock_hero_audio/audio_engine.h>

namespace rock_hero
{

struct MainWindow::ContentComponent : public juce::Component, public juce::KeyListener
{
    explicit ContentComponent(AudioEngine& engine)
        : m_audio_engine(engine), m_waveform_display(engine)
    {
        addAndMakeVisible(m_load_button);
        addAndMakeVisible(m_transport_controls);
        addAndMakeVisible(m_waveform_display);

        m_load_button.setButtonText("Load File...");
        m_load_button.onClick = [this] { OnLoadClicked(); };

        m_transport_controls.on_play = [this] {
            m_audio_engine.play();
            m_transport_controls.setPlaying(true);
        };

        m_transport_controls.on_pause = [this] {
            m_audio_engine.pause();
            m_transport_controls.setPlaying(false);
        };

        m_transport_controls.on_stop = [this] {
            m_audio_engine.stop();
            m_transport_controls.setPlaying(false);
        };

        m_waveform_display.on_seek = [this](double seconds) { m_audio_engine.seek(seconds); };

        setSize(800, 300);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto button_row = area.removeFromTop(32);
        m_load_button.setBounds(button_row.removeFromLeft(120));
        button_row.removeFromLeft(8);
        m_transport_controls.setBounds(button_row.removeFromLeft(176));
        area.removeFromTop(8);
        m_waveform_display.setBounds(area);
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component* /*origin*/) override
    {
        if (key == juce::KeyPress::spaceKey && m_transport_controls.isFileLoaded())
        {
            m_transport_controls.onPlayPauseClicked();
            return true;
        }
        return false;
    }

private:
    void OnLoadClicked()
    {
        // Keep the chooser alive for the duration of the async native dialog.
        m_file_chooser = std::make_unique<juce::FileChooser>(
            "Select an audio file",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.aiff;*.ogg;*.flac");

        m_file_chooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser) {
                const auto file = chooser.getResult();
                if (file.existsAsFile())
                {
                    // Only refresh the thumbnail after the engine has accepted the file.
                    // That keeps the UI from displaying a waveform for a file that failed to load.
                    if (m_audio_engine.loadFile(file))
                    {
                        m_waveform_display.setAudioFile(file);
                        m_transport_controls.setFileLoaded(true);
                        m_transport_controls.setPlaying(false); // engine stopped during load
                    }
                }
            });
    }

    AudioEngine& m_audio_engine;
    WaveformDisplay m_waveform_display;
    TransportControls m_transport_controls;
    juce::TextButton m_load_button;
    std::unique_ptr<juce::FileChooser> m_file_chooser;
};

MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons),
      m_audio_engine(std::make_unique<AudioEngine>()),
      m_content(std::make_unique<ContentComponent>(*m_audio_engine))
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(m_content.get(), true);
    setResizable(true, false);
    centreWithSize(800, 300);
    setVisible(true);

    // Register the content component as a key listener on this window so Space is
    // handled whenever any child button (or the file chooser's return) owns focus.
    addKeyListener(m_content.get());
}

MainWindow::~MainWindow()
{
    removeKeyListener(m_content.get());
    // Null out DocumentWindow's non-owning pointer before m_content is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace rock_hero
