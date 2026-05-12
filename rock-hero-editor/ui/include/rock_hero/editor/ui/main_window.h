/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <filesystem>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace rock_hero::common::audio
{
// Forward-declared so the editor window can own the engine without exposing its header here.
class Engine;
} // namespace rock_hero::common::audio

namespace rock_hero::editor::ui
{
// Forward-declared so the window can install the composed editor without exposing UI internals.
class Editor;

// Forward-declared so the window can persist app-local editor settings privately.
class EditorSettings;

/*!
\brief Main application window.

Owns the concrete audio engine and composed editor UI feature.

The destructor clears the DocumentWindow's non-owning content pointer before member destruction
to avoid dangling-pointer issues during teardown.

\see rock_hero::common::audio::Engine
\see rock_hero::editor::ui::Editor
*/
class MainWindow : public juce::DocumentWindow
{
public:
    /*!
    \brief Creates the window, its shared audio engine, and editor component.
    \param title Text shown in the title bar, typically the app name.
    */
    explicit MainWindow(const juce::String& title);

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

private:
    // Restores the previous editor project when the stored path still points at a file.
    void restoreLastOpenProject();

    // Requests normal JUCE application shutdown after saving app-local restore state.
    void closeWindow(std::optional<std::filesystem::path> project_file);

    // Owns editor app settings that live outside project packages.
    std::unique_ptr<EditorSettings> m_settings;

    // Owns Tracktion-backed playback for the lifetime of the editor window.
    std::unique_ptr<rock_hero::common::audio::Engine> m_audio_engine;

    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<Editor> m_editor;
};

} // namespace rock_hero::editor::ui
