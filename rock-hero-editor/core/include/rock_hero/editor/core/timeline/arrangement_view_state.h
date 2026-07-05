/*!
\file arrangement_view_state.h
\brief Framework-free per-arrangement view state used by editor view contracts.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/song/audio_asset.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <string>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief View-facing state for the arrangement currently displayed by the editor.

This state stays focused on the information the current editor UI can actually render. In the
current stage that is one full-source waveform for the displayed arrangement.
*/
/*! \brief One selectable arrangement offered by the arrangement switcher. */
struct ArrangementChoiceViewState
{
    /*! \brief Stable arrangement id reported back through selection intents. */
    std::string id;

    /*! \brief User-facing label such as "Bass" or "Rhythm 2". */
    std::string label;

    /*! \brief True when this arrangement is currently displayed. */
    bool selected{false};

    /*!
    \brief Compares two arrangement choices by their stored values.
    \param lhs Left-hand arrangement choice.
    \param rhs Right-hand arrangement choice.
    \return True when both choices store equal values.
    */
    friend bool operator==(
        const ArrangementChoiceViewState& lhs, const ArrangementChoiceViewState& rhs) = default;
};

struct ArrangementViewState
{
    /*! \brief Backing audio currently rendered for the arrangement, if any. */
    std::optional<common::core::AudioAsset> audio_asset;

    /*! \brief Full natural duration of the rendered backing audio. */
    common::core::TimeDuration audio_duration;

    /*! \brief Selectable arrangements of the loaded song, in song order. */
    std::vector<ArrangementChoiceViewState> choices;

    /*!
    \brief Reports whether this state has playable audio assigned.
    \return True when an audio asset is present and has a positive duration.
    */
    [[nodiscard]] bool hasAudio() const noexcept
    {
        return audio_asset.has_value() && audio_duration.seconds > 0.0;
    }

    /*!
    \brief Calculates the visible timeline range for the current audio.
    \return Timeline range from zero through the audio duration.
    */
    [[nodiscard]] constexpr common::core::TimeRange audioTimelineRange() const noexcept
    {
        return common::core::TimeRange{
            .start = common::core::TimePosition{},
            .end = common::core::TimePosition{audio_duration.seconds},
        };
    }

    /*!
    \brief Compares two arrangement-view states by their stored values.
    \param lhs Left-hand arrangement-view state.
    \param rhs Right-hand arrangement-view state.
    \return True when both arrangement-view states store equal values.
    */
    friend bool operator==(const ArrangementViewState& lhs, const ArrangementViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
