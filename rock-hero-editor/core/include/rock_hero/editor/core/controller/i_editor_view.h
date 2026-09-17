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
    \brief Presents a one-shot notice the user dismisses.

    The sibling of \ref showError for news that is not a failure — an open that had to normalize a
    chart says what it changed and where. One-shot like an error, and deliberately not a view-state
    field: the notice belongs to the moment of the open, not to the durable render state.

    Caution register, not FYI: every notice this channel carries reports something a load settled
    on the user's behalf without asking — data repaired, or a step it could not complete — and the
    user has to act on it before saving over the original. Views present it as a warning. There is
    no purely informational caller; adding one means deciding the register here rather than quietly
    borrowing this channel's.

    \param title Short window title naming the event.
    \param message User-facing notice body; may span several lines.
    */
    virtual void showNotice(const std::string& title, const std::string& message) = 0;

    /*!
    \brief Asks the view to offer the harmonic node picker: one row per node a selected note's label
    names, lowest partial first, the node it already touches ticked, led by a "No harmonic" row
    where a clear is among the changes.

    A one-shot request rather than view state, because the choice is the CONTROLLER's to ask for:
    `H` reaches it only after the settle prologue has committed any pending fret entry, so the rows
    always describe the chart the choice will land on, and a press that allows exactly one change —
    a lone node, a clear with nowhere else to move — is answered by the controller and never shows a
    menu. With no view attached the question is dropped like a notice and the press does nothing.
    The view presents the rows as a popup at the named note's head with the row the controller names
    preselected (\ref ChartHarmonicNodePicker::preselected), and returns the chosen row's answer — a
    partial, or none for the clear row — through
    \ref IEditorController::onChartHarmonicNodeRequested; dismissing chooses nothing and leaves the
    chart, and anything another verb has staged, untouched.

    \param picker The note the rows describe, the rows in the order to show them, and which opens
    selected.
    */
    virtual void showChartHarmonicNodePicker(ChartHarmonicNodePicker picker) = 0;

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
