#include <algorithm>
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
        if (arrangement.tone_document_ref.empty())
        {
            // No default tone exists to anchor a region; leave the empty tone track untouched.
            continue;
        }

        // Ensure the arrangement's default tone is in the catalog so its region resolves to a name.
        const bool catalog_has_default =
            std::ranges::any_of(arrangement.tones, [&arrangement](const Tone& tone) {
                return tone.tone_document_ref == arrangement.tone_document_ref;
            });
        if (!catalog_has_default)
        {
            arrangement.tones.push_back(
                Tone{.tone_document_ref = arrangement.tone_document_ref, .name = "Default"});
        }

        // Materialize the implicit whole-song region so the editor always has a concrete region to
        // split, select, and delete rather than a synthesized default with no identity.
        if (arrangement.tone_track.regions.empty())
        {
            const auto [terminal_measure, terminal_beat] =
                song.tempo_map.beatAtGlobalIndex(song.tempo_map.terminalGlobalBeatIndex());
            arrangement.tone_track.regions.push_back(
                ToneRegion{
                    .id = generatePackageId(),
                    .start = ToneGridPosition{.measure = 1, .beat = 1},
                    .end = ToneGridPosition{.measure = terminal_measure, .beat = terminal_beat},
                    .tone_document_ref = arrangement.tone_document_ref,
                });
        }
    }
}

} // namespace rock_hero::common::core
