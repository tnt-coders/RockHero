#include "main_window.h"

#include <rock_hero/audio/engine.h>
#include <rock_hero/ui/transport_controls.h>
#include <rock_hero/ui/waveform_display.h>

namespace rock_hero
{

// Editor content component that owns load controls, transport controls, and waveform display.
struct MainWindow::ContentComponent : public juce::Component,
                                      public juce::KeyListener,
                                      private audio::Engine::Listener
{
    // Wires editor controls directly to the audio engine while editor command services are absent.
    explicit ContentComponent(audio::Engine& engine)
        : m_audio_engine(engine), m_waveform_display(engine), m_engine_listener(engine, *this)
    {
        addAndMakeVisible(m_load_button);
        addAndMakeVisible(m_transport_controls);
        addAndMakeVisible(m_waveform_display);

        m_load_button.setButtonText("Load File...");
        m_load_button.onClick = [this] { OnLoadClicked(); };

        // Engine's playing-state events drive m_transport_controls.setPlaying() via
        // enginePlayingStateChanged(), so these handlers only forward the user intent.
        m_transport_controls.on_play = [this] { m_audio_engine.play(); };
        m_transport_controls.on_pause = [this] { m_audio_engine.pause(); };
        m_transport_controls.on_stop = [this] { m_audio_engine.stop(); };

        m_waveform_display.on_seek = [this](double seconds) { m_audio_engine.seek(seconds); };

        setSize(800, 300);
    }

    // Mirrors engine playing state into the transport button icon.
    void enginePlayingStateChanged(bool playing) override
    {
        m_transport_controls.setPlaying(playing);
    }

    // Mirrors engine cursor state into the transport controls for Stop-button gating.
    void engineTransportPositionChanged(double seconds) override
    {
        m_transport_controls.setTransportPosition(seconds);
    }

    // Keeps the editor controls in a compact top row and gives remaining space to the waveform.
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

    // Shares Space-bar playback toggling with the transport button path when a file is loaded.
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
    // Opens an asynchronous native file chooser and loads the selected audio file into the engine.
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
                    }
                    else
                    {
                        juce::NativeMessageBox::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                .withTitle("Could not load file")
                                .withMessage(
                                    "The selected file could not be loaded. It may be "
                                    "corrupt or in an unsupported format:\n\n" +
                                    file.getFullPathName())
                                .withButton("OK"),
                            nullptr);
                    }
                }
            });
    }

    audio::Engine& m_audio_engine;
    ui::WaveformDisplay m_waveform_display;
    ui::TransportControls m_transport_controls;
    juce::TextButton m_load_button;
    // Owned by the component so the asynchronous native file dialog remains alive.
    std::unique_ptr<juce::FileChooser> m_file_chooser;
    // Declared last so its destructor detaches the listener before other members are destroyed.
    audio::ScopedListener<audio::Engine, audio::Engine::Listener> m_engine_listener;
};

// Owns the editor audio engine before creating content that stores references to it.
MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons),
      m_audio_engine(std::make_unique<audio::Engine>()),
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

// Removes JUCE's non-owning pointers before owned content and engine members are destroyed.
MainWindow::~MainWindow()
{
    removeKeyListener(m_content.get());
    // Null out DocumentWindow's non-owning pointer before m_content is destroyed.
    // Otherwise ~ResizableWindow would call removeChildComponent on a dangling pointer.
    clearContentComponent();
}

// Routes the native close button through JUCE so normal application shutdown runs.
void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace rock_hero
