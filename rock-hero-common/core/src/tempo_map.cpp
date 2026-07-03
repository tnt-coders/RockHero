#include "tempo_map.h"

#include <algorithm>
#include <cmath>
#include <iterator>
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

// Normalizes the signature list into monotonic reigns with prefix beat indices, then addresses
// every anchor on that grid. Runs once per construction so beat and time queries stay logarithmic
// instead of rescanning the authored lists; the timeline grid scan calls them once per line.
void TempoMap::buildDerivedIndices()
{
    m_segments.clear();
    m_anchor_beat_indices.clear();

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
        });

    for (std::size_t index = 1; index < m_time_signatures.size(); ++index)
    {
        const TimeSignatureChange& signature = m_time_signatures[index];
        const SignatureSegment& previous = m_segments.back();
        // A malformed out-of-order measure clamps forward so segment starts stay monotonic for the
        // binary searches; the later-listed signature still wins from that point on, so a bad map
        // can only misplace beats, never crash.
        const int start_measure = std::max(signature.measure, previous.start_measure);
        m_segments.push_back(
            SignatureSegment{
                .start_measure = start_measure,
                .beats_per_measure = std::max(1, signature.numerator),
                .start_beat_index =
                    previous.start_beat_index +
                    static_cast<std::int64_t>(start_measure - previous.start_measure) *
                        previous.beats_per_measure,
            });
    }

    m_anchor_beat_indices.reserve(m_anchors.size());
    for (const BeatAnchor& anchor : m_anchors)
    {
        m_anchor_beat_indices.push_back(globalBeatIndex(anchor.measure, anchor.beat));
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
// signature loses to the later-listed one, matching the previous sequential-walk behavior.
const TempoMap::SignatureSegment& TempoMap::segmentForMeasure(int measure) const noexcept
{
    const auto after = std::upper_bound(
        m_segments.begin(),
        m_segments.end(),
        measure,
        [](int target, const SignatureSegment& segment) { return target < segment.start_measure; });
    return *std::prev(after);
}

// Finds the last segment whose first beat sits at or before the global index, with the same
// duplicate-start tie-break as segmentForMeasure.
const TempoMap::SignatureSegment& TempoMap::segmentForBeatIndex(
    std::int64_t global_beat_index) const noexcept
{
    const auto after = std::upper_bound(
        m_segments.begin(),
        m_segments.end(),
        global_beat_index,
        [](std::int64_t target, const SignatureSegment& segment) {
            return target < segment.start_beat_index;
        });
    return *std::prev(after);
}

// Creates the editor fallback grid used before imported songs receive authored timing.
TempoMap TempoMap::defaultMap(TimeDuration audio_duration)
{
    const double safe_duration =
        std::isfinite(audio_duration.seconds) ? std::max(0.0, audio_duration.seconds) : 0.0;
    const auto beat_count = static_cast<int>(std::ceil(safe_duration / g_default_seconds_per_beat));
    const int content_measures =
        std::max(1, (beat_count + g_default_beats_per_measure - 1) / g_default_beats_per_measure);

    return TempoMap{
        std::vector{
            TimeSignatureChange{
                .measure = 1,
                .numerator = g_default_beats_per_measure,
                .denominator = 4,
            },
        },
        std::vector{
            BeatAnchor{
                .measure = 1,
                .beat = 1,
                .seconds = 0.0,
            },
            BeatAnchor{
                .measure = content_measures + 1,
                .beat = 1,
                .seconds = static_cast<double>(content_measures * g_default_beats_per_measure) *
                           g_default_seconds_per_beat,
            },
        },
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

    const auto after = std::upper_bound(
        m_segments.begin(),
        m_segments.end(),
        seconds,
        [](double target, const SignatureSegment& segment) {
            return target < segment.start_seconds;
        });
    // Positions before the first downbeat still use the first signature, which governs from
    // measure 1.
    const std::size_t index =
        after == m_segments.begin()
            ? 0
            : static_cast<std::size_t>(std::distance(m_segments.begin(), after)) - 1;
    return m_time_signatures[index];
}

// Derives the quarter-note tempo of the anchor span containing the position. The beat rate is
// constant inside a span because interpolation is linear in beats; the quarter-note reference
// then scales by the active denominator (a beat is one 1/denominator note, so a quarter note is
// denominator/4 beats).
double TempoMap::quarterNoteBpmAtSeconds(double seconds) const noexcept
{
    if (m_anchors.size() < 2)
    {
        return 0.0;
    }

    // The span starts at the last anchor at or before the position, clamped so positions before
    // the first anchor use the first span and positions at or past the terminal anchor use the
    // span that ends there.
    const auto after = std::upper_bound(
        m_anchors.begin(), m_anchors.end(), seconds, [](double target, const BeatAnchor& anchor) {
            return target < anchor.seconds;
        });
    const auto first_after = static_cast<std::size_t>(std::distance(m_anchors.begin(), after));
    const std::size_t span_start =
        std::min(first_after > 0 ? first_after - 1 : 0, m_anchors.size() - 2);

    const auto beat_span = static_cast<double>(
        m_anchor_beat_indices[span_start + 1] - m_anchor_beat_indices[span_start]);
    const double seconds_span = m_anchors[span_start + 1].seconds - m_anchors[span_start].seconds;
    if (beat_span <= 0.0 || seconds_span <= 0.0)
    {
        return 0.0;
    }

    const double beats_per_minute = beat_span / seconds_span * 60.0;
    const int denominator = std::max(1, timeSignatureAtSeconds(seconds).denominator);
    return beats_per_minute * 4.0 / static_cast<double>(denominator);
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

// Interpolates inside the anchor span found by binary search over the precomputed anchor beat
// indices, clamping outside the authored map. This is the shared hot query behind every beat,
// note, and grid-line time lookup, so it must never rescan the anchor list.
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
    const auto right_iterator = std::lower_bound(
        m_anchor_beat_indices.begin(),
        m_anchor_beat_indices.end(),
        global_beat_position,
        [](std::int64_t anchor_index, double position) {
            return static_cast<double>(anchor_index) < position;
        });
    const auto right_index = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::distance(m_anchor_beat_indices.begin(), right_iterator)));
    return secondsAtAnchorSpan(right_index, global_beat_position);
}

