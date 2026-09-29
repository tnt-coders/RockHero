#include "chart/chart.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <compare>
#include <cstddef>
#include <iterator>
#include <optional>
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

namespace
{

// One stop of a note's fret path: an offset along the ring and the position stated there.
struct PathStop
{
    Fraction offset;
    int fret{};
};

// The note's fret path as the stops that STATE it — its onset at offset zero, then each
// fret-stating keyframe in turn, the slide-out included. Between stops the position interpolates
// and past the last one it holds, which is the same sequence the board walks a projection later
// (`highwaySlideStateAt`), read here off the authored note.
[[nodiscard]] std::vector<PathStop> fretPathStops(const ChartNote& note)
{
    std::vector<PathStop> stops;
    stops.reserve(note.keyframes.size() + 1);
    stops.push_back(PathStop{.offset = Fraction{0}, .fret = note.fret});
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<int>& fret = keyframe.fret;
        if (fret.has_value())
        {
            stops.push_back(PathStop{.offset = keyframe.offset, .fret = *fret});
        }
    }
    return stops;
}

// Whether a point at `offset` stating `fret` would change the path function. Between two stops
// the path is linear, so "already passes through" is exact collinearity — asked by
// cross-multiplying rather than by evaluating a rational fret no integer statement could equal.
[[nodiscard]] bool statesNewPathPoint(const ChartNote& note, const Fraction offset, const int fret)
{
    const std::vector<PathStop> stops = fretPathStops(note);
    // The onset is always a stop, so the walk always has a segment start to measure from.
    PathStop previous = stops.front();
    for (const PathStop& stop : stops)
    {
        if (!(offset < stop.offset))
        {
            previous = stop;
            continue;
        }
        const Fraction stated = Fraction{fret - previous.fret} * (stop.offset - previous.offset);
        const Fraction travelled = Fraction{stop.fret - previous.fret} * (offset - previous.offset);
        return !(stated == travelled);
    }
    // Past the last stop the path HOLDS its target, so only a different fret says anything new.
    return fret != previous.fret;
}

// Whether a bend point at `offset` stating `semitones` would change the curve. The curve is
// interpolated like the path, but its values are doubles, so only the FLAT case is judged, by exact
// equality: a value that repeats the statement before it and is repeated by the one after (or is
// the last, past which the curve holds) lies on a flat segment and says nothing. A point on a
// sloped segment is kept, whether or not it is collinear — no information is ever stripped by an
// approximate test.
[[nodiscard]] bool statesNewBendPoint(
    const ChartNote& note, const Fraction offset, const double semitones)
{
    if (std::is_neq(semitones <=> ringStateAt(note, offset).bend))
    {
        return true;
    }
    for (const Keyframe& keyframe : note.keyframes)
    {
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<double>& bend = keyframe.bend;
        if (offset < keyframe.offset && bend.has_value())
        {
            return std::is_neq(semitones <=> *bend);
        }
    }
    return false;
}

// Whether the note's LAST keyframe says nothing its path does not already say. THE KEYFRAME COMMIT
// LAW is defined on the path WITHOUT the point, so the point comes off a copy before the walk reads
// it — which is also what makes the walk account for every earlier junction rather than the onset
// alone. Written once here because two rules ask it of the same keyframe: the landing grant, which
// refuses to bare a point that would state nothing, and the slide-out dissolve, which removes one
// that already does. The note must carry at least one keyframe.
[[nodiscard]] bool lastKeyframeSaysNothingNew(const ChartNote& note)
{
    assert(!note.keyframes.empty() && "the commit law needs a last keyframe to judge");
    ChartNote without = note;
    without.keyframes.pop_back();
    return keyframeSaysNothingNew(without, note.keyframes.back());
}

} // namespace

Keyframe shedSilentStatements(const ChartNote& note, Keyframe point)
{
    // Each channel bound to a local so the presence test and the read are provably one object.
    // The vibrato has no absent value to shed to: a width equal to the leg before simply changes
    // nothing, and the point's other statements decide whether it stands.
    //
    // A fret on the path can still be a STOP: a vibrato change stands only at a stop or where the
    // hand rests (shedMidTravelVibrato), so a fret lying on a glide is what a change there stands
    // on. On a hold — the fret already in force — the change needs no stop and the fret goes.
    if (const std::optional<int>& fret = point.fret;
        fret.has_value() && !statesNewPathPoint(note, point.offset, *fret))
    {
        const RingState before = ringStateAt(note, point.offset);
        if (*fret == before.fret || point.vibrato == before.vibrato)
        {
            point.fret.reset();
        }
    }
    if (const std::optional<double>& bend = point.bend;
        bend.has_value() && !statesNewBendPoint(note, point.offset, *bend))
    {
        point.bend.reset();
    }
    return point;
}

