#include "timeline/tempo_map.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <rock_hero/common/core/timeline/tempo_map_rules.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

constexpr double g_default_seconds_per_beat = 0.5;
constexpr int g_default_beats_per_measure = 4;

// Keeps negative or otherwise invalid measure inputs from underflowing segment calculations.
[[nodiscard]] int normalizedMeasure(int measure) noexcept
{
    return std::max(1, measure);
}

// Keeps negative global indices pinned to the first beat of the song.
[[nodiscard]] std::int64_t normalizedGlobalBeatIndex(std::int64_t global_beat_index) noexcept
{
    return std::max<std::int64_t>(0, global_beat_index);
}

} // namespace

// Creates a minimal usable map for draft songs and tests that do not author tempo yet.
TempoMap::TempoMap()
    : m_time_signatures{
          TimeSignatureChange{
              .measure = 1,
              .numerator = g_default_beats_per_measure,
              .denominator = 4,
          },
      }
    , m_anchors{
          BeatAnchor{
              .measure = 1,
              .beat = 1,
              .seconds = 0.0,
          },
          BeatAnchor{
              .measure = 2,
              .beat = 1,
              .seconds = g_default_seconds_per_beat * g_default_beats_per_measure,
          },
      }
{
    buildDerivedIndices();
}

// Stores caller-provided map data without imposing package validation policy on the value type.
TempoMap::TempoMap(
    std::vector<TimeSignatureChange> time_signatures, std::vector<BeatAnchor> anchors)
    : m_time_signatures(std::move(time_signatures))
    , m_anchors(std::move(anchors))
{
    buildDerivedIndices();
}

// Normalizes the signature list into monotonic reigns with prefix beat and whole-note
// positions, then addresses every anchor on both axes. Runs once per construction so beat and
// time queries stay logarithmic instead of rescanning the authored lists; the timeline grid scan
// calls them once per line.
void TempoMap::buildDerivedIndices()
{
    m_segments.clear();
    m_anchor_beat_indices.clear();
    m_anchor_quarter_positions.clear();

    // The first signature governs from measure 1 regardless of its stored measure, so maps whose
    // first change starts late still define a grid at the song start.
    const TimeSignatureChange default_signature{};
    const TimeSignatureChange& first_signature =
        m_time_signatures.empty() ? default_signature : m_time_signatures.front();
    m_segments.push_back(
        SignatureSegment{
            .start_measure = 1,
            .beats_per_measure = std::max(1, first_signature.numerator),
            .start_beat_index = 0,
            .denominator = std::max(1, first_signature.denominator),
            .start_whole_note_position = {},
        });

    for (std::size_t index = 1; index < m_time_signatures.size(); ++index)
    {
        const TimeSignatureChange& signature = m_time_signatures[index];
        const SignatureSegment& previous = m_segments.back();
        // A malformed out-of-order measure clamps forward so segment starts stay monotonic for the
        // binary searches; the later-listed signature still wins from that point on, so a bad map
        // can only misplace beats, never crash.
        const int start_measure = std::max(signature.measure, previous.start_measure);
        const std::int64_t start_beat_index =
            previous.start_beat_index +
            static_cast<std::int64_t>(start_measure - previous.start_measure) *
                previous.beats_per_measure;
        m_segments.push_back(
            SignatureSegment{
                .start_measure = start_measure,
                .beats_per_measure = std::max(1, signature.numerator),
                .start_beat_index = start_beat_index,
                .denominator = std::max(1, signature.denominator),
                .start_whole_note_position =
                    previous.start_whole_note_position +
                    Fraction{
                        static_cast<int>(start_beat_index - previous.start_beat_index),
                        previous.denominator
                    },
            });
    }

    m_anchor_beat_indices.reserve(m_anchors.size());
    m_anchor_quarter_positions.reserve(m_anchors.size());
    for (const BeatAnchor& anchor : m_anchors)
    {
        m_anchor_beat_indices.push_back(globalBeatIndex(anchor.measure, anchor.beat));
        m_anchor_quarter_positions.push_back(
            quarterPositionAtBeatPosition(static_cast<double>(m_anchor_beat_indices.back())));
    }

    // Segment downbeat times resolve through anchor interpolation, so this fill must run after
    // the anchor indices above exist.
    for (SignatureSegment& segment : m_segments)
    {
        segment.start_seconds =
            secondsAtGlobalBeatPosition(static_cast<double>(segment.start_beat_index));
    }
}

