/*!
\file bgfx_color.h
\brief The packed color words bgfx consumes, converted from the project's 0xAARRGGBB colors.
*/

#pragma once

#include <algorithm>
#include <cstdint>
#include <rock_hero/common/ui/string_colors/string_color_palette.h>

namespace rock_hero::common::ui
{

/*!
\brief Packs a color into the 0xAABBGGRR word a vertex's color attribute carries.
\param argb Color to pack.
\param alpha_scale Factor applied to the color's own alpha; the result is clamped to the byte range.
\return The packed vertex color.
*/
[[nodiscard]] inline std::uint32_t packAbgr(ArgbColor argb, double alpha_scale = 1.0)
{
    const auto alpha = static_cast<std::uint32_t>(
        std::clamp(static_cast<double>((argb >> 24U) & 0xFFU) * alpha_scale, 0.0, 255.0));
    const std::uint32_t red = (argb >> 16U) & 0xFFU;
    const std::uint32_t green = (argb >> 8U) & 0xFFU;
    const std::uint32_t blue = argb & 0xFFU;
    return (alpha << 24U) | (blue << 16U) | (green << 8U) | red;
}

/*!
\brief Packs a color into the 0xRRGGBBAA word bgfx::setViewClear takes.
\param argb Color to pack, alpha included.
\return The packed clear color.
*/
[[nodiscard]] constexpr std::uint32_t packRgba(ArgbColor argb) noexcept
{
    return (argb << 8U) | (argb >> 24U);
}

} // namespace rock_hero::common::ui
