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
