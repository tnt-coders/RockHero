#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/tab/tab_projection.h>
#include <vector>

namespace rock_hero::common::core
{

TabViewState makeTabViewState(const Arrangement& arrangement, const TempoMap& tempo_map)
{
    TabViewState state;
    if (!arrangement.chart.has_value())
    {
        return state;
    }

    const Chart& chart = *arrangement.chart;
    state.string_count = static_cast<int>(chart.tuning.strings.size());
    state.capo = chart.tuning.capo;

    // Note onsets ascend, so the forward cursor resolves them in amortized constant time.
    // Sustain ends and intra-note payload offsets can jump past later onsets, so those use the
    // plain resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    state.notes.reserve(chart.notes.size());
    for (const ChartNote& note : chart.notes)
    {
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        // A pick slide overrides its other techniques in memory (chart.h): the projection is
        // the one seam that suppresses the latents, and its path renders unpitched with no
        // linked continuation heads — the chips at each leg carry the traveled positions.
        const bool scrape = note.attack == NoteAttack::PickSlide;
        TabNoteView view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        view.mute = scrape ? NoteMute::None : note.mute;
        view.harmonic_node = scrape ? std::optional<double>{} : note.harmonic_node;
        view.vibrato = !scrape && note.vibrato;
        view.tremolo = !scrape && note.tremolo;
        view.accent = note.accent;
        if (!scrape)
        {
            view.bend.reserve(note.bend.size());
            for (const BendPoint& point : note.bend)
            {
                view.bend.push_back(
                    TabBendPointView{
                        .seconds = tempo_map.secondsAtGlobalBeatPosition(
                            onset_beat + point.offset.toDouble()),
                        .semitones = point.semitones,
                    });
            }
        }
        // A waypoint strictly inside the sustain is a legato junction or hold — the same note
        // continues, so it draws the linked continuation head. A waypoint at exactly the
        // sustain end is a shift-slide glide-end: the note stops there and the re-picked
        // landing renders its own head, so no linked glyph.
        view.slides.reserve(note.slides.size() + 1);
        for (const SlideWaypoint& waypoint : note.slides)
        {
            view.slides.push_back(
                TabSlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + waypoint.offset.toDouble()),
                    .fret = waypoint.fret,
                    .unpitched = scrape,
                    .linked = !scrape && waypoint.offset < note.sustain,
                });
        }
        // The unpitched slide-out flattens into the view's slide list; it owns its geometry.
        // A scrape's slide-out is its required terminal and flattens the same way.
        if (note.slide_out.has_value())
        {
            view.slides.push_back(
                TabSlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + note.slide_out->offset.toDouble()),
                    .fret = note.slide_out->fret,
                    .unpitched = true,
                    .linked = false,
                });
        }
        state.notes.push_back(std::move(view));
    }

    state.shapes.reserve(chart.shapes.size());
    for (const ChartShape& shape : chart.shapes)
    {
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // Chart notes are sorted, so the onsets at the span start are contiguous (used for the
        // per-string sounded flags below).
        const auto first_at_start = std::ranges::lower_bound(
            chart.notes, shape.position, std::ranges::less{}, &ChartNote::position);

        std::string name = shape.chord < chart.templates.size() ? chart.templates[shape.chord].name
                                                                : std::string{};
        // The shared arrival rule: a strummed chord is a box; sequential arrival, or a posture
        // string ringing through the start un-restruck, renders as arpeggio brackets.
        const bool arpeggio = chartShapeArrivesAsArpeggio(chart, shape, tempo_map);

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
