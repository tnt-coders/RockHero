/*!
\file session.h
\brief Editable session state for the current song workspace.
*/

#pragma once

#include <rock_hero/core/arrangement.h>
#include <rock_hero/core/timeline.h>
#include <vector>

namespace rock_hero::core
{

/*!
\brief Editable in-memory session state.

Session owns the arrangements available in the current editor workflow. It is deliberately
framework-free so controllers and tests can exercise session behavior without JUCE or Tracktion.
The current editor presents one arrangement at a time; until project-file loading exists, a new
session starts with a single empty arrangement shell.
*/
class Session
{
public:
    /*!
    \brief Returns arrangements in project order.
    \return Ordered arrangements owned by the session.
    */
    [[nodiscard]] const std::vector<Arrangement>& arrangements() const noexcept;

    /*!
    \brief Returns the project timeline range covered by loaded arrangement content.
    \return Current project timeline range.
    */
    [[nodiscard]] TimeRange timeline() const noexcept;

    /*!
    \brief Returns the arrangement currently displayed by the editor.
    \return Current arrangement, or nullptr if a future loaded project contains none.
    */
    [[nodiscard]] const Arrangement* currentArrangement() const noexcept;

    /*!
    \brief Sets the backing audio for the currently displayed arrangement.

    Editor orchestration should ask the playback backend to accept the candidate audio before
    storing it in Session, but Session deliberately stays framework-free and stores only the
    accepted project-owned values.

    \param audio_asset Audio asset to store on the arrangement.
    \param audio_duration Full natural duration of the accepted asset.
    \return True when the current arrangement existed and the audio values were stored.
    */
    bool setCurrentArrangementAudio(AudioAsset audio_asset, TimeDuration audio_duration);

private:
    // Starts with one shell because project-file loading is not available yet.
    std::vector<Arrangement> m_arrangements{Arrangement{}};

    // Canonical timeline range for loaded project content.
    TimeRange m_timeline{};
};

} // namespace rock_hero::core
