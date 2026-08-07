#include "highway/box_mute_profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include <span>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// Coverage floor under which a texel counts as unpainted, in [0, 1]. Coverage is channel B of the
// structural-art scheme; the art carries no alpha channel, so nothing here reads one.
constexpr float g_visible_coverage = 2.0F / 255.0F;

// Measures one cell's glyph. Cuts are taken perpendicular to both arms at stations along the
// outer spans (clear of the crossing and of the corner tips), sampled bilinearly, and
// averaged into the ramp.
[[nodiscard]] std::expected<BoxMuteProfile, BoxMuteProfileError> measureCell(
    const juce::Image::BitmapData& bitmap, const int y_begin, const int y_end)
{
    const int width = bitmap.width;

    // Two passes over the cell: the first finds any-visible bounds (the falloff's reach) and the
    // glyph's peak pixel coverage; the second takes the solid bounds at half that peak — the glyph
    // rect the contract clips the stroke to — so its corners pin the arm centerlines exactly even
    // for a rim authored below full coverage, and regardless of any faint core or falloff around
    // it.
    int faint_min_y = y_end;
    int faint_max_y = -1;
    float peak_pixel_coverage = 0.0F;
    for (int y = y_begin; y < y_end; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float coverage = bitmap.getPixelColour(x, y).getFloatBlue();
            if (coverage < g_visible_coverage)
            {
                continue;
            }
            faint_min_y = std::min(faint_min_y, y);
            faint_max_y = std::max(faint_max_y, y);
            peak_pixel_coverage = std::max(peak_pixel_coverage, coverage);
        }
    }
    if (peak_pixel_coverage < 0.25F)
    {
        return std::unexpected(BoxMuteProfileError::UnanalyzableGlyph);
    }
    const float solid_coverage = peak_pixel_coverage / 2.0F;
    int min_x = width;
    int min_y = y_end;
    int max_x = -1;
    int max_y = -1;
    for (int y = y_begin; y < y_end; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (bitmap.getPixelColour(x, y).getFloatBlue() < solid_coverage)
            {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    if (max_x < 0 || (max_x - min_x) < 8 || (max_y - min_y) < 8)
    {
        return std::unexpected(BoxMuteProfileError::UnanalyzableGlyph);
    }
    const double rect_width = max_x - min_x + 1;
    const double rect_height = max_y - min_y + 1;
    const double center_x = (min_x + max_x) / 2.0;
    const double center_y = (min_y + max_y) / 2.0;

    // Authoring contract: arms run corner to corner of the glyph rect.
    const double arm_length = std::sqrt(
        ((rect_width / 2.0) * (rect_width / 2.0)) + ((rect_height / 2.0) * (rect_height / 2.0)));
    const double dir_x = (rect_width / 2.0) / arm_length;
    const double dir_y = (rect_height / 2.0) / arm_length;

    const auto sample = [&](const double x, const double y) {
        // Bilinear sample of the three structural channels, clamped to the cell.
        const double fx = std::clamp(x, 0.0, static_cast<double>(width - 1));
        const double fy = std::clamp(y, static_cast<double>(y_begin), y_end - 1.0);
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const int x1 = std::min(x0 + 1, width - 1);
        const int y1 = std::min(y0 + 1, y_end - 1);
        const double tx = fx - x0;
        const double ty = fy - y0;
        std::array<double, 3> out{};
        const auto accumulate = [&](const int px, const int py, const double weight) {
            const juce::Colour texel = bitmap.getPixelColour(px, py);
            out[0] += weight * texel.getFloatRed();
            out[1] += weight * texel.getFloatGreen();
            out[2] += weight * texel.getFloatBlue();
        };
        accumulate(x0, y0, (1.0 - tx) * (1.0 - ty));
        accumulate(x1, y0, tx * (1.0 - ty));
        accumulate(x0, y1, (1.0 - tx) * ty);
        accumulate(x1, y1, tx * ty);
        return out;
    };

    // Averages one cross-section cut set over both arms' outer spans into `samples`, each
    // sample the mean of the three channels at its distance from the measured arm's centerline
    // across all valid cuts. The art composes as f(max(stripe, rect clip)), so a sample only
    // carries cross-section truth where the stripe term owns the pixel; two guards enforce that.
    // First, a sample must be at least as close to the measured arm as to the other one —
    // past that bisector the pixel shows the other arm's cross-section (on a wide glyph the
    // shallow arm angle makes perpendicular cuts run straight into the other arm's rim).
    // Second, given a stroke-half estimate, a sample within (stroke half - distance) of the
    // glyph rect boundary is owned by the tip clip — its wrapped rim brightens pixels even
    // on the centerline near the tips, which otherwise smears a radial gradient across a
    // flat-painted fill (the sighted ghost inner X on the full mute). The survey pass runs
    // unguarded (it has no estimate yet); its stroke edge is set by the rim's sharp falloff,
    // which the tip smear does not move.
    const auto accumulate = [&](const double extent,
                                const std::span<std::array<double, 3>>
                                    samples,
                                const double stroke_half_guard) {
        std::ranges::fill(samples, std::array<double, 3>{});
        std::vector<double> weights(samples.size(), 0.0);
        constexpr int g_stations = 6;
        for (int station = 0; station < g_stations; ++station)
        {
            const double along = arm_length * (0.45 + (0.35 * station / (g_stations - 1)));
            for (const double along_sign : {-1.0, 1.0})
            {
                for (const double mirror : {1.0, -1.0})
                {
                    const double ax = dir_x;
                    const double ay = dir_y * mirror;
                    const double station_x = center_x + (along_sign * along * ax);
                    const double station_y = center_y + (along_sign * along * ay);
                    for (std::size_t i = 0; i < samples.size(); ++i)
                    {
                        const double distance = extent * (static_cast<double>(i) + 0.5) /
                                                static_cast<double>(samples.size());
                        for (const double side : {-1.0, 1.0})
                        {
                            const double sample_x = station_x + (side * distance * -ay);
                            const double sample_y = station_y + (side * distance * ax);
                            // The other arm mirrors the measured one, so its perpendicular
                            // is (ay, ax); skip samples the other arm owns.
                            const double other_distance = std::abs(
                                ((sample_x - center_x) * ay) + ((sample_y - center_y) * ax));
                            if (other_distance < distance)
                            {
                                continue;
                            }
                            if (stroke_half_guard > 0.0)
                            {
                                // Skip samples the tip clip owns (see the guard note
                                // above): rect-boundary distance below what the stripe
                                // needs to dominate.
                                const double inside_x =
                                    (rect_width / 2.0) - std::abs(sample_x - center_x);
                                const double inside_y =
                                    (rect_height / 2.0) - std::abs(sample_y - center_y);
                                if (std::min(inside_x, inside_y) < stroke_half_guard - distance)
                                {
                                    continue;
                                }
                            }
                            const std::array<double, 3> texel = sample(sample_x, sample_y);
                            for (std::size_t channel = 0; channel < 3; ++channel)
                            {
                                samples[i].at(channel) += texel.at(channel);
                            }
                            weights[i] += 1.0;
                        }
                    }
                }
            }
        }
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            if (weights[i] <= 0.0)
            {
                continue;
            }
            for (double& channel : samples[i])
            {
                channel /= weights[i];
            }
        }
    };

    // First pass: a fine survey over the largest reach any art can have — from the centerline
    // out past the faint bounding box — locates the stroke edge and the last visible art.
    constexpr std::size_t g_survey_samples = 256;
    const double survey_extent = ((faint_max_y - faint_min_y + 1) / 2.0) + 2.0;
    std::array<std::array<double, 3>, g_survey_samples> survey{};
    accumulate(survey_extent, survey, 0.0);

    // The stroke boundary anchors to the rim's outer falloff at HALF THE GLYPH'S PEAK coverage
    // rather than a fixed 50%, so a rim the art did not carry to full coverage still anchors at
    // its own edge instead of straddling a fixed threshold. A glyph whose peak never reaches a
    // quarter has no rim to anchor to.
    double peak_coverage = 0.0;
    for (const std::array<double, 3>& value : survey)
    {
        peak_coverage = std::max(peak_coverage, value[2]);
    }
    if (peak_coverage < 0.25)
    {
        return std::unexpected(BoxMuteProfileError::UnanalyzableGlyph);
    }
    const double edge_coverage = peak_coverage / 2.0;

    double stroke_half = -1.0;
    double last_visible = 0.0;
    double previous_coverage = 0.0;
    for (std::size_t i = 0; i < g_survey_samples; ++i)
    {
        const double distance = survey_extent * (static_cast<double>(i) + 0.5) / g_survey_samples;
        const double coverage = survey.at(i)[2];
        // Take the OUTERMOST falling crossing — not the first — so the anchor stays on the
        // rim even when the art's core is faint or fully hollow, where coverage starts below
        // the edge threshold at the centerline.
        if (previous_coverage >= edge_coverage && coverage < edge_coverage)
        {
            stroke_half = distance;
        }
        if (coverage >= g_visible_coverage)
        {
            last_visible = distance;
        }
        previous_coverage = coverage;
    }
    if (stroke_half <= 0.0 || last_visible <= stroke_half)
    {
        return std::unexpected(BoxMuteProfileError::UnanalyzableGlyph);
    }

    // Second pass: the shipped ramp, cut over a tight extent that ends at the last visible
    // art so every sample carries signal and the renderer's overshoot — which follows
    // extent_fraction — stays as small as the art allows; the forced-zero tail below
    // guarantees the dissolve past it.
    const double extent = last_visible;
    std::array<std::array<double, 3>, g_box_mute_ramp_samples> accumulated{};
    accumulate(extent, accumulated, stroke_half);

    BoxMuteProfile profile;
    for (std::size_t i = 0; i < g_box_mute_ramp_samples; ++i)
    {
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            profile.ramp.at((i * 4) + channel) = static_cast<std::uint8_t>(
                std::lround(std::clamp(accumulated.at(i).at(channel), 0.0, 1.0) * 255.0));
        }
        // The fourth byte only pads the ramp out to the RGBA8 texture it uploads into; the
        // shader never reads it.
        profile.ramp.at((i * 4) + 3) = 255;
    }
    // Force the tail to zero coverage so sampling past the extent always dissolves.
    for (std::size_t channel = 0; channel < 4; ++channel)
    {
        profile.ramp.at(((g_box_mute_ramp_samples - 1) * 4) + channel) = 0;
    }
    profile.stroke_half_fraction = stroke_half / rect_height;
    profile.extent_fraction = extent / rect_height;
    return profile;
}

} // namespace

