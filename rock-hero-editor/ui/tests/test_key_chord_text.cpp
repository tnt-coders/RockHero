#include "keybinds/editor_command_id.h"
#include "keybinds/key_chord_text.h"

#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>

namespace rock_hero::editor::ui
{

namespace
{

// Fixed fake layout so the collapse rule is asserted layout-independently: '/' shifts to '?',
// '1' to '!', letters to their capitals, everything else unresolvable.
[[nodiscard]] juce::juce_wchar fakeShiftResolver(juce::juce_wchar base)
{
    if (base == '/')
    {
        return '?';
    }
    if (base == '1')
    {
        return '!';
    }
    if (base >= 'a' && base <= 'z')
    {
        return juce::CharacterFunctions::toUpperCase(base);
    }
    return 0;
}

[[nodiscard]] juce::KeyPress chord(int key_code, int modifier_flags = 0)
{
    return juce::KeyPress{key_code, juce::ModifierKeys{modifier_flags}, 0};
}

} // namespace

// The collapse rule: a shifted chord renders as the character it types when that differs from
// the base by more than case; remaining modifiers stay; everything else keeps JUCE's text.
TEST_CASE("keyChordText collapses shifted characters", "[ui][keybinds]")
{
    constexpr int shift = juce::ModifierKeys::shiftModifier;
    constexpr int ctrl = juce::ModifierKeys::ctrlModifier;

    CHECK(keyChordText(chord('/', shift), &fakeShiftResolver) == "?");
    CHECK(keyChordText(chord('1', shift), &fakeShiftResolver) == "!");
    CHECK(keyChordText(chord('/', ctrl | shift), &fakeShiftResolver) == "ctrl + ?");

    // Letters differ only by case, so they keep the explicit shift form — a bare capital would
    // be ambiguous against the lowercase chip convention.
    CHECK(keyChordText(chord('z', shift), &fakeShiftResolver) == "shift + Z");

    // Unresolvable (dead key / not a plain key on this layout) and shiftless chords fall back.
    CHECK(keyChordText(chord('[', shift), &fakeShiftResolver) == "shift + [");
    CHECK(keyChordText(chord('/'), &fakeShiftResolver) == "/");
    CHECK(keyChordText(chord('z', ctrl), &fakeShiftResolver) == "ctrl + Z");

    // Named keys sit outside the printable range and never collapse.
    CHECK(
        keyChordText(chord(juce::KeyPress::F9Key, shift), &fakeShiftResolver) ==
        chord(juce::KeyPress::F9Key, shift).getTextDescription());
}

// addEditorCommandItem pre-fills the item's shortcut text through the shared formatter (JUCE
// only derives its own raw text when the field arrives empty), keeping menus and dialog chips
// on one rendering. Letter chords are layout-stable, so live-resolver output is deterministic.
TEST_CASE("addEditorCommandItem renders shortcuts through the formatter", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    view.setState(core::EditorViewState{});

    juce::PopupMenu menu;
    addEditorCommandItem(menu, view.commandManager(), EditorCommandId::Redo);
    addEditorCommandItem(menu, view.commandManager(), EditorCommandId::ShowActions);

    juce::PopupMenu::MenuItemIterator iterator{menu};
    REQUIRE(iterator.next());
    juce::PopupMenu::Item& redo_item = iterator.getItem();
    CHECK(redo_item.itemID == toJuceCommandId(EditorCommandId::Redo));
    CHECK(redo_item.commandManager == &view.commandManager());
    CHECK(redo_item.shortcutKeyDescription == "ctrl + Y, ctrl + shift + Z");

    // The actions item's text matches the formatter by construction on every layout: "?"
    // where Shift+/ resolves, the explicit "shift + /" elsewhere.
    REQUIRE(iterator.next());
    juce::PopupMenu::Item& actions_item = iterator.getItem();
    CHECK(actions_item.itemID == toJuceCommandId(EditorCommandId::ShowActions));
    CHECK(
        actions_item.shortcutKeyDescription ==
        keyChordText(chord('/', juce::ModifierKeys::shiftModifier)));
}

} // namespace rock_hero::editor::ui