// Interpolates inside one anchor span. Shared by the binary-search query and the forward cursor so
// both resolve identical times for the same position.
double TempoMap::secondsAtAnchorSpan(
    std::size_t right_index, double global_beat_position) const noexcept
{
    if (right_index >= m_anchors.size())
    {
        return m_anchors.back().seconds;
    }

    const auto left_beat = static_cast<double>(m_anchor_beat_indices[right_index - 1]);
    const auto right_beat = static_cast<double>(m_anchor_beat_indices[right_index]);
    const double left_seconds = m_anchors[right_index - 1].seconds;
    const double right_seconds = m_anchors[right_index].seconds;
    if (right_beat <= left_beat)
    {
        // A non-increasing anchor pair from a malformed map cannot interpolate; snap to the later
        // anchor rather than dividing by a non-positive span.
        return right_seconds;
    }

    const double fraction = (global_beat_position - left_beat) / (right_beat - left_beat);
    return left_seconds + ((right_seconds - left_seconds) * fraction);
}

// Binds to the map; the right-anchor index starts at the first interpolable pair.
TempoMap::ForwardBeatTimeCursor::ForwardBeatTimeCursor(const TempoMap& tempo_map) noexcept
    : m_tempo_map(&tempo_map)
{}

// Advances the anchor span linearly instead of binary-searching. Total advancement across a scan
// is bounded by the anchor count, so a whole monotonic scan costs one pass over the anchors; the
// stop condition mirrors secondsAtGlobalBeatPosition's lower_bound so both pick the same span.
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

    while (m_right_anchor < tempo_map.m_anchor_beat_indices.size() &&
           static_cast<double>(tempo_map.m_anchor_beat_indices[m_right_anchor]) <
               global_beat_position)
    {
        ++m_right_anchor;
    }

    return tempo_map.secondsAtAnchorSpan(m_right_anchor, global_beat_position);
}

// Compares vector-owned map data while BeatAnchor owns exact floating-point equality. The derived
// index tables are a pure function of that data, so they intentionally stay out of the comparison.
bool operator==(const TempoMap& lhs, const TempoMap& rhs)
{
    return lhs.m_time_signatures == rhs.m_time_signatures && lhs.m_anchors == rhs.m_anchors;
}

} // namespace rock_hero::common::core
