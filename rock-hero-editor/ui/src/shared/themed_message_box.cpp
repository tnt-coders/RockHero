#include "shared/themed_message_box.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

// Follows the promptForText() modal pattern: a directly constructed, self-deleting AlertWindow.
// The modal callback fires exactly once for every dismissal path — the OK button, Return (mapped
// to OK), and Escape (AlertWindow's built-in escape exit).
void showThemedWarningBox(
    juce::Component* associated_component, const juce::String& title, const juce::String& message,
    std::function<void()> on_dismissed)
{
    auto window = std::make_unique<juce::AlertWindow>(
        title, message, juce::MessageBoxIconType::WarningIcon, associated_component);
    window->addButton("OK", 0, juce::KeyPress{juce::KeyPress::returnKey});

    juce::AlertWindow* const window_ptr = window.release();
    window_ptr->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            // Distinct capture name: clang's -Wshadow-uncaptured-local flags `x = std::move(x)`.
            [owned_on_dismissed = std::move(on_dismissed)](int) {
                if (owned_on_dismissed)
                {
                    owned_on_dismissed();
                }
            }),
        true);
}

} // namespace rock_hero::editor::ui
