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
    // A held chord played under a right-hand onset reads as a held arpeggio, not a strummed
    // box: the fretting hand holds the shape while the other hand sounds above it — taps and
    // pick slides alike. Any such note sounding within the span flips the box.
    const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);
    for (const ChartNote& note : chart.notes)
    {
        if (rightHandOnset(note.attack) && !(note.position < shape.position) &&
            note.position < span_end)
        {
            return true;
        }
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
        if (note.harmonic_node.has_value() &&
            (*note.harmonic_node <= 0.0 || *note.harmonic_node > g_max_harmonic_node))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message =
                    "harmonic node position is out of range at " + positionText(note.position),
            }};
        }
        // A node lies on the speaking length, so it cannot sit at or behind the stop — nothing
        // vibrates there. Universal because a natural harmonic's `fret` is its actual stop (the
        // nut, or the capo), never a rounded copy of its own node.
        if (note.harmonic_node.has_value() && *note.harmonic_node <= static_cast<double>(note.fret))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message =
                    "harmonic node must lie beyond the stop at " + positionText(note.position),
            }};
        }
        // A pinch is picking while damping a node, so a pinch without one is missing data rather
        // than a different technique — the overtone that squeals is *determined* by where the thumb
        // lands. Enforcing it is what lets node presence alone assert the harmonic.
        if (note.attack == NoteAttack::Pinch && !note.harmonic_node.has_value())
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNote,
                .message = "pinch harmonic must carry its node at " + positionText(note.position),
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
            // A curve waypoint may never sit on a later onset of its own string: a glide into a
            // real note is the slideEnd "next" terminal, which stores no coordinates. Rejecting
            // the coordinate copy here is what keeps the desyncable encoding unrepresentable.
            // The one exception is a scrape's TERMINAL waypoint: it is unpitched gesture
            // geometry pinned to the sustain end (never a linked-head landing), and 40-Q2-B
            // truncation legally parks the sustain — and therefore the terminal — exactly on
            // the silencing next onset. Scrape interiors stay bound by the rule.
            if (note.attack == NoteAttack::PickSlide && waypoint.offset == note.sustain)
            {
                previous_offset = waypoint.offset;
                continue;
            }
            const GridPosition waypoint_position =
                advanceGridPosition(tempo_map, note.position, waypoint.offset);
            for (auto at_waypoint = std::ranges::lower_bound(
                     chart.notes, waypoint_position, std::ranges::less{}, &ChartNote::position);
                 at_waypoint != chart.notes.end() && at_waypoint->position == waypoint_position;
                 ++at_waypoint)
            {
                if (at_waypoint->string == note.string)
                {
                    return std::unexpected{ChartError{
                        .code = ChartErrorCode::InvalidNotePayload,
                        .message = "slide waypoint may not sit on a later onset of its string at " +
                                   positionText(note.position) +
                                   "; a glide ends before its re-picked landing",
                    }};
                }
            }
            previous_offset = waypoint.offset;
        }

        // A slide-out owns its geometry and must stay ordered like any payload.
        const SlideOut* const slide_out = slideOutOrNull(note);
        if (slide_out != nullptr &&
            (slide_out->offset <= previous_offset || slide_out->offset > note.sustain ||
             slide_out->fret < 0 || slide_out->fret > g_max_fret))
        {
            return std::unexpected{ChartError{
                .code = ChartErrorCode::InvalidNotePayload,
                .message = "slide-out must end after every waypoint, within the sustain at " +
                           positionText(note.position),
            }};
        }

        // A SAVED pick-slide note carries no other technique — the document writer omits them
        // (the in-memory override design, chart.h) — so a document that does is hand-made or a
        // bug and fails loudly. The path is the gesture: required, always traveling
        // (consecutive neck positions strictly differ, the start fret included — a scrape
        // cannot sit still, unlike note slides, whose equal-fret segments are holds), and
        // ending exactly at the sustain, because nothing rings past a scrape.
        if (note.attack == NoteAttack::PickSlide)
        {
            if (note.mute != NoteMute::None || note.harmonic_node.has_value() || note.vibrato ||
                note.tremolo || note.accent || !note.bend.empty() || slide_out != nullptr)
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidPickSlide,
                    .message = "pick-slide note must not carry other techniques at " +
                               positionText(note.position),
                }};
            }
            if (note.slides.empty() || !(note.slides.back().offset == note.sustain))
            {
                return std::unexpected{ChartError{
                    .code = ChartErrorCode::InvalidPickSlide,
                    .message = "pick-slide path must end exactly at the sustain at " +
                               positionText(note.position),
                }};
            }
            int previous_fret = note.fret;
            for (const SlideWaypoint& waypoint : note.slides)
            {
                if (waypoint.fret == previous_fret)
                {
                    return std::unexpected{ChartError{
                        .code = ChartErrorCode::InvalidPickSlide,
                        .message =
                            "pick-slide path must keep traveling at " + positionText(note.position),
                    }};
                }
                previous_fret = waypoint.fret;
            }
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
