#include "keybinds/editor_keymap_persistence.h"

#include "keybinds/editor_command_registry.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Drops stored mapping entries the current editor must not restore: unknown command ids (a
// newer editor's blob — release JUCE ignores them but debug builds hit a jassert inside
// restoreFromXml) and non-rebindable commands (the fixed core trio stays fixed even against a
// hand-edited settings file, which keeps the plugin-window shortcut mirror correct).
void removeUnrestorableEntries(juce::XmlElement& keymap)
{
    for (int index = keymap.getNumChildElements(); --index >= 0;)
    {
        juce::XmlElement* const entry = keymap.getChildElement(index);
        const juce::CommandID command_id = entry->getStringAttribute("commandId").getHexValue32();
        const EditorCommandSpec* const spec = findEditorCommandSpec(command_id);
        if (spec == nullptr || !spec->rebindable)
        {
            keymap.removeChildElement(entry, true);
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
            m_command_manager.getKeyMappings()->restoreFromXml(*keymap);
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
        // Non-fatal: the mapping still applies this session, and the next change retries.
    }
}

} // namespace rock_hero::editor::ui
