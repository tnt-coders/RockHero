#include "tracktion/tempo_mirror.h"

#include <cstddef>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Quarter-note position of a reign's first downbeat, where its time signature takes effect. The
// edit's beat unit is pinned to quarters, so every address mirrors through the map's own quarter
// axis.
[[nodiscard]] double downbeatQuarterPosition(
    const core::TempoMap& tempo_map, const core::MeterReign& reign)
{
    return tempo_map.quarterPositionAt(core::measureDownbeat(reign.first_measure));
}

} // namespace

void mirrorTempoMapIntoSequence(tracktion::TempoSequence& sequence, const core::TempoMap& tempo_map)
{
    const std::vector<core::BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return;
    }

    // Every reign, the derived short bar included, so the host counts the same bars the editor
    // shows.
    const std::vector<core::MeterReign> signatures = tempo_map.reigns();

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
        const double from_quarters = tempo_map.quarterPositionAt(from.position);
        const double to_quarters = tempo_map.quarterPositionAt(to.position);
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

    if (tracktion::TimeSigSetting* const first_signature = sequence.getTimeSig(0))
    {
        first_signature->startBeatNumber = tracktion::BeatPosition::fromBeats(
            downbeatQuarterPosition(tempo_map, signatures.front()));
        first_signature->numerator = signatures.front().meter.numerator;
        first_signature->denominator = signatures.front().meter.denominator;
    }
    for (std::size_t index = 1; index < signatures.size(); ++index)
    {
        const tracktion::TimeSigSetting::Ptr inserted = sequence.insertTimeSig(
            tracktion::BeatPosition::fromBeats(
                downbeatQuarterPosition(tempo_map, signatures[index])));
        if (inserted != nullptr)
        {
            inserted->numerator = signatures[index].meter.numerator;
            inserted->denominator = signatures[index].meter.denominator;
        }
    }
}

} // namespace rock_hero::common::audio
