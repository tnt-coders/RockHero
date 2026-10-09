/*!
\file tempo_scale.h
\brief Scale Tempo: the whole song's grid scaled by a factor, every time held.
*/

#pragma once

#include "timeline/timeline_readdress.h"

#include <expected>
#include <rock_hero/common/core/song/song_timeline.h>
#include <rock_hero/common/core/timeline/fraction.h>
#include <string>
#include <string_view>

namespace rock_hero::editor::core
{

/*!
\brief The largest numerator or denominator a scale factor may have once reduced.

A tempo repair halves, doubles, or converts between related meters (3/2, 4/3); a larger term is a
typo, and it would carry the song's places past the range a grid position can address.
*/
inline constexpr int g_max_scale_factor_term = 16;

/*!
\brief Reads the Scale Tempo prompt's text as a factor.

A positive whole number, or two around one slash, spaces and tabs around either allowed: "2",
"1/2", "3/2". The factor is reduced, and each of its terms must then be at most
\ref g_max_scale_factor_term.

\param text Text as typed.
\return The reduced factor, or the refusal's diagnostic for the log.
*/
[[nodiscard]] std::expected<common::core::Fraction, std::string> parseScaleFactor(
    std::string_view text);

/*!
\brief Scale Tempo's content map: each place w sent to f times w.
\param factor Positive scale factor.
\return The content map.
*/
[[nodiscard]] ContentMap scaledPlaces(common::core::Fraction factor);

/*!
\brief Scales every grid address in the song by a factor on the whole-note axis, every time held.

Scale Tempo's driver of \ref readdressTimeline: the content map is \ref scaledPlaces, and the
meters and anchors are placed by it too, each anchor keeping its milliseconds, so every span's
tempo is multiplied by the factor and its music keeps its time. x2 doubles the measure count — a
meter at measure m lands on measure 2m-1 — and is exact; /2 and a free factor such as 3/2 round
what lands between two ticks onto the nearer. A reign of k measures becomes f times k measures,
ending in a derived short bar where that is not whole, and the terminal stands where it lands. The
one refusal: a factor whose reduced denominator is not a power of two (4/3) can carry a meter off
the meter lattice (\ref common::core::isOnMeterLattice), where the bar it cuts has no meter;
rounding the meter's place apart from the content beside it would leave a note that stood on the
downbeat a tick off the barline, so the factor is refused naming the meter where it stands now.

\param timeline Timeline to scale.
\param factor Positive scale factor, as \ref parseScaleFactor reads one.
\return The scaled timeline, not yet validated, or the refusal's diagnostic for the log.
*/
[[nodiscard]] std::expected<common::core::SongTimeline, std::string> scaledTimeline(
    const common::core::SongTimeline& timeline, common::core::Fraction factor);

} // namespace rock_hero::editor::core
