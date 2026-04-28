/*!
\file i_editor_view.h
\brief Framework-free editor view contract.
*/

#pragma once

#include <rock_hero/ui/editor_view_state.h>

namespace rock_hero::ui
{

/*!
\brief Project-owned boundary for applying derived editor state to a concrete view.

Concrete JUCE implementations receive an already-derived EditorViewState through this interface so
controller tests can run headlessly without JUCE initialization.
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

} // namespace rock_hero::ui
