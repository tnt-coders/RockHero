/*!
\file tempo_span_text.h
\brief Formats an anchor span's tempo for the ruler chip and the tempo prompt.
*/

#pragma once

#include <cstddef>
#include <rock_hero/common/core/timeline/tempo_map.h>
#include <string>

namespace rock_hero::editor::core
{

/*!
\brief Formats the quarter-note tempo of one anchor span as the shortest honest decimal.

Anchor seconds sit on a millisecond grid, so a stored span is uncertain by one millisecond and
every tempo whose span lies within that of the stored one describes the same map. The text is the
shortest such value with at most two decimals: a typed 137 reads back as "137", and an insert into
a span of a typed tempo leaves both halves reading it. A span whose stored length is already up to
a millisecond off its printed value can split into a half that prints more decimals: the half
inherits that error over a shorter length.

\param tempo_map Song tempo map.
\param span Zero-based span index: span i runs from anchor i to the anchor after it.
\return Tempo text such as "137" or "136.5", or empty when the span does not exist.
*/
[[nodiscard]] std::string tempoSpanBpmText(
    const common::core::TempoMap& tempo_map, std::size_t span);

} // namespace rock_hero::editor::core
