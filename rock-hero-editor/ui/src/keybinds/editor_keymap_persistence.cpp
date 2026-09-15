#include "keybinds/editor_keymap_persistence.h"

#include "keybinds/editor_command_registry.h"
#include "keybinds/keymap_ownership.h"

#include <expected>
#include <memory>
#include <optional>
#include <rock_hero/common/core/shared/logger.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Drops stored mapping entries for unknown command ids (a newer editor's blob — release JUCE
// ignores them but debug builds hit a jassert inside restoreFromXml).
void removeUnrestorableEntries(juce::XmlElement& keymap)
{
    for (int index = keymap.getNumChildElements(); --index >= 0;)
    {
        juce::XmlElement* const entry = keymap.getChildElement(index);
        const juce::CommandID command_id = entry->getStringAttribute("commandId").getHexValue32();
        if (findEditorCommandSpec(command_id) == nullptr)
        {
            keymap.removeChildElement(entry, true);
        }
    }
}

// Restores a stored blob the way KeyPressMappingSet::restoreFromXml would, except that a restored
// binding takes its chord through the one-owner law. JUCE's loop adds a second owner whenever a
// chord the user moved to one command has since become another command's DEFAULT — the stored
// UNMAPPING for the old owner never names the new one — and dispatch then picks an owner by
// mapping order. Here the user's override wins. Every blob this editor writes is a diff from the
// defaults; a whole-set blob is honoured the way JUCE honours it, for a file from another source.
void restoreKeymap(juce::KeyPressMappingSet& mappings, const juce::XmlElement& keymap)
{
    if (!keymap.hasTagName("KEYMAPPINGS"))
    {
        return;
    }
    if (keymap.getBoolAttribute("basedOnDefaults", true))
    {
        mappings.resetToDefaultMappings();
    }
    else
    {
        mappings.clearAllKeyPresses();
    }
    for (const juce::XmlElement* const entry : keymap.getChildIterator())
    {
        const juce::CommandID command = entry->getStringAttribute("commandId").getHexValue32();
        if (command == 0)
        {
            continue;
        }
        const juce::KeyPress key =
            juce::KeyPress::createFromDescription(entry->getStringAttribute("key"));
        if (entry->hasTagName("MAPPING"))
        {
            assignKeyPressToCommand(mappings, command, key, -1);
        }
        else if (entry->hasTagName("UNMAPPING"))
        {
            removeKeyPressFromCommand(mappings, command, key);
        }
    }
}

} // namespace

EditorKeymapPersistence::EditorKeymapPersistence(
    juce::ApplicationCommandManager& command_manager, core::IEditorSettings& settings)
    : m_command_manager(command_manager)
    , m_settings(settings)
{
    if (const std::optional<std::string> stored = m_settings.keymapXml(); stored.has_value())
    {
        // An unparseable blob falls back to pure defaults; the next mapping change overwrites
        // it, so a corrupt settings file can never brick startup.
        if (const std::unique_ptr<juce::XmlElement> keymap = juce::parseXML(juce::String{*stored}))
        {
            removeUnrestorableEntries(*keymap);
            restoreKeymap(*m_command_manager.getKeyMappings(), *keymap);
        }
    }

    m_command_manager.getKeyMappings()->addChangeListener(this);
}

EditorKeymapPersistence::~EditorKeymapPersistence()
{
    m_command_manager.getKeyMappings()->removeChangeListener(this);
}

void EditorKeymapPersistence::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    const std::unique_ptr<juce::XmlElement> diff = m_command_manager.getKeyMappings()->createXml(
        /*saveDifferencesFromDefaultSet=*/true);

    std::optional<std::string> serialized;
    if (diff != nullptr && diff->getNumChildElements() > 0)
    {
        serialized = diff->toString(juce::XmlElement::TextFormat().singleLine()).toStdString();
    }

    if (serialized == m_settings.keymapXml())
    {
        return;
    }

    const std::expected<void, core::EditorSettingsError> saved =
        m_settings.setKeymapXml(std::move(serialized));
    if (!saved.has_value())
    {
        // Non-fatal for this session: the mapping still applies and the next change retries. It is
        // user-visible after a restart, though — the rebind silently reverts — so the reason is
        // logged rather than dropped.
        RH_LOG_WARNING(
            "editor.keybinds", "Could not persist the keymap: {:?}", saved.error().message);
    }
}

} // namespace rock_hero::editor::ui
