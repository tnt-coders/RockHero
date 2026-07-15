/*!
\file game_audio_recommendation_dialog.h
\brief Private editor UI dialog recommending adoption of the game's audio settings.
*/

#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/editor/core/controller/editor_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Opens the startup dialog recommending the game's audio settings.

Shown when the "use game audio settings" toggle is off, a calibrated game configuration exists to
adopt, and the user has not suppressed the recommendation. Rendered as the editor's standard
juce::AlertWindow info dialog — matching the app's other prompts — with the recommended adopt path
and an "Open Audio Settings" decline path as buttons (the view follows the decline by opening the audio
device settings window), plus a "don't show this message again" checkbox whose value is reported
with every decision. Escape reports GameAudioRecommendationDecision::Dismissed.
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
    \brief Opens the self-deleting modal alert associated with the window that owns the anchor.
    \param anchor Component used to find the owning editor window for positioning.
    \param on_decision Called exactly once with the user's decision and checkbox value.
    */
    static void show(juce::Component& anchor, DecisionCallback on_decision);

private:
    GameAudioRecommendationDialog() = default;
};

} // namespace rock_hero::editor::ui
