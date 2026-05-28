#include "editor.h"

#include "editor_view.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

// Wires the controller and view after construction dependencies are available.
Editor::Editor(AudioPorts audio_ports, core::EditorController::Services services)
    : m_controller(
          core::EditorController::AudioPorts{
              .transport = audio_ports.transport,
              .song_audio = audio_ports.song_audio,
              .audio_devices = audio_ports.audio_devices,
              .plugin_host = audio_ports.plugin_host,
              .live_rig = audio_ports.live_rig,
              .live_input = audio_ports.live_input,
          },
          std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, EditorView::AudioPorts{
                                .transport = audio_ports.transport,
                                .thumbnail_factory = audio_ports.thumbnail_factory,
                                .audio_devices = audio_ports.audio_devices,
                                .meter_source = audio_ports.meter_source,
                                .live_input = audio_ports.live_input,
                            }))
{
    m_controller.attachView(*m_view);
}

// Uses member declaration order for teardown: the view is destroyed before the controller.
Editor::~Editor() = default;

// Exposes the composed JUCE component without exposing controller/view wiring knobs.
juce::Component& Editor::component() noexcept
{
    return *m_view;
}

// Opens a project after construction so app startup can restore the previous session.
void Editor::openProject(std::filesystem::path file)
{
    m_controller.onOpenRequested(std::move(file));
}

// Exposes only the restorable project path, not the controller internals.
std::optional<std::filesystem::path> Editor::currentProjectFile() const
{
    return m_controller.currentProjectFile();
}

// Starts settings-backed project restore after the full view/controller stack is ready.
void Editor::restoreLastOpenProject()
{
    m_controller.restoreLastOpenProject();
}

// Lets the app window close button use the same unsaved-change gate as File > Exit.
void Editor::requestExit()
{
    m_controller.onExitRequested();
}

} // namespace rock_hero::editor::ui
