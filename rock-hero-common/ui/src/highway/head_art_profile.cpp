#include "highway/head_art_profile.h"

#include "highway/highway_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <juce_graphics/juce_graphics.h>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace rock_hero::common::ui
{

namespace
{

// Peak-coverage floor under which a cell counts as empty rather than measurable art.
constexpr float g_measurable_peak = 0.25F;

// One cell's pixel window and its coverage reader (B of the structural scheme). Holds the bitmap
// by pointer purely as a value type; the view never outlives the measurement pass.
struct CellView
{
    const juce::Image::BitmapData* bitmap;
    int x0;
    int y0;
    int size;

    [[nodiscard]] float coverage(const int x, const int y) const
    {
        return bitmap->getPixelColour(x0 + x, y0 + y).getFloatBlue();
    }
};

/*
Interpolated crossing positions of a profile through a threshold, in index space: the rising
crossing scanned from the left, the falling scanned from the right. A profile already above the
threshold at the window's edge crosses half a texel outside it, matching a sample owning
[index - 0.5, index + 0.5].
*/
struct Crossings
{
    double lo;
    double hi;
};

// Interpolated threshold crossings of one max-projection profile: the sub-texel positions
// where coverage rises above and falls back below the threshold. Empty when the profile never
// reaches it.
[[nodiscard]] std::optional<Crossings> crossingsAt(
    const std::span<const double> profile, const double threshold)
{
    const std::size_t count = profile.size();
    std::size_t first = count;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (profile[i] >= threshold)
        {
            first = i;
            break;
        }
    }
    if (first == count)
    {
        return std::nullopt;
    }
    std::size_t last = first;
    for (std::size_t i = count; i-- > first;)
    {
        if (profile[i] >= threshold)
        {
            last = i;
            break;
        }
    }
    // Both divisions are safe: the sample beyond each crossing is strictly below the threshold by
    // the first/last definitions, so the denominators are strictly positive.
    double lo = -0.5;
    if (first > 0)
    {
        const double below = profile[first - 1];
        const double above = profile[first];
        lo = (static_cast<double>(first) - 1.0) + ((threshold - below) / (above - below));
    }
    double hi = static_cast<double>(count) - 0.5;
    if (last + 1 < count)
    {
        const double above = profile[last];
        const double below = profile[last + 1];
        hi = static_cast<double>(last) + ((above - threshold) / (above - below));
    }
    return Crossings{.lo = lo, .hi = hi};
}

// The cell's coverage collapsed along each axis by maximum, plus the cell's peak — the profiles
// the 50%-of-peak extents are read from.
struct CellProfiles
{
    std::vector<double> by_x;
    std::vector<double> by_y;
    double peak{0.0};
};

[[nodiscard]] CellProfiles maxProjections(const CellView& cell)
{
    CellProfiles out{
        .by_x = std::vector<double>(static_cast<std::size_t>(cell.size), 0.0),
        .by_y = std::vector<double>(static_cast<std::size_t>(cell.size), 0.0),
        .peak = 0.0,
    };
    for (int y = 0; y < cell.size; ++y)
    {
        for (int x = 0; x < cell.size; ++x)
        {
            const double value = cell.coverage(x, y);
            double& column = out.by_x[static_cast<std::size_t>(x)];
            double& row = out.by_y[static_cast<std::size_t>(y)];
            column = std::max(column, value);
            row = std::max(row, value);
            out.peak = std::max(out.peak, value);
        }
    }
    return out;
}

// One row's (or column's) own coverage profile — the contour work needs actual line profiles, not
// the projections. Load-time only, so per-line vectors are fine.
[[nodiscard]] std::optional<Crossings> lineCrossings(
    const CellView& cell, const int fixed, const bool row, const double threshold)
{
    std::vector<double> line(static_cast<std::size_t>(cell.size), 0.0);
    for (int i = 0; i < cell.size; ++i)
    {
        line[static_cast<std::size_t>(i)] = row ? cell.coverage(i, fixed) : cell.coverage(fixed, i);
    }
    return crossingsAt(line, threshold);
}

/*
Fits the corner radius of the rectangle's 50% contour. Contour crossings are collected from every
row and column, all four corners are folded into one quadrant about the measured centre, and one
radius minimizes the arc's squared error by golden-section search. Per candidate radius only
points inside the arc's own quadrant count — points on the straight edges describe the sides, not
the corner.
*/
[[nodiscard]] std::optional<double> fitCornerRadius(
    const CellView& cell, const double threshold, const double center_x, const double center_y,
    const double half_w, const double half_h)
{
    struct FoldedPoint
    {
        double dx;
        double dy;
    };
    std::vector<FoldedPoint> folded;
    for (int y = 0; y < cell.size; ++y)
    {
        const std::optional<Crossings> line = lineCrossings(cell, y, true, threshold);
        if (!line.has_value())
        {
            continue;
        }
        const double dy = std::abs(static_cast<double>(y) - center_y);
        folded.push_back(FoldedPoint{.dx = std::abs(line->lo - center_x), .dy = dy});
        folded.push_back(FoldedPoint{.dx = std::abs(line->hi - center_x), .dy = dy});
    }
    for (int x = 0; x < cell.size; ++x)
    {
        const std::optional<Crossings> line = lineCrossings(cell, x, false, threshold);
        if (!line.has_value())
        {
            continue;
        }
        const double dx = std::abs(static_cast<double>(x) - center_x);
        folded.push_back(FoldedPoint{.dx = dx, .dy = std::abs(line->lo - center_y)});
        folded.push_back(FoldedPoint{.dx = dx, .dy = std::abs(line->hi - center_y)});
    }
    const auto cost = [&](const double radius) {
        double sum = 0.0;
        int count = 0;
        for (const FoldedPoint& point : folded)
        {
            if (point.dx < half_w - radius || point.dy < half_h - radius)
            {
                continue;
            }
            const double error =
                std::hypot(point.dx - (half_w - radius), point.dy - (half_h - radius)) - radius;
            sum += error * error;
            ++count;
        }
        // A radius whose quadrant captures too few points describes nothing; rule it out rather
        // than letting an empty sum look like a perfect fit.
        return count < 4 ? std::numeric_limits<double>::max() : sum / count;
    };
    constexpr double golden = 0.6180339887498949;
    double lo = 0.25;
    double hi = std::min(half_w, half_h);
    double a = hi - ((hi - lo) * golden);
    double b = lo + ((hi - lo) * golden);
    double cost_a = cost(a);
    double cost_b = cost(b);
    for (int i = 0; i < 60; ++i)
    {
        if (cost_a < cost_b)
        {
            hi = b;
            b = a;
            cost_b = cost_a;
            a = hi - ((hi - lo) * golden);
            cost_a = cost(a);
        }
        else
        {
            lo = a;
            a = b;
            cost_a = cost_b;
            b = lo + ((hi - lo) * golden);
            cost_b = cost(b);
        }
    }
    const double radius = (lo + hi) / 2.0;
    // If even the converged radius captures too few points, the search only ever walked the
    // flat ruled-out plateau: art with no corner arc to read. Fail loudly rather than return
    // the capsule bound the plateau converges to as though it were a fit.
    if (cost(radius) >= std::numeric_limits<double>::max())
    {
        return std::nullopt;
    }
    return radius;
}

} // namespace