// Finds the last segment whose reign starts at or before the measure. upper_bound lands past the
// last segment with an equal start, so a zero-length reign left by a duplicate or out-of-order
// signature loses to the later-listed one, the same answer timeSignatureAt's walk gives.
const TempoMap::SignatureSegment& TempoMap::segmentForMeasure(int measure) const noexcept
{
    const auto after =
        std::ranges::upper_bound(m_segments, measure, {}, &SignatureSegment::start_measure);
    return *std::prev(after);
}

// Finds the last segment whose first beat sits at or before the global index, with the same
// duplicate-start tie-break as segmentForMeasure.
const TempoMap::SignatureSegment& TempoMap::segmentForBeatIndex(
    std::int64_t global_beat_index) const noexcept
{
    const auto after = std::ranges::upper_bound(
        m_segments, global_beat_index, {}, &SignatureSegment::start_beat_index);
    return *std::prev(after);
}

// Finds the last segment whose downbeat sits at or before a whole-note position, with the same
// duplicate-start tie-break as the other segment lookups.
const TempoMap::SignatureSegment& TempoMap::segmentForWholeNotePosition(
    const Fraction whole_note_position) const noexcept
{
    const auto after = std::ranges::upper_bound(
        m_segments, whole_note_position, {}, &SignatureSegment::start_whole_note_position);
    return *std::prev(after);
}

// Finds the last segment starting at or before a quarter-note position, with the same
// duplicate-start tie-break as the other segment lookups; positions before zero clamp to the
// front segment.
const TempoMap::SignatureSegment& TempoMap::segmentForQuarterPosition(
    double quarter_position) const noexcept
{
    const auto after = std::ranges::upper_bound(
        m_segments, quarter_position, {}, &SignatureSegment::startQuarterPosition);
    return after == m_segments.begin() ? m_segments.front() : *std::prev(after);
}

// Converts a beat position to metronome time. A fractional position belongs to the beat it starts
// in, so the containing segment comes from the floored index; multiplying by a power-of-two
// quarters-per-beat scale keeps constant-denominator spans bit-identical to beat-axis math.
double TempoMap::quarterPositionAtBeatPosition(double global_beat_position) const noexcept
{
    const double clamped_position = std::max(0.0, global_beat_position);
    const SignatureSegment& segment =
        segmentForBeatIndex(static_cast<std::int64_t>(clamped_position));
    return segment.startQuarterPosition() +
           (clamped_position - static_cast<double>(segment.start_beat_index)) *
               segment.quartersPerBeat();
}

// Creates the editor fallback grid used before imported songs receive authored timing.
TempoMap TempoMap::defaultMap(TimeDuration audio_duration)
{
    // The one-measure default grown by the one covering rule, so this map's end and the end a
    // typed tempo grows to can never be two different answers.
    const TempoMap seed;
    return TempoMap{
        seed.m_time_signatures,
        std::vector{
            seed.m_anchors.front(),
            seed.terminalCovering(audio_duration.seconds, g_default_seconds_per_beat),
        },
    };
}

BeatAnchor TempoMap::terminalCovering(double cover_until_seconds, double seconds_per_quarter) const
{
    if (m_anchors.size() < 2)
    {
        return m_anchors.empty() ? BeatAnchor{} : m_anchors.back();
    }
    const BeatAnchor& terminal = m_anchors.back();
    if (!std::isfinite(cover_until_seconds) || !std::isfinite(seconds_per_quarter) ||
        seconds_per_quarter <= 0.0 || terminal.seconds >= cover_until_seconds)
    {
        return terminal;
    }

    // Every candidate downbeat is timed from the last anchor before the terminal and rounded once,
    // never by stepping from a rounded time, so the error stays half a millisecond however far the
    // terminal travels.
    const BeatAnchor& opener = m_anchors[m_anchors.size() - 2];
    const auto seconds_at = [&opener, seconds_per_quarter](const double quarter_count) {
        return nearestAnchorSeconds(opener.seconds + quarter_count * seconds_per_quarter);
    };
    const auto measure_quarters = [this](const int at_measure) {
        const TimeSignatureChange signature = timeSignatureAt(at_measure);
        return static_cast<double>(signature.numerator) * 4.0 /
               static_cast<double>(signature.denominator);
    };

    int measure = terminal.measure;
    double quarters = spanQuarterLength(m_anchors.size() - 2);

    // Measure by measure through any meter change still ahead of the terminal...
    const int last_change = m_time_signatures.back().measure;
    while (measure < last_change && seconds_at(quarters) < cover_until_seconds)
    {
        quarters += measure_quarters(measure);
        ++measure;
    }

    // ...past which every measure is the same length, so the count still missing is solved in one
    // step and settled against the rounding, however fast the tempo.
    if (seconds_at(quarters) < cover_until_seconds)
    {
        const double per_measure = measure_quarters(measure);
        const double missing =
            (cover_until_seconds - opener.seconds) / seconds_per_quarter - quarters;
        const double most_steps = static_cast<double>(std::numeric_limits<int>::max() - measure);
        auto steps =
            static_cast<int>(std::clamp(std::ceil(missing / per_measure), 0.0, most_steps));
        while (steps > 0 && seconds_at(quarters + static_cast<double>(steps - 1) * per_measure) >=
                                cover_until_seconds)
        {
            --steps;
        }
        while (steps < std::numeric_limits<int>::max() - measure &&
               seconds_at(quarters + static_cast<double>(steps) * per_measure) <
                   cover_until_seconds)
        {
            ++steps;
        }
        measure += steps;
        quarters += static_cast<double>(steps) * per_measure;
    }

    return BeatAnchor{.measure = measure, .beat = 1, .seconds = seconds_at(quarters)};
}

