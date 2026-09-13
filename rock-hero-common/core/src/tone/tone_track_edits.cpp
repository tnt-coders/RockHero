#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <rock_hero/common/core/tone/tone_track_edits.h>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Returns the index of the region with the given id, or the rejection an edit reports for an id
// the track does not hold.
[[nodiscard]] std::expected<std::size_t, ToneTrackError> indexOfRegion(
    const ToneTrack& tone_track, const std::string& region_id)
{
    const auto found = std::ranges::find(tone_track.regions, region_id, &ToneRegion::id);
    if (found == tone_track.regions.end())
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::RegionNotFound,
            .message = "tone region not found: " + region_id,
        }};
    }
    return static_cast<std::size_t>(std::distance(tone_track.regions.begin(), found));
}

} // namespace

const ToneRegion* toneRegionAt(const ToneTrack& tone_track, const GridPosition position)
{
    // The first region starting AFTER the position; the one before it contains the position.
    const auto after = std::ranges::upper_bound(
        tone_track.regions, position, std::ranges::less{}, &ToneRegion::start);
    return after == tone_track.regions.begin() ? nullptr : &*std::prev(after);
}

void coalesceToneRegions(ToneTrack& tone_track)
{
    // unique keeps the first of each run of equal tones, which is the earlier region: its id and
    // start survive and the later, redundant change simply drops out.
    const auto [redundant, end] = std::ranges::unique(
        tone_track.regions, std::ranges::equal_to{}, &ToneRegion::tone_document_ref);
    tone_track.regions.erase(redundant, end);
}

std::expected<void, ToneTrackError> createToneRegion(
    ToneTrack& tone_track, const GridPosition position, std::string new_region_id,
    std::string new_tone_document_ref)
{
    // A position ON a region's start is that region's own tone change, not a place to split;
    // one before the first region lies outside the track.
    const ToneRegion* const container = toneRegionAt(tone_track, position);
    if (container == nullptr || container->start == position)
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::PositionOutsideAnyRegion,
            .message = "tone region create position must fall strictly inside a region",
        }};
    }

    // Insert immediately after the container so the track stays sorted; the container now ends
    // where the new region begins, by definition.
    const auto after_container =
        std::next(tone_track.regions.begin(), (container - tone_track.regions.data()) + 1);
    tone_track.regions.insert(
        after_container,
        ToneRegion{
            .id = std::move(new_region_id),
            .start = position,
            .tone_document_ref = std::move(new_tone_document_ref),
        });
    coalesceToneRegions(tone_track);
    return std::expected<void, ToneTrackError>{};
}

std::expected<void, ToneTrackError> deleteToneRegion(
    ToneTrack& tone_track, const std::string& region_id)
{
    const auto index = indexOfRegion(tone_track, region_id);
    if (!index.has_value())
    {
        return std::unexpected{index.error()};
    }
    if (tone_track.regions.size() <= 1)
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::CannotRemoveOnlyRegion,
            .message = "cannot remove the only tone region; the song must stay covered",
        }};
    }

    // Erasing a region hands its span to the previous one, which now runs on to the next start.
    // The first region has no previous, so the next region takes over the song start instead.
    if (*index == 0)
    {
        tone_track.regions[1].start = tone_track.regions[0].start;
    }
    tone_track.regions.erase(
        std::next(tone_track.regions.begin(), static_cast<std::ptrdiff_t>(*index)));
    coalesceToneRegions(tone_track);
    return std::expected<void, ToneTrackError>{};
}

std::expected<void, ToneTrackError> retoneToneRegion(
    ToneTrack& tone_track, const std::string& region_id, std::string tone_document_ref)
{
    const auto index = indexOfRegion(tone_track, region_id);
    if (!index.has_value())
    {
        return std::unexpected{index.error()};
    }

    tone_track.regions[*index].tone_document_ref = std::move(tone_document_ref);
    coalesceToneRegions(tone_track);
    return std::expected<void, ToneTrackError>{};
}

std::expected<void, ToneTrackError> moveToneBoundary(
    ToneTrack& tone_track, const std::string& region_id, const GridPosition position)
{
    const auto index = indexOfRegion(tone_track, region_id);
    if (!index.has_value())
    {
        return std::unexpected{index.error()};
    }
    if (*index == 0)
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::CannotMoveSongStart,
            .message = "the first tone region starts where the song does: " + region_id,
        }};
    }

    // Strictly between the neighbors' starts, or a region would be emptied or reordered.
    const bool below_previous = position <= tone_track.regions[*index - 1].start;
    const bool above_next =
        *index + 1 < tone_track.regions.size() && position >= tone_track.regions[*index + 1].start;
    if (below_previous || above_next)
    {
        return std::unexpected{ToneTrackError{
            .code = ToneTrackErrorCode::UnsortedOrOverlappingRegions,
            .message = "tone boundary must stay strictly between its neighbors: " + region_id,
        }};
    }

    tone_track.regions[*index].start = position;
    return std::expected<void, ToneTrackError>{};
}

} // namespace rock_hero::common::core
