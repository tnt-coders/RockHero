#include "keybinds/editor_command_registry.h"

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
            .default_keypresses = {},
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
            .name = "Insert Tone Change",
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

    // Editing.
    add(EditorCommandId::SustainLengthen,
        "Lengthen Sustain",
        "Editing",
        {chord(juce::KeyPress::rightKey, alt | shift)});
    add(EditorCommandId::SustainShorten,
        "Shorten Sustain",
        "Editing",
        {chord(juce::KeyPress::leftKey, alt | shift)});
    add(EditorCommandId::SustainLengthenFine,
        "Lengthen Sustain (Fine)",
        "Editing",
        {chord(juce::KeyPress::rightKey, command | alt | shift)});
    add(EditorCommandId::SustainShortenFine,
        "Shorten Sustain (Fine)",
        "Editing",
        {chord(juce::KeyPress::leftKey, command | alt | shift)});
    add(EditorCommandId::FretShiftUp,
        "Shift Frets Up",
        "Editing",
        {chord(juce::KeyPress::upKey, alt | shift)});
    add(EditorCommandId::FretShiftDown,
        "Shift Frets Down",
        "Editing",
        {chord(juce::KeyPress::downKey, alt | shift)});
    add(EditorCommandId::NeutralInsert,
        "Insert Note / Point",
        "Editing",
        {chord(juce::KeyPress::insertKey)});
    add(EditorCommandId::CancelDismiss,
        "Cancel / Clear",
        "Editing",
        {chord(juce::KeyPress::escapeKey)});

    // Value entry: digit N types into the armed row's payload; the numpad chord is a
    // first-class alias of the same command.
    for (int digit = 0; digit <= 9; ++digit)
    {
        static constexpr const char* digit_names[] = {
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
            digit_names[digit],
            "Value Entry",
            {chord('0' + digit), chord(juce::KeyPress::numberPad0 + digit)});
    }

    // Grid & zoom: the old decoder's key-shape union becomes alternative default chords — the
    // main-row '=' key (shifted or not — Shift+'=' types '+'), the '+'/'-' character shapes
    // some numpad events arrive as, and the numpad add/subtract key codes.
    add(EditorCommandId::GridFiner,
        "Grid Finer",
        "Grid & Zoom",
        {chord('='), chord('=', shift), chord('+'), chord(juce::KeyPress::numberPadAdd)});
    add(EditorCommandId::GridCoarser,
        "Grid Coarser",
        "Grid & Zoom",
        {chord('-'), chord('-', shift), chord(juce::KeyPress::numberPadSubtract)});
    add(EditorCommandId::ZoomIn,
        "Zoom In",
        "Grid & Zoom",
        {chord('=', command), chord('+', command), chord(juce::KeyPress::numberPadAdd, command)});
    add(EditorCommandId::ZoomOut,
        "Zoom Out",
        "Grid & Zoom",
        {chord('-', command), chord(juce::KeyPress::numberPadSubtract, command)});

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
