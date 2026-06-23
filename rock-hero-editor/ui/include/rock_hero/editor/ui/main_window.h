/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::editor::ui
{
// Forward-declared so the window can install the composed editor without exposing UI internals.
class Editor;

/*!
\brief Main application window.

Owns the composed editor UI feature and handles top-level JUCE window presentation.

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
    \brief Creates the window around an already-composed editor feature.
    \param title Text shown in the title bar, typically the app name.
    \param editor Composed editor feature owned by the window. Must not be null.
    \param exit_callback Callback used when guarded editor exit is allowed to continue.
    */
    MainWindow(
        const juce::String& title, std::unique_ptr<Editor> editor, ExitCallback exit_callback);

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

    /*! \brief Restores the previous project through the composed editor workflow. */
    void restoreLastOpenProject();

private:
    bool keyPressed(const juce::KeyPress& key) override;

    // Requests persisted app exit after controller-level guards allow shutdown.
    ExitCallback m_exit_callback;

    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<Editor> m_editor;
};

} // namespace rock_hero::editor::ui
