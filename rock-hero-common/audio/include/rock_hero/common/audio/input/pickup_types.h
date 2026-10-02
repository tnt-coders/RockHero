/*!
\file pickup_types.h
\brief The kinds of guitar pickup the automatic measurement distinguishes, as one table.
*/

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief The guitar's pickups, as far as the peak of a hard strum cares.

Only kinds whose hard strum differs by about 4 dB or more, or that a player would otherwise file
under the wrong kind, get their own value; finer distinctions (output tiers, Strat against Tele)
sit inside the spread every guitar has. pickupTypes() holds what each one covers. Active stays
the last kind: the table is sized through it, so a new kind goes after it and takes its place in
pickup_types.cpp's size check.
*/
enum class PickupClass : std::uint8_t
{
    /*! \brief Humbuckers. */
    Humbucker,

    /*! \brief Single coils. */
    SingleCoil,

    /*! \brief P-90s. */
    P90,

    /*! \brief Mini-humbuckers. */
    MiniHumbucker,

    /*! \brief Active pickups. */
    Active,
};

/*!
\brief One pickup kind: its name, what it covers, and the hard-strum peak the measurement assumes.

The one statement of each kind. The calibration window's chooser shows the name, the
measurement aims at the peak, and the user guide's Pickup Types table is generated from the rows.
*/
struct PickupType
{
    /*! \brief The kind this row describes. */
    PickupClass pickups{};

    /*!
    \brief The one name, lower case except where the name itself is capitalized ("humbucker",
    "P-90"), so it reads inside a sentence.
    */
    std::string_view name;

    /*! \brief What the kind covers, in sentences a player matches their guitar against. */
    std::string_view covers;

    /*!
    \brief The true sample peak a hard strum on this kind typically reaches, as the peak of the
    equivalent sine. Every guitar sits within about 6 dB of it. Sources:
    docs/tracking/2026-10-01-hard-strum-peak-research.md and
    docs/tracking/2026-10-01-pickup-type-output-research.md.
    */
    double hard_strum_peak_volts{};

    /*! \brief How the peak was established, for the guide's table. */
    std::string_view evidence;
};

/*!
\brief Returns every pickup kind, in the order a chooser lists them.
\return The rows, row i being the kind whose underlying value is i, so a kind's value indexes both
        the table and a chooser built from it; they live for the program's lifetime.
*/
[[nodiscard]] std::span<const PickupType> pickupTypes() noexcept;

/*!
\brief Returns the row for one pickup kind.
\param pickups The kind.
\return Its row in pickupTypes().
*/
[[nodiscard]] const PickupType& pickupType(PickupClass pickups) noexcept;

/*!
\brief Returns a pickup kind's name as a list item shows it: capitalized ("Humbucker", "P-90").
\param pickups The kind.
\return The name with its first letter in upper case.
*/
[[nodiscard]] std::string pickupTypeLabel(PickupClass pickups);

} // namespace rock_hero::common::audio
