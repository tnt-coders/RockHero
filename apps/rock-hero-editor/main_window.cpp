#include "main_window.h"

#include <rock_hero/audio/engine.h>
#include <rock_hero/core/audio_asset.h>
#include <rock_hero/ui/transport_controls.h>
#include <rock_hero/ui/waveform_display.h>

namespace rock_hero
{

// Editor content component that owns load controls, transport controls, and waveform display.
struct MainWindow::ContentComponent : public juce::Component,
                                      public juce::KeyListener,
                                      private ui::TransportControls::Listener,
                                      private audio::Engine::Listener
{
    // Wires editor controls directly to the audio engine while editor command services are absent.
    explicit ContentComponent(audio::Engine& engine)
        : m_audio_engine(engine)
        , m_waveform_display(engine)
        , m_transport_controls(*this)
        , m_engine_listener(engine, *this)
    {
        addAndMakeVisible(m_load_button);
        addAndMakeVisible(m_transport_controls);
        addAndMakeVisible(m_waveform_display);

        m_load_button.setButtonText("Load File...");
        m_load_button.onClick = [this] { onLoadClicked(); };

        m_waveform_display.setOnSeek([this](double seconds) { m_audio_engine.seek(seconds); });

        setSize(800, 300);
    }

    // Mirrors engine playing state into the transport button icon.
    void enginePlayingStateChanged(bool playing) override
    {
        m_transport_controls_state.play_pause_shows_pause_icon = playing;
        updateTransportControlsState();
    }

    // Mirrors engine cursor state into the transport controls for Stop-button gating.
    void engineTransportPositionChanged(double seconds) override
    {
        m_transport_position = seconds;
        updateTransportControlsState();
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
        if (key == juce::KeyPress::spaceKey && m_transport_controls_state.play_pause_enabled)
        {
            onPlayPausePressed();
            return true;
        }
        return false;
    }

private:
    // Temporarily derives widget state from the concrete engine until EditorView lands.
    void updateTransportControlsState()
    {
        m_transport_controls_state.play_pause_enabled = m_file_loaded;
        m_transport_controls_state.stop_enabled =
            m_file_loaded &&
            (m_transport_controls_state.play_pause_shows_pause_icon || m_transport_position > 0.0);
        m_transport_controls.setState(m_transport_controls_state);
    }

    // Maps the transport-controls play/pause intent directly to the concrete engine for now.
    void onPlayPausePressed() override
    {
        if (m_transport_controls_state.play_pause_shows_pause_icon)
        {
            m_audio_engine.pause();
            return;
        }
        m_audio_engine.play();
    }

    // Maps the transport-controls stop intent directly to the concrete engine for now.
    void onStopPressed() override
    {
        m_audio_engine.stop();
    }

    // Opens an asynchronous native file chooser and loads the selected audio file into the engine.
    void onLoadClicked()
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
                    const core::AudioAsset audio_asset{
                        std::filesystem::path{file.getFullPathName().toWideCharPointer()}
                    };

                    // Only refresh the thumbnail after the engine has accepted the file.
                    // That keeps the UI from displaying a waveform for a file that failed to load.
                    if (m_audio_engine.loadFile(file))
                    {
                        m_waveform_display.setAudioSource(audio_asset);
                        m_file_loaded = true;
                        updateTransportControlsState();
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

    // Referenced engine owns playback and outlives the content component in MainWindow.
    audio::Engine& m_audio_engine;

    // Displays the loaded audio file and sends user seek intent back to the engine.
    ui::WaveformDisplay m_waveform_display;

    // Presents playback controls while a temporary shim derives widget state from Engine.
    ui::TransportControls m_transport_controls;

    // Opens the asynchronous file chooser used to pick a backing track.
    juce::TextButton m_load_button;

    // Owned by the component so the asynchronous native file dialog remains alive.
    std::unique_ptr<juce::FileChooser> m_file_chooser;

    // Temporary state projection used to adapt the old editor wiring to the stage-09 widget API.
    ui::TransportControlsState m_transport_controls_state{};

    // Tracks whether the current editor session has successfully loaded an audio file.
    bool m_file_loaded{false};

    // Cached engine transport position used only to derive temporary stop-button enabledness.
    double m_transport_position{0.0};

    // Declared last so its destructor detaches the listener before other members are destroyed.
    audio::ScopedListener<audio::Engine, audio::Engine::Listener> m_engine_listener;
};

// Owns the editor audio engine before creating content that stores references to it.
MainWindow::MainWindow(const juce::String& title)
    : juce::DocumentWindow(
          title,
          juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
              juce::ResizableWindow::backgroundColourId),
          juce::DocumentWindow::allButtons)
    , m_audio_engine(std::make_unique<audio::Engine>())
    , m_content(std::make_unique<ContentComponent>(*m_audio_engine))
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
