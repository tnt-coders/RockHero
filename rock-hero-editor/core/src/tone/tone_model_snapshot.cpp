#include "tone/tone_model_snapshot.h"

#include <algorithm>
#include <rock_hero/common/core/song/arrangement.h>
#include <rock_hero/common/core/tone/tone_track_rules.h>

namespace rock_hero::editor::core
{

// An empty snapshot for a session with no arrangement, rather than a failure: every tone verb then
// finds nothing to edit, its common-core primitive refuses, and no commit is ever reached.
ToneModelSnapshot ToneModelSnapshot::capture(const common::core::Session& session)
{
    const common::core::Arrangement* const arrangement = session.currentArrangement();
    if (arrangement == nullptr)
    {
        return {};
    }
    return ToneModelSnapshot{.tones = arrangement->tones, .tone_track = arrangement->tone_track};
}

// Reports failure rather than silently doing nothing when no arrangement owns a tone model, which
// is what lets an undo of a tone edit fault the transition instead of appearing to succeed.
bool ToneModelSnapshot::applyTo(common::core::Session& session) const
{
    common::core::ToneTrack* const target_track = session.currentToneTrack();
    std::vector<common::core::Tone>* const target_catalog = session.currentToneCatalog();
    if (target_track == nullptr || target_catalog == nullptr)
    {
        return false;
    }
    // Assignment is the whole apply: region ids are carried in the snapshot, so a selection naming
    // one still resolves.
    *target_catalog = tones;
    *target_track = tone_track;
    return true;
}

// Run on every tone commit, so the two verbs that can take a tone's last reference (delete, retone)
// can never disagree about whether the entry it leaves behind goes. Region coalescing is NOT done
// here: the common-core edit primitives already end there, so a track reaching this is coalesced.
void ToneModelSnapshot::normalize()
{
    std::erase_if(tones, [this](const common::core::Tone& tone) {
        return std::ranges::none_of(
            tone_track.regions, [&tone](const common::core::ToneRegion& region) {
                return region.tone_document_ref == tone.tone_document_ref;
            });
    });
}

// The track's own rules are the ones persistence enforces, asked of common core; the catalog
// pairing below is the half those rules cannot see, because a ToneTrack does not carry the catalog
// it references. Tone-name uniqueness is deliberately NOT here: a typed name is input the charter
// can retype, so the verbs that take one report it to them rather than refusing a structurally
// sound model.
std::optional<std::string> ToneModelSnapshot::validate(const common::core::Session& session) const
{
    if (const auto valid =
            common::core::validateToneTrackRules(tone_track, session.song().tempo_map);
        !valid.has_value())
    {
        return valid.error().message;
    }
    // A region pointing at a tone the catalog does not hold would draw an unnamed region the
    // picker could never reach; the rules above cannot see the catalog, so the coverage pair is
    // asked here.
    for (const common::core::ToneRegion& region : tone_track.regions)
    {
        if (std::ranges::none_of(tones, [&region](const common::core::Tone& tone) {
                return tone.tone_document_ref == region.tone_document_ref;
            }))
        {
            return std::string{"tone is not in the arrangement catalog"};
        }
    }
    return std::nullopt;
}

} // namespace rock_hero::editor::core
