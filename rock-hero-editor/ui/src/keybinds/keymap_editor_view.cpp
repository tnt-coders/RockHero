#include "keybinds/keymap_editor_view.h"

#include "keybinds/editor_command_registry.h"
#include "shared/editor_theme.h"
#include "shared/text_metrics.h"
#include "shared/themed_message_box.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_row_height{28};
constexpr int g_header_height{26};
constexpr int g_row_inset{12};
constexpr int g_chip_gap{6};
constexpr int g_chip_min_width{44};
constexpr int g_chip_text_pad{18};
constexpr int g_reset_strip_height{40};
constexpr float g_row_font_height{15.0f};
constexpr float g_chip_font_height{13.0f};

// The press-a-key capture window. Buttons decline focus and register no key shortcuts so every
// chord — Return and Escape included — is capturable, matching the stock component's capture
// semantics; OK and Cancel are mouse-only here.
class KeyCaptureDialog final : public juce::AlertWindow
{
public:
    KeyCaptureDialog(juce::ApplicationCommandManager& command_manager, juce::Component* owner)
        : juce::AlertWindow(
              "Set Key Binding", "Press a key combination now...", juce::MessageBoxIconType::NoIcon,
              owner)
        , m_command_manager(command_manager)
    {
        addButton("OK", 1);
        addButton("Cancel", 0);
        for (juce::Component* const child : getChildren())
        {
            child->setWantsKeyboardFocus(false);
        }
        setWantsKeyboardFocus(true);
    }

    // Records the pressed chord and previews it, naming the current owner when the chord is
    // already assigned so the user sees the coming overwrite before confirming.
    bool keyPressed(const juce::KeyPress& key) override
    {
        m_captured = key;
        juce::String message = "Key: " + key.getTextDescription();
        const juce::CommandID owner_id =
            m_command_manager.getKeyMappings()->findCommandForKeyPress(key);
        if (owner_id != 0)
        {
            message << "\n\n(Currently assigned to \""
                    << m_command_manager.getNameOfCommand(owner_id) << "\")";
        }
        setMessage(message);
        return true;
    }

    // Consumes key-up events too so held modifiers never leak shortcuts into the editor.
    bool keyStateChanged(bool /*is_key_down*/) override
    {
        return true;
    }

    // Focus can only be grabbed once the modal entry has attached the window to the desktop.
    void visibilityChanged() override
    {
        if (isVisible())
        {
            grabKeyboardFocus();
        }
    }

    /*! \brief Returns the last chord pressed, or an invalid key when none was. */
    [[nodiscard]] const juce::KeyPress& captured() const noexcept
    {
        return m_captured;
    }

private:
    juce::ApplicationCommandManager& m_command_manager;
    juce::KeyPress m_captured{};
};

} // namespace

// One binding chip: a themed rounded button showing a chord (or "+" for the add affordance).
class KeymapEditorView::ChipButton final : public juce::Button
{
public:
    ChipButton(const juce::String& text, bool interactive)
        : juce::Button(text)
    {
        setEnabled(interactive);
        setWantsKeyboardFocus(false);
    }

    void paintButton(juce::Graphics& g, bool is_over, bool is_down) override
    {
        const EditorTheme& theme = editorTheme();
        const juce::Rectangle<float> bounds = getLocalBounds().toFloat().reduced(1.0f);
        juce::Colour fill = theme.bar_background;
        if (isEnabled() && (is_over || is_down))
        {
            fill = fill.brighter(is_down ? 0.25f : 0.12f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(isEnabled() ? theme.grid_measure : theme.grid_beat);
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
        g.setColour(isEnabled() ? theme.primary_text : theme.muted_text);
        g.setFont(juce::Font{juce::FontOptions{g_chip_font_height}});
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, true);
    }
};

// A bold category strip separating the registry's command groups.
class KeymapEditorView::CategoryHeader final : public juce::Component
{
public:
    explicit CategoryHeader(juce::String text)
        : m_text(std::move(text))
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        const EditorTheme& theme = editorTheme();
        g.fillAll(theme.panel_header);
        g.setColour(theme.primary_text);
        g.setFont(juce::Font{juce::FontOptions{g_row_font_height, juce::Font::bold}});
        g.drawText(
            m_text,
            getLocalBounds().withTrimmedLeft(g_row_inset),
            juce::Justification::centredLeft,
            true);
    }

private:
    juce::String m_text;
};

