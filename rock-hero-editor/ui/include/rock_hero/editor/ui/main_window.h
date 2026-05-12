/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::common::audio
{
// Forward-declared so the window can receive audio ports without exposing their headers here.
class IAudio;
class IThumbnailFactory;
class ITransport;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::core
{
// Forward-declared so the window can pass settings into the editor workflow without owning them.
class EditorSettings;
} // namespace rock_hero::editor::core

namespace rock_hero::editor::ui
{
// Forward-declared so the window can install the composed editor without exposing UI internals.
class Editor;

/*!
\brief Main application window.

Owns the composed editor UI feature and receives its concrete runtime dependencies from the app
shell.

The destructor clears the DocumentWindow's non-owning content pointer before member destruction
to avoid dangling-pointer issues during teardown.

\see rock_hero::editor::ui::Editor
*/
class MainWindow : public juce::DocumentWindow
{
public:
    /*!
    \brief Callback used when guarded editor exit is allowed to continue.
    */
    using ExitCallback = std::function<void()>;

    /*!
    \brief Creates the window around already-composed editor runtime ports.
    \param title Text shown in the title bar, typically the app name.
    \param transport Transport used by the editor controller and cursor overlay.
    \param audio Audio port used by the editor controller.
    \param thumbnail_factory Factory used by the editor view for arrangement waveforms.
    \param settings Settings store used by the editor workflow for restore-state behavior.
    \param exit_callback Callback used when guarded editor exit is allowed to continue.
    */
    MainWindow(
        const juce::String& title, common::audio::ITransport& transport,
        common::audio::IAudio& audio, common::audio::IThumbnailFactory& thumbnail_factory,
        core::EditorSettings& settings, ExitCallback exit_callback);

    /*! \brief Clears the content component pointer before destroying owned members. */
    ~MainWindow() override;

    /*! \brief Copying is disabled because JUCE windows and owned runtime state are fixed. */
    MainWindow(const MainWindow&) = delete;

    /*!
    \brief Copy assignment is disabled because JUCE windows and owned runtime state are not
    copyable.
    */
    MainWindow& operator=(const MainWindow&) = delete;

    /*! \brief Moving is disabled because JUCE windows and owned runtime state are not movable. */
    MainWindow(MainWindow&&) = delete;

    /*!
    \brief Move assignment is disabled because JUCE windows and owned runtime state are not
    movable.
    */
    MainWindow& operator=(MainWindow&&) = delete;

    /*! \brief Requests application quit when the user closes the window. */
    void closeButtonPressed() override;

    /*! \brief Requests the same guarded exit workflow used by File > Exit. */
    void requestExit();

    /*! \brief Restores the previously open project when settings contain a valid path. */
    void restoreLastOpenProject();

private:
    // Requests persisted app exit after controller-level guards allow shutdown.
    ExitCallback m_exit_callback;

    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<Editor> m_editor;
};

} // namespace rock_hero::editor::ui
