/*!
\file main_window.h
\brief Main application window for Rock Hero Editor.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace rock_hero::editor::ui
{
// Forward-declared so the window can install the composed editor without exposing UI internals.
class Editor;

// Forward-declared because the filter is a private implementation header of the UI library.
class ComposedCharacterFilter;

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
    \brief Creates the window around an already-composed editor feature.

    The window owns no exit policy of its own: every exit path delegates to the composed editor,
    which runs the guards and then invokes the quit function supplied to it.

    \param title Text shown in the title bar, typically the app name.
    \param editor Composed editor feature owned by the window. Must not be null.
    */
    MainWindow(const juce::String& title, std::unique_ptr<Editor> editor);

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

    /*!
    \brief Dispatches a key press through the command mapping set, unless a text field is being
    edited.

    Every keybind resolves here: the focused editor declines the keys it does not use and the press
    bubbles up to the window, and a press made while native focus sits on the shell arrives here
    directly. While a text field in the window is being edited no command runs at all, so the
    field keeps what it types and the keys it declines do nothing, except that JUCE's own fallback
    still moves focus on an unclaimed Tab.

    \param key The press JUCE is offering the window.
    \return True when a command consumed the press.
    */
    bool keyPressed(const juce::KeyPress& key) override;

    /*! \brief Requests the same guarded exit workflow used by File > Exit. */
    void requestExit();

    /*! \brief Restores the previous project through the composed editor workflow. */
    void restoreLastOpenProject();

private:
    // Owns the UI component tree installed into the non-owning DocumentWindow content slot.
    std::unique_ptr<Editor> m_editor;

    // Key listener that drops OS-composed characters before the mapping set can match them as
    // chords. Held by pointer because the key-listener list is non-owning, and heap-allocated
    // because the type stays private to the UI library.
    std::unique_ptr<ComposedCharacterFilter> m_composed_character_filter;
};

} // namespace rock_hero::editor::ui
