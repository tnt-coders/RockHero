#include "audio_device/game_audio_recommendation_dialog.h"

#include "shared/editor_theme.h"

#include <utility>

namespace rock_hero::editor::ui
{

namespace
{

constexpr int g_dialog_width = 460;
constexpr int g_margin = 16;
constexpr int g_row_gap = 12;
constexpr int g_button_height = 28;
constexpr int g_checkbox_height = 22;

// Message body, checkbox, and the two decision buttons; the host window turns button clicks and
// bypass closes into exactly one decision callback.
class RecommendationContent final : public juce::Component
{
public:
    RecommendationContent(std::function<void()> on_use_game, std::function<void()> on_use_custom)
    {
        m_message.setComponentID("game_audio_recommendation_message");
        m_message.setText(
            "Rock Hero game audio settings were detected.\n\n"
            "Using the game's audio settings in the editor is recommended for the most consistent "
            "experience across the game and the editor.",
            juce::dontSendNotification);
        m_message.setJustificationType(juce::Justification::topLeft);
        m_message.setMinimumHorizontalScale(1.0F);

        m_suppress_checkbox.setComponentID("game_audio_recommendation_suppress");
        m_suppress_checkbox.setButtonText("Don't show this message again");

        m_use_game_button.setComponentID("game_audio_recommendation_use_game");
        m_use_game_button.setButtonText("Use game audio settings (recommended)");
        m_use_game_button.onClick = std::move(on_use_game);

        m_use_custom_button.setComponentID("game_audio_recommendation_use_custom");
        m_use_custom_button.setButtonText("Use custom audio settings");
        m_use_custom_button.onClick = std::move(on_use_custom);

        addAndMakeVisible(m_message);
        addAndMakeVisible(m_suppress_checkbox);
        addAndMakeVisible(m_use_game_button);
        addAndMakeVisible(m_use_custom_button);

        const int message_height = 96;
        const int content_height = g_margin + message_height + g_row_gap + g_checkbox_height +
                                   g_row_gap + (2 * g_button_height) + g_row_gap + g_margin;
        setSize(g_dialog_width, content_height);
    }

    [[nodiscard]] bool suppressChecked() const
    {
        return m_suppress_checkbox.getToggleState();
    }

    void resized() override
    {
        juce::Rectangle<int> area = getLocalBounds().reduced(g_margin);
        m_message.setBounds(area.removeFromTop(96));
        area.removeFromTop(g_row_gap);
        m_suppress_checkbox.setBounds(area.removeFromTop(g_checkbox_height));
        area.removeFromTop(g_row_gap);
        m_use_game_button.setBounds(area.removeFromTop(g_button_height));
        area.removeFromTop(g_row_gap);
        m_use_custom_button.setBounds(area.removeFromTop(g_button_height));
    }

private:
    juce::Label m_message;
    juce::ToggleButton m_suppress_checkbox;
    juce::TextButton m_use_game_button;
    juce::TextButton m_use_custom_button;
};

// Modal host that reports exactly one decision: a button click, or Dismissed for Escape and the
// native title-bar close. The checkbox value is read at decision time on every path.
class RecommendationDialogWindow final : public juce::DialogWindow
{
public:
    RecommendationDialogWindow(
        juce::Component* centering_component,
        GameAudioRecommendationDialog::DecisionCallback on_decision)
        : juce::DialogWindow(
              "Audio Settings", editorTheme().bar_background, true, true,
              centering_component != nullptr
                  ? juce::Component::getApproximateScaleFactorForComponent(centering_component)
                  : 1.0F)
        , m_on_decision(std::move(on_decision))
    {
        setUsingNativeTitleBar(true);
        auto content = std::make_unique<RecommendationContent>(
            [this] { finish(core::GameAudioRecommendationDecision::UseGameSettings); },
            [this] { finish(core::GameAudioRecommendationDecision::UseCustomSettings); });
        m_content = content.get();
        setContentOwned(content.release(), true);
        setResizable(false, false);
        centreAroundComponent(centering_component, getWidth(), getHeight());
    }

    RecommendationDialogWindow(const RecommendationDialogWindow&) = delete;
    RecommendationDialogWindow& operator=(const RecommendationDialogWindow&) = delete;
    RecommendationDialogWindow(RecommendationDialogWindow&&) = delete;
    RecommendationDialogWindow& operator=(RecommendationDialogWindow&&) = delete;
    ~RecommendationDialogWindow() override = default;

    // Starts a non-auto-delete modal lifetime; the external owner releases the window after the
    // decision callback has been delivered.
    void showModal()
    {
        enterModalState(true, nullptr, false);
    }

    // Title-bar close is a dismissal: nothing is persisted beyond the checkbox and the prompt may
    // re-ask on a later launch.
    void closeButtonPressed() override
    {
        finish(core::GameAudioRecommendationDecision::Dismissed);
    }

    // Escape matches the title-bar close instead of DialogWindow's default hide-only behavior.
    bool escapeKeyPressed() override
    {
        finish(core::GameAudioRecommendationDecision::Dismissed);
        return true;
    }

private:
    // Delivers the single decision, then hides and leaves disposal to the external owner.
    void finish(core::GameAudioRecommendationDecision decision)
    {
        if (m_decision_sent)
        {
            return;
        }

        m_decision_sent = true;
        const bool suppress = m_content != nullptr && m_content->suppressChecked();
        setVisible(false);
        if (isCurrentlyModal(false))
        {
            exitModalState(0);
        }
        if (m_on_decision)
        {
            auto on_decision = std::move(m_on_decision);
            on_decision(decision, suppress);
        }
    }

    GameAudioRecommendationDialog::DecisionCallback m_on_decision;

    // Owned by the DialogWindow content ownership; read for the checkbox value at decision time.
    RecommendationContent* m_content{nullptr};

    // Guards the one-shot decision against repeated close paths.
    bool m_decision_sent{false};
};

} // namespace

// Launches the recommendation dialog centered on the editor window that owns the anchor.
std::unique_ptr<juce::DocumentWindow> GameAudioRecommendationDialog::show(
    juce::Component& anchor, DecisionCallback on_decision)
{
    auto window = std::make_unique<RecommendationDialogWindow>(
        anchor.getTopLevelComponent(), std::move(on_decision));
    window->setVisible(true);
    window->showModal();
    return std::unique_ptr<juce::DocumentWindow>{std::move(window)};
}

} // namespace rock_hero::editor::ui
