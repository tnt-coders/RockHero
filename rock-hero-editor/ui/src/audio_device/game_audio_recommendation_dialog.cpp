#include "audio_device/game_audio_recommendation_dialog.h"

#include "shared/themed_message_box.h"

#include <memory>
#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_use_game_result = 1;
constexpr int g_use_custom_result = 2;
constexpr int g_checkbox_width = 260;
constexpr int g_checkbox_height = 22;

// The editor's standard alert dialog carrying the one non-standard control: the suppression
// checkbox rides along as an owned custom component so its value can be read at decision time.
class RecommendationAlertWindow final : public juce::AlertWindow
{
public:
    explicit RecommendationAlertWindow(juce::Component* associated_component)
        : juce::AlertWindow(
              "Game audio settings detected",
              "Using the game's audio settings in the editor is recommended for the most "
              "consistent experience across the game and the editor.",
              juce::MessageBoxIconType::InfoIcon, associated_component)
    {
        m_suppress_checkbox.setComponentID("game_audio_recommendation_suppress");
        m_suppress_checkbox.setButtonText("Don't show this message again");
        m_suppress_checkbox.setSize(g_checkbox_width, g_checkbox_height);
        addCustomComponent(&m_suppress_checkbox);
        addButton("Use Game Audio Settings (Recommended)", g_use_game_result);
        // The decline path lands the user in the audio settings window (the view opens it on this
        // decision), so the button names that destination rather than an abstract "custom" mode.
        addButton("Open Audio Settings", g_use_custom_result);
    }

    RecommendationAlertWindow(const RecommendationAlertWindow&) = delete;
    RecommendationAlertWindow& operator=(const RecommendationAlertWindow&) = delete;
    RecommendationAlertWindow(RecommendationAlertWindow&&) = delete;
    RecommendationAlertWindow& operator=(RecommendationAlertWindow&&) = delete;
    ~RecommendationAlertWindow() override = default;

    [[nodiscard]] bool suppressChecked() const
    {
        return m_suppress_checkbox.getToggleState();
    }

private:
    juce::ToggleButton m_suppress_checkbox;
};

} // namespace

// Launches the custom-content dialog through the editor's shared modal launcher. Every dismissal
// path reaches the callback exactly once: the two buttons report their decisions and Escape
// reports Dismissed (modal result 0).
void GameAudioRecommendationDialog::show(juce::Component& anchor, DecisionCallback on_decision)
{
    auto window = std::make_unique<RecommendationAlertWindow>(anchor.getTopLevelComponent());
    const RecommendationAlertWindow* const window_ptr = window.get();
    showThemedDialogModally(
        std::move(window),
        &anchor,
        [window_ptr, owned_on_decision = std::move(on_decision)](int result) {
            if (!owned_on_decision)
            {
                return;
            }

            const core::GameAudioRecommendationDecision decision =
                result == g_use_game_result
                    ? core::GameAudioRecommendationDecision::UseGameSettings
                    : (result == g_use_custom_result
                           ? core::GameAudioRecommendationDecision::UseCustomSettings
                           : core::GameAudioRecommendationDecision::Dismissed);
            owned_on_decision(decision, window_ptr->suppressChecked());
        });
}

} // namespace rock_hero::editor::ui
