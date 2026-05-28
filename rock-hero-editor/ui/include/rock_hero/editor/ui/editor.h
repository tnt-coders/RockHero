/*!
\file editor.h
\brief Fully wired editor feature component.
*/

#pragma once

#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <rock_hero/editor/core/editor_controller.h>

namespace rock_hero::common::audio
{
class IAudioDeviceConfiguration;
class IAudioMeterSource;
class ILiveInput;
class ILiveRig;
class IPluginHost;
class ISongAudio;
class IThumbnailFactory;
class ITransport;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::ui
{

class EditorView;

/*!
\brief Owns the editor controller and view as one fully wired feature.

Editor is the composition boundary for the editor UI. It prevents app code from constructing a
controller, view, audio ports, and thumbnail callback as separate half-wired objects. The audio
ports and thumbnail-factory dependency must outlive the editor.
*/
class Editor final
{
public:
    /*!
    \brief Audio ports consumed by the composed editor feature.

    The editor forwards workflow ports to EditorController and display-only ports to EditorView.
    The editor requires the full audio capability set used by Rock Hero Editor. Runtime
    unavailability such as a closed device or inactive input is represented by each port's state,
    not by omitting a port.
    */
    struct AudioPorts final
    {
        /*! \brief Transport port used by controller workflows and view cursor drawing. */
        common::audio::ITransport& transport;

        /*! \brief Song-audio port used for preparation and active arrangement playback. */
        common::audio::ISongAudio& song_audio;

        /*! \brief Factory used during view construction for arrangement waveform rendering. */
        common::audio::IThumbnailFactory& thumbnail_factory;

        /*! \brief Audio-device port used for hardware input/output routing. */
        common::audio::IAudioDeviceConfiguration& audio_devices;

        /*! \brief Plugin-host port used for plugin insertion. */
        common::audio::IPluginHost& plugin_host;

        /*! \brief Live rig port used for tone document save and restore. */
        common::audio::ILiveRig& live_rig;

        /*! \brief Live-input port used for monitoring and calibration. */
        common::audio::ILiveInput& live_input;

        /*! \brief Meter source sampled by the view for continuous level display. */
        const common::audio::IAudioMeterSource& meter_source;
    };

    /*!
    \brief Creates the editor feature and immediately pushes initial state to the view.
    \param audio_ports Required ports used by the composed editor feature.
    \param services Optional controller services used by the composed editor workflow.
    */
    explicit Editor(AudioPorts audio_ports, core::EditorController::Services services = {});

    /*! \brief Releases the composed editor view before controller-owned subscriptions detach. */
    ~Editor();

    /*! \brief Copying is disabled because the view and controller own registrations. */
    Editor(const Editor&) = delete;

    /*! \brief Copy assignment is disabled because the editor has fixed ownership. */
    Editor& operator=(const Editor&) = delete;

    /*! \brief Moving is disabled so component and listener identities stay stable. */
    Editor(Editor&&) = delete;

    /*! \brief Move assignment is disabled so component and listener identities stay stable. */
    Editor& operator=(Editor&&) = delete;

    /*!
    \brief Returns the concrete JUCE component for app composition.
    \return Editor view component owned by this feature wrapper.
    */
    [[nodiscard]] juce::Component& component() noexcept;

    /*!
    \brief Opens an editor project package through the guarded controller workflow.
    \param file Project package path to open.
    */
    void openProject(std::filesystem::path file);

    /*!
    \brief Returns the editor project file that can be reopened on the next launch.
    \return Current `.rhp` project path, or empty when the loaded work has no project file.
    */
    [[nodiscard]] std::optional<std::filesystem::path> currentProjectFile() const;

    /*! \brief Restores the previously open project when settings contain a valid path. */
    void restoreLastOpenProject();

    /*! \brief Requests the same guarded exit workflow used by File > Exit. */
    void requestExit();

private:
    // Controller must be constructed before the view so the view can safely call it.
    core::EditorController m_controller;

    // Concrete private view that renders controller-derived state and emits user intent.
    std::unique_ptr<EditorView> m_view;
};

} // namespace rock_hero::editor::ui
