/*!
\file i_editor_view.h
\brief Framework-free editor view contract.
*/

#pragma once

#include <functional>
#include <rock_hero/editor/core/controller/editor_view_state.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Project-owned boundary for applying editor state and transient effects to a view.

Concrete JUCE implementations receive an already-derived EditorViewState through this interface so
controller tests can run headlessly without JUCE initialization. One-shot effects, such as workflow
errors, stay separate from durable render state.
*/
class IEditorView
{
public:
    /*! \brief Destroys the editor-view interface. */
    virtual ~IEditorView() = default;

    /*!
    \brief Replaces the currently rendered editor state.
    \param state Fully derived editor state to render.
    */
    virtual void setState(const EditorViewState& state) = 0;

    /*!
    \brief Presents a transient workflow error to the user.
    \param message User-facing error message.
    */
    virtual void showError(const std::string& message) = 0;

    /*!
    \brief Runs a callback after the busy overlay has painted once.

    Message-thread-only operations that would otherwise block repaint can use this fence after
    pushing busy state. Concrete views should call the callback asynchronously after the first
    busy-overlay paint, not directly from the paint callback itself. If the view cannot currently
    paint, it should run the callback without waiting indefinitely.

    \param callback Callback to run after the busy overlay paints.
    */
    virtual void runAfterBusyOverlayPainted(std::function<void()> callback) = 0;

    /*!
    \brief Runs a callback after the busy overlay has been removed from the presented view.

    Operations that temporarily reveal the editor behind another window can use this fence after
    clearing busy state. Concrete views should call the callback asynchronously after the first
    non-busy repaint, not directly from the paint callback itself. If the view cannot currently
    paint, it should run the callback without waiting indefinitely.

    \param callback Callback to run after the editor repaints without the busy overlay.
    */
    virtual void runAfterBusyOverlayRemoved(std::function<void()> callback) = 0;

protected:
    /*! \brief Creates the editor-view interface. */
    IEditorView() = default;

    /*! \brief Copies the editor-view interface. */
    IEditorView(const IEditorView&) = default;

    /*! \brief Moves the editor-view interface. */
    IEditorView(IEditorView&&) = default;

    /*!
    \brief Assigns the editor-view interface from another interface.
    \return Reference to this editor-view interface.
    */
    IEditorView& operator=(const IEditorView&) = default;

    /*!
    \brief Move-assigns the editor-view interface from another interface.
    \return Reference to this editor-view interface.
    */
    IEditorView& operator=(IEditorView&&) = default;
};

} // namespace rock_hero::editor::core
