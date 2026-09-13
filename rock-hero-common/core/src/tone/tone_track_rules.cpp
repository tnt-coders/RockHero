#include "tone/tone_track_rules.h"

#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/package/package_id.h>
#include <set>
#include <string>

namespace rock_hero::common::core
{

std::expected<void, ToneTrackError> validateToneTrackRules(
    const ToneTrack& tone_track, const TempoMap& tempo_map)
{
    const GridPosition terminal_position = terminalGridPosition(tempo_map);
    std::set<std::string> region_ids;
    const ToneRegion* previous = nullptr;

    for (const ToneRegion& region : tone_track.regions)
    {
        if (!isCanonicalPackageId(region.id))
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::InvalidRegionId,
                .message = "tone region id must be a canonical UUIDv4: " + region.id,
            }};
        }

        if (!region_ids.insert(region.id).second)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::InvalidRegionId,
                .message = "duplicate tone region id: " + region.id,
            }};
        }

        // The start must name a real sub-beat position — the same rule the chart applies, asked
        // of the one authority rather than restated here.
        if (!isValidGridPosition(region.start, tempo_map))
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::InvalidEndpoint,
                .message = "tone region start is not a valid beat position: " +
                           formatGridPositionToken(region.start),
            }};
        }

        // A region ends where the next begins, so coverage is the first start being the song's
        // and every start lying strictly before the terminal; starts order by exact musical
        // position (measure, beat, then sub-beat offset), so strictly ascending starts leave no
        // region empty and none reversed.
        if (previous == nullptr && region.start != GridPosition{.measure = 1, .beat = 1})
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::SongStartUncovered,
                .message =
                    "the first tone region must start at the song's first downbeat: " + region.id,
            }};
        }

        if (region.start >= terminal_position)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::RegionPastTerminalAnchor,
                .message =
                    "tone region starts at or past the tempo-map terminal anchor: " + region.id,
            }};
        }

        if (previous != nullptr && region.start <= previous->start)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::UnsortedOrOverlappingRegions,
                .message = "tone region starts must be strictly ascending: " + region.id,
            }};
        }

        if (!isCanonicalToneDocumentRef(region.tone_document_ref))
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::InvalidToneDocumentRef,
                .message = "tone region document path must be tones/<uuid>/tone.json: " +
                           region.tone_document_ref,
            }};
        }

        previous = &region;
    }

    return std::expected<void, ToneTrackError>{};
}

} // namespace rock_hero::common::core