// Validates the atlas shape, measures the standard and node silhouettes, and enforces the
// tech-cell byte-identity guard. The no-alpha half of the contract is enforced by the byte
// overload below; this overload reads the coverage channel of whatever it is handed.
std::expected<HeadArtProfile, StructuralArtError> measureHeadArtProfile(const juce::Image& image)
{
    const int cell_size = image.getWidth() / g_head_atlas_columns;
    if (cell_size < 16 || image.getHeight() < cell_size * 2)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const juce::Image::BitmapData bitmap{image, juce::Image::BitmapData::readOnly};
    const auto cell_view = [&](const int index) {
        return CellView{
            .bitmap = &bitmap,
            .x0 = (index % g_head_atlas_columns) * cell_size,
            .y0 = (index / g_head_atlas_columns) * cell_size,
            .size = cell_size,
        };
    };
    // The drawn quad samples texel centres 0.5 .. size - 0.5 (cellRect insets each cell's UVs by
    // half a texel), so its centre sits at index (size - 1) / 2 and every offset below is
    // relative to that, +x right and +y up (image rows grow down, hence the y negation).
    const double quad_center = (static_cast<double>(cell_size) - 1.0) / 2.0;

    const CellView standard = cell_view(g_head_cell_standard);
    // The tech head (cell 1) shares this profile: the renderer applies cell 0's measured
    // silhouette to heads drawn from either cell, which is only honest while their coverage is
    // byte-identical. Guarded here so a rebake that diverges them fails as an invalid asset
    // instead of silently lighting tech heads with the wrong silhouette. Coverage bytes only —
    // the tech head's tint channels differ by design.
    const CellView tech = cell_view(g_head_cell_tech);
    for (int y = 0; y < cell_size; ++y)
    {
        for (int x = 0; x < cell_size; ++x)
        {
            if (bitmap.getPixelColour(standard.x0 + x, standard.y0 + y).getBlue() !=
                bitmap.getPixelColour(tech.x0 + x, tech.y0 + y).getBlue())
            {
                return std::unexpected(StructuralArtError::UnanalyzableArt);
            }
        }
    }
    const CellProfiles standard_profiles = maxProjections(standard);
    if (standard_profiles.peak < g_measurable_peak)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const double standard_threshold = standard_profiles.peak / 2.0;
    const std::optional<Crossings> standard_x =
        crossingsAt(standard_profiles.by_x, standard_threshold);
    const std::optional<Crossings> standard_y =
        crossingsAt(standard_profiles.by_y, standard_threshold);
    if (!standard_x.has_value() || !standard_y.has_value())
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const double half_width = (standard_x->hi - standard_x->lo) / 2.0;
    const double half_height = (standard_y->hi - standard_y->lo) / 2.0;
    const double standard_cx = (standard_x->lo + standard_x->hi) / 2.0;
    const double standard_cy = (standard_y->lo + standard_y->hi) / 2.0;
    if (half_width < 4.0 || half_height < 2.0 || half_width > quad_center ||
        half_height > quad_center)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }

    const CellView node = cell_view(g_head_cell_harmonic_base);
    const CellProfiles node_profiles = maxProjections(node);
    if (node_profiles.peak < g_measurable_peak)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const double node_threshold = node_profiles.peak / 2.0;
    const std::optional<Crossings> node_x = crossingsAt(node_profiles.by_x, node_threshold);
    const std::optional<Crossings> node_y = crossingsAt(node_profiles.by_y, node_threshold);
    if (!node_x.has_value() || !node_y.has_value())
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const double node_cx = (node_x->lo + node_x->hi) / 2.0;
    const double node_cy = (node_y->lo + node_y->hi) / 2.0;
    // The diamond's half-span through the edge law: every row's crossings satisfy
    // |x - cx| + |y - cy| = span on the art's straight edges, so the span is the mean over rows
    // clear of the vertices (where a degenerate sliver would swamp the interpolation).
    double span_sum = 0.0;
    std::vector<double> row_spans;
    row_spans.reserve(static_cast<std::size_t>(node.size));
    for (int y = 0; y < node.size; ++y)
    {
        const std::optional<Crossings> line = lineCrossings(node, y, true, node_threshold);
        if (!line.has_value() || (line->hi - line->lo) < 2.0)
        {
            continue;
        }
        const double row_span =
            ((line->hi - line->lo) / 2.0) + std::abs(static_cast<double>(y) - node_cy);
        span_sum += row_span;
        row_spans.push_back(row_span);
    }
    if (row_spans.size() < 4)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    const double node_half_span = span_sum / static_cast<double>(row_spans.size());
    // The edge law is the measurement MODEL, so it is also the guard: every row of a true
    // diamond agrees on the span (the shipped art deviates by under 0.12 texels), while a
    // non-diamond here would land a plausible mean inside the sanity band below and get a
    // rhombus field fitted to a silhouette that is not there. A full texel of disagreement is
    // far outside any authored diamond.
    for (const double row_span : row_spans)
    {
        if (std::abs(row_span - node_half_span) > 1.0)
        {
            return std::unexpected(StructuralArtError::UnanalyzableArt);
        }
    }
    if (node_half_span < 4.0 || node_half_span > quad_center)
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }

    const std::optional<double> corner = fitCornerRadius(
        standard, standard_threshold, standard_cx, standard_cy, half_width, half_height);
    if (!corner.has_value())
    {
        return std::unexpected(StructuralArtError::UnanalyzableArt);
    }
    return HeadArtProfile{
        .half_width_texels = half_width,
        .half_height_texels = half_height,
        .corner_texels = *corner,
        .center_x_texels = standard_cx - quad_center,
        .center_y_texels = -(standard_cy - quad_center),
        .node_half_span_texels = node_half_span,
        .node_center_x_texels = node_cx - quad_center,
        .node_center_y_texels = -(node_cy - quad_center),
    };
}

// Decodes through the shared structural-art seam, which enforces the no-alpha rule, then defers
// to the image overload.
std::expected<HeadArtProfile, StructuralArtError> measureHeadArtProfile(
    const std::span<const std::byte> png_bytes)
{
    const std::expected<juce::Image, StructuralArtError> decoded =
        decodeStructuralArtPng(png_bytes);
    if (!decoded.has_value())
    {
        return std::unexpected(decoded.error());
    }
    return measureHeadArtProfile(*decoded);
}

} // namespace rock_hero::common::ui
