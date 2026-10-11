/*!
\file highway_player_settings.h
\brief The player's highway settings both products share.
*/

#pragma once

#include <optional>
#include <rock_hero/common/core/highway/highway_view_preferences.h>
#include <string>

namespace rock_hero::common::core
{

/*!
\brief Everything about the highway the player chooses, as the shared settings file stores it.

Shared because it describes the player, not one product's workspace: both products draw the
highway with the same theme and the same reading choices.
*/
struct HighwayPlayerSettings
{
    /*!
    \brief The id of the theme the player picked; absent when they never picked one.

    Resolved against the loaded themes by each product: no pick, or a pick no loaded theme has
    (a user theme since removed), draws with the default theme.
    */
    std::optional<std::string> theme_id;

    /*! \brief How the player reads the highway. */
    HighwayViewPreferences view{};

    /*!
    \brief Compares two settings records by their stored fields.
    \param lhs Left-hand settings.
    \param rhs Right-hand settings.
    \return True when both records store equal values.
    */
    friend bool operator==(const HighwayPlayerSettings& lhs, const HighwayPlayerSettings& rhs) =
        default;
};

} // namespace rock_hero::common::core
