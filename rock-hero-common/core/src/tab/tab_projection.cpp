#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
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

    // Every per-note fact this projection derives comes from the one resolutions pass: the SAVED
    // stream it draws (a pick slide overrides its other techniques in memory, per chart.h, and the
    // lane must show the scrape without them — restating that list is how the lane and the document
    // drift apart), each note's resolved connection motion, and the effective holds the span
    // convention implies.
    const ChartResolutions resolutions = chartResolutions(chart.notes, chart.shapes, tempo_map);
    const std::vector<ChartNote>& saved_notes = resolutions.saved_notes;

    // Note onsets ascend, so the forward cursor resolves them in amortized constant time.
    // Sustain ends and intra-note payload offsets can jump past later onsets, so those use the
    // plain resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    state.notes.reserve(saved_notes.size());
    state.display_hold_ends.reserve(saved_notes.size());
    for (std::size_t note_index = 0; note_index < saved_notes.size(); ++note_index)
    {
        const ChartNote& note = saved_notes[note_index];
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        const bool scrape = note.attack == NoteAttack::PickSlide;
        TabNoteView view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        // The span-implied hold, resolved from the same authority the board resolves it from, so
        // one chart draws the same tails on both surfaces.
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.effective_sustains[note_index].toDouble()));
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        view.legato = resolutions.legato[note_index];
        view.mute = note.mute;
        view.harmonic_node = note.harmonic_node;
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
        // A waypoint strictly inside the sustain is a legato junction or hold — the same note
        // continues, so it draws the linked continuation head, in the note's own head shape.
        // A waypoint at exactly the sustain end is a shift-slide glide-end: the note stops
        // there and the re-picked landing renders its own head, so no linked glyph. Being
        // unpitched does not unlink a waypoint: a scrape's turnaround is still one gesture
        // continuing, and its head is what keeps the corner from reading as a break.
        view.slides.reserve(note.slides.size() + 1);
        for (const SlideWaypoint& waypoint : note.slides)
        {
            view.slides.push_back(
                TabSlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + waypoint.offset.toDouble()),
                    .fret = waypoint.fret,
                    .unpitched = scrape,
                    .linked = waypoint.offset < note.sustain,
                });
        }
        // The unpitched slide-out flattens into the view's slide list; it owns its geometry.
        // A scrape's slide-out is its required terminal and flattens the same way.
        if (const SlideOut* const slide_out = slideOutOrNull(note); slide_out != nullptr)
        {
            view.slides.push_back(
                TabSlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + slide_out->offset.toDouble()),
                    .fret = slide_out->fret,
                    .unpitched = true,
                    .linked = false,
                });
        }
        state.notes.push_back(std::move(view));
    }

    state.shapes.reserve(chart.shapes.size());
    // The shared arrival rule, answered for every span in one pass.
    const std::vector<bool> arrivals = chartShapeArrivals(chart, tempo_map);
    for (std::size_t shape_index = 0; shape_index < chart.shapes.size(); ++shape_index)
    {
        const ChartShape& shape = chart.shapes[shape_index];
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // Chart notes are sorted, so the onsets at the span start are contiguous (used for the
        // per-string sounded flags below).
        const auto first_at_start = std::ranges::lower_bound(
            chart.notes, shape.position, std::ranges::less{}, &ChartNote::position);

        std::string name = shape.chord < chart.templates.size() ? chart.templates[shape.chord].name
                                                                : std::string{};
        // A strummed chord is a box; sequential arrival, or a posture string ringing through the
        // start un-restruck, renders as arpeggio brackets.
        const bool arpeggio = arrivals[shape_index];

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