bool keyframeSaysNothingNew(const ChartNote& note, const Keyframe& point)
{
    const Keyframe spoken = shedSilentStatements(note, point);
    if (spoken.fret.has_value() || spoken.bend.has_value())
    {
        return false;
    }
    // The leg this point begins says something exactly when its width differs from the leg
    // before it: with the point gone that leg would cover this stretch, so a repeated width
    // changes nothing, and beginning an unvibrated leg after a vibrated one changes everything.
    // At the ring's end no leg begins, so no width says anything there. `note` is the path
    // WITHOUT the point, so the state at its offset is the leg before.
    return !(point.offset < note.sustain) ||
           point.vibrato == ringStateAt(note, point.offset).vibrato;
}

ChartNote noteWithoutKeyframe(ChartNote note, const Fraction offset)
{
    std::erase_if(
        note.keyframes, [offset](const Keyframe& keyframe) { return keyframe.offset == offset; });
    return note;
}

bool stripSilentKeyframes(ChartNote& note)
{
    // Each point is judged against the note WITHOUT it and WITH every other: a silent statement
    // leaves the path unchanged by definition, so the verdicts do not depend on the order they are
    // read in, and one pass takes every silent statement — and every point left saying nothing —
    // at once.
    std::vector<Keyframe> kept;
    kept.reserve(note.keyframes.size());
    for (const Keyframe& keyframe : note.keyframes)
    {
        const ChartNote without = noteWithoutKeyframe(note, keyframe.offset);
        if (!keyframeSaysNothingNew(without, keyframe))
        {
            kept.push_back(shedSilentStatements(without, keyframe));
        }
    }
    const bool stripped = kept != note.keyframes;
    note.keyframes = std::move(kept);
    return stripped;
}

bool ringEndMayLandOnLastKeyframe(const ChartNote& note)
{
    // The ring must simply END: where a statement already stands there, the end IS that point and
    // its own length is the point's to change. Asked of the end's own statement whatever it states,
    // which is what makes "the tail simply ends" one comparison rather than a list of channels.
    if (note.keyframes.empty() || endStatement(note) != nullptr)
    {
        return false;
    }
    const Keyframe& last = note.keyframes.back();
    if (!last.fret.has_value() || endStatementWouldShedVibrato(last))
    {
        return false;
    }
    // Whether the landing TRAVELS is THE KEYFRAME COMMIT LAW's own question. With no vibrato left
    // to judge (guarded above) and a bend beside the fret saying nothing about position, what it
    // answers here is exactly whether the fret differs from the one already in force at that
    // offset.
    return !lastKeyframeSaysNothingNew(note);
}

ChartNote savedChartNote(const ChartNote& note)
{
    ChartNote saved = note;
    // A planted finger exists only where the picking hand is what stops the string. Everywhere
    // else — an ordinary press, and a harmonic of either hand — the fretting hand's stop already IS
    // `fret`, and a second copy beside it could only ever drift; stripping it here rather than
    // listing the legal shapes in the validator is what makes one rule answer for the reader, the
    // writer and the refusal at once.
    if (!pickingHandStopsString(saved.attack, saved.harmonic_node))
    {
        saved.held.reset();
    }
    if (isScrape(saved.attack))
    {
        saved.palm_mute = false;
        saved.dead = false;
        saved.harmonic_node.reset();
        saved.vibrato = VibratoState::None;
        saved.tremolo = false;
        saved.bend = 0.0;
        // The pitched CHANNELS go with the pitched fields: a scrape's turnarounds are pick travel,
        // so a bend or vibrato statement riding one is exactly as latent as the note's own. What
        // survives is the fret channel, which is the path itself; a keyframe left bare begins an
        // unvibrated leg, and the commit law sweeps it where that says nothing.
        static_cast<void>(stripKeyframeChannels(saved, [](Keyframe& keyframe) {
            const bool latent = keyframe.bend.has_value() || hasVibrato(keyframe.vibrato);
            keyframe.bend.reset();
            keyframe.vibrato = VibratoState::None;
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
            // pair under the widest cap any caller passes, g_max_harmonic_partial, is 0.077
            // apart — and far above the last-place error of a logarithm.
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
    // Ascending by POSITION, the ladder as it lies along the string; the partial-first walk above
    // was only the way to reach the lowest ordinal per position. The editor re-sorts by partial for
    // its picker, so this order serves import's nearest-node read and the tests that pin it.
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
