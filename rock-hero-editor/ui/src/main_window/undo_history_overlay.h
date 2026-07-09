/*!
\file undo_history_overlay.h
\brief Floating panel that lists the whole undo/redo stack and updates in real time.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Informational overlay that renders the editor's undo/redo stack.

Refreshed on every controller push so the user can watch history change and see where the undo
cursor and the saved (clean) revision sit. It never intercepts pointer input, and the editor shows
it only while the user has toggled it on (View > Undo History, or F8).
*/
class UndoHistoryOverlay final : public juce::Component
{
public:
    /*! \brief Creates the overlay as a click-through, initially empty panel. */
    UndoHistoryOverlay();

    /*!
    \brief Replaces the displayed stack, repainting only when the contents changed.
    \param history Latest undo/redo stack snapshot derived by the controller.
    */
    void setHistory(const core::UndoHistoryState& history);

    /*! \brief Paints the panel background, a summary line, and the entry list. */
    void paint(juce::Graphics& graphics) override;

private:
    // Latest stack snapshot rendered by paint(); replaced wholesale by setHistory().
    core::UndoHistoryState m_history;
};

} // namespace rock_hero::editor::ui
