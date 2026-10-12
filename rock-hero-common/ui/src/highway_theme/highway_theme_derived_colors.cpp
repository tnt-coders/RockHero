#include "highway_theme/highway_theme_derived_colors.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rock_hero::common::ui
{

namespace
{

// The share of its chroma a fretting-hand color keeps in its picking-hand counterpart.
constexpr double g_picking_hand_chroma_scale = 0.25;

// The box fill's middle as a fraction of the box color, per channel, in thousandths so the rounding
// stays exact integer arithmetic.
constexpr ArgbColor g_box_fill_middle_permille = 286;

// The WCAG contrast white label text keeps over a 2D span's ink, and the relative luminance that
// puts the ink at it: (1 + 0.05) / (Y + 0.05) = 4.45.
constexpr double g_lane_label_contrast = 4.45;
constexpr double g_lane_shape_luminance = (1.05 / g_lane_label_contrast) - 0.05;

// The D65 white in CIE XYZ, which the conversions normalize by so white lands on (1, 1, 1).
constexpr double g_white_x = 0.95047;
constexpr double g_white_z = 1.08883;

// The boundary of CIELAB's linear segment near black, in the companded domain.
constexpr double g_lab_delta = 6.0 / 29.0;

// A color's RGB in linear light, each channel from 0 to 1.
struct LinearRgb
{
    double red;
    double green;
    double blue;
};

// Decodes one sRGB channel byte to linear light.
[[nodiscard]] double linearChannel(const ArgbColor color, const unsigned shift)
{
    const double encoded = static_cast<double>((color >> shift) & 0xFFU) / 255.0;
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
}

// Decodes a color's RGB, ignoring its alpha, to linear light.
[[nodiscard]] LinearRgb linearRgbOf(const ArgbColor color)
{
    return LinearRgb{
        .red = linearChannel(color, 16U),
        .green = linearChannel(color, 8U),
        .blue = linearChannel(color, 0U),
    };
}

// The relative luminance of a linear color: CIE Y under D65, where white is 1.
[[nodiscard]] double luminanceOf(const LinearRgb& color)
{
    return (0.2126729 * color.red) + (0.7151522 * color.green) + (0.0721750 * color.blue);
}

// Encodes one linear channel, clamped to sRGB's range, as its nearest byte.
[[nodiscard]] ArgbColor encodedChannel(const double linear)
{
    const double clamped = std::clamp(linear, 0.0, 1.0);
    const double encoded =
        clamped <= 0.0031308 ? clamped * 12.92 : (1.055 * std::pow(clamped, 1.0 / 2.4)) - 0.055;
    return static_cast<ArgbColor>(std::lround(encoded * 255.0));
}

// Converts a CIELAB color back through CIE XYZ and linear sRGB, clamping each channel to sRGB's
// range, under the given alpha byte.
[[nodiscard]] ArgbColor argbOf(const Cielab& color, const ArgbColor alpha)
{
    const auto uncompand = [](const double t) {
        return t > g_lab_delta ? t * t * t : 3.0 * g_lab_delta * g_lab_delta * (t - (4.0 / 29.0));
    };
    const double fy = (color.lightness + 16.0) / 116.0;
    const double x = uncompand(fy + (color.a / 500.0)) * g_white_x;
    const double y = uncompand(fy);
    const double z = uncompand(fy - (color.b / 200.0)) * g_white_z;

    const double red = (3.2404542 * x) - (1.5371385 * y) - (0.4985314 * z);
    const double green = (-0.9692660 * x) + (1.8760108 * y) + (0.0415560 * z);
    const double blue = (0.0556434 * x) - (0.2040259 * y) + (1.0572252 * z);
    return (alpha << 24U) | (encodedChannel(red) << 16U) | (encodedChannel(green) << 8U) |
           encodedChannel(blue);
}

} // namespace

Cielab cielabOf(const ArgbColor color)
{
    const LinearRgb linear = linearRgbOf(color);
    const double x =
        ((0.4124564 * linear.red) + (0.3575761 * linear.green) + (0.1804375 * linear.blue)) /
        g_white_x;
    const double y = luminanceOf(linear);
    const double z =
        ((0.0193339 * linear.red) + (0.1191920 * linear.green) + (0.9503041 * linear.blue)) /
        g_white_z;

    // CIELAB's cube root, with its linear segment near black.
    const auto compand = [](const double t) {
        return t > g_lab_delta * g_lab_delta * g_lab_delta
                   ? std::cbrt(t)
                   : (t / (3.0 * g_lab_delta * g_lab_delta)) + (4.0 / 29.0);
    };
    const double fx = compand(x);
    const double fy = compand(y);
    const double fz = compand(z);
    return Cielab{
        .lightness = (116.0 * fy) - 16.0,
        .a = 500.0 * (fx - fy),
        .b = 200.0 * (fy - fz),
    };
}

double deltaE(const ArgbColor first, const ArgbColor second)
{
    const Cielab lhs = cielabOf(first);
    const Cielab rhs = cielabOf(second);
    return std::hypot(lhs.lightness - rhs.lightness, lhs.a - rhs.a, lhs.b - rhs.b);
}

// Keeps the hue by scaling a* and b* together, and lifts L* to Fairchild and Pirrotta's L**:
// L** = L* + (2.5 - 0.025 L*)(0.116 |sin((h - 90 degrees) / 2)| + 0.085) C*.
ArgbColor pickingHandColor(const ArgbColor fretting_color)
{
    const Cielab source = cielabOf(fretting_color);
    const double chroma = std::hypot(source.a, source.b);
    const double hue = std::atan2(source.b, source.a);
    const double colorfulness_term =
        (0.116 * std::abs(std::sin((hue - (std::numbers::pi / 2.0)) / 2.0))) + 0.085;
    const double perceived_lightness =
        source.lightness + ((2.5 - (0.025 * source.lightness)) * colorfulness_term * chroma);
    return argbOf(
        Cielab{
            .lightness = std::min(perceived_lightness, 100.0),
            .a = source.a * g_picking_hand_chroma_scale,
            .b = source.b * g_picking_hand_chroma_scale,
        },
        (fretting_color >> 24U) & 0xFFU);
}

ArgbColor boxFillMiddle(const ArgbColor box_color)
{
    ArgbColor result = box_color & 0xFF000000U;
    for (const unsigned shift : {16U, 8U, 0U})
    {
        const ArgbColor channel = (box_color >> shift) & 0xFFU;
        result |= (((channel * g_box_fill_middle_permille) + 500U) / 1000U) << shift;
    }
    return result;
}

// Scales the base to the ink's luminance where its brightest channel allows, and blends it toward
// white otherwise. Luminance is linear in each channel, so either move lands on it exactly before
// the bytes round.
ArgbColor laneShapeInk(const ArgbColor base)
{
    const LinearRgb linear = linearRgbOf(base);
    const double luminance = luminanceOf(linear);
    const double brightest = std::max({linear.red, linear.green, linear.blue});

    // Scaling by Y_t / Y fits while the brightest channel stays within full, Y_t * max <= Y, and
    // needs a luminance to divide by. Blending runs only for a base darker than the ink, so 1 - Y
    // never reaches zero.
    const bool scales = luminance > 0.0 && g_lane_shape_luminance * brightest <= luminance;
    const auto shaded = [&](const double channel) {
        if (scales)
        {
            return channel * g_lane_shape_luminance / luminance;
        }
        const double blend = (g_lane_shape_luminance - luminance) / (1.0 - luminance);
        return channel + (blend * (1.0 - channel));
    };
    return (base & 0xFF000000U) | (encodedChannel(shaded(linear.red)) << 16U) |
           (encodedChannel(shaded(linear.green)) << 8U) | encodedChannel(shaded(linear.blue));
}

ArgbColor fretNumberDim(const HighwayTheme& theme)
{
    const ArgbColor alpha = (theme.lane_line_lit >> 24U) & 0xFFU;
    return (((alpha + 1U) / 2U) << 24U) | (theme.lane_line_lit & 0x00FFFFFFU);
}

} // namespace rock_hero::common::ui
