/*!
\file tone_output.h
\brief The output stage of one tone: its authored level and pan as a single value.
*/

#pragma once

#include <rock_hero/common/audio/shared/gain.h>
#include <rock_hero/common/audio/shared/pan.h>
#include <type_traits>

namespace rock_hero::common::audio
{

/*!
\brief A tone's output stage, applied after its plugin chain: one level and one pan.

Stored on the tone's own output plugin and persisted in its tone document, so the tone carries both
through every switch. The editor sets, previews and undoes the stage as one value rather than
keeping a level path and a pan path that must agree. Pass by value; the type is trivially copyable.

\see clampToneOutput
*/
struct ToneOutput
{
    /*! \brief Authored output level. */
    Gain level{};

    /*! \brief Authored stereo balance. */
    Pan pan{};

    /*!
    \brief Compares two output stages by their stored values.
    \param lhs Left-hand output stage.
    \param rhs Right-hand output stage.
    \return True when both stages store equal levels and pans.
    */
    friend constexpr bool operator==(ToneOutput lhs, ToneOutput rhs) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<ToneOutput>);

/*!
\brief Returns the output stage with both values clamped to their accepted ranges.
\param output Input output stage.
\return Output stage with clampGain() and clampPan() applied.
*/
[[nodiscard]] constexpr ToneOutput clampToneOutput(ToneOutput output) noexcept
{
    return ToneOutput{.level = clampGain(output.level), .pan = clampPan(output.pan)};
}

} // namespace rock_hero::common::audio