// One command row: the display name on the left, binding chips (and the add chip for
// rebindable commands) laid out from the right edge.
class KeymapEditorView::CommandRow final : public juce::Component
{
public:
    CommandRow(KeymapEditorView& owner, const EditorCommandSpec& spec)
        : m_owner(owner)
        , m_spec(spec)
    {
        setComponentID("keymap_row_" + juce::String::toHexString(toJuceCommandId(spec.id)));

        const juce::Array<juce::KeyPress> keys =
            m_owner.m_command_manager.getKeyMappings()->getKeyPressesAssignedToCommand(
                toJuceCommandId(spec.id));
        for (int index = 0; index < keys.size(); ++index)
        {
            auto chip =
                std::make_unique<ChipButton>(keys[index].getTextDescription(), spec.rebindable);
            chip->setComponentID(
                "keymap_chip_" + juce::String::toHexString(toJuceCommandId(spec.id)) + "_" +
                juce::String{index});
            if (spec.rebindable)
            {
                chip->onClick = [this, index] { showChipMenu(index); };
            }
            addAndMakeVisible(*chip);
            m_chips.push_back(std::move(chip));
        }

        if (spec.rebindable)
        {
            m_add_chip = std::make_unique<ChipButton>("+", true);
            m_add_chip->setComponentID(
                "keymap_add_" + juce::String::toHexString(toJuceCommandId(spec.id)));
            m_add_chip->onClick = [this] { m_owner.beginCapture(m_spec.id, -1); };
            addAndMakeVisible(*m_add_chip);
        }
    }

    void paint(juce::Graphics& g) override
    {
        const EditorTheme& theme = editorTheme();
        g.setColour(m_spec.rebindable ? theme.primary_text : theme.muted_text);
        g.setFont(juce::Font{juce::FontOptions{g_row_font_height}});
        g.drawText(
            m_spec.name,
            getLocalBounds().withTrimmedLeft(g_row_inset),
            juce::Justification::centredLeft,
            true);
    }

    void resized() override
    {
        const juce::Font chip_font{juce::FontOptions{g_chip_font_height}};
        int right = getWidth() - g_row_inset;
        const auto place = [&right, this](juce::Component& chip, const juce::String& text) {
            const juce::Font font{juce::FontOptions{g_chip_font_height}};
            const int width = std::max(g_chip_min_width, textWidth(font, text) + g_chip_text_pad);
            chip.setBounds(right - width, 3, width, getHeight() - 6);
            right -= width + g_chip_gap;
        };
        if (m_add_chip != nullptr)
        {
            place(*m_add_chip, "+");
        }
        for (int index = static_cast<int>(m_chips.size()); --index >= 0;)
        {
            place(
                *m_chips[static_cast<std::size_t>(index)],
                m_chips[static_cast<std::size_t>(index)]->getButtonText());
        }
    }

private:
    // Offers change/remove for one existing binding. The menu owns no state: the callbacks
    // capture the view and plain values, and the deletion check guards against this row being
    // rebuilt while the menu is open.
    void showChipMenu(int key_index)
    {
        juce::PopupMenu menu;
        const juce::Component::SafePointer<KeymapEditorView> safe_owner{&m_owner};
        const EditorCommandId command = m_spec.id;
        menu.addItem("Change this binding...", [safe_owner, command, key_index] {
            if (safe_owner != nullptr)
            {
                safe_owner->beginCapture(command, key_index);
            }
        });
        menu.addItem("Remove this binding", [safe_owner, command, key_index] {
            if (safe_owner != nullptr)
            {
                safe_owner->removeBinding(command, key_index);
            }
        });
        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(m_chips[static_cast<std::size_t>(key_index)].get())
                .withDeletionCheck(*this));
    }

    KeymapEditorView& m_owner;
    const EditorCommandSpec& m_spec;
    std::vector<std::unique_ptr<ChipButton>> m_chips;
    std::unique_ptr<ChipButton> m_add_chip;
};

KeymapEditorView::KeymapEditorView(juce::ApplicationCommandManager& command_manager)
    : m_command_manager(command_manager)
{
    m_viewport.setViewedComponent(&m_row_list, false);
    m_viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(m_viewport);
    m_reset_button.setComponentID("keymap_reset_all_button");
    m_reset_button.onClick = [this] { confirmResetAll(); };
    addAndMakeVisible(m_reset_button);
    rebuildRows();
    m_command_manager.getKeyMappings()->addChangeListener(this);
}

KeymapEditorView::~KeymapEditorView()
{
    m_command_manager.getKeyMappings()->removeChangeListener(this);
}

void KeymapEditorView::paint(juce::Graphics& g)
{
    g.fillAll(editorTheme().panel_background);
}

