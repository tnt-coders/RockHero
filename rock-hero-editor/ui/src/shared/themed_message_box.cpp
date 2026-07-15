#include "shared/themed_message_box.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// Builds the fixed-choice box shared by every themed message box: buttons carry one-based modal
// return values so the built-in Escape exit (modal result 0) stays distinguishable, Return
// activates the first button, and Escape lands on the last one both through its explicit shortcut
// and through the built-in exit.
void showThemedButtonBox(
    juce::Component* associated_component, juce::MessageBoxIconType icon, const juce::String& title,
    const juce::String& message, const juce::StringArray& buttons,
    std::function<void(int)> on_choice)
{
    auto window = std::make_unique<juce::AlertWindow>(title, message, icon, associated_component);
    for (int index = 0; index < buttons.size(); ++index)
    {
        const juce::KeyPress return_shortcut =
            index == 0 ? juce::KeyPress{juce::KeyPress::returnKey} : juce::KeyPress{};
        const juce::KeyPress escape_shortcut = index == buttons.size() - 1
                                                   ? juce::KeyPress{juce::KeyPress::escapeKey}
                                                   : juce::KeyPress{};
        window->addButton(buttons[index], index + 1, return_shortcut, escape_shortcut);
    }

    const int last_index = buttons.size() - 1;
    showThemedDialogModally(
        std::move(window),
        associated_component,
        [last_index, owned_on_choice = std::move(on_choice)](int result) {
            if (owned_on_choice)
            {
                owned_on_choice(result == 0 ? last_index : result - 1);
            }
        });
}

// Adapts a no-argument dismissal callback to the button-index signature of the one-button boxes.
std::function<void(int)> ignoringChoice(std::function<void()> on_dismissed)
{
    return [owned_on_dismissed = std::move(on_dismissed)](int) {
        if (owned_on_dismissed)
        {
            owned_on_dismissed();
        }
    };
}

} // namespace

void showThemedDialogModally(
    std::unique_ptr<juce::AlertWindow> window, juce::Component* owner,
    std::function<void(int)> on_result)
{
    juce::AlertWindow* const window_ptr = window.release();
    window_ptr->enterModalState(
        true,
        // A self-deleting modal dialog can outlive the component that opened it (e.g. the editor is
        // torn down at app shutdown while a prompt is up), and the result callback captures that
        // component, so drop the result once the owner is gone. A null owner opts out. Distinct
        // capture name: clang's -Wshadow-uncaptured-local flags `x = std::move(x)`.
        juce::ModalCallbackFunction::create(
            [owner_guard = juce::Component::SafePointer<juce::Component>{owner},
             owner_was_set = owner != nullptr,
             owned_on_result = std::move(on_result)](int result) {
                if (!owned_on_result)
                {
                    return;
                }
                if (owner_was_set && owner_guard.getComponent() == nullptr)
                {
                    return;
                }
                owned_on_result(result);
            }),
        true);
}

void showThemedWarningBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    std::function<void()> on_dismissed)
{
    showThemedButtonBox(
        associated_component,
        juce::MessageBoxIconType::WarningIcon,
        title,
        message,
        {"OK"},
        ignoringChoice(std::move(on_dismissed)));
}

void showThemedQuestionBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    const juce::StringArray& buttons, std::function<void(int)> on_choice)
{
    showThemedButtonBox(
        associated_component,
        juce::MessageBoxIconType::QuestionIcon,
        title,
        message,
        buttons,
        std::move(on_choice));
}

void showThemedTextPrompt(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    const juce::String& initial_value, const juce::String& accept_label,
    std::function<void(const juce::String&)> on_accept)
{
    auto window = std::make_unique<juce::AlertWindow>(
        title, message, juce::MessageBoxIconType::QuestionIcon, associated_component);
    window->addTextEditor("value", initial_value);
    window->addButton(accept_label, 1, juce::KeyPress{juce::KeyPress::returnKey});
    window->addButton("Cancel", 0, juce::KeyPress{juce::KeyPress::escapeKey});

    // The modal callback runs before the self-deleting window is destroyed, so the raw pointer is
    // still valid for reading the entered text.
    const juce::AlertWindow* const window_ptr = window.get();
    showThemedDialogModally(
        std::move(window),
        associated_component,
        [window_ptr, owned_on_accept = std::move(on_accept)](int result) {
            if (result == 1 && owned_on_accept)
            {
                owned_on_accept(window_ptr->getTextEditorContents("value"));
            }
        });
}

} // namespace rock_hero::editor::ui
