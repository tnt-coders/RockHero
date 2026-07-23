#include "keybinds/editor_command_registry.h"

#include <array>
#include <cstddef>

namespace rock_hero::editor::ui
{

namespace
{

// Chords register lowercase letters (the mapping set asserts on uppercase-without-shift) with
// commandModifier, which is the Ctrl key on Windows. KeyPress matching is exact on modifiers, so
// a Ctrl+Z entry does not fire on Ctrl+Shift+Z or Ctrl+Alt+Z; letter matching is
// case-insensitive, so uppercase key codes delivered by the OS still match.
[[nodiscard]] juce::KeyPress chord(int key_code, int modifier_flags = 0)
{
    return juce::KeyPress{key_code, juce::ModifierKeys{modifier_flags}, 0};
}

[[nodiscard]] std::vector<EditorCommandSpec> makeRegistry()
{
    constexpr int command = juce::ModifierKeys::commandModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;

    std::vector<EditorCommandSpec> registry;
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::OpenProject,
            .name = "Open...",
            .category = "File",
            .default_keypresses = {chord('o', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ImportSong,
            .name = "Import...",
            .category = "File",
            // Ctrl+Shift+O avoids the Ctrl+I italics muscle-memory collision (plan 46 tier A).
            .default_keypresses = {chord('o', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::SaveProject,
            .name = "Save",
            .category = "File",
            .default_keypresses = {chord('s', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::SaveProjectAs,
            .name = "Save As...",
            .category = "File",
            .default_keypresses = {chord('s', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::PublishSong,
            .name = "Publish...",
            .category = "File",
            .default_keypresses = {chord('p', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::CloseProject,
            .name = "Close",
            .category = "File",
            .default_keypresses = {chord('w', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ExitEditor,
            .name = "Exit",
            .category = "File",
            // Ctrl+Q is the app-owned quit chord (user decision 2026-07-20); the OS separately
            // owns Alt+F4, which needs no registration.
            .default_keypresses = {chord('q', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::Undo,
            .name = "Undo",
            .category = "Edit",
            .default_keypresses = {chord('z', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::Redo,
            .name = "Redo",
            .category = "Edit",
            // Ctrl+Shift+Z is the DAW-convention alternative (decision 2026-07-20); alternatives
            // per command are first-class in the mapping set.
            .default_keypresses = {chord('y', command), chord('z', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ShowActions,
            .name = "Actions...",
            .category = "Edit",
            // Shift+/ types "?" — REAPER's actions-list key. The key code addresses the "/"
            // key, so the default only lands on layouts where "?" lives there; elsewhere the
            // user rebinds, which stores their own layout's chord.
            .default_keypresses = {chord('/', shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::PlayPause,
            .name = "Play/Pause",
            .category = "Transport",
            .default_keypresses = {chord(juce::KeyPress::spaceKey)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleWaveform,
            .name = "Show Waveform",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F5Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleUndoHistory,
            .name = "Undo History",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F8Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::TogglePreview3D,
            .name = "3D Preview",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F3Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::InsertToneChange,
            // "at Cursor" = the marker rule (E2 as amended 2026-07-21): the armed caret when
            // one exists, else the transport position — the same "one position concept" play
            // follows, so the insert always lands where play would pick up.
            .name = "Insert Tone Change at Cursor",
            .category = "Tone",
            // Exact modifier matching gives the guard-against-Alt for free: Ctrl+Alt+T does not
            // match, keeping the Ctrl+Alt namespace with the fine-tier authoring composition.
            .default_keypresses = {chord('t', command)},
        });

    // The grammar verbs (plan 53 Phase 1b, total rebindability 2026-07-20): one command per
    // (chord, verb) pair, so the precision/reach tiers are separate commands and every binding
    // is individually rebindable. The defaults below ARE the interaction grammar's modifier
    // algebra; the algebra is no longer enforced, only shipped.
    constexpr int alt = juce::ModifierKeys::altModifier;
    const auto add = [&registry](
                         EditorCommandId id,
                         const char* name,
                         const char* category,
                         std::vector<juce::KeyPress>
                             chords) {
        registry.push_back(
            EditorCommandSpec{
                .id = id,
                .name = name,
                .category = category,
                .default_keypresses = std::move(chords),
            });
    };

    // Navigation. The Ctrl aliases on the jump chords preserve the held-Ctrl navigation that
    // would otherwise read as broken (decision A / the accepted PageUp ride-along).
    add(EditorCommandId::CaretStepLeft,
        "Step Caret Left",
        "Navigation",
        {chord(juce::KeyPress::leftKey)});
    add(EditorCommandId::CaretStepRight,
        "Step Caret Right",
        "Navigation",
        {chord(juce::KeyPress::rightKey)});
    add(EditorCommandId::CaretStepUp,
        "Step Caret Up",
        "Navigation",
        {chord(juce::KeyPress::upKey)});
    add(EditorCommandId::CaretStepDown,
        "Step Caret Down",
        "Navigation",
        {chord(juce::KeyPress::downKey)});
    add(EditorCommandId::CaretMeasureJumpLeft,
        "Jump Measure Left",
        "Navigation",
        {chord(juce::KeyPress::leftKey, command)});
    add(EditorCommandId::CaretMeasureJumpRight,
        "Jump Measure Right",
        "Navigation",
        {chord(juce::KeyPress::rightKey, command)});
    add(EditorCommandId::CaretJumpChartStart,
        "Jump to Chart Start",
        "Navigation",
        {chord(juce::KeyPress::homeKey), chord(juce::KeyPress::homeKey, command)});
    add(EditorCommandId::CaretJumpChartEnd,
        "Jump to Chart End",
        "Navigation",
        {chord(juce::KeyPress::endKey), chord(juce::KeyPress::endKey, command)});
    add(EditorCommandId::CaretJumpPreviousSection,
        "Jump to Previous Section",
        "Navigation",
        {chord(juce::KeyPress::pageUpKey), chord(juce::KeyPress::pageUpKey, command)});
    add(EditorCommandId::CaretJumpNextSection,
        "Jump to Next Section",
        "Navigation",
        {chord(juce::KeyPress::pageDownKey), chord(juce::KeyPress::pageDownKey, command)});

    // Selection.
    add(EditorCommandId::TimeSelectionExtendLeft,
        "Extend Time Selection Left",
        "Selection",
        {chord(juce::KeyPress::leftKey, shift)});
    add(EditorCommandId::TimeSelectionExtendRight,
        "Extend Time Selection Right",
        "Selection",
        {chord(juce::KeyPress::rightKey, shift)});
    add(EditorCommandId::TimeSelectionExtendMeasureLeft,
        "Extend Time Selection Left (Measure)",
        "Selection",
        {chord(juce::KeyPress::leftKey, command | shift)});
    add(EditorCommandId::TimeSelectionExtendMeasureRight,
        "Extend Time Selection Right (Measure)",
        "Selection",
        {chord(juce::KeyPress::rightKey, command | shift)});
    add(EditorCommandId::TimeSelectionExtendPreviousSection,
        "Extend Time Selection to Previous Section",
        "Selection",
        {chord(juce::KeyPress::pageUpKey, shift)});
    add(EditorCommandId::TimeSelectionExtendNextSection,
        "Extend Time Selection to Next Section",
        "Selection",
        {chord(juce::KeyPress::pageDownKey, shift)});
    add(EditorCommandId::TimeSelectionExtendChartStart,
        "Extend Time Selection to Chart Start",
        "Selection",
        {chord(juce::KeyPress::homeKey, shift)});
    add(EditorCommandId::TimeSelectionExtendChartEnd,
        "Extend Time Selection to Chart End",
        "Selection",
        {chord(juce::KeyPress::endKey, shift)});
    add(EditorCommandId::SelectionMoveLeft,
        "Move Selection Left",
        "Selection",
        {chord(juce::KeyPress::leftKey, alt)});
    add(EditorCommandId::SelectionMoveRight,
        "Move Selection Right",
        "Selection",
        {chord(juce::KeyPress::rightKey, alt)});
    add(EditorCommandId::SelectionMoveUp,
        "Move Selection Up",
        "Selection",
        {chord(juce::KeyPress::upKey, alt)});
    add(EditorCommandId::SelectionMoveDown,
        "Move Selection Down",
        "Selection",
        {chord(juce::KeyPress::downKey, alt)});
    add(EditorCommandId::SelectionMoveFineLeft,
        "Move Selection Left (Fine)",
        "Selection",
        {chord(juce::KeyPress::leftKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineRight,
        "Move Selection Right (Fine)",
        "Selection",
        {chord(juce::KeyPress::rightKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineUp,
        "Move Selection Up (Fine)",
        "Selection",
        {chord(juce::KeyPress::upKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineDown,
        "Move Selection Down (Fine)",
        "Selection",
        {chord(juce::KeyPress::downKey, command | alt)});
    add(EditorCommandId::SelectionDelete,
        "Delete Selection",
        "Selection",
        {chord(juce::KeyPress::deleteKey)});
    // Cancel/Clear sits with the selection verbs (its user-visible rungs disarm the caret and
    // clear the selection); its 0x1708 id stays in the authoring block — id blocks are
    // historical hints, the registry row owns the category.
    add(EditorCommandId::CancelDismiss,
        "Cancel / Clear",
        "Selection",
        {chord(juce::KeyPress::escapeKey)});

    // Authoring ("Editing" would collide with the Edit menu category in the dialog).
    add(EditorCommandId::SustainLengthen,
        "Lengthen Sustain",
        "Authoring",
        {chord(juce::KeyPress::rightKey, alt | shift)});
    add(EditorCommandId::SustainShorten,
        "Shorten Sustain",
        "Authoring",
        {chord(juce::KeyPress::leftKey, alt | shift)});
    add(EditorCommandId::SustainLengthenFine,
        "Lengthen Sustain (Fine)",
        "Authoring",
        {chord(juce::KeyPress::rightKey, command | alt | shift)});
    add(EditorCommandId::SustainShortenFine,
        "Shorten Sustain (Fine)",
        "Authoring",
        {chord(juce::KeyPress::leftKey, command | alt | shift)});
    add(EditorCommandId::FretShiftUp,
        "Shift Frets Up",
        "Authoring",
        {chord(juce::KeyPress::upKey, alt | shift)});
    add(EditorCommandId::FretShiftDown,
        "Shift Frets Down",
        "Authoring",
        {chord(juce::KeyPress::downKey, alt | shift)});
    add(EditorCommandId::NeutralInsert,
        "Insert Note / Point",
        "Authoring",
        {chord(juce::KeyPress::insertKey)});

    // Value entry: digit N types into the armed row's payload; the numpad chord is a
    // first-class alias of the same command.
    for (int digit = 0; digit <= 9; ++digit)
    {
        static constexpr std::array<const char*, 10> digit_names{
            "Type Digit 0",
            "Type Digit 1",
            "Type Digit 2",
            "Type Digit 3",
            "Type Digit 4",
            "Type Digit 5",
            "Type Digit 6",
            "Type Digit 7",
            "Type Digit 8",
            "Type Digit 9",
        };
        add(static_cast<EditorCommandId>(static_cast<int>(EditorCommandId::TypeDigit0) + digit),
            digit_names.at(static_cast<std::size_t>(digit)),
            "Value Entry",
            {chord('0' + digit), chord(juce::KeyPress::numberPad0 + digit)});
    }

    // Grid & zoom. The numpad add/subtract keys arrive as their character key codes on
    // Windows — doKeyDown has no VK_ADD/VK_SUBTRACT case and doKeyChar's numpad remap covers
    // digits only (juce_Windowing_windows.cpp:3141-3161, :3178-3195) — so the bare '+'/'-'
    // chords ARE the numpad bindings and juce::KeyPress::numberPadAdd/Subtract never match
    // (registering them would ship lying chips). Shift+'=' is the main-row plus; it and '+'
    // display as one grouped "+" chip. The off-grammar neighbors stay as convenience aliases
    // until something better claims them (user 2026-07-21): '=' unshifted on the plus side,
    // Shift+'-' ('_') on the minus side — symmetric slop around the +/- grammar.
    add(EditorCommandId::GridFiner,
        "Grid Finer",
        "Grid & Zoom",
        {chord('=', shift), chord('+'), chord('=')});
    add(EditorCommandId::GridCoarser,
        "Grid Coarser",
        "Grid & Zoom",
        {chord('-'), chord('-', shift)});
    add(EditorCommandId::ZoomIn,
        "Zoom In",
        "Grid & Zoom",
        {chord('=', command | shift), chord('+', command), chord('=', command)});
    add(EditorCommandId::ZoomOut,
        "Zoom Out",
        "Grid & Zoom",
        {chord('-', command), chord('-', command | shift)});

    return registry;
}

} // namespace

const std::vector<EditorCommandSpec>& editorCommandRegistry()
{
    static const std::vector<EditorCommandSpec> registry = makeRegistry();
    return registry;
}

const EditorCommandSpec* findEditorCommandSpec(juce::CommandID command_id)
{
    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        if (toJuceCommandId(spec.id) == command_id)
        {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace rock_hero::editor::ui
