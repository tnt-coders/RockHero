/*!
\file editor_command_id.h
\brief Stable editor command identifiers for the keybind registry.
*/

#pragma once

#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

namespace rock_hero::editor::ui
{

/*!
\brief Stable identifier for one editor command in the keybind registry.

Values are the persistence contract: the keymap XML keys bindings off the hex value of these ids,
so an id, once shipped, is locked forever. Growth conventions (plan 46; total rebindability per
plan 53 Phase 1b):

- New commands append new explicit values; never renumber, reuse, or reorder existing ones. Id
  blocks group by category: 0x1x file/edit/transport/view/tone, 0x15xx navigation, 0x16xx
  selection, 0x17xx authoring, 0x18xx value entry, 0x19xx grid & zoom, 0x1Bxx menus. Blocks
  are historical hints only — the registry row owns the display category (CancelDismiss,
  0x1708, lists under Selection).
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
enum class EditorCommandId : std::uint16_t
{
    /*! \brief File > Open... (`Ctrl+O`). */
    OpenProject = 0x1001,

    /*! \brief File > Import Song... (`Ctrl+I`). */
    ImportSong = 0x1002,

    /*! \brief File > Save (`Ctrl+S`). */
    SaveProject = 0x1003,

    /*! \brief File > Save As... (`Ctrl+Shift+S`). */
    SaveProjectAs = 0x1004,

    /*! \brief File > Export Song... (`Ctrl+E`). */
    ExportSong = 0x1005,

    /*! \brief File > Close (`Ctrl+W`). */
    CloseProject = 0x1006,

    /*! \brief File > Exit (`Ctrl+Q`; the OS separately owns `Alt+F4`). */
    ExitEditor = 0x1007,

    /*!
    \brief Import a tone file into the active tone (`Ctrl+Shift+I`).

    The signal-chain header's Import Tone button is this command's visible surface, so it carries no
    menu item; the Actions dialog lists it like every other command. `I` has two claimants: the bare
    `Ctrl+I` imports the SONG (\ref ImportSong) and the Shift tier imports the tone.
    */
    ImportTone = 0x1008,

    /*!
    \brief Export the active tone to a tone file (`Ctrl+Shift+E`).

    The signal-chain header's Export Tone button is this command's visible surface, so it carries no
    menu item. `E` has two claimants exactly as `I` does: the bare `Ctrl+E` exports the SONG
    (\ref ExportSong) and the Shift tier exports the tone.
    */
    ExportTone = 0x1009,

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

    // 0x1304 through 0x130E were sighting samplers (accent light, family scale, string spacing,
    // harmonic size, head width, staged-atlas cycling, the 2D note trim, and the actual-ring
    // rigs — the 3D floor mark with its two filters, and the 2D reveal's style flip), each
    // deleted once its decision settled; retired ids are never revived.

    /*!
    \brief Insert a tone change at the cursor, or retone the region starting exactly there
    (`Ctrl+T`).

    The marker grammar, shared verbatim with \ref InsertSongSection: the chord AUTHORS at the
    cursor — the armed caret, else the paused cursor — and never reads the selection. A tone change
    IS a region boundary, so the cursor standing exactly on a region's start restates that change
    (picks another catalog tone for the region), while the cursor anywhere inside a region splits
    it into a new one; a restate selects its target, as every insert does. The core publishes the
    verb (`EditorViewState::tone_chord_target`) and the view opens the picker it names; the press
    is inert while the transport plays and with no song. The walk, Tab and a click are the ways
    onto an existing region; \ref RestateSelection and \ref RenameSelection are its verbs.
    */
    InsertToneChange = 0x1401,

    /*!
    \brief Insert a section at the cursor's measure, or rename the one starting there (`Ctrl+M`).

    Shares the tone change's marker grammar. The chord reads the MEASURE the cursor is in — the
    downbeat is the only place a section can start — and renames the section standing on that
    downbeat or inserts one where it is free, a prompt naming it; the terminal downbeat, which can
    carry no section, makes the press inert. The core publishes the verb
    (`EditorViewState::section_chord_target`); the view opens the prompt it names. A second press
    after an insert therefore renames what the first made, since the cursor is still in its
    measure.
    */
    InsertSongSection = 0x1402,

    // 0x1403 was Rename Section (F2), retired 2026-09-12 when the section chord took restating
    // a selected section as its own second half; retired ids are never revived.

