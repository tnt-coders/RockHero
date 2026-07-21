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
so an id, once shipped, is locked forever. Growth conventions (plan 46; total rebindability per
plan 53 Phase 1b, 2026-07-20):

- New commands append new explicit values; never renumber, reuse, or reorder existing ones. Id
  blocks group by category: 0x1x file/edit/transport/view/tone, 0x15xx navigation, 0x16xx
  selection, 0x17xx authoring, 0x18xx value entry, 0x19xx grid & zoom. Blocks are historical
  hints only — the registry row owns the display category (CancelDismiss, 0x1708, lists under
  Selection).
- One command per (chord, verb) pair: precision/reach tiers (`Ctrl` variants) are separate
  commands, so every binding is individually rebindable. The interaction grammar's modifier
  algebra survives as the *shape of the default map*, not as an enforced restriction.
- Today a chord keeps exactly one owner; the mapping set's dispatch loop skips disabled
  commands and keeps looking (juce_KeyPressMappingSet.cpp:322-357), so enablement-partitioned
  chord sharing is the recorded future mechanism for modal scopes (the plugin-chain section),
  not something current commands use.
- Commands whose old decoder branch declined silently (Esc with no rung, Delete with no
  selection, digits with no typing surface) register always-active and self-gate in `perform`:
  a disabled command whose chord matches makes JUCE play the system alert sound, and those
  keys must stay silent no-ops.
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

    /*! \brief File > Exit (`Ctrl+Q`; the OS separately owns `Alt+F4`). */
    ExitEditor = 0x1007,

    /*! \brief Edit > Undo (`Ctrl+Z`). */
    Undo = 0x1101,

    /*! \brief Edit > Redo (`Ctrl+Y`, `Ctrl+Shift+Z`). */
    Redo = 0x1102,

    /*! \brief Edit > Actions... (`Shift+/`, i.e. `?` — the REAPER actions-list convention). */
    ShowActions = 0x1103,

    /*! \brief Toggle transport playback (`Space`). */
    PlayPause = 0x1201,

    /*! \brief View > Show Waveform (`F5`). */
    ToggleWaveform = 0x1301,

    /*! \brief View > Undo History (`F8`). */
    ToggleUndoHistory = 0x1302,

    /*! \brief View > 3D Preview (`F3`). */
    TogglePreview3D = 0x1303,

    /*! \brief Insert a tone-change marker at the playhead (`Ctrl+T`). */
    InsertToneChange = 0x1401,

    /*! \brief Step the caret one grid slot left (`Left`). */
    CaretStepLeft = 0x1501,

    /*! \brief Step the caret one grid slot right (`Right`). */
    CaretStepRight = 0x1502,

    /*! \brief Step the caret one row up (`Up`). */
    CaretStepUp = 0x1503,

    /*! \brief Step the caret one row down (`Down`). */
    CaretStepDown = 0x1504,

    /*! \brief Jump the caret one measure left (`Ctrl+Left`). */
    CaretMeasureJumpLeft = 0x1505,

    /*! \brief Jump the caret one measure right (`Ctrl+Right`). */
    CaretMeasureJumpRight = 0x1506,

    /*! \brief Jump the caret to the chart start (`Home`, `Ctrl+Home`). */
    CaretJumpChartStart = 0x1507,

    /*! \brief Jump the caret to the chart end (`End`, `Ctrl+End`). */
    CaretJumpChartEnd = 0x1508,

    /*! \brief Jump the caret to the previous section (`PageUp`, `Ctrl+PageUp`). */
    CaretJumpPreviousSection = 0x1509,

    /*! \brief Jump the caret to the next section (`PageDown`, `Ctrl+PageDown`). */
    CaretJumpNextSection = 0x150A,

    /*! \brief Extend the time selection one grid slot left (`Shift+Left`). */
    TimeSelectionExtendLeft = 0x1601,

    /*! \brief Extend the time selection one grid slot right (`Shift+Right`). */
    TimeSelectionExtendRight = 0x1602,

    /*! \brief Extend the time selection one measure left (`Ctrl+Shift+Left`). */
    TimeSelectionExtendMeasureLeft = 0x1603,

    /*! \brief Extend the time selection one measure right (`Ctrl+Shift+Right`). */
    TimeSelectionExtendMeasureRight = 0x1604,

    /*! \brief Extend the time selection to the previous section (`Shift+PageUp`). */
    TimeSelectionExtendPreviousSection = 0x1605,

    /*! \brief Extend the time selection to the next section (`Shift+PageDown`). */
    TimeSelectionExtendNextSection = 0x1606,

    /*! \brief Extend the time selection to the chart start (`Shift+Home`). */
    TimeSelectionExtendChartStart = 0x1607,

    /*! \brief Extend the time selection to the chart end (`Shift+End`). */
    TimeSelectionExtendChartEnd = 0x1608,

    /*! \brief Move the selection one grid slot left (`Alt+Left`). */
    SelectionMoveLeft = 0x1609,

    /*! \brief Move the selection one grid slot right (`Alt+Right`). */
    SelectionMoveRight = 0x160A,

    /*! \brief Move the selection up (`Alt+Up`). */
    SelectionMoveUp = 0x160B,

    /*! \brief Move the selection down (`Alt+Down`). */
    SelectionMoveDown = 0x160C,

    /*! \brief Move the selection one fine (1/960-beat) step left (`Ctrl+Alt+Left`). */
    SelectionMoveFineLeft = 0x160D,

    /*! \brief Move the selection one fine step right (`Ctrl+Alt+Right`). */
    SelectionMoveFineRight = 0x160E,

    /*! \brief Move the selection one fine step up (`Ctrl+Alt+Up`). */
    SelectionMoveFineUp = 0x160F,

    /*! \brief Move the selection one fine step down (`Ctrl+Alt+Down`). */
    SelectionMoveFineDown = 0x1610,

    /*! \brief Delete the selection, whatever its kind (`Delete`). */
    SelectionDelete = 0x1611,

    /*! \brief Lengthen the selected sustain one grid step (`Alt+Shift+Right`). */
    SustainLengthen = 0x1701,

    /*! \brief Shorten the selected sustain one grid step (`Alt+Shift+Left`). */
    SustainShorten = 0x1702,

    /*! \brief Lengthen the selected sustain one fine step (`Ctrl+Alt+Shift+Right`). */
    SustainLengthenFine = 0x1703,

    /*! \brief Shorten the selected sustain one fine step (`Ctrl+Alt+Shift+Left`). */
    SustainShortenFine = 0x1704,

    /*! \brief Shift the selected notes' frets up (`Alt+Shift+Up`). */
    FretShiftUp = 0x1705,

    /*! \brief Shift the selected notes' frets down (`Alt+Shift+Down`). */
    FretShiftDown = 0x1706,

    /*! \brief Neutral create at the armed caret: a fret-0 note or on-curve point (`Insert`). */
    NeutralInsert = 0x1707,

    /*! \brief Cancel the Esc ladder's top rung: gesture, then caret, then selection (`Esc`). */
    CancelDismiss = 0x1708,

    /*! \brief Type digit 0 into the armed row's payload (`0`, numpad `0`). */
    TypeDigit0 = 0x1801,

    /*! \brief Type digit 1 (`1`, numpad `1`). */
    TypeDigit1 = 0x1802,

    /*! \brief Type digit 2 (`2`, numpad `2`). */
    TypeDigit2 = 0x1803,

    /*! \brief Type digit 3 (`3`, numpad `3`). */
    TypeDigit3 = 0x1804,

    /*! \brief Type digit 4 (`4`, numpad `4`). */
    TypeDigit4 = 0x1805,

    /*! \brief Type digit 5 (`5`, numpad `5`). */
    TypeDigit5 = 0x1806,

    /*! \brief Type digit 6 (`6`, numpad `6`). */
    TypeDigit6 = 0x1807,

    /*! \brief Type digit 7 (`7`, numpad `7`). */
    TypeDigit7 = 0x1808,

    /*! \brief Type digit 8 (`8`, numpad `8`). */
    TypeDigit8 = 0x1809,

    /*! \brief Type digit 9 (`9`, numpad `9`). */
    TypeDigit9 = 0x180A,

    /*! \brief Step the grid one preset finer (`+` main-row or numpad; `=` unshifted alias). */
    GridFiner = 0x1901,

    /*! \brief Step the grid one preset coarser (`-`, main-row or numpad). */
    GridCoarser = 0x1902,

    /*! \brief Zoom the timeline in around the marker (`Ctrl` + the plus family). */
    ZoomIn = 0x1903,

    /*! \brief Zoom the timeline out around the marker (`Ctrl+-`). */
    ZoomOut = 0x1904,
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
