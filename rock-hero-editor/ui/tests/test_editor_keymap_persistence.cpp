#include "keybinds/editor_command_registry.h"
#include "keybinds/editor_keymap_persistence.h"

#include <optional>
#include <rock_hero/editor/core/testing/null_editor_settings.h>
#include <rock_hero/editor/ui/testing/editor_view_test_harness.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Records the keymap blob in memory so tests observe restore reads and save writes.
class RecordingKeymapSettings final : public core::testing::NullEditorSettings
{
public:
    /*! \copydoc rock_hero::editor::core::IEditorSettings::keymapXml */
    [[nodiscard]] std::optional<std::string> keymapXml() const override
    {
        return stored_keymap;
    }

    /*! \copydoc rock_hero::editor::core::IEditorSettings::setKeymapXml */
    [[nodiscard]] std::expected<void, core::EditorSettingsError> setKeymapXml(
        std::optional<std::string> keymap_xml) override
    {
        stored_keymap = std::move(keymap_xml);
        ++write_count;
        return {};
    }

    /*! \brief Blob most recently stored, or empty when cleared or never written. */
    std::optional<std::string> stored_keymap{};

    /*! \brief Number of setKeymapXml calls, for asserting the equality gate. */
    int write_count{0};
};

} // namespace

// A rebind saves the diff-versus-defaults XML; returning to pure defaults clears the stored
// value entirely, so shipped default changes always merge under user overrides.
TEST_CASE("EditorKeymapPersistence saves rebinds and clears on reset", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingKeymapSettings settings;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    const EditorKeymapPersistence persistence{view.commandManager(), settings};

    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    CHECK_FALSE(settings.stored_keymap.has_value());
    CHECK(settings.write_count == 0);

    mappings.addKeyPress(
        toJuceCommandId(EditorCommandId::ToggleUndoHistory), juce::KeyPress{juce::KeyPress::F9Key});
    mappings.sendSynchronousChangeMessage();
    REQUIRE(settings.stored_keymap.has_value());
    if (settings.stored_keymap.has_value())
    {
        CHECK(settings.stored_keymap->find("KEYMAPPINGS") != std::string::npos);
    }
    const int writes_after_rebind = settings.write_count;

    // A change notification with an unchanged diff writes nothing (the equality gate).
    mappings.sendSynchronousChangeMessage();
    CHECK(settings.write_count == writes_after_rebind);

    mappings.resetToDefaultMappings();
    mappings.sendSynchronousChangeMessage();
    CHECK_FALSE(settings.stored_keymap.has_value());
}

// A stored diff restores into a fresh mapping set at construction — the restart simulation.
TEST_CASE("EditorKeymapPersistence restores stored overrides", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingKeymapSettings settings;
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;

    {
        EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
        const EditorKeymapPersistence persistence{view.commandManager(), settings};
        view.commandManager().getKeyMappings()->addKeyPress(
            toJuceCommandId(EditorCommandId::ToggleUndoHistory),
            juce::KeyPress{juce::KeyPress::F9Key});
        view.commandManager().getKeyMappings()->sendSynchronousChangeMessage();
        REQUIRE(settings.stored_keymap.has_value());
    }

    settings.write_count = 0;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    const EditorKeymapPersistence persistence{view.commandManager(), settings};

    juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    CHECK(
        mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F9Key}) ==
        toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    // The restore's own change notification re-saves nothing: the diff equals the stored blob.
    mappings.sendSynchronousChangeMessage();
    CHECK(settings.write_count == 0);
}

// Stored entries with unknown command ids (a newer editor's blob would trip the mapping set's
// debug assertion) are dropped before restore; known entries — the fully rebindable trio
// included — restore normally.
TEST_CASE("EditorKeymapPersistence drops unknown stored entries", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingKeymapSettings settings;
    settings.stored_keymap =
        std::string{R"(<KEYMAPPINGS>)"
                    R"(<MAPPING commandId="9999" description="F10" key="F10"/>)"
                    R"(<MAPPING commandId="1101" description="F11" key="F11"/>)"
                    R"(<MAPPING commandId="1302" description="F9" key="F9"/>)"
                    R"(</KEYMAPPINGS>)"};
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    const EditorKeymapPersistence persistence{view.commandManager(), settings};

    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    // Known entries restored — an added F11 alias for Undo included — the unknown one did not.
    CHECK(
        mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F9Key}) ==
        toJuceCommandId(EditorCommandId::ToggleUndoHistory));
    CHECK(
        mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F11Key}) ==
        toJuceCommandId(EditorCommandId::Undo));
    CHECK(mappings.findCommandForKeyPress(juce::KeyPress{juce::KeyPress::F10Key}) == 0);
}

// A corrupt blob must never brick startup: restore falls back to pure defaults.
TEST_CASE("EditorKeymapPersistence tolerates an unparseable blob", "[ui][keybinds]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordingKeymapSettings settings;
    settings.stored_keymap = std::string{"not xml at all"};
    core::testing::RecordingEditorController controller;
    const FakeTransport transport;
    RecordingThumbnailFactory thumbnail_factory;
    EditorView view{controller, viewAudioPorts(transport, thumbnail_factory)};
    const EditorKeymapPersistence persistence{view.commandManager(), settings};

    const juce::KeyPressMappingSet& mappings = *view.commandManager().getKeyMappings();
    CHECK(
        mappings.findCommandForKeyPress(
            juce::KeyPress{'z', juce::ModifierKeys::commandModifier, 0}) ==
        toJuceCommandId(EditorCommandId::Undo));
}

} // namespace rock_hero::editor::ui