// Exposes time signatures as read-only package data.
const std::vector<TimeSignatureChange>& TempoMap::timeSignatures() const noexcept
{
    return m_time_signatures;
}

// Exposes anchors as read-only package data.
const std::vector<BeatAnchor>& TempoMap::anchors() const noexcept
{
    return m_anchors;
}

// Carries the last change forward into the requested measure.
TimeSignatureChange TempoMap::timeSignatureAt(int measure) const noexcept
{
    if (m_time_signatures.empty())
    {
        return TimeSignatureChange{};
    }

    const int target_measure = normalizedMeasure(measure);
    TimeSignatureChange active_signature = m_time_signatures.front();
    for (const TimeSignatureChange& signature : m_time_signatures)
    {
        if (signature.measure > target_measure)
        {
            break;
        }
        active_signature = signature;
    }

    return active_signature;
}

// Returns the active numerator because note beat addresses are counted in denominator beats.
int TempoMap::beatsPerMeasureAt(int measure) const noexcept
{
    return std::max(1, timeSignatureAt(measure).numerator);
}

// Finds the signature whose reign contains the absolute time via the derived segments' downbeat
// seconds; segments map one-to-one onto the authored signature list.
TimeSignatureChange TempoMap::timeSignatureAtSeconds(double seconds) const noexcept
{
    if (m_time_signatures.empty())
    {
        return TimeSignatureChange{};
    }

    const auto after =
        std::ranges::upper_bound(m_segments, seconds, {}, &SignatureSegment::start_seconds);
    // Positions before the first downbeat still use the first signature, which governs from
    // measure 1.
    const std::size_t index =
        after == m_segments.begin()
            ? 0
            : static_cast<std::size_t>(std::distance(m_segments.begin(), after)) - 1;
    return m_time_signatures[index];
}

// Inverts the metronome-linear interpolation: inverse-lerps the containing anchor span to a
// quarter position, then converts back to beats through the containing segment's scale.
double TempoMap::beatPositionAtSeconds(double seconds) const noexcept
{
    if (m_anchors.empty())
    {
        return 0.0;
    }

    if (m_anchors.size() == 1 || seconds <= m_anchors.front().seconds)
    {
        return static_cast<double>(m_anchor_beat_indices.front());
    }

    if (seconds >= m_anchors.back().seconds)
    {
        return static_cast<double>(m_anchor_beat_indices.back());
    }

    // The span starts at the last anchor at or before the position; the clamps above guarantee an
    // interior position with a real span around it.
    const auto after = std::ranges::upper_bound(m_anchors, seconds, {}, &BeatAnchor::seconds);
    const auto first_after = static_cast<std::size_t>(std::distance(m_anchors.begin(), after));
    const std::size_t span_start =
        std::min(first_after > 0 ? first_after - 1 : 0, m_anchors.size() - 2);

    const double seconds_span = m_anchors[span_start + 1].seconds - m_anchors[span_start].seconds;
    if (seconds_span <= 0.0)
    {
        return static_cast<double>(m_anchor_beat_indices[span_start]);
    }

    const double fraction = (seconds - m_anchors[span_start].seconds) / seconds_span;
    const double quarter_position = m_anchor_quarter_positions[span_start] +
                                    fraction * (m_anchor_quarter_positions[span_start + 1] -
                                                m_anchor_quarter_positions[span_start]);
    const SignatureSegment& segment = segmentForQuarterPosition(quarter_position);
    return static_cast<double>(segment.start_beat_index) +
           (quarter_position - segment.startQuarterPosition()) / segment.quartersPerBeat();
}

