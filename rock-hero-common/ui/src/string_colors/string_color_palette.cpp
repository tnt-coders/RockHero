#include "string_colors/string_color_palette.h"

#include <algorithm>
#include <cmath>

namespace rock_hero::common::ui
{

namespace
{

constexpr ArgbColor g_opaque_alpha = 0xff000000U;

// The strings of a standard guitar: a chart with fewer takes its colors from the low E up.
constexpr int g_standard_string_count = 6;

// Channel extraction for the packed 0xAARRGGBB layout.
[[nodiscard]] constexpr int redOf(ArgbColor color)
{
    return static_cast<int>((color >> 16U) & 0xffU);
}

[[nodiscard]] constexpr int greenOf(ArgbColor color)
{
    return static_cast<int>((color >> 8U) & 0xffU);
}

[[nodiscard]] constexpr int blueOf(ArgbColor color)
{
    return static_cast<int>(color & 0xffU);
}

// Packs byte-range channels back into an opaque ARGB value.
[[nodiscard]] constexpr ArgbColor packColor(int red, int green, int blue)
{
    return g_opaque_alpha | (static_cast<ArgbColor>(red) << 16U) |
           (static_cast<ArgbColor>(green) << 8U) | static_cast<ArgbColor>(blue);
}

// Accent brightening that keeps the string's hue: java's brighter() clamps each channel
// independently, so Charter's ring.brighter().brighter() bleaches saturated hues toward yellow
// or white (an orange or green accent glow would render yellow). One shared gain preserves the
// channel ratios; it targets java's double-brighter intensity (1 / 0.7^2) and stops where the
// hottest channel saturates. Rounded, not truncated, so a capped gain lands the hottest channel
// exactly on 255.
[[nodiscard]] ArgbColor huePreservingAccentColor(ArgbColor color)
{
    const int max_channel = std::max({redOf(color), greenOf(color), blueOf(color)});
    if (max_channel == 0)
    {
        return color;
    }

    const double gain = std::min(1.0 / (0.7 * 0.7), 255.0 / max_channel);
    const auto scale = [gain](int channel) {
        return static_cast<int>(std::min(255L, std::lround(channel * gain)));
    };
    return packColor(scale(redOf(color)), scale(greenOf(color)), scale(blueOf(color)));
}

} // namespace

// Maps a lane counted from the bottom onto the highest-string-first slots: the lane sits that many
// strings below the top of a window at least six strings tall.
ArgbColor stringLaneColor(
    int displayed_string, int displayed_string_count, const StringColorPalette& palette)
{
    const int window = std::max(displayed_string_count, g_standard_string_count);
    const int last_slot = static_cast<int>(palette.colors.size()) - 1;
    const int slot = std::clamp(window - displayed_string, 0, last_slot);
    return palette.colors.at(static_cast<std::size_t>(slot));
}

// Reproduces java.awt.Color.darker(): each channel scaled by 0.7 and truncated.
ArgbColor darkerColor(ArgbColor color)
{
    return packColor(
        static_cast<int>(redOf(color) * 0.7),
        static_cast<int>(greenOf(color) * 0.7),
        static_cast<int>(blueOf(color) * 0.7));
}

// Reproduces java.awt.Color.brighter(): channels divided by 0.7 with the small-value bump, so
// derived note colors match Charter bit-for-bit.
ArgbColor brighterColor(ArgbColor color)
{
    const int red = redOf(color);
    const int green = greenOf(color);
    const int blue = blueOf(color);
    constexpr int minimum = 3; // (int) (1 / (1 - 0.7))
    if (red == 0 && green == 0 && blue == 0)
    {
        return packColor(minimum, minimum, minimum);
    }

    const auto lift = [](int channel) {
        if (channel > 0 && channel < minimum)
        {
            channel = minimum;
        }
        return std::min(255, static_cast<int>(channel / 0.7));
    };
    return packColor(lift(red), lift(green), lift(blue));
}

// Reproduces Charter's ColorUtils.multiplyColor with truncation.
ArgbColor multiplyColor(ArgbColor color, double multiplier)
{
    const auto scale = [multiplier](int channel) {
        return std::clamp(static_cast<int>(channel * multiplier), 0, 255);
    };
    return packColor(scale(redOf(color)), scale(greenOf(color)), scale(blueOf(color)));
}

// Charter derives every per-string surface from the base string color with fixed multipliers.
StringLaneStyle::StringLaneStyle(ArgbColor base)
    : lane(multiplyColor(base, 0.8))
    , border_inner(brighterColor(lane))
    , inner(darkerColor(darkerColor(border_inner)))
    , linked_inner(darkerColor(darkerColor(inner)))
    , tail(multiplyColor(base, 0.66))
    , tail_edge(brighterColor(tail))
    , accent(huePreservingAccentColor(border_inner))
{}

} // namespace rock_hero::common::ui
