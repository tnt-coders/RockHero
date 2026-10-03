#include "tracktion/tempo_mirror.h"

#include <cstddef>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Quarter-note position of a grid position: the edit's beat unit is pinned to quarters, so every
// address mirrors through the map's own whole-note axis, which a quarter is a quarter of.
[[nodiscard]] double quarterPositionAt(
    const core::TempoMap& tempo_map, const core::GridPosition& position)
{
    return 4.0 * tempo_map.wholeNotePositionAt(position).toDouble();
}

// Quarter-note position of a measure's downbeat, where a time signature takes effect.
[[nodiscard]] double downbeatQuarterPosition(const core::TempoMap& tempo_map, int measure)
{
    return quarterPositionAt(
        tempo_map, core::GridPosition{.measure = measure, .beat = 1, .offset = {}});
}

} // namespace

void mirrorTempoMapIntoSequence(tracktion::TempoSequence& sequence, const core::TempoMap& tempo_map)
{
    const std::vector<core::BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return;
    }

    const std::vector<core::TimeSignatureChange>& signatures = tempo_map.timeSignatures();

    // One flat tempo step per anchor span: Rock Hero tempo is constant between anchors, and the
    // final span's tempo simply extends past the terminal anchor.
    struct TempoStep
    {
        double start_quarters{};
        double bpm{};
    };
    std::vector<TempoStep> steps;
    steps.reserve(anchors.size() - 1);
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index)
    {
        const core::BeatAnchor& from = anchors[index];
        const core::BeatAnchor& to = anchors[index + 1];
        const double from_quarters = quarterPositionAt(tempo_map, from.position);
        const double to_quarters = quarterPositionAt(tempo_map, to.position);
        const double span_seconds = to.seconds() - from.seconds();
        if (span_seconds <= 0.0 || to_quarters <= from_quarters)
        {
            continue;
        }
        steps.push_back(
            TempoStep{
                .start_quarters = from_quarters,
                .bpm = (to_quarters - from_quarters) / span_seconds * 60.0,
            });
    }
    if (steps.empty())
    {
        return;
    }

    // Rewrite from a single-entry baseline. removeTempo with remapping disabled and direct
    // CachedValue writes bypass both the remap machinery and the official mutator's BPM clamp,
    // so >300 BPM charts mirror faithfully and nothing beat-shifts existing content.
    while (sequence.getNumTempos() > 1)
    {
        sequence.removeTempo(sequence.getNumTempos() - 1, false);
    }
    while (sequence.getNumTimeSigs() > 1)
    {
        sequence.removeTimeSig(sequence.getNumTimeSigs() - 1);
    }

    if (tracktion::TempoSetting* const first_tempo = sequence.getTempo(0))
    {
        first_tempo->startBeatNumber =
            tracktion::BeatPosition::fromBeats(steps.front().start_quarters);
        first_tempo->bpm = steps.front().bpm;
        first_tempo->curve = 1.0F;
    }
    for (std::size_t index = 1; index < steps.size(); ++index)
    {
        sequence.insertTempo(
            tracktion::BeatPosition::fromBeats(steps[index].start_quarters),
            steps[index].bpm,
            1.0F);
    }

    if (!signatures.empty())
    {
        if (tracktion::TimeSigSetting* const first_signature = sequence.getTimeSig(0))
        {
            first_signature->startBeatNumber = tracktion::BeatPosition::fromBeats(
                downbeatQuarterPosition(tempo_map, signatures.front().measure));
            first_signature->numerator = signatures.front().numerator;
            first_signature->denominator = signatures.front().denominator;
        }
        for (std::size_t index = 1; index < signatures.size(); ++index)
        {
            const tracktion::TimeSigSetting::Ptr inserted = sequence.insertTimeSig(
                tracktion::BeatPosition::fromBeats(
                    downbeatQuarterPosition(tempo_map, signatures[index].measure)));
            if (inserted != nullptr)
            {
                inserted->numerator = signatures[index].numerator;
                inserted->denominator = signatures[index].denominator;
            }
        }
    }
}

} // namespace rock_hero::common::audio