// Validates the two-cell stack shape, then measures each cell's glyph. The no-alpha half of the
// contract is enforced by the byte overload below, which is the only caller that can see what the
// FILE carried; this overload reads the three structural channels of whatever it is handed.
std::expected<BoxMuteProfiles, BoxMuteProfileError> measureBoxMuteProfiles(const juce::Image& image)
{
    if (image.getWidth() < 16 || image.getHeight() < 16 || (image.getHeight() % 2) != 0)
    {
        return std::unexpected(BoxMuteProfileError::UnanalyzableGlyph);
    }
    const juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::readOnly};
    const int cell_height = image.getHeight() / 2;
    const std::expected<BoxMuteProfile, BoxMuteProfileError> palm =
        measureCell(bitmap, 0, cell_height);
    if (!palm.has_value())
    {
        return std::unexpected(palm.error());
    }
    const std::expected<BoxMuteProfile, BoxMuteProfileError> full =
        measureCell(bitmap, cell_height, image.getHeight());
    if (!full.has_value())
    {
        return std::unexpected(full.error());
    }
    return BoxMuteProfiles{.palm = *palm, .full = *full};
}

// Decodes the PNG bytes, then defers to the image overload.
std::expected<BoxMuteProfiles, BoxMuteProfileError> measureBoxMuteProfiles(
    const std::span<const std::byte> png_bytes)
{
    if (png_bytes.empty())
    {
        return std::unexpected(BoxMuteProfileError::UndecodableImage);
    }
    juce::MemoryInputStream stream{png_bytes.data(), png_bytes.size(), false};
    const juce::Image decoded = juce::PNGImageFormat{}.decodeImage(stream);
    if (decoded.isNull())
    {
        return std::unexpected(BoxMuteProfileError::UndecodableImage);
    }
    // Whether the decoded image has an alpha channel is NOT the same question as whether the file
    // did, so it cannot be the test. On macOS JUCE decodes every PNG through CoreImage, which
    // cannot produce a 24-bit image and so always hands back ARGB with opaque alpha
    // (juce_CoreGraphicsContext_mac.mm juce_loadWithCoreImage); asking hasAlphaChannel() there
    // rejects every alpha-free PNG, including the shipped asset. Both of JUCE's decode paths
    // record the file's real alpha state in this property for exactly this purpose, so it is the
    // portable answer. An opaque alpha channel premultiplies by 1 and leaves the structural
    // channels intact, which is why measuring the macOS ARGB image is still bit-exact.
    if (static_cast<bool>(decoded.getProperties()->getWithDefault("originalImageHadAlpha", false)))
    {
        return std::unexpected(BoxMuteProfileError::AlphaBearingImage);
    }
    return measureBoxMuteProfiles(decoded);
}

} // namespace rock_hero::common::ui
