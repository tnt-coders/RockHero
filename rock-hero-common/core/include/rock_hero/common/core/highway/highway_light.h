/*!
\file highway_light.h
\brief When a hand's light is lit, the one envelope every floor layer shapes it by, and its pops.
*/

#pragma once

#include <compare>
#include <optional>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief WHEN one hand's light is lit: rising over \ref rise_seconds to full at \ref start_seconds,
full through \ref release_seconds, then decaying.

Where the light stands is not part of the stretch: that is the hand's track in the board's one
motion element (\ref HighwayHandArrival). How fast it decays is not part of it either: the decay
belongs to the LAYER that draws the light, never to the hand. A stretch gathered as evidence, before
\ref mergeLitEvidence, carries its rise unclamped.
*/
struct HighwayLitStretch
{
    /*! \brief Absolute position the light reaches full: the strike it lights. */
    double start_seconds{0.0};

    /*! \brief When the hand leaves: the light's decay begins here. */
    double release_seconds{0.0};

    /*!
    \brief Duration of the light's rise ending at \ref start_seconds; never negative, and clamped
    by \ref mergeLitEvidence so a light never rises back through an earlier hold.
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
\brief THE ESTABLISHMENT RULE: a hand's position stays established across a gap shorter than this.

Two things read it. The fretting hand's light merges its evidence under it (\ref mergeLitEvidence):
released at each note's drawn end, that light would dip at every margin trim and strobe through a
sustainless chug riff, so a dark gap has to be long enough to read as a rest rather than as a
flicker before the light goes out. And a repeated tap within it is an established position, so its
number is not printed again. Seconds rather than beats because that is a readability question, not
a musical one — the minimum sustain distance, the same question for tails, reads consistently
across slow and fast songs because it is time-based — and because the light's own rise and decay
are already in seconds. The value is a sighting knob.
*/
inline constexpr double g_hand_rest_seconds = 1.0;

/*!
\brief The picking hand's light merges its evidence under this gap (\ref mergeLitEvidence).

Zero: every strike is its own light, and the dip between strikes mirrors the finger lifting. Strikes
that overlap still merge, since their gap is negative. A look chosen on sight, distinct from the
establishment rule (\ref g_hand_rest_seconds) the picking hand's labels still follow.
*/
inline constexpr double g_pick_light_rest_seconds = 0.0;

/*!
\brief Merges gathered evidence into a hand's lit stretches.

THE merge: sorted by start, evidence gathers into one run while each next item starts less than
\p rest_seconds after the latest release seen in the run so far — a gap of exactly the tolerance
splits, and a gap narrower than \ref g_onset_match_epsilon never does, so the members of one strike
stay one light even under a zero tolerance. Each run folds into one stretch: the earliest start,
the latest release, and the widest rise among the items starting within
\ref g_onset_match_epsilon of that earliest start (a later item's rise ends at its own start,
where the light is already full). Each stretch is then crowded after the one before it: its rise
is clamped to the gap after the previous release, so a light never rises back through an earlier
hold.

Taken by value on purpose: the merge sorts its evidence and folds it in place, returning the same
storage.

\param items Evidence, one stretch per proof, rises unclamped, in any order.
\param rest_seconds The rest tolerance; never negative.
\return The lit stretches: disjoint and ascending, rises crowding-clamped.
*/
[[nodiscard]] std::vector<HighwayLitStretch> mergeLitEvidence(
    std::vector<HighwayLitStretch> items, double rest_seconds);

/*!
\brief One strike-glow pop of a hand: when it flashes, how long it fades, and which strips it lands
on.

A strike, a slide landing or a bend arrival pops the two wires bounding its slot; a boxed strike,
and a lone open whose bar spans the window, pops the hand's box sides. The release is already
clamped (\ref highwayHitGlowRelease) against the next pop of the same hand on the same strips, so
the renderer reads it as a chart fact instead of re-deriving it per frame.
*/
struct HighwayStrikePop
{
    /*! \brief Absolute position the pop flashes. */
    double onset_seconds{0.0};

    /*! \brief How long the pop takes to fade, clamped against the next pop on its strips. */
    double release_seconds{0.0};

    /*!
    \brief The fret whose slot's two wires pop; no value for a pop on the hand's box sides, which
    stand wherever the hand's window stands.
    */
    std::optional<int> fret;

    /*!
    \brief Compares two pops by their stored fields.
    \param lhs Left-hand pop.
    \param rhs Right-hand pop.
    \return True when both pops store equal values.

    Exact field equality: pops are compared against values the projection produced, so is_eq keeps
    GCC's -Wfloat-equal satisfied that the exactness is intended.
    */
    friend constexpr bool operator==(
        const HighwayStrikePop& lhs, const HighwayStrikePop& rhs) noexcept
    {
        return std::is_eq(lhs.onset_seconds <=> rhs.onset_seconds) &&
               std::is_eq(lhs.release_seconds <=> rhs.release_seconds) && lhs.fret == rhs.fret;
    }
};

} // namespace rock_hero::common::core
