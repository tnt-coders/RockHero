/*!
\file game_audio_config.h
\brief Headless multi-input-aware model mapping physical routes to player slots.
*/

#pragma once

#include <optional>
#include <rock_hero/common/audio/input/input_device_identity.h>
#include <vector>

namespace rock_hero::game::core
{

/*!
\brief Binds one physical input route to one player slot.

The route reuses the shared common::audio::InputDeviceIdentity unchanged: gain calibration and
latency offsets stay keyed by that identity in the shared audio-config store, never on this type.
This record carries only the player-slot assignment the editor has no concept of, which is why it
lives in game/core rather than the shared schema.
*/
struct [[nodiscard]] PlayerInputConfig
{
    /*! \brief Zero-based player slot; always 0 at v1 (single-player). */
    int player_slot{0};

    /*! \brief Physical input route feeding this player slot. */
    common::audio::InputDeviceIdentity route;

    /*!
    \brief Compares two player-input configs by their stored values.
    \param lhs Left-hand player-input config.
    \param rhs Right-hand player-input config.
    \return True when both configs store equal values.
    */
    friend bool operator==(const PlayerInputConfig& lhs, const PlayerInputConfig& rhs) = default;
};

/*!
\brief The game's player-slot-to-route mapping, persisted through game/core.

v1 holds exactly one entry (slot 0), persisted and read back at startup, so the multi-input schema
is genuinely exercised rather than a hollow symmetry type. The multi-input boundary is honest and
additive: N players as N channels on the one active device is purely additive (append entries, each
naming a different channel index on the same active route). N players on separate physical
interfaces is genuine future work needing a multi-device active-route representation and a
multi-channel selection surface, and is out of v1 scope.
*/
struct [[nodiscard]] GameAudioConfig
{
    /*! \brief Player-slot-to-route bindings; empty until the player configures audio. */
    std::vector<PlayerInputConfig> players;

    /*!
    \brief Compares two game audio configs by their stored values.
    \param lhs Left-hand game audio config.
    \param rhs Right-hand game audio config.
    \return True when both configs store equal values.
    */
    friend bool operator==(const GameAudioConfig& lhs, const GameAudioConfig& rhs) = default;
};

/*!
\brief Selects the primary player's route (slot 0) for the shared-store identity mirror.

This is the pure mapping the P2 apply step feeds into the shared audio-config store's
activeDeviceRoute().identity — the single route-level fact the editor's "use game settings" toggle
reads. The store write itself is a P2 side effect; this function is only the contract.

\param config Game audio config to inspect.
\return Slot-0's route, or empty when no slot-0 player is configured.
*/
[[nodiscard]] inline std::optional<common::audio::InputDeviceIdentity> primaryPlayerRoute(
    const GameAudioConfig& config)
{
    for (const PlayerInputConfig& player : config.players)
    {
        if (player.player_slot == 0)
        {
            return player.route;
        }
    }
    return std::nullopt;
}

} // namespace rock_hero::game::core
