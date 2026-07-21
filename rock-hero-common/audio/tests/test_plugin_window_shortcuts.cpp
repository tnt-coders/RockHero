#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/audio/plugin/plugin_window_shortcuts.h>

namespace rock_hero::common::audio
{

// The built-in defaults keep an editor-less engine on the editor's default keymap: the
// historical trio plus the Ctrl+Shift+Z redo alias.
TEST_CASE("Plugin window shortcut defaults match the editor's default keymap", "[audio][plugin]")
{
    const PluginWindowShortcutBindings bindings = defaultPluginWindowShortcutBindings();

    CHECK(
        matchPluginWindowCommand(
            bindings, PluginWindowShortcutChord{.character = U'z', .ctrl = true}) ==
        PluginWindowCommand::Undo);
    CHECK(
        matchPluginWindowCommand(
            bindings, PluginWindowShortcutChord{.character = U'y', .ctrl = true}) ==
        PluginWindowCommand::Redo);
    CHECK(
        matchPluginWindowCommand(
            bindings, PluginWindowShortcutChord{.character = U'z', .ctrl = true, .shift = true}) ==
        PluginWindowCommand::Redo);
    CHECK(
        matchPluginWindowCommand(bindings, PluginWindowShortcutChord{.character = U' '}) ==
        PluginWindowCommand::PlayPause);
}

// Matching is exact on modifiers, honors alternative chords, and never matches an unidentified
// chord (no character and no named key).
TEST_CASE("Plugin window shortcut matching is exact", "[audio][plugin]")
{
    const PluginWindowShortcutBindings bindings = defaultPluginWindowShortcutBindings();

    CHECK_FALSE(
        matchPluginWindowCommand(
            bindings, PluginWindowShortcutChord{.character = U'z', .ctrl = true, .alt = true})
            .has_value());
    CHECK_FALSE(matchPluginWindowCommand(bindings, PluginWindowShortcutChord{.character = U'z'})
                    .has_value());
    CHECK_FALSE(matchPluginWindowCommand(bindings, PluginWindowShortcutChord{}).has_value());

    // Injected bindings replace the defaults wholesale: an Alt+character chord and a named key
    // both match once injected, and the previous default no longer does.
    PluginWindowShortcutBindings custom;
    custom.play_pause.push_back(PluginWindowShortcutChord{.character = U';', .alt = true});
    custom.undo.push_back(
        PluginWindowShortcutChord{.named_key = PluginWindowShortcutKey::F9, .ctrl = true});
    CHECK(
        matchPluginWindowCommand(
            custom, PluginWindowShortcutChord{.character = U';', .alt = true}) ==
        PluginWindowCommand::PlayPause);
    CHECK(
        matchPluginWindowCommand(
            custom,
            PluginWindowShortcutChord{.named_key = PluginWindowShortcutKey::F9, .ctrl = true}) ==
        PluginWindowCommand::Undo);
    CHECK_FALSE(
        matchPluginWindowCommand(custom, PluginWindowShortcutChord{.character = U' '}).has_value());
}

// The JUCE key-press decode agrees with the layout-neutral chord model: letters lowercase off
// their key codes, named keys map to named chords, numpad keys unify with their main-row twins,
// and Ctrl reads through the command modifier.
TEST_CASE("Plugin window chords decode from JUCE key presses", "[audio][plugin]")
{
    constexpr int command = juce::ModifierKeys::commandModifier;
    constexpr int alt = juce::ModifierKeys::altModifier;

    const PluginWindowShortcutChord ctrl_z =
        pluginWindowChordFromKeyPress(juce::KeyPress{'z', juce::ModifierKeys{command}, 0});
    CHECK(ctrl_z == PluginWindowShortcutChord{.character = U'z', .ctrl = true});

    const PluginWindowShortcutChord alt_semicolon =
        pluginWindowChordFromKeyPress(juce::KeyPress{';', juce::ModifierKeys{alt}, 0});
    CHECK(alt_semicolon == PluginWindowShortcutChord{.character = U';', .alt = true});

    const PluginWindowShortcutChord f9 =
        pluginWindowChordFromKeyPress(juce::KeyPress{juce::KeyPress::F9Key});
    CHECK(f9 == PluginWindowShortcutChord{.named_key = PluginWindowShortcutKey::F9});

    const PluginWindowShortcutChord space =
        pluginWindowChordFromKeyPress(juce::KeyPress{juce::KeyPress::spaceKey});
    CHECK(space == PluginWindowShortcutChord{.character = U' '});

    const PluginWindowShortcutChord numpad_five =
        pluginWindowChordFromKeyPress(juce::KeyPress{juce::KeyPress::numberPad5});
    CHECK(numpad_five == PluginWindowShortcutChord{.character = U'5'});

    const PluginWindowShortcutChord numpad_add =
        pluginWindowChordFromKeyPress(juce::KeyPress{juce::KeyPress::numberPadAdd});
    CHECK(numpad_add == PluginWindowShortcutChord{.character = U'+'});
}

} // namespace rock_hero::common::audio
