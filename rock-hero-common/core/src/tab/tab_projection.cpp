#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/tab/tab_projection.h>
#include <vector>

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

// Reports whether a re-picked note sounds on the string exactly at the waypoint's grid
// position — a shift slide's target. Exact rational arithmetic, so an adjacent onset and a
// glide landing can never miss each other to rounding.
[[nodiscard]] bool waypointCoversOnset(
    const Chart& chart, const TempoMap& tempo_map, const ChartNote& note,
    const SlideWaypoint& waypoint)
{
    const GridPosition landing = advanceGridPosition(tempo_map, note.position, waypoint.offset);
    auto it =
        std::ranges::lower_bound(chart.notes, landing, std::ranges::less{}, &ChartNote::position);
    for (; it != chart.notes.end() && it->position == landing; ++it)
    {
        if (it->string == note.string)
        {
            return true;
        }
    }
    return false;
}

} // namespace

TabViewState makeTabViewState(const Arrangement& arrangement, const TempoMap& tempo_map)
{
    TabViewState state;
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
        TabNoteView view;
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
        view.vibrato = note.vibrato;
        view.tremolo = note.tremolo;
        view.accent = note.accent;
        view.bend.reserve(note.bend.size());
        for (const BendPoint& point : note.bend)
        {
            view.bend.push_back(
                TabBendPointView{
                    .seconds =
                        tempo_map.secondsAtGlobalBeatPosition(onset_beat + point.offset.toDouble()),
                    .semitones = point.semitones,
                });
        }
        view.slides.reserve(note.slides.size());
        for (const SlideWaypoint& waypoint : note.slides)
        {
            view.slides.push_back(
                TabSlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + waypoint.offset.toDouble()),
                    .fret = waypoint.fret,
                    .unpitched = waypoint.unpitched,
                    .linked = !waypoint.unpitched &&
                              !waypointCoversOnset(chart, tempo_map, note, waypoint),
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
        // arpeggio bracket. Chart notes are sorted, so the onsets at the start are contiguous.
        const auto first_at_start = std::ranges::lower_bound(
            chart.notes, shape.position, std::ranges::less{}, &ChartNote::position);
        std::size_t simultaneous = 0;
        for (auto it = first_at_start; it != chart.notes.end() && it->position == shape.position;
             ++it)
        {
            ++simultaneous;
        }

        std::string name = shape.chord < chart.templates.size() ? chart.templates[shape.chord].name
                                                                : std::string{};
        const bool arpeggio = simultaneous < 2;

        // An arpeggio bracket start marks the whole held posture: every template string, each
        // flagged by whether a chart note actually sounds there at the start. Template array
        // index 0 is the lowest string, matching the highway projection's convention.
        std::vector<TabArpeggioNoteView> arpeggio_notes;
        if (arpeggio && shape.chord < chart.templates.size())
        {
            const ChordTemplate& chord_template = chart.templates[shape.chord];
            for (std::size_t index = 0; index < chord_template.frets.size(); ++index)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const std::optional<int>& fret = chord_template.frets[index];
                if (!fret.has_value())
                {
                    continue;
                }
                const int string = static_cast<int>(index) + 1;
                bool sounded = false;
                for (auto it = first_at_start;
                     it != chart.notes.end() && it->position == shape.position;
                     ++it)
                {
                    sounded = sounded || it->string == string;
                }
                arpeggio_notes.push_back(
                    TabArpeggioNoteView{.string = string, .fret = *fret, .sounded = sounded});
            }
        }

        state.shapes.push_back(
            TabShapeView{
                .start_seconds = tempo_map.secondsAtGlobalBeatPosition(start_beat),
                .end_seconds =
                    tempo_map.secondsAtGlobalBeatPosition(start_beat + shape.sustain.toDouble()),
                .name = std::move(name),
                .arpeggio = arpeggio,
                .arpeggio_notes = std::move(arpeggio_notes),
            });
    }

    state.fret_hand_positions.reserve(chart.fret_hand_positions.size());
    for (const FretHandPosition& fhp : chart.fret_hand_positions)
    {
        state.fret_hand_positions.push_back(
            TabFhpView{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, fhp.position)),
                .fret = fhp.fret,
                .width = fhp.width,
            });
    }

    return state;
}

} // namespace rock_hero::common::core
