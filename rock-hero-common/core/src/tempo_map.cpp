#include "tempo_map.h"

#include <algorithm>
#include <cmath>
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
{}

// Stores caller-provided map data without imposing package validation policy on the value type.
TempoMap::TempoMap(
    std::vector<TimeSignatureChange> time_signatures, std::vector<BeatAnchor> anchors)
    : m_time_signatures(std::move(time_signatures))
    , m_anchors(std::move(anchors))
{}

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

// Converts sparse measure/beat addresses into a continuous beat axis for interpolation.
std::int64_t TempoMap::globalBeatIndex(int measure, int beat) const noexcept
{
    const int target_measure = normalizedMeasure(measure);
    const int target_beat = std::max(1, beat);
    const TimeSignatureChange default_signature{};
    TimeSignatureChange active_signature =
        m_time_signatures.empty() ? default_signature : m_time_signatures.front();

    std::int64_t global_index = 0;
    int current_measure = 1;
    for (std::size_t index = 1; index < m_time_signatures.size(); ++index)
    {
        const TimeSignatureChange& next_signature = m_time_signatures[index];
        const int segment_end_measure = std::min(target_measure, next_signature.measure);
        if (segment_end_measure > current_measure)
        {
            global_index += static_cast<std::int64_t>(segment_end_measure - current_measure) *
                            std::max(1, active_signature.numerator);
            current_measure = segment_end_measure;
        }

        if (target_measure < next_signature.measure)
        {
            break;
        }
        active_signature = next_signature;
    }

    if (target_measure > current_measure)
    {
        global_index += static_cast<std::int64_t>(target_measure - current_measure) *
                        std::max(1, active_signature.numerator);
    }

    return global_index + static_cast<std::int64_t>(target_beat - 1);
}

// Walks signature segments until the remaining beat count lands inside the active segment.
std::pair<int, int> TempoMap::beatAtGlobalIndex(std::int64_t global_beat_index) const noexcept
{
    std::int64_t remaining = normalizedGlobalBeatIndex(global_beat_index);
    const TimeSignatureChange default_signature{};
    TimeSignatureChange active_signature =
        m_time_signatures.empty() ? default_signature : m_time_signatures.front();
    int measure = 1;

    for (std::size_t index = 1; index < m_time_signatures.size(); ++index)
    {
        const TimeSignatureChange& next_signature = m_time_signatures[index];
        const int segment_measures = std::max(0, next_signature.measure - measure);
        const int beats_per_measure = std::max(1, active_signature.numerator);
        const std::int64_t segment_beats =
            static_cast<std::int64_t>(segment_measures) * beats_per_measure;
        if (remaining < segment_beats)
        {
            return {
                measure + static_cast<int>(remaining / beats_per_measure),
                static_cast<int>(remaining % beats_per_measure) + 1,
            };
        }

        remaining -= segment_beats;
        measure = next_signature.measure;
        active_signature = next_signature;
    }

    const int beats_per_measure = std::max(1, active_signature.numerator);
    return {
        measure + static_cast<int>(remaining / beats_per_measure),
        static_cast<int>(remaining % beats_per_measure) + 1,
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
    if (m_anchors.empty())
    {
        return 0;
    }

    const BeatAnchor& terminal_anchor = m_anchors.back();
    return globalBeatIndex(terminal_anchor.measure, terminal_anchor.beat);
}

// Interpolates inside the nearest anchor span, clamping outside the authored map.
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

    const auto anchor_global_index = [this](const BeatAnchor& anchor) {
        return static_cast<double>(globalBeatIndex(anchor.measure, anchor.beat));
    };

    double left_index = anchor_global_index(m_anchors.front());
    double left_seconds = m_anchors.front().seconds;
    for (std::size_t index = 1; index < m_anchors.size(); ++index)
    {
        const double right_index = anchor_global_index(m_anchors[index]);
        const double right_seconds = m_anchors[index].seconds;
        if (global_beat_position <= right_index)
        {
            if (right_index <= left_index)
            {
                return right_seconds;
            }

            const double fraction =
                (global_beat_position - left_index) / (right_index - left_index);
            return left_seconds + ((right_seconds - left_seconds) * fraction);
        }

        left_index = right_index;
        left_seconds = right_seconds;
    }

    return m_anchors.back().seconds;
}

// Compares vector-owned map data while BeatAnchor owns exact floating-point equality.
bool operator==(const TempoMap& lhs, const TempoMap& rhs)
{
    return lhs.m_time_signatures == rhs.m_time_signatures && lhs.m_anchors == rhs.m_anchors;
}

} // namespace rock_hero::common::core
