/*!
\file pan.h
\brief Project-owned stereo pan value type with its clamping policy and one text format.
*/

#pragma once

#include <algorithm>
#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace rock_hero::common::audio
{

/*!
\brief Stereo pan position carried through the live rig boundary and persisted in tone documents.

A balance with unity at centre: centre leaves both channels untouched, and moving off centre only
attenuates the far channel, so a stereo signal keeps its width and a dual-mono one is placed like a
pan. Pass by value; the type is trivially copyable.

\see clampPan
\see formatPan
\see parsePan
*/
struct Pan
{
    /*! \brief Position from -1 (full left) through 0 (centre) to +1 (full right). */
    double position{0.0};

    /*!
    \brief Compares two pan values by their positions.
    \param lhs Left-hand pan value.
    \param rhs Right-hand pan value.
    \return True when both pan values store equal positions.
    */
    friend constexpr bool operator==(Pan lhs, Pan rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.position <=> rhs.position);
    }
};

/*!
\brief Returns the leftmost pan position the system accepts.
\return Full-left position.
*/
[[nodiscard]] constexpr double minimumPanPosition() noexcept
{
    return -1.0;
}

/*!
\brief Returns the rightmost pan position the system accepts.
\return Full-right position.
*/
[[nodiscard]] constexpr double maximumPanPosition() noexcept
{
    return 1.0;
}

/*!
\brief Returns the pan clamped to the accepted range.
\param pan Input pan value.
\return Pan with its position clamped to [minimumPanPosition(), maximumPanPosition()].
*/
[[nodiscard]] constexpr Pan clampPan(Pan pan) noexcept
{
    return Pan{std::clamp(pan.position, minimumPanPosition(), maximumPanPosition())};
}

/*!
\brief Formats a pan as the one text form every pan readout uses: `C`, `L35`, `R100`.

The number is the whole-percent distance from centre, so a position that rounds to centre reads
`C`. The knob's popup and tooltip and the automation lane's value readout all come from here.

\param pan Pan value to format.
\return Display text in the `C` / `L<percent>` / `R<percent>` form.
*/
[[nodiscard]] std::string formatPan(Pan pan);

/*!
\brief Parses the display form back into a pan, the exact inverse of formatPan().

Accepts `C`, `L<percent>` and `R<percent>` case-insensitively with surrounding whitespace; the
result is clamped.

\param text Display text to parse.
\return The parsed pan, or `nullopt` when the text is not in any accepted form.
*/
[[nodiscard]] std::optional<Pan> parsePan(std::string_view text);

} // namespace rock_hero::common::audio
