/*!
\file editor.h
\brief Fully wired editor feature component.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/audio/i_edit.h>
#include <rock_hero/audio/i_thumbnail_factory.h>
#include <rock_hero/audio/i_transport.h>
#include <rock_hero/ui/edit_coordinator.h>
#include <rock_hero/ui/editor_controller.h>
#include <rock_hero/ui/editor_view.h>

namespace rock_hero::ui
{

/*!
\brief Owns the editor controller and view as one fully wired feature.

Editor is the composition boundary for the editor UI. It prevents app code from constructing a
controller, view, edit coordinator, and thumbnail callback as separate half-wired objects. The
internal edit coordinator owns the editor session so app code cannot bypass coordinated edits.
The referenced transport, edit port, and thumbnail-factory dependencies must outlive the editor.
*/
class Editor final
{
public:
    /*!
    \brief Creates the editor feature and immediately pushes initial state to the view.
    \param transport Transport used by the controller and read by the view cursor overlay.
    \param edit Audio edit port used by the internal coordinator for cross-boundary edits.
    \param thumbnail_factory Factory used during view construction for the initial track row.
    */
    Editor(
        audio::ITransport& transport, audio::IEdit& edit,
        audio::IThumbnailFactory& thumbnail_factory);

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

private:
    // Owns edit orchestration before the controller stores its non-owning reference.
    EditCoordinator m_edit_coordinator;

    // Controller must be constructed before the view so the view can safely call it.
    EditorController m_controller;

    // Concrete view that renders controller-derived state and emits user intent.
    EditorView m_view;
};

} // namespace rock_hero::ui