void KeymapEditorView::resized()
{
    juce::Rectangle<int> bounds = getLocalBounds();
    juce::Rectangle<int> reset_strip = bounds.removeFromBottom(g_reset_strip_height);
    m_reset_button.changeWidthToFitText(reset_strip.getHeight() - 16);
    m_reset_button.setBounds(
        reset_strip.reduced(g_row_inset, 8).removeFromRight(m_reset_button.getWidth()));
    m_viewport.setBounds(bounds);

    // Row widths track the viewport; heights were fixed by the rebuild.
    const int row_width = m_viewport.getMaximumVisibleWidth();
    int y = 0;
    for (const std::unique_ptr<juce::Component>& row : m_rows)
    {
        row->setBounds(0, y, row_width, row->getHeight());
        y += row->getHeight();
    }
    m_row_list.setSize(row_width, y);
}

void KeymapEditorView::applyBindingChange(
    EditorCommandId command, const juce::KeyPress& key, int replace_index)
{
    const EditorCommandSpec* const spec = findEditorCommandSpec(toJuceCommandId(command));
    if (spec == nullptr || !spec->rebindable || !key.isValid())
    {
        return;
    }

    // The remove-then-add dance: strip the chord from whichever command owns it, drop the
    // binding being replaced, then assign — exactly one owner remains. addKeyPress alone must
    // never be trusted to resolve conflicts (its documented removal does not exist in code).
    juce::KeyPressMappingSet& mappings = *m_command_manager.getKeyMappings();
    mappings.removeKeyPress(key);
    if (replace_index >= 0)
    {
        mappings.removeKeyPress(toJuceCommandId(command), replace_index);
    }
    mappings.addKeyPress(toJuceCommandId(command), key, replace_index);
}

void KeymapEditorView::removeBinding(EditorCommandId command, int key_index)
{
    m_command_manager.getKeyMappings()->removeKeyPress(toJuceCommandId(command), key_index);
}

void KeymapEditorView::rebuildRows()
{
    m_rows.clear();
    const char* current_category = "";
    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        if (juce::String{spec.category} != juce::String{current_category})
        {
            current_category = spec.category;
            auto header = std::make_unique<CategoryHeader>(juce::String{spec.category});
            header->setSize(0, g_header_height);
            m_row_list.addAndMakeVisible(*header);
            m_rows.push_back(std::move(header));
        }
        auto row = std::make_unique<CommandRow>(*this, spec);
        row->setSize(0, g_row_height);
        m_row_list.addAndMakeVisible(*row);
        m_rows.push_back(std::move(row));
    }
    resized();
}

void KeymapEditorView::beginCapture(EditorCommandId command, int replace_index)
{
    auto dialog = std::make_unique<KeyCaptureDialog>(m_command_manager, this);
    const KeyCaptureDialog* const dialog_ptr = dialog.get();
    const juce::Component::SafePointer<KeymapEditorView> safe_this{this};
    // The modal callback runs before the self-deleting dialog is destroyed, so the raw pointer
    // is still valid for reading the captured chord.
    showThemedDialogModally(
        std::move(dialog), this, [safe_this, dialog_ptr, command, replace_index](int result) {
            if (safe_this == nullptr || result != 1)
            {
                return;
            }
            safe_this->confirmAndApply(command, dialog_ptr->captured(), replace_index);
        });
}

void KeymapEditorView::confirmAndApply(
    EditorCommandId command, const juce::KeyPress& key, int replace_index)
{
    if (!key.isValid())
    {
        return;
    }

    const juce::CommandID owner_id =
        m_command_manager.getKeyMappings()->findCommandForKeyPress(key);
    if (owner_id == 0 || owner_id == toJuceCommandId(command))
    {
        applyBindingChange(command, key, replace_index);
        return;
    }

    const juce::Component::SafePointer<KeymapEditorView> safe_this{this};
    showThemedQuestionBox(
        this,
        "Change Key Binding",
        "\"" + key.getTextDescription() + "\" is already assigned to \"" +
            m_command_manager.getNameOfCommand(owner_id) + "\".\n\nRe-assign it to \"" +
            juce::String{findEditorCommandSpec(toJuceCommandId(command))->name} + "\" instead?",
        {"Re-assign", "Cancel"},
        [safe_this, command, key, replace_index](int choice) {
            if (safe_this != nullptr && choice == 0)
            {
                safe_this->applyBindingChange(command, key, replace_index);
            }
        });
}

void KeymapEditorView::confirmResetAll()
{
    const juce::Component::SafePointer<KeymapEditorView> safe_this{this};
    showThemedQuestionBox(
        this,
        "Reset Key Bindings",
        "Reset every key binding to its default?",
        {"Reset", "Cancel"},
        [safe_this](int choice) {
            if (safe_this != nullptr && choice == 0)
            {
                safe_this->m_command_manager.getKeyMappings()->resetToDefaultMappings();
            }
        });
}

// Mapping-set broadcasts are asynchronous, so this rebuild never destroys the control whose
// click produced the change.
void KeymapEditorView::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    rebuildRows();
}

} // namespace rock_hero::editor::ui
