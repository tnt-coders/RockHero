#include "timeline/grid_snap_warning_dialog.h"

#include "keybinds/editor_command_id.h"
#include "keybinds/key_chord_text.h"
#include "shared/themed_message_box.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

// One-based so the built-in Escape exit (modal result 0) stays distinguishable, and so every
// result that is not the explicit turn-off button reports the safe answer.
constexpr int g_keep_snapping_result = 1;
constexpr int g_turn_snapping_off_result = 2;

// The command's shipped display name, as the Grid menu and the actions list print it. Hardcoded
// where the chord is not: a rebind is user state the dialog must read live, a rename is a change
// to the command table this string is written against.
constexpr const char* g_command_name = "Grid Snap";

// Names the command the way the copy needs it: "Grid Snap (Ctrl · G)" while a chord is bound, and
// the bare name once a user has unbound every chord, so the sentences stay grammatical either way.
[[nodiscard]] juce::String commandPhrase(juce::ApplicationCommandManager& command_manager)
{
    const juce::String chord_text =
        commandChordText(command_manager, EditorCommandId::ToggleGridSnap);
    if (chord_text.isEmpty())
    {
        return g_command_name;
    }

    return juce::String{g_command_name} + " (" + chord_text + ")";
}

} // namespace

// Builds the two-button warning directly rather than through the fixed-shape message boxes: those
// put Return on the first button and Escape on the last, and this dialog needs both on the SAME
// (recommended) button, so no reflex can turn snapping off.
void GridSnapWarningDialog::show(
    juce::Component& anchor, juce::ApplicationCommandManager& command_manager,
    DecisionCallback on_decision)
{
    const juce::String command_phrase = commandPhrase(command_manager);
    auto window = std::make_unique<juce::AlertWindow>(
        "Turn grid snapping off?",
        command_phrase +
            ", which you just triggered, turns snapping off: everything you place would land on a "
            "raw 1/3840-note tick instead of on the grid. Free placement is for micro-timing fixes "
            "and is almost never what you want while charting. Use " +
            command_phrase + " again to turn snapping back on.",
        juce::MessageBoxIconType::WarningIcon,
        anchor.getTopLevelComponent());
    window->addButton(
        "Keep Snapping On (Recommended)",
        g_keep_snapping_result,
        juce::KeyPress{juce::KeyPress::returnKey},
        juce::KeyPress{juce::KeyPress::escapeKey});
    window->addButton("Turn Snapping Off", g_turn_snapping_off_result);

    showThemedDialogModally(
        std::move(window), &anchor, [owned_on_decision = std::move(on_decision)](int result) {
            if (!owned_on_decision)
            {
                return;
            }

            owned_on_decision(
                result == g_turn_snapping_off_result
                    ? core::GridSnapWarningDecision::TurnSnappingOff
                    : core::GridSnapWarningDecision::KeepSnappingOn);
        });
}

} // namespace rock_hero::editor::ui
