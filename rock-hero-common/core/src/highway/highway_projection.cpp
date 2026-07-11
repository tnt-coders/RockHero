#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Converts a chart grid position onto the tempo map's fractional global beat axis.
[[nodiscard]] double globalBeatPosition(const TempoMap& tempo_map, const GridPosition& position)
{
    return static_cast<double>(tempo_map.globalBeatIndex(position.measure, position.beat)) +
           position.offset.toDouble();
}

} // namespace

HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map, HighwayDisplayOptions options)
{
    HighwayViewState state;
    state.options = options;
    if (!arrangement.chart.has_value())
    {
        return state;
    }

    const Chart& chart = *arrangement.chart;
    state.string_count = static_cast<int>(chart.tuning.strings.size());

    // Note onsets ascend, so the forward cursor resolves them in amortized constant time.
    // Sustain ends and intra-note payload offsets can jump past later onsets, so those use the
    // plain resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    state.notes.reserve(chart.notes.size());
    for (const ChartNote& note : chart.notes)
    {
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        HighwayNoteView view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        view.mute = note.mute;
        view.harmonic = note.harmonic;
        view.touch = note.touch;
        view.vibrato = note.vibrato;
        view.tremolo = note.tremolo;
        view.accent = note.accent;
        view.bend.reserve(note.bend.size());
        for (const BendPoint& point : note.bend)
        {
            view.bend.push_back(
                HighwayBendPointView{
                    .seconds =
                        tempo_map.secondsAtGlobalBeatPosition(onset_beat + point.offset.toDouble()),
                    .semitones = point.semitones,
                });
        }
        view.slides.reserve(note.slides.size());
        for (const SlideWaypoint& waypoint : note.slides)
        {
            view.slides.push_back(
                HighwaySlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + waypoint.offset.toDouble()),
                    .fret = waypoint.fret,
                    .unpitched = waypoint.unpitched,
                });
        }
        state.notes.push_back(std::move(view));
    }

    state.shapes.reserve(chart.shapes.size());
    for (const ChartShape& shape : chart.shapes)
    {
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // A span whose start carries two or more simultaneous onsets reads as a strummed chord
        // box; a single onset at the start means the shape's notes arrive sequentially — an
        // arpeggio treatment. Chart notes are sorted, so the onsets at the start are contiguous.
        const auto first_at_start = std::ranges::lower_bound(
            chart.notes, shape.position, std::ranges::less{}, &ChartNote::position);
        std::size_t simultaneous = 0;
        for (auto it = first_at_start; it != chart.notes.end() && it->position == shape.position;
             ++it)
        {
            ++simultaneous;
        }

        std::string name;
        std::vector<HighwayShapeStringView> strings;
        if (shape.chord < chart.templates.size())
        {
            const ChordTemplate& chord_template = chart.templates[shape.chord];
            name = chord_template.name;
            // Posture entries carry the template's per-string frets and fingerings for the
            // fingering panel and the arpeggio brackets; array index 0 is the lowest string.
            for (std::size_t index = 0; index < chord_template.frets.size(); ++index)
            {
                if (!chord_template.frets[index].has_value())
                {
                    continue;
                }
                strings.push_back(
                    HighwayShapeStringView{
                        .string = static_cast<int>(index) + 1,
                        .fret = *chord_template.frets[index],
                        .finger = index < chord_template.fingers.size()
                                      ? chord_template.fingers[index]
                                      : std::nullopt,
                    });
            }
        }
        state.shapes.push_back(
            HighwayShapeView{
                .start_seconds = tempo_map.secondsAtGlobalBeatPosition(start_beat),
                .end_seconds =
                    tempo_map.secondsAtGlobalBeatPosition(start_beat + shape.sustain.toDouble()),
                .name = std::move(name),
                .arpeggio = simultaneous < 2,
                .strings = std::move(strings),
            });
    }

    state.fret_hand_positions.reserve(chart.fret_hand_positions.size());
    for (const FretHandPosition& fhp : chart.fret_hand_positions)
    {
        state.fret_hand_positions.push_back(
            HighwayFhpView{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, fhp.position)),
                .fret = fhp.fret,
                .width = fhp.width,
            });
    }

    // Every beat of the song grid, resolved once so beat bars never touch the tempo map per
    // frame. Beat indices ascend, so a second forward cursor keeps this one pass over the
    // anchors regardless of song length.
    TempoMap::ForwardBeatTimeCursor beat_cursor{tempo_map};
    const std::int64_t terminal_beat = tempo_map.terminalGlobalBeatIndex();
    state.beats.reserve(static_cast<std::size_t>(terminal_beat) + 1);
    for (std::int64_t index = 0; index <= terminal_beat; ++index)
    {
        state.beats.push_back(
            HighwayBeatView{
                .seconds = beat_cursor.secondsAt(static_cast<double>(index)),
                .measure_downbeat = tempo_map.beatAtGlobalIndex(index).second == 1,
            });
    }

    state.sections.reserve(chart.sections.size());
    for (const ChartSection& section : chart.sections)
    {
        state.sections.push_back(
            HighwaySectionView{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, section.position)),
                .type = section.type,
            });
    }

    return state;
}

} // namespace rock_hero::common::core
