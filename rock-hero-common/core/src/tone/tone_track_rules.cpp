#include "tone/tone_track_rules.h"

#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/package/package_id.h>
#include <set>
#include <string>

namespace rock_hero::common::core
{

std::expected<void, ToneTrackError> validateToneTrackRules(
    const ToneTrack& tone_track, const TempoMap& tempo_map)
{
    const auto [terminal_measure, terminal_beat] =
        tempo_map.beatAtGlobalIndex(tempo_map.terminalGlobalBeatIndex());
    const GridPosition terminal_position{.measure = terminal_measure, .beat = terminal_beat};
    std::set<std::string> region_ids;
    GridPosition previous_end;
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

        for (const GridPosition& endpoint : {region.start, region.end})
        {
            // The endpoint must name a real beat in its measure, and any sub-beat offset must be a
            // proper fraction of that beat (in [0, 1)) so it stays inside the addressed beat span.
            if (endpoint.measure < 1 || endpoint.beat < 1 ||
                endpoint.beat > tempo_map.beatsPerMeasureAt(endpoint.measure) ||
                endpoint.offset < Fraction{0, 1} || !(endpoint.offset < Fraction{1, 1}))
            {
                return std::unexpected{ToneTrackError{
                    .code = ToneTrackErrorCode::InvalidEndpoint,
                    .message = "tone region endpoint is not a valid beat position: " +
                               formatGridPositionToken(endpoint),
                }};
            }
        }

        // Endpoints order by exact musical position (measure, then beat, then sub-beat offset), so
        // the ordering, terminal, and overlap checks compare GridPositions directly rather than
        // collapsing sub-beat offsets onto whole-beat indices.
        if (!(region.start < region.end))
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::EmptyOrReversedRegion,
                .message = "tone region start must be before its end: " + region.id,
            }};
        }

        if (region.end > terminal_position)
        {
            return std::unexpected{ToneTrackError{
                .code = ToneTrackErrorCode::RegionPastTerminalAnchor,
                .message = "tone region ends past the tempo-map terminal anchor: " + region.id,
            }};
        }

        if (has_previous_region && region.start < previous_end)
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

        previous_end = region.end;
        has_previous_region = true;
    }

    return std::expected<void, ToneTrackError>{};
}

} // namespace rock_hero::common::core
