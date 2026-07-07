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

std::expected<void, ToneTrackError> createToneRegion(
    ToneTrack& tone_track, ToneGridPosition at, std::string new_region_id,
    std::string new_tone_document_ref)
{
    const auto container = std::ranges::find_if(tone_track.regions, [at](const ToneRegion& region) {
        return gridPositionLess(region.start, at) && gridPositionLess(at, region.end);
    });
    if (container == tone_track.regions.end())
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::PositionOutsideAnyRegion,
            .message = "cannot create a tone region at a position that is not inside a region",
        }};
    }

    // Build the new right half from the original bounds before shrinking the left half, then insert
    // it immediately after so the track stays sorted and gap-free.
    ToneRegion new_region{
        .id = std::move(new_region_id),
        .start = at,
        .end = container->end,
        .tone_document_ref = std::move(new_tone_document_ref),
    };
    container->end = at;
    tone_track.regions.insert(std::next(container), std::move(new_region));
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

    if (tone_track.regions.size() <= 1)
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::CannotRemoveOnlyRegion,
            .message = "cannot remove the only tone region; the song must always be covered",
        }};
    }

    const std::size_t position = *index;
    if (position > 0)
    {
        // Extend the previous region over the removed span so no gap opens.
        tone_track.regions[position - 1].end = tone_track.regions[position].end;
    }
    else
    {
        // First region: the next region absorbs the removed span from the front instead.
        tone_track.regions[position + 1].start = tone_track.regions[position].start;
    }

    tone_track.regions.erase(tone_track.regions.begin() + static_cast<std::ptrdiff_t>(position));
    return {};
}

} // namespace rock_hero::common::core
