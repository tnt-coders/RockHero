/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::audio
{
// Forward-declared so the editor window can own the engine without exposing its header here.
class Engine;
} // namespace rock_hero::audio

namespace rock_hero::core
{
// Forward-declared so the app composition root can own the session without exposing its header.
class Session;
} // namespace rock_hero::core

namespace rock_hero::ui
{
// Forward-declared so the window can install the composed editor without exposing UI internals.
class Editor;
} // namespace rock_hero::ui

namespace rock_hero
{

/*!
\brief Main application window.

Owns the concrete audio engine, editable session, and composed editor UI feature.

The destructor clears the DocumentWindow's non-owning content pointer before member destruction
to avoid dangling-pointer issues during teardown.

\see rock_hero::audio::Engine
\see rock_hero::core::Session
\see rock_hero::ui::Editor
*/
class MainWindow : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the window, its audio::Engine, session, and editor component.
    \param title Text shown in the title bar, typically the app name.
    */
    explicit MainWindow(const juce::String& title);

    /*! \brief Clears the content component pointer before destroying owned members. */
    ~MainWindow() override;

    /*! \brief Copying is disabled because JUCE windows and owned runtime state are not copyable. */
    MainWindow(const MainWindow&) = delete;

    /*! \brief Copy assignment is disabled because JUCE windows and owned runtime state are not
     * copyable. */
    MainWindow& operator=(const MainWindow&) = delete;

    /*! \brief Moving is disabled because JUCE windows and owned runtime state are not movable. */
    MainWindow(MainWindow&&) = delete;

    /*! \brief Move assignment is disabled because JUCE windows and owned runtime state are not
     * movable. */
    MainWindow& operator=(MainWindow&&) = delete;

    /*! \brief Requests application quit when the user closes the window. */
    void closeButtonPressed() override;

private:
    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<audio::Engine> m_audio_engine;

    // Owns the framework-free editor session state projected by the controller.
    std::unique_ptr<core::Session> m_session;

    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<ui::Editor> m_editor;
};

} // namespace rock_hero
