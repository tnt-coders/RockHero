#include "audio_device/game_audio_recommendation_dialog.h"

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
        addButton("Use game audio settings (recommended)", g_use_game_result);
        addButton("Use custom audio settings", g_use_custom_result);
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

// Launches the self-deleting standard alert, following the editor's promptForText() modal pattern.
// Every dismissal path reaches the modal callback exactly once: the two buttons report their
// decisions and Escape reports Dismissed (AlertWindow exits modal state with 0 on Escape).
void GameAudioRecommendationDialog::show(juce::Component& anchor, DecisionCallback on_decision)
{
    auto window = std::make_unique<RecommendationAlertWindow>(anchor.getTopLevelComponent());
    RecommendationAlertWindow* const window_ptr = window.release();
    window_ptr->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
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
            }),
        true);
}

} // namespace rock_hero::editor::ui
