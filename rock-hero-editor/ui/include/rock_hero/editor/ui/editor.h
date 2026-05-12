/*!
\file editor.h
\brief Fully wired editor feature component.
*/

#pragma once

#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <rock_hero/common/audio/i_audio.h>
#include <rock_hero/common/audio/i_thumbnail_factory.h>
#include <rock_hero/common/audio/i_transport.h>
#include <rock_hero/editor/core/editor_controller.h>
#include <rock_hero/editor/ui/editor_view.h>

namespace rock_hero::editor::ui
{

/*!
\brief Owns the editor controller and view as one fully wired feature.

Editor is the composition boundary for the editor UI. It prevents app code from constructing a
controller, view, audio port, and thumbnail callback as separate half-wired objects. The referenced
transport, audio port, and thumbnail-factory dependencies must outlive the editor.
*/
class Editor final
{
public:
    /*!
    \brief Creates the editor feature and immediately pushes initial state to the view.
    \param transport Transport used by the controller and read by the view cursor overlay.
    \param audio Audio port used by the controller for song preparation and arrangement activation.
    \param thumbnail_factory Factory used during view construction for arrangement waveform.
    \param services Optional controller services used by the composed editor workflow.
    */
    Editor(
        common::audio::ITransport& transport, common::audio::IAudio& audio,
        common::audio::IThumbnailFactory& thumbnail_factory,
        core::EditorController::Services services = {});

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

    // Concrete view that renders controller-derived state and emits user intent.
    EditorView m_view;
};

} // namespace rock_hero::editor::ui
