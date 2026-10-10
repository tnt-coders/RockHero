#include "tracktion/tempo_mirror.h"

#include <algorithm>
#include <cstddef>
#include <rock_hero/common/core/timeline/fraction.h>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// A whole-note place in the edit's beats. Tracktion's beat is the governing meter's note value (an
// eighth in 6/8), which is what lets it count x/8 bars and quarter-note song positions right, so
// each reign before the place adds its length counted in its own beat. Exact rationals; every
// reign starts on a bar line, so every time signature lands on a whole beat.
[[nodiscard]] double editBeatsAt(
    const std::vector<core::MeterReign>& reigns, const core::Fraction place)
{
    core::Fraction beats{};
    for (std::size_t index = 0; index < reigns.size() && reigns[index].place < place; ++index)
    {
        const core::Fraction end =
            index + 1 < reigns.size() ? std::min(place, reigns[index + 1].place) : place;
        beats =
            beats + (end - reigns[index].place) * core::Fraction{reigns[index].meter.denominator};
    }
    return beats.toDouble();
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
    // final span's tempo simply extends past the terminal anchor. The tempo is quarter-note BPM,
    // which is what Tracktion's BPM means in every meter.
    struct TempoStep
    {
        double start_beats{};
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
                .start_beats =
                    editBeatsAt(signatures, tempo_map.wholeNotePositionAt(from.position)),
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
            tracktion::BeatPosition::fromBeats(steps.front().start_beats);
        first_tempo->bpm = steps.front().bpm;
        first_tempo->curve = 1.0F;
    }
    for (std::size_t index = 1; index < steps.size(); ++index)
    {
        sequence.insertTempo(
            tracktion::BeatPosition::fromBeats(steps[index].start_beats), steps[index].bpm, 1.0F);
    }

    if (tracktion::TimeSigSetting* const first_signature = sequence.getTimeSig(0))
    {
        first_signature->startBeatNumber =
            tracktion::BeatPosition::fromBeats(editBeatsAt(signatures, signatures.front().place));
        first_signature->numerator = signatures.front().meter.numerator;
        first_signature->denominator = signatures.front().meter.denominator;
    }
    // insertTimeSig rounds its start to the nearest whole beat, which a start on a bar line already
    // is.
    for (std::size_t index = 1; index < signatures.size(); ++index)
    {
        const tracktion::TimeSigSetting::Ptr inserted = sequence.insertTimeSig(
            tracktion::BeatPosition::fromBeats(editBeatsAt(signatures, signatures[index].place)));
        if (inserted != nullptr)
        {
            inserted->numerator = signatures[index].meter.numerator;
            inserted->denominator = signatures[index].meter.denominator;
        }
    }
}

} // namespace rock_hero::common::audio