double TempoMap::spanQuarterLength(std::size_t span) const noexcept
{
    if (span + 1 >= m_anchor_quarter_positions.size())
    {
        return 0.0;
    }
    return m_anchor_quarter_positions[span + 1] - m_anchor_quarter_positions[span];
}

// Only anchors before the terminal open a span, so the terminal is left out of the search.
std::optional<std::size_t> TempoMap::spanStartingAt(
    const int measure, const int beat) const noexcept
{
    for (std::size_t span = 0; span + 1 < m_anchors.size(); ++span)
    {
        if (m_anchors[span].measure == measure && m_anchors[span].beat == beat)
        {
            return span;
        }
    }
    return std::nullopt;
}

// Converts sparse measure/beat addresses into a continuous beat axis for interpolation.
std::int64_t TempoMap::globalBeatIndex(int measure, int beat) const noexcept
{
    const int target_measure = normalizedMeasure(measure);
    const SignatureSegment& segment = segmentForMeasure(target_measure);
    return segment.start_beat_index +
           static_cast<std::int64_t>(target_measure - segment.start_measure) *
               segment.beats_per_measure +
           static_cast<std::int64_t>(std::max(1, beat) - 1);
}

// Inverts the beat axis back to a measure address inside the owning signature segment.
std::pair<int, int> TempoMap::beatAtGlobalIndex(std::int64_t global_beat_index) const noexcept
{
    const std::int64_t target = normalizedGlobalBeatIndex(global_beat_index);
    const SignatureSegment& segment = segmentForBeatIndex(target);
    const std::int64_t beats_into_segment = target - segment.start_beat_index;
    return {
        segment.start_measure + static_cast<int>(beats_into_segment / segment.beats_per_measure),
        static_cast<int>(beats_into_segment % segment.beats_per_measure) + 1,
    };
}

// The segment holds its own denominator, so a whole-note position is the segment's downbeat plus
// the beats into it, each worth one signature-denominator note.
Fraction TempoMap::wholeNotePositionAt(const GridPosition position) const noexcept
{
    const std::int64_t target = globalBeatIndex(position.measure, position.beat);
    const SignatureSegment& segment = segmentForBeatIndex(target);
    const Fraction beats_into_segment =
        Fraction{static_cast<int>(target - segment.start_beat_index)} + position.offset;
    return segment.start_whole_note_position +
           beats_into_segment * Fraction{1, segment.denominator};
}

// Inverts the whole-note axis inside the owning segment: the whole notes past its downbeat, in that
// segment's beats, split into the beat index and the sub-beat remainder. The clamp to the song's
// start keeps the remainder non-negative, so plain integer division floors it.
std::pair<std::int64_t, Fraction> TempoMap::beatAtWholeNotePosition(
    const Fraction whole_note_position) const noexcept
{
    const Fraction target = std::max(whole_note_position, Fraction{});
    const SignatureSegment& segment = segmentForWholeNotePosition(target);
    const Fraction beats_into_segment =
        (target - segment.start_whole_note_position) * Fraction{segment.denominator};
    const int whole_beats = beats_into_segment.numerator / beats_into_segment.denominator;
    return {
        segment.start_beat_index + whole_beats,
        beats_into_segment - Fraction{whole_beats},
    };
}

// Resolves beat addresses through the same fractional interpolation used for note offsets.
double TempoMap::secondsAtBeat(int measure, int beat) const noexcept
{
    return secondsAtGlobalBeatPosition(static_cast<double>(globalBeatIndex(measure, beat)));
}

// Resolves chart-note offsets as a fraction of the beat span containing the note.
double TempoMap::secondsAtNote(int measure, int beat, Fraction offset) const noexcept
{
    return secondsAtGlobalBeatPosition(
        static_cast<double>(globalBeatIndex(measure, beat)) + offset.toDouble());
}

// Reports the last anchor's grid address so package validation can enforce the terminal boundary.
std::int64_t TempoMap::terminalGlobalBeatIndex() const noexcept
{
    return m_anchor_beat_indices.empty() ? 0 : m_anchor_beat_indices.back();
}

