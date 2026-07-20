/*!
\file editor_command_id.h
\brief Stable editor command identifiers for the keybind registry.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Stable identifier for one editor command in the keybind registry.

Values are the persistence contract: the keymap XML keys bindings off the hex value of these ids,
so an id, once shipped, is locked forever. Growth conventions (plan 46):

- New commands append new explicit values; never renumber, reuse, or reorder existing ones.
- Within the single command manager a key chord maps to exactly one command; context sensitivity
  is expressed through enablement, never through one chord on two ids.
- Plain letters and digits stay reserved for chart-editing tools and fret entry (the interaction
  grammar); global destructive actions always carry a modifier.
- The interaction-grammar keys (arrows, digits, Esc, Delete, Insert, and the union-matched `+`/`-`
  grid/zoom family) are dispatched by the view's grammar decoder in `EditorView::keyPressed`, not
  by the mapping set; their chords are reserved and must never be assigned as defaults here.
- These ids are presentation-layer command identities, distinct from `core::EditorActionId`
  (controller-action identity); the two enums never converge.
*/
enum class EditorCommandId : int
{
    /*! \brief File > Open... (`Ctrl+O`). */
    OpenProject = 0x1001,

    /*! \brief File > Import... (`Ctrl+Shift+O`). */
    ImportSong = 0x1002,

    /*! \brief File > Save (`Ctrl+S`). */
    SaveProject = 0x1003,

    /*! \brief File > Save As... (`Ctrl+Shift+S`). */
    SaveProjectAs = 0x1004,

    /*! \brief File > Publish... (`Ctrl+Shift+P`). */
    PublishSong = 0x1005,

    /*! \brief File > Close (`Ctrl+W`). */
    CloseProject = 0x1006,

    /*! \brief File > Exit (menu-only; the OS owns `Alt+F4`). */
    ExitEditor = 0x1007,

    /*! \brief Edit > Undo (`Ctrl+Z`; non-rebindable core command). */
    Undo = 0x1101,

    /*! \brief Edit > Redo (`Ctrl+Y`, `Ctrl+Shift+Z`; non-rebindable core command). */
    Redo = 0x1102,

    /*! \brief Toggle transport playback (`Space`; non-rebindable core command). */
    PlayPause = 0x1201,

    /*! \brief View > Show Waveform (menu-only). */
    ToggleWaveform = 0x1301,

    /*! \brief View > Undo History (`F8`). */
    ToggleUndoHistory = 0x1302,

    /*! \brief View > 3D Preview (`F3`). */
    TogglePreview3D = 0x1303,

    /*! \brief Insert a tone-change marker at the playhead (`Ctrl+T`). */
    InsertToneChange = 0x1401,
};

/*!
\brief Converts an editor command id to the JUCE command id it registers under.
\param id Editor command id to convert.
\return JUCE command id carrying the same stable value.
*/
[[nodiscard]] constexpr juce::CommandID toJuceCommandId(EditorCommandId id) noexcept
{
    return static_cast<juce::CommandID>(id);
}

} // namespace rock_hero::editor::ui
