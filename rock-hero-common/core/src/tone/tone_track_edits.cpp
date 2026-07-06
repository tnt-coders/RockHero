#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Lexicographic grid order (measure, then beat). Region endpoints are validated against the tempo
// map elsewhere, so a plain field comparison is enough to order well-formed positions here.
[[nodiscard]] bool gridPositionLess(ToneGridPosition lhs, ToneGridPosition rhs) noexcept
{
    return lhs.measure != rhs.measure ? lhs.measure < rhs.measure : lhs.beat < rhs.beat;
}

// Returns the index of the region with the given id, or nothing when it is not present.
[[nodiscard]] std::optional<std::size_t> indexOfRegion(
    const ToneTrack& tone_track, const std::string& region_id)
{
    const auto found = std::ranges::find(tone_track.regions, region_id, &ToneRegion::id);
    if (found == tone_track.regions.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(tone_track.regions.begin(), found));
}

} // namespace

std::expected<void, ToneTrackError> sliceToneRegion(
    ToneTrack& tone_track, const std::string& region_id, ToneGridPosition cut,
    std::string new_region_id, std::string new_tone_document_ref)
{
    const auto index = indexOfRegion(tone_track, region_id);
    if (!index.has_value())
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::RegionNotFound,
            .message = "cannot slice unknown tone region: " + region_id,
        }};
    }

    ToneRegion& region = tone_track.regions[*index];
    if (!gridPositionLess(region.start, cut) || !gridPositionLess(cut, region.end))
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::SlicePositionOutsideRegion,
            .message = "slice position must fall strictly inside the tone region",
        }};
    }

    // Build the right half from the original bounds before shrinking the left half, then insert it
    // immediately after so the track stays sorted and gap-free.
    ToneRegion right_region{
        .id = std::move(new_region_id),
        .name = region.name,
        .start = cut,
        .end = region.end,
        .tone_document_ref = std::move(new_tone_document_ref),
    };
    region.end = cut;
    tone_track.regions.insert(
        tone_track.regions.begin() + static_cast<std::ptrdiff_t>(*index) + 1,
        std::move(right_region));
    return {};
}

std::expected<void, ToneTrackError> deleteToneRegion(
    ToneTrack& tone_track, const std::string& region_id)
{
    const auto index = indexOfRegion(tone_track, region_id);
    if (!index.has_value())
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::RegionNotFound,
            .message = "cannot delete unknown tone region: " + region_id,
        }};
    }

    const std::size_t position = *index;
    if (position > 0)
    {
        // Extend the previous region over the removed span so no gap opens.
        tone_track.regions[position - 1].end = tone_track.regions[position].end;
    }
    else if (tone_track.regions.size() > 1)
    {
        // First region: the next region absorbs the removed span from the front instead.
        tone_track.regions[position + 1].start = tone_track.regions[position].start;
    }
    // Only region: nothing to extend; the emptied track means the arrangement's whole-song default.

    tone_track.regions.erase(tone_track.regions.begin() + static_cast<std::ptrdiff_t>(position));
    return {};
}

} // namespace rock_hero::common::core
