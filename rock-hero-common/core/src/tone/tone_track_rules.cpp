#include "tone/tone_track_rules.h"

#include <cstdint>
#include <rock_hero/common/core/package/package_id.h>
#include <set>
#include <string>

namespace rock_hero::common::core
{

namespace
{

// Formats an endpoint for diagnostics with the same token spelling the package format persists.
[[nodiscard]] std::string endpointText(const ToneGridPosition& endpoint)
{
    return std::to_string(endpoint.measure) + ":" + std::to_string(endpoint.beat);
}

} // namespace

std::expected<void, ToneTrackError> validateToneTrackRules(
    const ToneTrack& tone_track, const TempoMap& tempo_map)
{
    const std::int64_t terminal_beat_index = tempo_map.terminalGlobalBeatIndex();
    std::set<std::string> region_ids;
    std::int64_t previous_end_index = 0;
    bool has_previous_region = false;

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

        for (const ToneGridPosition& endpoint : {region.start, region.end})
        {
            if (endpoint.measure < 1 || endpoint.beat < 1 ||
                endpoint.beat > tempo_map.beatsPerMeasureAt(endpoint.measure))
            {
                return std::unexpected{ToneTrackError{
                    .code = ToneTrackErrorCode::InvalidEndpoint,
                    .message = "tone region endpoint is not a valid beat position: " +
                               endpointText(endpoint),
                }};
            }
        }

        const std::int64_t start_index =
            tempo_map.globalBeatIndex(region.start.measure, region.start.beat);
        const std::int64_t end_index =
            tempo_map.globalBeatIndex(region.end.measure, region.end.beat);
        if (start_index >= end_index)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::EmptyOrReversedRegion,
                .message = "tone region start must be before its end: " + region.id,
            }};
        }

        if (end_index > terminal_beat_index)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::RegionPastTerminalAnchor,
                .message = "tone region ends past the tempo-map terminal anchor: " + region.id,
            }};
        }

        if (has_previous_region && start_index < previous_end_index)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::UnsortedOrOverlappingRegions,
                .message = "tone regions must be sorted and must not overlap: " + region.id,
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

        previous_end_index = end_index;
        has_previous_region = true;
    }

    return std::expected<void, ToneTrackError>{};
}

} // namespace rock_hero::common::core
