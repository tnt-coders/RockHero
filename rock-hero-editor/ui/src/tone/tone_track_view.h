/*!
\file tone_track_view.h
\brief JUCE component that renders the tone track row below the backing waveform.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <rock_hero/editor/core/tone/tone_track_view_state.h>

namespace rock_hero::editor::ui
{

/*!
\brief Renders tone regions from framework-free state on the shared timeline canvas.

The first tone-track slice is read-only: the row shows where authored (or synthesized default)
tone regions sit on the song grid. Selection and editing intents arrive in later slices.
*/
class ToneTrackView final : public juce::Component
{
public:
    /*! \brief Creates the tone track row. */
    ToneTrackView();

    /*!
    \brief Stores the visible timeline range used to map region spans to pixels.
    \param visible_timeline Timeline range represented by the component width.
    */
    void setVisibleTimeline(common::core::TimeRange visible_timeline);

    /*!
    \brief Applies the current tone-track render state.
    \param state State derived by the editor controller.
    */
    void setState(const core::ToneTrackViewState& state);

    /*!
    \brief Paints the row background and the tone regions.
    \param g Graphics context used for drawing.
    */
    void paint(juce::Graphics& g) override;

private:
    // Visible timeline range represented by the component width.
    common::core::TimeRange m_visible_timeline{};

    // Last render state pushed by the editor controller.
    core::ToneTrackViewState m_state{};
};

} // namespace rock_hero::editor::ui
