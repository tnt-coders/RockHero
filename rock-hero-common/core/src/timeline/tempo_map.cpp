#include "timeline/tempo_map.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

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

// Clamps both terms of a meter to at least one, so a malformed map's segments still advance.
[[nodiscard]] TimeSignature clampedMeter(const TimeSignature meter) noexcept
{
    return TimeSignature{
        .numerator = std::max(1, meter.numerator),
        .denominator = std::max(1, meter.denominator),
    };
}

// Keeps negative global indices pinned to the first beat of the song.
[[nodiscard]] std::int64_t normalizedGlobalBeatIndex(std::int64_t global_beat_index) noexcept
{
    return std::max<std::int64_t>(0, global_beat_index);
}

} // namespace

// The int64 maximum converts up to exactly 2^63, so every double strictly inside it rounds to a
// whole number that fits; NaN fails the comparison, and so does infinity.
bool isRepresentableAnchorTime(const double seconds) noexcept
{
    return std::abs(seconds * 1000.0) <
           static_cast<double>(std::numeric_limits<std::int64_t>::max());
}

// Rounds once with halves away from zero, the one rounding every finer time takes into storage.
std::int64_t nearestAnchorMilliseconds(const double seconds) noexcept
{
    return std::llround(seconds * 1000.0);
}

