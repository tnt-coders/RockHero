/*!
\file game_audio_source_state.h
\brief Adoption-readiness states of the game's audio configuration as an editor read source.
*/

#pragma once

#include <cstdint>

namespace rock_hero::editor::core
{

/*!
\brief Describes whether the game's audio configuration can be adopted by the editor.

Availability is calibration-aware, not device-presence: a game that recorded a device route but
never calibrated that exact route reports Uncalibrated rather than Available, so the editor never
arms a silent, locked, dead source. The two non-Available states are distinct because they steer
the user differently — NotConfigured means set up audio in the game, Uncalibrated means the setup
exists but calibration must still be performed in the game.
*/
enum class GameAudioSourceState : std::uint8_t
{
    /*! \brief The game's audio-config file is missing, unreadable, or stores no device route. */
    NotConfigured,

    /*! \brief The game stored a device route but no input calibration exists for that route. */
    Uncalibrated,

    /*! \brief The game stored a device route with a matching input calibration; adoptable. */
    Available,
};

} // namespace rock_hero::editor::core
