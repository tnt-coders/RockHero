#include "chart/chart_rules.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <tuple>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

constexpr int g_max_capo{12};
// A full octave: fine tuning stays within a semitone, but real bass arrangements charted on
// guitar strings pitch down a whole octave via -1200 cents (a common charting practice).
constexpr double g_max_cent_offset{1200.0};

[[nodiscard]] std::string positionText(const GridPosition& position)
{
    return formatGridPositionToken(position);
}

} // namespace

bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1};
}

bool chartShapeArrivesAsArpeggio(
    const Chart& chart, const ChartShape& shape, const TempoMap& tempo_map)
{
    // Chart notes are sorted, so the onsets at the span start are contiguous.
    const auto first_at_start = std::ranges::lower_bound(
        chart.notes, shape.position, std::ranges::less{}, &ChartNote::position);
    std::size_t simultaneous = 0;
    for (auto it = first_at_start; it != chart.notes.end() && it->position == shape.position; ++it)
    {
        ++simultaneous;
    }
    if (simultaneous < 2)
    {
        return true;
    }
    if (shape.chord >= chart.templates.size())
    {
        return false;
    }
    // A posture string still ringing at the start without an onset there was not re-struck —
    // the strum picks around the held note, so the span cannot be one full strum. The backward
    // scan resolves each such string to its most recent earlier note (the only one that can
    // still be ringing) and stops once every candidate string is settled.
    const ChordTemplate& chord_template = chart.templates[shape.chord];
    std::vector<bool> pending(chord_template.frets.size(), false);
    std::size_t pending_count = 0;
    for (std::size_t index = 0; index < chord_template.frets.size(); ++index)
    {
        // Bound to a local so the optional check and the access are provably the same object.
        const std::optional<int>& fret = chord_template.frets[index];
        if (!fret.has_value())
        {
            continue;
        }
        const int string = static_cast<int>(index) + 1;
        bool struck = false;
        for (auto it = first_at_start; it != chart.notes.end() && it->position == shape.position;
             ++it)
        {
            struck = struck || it->string == string;
        }
        if (!struck)
        {
            pending[index] = true;
            ++pending_count;
        }
    }
    for (auto it = first_at_start; pending_count > 0 && it != chart.notes.begin();)
    {
        --it;
        const auto index = static_cast<std::size_t>(it->string - 1);
        if (index >= pending.size() || !pending[index])
        {
            continue;
        }
        pending[index] = false;
        --pending_count;
        if (shape.position < sustainEndPosition(tempo_map, *it))
        {
            return true;
        }
    }
    return false;
}

std::expected<void, ChartError> validateChartRules(const Chart& chart, const TempoMap& tempo_map)
{
    const auto string_count = static_cast<int>(chart.tuning.strings.size());
    if (string_count < 1 || string_count > g_max_chart_strings)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidTuning,
            .message = "chart tuning must name between 1 and " +
                       std::to_string(g_max_chart_strings) + " strings",
        }};
    }
    for (const std::string& open_string : chart.tuning.strings)
    {
        if (open_string.empty())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidTuning,
                .message = "chart tuning strings must name their open pitch",
            }};
        }
    }
    if (chart.tuning.capo < 0 || chart.tuning.capo > g_max_capo ||
        std::abs(chart.tuning.cent_offset) > g_max_cent_offset)
    {
        return std::unexpected{ChartError{
            .code = ChartErrorCode::InvalidTuning,
            .message = "chart capo or cent offset is out of range",
        }};
    }

    for (std::size_t index = 0; index < chart.templates.size(); ++index)
    {
        const ChordTemplate& chord_template = chart.templates[index];
        if (chord_template.frets.size() != chart.tuning.strings.size() ||
            chord_template.fingers.size() != chart.tuning.strings.size())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidTemplate,
                .message = "chord template arrays must match the string count: template " +
                           std::to_string(index),
            }};
        }
        for (const std::optional<int>& fret : chord_template.frets)
        {
            if (fret.has_value() && (*fret < 0 || *fret > g_max_fret))
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidTemplate,
                    .message =
                        "chord template fret is out of range: template " + std::to_string(index),
                }};
            }
        }
    }

    const ChartNote* previous_note = nullptr;
    for (const ChartNote& note : chart.notes)
    {
        if (note.string < 1 || note.string > string_count || note.fret < 0 ||
            note.fret > g_max_fret || !isValidGridPosition(note.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message = "note is out of range at " + positionText(note.position),
            }};
        }
        if (note.sustain.numerator < 0)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message = "note sustain must not be negative at " + positionText(note.position),
            }};
        }
        if (note.touch.has_value() && (note.harmonic == NoteHarmonic::None || *note.touch <= 0.0 ||
                                       *note.touch > static_cast<double>(g_max_fret)))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message =
                    "harmonic touch position is out of range at " + positionText(note.position),
            }};
        }
        if (previous_note != nullptr)
        {
            const auto order_key = [](const ChartNote& entry) {
                return std::make_tuple(entry.position, entry.string);
            };
            if (order_key(*previous_note) >= order_key(note))
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::UnsortedOrDuplicateNotes,
                    .message = "notes must be sorted by position and string with unique onsets"
                               " at " +
                               positionText(note.position),
                }};
            }
        }

        Fraction previous_offset{-1, 1};
        for (const BendPoint& point : note.bend)
        {
            if (point.offset.numerator < 0 || point.offset > note.sustain ||
                point.offset <= previous_offset)
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidNotePayload,
                    .message = "bend offsets must ascend within the sustain at " +
                               positionText(note.position),
                }};
            }
            previous_offset = point.offset;
        }

        previous_offset = Fraction{0};
        for (const SlideWaypoint& waypoint : note.slides)
        {
            if (waypoint.offset <= previous_offset || waypoint.offset > note.sustain ||
                waypoint.fret < 0 || waypoint.fret > g_max_fret)
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidNotePayload,
                    .message = "slide waypoints must ascend within the sustain at " +
                               positionText(note.position),
                }};
            }
            previous_offset = waypoint.offset;
        }

        previous_note = &note;
    }

    const ChartShape* previous_shape = nullptr;
    for (const ChartShape& shape : chart.shapes)
    {
        if (shape.chord >= chart.templates.size() || shape.sustain.numerator <= 0 ||
            !isValidGridPosition(shape.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidShape,
                .message = "shape span is invalid at " + positionText(shape.position),
            }};
        }
        if (previous_shape != nullptr && shape.position < previous_shape->position)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidShape,
                .message = "shape spans must be sorted at " + positionText(shape.position),
            }};
        }
        previous_shape = &shape;
    }

    const FretHandPosition* previous_fhp = nullptr;
    for (const FretHandPosition& fhp : chart.fret_hand_positions)
    {
        if (fhp.fret < 1 || fhp.fret > g_max_fret || fhp.width < 1 ||
            !isValidGridPosition(fhp.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand position is invalid at " + positionText(fhp.position),
            }};
        }
        if (previous_fhp != nullptr && fhp.position < previous_fhp->position)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidFretHandPosition,
                .message = "fret-hand positions must be sorted at " + positionText(fhp.position),
            }};
        }
        previous_fhp = &fhp;
    }

    return std::expected<void, ChartError>{};
}

} // namespace rock_hero::common::core