    /*!
    \brief Restate the selected marker (`Enter`).

    The marker plane's chords author at the cursor; this is the SELECTION's own verb: a section's
    name through the rename prompt, a tone region's tone through the picker, the "+" row's
    parameter picker. The core publishes the verb (`EditorViewState::restate_target`), so the view
    dispatches on no kind of its own. A separate command rather than a second chord on either
    marker chord, because Enter must never ADD a marker: with nothing selected this press is inert.
    */
    RestateSelection = 0x1404,

    /*!
    \brief Rename the selected marker where its kind has a name (`Ctrl+R`).

    A section's own name, or a tone region's TONE — the document every region on that tone
    shares, which is what the tone row's double-click renames. Silently inert on every kind with
    no name of its own. The core publishes the verb (`EditorViewState::rename_target`). It sits on
    the document plane with `Ctrl+S` and `Ctrl+G`, not on the marker plane: R is no marker's
    letter, and the verb reads the selection, never the cursor. F2's retired id is not reused,
    since saved keymaps key off the numeric id.
    */
    RenameSelection = 0x1405,

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

    /*!
    \brief Jump to the nearest row of the group of rows above (`Ctrl+Up`).

    The groups are the ruler's section, tempo and time-signature rows, the strings, the tone row,
    the automation lanes and the "+" row beneath them, and the jump lands on the destination group's
    nearest row: from any string the time-signature row, from any lane the tone row, from the "+"
    row the last lane.
    */
    CaretJumpSurfaceAbove = 0x150B,

    /*!
    \brief Jump to the nearest row of the group of rows below (`Ctrl+Down`).

    From the time-signature row this reaches the top string, from any string the tone row, and from
    any lane the "+" row.
    */
    CaretJumpSurfaceBelow = 0x150C,

    /*!
    \brief Step to the next object on the focused row (`Tab`).

    The grid is ignored: a string's next note or keyframe, a lane's next point, a marker row's next
    marker. A named exception to "Shift extends": its pair steps back rather than extending.
    */
    CaretStepNextObject = 0x150D,

    /*! \brief Step to the previous object on the focused row (`Shift+Tab`). */
    CaretStepPreviousObject = 0x150E,

    /*!
    \brief Step to the next note on the caret's string, over its keyframes (`Ctrl+Tab`).

    Bound to the physical Ctrl key on every platform, since Cmd+Tab belongs to macOS. A lane or a
    marker row has no keyframes to step over, so there it steps exactly as \ref CaretStepNextObject.
    */
    CaretStepNextNote = 0x150F,

    /*! \brief Step to the previous note on the caret's string (`Ctrl+Shift+Tab`). */
    CaretStepPreviousNote = 0x1510,

    /*!
    \brief Jump keyboard focus onto the ruler's section row (`Ctrl+Shift+M`).

    The `Ctrl+Shift`+letter family: each marker kind's letter authors with `Ctrl` and selects with
    `Ctrl+Shift`, the pair composed from one declared letter so the two chords cannot drift. A jump
    lands as the walk does — the marker holding the cursor is selected and the caret demotes in
    place — and does nothing, silently, where the row has nothing to hold the cursor. Main window
    only: the 3D preview draws no marker rows.
    */
    CaretJumpSectionRow = 0x1511,

    /*! \brief Jump keyboard focus onto the ruler's tempo row (`Ctrl+Shift+B`). */
    CaretJumpTempoRow = 0x1512,

    /*!
    \brief Jump keyboard focus onto the ruler's time-signature row (`Ctrl+Shift+/`).

    The default works on US Windows and Linux only: macOS key codes keep Shift, so the chord
    arrives as `?` there, and German, French, Nordic and Swiss layouts have no `/` key. Rebinding
    is the answer until per-language keymaps exist (decision D1 of the focus-rows plan).
    */
    CaretJumpTimeSignatureRow = 0x1513,

    /*! \brief Jump keyboard focus onto the tone row (`Ctrl+Shift+T`). */
    CaretJumpToneRow = 0x1514,

    /*!
    \brief Jump keyboard focus onto the "+" row that adds an automation lane (`Ctrl+Shift+A`).

    It only lands on the row; `Enter` then opens the parameter picker. A for automation: nothing in
    this editor uses `Ctrl+Shift+A` for select-none.
    */
    CaretJumpAddLaneRow = 0x1515,

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