// Creates a minimal usable map for draft songs and tests that do not author tempo yet.
TempoMap::TempoMap()
    : m_time_signatures{
          TimeSignatureChange{
              .place = {},
              .meter = {.numerator = g_default_beats_per_measure, .denominator = 4},
          },
      }
    , m_anchors{
          BeatAnchor{
              .position = {.measure = 1, .beat = 1, .offset = {}},
              .milliseconds = 0,
          },
          BeatAnchor{
              .position = {.measure = 2, .beat = 1, .offset = {}},
              .milliseconds = nearestAnchorMilliseconds(
                  g_default_seconds_per_beat * g_default_beats_per_measure),
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

// Derives the reign table from the placed changes — every measure's length, the derived short bar
// included — with prefix beat and whole-note positions, then addresses every anchor on both axes.
// Runs once per construction so beat and time queries stay logarithmic instead of rescanning the
// authored lists; the timeline grid scan calls them once per line.
void TempoMap::buildDerivedIndices()
{
    m_segments.clear();
    m_anchor_quarter_positions.clear();

    // The segment `measures` whole measures after `previous`, governing `meter` from there on.
    const auto following = [](const SignatureSegment& previous,
                              const int measures,
                              const TimeSignature meter,
                              const bool derived) {
        return SignatureSegment{
            .start_measure = previous.start_measure + measures,
            .meter = meter,
            .derived = derived,
            .start_beat_index = previous.start_beat_index +
                                static_cast<std::int64_t>(measures) * previous.meter.numerator,
            .start_whole_note_position = previous.start_whole_note_position +
                                         previous.meter.measureWholeNotes() * Fraction{measures},
            .start_seconds = 0.0,
        };
    };

    // The first signature governs from the song's start regardless of its stored place, so maps
    // whose first change starts late still define a grid at the song start.
    const TimeSignatureChange default_signature{};
    const TimeSignatureChange& first_signature =
        m_time_signatures.empty() ? default_signature : m_time_signatures.front();
    m_segments.push_back(
        SignatureSegment{
            .start_measure = 1,
            .meter = clampedMeter(first_signature.meter),
            .derived = false,
            .start_beat_index = 0,
            .start_whole_note_position = {},
            .start_seconds = 0.0,
        });

    for (std::size_t index = 1; index < m_time_signatures.size(); ++index)
    {
        const TimeSignatureChange& signature = m_time_signatures[index];
        // A copy, not a reference: the push below may reallocate.
        const SignatureSegment previous = m_segments.back();
        // A malformed out-of-order place clamps forward so segment starts stay monotonic for the
        // binary searches; the later-listed signature still wins from that point on, so a bad map
        // can only misplace beats, never crash.
        const Fraction place = std::max(signature.place, previous.start_whole_note_position);
        const Fraction measure_length = previous.meter.measureWholeNotes();
        const Fraction reign = place - previous.start_whole_note_position;
        // The whole measures of the reign are the floor of its length in measures; the division
        // is exact and non-negative, so integer division floors it.
        const Fraction measures_exact =
            reign * Fraction{measure_length.denominator, measure_length.numerator};
        const int whole_measures = measures_exact.numerator / measures_exact.denominator;
        const Fraction remainder = reign - measure_length * Fraction{whole_measures};

        SignatureSegment next =
            following(previous, whole_measures, clampedMeter(signature.meter), false);
        if (remainder > Fraction{})
        {
            // The derived short bar: the remainder counted in the governing beat where it is a
            // whole number of them, else in the finer unit both divide. The lcm of two powers of
            // two is the larger; for a malformed place off the meter lattice it is still the unit
            // that makes the count whole, so the table never holds a fractional beat count.
            const int beat = std::lcm(previous.meter.denominator, remainder.denominator);
            const Fraction beats = remainder * Fraction{beat};
            m_segments.push_back(following(
                previous,
                whole_measures,
                TimeSignature{.numerator = beats.numerator, .denominator = beat},
                true));
            next = following(m_segments.back(), 1, clampedMeter(signature.meter), false);
        }
        m_segments.push_back(next);
    }

    // Through the same quarter query every chart instant's time takes, so an instant standing on
    // an anchor resolves to that anchor's own time bit for bit.
    m_anchor_quarter_positions.reserve(m_anchors.size());
    for (const BeatAnchor& anchor : m_anchors)
    {
        m_anchor_quarter_positions.push_back(quarterPositionAt(anchor.position));
    }

    // Segment downbeat times resolve through anchor interpolation, so this fill must run after
    // the anchor quarter positions above exist.
    for (SignatureSegment& segment : m_segments)
    {
        segment.start_seconds = secondsAtWholeNote(segment.start_whole_note_position);
    }
}

// Finds the last segment whose reign starts at or before the measure. upper_bound lands past the
// last segment with an equal start, so a zero-length reign left by a duplicate or out-of-order
// signature loses to the later-listed one.
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
        seconds_per_quarter <= 0.0 || terminal.seconds() >= cover_until_seconds)
    {
        return terminal;
    }

    // Every candidate downbeat is timed from the last anchor before the terminal and rounded once,
    // never by stepping from a rounded time, so the error stays half a millisecond however far the
    // terminal travels. The covered time is a threshold, never stored, so it is compared unrounded.
    const std::size_t opener_index = m_anchors.size() - 2;
    const BeatAnchor& opener = m_anchors[opener_index];
    const auto milliseconds_at = [&opener, seconds_per_quarter](const double quarter_count) {
        return nearestAnchorMilliseconds(opener.seconds() + quarter_count * seconds_per_quarter);
    };
    const double cover_until_milliseconds = cover_until_seconds * 1000.0;
    const auto covers = [&milliseconds_at, cover_until_milliseconds](const double quarter_count) {
        return static_cast<double>(milliseconds_at(quarter_count)) >= cover_until_milliseconds;
    };
    const auto measure_quarters = [this](const int at_measure) {
        return quarterPositionOf(segmentForMeasure(at_measure).meter.measureWholeNotes());
    };

    // The walk starts at the downbeat of the terminal's own measure. The terminal does not cover
    // the time, so neither does that downbeat (at or before it), and the first covering downbeat
    // is a later barline either way: a terminal standing mid-measure grows past its short last bar.
    int measure = terminal.position.measure;
    double quarters =
        quarterPositionAt(measureDownbeat(measure)) - m_anchor_quarter_positions[opener_index];

    // Measure by measure through any meter change still ahead of the terminal: the last segment is
    // the last authored change's reign (a derived one only ever precedes a change).
    const int last_change = m_segments.back().start_measure;
    while (measure < last_change && !covers(quarters))
    {
        quarters += measure_quarters(measure);
        ++measure;
    }

    // ...past which every measure is the same length, so the count still missing is solved in one
    // step and settled against the rounding, however fast the tempo.
    if (!covers(quarters))
    {
        const double per_measure = measure_quarters(measure);
        const double missing =
            (cover_until_seconds - opener.seconds()) / seconds_per_quarter - quarters;
        const auto most_steps = static_cast<double>(std::numeric_limits<int>::max() - measure);
        auto steps =
            static_cast<int>(std::clamp(std::ceil(missing / per_measure), 0.0, most_steps));
        while (steps > 0 && covers(quarters + static_cast<double>(steps - 1) * per_measure))
        {
            --steps;
        }
        while (steps < std::numeric_limits<int>::max() - measure &&
               !covers(quarters + static_cast<double>(steps) * per_measure))
        {
            ++steps;
        }
        measure += steps;
        quarters += static_cast<double>(steps) * per_measure;
    }

    return BeatAnchor{
        .position = {.measure = measure, .beat = 1, .offset = {}},
        .milliseconds = milliseconds_at(quarters),
    };
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

MeterReign TempoMap::reignOf(const SignatureSegment& segment) noexcept
{
    return MeterReign{
        .first_measure = segment.start_measure,
        .place = segment.start_whole_note_position,
        .meter = segment.meter,
        .derived = segment.derived,
    };
}

std::vector<MeterReign> TempoMap::reigns() const
{
    std::vector<MeterReign> reigns;
    reigns.reserve(m_segments.size());
    for (const SignatureSegment& segment : m_segments)
    {
        reigns.push_back(reignOf(segment));
    }
    return reigns;
}

// The reign whose segment holds the measure; the derived short bar is a reign like any other.
MeterReign TempoMap::timeSignatureAt(int measure) const noexcept
{
    return reignOf(segmentForMeasure(normalizedMeasure(measure)));
}

// The segment's numerator, already clamped to at least one, because note beat addresses are
// counted in denominator beats.
int TempoMap::beatsPerMeasureAt(int measure) const noexcept
{
    return timeSignatureAt(measure).meter.numerator;
}

// Inverts the metronome-linear interpolation: inverse-lerps the containing anchor span to a
// quarter position. A map with no anchors has no time to invert and answers the origin.
double TempoMap::quarterPositionAtSeconds(double seconds) const noexcept
{
    if (m_anchors.empty())
    {
        return 0.0;
    }

    if (m_anchors.size() == 1 || seconds <= m_anchors.front().seconds())
    {
        return m_anchor_quarter_positions.front();
    }

    if (seconds >= m_anchors.back().seconds())
    {
        return m_anchor_quarter_positions.back();
    }

    // The clamps above guarantee an interior position with a real span around it.
    const std::size_t span_start = spanAtSeconds(seconds);
    const double seconds_span =
        m_anchors[span_start + 1].seconds() - m_anchors[span_start].seconds();
    if (seconds_span <= 0.0)
    {
        return m_anchor_quarter_positions[span_start];
    }

    const double fraction = (seconds - m_anchors[span_start].seconds()) / seconds_span;
    return m_anchor_quarter_positions[span_start] +
           fraction * (m_anchor_quarter_positions[span_start + 1] -
                       m_anchor_quarter_positions[span_start]);
}

// The projection is the seconds accessor, so the query compares in its own unit and a time between
// two whole milliseconds finds its span.
std::size_t TempoMap::spanAtSeconds(double seconds) const noexcept
{
    const auto after = std::ranges::upper_bound(m_anchors, seconds, {}, &BeatAnchor::seconds);
    const auto first_after = static_cast<std::size_t>(std::distance(m_anchors.begin(), after));
    return std::min(first_after > 0 ? first_after - 1 : 0, m_anchors.size() - 2);
}

// The quarter position converted back to beats through the containing segment's scale.
double TempoMap::beatPositionAtSeconds(double seconds) const noexcept
{
    const double quarter_position = quarterPositionAtSeconds(seconds);
    const SignatureSegment& segment = segmentForQuarterPosition(quarter_position);
    return static_cast<double>(segment.start_beat_index) +
           (quarter_position - segment.startQuarterPosition()) / segment.quartersPerBeat();
}

// Equal meters make the two maps' metronome axes one axis, so the music at a time is its quarter
// position under the origin. The shared-span answer is the exact one, not a shortcut: the
// interpolation there and back would return the time only to within rounding.
double TempoMap::secondsRetimedFrom(const TempoMap& origin, const double seconds) const noexcept
{
    assert(m_time_signatures == origin.m_time_signatures);
    const std::vector<BeatAnchor>& from = origin.m_anchors;
    if (from.size() < 2 || seconds < from.front().seconds() || seconds > from.back().seconds())
    {
        return seconds;
    }
    const std::size_t span = origin.spanAtSeconds(seconds);
    if (span + 1 < m_anchors.size() && m_anchors[span] == from[span] &&
        m_anchors[span + 1] == from[span + 1])
    {
        return seconds;
    }
    return secondsAtQuarterPosition(origin.quarterPositionAtSeconds(seconds));
}

double TempoMap::wholeNotePositionAtSeconds(double seconds) const noexcept
{
    return wholeNotePositionOf(quarterPositionAtSeconds(seconds));
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
std::optional<std::size_t> TempoMap::spanStartingAt(const GridPosition position) const noexcept
{
    for (std::size_t span = 0; span + 1 < m_anchors.size(); ++span)
    {
        if (m_anchors[span].position == position)
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
               segment.meter.numerator +
           static_cast<std::int64_t>(std::max(1, beat) - 1);
}

// Inverts the beat axis back to a measure address inside the owning signature segment.
std::pair<int, int> TempoMap::beatAtGlobalIndex(std::int64_t global_beat_index) const noexcept
{
    const std::int64_t target = normalizedGlobalBeatIndex(global_beat_index);
    const SignatureSegment& segment = segmentForBeatIndex(target);
    const std::int64_t beats_into_segment = target - segment.start_beat_index;
    return {
        segment.start_measure + static_cast<int>(beats_into_segment / segment.meter.numerator),
        static_cast<int>(beats_into_segment % segment.meter.numerator) + 1,
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
           beats_into_segment * Fraction{1, segment.meter.denominator};
}

// Inverts the whole-note axis inside the owning segment: the whole notes past its downbeat, in that
// segment's beats, split into the whole beats and the sub-beat remainder. The clamp to the song's
// start keeps the remainder non-negative, so plain integer division floors it.
GridPosition TempoMap::gridPositionAtWholeNote(const Fraction whole_note_position) const noexcept
{
    const Fraction target = std::max(whole_note_position, Fraction{});
    const SignatureSegment& segment = segmentForWholeNotePosition(target);
    const Fraction beats_into_segment =
        (target - segment.start_whole_note_position) * Fraction{segment.meter.denominator};
    const int whole_beats = beats_into_segment.numerator / beats_into_segment.denominator;
    const auto [measure, beat] = beatAtGlobalIndex(segment.start_beat_index + whole_beats);
    return GridPosition{
        .measure = measure,
        .beat = beat,
        .offset = beats_into_segment - Fraction{whole_beats},
    };
}

double TempoMap::quarterPositionOf(const Fraction whole_note_position) noexcept
{
    return 4.0 * whole_note_position.toDouble();
}

double TempoMap::wholeNotePositionOf(const double quarter_position) noexcept
{
    return quarter_position / 4.0;
}

double TempoMap::quarterPositionAt(const GridPosition position) const noexcept
{
    return quarterPositionOf(wholeNotePositionAt(position));
}

double TempoMap::secondsAt(const GridPosition position) const noexcept
{
    return secondsAtWholeNote(wholeNotePositionAt(position));
}

double TempoMap::secondsAtWholeNote(const Fraction whole_note_position) const noexcept
{
    return secondsAtQuarterPosition(quarterPositionOf(whole_note_position));
}

double TempoMap::secondsAtBeat(int measure, int beat) const noexcept
{
    return secondsAt(GridPosition{.measure = measure, .beat = beat, .offset = {}});
}

// A beat position's time, through its quarter position: secondsAtQuarterPosition owns the binary
// search and the clamps outside the authored map, so this hot grid-line query never rescans the
// anchor list.
double TempoMap::secondsAtGlobalBeatPosition(double global_beat_position) const noexcept
{
    return secondsAtQuarterPosition(quarterPositionAtBeatPosition(global_beat_position));
}

// First anchor at or after the position, clamped so a left neighbor always exists; positions past
// the terminal anchor clamp to its time.
double TempoMap::secondsAtQuarterPosition(const double quarter_position) const noexcept
{
    if (m_anchors.empty())
    {
        return 0.0;
    }
    if (m_anchors.size() == 1)
    {
        return m_anchors.front().seconds();
    }
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
        return m_anchors.front().seconds();
    }

    if (right_index >= m_anchors.size())
    {
        return m_anchors.back().seconds();
    }

    const double left_quarter = m_anchor_quarter_positions[right_index - 1];
    const double right_quarter = m_anchor_quarter_positions[right_index];
    const double left_seconds = m_anchors[right_index - 1].seconds();
    const double right_seconds = m_anchors[right_index].seconds();
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
        return tempo_map.m_anchors.front().seconds();
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

// Compares the authored vectors; anchors compare as plain values. The derived index tables are a
// pure function of that data, so they intentionally stay out of the comparison.
bool operator==(const TempoMap& lhs, const TempoMap& rhs)
{
    return lhs.m_time_signatures == rhs.m_time_signatures && lhs.m_anchors == rhs.m_anchors;
}

bool isRedundantTimeSignature(
    const std::span<const MeterReign> reigns, const TimeSignatureChange& change) noexcept
{
    const auto opened = std::ranges::find(reigns, change.place, &MeterReign::place);
    if (opened == reigns.begin() || opened == reigns.end())
    {
        return false;
    }
    const MeterReign& before = *std::prev(opened);
    return !before.derived && before.meter == change.meter;
}

// The reign a dropped change opened is the same grid its predecessor's was, so the next change is
// judged alike against either, and one pass over the table settles every change.
TempoMap withoutRedundantTimeSignatures(TempoMap tempo_map)
{
    const std::vector<MeterReign> reigns = tempo_map.reigns();
    std::vector<TimeSignatureChange> kept;
    std::ranges::remove_copy_if(
        tempo_map.timeSignatures(),
        std::back_inserter(kept),
        [&reigns](const TimeSignatureChange& change) {
            return isRedundantTimeSignature(reigns, change);
        });
    if (kept.size() == tempo_map.timeSignatures().size())
    {
        return tempo_map;
    }
    return TempoMap{std::move(kept), tempo_map.anchors()};
}

} // namespace rock_hero::common::core