// Interpolates on the metronome axis inside the anchor span found by binary search over the
// precomputed anchor quarter positions, clamping outside the authored map. This is the shared hot
// query behind every beat, note, and grid-line time lookup, so it must never rescan the anchor
// list.
double TempoMap::secondsAtGlobalBeatPosition(double global_beat_position) const noexcept
{
    if (m_anchors.empty())
    {
        return 0.0;
    }

    if (m_anchors.size() == 1 || global_beat_position <= 0.0)
    {
        return m_anchors.front().seconds;
    }

    // First anchor at or after the position, clamped so a left neighbor always exists; positions
    // past the terminal anchor clamp to its time.
    const double quarter_position = quarterPositionAtBeatPosition(global_beat_position);
    const auto right_iterator =
        std::ranges::lower_bound(m_anchor_quarter_positions, quarter_position);
    const auto right_index = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(
            std::distance(m_anchor_quarter_positions.begin(), right_iterator)));
    return secondsAtAnchorSpan(right_index, quarter_position);
}

// Interpolates inside one anchor span on the metronome axis. Shared by the binary-search query
// and the forward cursor so both resolve identical times for the same position.
double TempoMap::secondsAtAnchorSpan(
    std::size_t right_index, double quarter_position) const noexcept
{
    // Clamp below the first anchor instead of extrapolating a negative fraction through the first
    // span. Only reachable when the first anchor is not measure 1 beat 1 — package validation
    // forbids that, but unvalidated value-constructed maps must still honor the documented
    // clamp-outside-the-range contract, and beatPositionAtSeconds already clamps the inverse.
    if (quarter_position <= m_anchor_quarter_positions.front())
    {
        return m_anchors.front().seconds;
    }

    if (right_index >= m_anchors.size())
    {
        return m_anchors.back().seconds;
    }

    const double left_quarter = m_anchor_quarter_positions[right_index - 1];
    const double right_quarter = m_anchor_quarter_positions[right_index];
    const double left_seconds = m_anchors[right_index - 1].seconds;
    const double right_seconds = m_anchors[right_index].seconds;
    if (right_quarter <= left_quarter)
    {
        // A non-increasing anchor pair from a malformed map cannot interpolate; snap to the later
        // anchor rather than dividing by a non-positive span.
        return right_seconds;
    }

    const double fraction = (quarter_position - left_quarter) / (right_quarter - left_quarter);
    return left_seconds + ((right_seconds - left_seconds) * fraction);
}

// Binds to the map; the right-anchor index starts at the first interpolable pair.
TempoMap::ForwardBeatTimeCursor::ForwardBeatTimeCursor(const TempoMap& tempo_map) noexcept
    : m_tempo_map(&tempo_map)
{}

// Advances the segment and anchor-span indices linearly instead of binary-searching. Total
// advancement across a scan is bounded by the segment and anchor counts, so a whole monotonic
// scan costs one pass over each; the stop conditions mirror the binary lookups so both resolve
// identical spans and therefore identical times.
double TempoMap::ForwardBeatTimeCursor::secondsAt(double global_beat_position) noexcept
{
    const TempoMap& tempo_map = *m_tempo_map;
    if (tempo_map.m_anchors.empty())
    {
        return 0.0;
    }

    if (tempo_map.m_anchors.size() == 1 || global_beat_position <= 0.0)
    {
        return tempo_map.m_anchors.front().seconds;
    }

    // Advance to the last segment starting at or before the position's beat, matching
    // segmentForBeatIndex's tie-break, then convert to metronome time exactly as
    // quarterPositionAtBeatPosition does.
    const auto beat_floor = static_cast<std::int64_t>(global_beat_position);
    while (m_segment + 1 < tempo_map.m_segments.size() &&
           tempo_map.m_segments[m_segment + 1].start_beat_index <= beat_floor)
    {
        ++m_segment;
    }
    const SignatureSegment& segment = tempo_map.m_segments[m_segment];
    const double quarter_position =
        segment.startQuarterPosition() +
        (global_beat_position - static_cast<double>(segment.start_beat_index)) *
            segment.quartersPerBeat();

    while (m_right_anchor < tempo_map.m_anchor_quarter_positions.size() &&
           tempo_map.m_anchor_quarter_positions[m_right_anchor] < quarter_position)
    {
        ++m_right_anchor;
    }

    return tempo_map.secondsAtAnchorSpan(m_right_anchor, quarter_position);
}

// Compares vector-owned map data while BeatAnchor owns exact floating-point equality. The derived
// index tables are a pure function of that data, so they intentionally stay out of the comparison.
bool operator==(const TempoMap& lhs, const TempoMap& rhs)
{
    return lhs.m_time_signatures == rhs.m_time_signatures && lhs.m_anchors == rhs.m_anchors;
}

} // namespace rock_hero::common::core
