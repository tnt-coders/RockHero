#include "tracktion/tempo_mirror.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace rock_hero::common::audio
{

namespace
{

// Quarter notes per whole signature beat in a measure of the given denominator.
[[nodiscard]] double quartersPerBeat(int denominator)
{
    return denominator > 0 ? 4.0 / static_cast<double>(denominator) : 1.0;
}

// Cumulative quarter-note positions of measure starts, index 0 = measure 1. Rock Hero anchors and
// signatures address signature beats; the edit's beat unit is pinned to quarters, so mirroring
// converts every address through quarter-note positions.
[[nodiscard]] std::vector<double> measureStartQuarters(
    const core::TempoMap& tempo_map, int measure_count)
{
    std::vector<double> starts;
    starts.reserve(static_cast<std::size_t>(measure_count) + 1);
    double quarters = 0.0;
    for (int measure = 1; measure <= measure_count + 1; ++measure)
    {
        starts.push_back(quarters);
        const core::TimeSignatureChange signature = tempo_map.timeSignatureAt(measure);
        quarters +=
            static_cast<double>(signature.numerator) * quartersPerBeat(signature.denominator);
    }
    return starts;
}

// Quarter-note position of a (measure, beat) grid address.
[[nodiscard]] double quartersAtBeat(
    const std::vector<double>& measure_starts, const core::TempoMap& tempo_map, int measure,
    int beat)
{
    const std::size_t measure_index = static_cast<std::size_t>(measure - 1);
    const double start =
        measure_index < measure_starts.size() ? measure_starts[measure_index] : 0.0;
    const core::TimeSignatureChange signature = tempo_map.timeSignatureAt(measure);
    return start + static_cast<double>(beat - 1) * quartersPerBeat(signature.denominator);
}

} // namespace

void mirrorTempoMapIntoSequence(tracktion::TempoSequence& sequence, const core::TempoMap& tempo_map)
{
    const std::vector<core::BeatAnchor>& anchors = tempo_map.anchors();
    if (anchors.size() < 2)
    {
        return;
    }

    // The table must cover every addressed measure: anchors plus signature changes, which may
    // legally start past the final anchor.
    const std::vector<core::TimeSignatureChange>& signatures = tempo_map.timeSignatures();
    const int last_addressed_measure =
        std::max(anchors.back().measure, signatures.empty() ? 1 : signatures.back().measure);
    const std::vector<double> measure_starts =
        measureStartQuarters(tempo_map, last_addressed_measure);

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
        const double from_quarters =
            quartersAtBeat(measure_starts, tempo_map, from.measure, from.beat);
        const double to_quarters = quartersAtBeat(measure_starts, tempo_map, to.measure, to.beat);
        const double span_seconds = to.seconds - from.seconds;
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
                quartersAtBeat(measure_starts, tempo_map, signatures.front().measure, 1));
            first_signature->numerator = signatures.front().numerator;
            first_signature->denominator = signatures.front().denominator;
        }
        for (std::size_t index = 1; index < signatures.size(); ++index)
        {
            const tracktion::TimeSigSetting::Ptr inserted = sequence.insertTimeSig(
                tracktion::BeatPosition::fromBeats(
                    quartersAtBeat(measure_starts, tempo_map, signatures[index].measure, 1)));
            if (inserted != nullptr)
            {
                inserted->numerator = signatures[index].numerator;
                inserted->denominator = signatures[index].denominator;
            }
        }
    }
}

} // namespace rock_hero::common::audio
