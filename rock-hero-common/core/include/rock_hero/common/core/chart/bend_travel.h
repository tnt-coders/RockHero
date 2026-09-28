/*!
\file bend_travel.h
\brief How far a string physically travels to bend a pitch, the one law both surfaces draw.
*/

#pragma once

#include <cmath>

namespace rock_hero::common::core
{

/*!
\brief The finest bend a charter writes, in semitones: a quarter step.

The grid every bend amount is spelled on — the chips' "1 1/4" and the picker's rows alike.
*/
inline constexpr double g_bend_quarter_step_semitones = 0.5;

/*!
\brief The largest bend the surfaces scale to, in semitones: three whole steps, Guitar Pro's
maximum.

The 2D curve's ceiling, and the top of the amounts a charter is offered; the corpus puts 99.9% of
real bends at or below half of it.
*/
inline constexpr double g_bend_ceiling_semitones = 6.0;

/*!
\brief Returns how far a string travels sideways to bend \p semitones, in units of the travel a
half step takes.

The PHYSICAL displacement law. Lateral travel d stretches the string by about d squared and Hooke
turns that stretch into tension, so displacement squared is proportional to the tension GAIN — and
reaching n semitones means raising the tension ratio to 2^(n/6), because pitch is logarithmic in
frequency and frequency rises with the square root of tension (a full three-whole-step bend
literally doubles the tension). Every string-gauge, scale-length, and fret-position specific lands
in one constant, which the half-step unit divides out, so this one curve is exact for every note.
The travel runs 1.00, 1.46, 1.84, 2.19, 2.53, 2.86 over the first six semitones: each extra
semitone moves the string less than the one before even as the force keeps climbing, which is what
makes a drawn bend read as a string being bent rather than a pitch plot. Negative offsets mirror
the same curve.

\param semitones Pitch offset above the unbent string in half steps; fractional values are
       ordinary (a bend in progress).
\return The travel, signed like \p semitones.
*/
[[nodiscard]] inline double bendTravel(const double semitones)
{
    const double tension_gain = std::exp2(std::abs(semitones) / 6.0) - 1.0;
    const double half_step_gain = std::exp2(1.0 / 6.0) - 1.0;
    return std::copysign(std::sqrt(tension_gain / half_step_gain), semitones);
}

} // namespace rock_hero::common::core
