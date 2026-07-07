#include "main_window/editor.h"

#include "main_window/editor_view.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Maps the UI-level project operation bundle into the controller bundle that owns the behavior.
[[nodiscard]] core::EditorController::ProjectOperations controllerProjectOperationsFrom(
    Editor::ProjectOperations project_operations)
{
    return core::EditorController::ProjectOperations{
        .open_function = std::move(project_operations.open_function),
        .import_function = std::move(project_operations.import_function),
        .save_function = std::move(project_operations.save_function),
        .save_as_function = std::move(project_operations.save_as_function),
        .publish_function = std::move(project_operations.publish_function),
    };
}

} // namespace

// Wires the editor with default production project IO operations.
Editor::Editor(
    Editor::AudioPorts audio_ports, Editor::Services services, Editor::ExitFunction exit_function)
    : Editor(audio_ports, services, std::move(exit_function), Editor::ProjectOperations{})
{}

// Wires the controller and view after construction dependencies are available.
Editor::Editor(
    Editor::AudioPorts audio_ports, Editor::Services services, Editor::ExitFunction exit_function,
    Editor::ProjectOperations project_operations)
    : m_controller(
          core::EditorController::AudioPorts{
              .transport = audio_ports.transport,
              .song_audio = audio_ports.song_audio,
              .audio_devices = audio_ports.audio_devices,
              .plugin_host = audio_ports.plugin_host,
              .live_rig = audio_ports.live_rig,
              .tone_automation = audio_ports.tone_automation,
              .live_input = audio_ports.live_input,
          },
          core::EditorController::Services{
              .settings = services.settings,
              .task_runner = services.task_runner,
              .message_thread_scheduler = services.message_thread_scheduler,
          },
          std::move(exit_function), controllerProjectOperationsFrom(std::move(project_operations)))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, EditorView::AudioPorts{
                                .transport = audio_ports.transport,
                                .thumbnail_factory = audio_ports.thumbnail_factory,
                                .audio_devices = audio_ports.audio_devices,
                                .meter_source = audio_ports.meter_source,
                                .live_input = audio_ports.live_input,
                                .tone_automation = audio_ports.tone_automation,
                            }))
{
    m_controller.attachView(*m_view);
}

// Clears the controller's view callbacks before destroying the concrete view.
Editor::~Editor()
{
    m_controller.detachView();
    m_view.reset();
}

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
