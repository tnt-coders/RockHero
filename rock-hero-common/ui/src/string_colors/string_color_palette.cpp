#include "string_colors/string_color_palette.h"

#include <algorithm>

namespace rock_hero::common::ui
{

namespace
{

constexpr ArgbColor g_opaque_alpha = 0xff000000U;

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

// Charter Classic: Charter's default six string colors (ChartPanelColors STRING_0..5) ordered
// from the sixth-highest displayed lane upward, plus the shipped extended tier — our RYB teal
// 7th and Charter's near-white gray 8th (STRING_7).
constexpr std::array<StringColorPalette, 1> g_string_color_palettes{StringColorPalette{
    .id = "charter-classic",
    .display_name = "Charter Classic",
    .colorblind_safe = false,
    .standard =
        {
            0xffed0000, // red (lowest string of a standard six)
            0xfff2d706, // yellow
            0xff25b2ff, // blue
            0xffff870a, // orange
            0xff85e747, // green
            0xffd22cf8, // purple (highest string)
        },
    .extended = {
        0xff00b5a0, // teal (7th string)
        0xffb6b6b6, // near-white gray (8th string, Charter STRING_7)
    },
}};

} // namespace

// The six highest lanes take the standard set; lower lanes walk the extended tier downward.
// Ported verbatim from the editor tab renderer so the extraction is behavior-preserving.
ArgbColor stringLaneColor(
    int displayed_string, int displayed_string_count, const StringColorPalette& palette)
{
    const int below_standard_window = std::max(0, displayed_string_count - 6);
    if (displayed_string > below_standard_window)
    {
        const auto standard_index =
            static_cast<std::size_t>(displayed_string - below_standard_window - 1);
        return palette.standard.at(std::min(standard_index, palette.standard.size() - 1));
    }

    const auto tertiary_index = static_cast<std::size_t>(below_standard_window - displayed_string);
    return palette.extended.at(tertiary_index % palette.extended.size());
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
    , accent(brighterColor(brighterColor(border_inner)))
{}

// Span keeps callers away from the registry's storage type; order is stable and the first entry
// is the default preset.
std::span<const StringColorPalette> stringColorPalettes()
{
    return g_string_color_palettes;
}

const StringColorPalette& charterClassicPalette()
{
    return g_string_color_palettes.front();
}

} // namespace rock_hero::common::ui
