#include "chart/chart_rules.h"

#include <cmath>
#include <cstddef>
#include <rock_hero/common/core/chart/chart_tokens.h>
#include <tuple>

namespace rock_hero::common::core
{

namespace
{

// Frets above the low twenties do not exist on real instruments; leave headroom for extended
// range hardware without accepting junk data.
constexpr int g_max_fret{30};
constexpr int g_max_capo{12};
// A full octave: fine tuning stays within a semitone, but real bass arrangements charted on
// guitar strings pitch down a whole octave via -1200 cents (a common charting practice).
constexpr double g_max_cent_offset{1200.0};

[[nodiscard]] bool isValidGridPosition(const GridPosition& position, const TempoMap& tempo_map)
{
    return position.measure >= 1 && position.beat >= 1 &&
           position.beat <= tempo_map.beatsPerMeasureAt(position.measure) &&
           position.offset.numerator >= 0 && position.offset < Fraction{1};
}

[[nodiscard]] std::string positionText(const GridPosition& position)
{
    return formatGridPositionToken(position);
}

} // namespace

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

    const ChartSection* previous_section = nullptr;
    for (const ChartSection& section : chart.sections)
    {
        if (section.type.empty() || !isValidGridPosition(section.position, tempo_map))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidSection,
                .message = "section is invalid at " + positionText(section.position),
            }};
        }
        if (previous_section != nullptr && section.position < previous_section->position)
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidSection,
                .message = "sections must be sorted at " + positionText(section.position),
            }};
        }
        previous_section = &section;
    }

    return std::expected<void, ChartError>{};
}

} // namespace rock_hero::common::core
