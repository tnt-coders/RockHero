/*!
\file i_editor_controller.h
\brief Framework-free editor controller contract.
*/

#pragma once

#include <filesystem>
#include <rock_hero/editor/core/editor_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Project-owned boundary for editor user intents.

Concrete implementations translate these plain-English intents into transport, audio, session, and
view updates without exposing JUCE callback types to tests or to non-UI code.
*/
class IEditorController
{
public:
    /*! \brief Destroys the editor-controller interface. */
    virtual ~IEditorController() = default;

    /*!
    \brief Handles a request to open an editor project package.
    \param file Filesystem path selected by the user.
    */
    virtual void onOpenRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to import a song source.
    \param file Filesystem path selected by the user.
    */
    virtual void onImportRequested(std::filesystem::path file) = 0;

    /*! \brief Handles a request to save to the current destination. */
    virtual void onSaveRequested() = 0;

    /*!
    \brief Handles a request to save to a chosen destination.
    \param file Filesystem path selected by the user.
    */
    virtual void onSaveAsRequested(std::filesystem::path file) = 0;

    /*!
    \brief Handles a request to publish a native song package.
    \param file Filesystem path selected by the user.
    */
    virtual void onPublishRequested(std::filesystem::path file) = 0;

    /*! \brief Handles cancellation of a controller-requested Save As destination chooser. */
    virtual void onSaveAsCancelled() = 0;

    /*! \brief Handles a request to close the current project without exiting the app. */
    virtual void onCloseRequested() = 0;

    /*! \brief Handles a request to exit the editor application. */
    virtual void onExitRequested() = 0;

    /*!
    \brief Handles the user's response to an unsaved-changes confirmation prompt.
    \param decision Decision selected by the user.
    */
    virtual void onUnsavedChangesDecision(UnsavedChangesDecision decision) = 0;

    /*! \brief Handles a play/pause button press from the editor UI. */
    virtual void onPlayPausePressed() = 0;

    /*! \brief Handles a stop button press from the editor UI. */
    virtual void onStopPressed() = 0;

    /*!
    \brief Handles a click on a waveform at a normalized horizontal position.
    \param normalized_x Click position normalized to the interval [0, 1].
    */
    virtual void onWaveformClicked(double normalized_x) = 0;

    /*! \brief Handles a request to show the scanned plugin browser. */
    virtual void onPluginBrowserRequested() = 0;

    /*! \brief Handles the plugin browser window closing. */
    virtual void onPluginBrowserClosed() = 0;

    /*! \brief Handles a request to rescan the plugin browser catalog. */
    virtual void onPluginCatalogScanRequested() = 0;

    /*!
    \brief Handles a request to add a selected plugin from the browser.
    \param plugin_id Opaque plugin ID selected by the user.
    */
    virtual void onAddPluginRequested(std::string plugin_id) = 0;

    /*!
    \brief Handles a request to remove a plugin instance from the plugin chain.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    virtual void onRemovePluginRequested(std::string instance_id) = 0;

    /*!
    \brief Handles a request to open a plugin instance editor window.
    \param instance_id Opaque plugin instance ID selected by the user.
    */
    virtual void onOpenPluginRequested(std::string instance_id) = 0;

protected:
    /*! \brief Creates the editor-controller interface. */
    IEditorController() = default;

    /*! \brief Copies the editor-controller interface. */
    IEditorController(const IEditorController&) = default;

    /*! \brief Moves the editor-controller interface. */
    IEditorController(IEditorController&&) = default;

    /*!
    \brief Assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(const IEditorController&) = default;

    /*!
    \brief Move-assigns the editor-controller interface from another interface.
    \return Reference to this editor-controller interface.
    */
    IEditorController& operator=(IEditorController&&) = default;
};

} // namespace rock_hero::editor::core