    // 0x160D-0x1610 were the fine-tier selection moves, retired with the Ctrl fine tier when grid
    // snap took over off-grid placement (docs/plans/completed/grid-snap.md). The values stay
    // spent: a stale persisted keymap naming one resolves to no spec and is dropped.

    /*! \brief Delete the selection, whatever its kind (`Delete`). */
    SelectionDelete = 0x1611,

    /*! \brief Lengthen the selected sustain one grid step (`Alt+Shift+Right`). */
    SustainLengthen = 0x1701,

    /*! \brief Shorten the selected sustain one grid step (`Alt+Shift+Left`). */
    SustainShorten = 0x1702,

    // 0x1703-0x1704 were the fine-tier sustain steps, retired with the rest of the Ctrl fine tier.

    /*! \brief Shift the selected notes' frets up (`Alt+Shift+Up`). */
    FretShiftUp = 0x1705,

    /*! \brief Shift the selected notes' frets down (`Alt+Shift+Down`). */
    FretShiftDown = 0x1706,

    /*!
    \brief Plant an on-curve point at an armed AUTOMATION-LANE caret slot (`Insert`).

    The key's whole meaning. The chart lane has no share in it: every object there is TYPED, so a
    digit states the note or the point and a key carrying no value has nothing to place.
    */
    InsertLanePoint = 0x1707,

    /*! \brief Cancel the Esc ladder's top rung: gesture, then caret, then selection (`Esc`). */
    CancelDismiss = 0x1708,

    /*! \brief Toggle the selected notes to or from the pick-slide attack (no default chord). */
    ChartPickSlideToggle = 0x1709,

    /*! \brief Claim or clear a legato connection on the selected notes (`L`). */
    ChartLegatoToggle = 0x170A,

    /*! \brief Set the selected notes to the left-hand tap attack (`Shift+T`). */
    ChartLeftTap = 0x170B,

    /*! \brief Toggle the picking hand's palm mute on the selected notes (`M`). */
    ChartPalmMuteToggle = 0x170C,

    /*! \brief Toggle the dead-note mute on the selected notes (`X`). */
    ChartDeadNoteToggle = 0x170D,

    /*! \brief Toggle the accent on the selected notes (`A`). */
    ChartAccentToggle = 0x170E,

    /*! \brief Toggle the ghost note on the selected notes (`G`). */
    ChartGhostToggle = 0x170F,

    /*! \brief Toggle tremolo picking on the selected notes (`R`). */
    ChartTremoloToggle = 0x1710,

    /*! \brief Toggle the ordinary vibrato on the selected notes (`V`). */
    ChartVibratoToggle = 0x1711,

    /*!
    \brief Toggle every selected junction — keyframe to head, head to point (`Shift+L`).

    The VALUE is the contract, never the name: it is what a saved keymap stores and what a
    rebinding resolves through, so it stays 0x1713 across every rename this verb takes. This one
    was `ChartKeyframeDisconnect` while only the split half existed.
    */
    ChartJunctionToggle = 0x1713,

    /*! \brief Toggle the WIDE vibrato on the selected notes (`Shift+V`). */
    ChartWideVibratoToggle = 0x1714,

    /*! \brief Toggle the selected notes to or from the right-hand tap attack (`T`). */
    ChartTapToggle = 0x1715,

    /*! \brief Toggle the selected notes to or from the slap attack (`S`). */
    ChartSlapToggle = 0x1716,

    /*! \brief Toggle the selected notes to or from the pop attack (`P`). */
    ChartPopToggle = 0x1717,

    /*!
    \brief Offer the fretting hand's harmonic on the selected notes: write the one change or open
    the node picker (`H`).
    */
    ChartHarmonic = 0x1718,

    /*! \brief Toggle the pinch harmonic on the selected notes (`Shift+H`). */
    ChartPinchHarmonicToggle = 0x1719,

    // 0x171A-0x171B were Insert Point (Alt+Insert) and Insert Note, Repeating Fret
    // (Shift+Insert), retired with the fretless entry verbs: every note is typed now, and the
    // Insert key states nothing on the chart lane.

