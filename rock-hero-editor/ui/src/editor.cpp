#include "editor.h"

#include "editor_view.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Editor composition often receives one Engine object through several narrow ports. When that
// same object also exposes meter snapshots, wire the optional display-only meter source to the
// view without widening the controller dependency.
[[nodiscard]] const common::audio::IAudioMeterSource* meterSourceFrom(
    const common::audio::ITransport& transport) noexcept
{
    return dynamic_cast<const common::audio::IAudioMeterSource*>(&transport);
}

} // namespace

// Wires the controller and view after construction dependencies are available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    common::audio::IThumbnailFactory& thumbnail_factory, core::EditorController::Services services)
    : m_controller(transport, audio, audio_devices, plugin_host, live_rig, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, &audio_devices,
              meterSourceFrom(transport)))
{
    m_controller.attachView(*m_view);
}

// Wires the controller and view when no persistent tone backend is available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IPluginHost& plugin_host, common::audio::IThumbnailFactory& thumbnail_factory,
    core::EditorController::Services services)
    : m_controller(transport, audio, audio_devices, plugin_host, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, &audio_devices,
              meterSourceFrom(transport)))
{
    m_controller.attachView(*m_view);
}

// Wires the controller and view when no plugin-host backend is available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IAudioDeviceConfiguration& audio_devices,
    common::audio::IThumbnailFactory& thumbnail_factory, core::EditorController::Services services)
    : m_controller(transport, audio, audio_devices, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, &audio_devices,
              meterSourceFrom(transport)))
{
    m_controller.attachView(*m_view);
}

// Wires the controller and view when no audio-device backend is available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IPluginHost& plugin_host, common::audio::ILiveRig& live_rig,
    common::audio::IThumbnailFactory& thumbnail_factory, core::EditorController::Services services)
    : m_controller(transport, audio, plugin_host, live_rig, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, nullptr, meterSourceFrom(transport)))
{
    m_controller.attachView(*m_view);
}

// Wires the controller and view when no audio-device backend or persistent tone storage exists.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IPluginHost& plugin_host, common::audio::IThumbnailFactory& thumbnail_factory,
    core::EditorController::Services services)
    : m_controller(transport, audio, plugin_host, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, nullptr, meterSourceFrom(transport)))
{
    m_controller.attachView(*m_view);
}

// Wires the controller and view when no audio-device or plugin-host backend is available.
Editor::Editor(
    common::audio::ITransport& transport, common::audio::IAudio& audio,
    common::audio::IThumbnailFactory& thumbnail_factory, core::EditorController::Services services)
    : m_controller(transport, audio, std::move(services))
    , m_view(
          std::make_unique<EditorView>(
              m_controller, transport, thumbnail_factory, nullptr, meterSourceFrom(transport)))
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
