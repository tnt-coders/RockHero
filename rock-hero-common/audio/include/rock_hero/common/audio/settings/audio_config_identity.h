/*!
\file audio_config_identity.h
\brief Application-name constants naming each product's audio-config file partition.
*/

#pragma once

#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief Application name for the editor's audio-config file.

Names a file distinct from the editor's workflow-state file ("Rock Hero Editor"), so the audio
config is partitioned from last-open-project, cursor, grid, and zoom state.

\return Stable editor audio-config application name text.
*/
[[nodiscard]] constexpr std::string_view editorAudioConfigApplicationName() noexcept
{
    return "Rock Hero Editor Audio";
}

/*!
\brief Application name for the game's audio-config file.

Names a file distinct from the game's profile/library file ("Rock Hero"), so the editor can read the
game's audio config read-only without touching the game's profile or library data.

\return Stable game audio-config application name text.
*/
[[nodiscard]] constexpr std::string_view gameAudioConfigApplicationName() noexcept
{
    return "Rock Hero Audio";
}

} // namespace rock_hero::common::audio
