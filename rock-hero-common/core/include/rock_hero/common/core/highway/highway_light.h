/*!
\file highway_light.h
\brief When a hand's light is lit, and the one envelope every floor layer shapes it by.
*/

#pragma once

#include <compare>
#include <span>

namespace rock_hero::common::core
{

/*!
\brief WHEN one hand's light is lit: rising over \ref rise_seconds to full at \ref start_seconds,
full through \ref release_seconds, then decaying.

Where the light stands is not part of the stretch: that is the hand's track in the board's one
motion element (\ref HighwayHandArrival). How fast it decays is not part of it either: the decay
belongs to the LAYER that draws the light, never to the hand. A stretch gathered as evidence, before
\ref foldLitEvidence and \ref crowdedAfter, carries its rise unclamped.
*/
struct HighwayLitStretch
{
    /*! \brief Absolute position the light reaches full: the strike it lights. */
    double start_seconds{0.0};

    /*! \brief When the hand leaves: the light's decay begins here. */
    double release_seconds{0.0};

    /*!
    \brief Duration of the light's rise ending at \ref start_seconds; never negative, and clamped
    by \ref crowdedAfter so a light never rises back through an earlier hold.
    */
    double rise_seconds{0.0};

    /*!
    \brief Compares two stretches by their stored fields.
    \param lhs Left-hand stretch.
    \param rhs Right-hand stretch.
    \return True when both stretches store equal values.

    Exact field equality: stretches are compared against values the projection produced, so is_eq
    keeps GCC's -Wfloat-equal satisfied that the exactness is intended.
    */
    friend constexpr bool operator==(
        const HighwayLitStretch& lhs, const HighwayLitStretch& rhs) noexcept
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.release_seconds <=> rhs.release_seconds) &&
               std::is_eq(lhs.rise_seconds <=> rhs.rise_seconds);
    }
};

/*! \brief The closed span of time over which a stretch's light is above zero on one layer. */
struct HighwayLitInterval
{
    /*! \brief Absolute position the rise starts. */
    double from_seconds{0.0};

    /*! \brief Absolute position the decay ends. */
    double to_seconds{0.0};
};

/*!
\brief Returns the interval a stretch's light can be seen on a layer: [start - rise, release +
decay].

THE lit interval: every skip test and range bound that asks whether a light can reach an instant
asks this, so the extent a light occupies is stated once.

\param stretch Stretch whose light is asked about.
\param decay_seconds The drawing layer's decay after the release.
\return The interval from the rise's start to the decay's end.
*/
[[nodiscard]] HighwayLitInterval highwayLitInterval(
    const HighwayLitStretch& stretch, double decay_seconds) noexcept;

/*!
\brief Returns a stretch's light level at an instant on a layer with the given decay.

THE envelope: zero before the rise, a linear rise to full at the start (none at all when the rise is
zero), full through the hold up to and including the release, and a linear decay to zero over
\p decay_seconds after it.

\param stretch Stretch whose light is sampled.
\param seconds Absolute position to sample at.
\param decay_seconds The drawing layer's decay after the release.
\return Light level in [0, 1].
*/
[[nodiscard]] double highwayLightLevel(
    const HighwayLitStretch& stretch, double seconds, double decay_seconds) noexcept;

/*!
\brief Folds gathered evidence into one light.

THE fold: the light starts at the earliest start, releases at the latest release, and rises over the
widest rise among the items starting within \ref g_onset_match_epsilon of that earliest start — a
later item's rise ends at its own start, where the light is already full.

\param items Evidence to fold; must not be empty.
\return The folded stretch, its rise not yet crowding-clamped.
*/
[[nodiscard]] HighwayLitStretch foldLitEvidence(std::span<const HighwayLitStretch> items) noexcept;

/*!
\brief Returns a stretch with its rise clamped so it never rises back past the previous stretch's
release.

THE crowding clamp, mirroring the fret-hand ramps: the rise is clamped to [0, the gap between the
previous release and this start], the gap floored at zero, so a dense run keeps a dip between its
lights.

\param stretch Stretch to clamp.
\param previous The stretch lit immediately before it.
\return \p stretch with its rise clamped to the gap.
*/
[[nodiscard]] HighwayLitStretch crowdedAfter(
    HighwayLitStretch stretch, const HighwayLitStretch& previous) noexcept;

} // namespace rock_hero::common::core
