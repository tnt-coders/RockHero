#include "chart/chart.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

int fretFor(const int fret, const std::optional<double>& harmonic_node, const NoteAttack attack)
{
    return handFretOf(frettingStopAt(fret, harmonic_node, attack, fret));
}

int handFretOf(const ChartStop& stop)
{
    // Bound once so the presence test and the read are provably the same object.
    const std::optional<double>& node = stop.node;
    if (node.has_value())
    {
        return static_cast<int>(std::ceil(*node));
    }
    return stop.fret;
}

std::string chartStopText(const ChartStop& stop)
{
    const std::optional<double>& node = stop.node;
    if (node.has_value())
    {
        return harmonicNodeText(*node);
    }
    return std::to_string(stop.fret);
}

int fretFor(const ChartNote& note)
{
    return fretFor(note.fret, note.harmonic_node, note.attack);
}

std::string harmonicNodeText(const double node)
{
    // Rounded to tenths in integer space so the whole-number test and the printed tenth cannot
    // disagree the way separate float roundings can.
    const long tenths = std::lround(node * 10.0);
    if (tenths % 10 == 0)
    {
        return std::to_string(tenths / 10);
    }
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

ChartNote savedChartNote(const ChartNote& note)
{
    ChartNote saved = note;
    // The held stop belongs to the hand that did NOT make this onset, so it exists only where the
    // picking hand made it. Everywhere else the fretting hand's stop already IS `fret`, and a
    // second copy beside it could only ever drift; stripping it here rather than listing the legal
    // attacks in the validator is what makes one rule answer for the reader, the writer and the
    // refusal at once. A silent hold is refused by this too — it is the FRETTING hand's own
    // record — and the aggregate below states that positively rather than relying on this line.
    if (!rightHandOnset(saved.attack))
    {
        saved.held.reset();
    }
    if (silentHold(saved.attack))
    {
        // Built from a DEFAULT note rather than by clearing fields on a copy, so the record is
        // stated positively — a silent hold is its slot, its stop and its attack — and a technique
        // field added to ChartNote later is stripped here (and therefore refused by the validator's
        // fixpoint) without anyone remembering to add a line.
        return ChartNote{
            .position = note.position,
            .string = note.string,
            .fret = note.fret,
            .sustain = {},
            .attack = note.attack,
            .held = {},
            .palm_mute = false,
            .dead = false,
            .harmonic_node = {},
            .vibrato = VibratoState::Off,
            .tremolo = false,
            .emphasis = NoteEmphasis::Normal,
            .bend = 0.0,
            .keyframes = {},
            .slide_out = {},
        };
    }
    if (isScrape(saved.attack))
    {
        saved.palm_mute = false;
        saved.dead = false;
        saved.harmonic_node.reset();
        saved.vibrato = VibratoState::Off;
        saved.tremolo = false;
        saved.bend = 0.0;
        // The pitched CHANNELS go with the pitched fields: a scrape's turnarounds are pick travel,
        // so a bend or vibrato statement riding one is exactly as latent as the note's own. What
        // survives is the fret channel, which is the path itself; a keyframe left stating nothing
        // is no record at all and leaves with them.
        static_cast<void>(stripKeyframeChannels(saved.keyframes, [](Keyframe& keyframe) {
            const bool latent = keyframe.bend.has_value() || keyframe.vibrato.has_value();
            keyframe.bend.reset();
            keyframe.vibrato.reset();
            return latent;
        }));
    }
    return saved;
}

std::vector<HarmonicNodeCandidate> harmonicNodeCandidates(
    const double notated, const int max_partial)
{
    std::vector<HarmonicNodeCandidate> candidates;
    // Ascending by partial so the FIRST candidate found at a position is the lowest-order one —
    // the partial that actually sounds there — and the dedup below can then keep the first and
    // drop the rest without comparing ordinals. Every node of every partial in range, not just the
    // nut-side one: notation names bridge-side nodes too (19 and 24 are the 3rd and 4th partials'
    // second and third nodes).
    for (int partial = 2; partial <= max_partial; ++partial)
    {
        for (int index = 1; index < partial; ++index)
        {
            const double position =
                12.0 *
                std::log2(static_cast<double>(partial) / static_cast<double>(partial - index));
            // A partial's nodes ASCEND in `index` — the ratio grows as the divisor shrinks — so
            // once one has climbed past the label's window every later one has too. Worth the
            // break rather than a bare `continue`: this runs once per selected note on every
            // view-state push, and without it every label pays the whole 28-node table.
            if (position > notated + g_max_node_label_error)
            {
                break;
            }
            if (notated - position > g_max_node_label_error)
            {
                continue;
            }
            // Two partials share a node when their positions agree, and the arithmetic that
            // produces them is not bit-identical across the two derivations (12*log2(4/2) and
            // 12*log2(8/4) both mean the octave), so the test is a tolerance rather than equality.
            // A thousandth of a fret is far below the gap between distinct nodes — the closest
            // pair inside the partial cap is 0.043 apart — and far above the last-place error of
            // a logarithm.
            constexpr double same_node = 0.001;
            const bool already_listed =
                std::ranges::any_of(candidates, [position](const HarmonicNodeCandidate& listed) {
                    return std::abs(listed.position - position) < same_node;
                });
            if (!already_listed)
            {
                candidates.push_back(
                    HarmonicNodeCandidate{
                        .position = position,
                        .partial = partial,
                    });
            }
        }
    }
    // Ascending by POSITION, which is the ladder a charter reads and the order the picker cycles;
    // the partial-first walk above was only the way to reach the lowest ordinal per position.
    std::ranges::sort(candidates, {}, &HarmonicNodeCandidate::position);
    return candidates;
}

std::size_t nearestHarmonicNode(
    const std::span<const HarmonicNodeCandidate> candidates, const double notated)
{
    assert(!candidates.empty() && "nearestHarmonicNode needs a candidate to be nearest to");
    const auto nearest =
        std::ranges::min_element(candidates, {}, [notated](const HarmonicNodeCandidate& candidate) {
            return std::abs(candidate.position - notated);
        });
    return static_cast<std::size_t>(std::ranges::distance(candidates.begin(), nearest));
}

} // namespace rock_hero::common::core
