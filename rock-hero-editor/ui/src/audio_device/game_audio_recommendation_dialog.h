/*!
\file game_audio_recommendation_dialog.h
\brief Private editor UI dialog recommending adoption of the game's audio settings.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Opens the startup dialog recommending the game's audio settings.

Shown when the "use game audio settings" toggle is off, a calibrated game configuration exists to
adopt, and the user has not suppressed the recommendation. Offers the recommended adopt path and
the keep-custom path as buttons, plus a "don't show this message again" checkbox whose value is
reported with every decision — including a dismissal via Escape or the title-bar close, which
reports GameAudioRecommendationDecision::Dismissed.
*/
class GameAudioRecommendationDialog final
{
public:
    /*!
    \brief Called exactly once when the dialog is answered or dismissed.

    Receives the decision plus the "don't show this message again" checkbox value at close time.
    */
    using DecisionCallback =
        std::function<void(core::GameAudioRecommendationDecision decision, bool suppress_future)>;

    /*!
    \brief Opens the modal dialog centered on the window that owns the anchor.
    \param anchor Component used to find the owning editor window for centering.
    \param on_decision Called exactly once with the user's decision and checkbox value.
    \return The opened window. The caller owns it and should release it after the decision.
    */
    [[nodiscard]] static std::unique_ptr<juce::DocumentWindow> show(
        juce::Component& anchor, DecisionCallback on_decision);

private:
    GameAudioRecommendationDialog() = default;
};

} // namespace rock_hero::editor::ui