    /*!
    \brief Type digit 0 into the armed row's payload (`0`, numpad `0`).

    Every object on the chart lane is typed, and the bare digits are how: the value they accumulate
    states a note on a slot no ring covers, a point on the path of one that does, or a retype of a
    non-empty selection. \ref EditorCommandId::TypePathDigit0 and its siblings differ in exactly one
    cell — at a ring's exact END they state the slide-out where these place the adjacent head. On an
    automation lane row a digit is plain value entry, which has no second verb.
    */
    TypeDigit0 = 0x1801,

    /*! \brief Type digit 1 at the armed caret (`1`, numpad `1`). */
    TypeDigit1 = 0x1802,

    /*! \brief Type digit 2 at the armed caret (`2`, numpad `2`). */
    TypeDigit2 = 0x1803,

    /*! \brief Type digit 3 at the armed caret (`3`, numpad `3`). */
    TypeDigit3 = 0x1804,

    /*! \brief Type digit 4 at the armed caret (`4`, numpad `4`). */
    TypeDigit4 = 0x1805,

    /*! \brief Type digit 5 at the armed caret (`5`, numpad `5`). */
    TypeDigit5 = 0x1806,

    /*! \brief Type digit 6 at the armed caret (`6`, numpad `6`). */
    TypeDigit6 = 0x1807,

    /*! \brief Type digit 7 at the armed caret (`7`, numpad `7`). */
    TypeDigit7 = 0x1808,

    /*! \brief Type digit 8 at the armed caret (`8`, numpad `8`). */
    TypeDigit8 = 0x1809,

    /*! \brief Type digit 9 at the armed caret (`9`, numpad `9`). */
    TypeDigit9 = 0x180A,

    /*!
    \brief Type digit 0 into a value that STATES the path (`Alt+0`).

    The PATH verb, which differs from the bare digit (\ref EditorCommandId::TypeDigit0) in exactly
    one cell: at a ring's exact END it states the SLIDE-OUT the release names, where the bare digit
    places the adjacent head. Everywhere else the two say the same thing. Both keyboard rows carry
    it — see the registry for why the numpad row binds the top-row code.
    */
    TypePathDigit0 = 0x180B,

    /*! \brief Type digit 1, stating the path (`Alt+1`). */
    TypePathDigit1 = 0x180C,

    /*! \brief Type digit 2, stating the path (`Alt+2`). */
    TypePathDigit2 = 0x180D,

    /*! \brief Type digit 3, stating the path (`Alt+3`). */
    TypePathDigit3 = 0x180E,

    /*! \brief Type digit 4, stating the path (`Alt+4`). */
    TypePathDigit4 = 0x180F,

    /*! \brief Type digit 5, stating the path (`Alt+5`). */
    TypePathDigit5 = 0x1810,

    /*! \brief Type digit 6, stating the path (`Alt+6`). */
    TypePathDigit6 = 0x1811,

    /*! \brief Type digit 7, stating the path (`Alt+7`). */
    TypePathDigit7 = 0x1812,

    /*! \brief Type digit 8, stating the path (`Alt+8`). */
    TypePathDigit8 = 0x1813,

    /*! \brief Type digit 9, stating the path (`Alt+9`). */
    TypePathDigit9 = 0x1814,

    /*! \brief Step the grid one preset finer (`+` main-row or numpad; `=` unshifted alias). */
    GridFiner = 0x1901,

    /*! \brief Step the grid one preset coarser (`-`, main-row or numpad). */
    GridCoarser = 0x1902,

    /*! \brief Zoom the timeline in around the marker (`Ctrl` + the plus family). */
    ZoomIn = 0x1903,

    /*! \brief Zoom the timeline out around the marker (`Ctrl+-`). */
    ZoomOut = 0x1904,

    /*! \brief Flip grid snap, which decides the placement quantum (`Ctrl+G`). */
    ToggleGridSnap = 0x1905,

    // 0x1A01 was the `F6` span-minimum sighting toggle, deleted once the three-member accumulation
    // minimum settled; like every retired sighting sampler, the value stays spent and is never
    // revived.

    /*! \brief Open the File menu (`Alt+F`), the platform's own menu-access convention. */
    OpenFileMenu = 0x1B01,

    /*! \brief Open the Edit menu (`Alt+E`). */
    OpenEditMenu = 0x1B02,

    /*! \brief Open the View menu (`Alt+V`). */
    OpenViewMenu = 0x1B03,
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
