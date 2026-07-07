#include <rock_hero/common/core/package/package_id.h>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/common/core/tone/tone_track.h>
#include <rock_hero/common/core/tone/tone_track_normalize.h>
#include <string>
#include <utility>

namespace rock_hero::common::core
{

void ensureExplicitToneRegions(Song& song)
{
    for (Arrangement& arrangement : song.arrangements)
    {
        if (arrangement.tones.empty() || !arrangement.tone_track.regions.empty())
        {
            // Either there is no tone to anchor a region to (the load baseline mints one before
            // this runs), or explicit regions already cover the song.
            continue;
        }

        // Materialize the implicit whole-song region so the editor always has a concrete region to
        // split, select, and delete rather than a synthesized default with no identity. The first
        // catalog tone is the arrangement's baseline.
        const auto [terminal_measure, terminal_beat] =
            song.tempo_map.beatAtGlobalIndex(song.tempo_map.terminalGlobalBeatIndex());
        arrangement.tone_track.regions.push_back(
            ToneRegion{
                .id = generatePackageId(),
                .start = ToneGridPosition{.measure = 1, .beat = 1},
                .end = ToneGridPosition{.measure = terminal_measure, .beat = terminal_beat},
                .tone_document_ref = arrangement.tones.front().tone_document_ref,
            });
    }
}

} // namespace rock_hero::common::core
