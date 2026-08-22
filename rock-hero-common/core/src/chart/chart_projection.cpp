#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Where a fret-hand placement's approach ramp begins when the placement lands exactly on a glide
// arrival, keyed by the waypoint's advanced grid position. A placement sitting on a waypoint ties
// its ramp to that glide's own segment, so a drawn hand travels with the drawn rail instead of on
// an unrelated metrical margin. UNPITCHED trail-off ends are recorded too, and carry their family
// so the hand eases with the same curve the rail uses. Chord slides record identical values under
// one key.
struct SlideRamp
{
    double start_seconds{0.0};
    bool unpitched{false};
};

} // namespace

ChartViewState makeChartViewState(const Arrangement& arrangement, const TempoMap& tempo_map)
{
    ChartViewState state;
    if (!arrangement.chart.has_value())
    {
        return state;
    }

    const Chart& chart = *arrangement.chart;
    state.string_count = static_cast<int>(chart.tuning.strings.size());
    state.capo = chart.tuning.capo;

    // Every per-note fact this projection derives comes from the one resolutions pass: the
    // PRESENTED stream it draws, each note's resolved connection motion, and each note's hold. The
    // presented form is the whole of what a surface shows — the stored ring is the actual duration
    // the string sounds, and drawing it directly would run tails through the heads that follow
    // (`docs/plans/in-progress/note-sustain-model.md`). It is derived from the saved form, so a
    // pick slide's in-memory overrides (chart.h) are already stripped and the scrape draws as the
    // scrape it is.
    const ChartResolutions resolutions = chartResolutions(chart.notes, chart.shapes, tempo_map);
    const std::vector<ChartNote>& presented_notes = resolutions.presented_notes;

    // Note onsets ascend, so the forward cursor resolves them in amortized constant time.
    // Sustain ends and intra-note payload offsets can jump past later onsets, so those use the
    // plain resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    std::map<GridPosition, SlideRamp> slide_ramp_starts;
    state.notes.reserve(presented_notes.size());
    state.display_hold_ends.reserve(presented_notes.size());
    state.actual_end_seconds.reserve(presented_notes.size());
    for (std::size_t note_index = 0; note_index < presented_notes.size(); ++note_index)
    {
        const ChartNote& note = presented_notes[note_index];
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        // A scrape renders through the unpitched machinery end to end and never feeds the
        // slide-locked ramps: it has no fret-hand anchor to ramp.
        const bool scrape = isScrape(note.attack);
        NoteViewState view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.holds[note_index].toDouble()));
        // The ACTUAL ring, read off the SAVED note the presented one above was derived from — the
        // only place in the projection that reaches past presentation, and the editor's Alt reveal
        // is its only consumer (ruling 4: what a game surface scores is the presented form).
        state.actual_end_seconds.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.saved_notes[note_index].sustain.toDouble()));
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        view.legato = resolutions.legato[note_index];
        view.palm_mute = note.palm_mute;
        view.dead = note.dead;
        view.harmonic_node = note.harmonic_node;
        view.vibrato = note.vibrato;
        view.tremolo = note.tremolo;
        view.emphasis = note.emphasis;
        view.bend.reserve(note.bend.size());
        for (const BendPoint& point : note.bend)
        {
            view.bend.push_back(
                BendPointViewState{
                    .seconds =
                        tempo_map.secondsAtGlobalBeatPosition(onset_beat + point.offset.toDouble()),
                    .semitones = point.semitones,
                });
        }
        view.slides.reserve(note.slides.size() + 1);
        double glide_segment_start_seconds = view.start_seconds;
        int glide_segment_start_fret = note.fret;
        for (const SlideWaypoint& waypoint : note.slides)
        {
            const double waypoint_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + waypoint.offset.toDouble());
            view.slides.push_back(
                SlideViewState{
                    .seconds = waypoint_seconds,
                    .fret = waypoint.fret,
                    .unpitched = scrape,
                });
            // An equal-fret waypoint is a HOLD, not a glide — nothing travels across it (the
            // pitch is pinned, which is how a slide notated on a tied continuation records where
            // it leaves from). Tying a placement's ramp to a hold's span made the hand drift the
            // whole held stretch to arrive at a fret it never left, so holds fall through to the
            // margin morph. The segment start still advances, which is what gives the following
            // glide its true, shorter span.
            if (!scrape && waypoint.fret != glide_segment_start_fret)
            {
                slide_ramp_starts.try_emplace(
                    advanceGridPosition(tempo_map, note.position, waypoint.offset),
                    SlideRamp{.start_seconds = glide_segment_start_seconds, .unpitched = false});
            }
            glide_segment_start_seconds = waypoint_seconds;
            glide_segment_start_fret = waypoint.fret;
        }
        // The unpitched slide-out flattens into the slide list; it owns its geometry. A scrape's
        // slide-out is its required terminal and flattens the same way.
        if (const SlideOut* const slide_out = slideOutOrNull(note); slide_out != nullptr)
        {
            view.slides.push_back(
                SlideViewState{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + slide_out->offset.toDouble()),
                    .fret = slide_out->fret,
                    .unpitched = true,
                });
            // The trail-off's own segment starts where the last pitched waypoint left off (the
            // note's onset when there are none), which is exactly the span the rail is drawn
            // over. Recording it ties the hand to that span and marks the family so the ease
            // matches too.
            if (!scrape)
            {
                slide_ramp_starts.try_emplace(
                    advanceGridPosition(tempo_map, note.position, slide_out->offset),
                    SlideRamp{.start_seconds = glide_segment_start_seconds, .unpitched = true});
            }
        }
        state.notes.push_back(std::move(view));
    }

    state.shapes.reserve(chart.shapes.size());
    // The shared arrival rule, answered for every span in one pass — and asked of the same
    // presented stream every per-note fact above comes from, because whether a string is still
    // ringing across a span start is a question about what sounds, not about what is stored.
    const std::vector<bool> arrivals =
        chartShapeArrivals(presented_notes, chart.shapes, chart.templates, tempo_map);
    for (std::size_t shape_index = 0; shape_index < chart.shapes.size(); ++shape_index)
    {
        const ChartShape& shape = chart.shapes[shape_index];
        const double start_beat = globalBeatPosition(tempo_map, shape.position);

        std::string name;
        std::vector<ShapeStringViewState> strings;
        if (shape.chord < chart.templates.size())
        {
            const ChordTemplate& chord_template = chart.templates[shape.chord];
            name = chord_template.name;
            // Template array index 0 is the lowest string.
            for (std::size_t index = 0; index < chord_template.frets.size(); ++index)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const std::optional<int>& fret = chord_template.frets[index];
                if (!fret.has_value())
                {
                    continue;
                }
                strings.push_back(
                    ShapeStringViewState{
                        .string = static_cast<int>(index) + 1,
                        .fret = *fret,
                        .finger = index < chord_template.fingers.size()
                                      ? chord_template.fingers[index]
                                      : std::nullopt,
                    });
            }
        }
        state.shapes.push_back(
            ShapeViewState{
                .start_seconds = tempo_map.secondsAtGlobalBeatPosition(start_beat),
                .end_seconds =
                    tempo_map.secondsAtGlobalBeatPosition(start_beat + shape.sustain.toDouble()),
                .name = std::move(name),
                // A strummed chord is a box; sequential arrival, or a posture string ringing
                // through the start un-restruck, renders as arpeggio brackets.
                .arpeggio = arrivals[shape_index],
                .strings = std::move(strings),
            });
    }

    // Every placement gets an eased approach ramp: a slide-matched placement ramps over its glide
    // segment so a drawn hand travels with the note, any other placement morphs over the shared
    // minimum-sustain-distance margin at the arrival's meter, and crowded transitions shorten
    // against the previous arrival rather than overlapping it. The synthetic pre-first nut window
    // counts as arriving at the chart origin.
    state.fret_hand_positions.reserve(chart.fret_hand_positions.size());
    for (const FretHandPosition& fhp : chart.fret_hand_positions)
    {
        const double arrival_seconds =
            tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, fhp.position));
        double ramp_start_seconds = 0.0;
        bool unpitched_ramp = false;
        if (const auto slide = slide_ramp_starts.find(fhp.position);
            slide != slide_ramp_starts.end())
        {
            ramp_start_seconds = slide->second.start_seconds;
            unpitched_ramp = slide->second.unpitched;
        }
        else
        {
            ramp_start_seconds = tempo_map.secondsAtGlobalBeatPosition(
                globalBeatPosition(tempo_map, marginBefore(tempo_map, fhp.position)));
        }
        const double previous_arrival_seconds = state.fret_hand_positions.empty()
                                                    ? tempo_map.secondsAtBeat(1, 1)
                                                    : state.fret_hand_positions.back().seconds;
        ramp_start_seconds = std::clamp(
            ramp_start_seconds,
            std::min(previous_arrival_seconds, arrival_seconds),
            arrival_seconds);
        state.fret_hand_positions.push_back(
            FhpViewState{
                .seconds = arrival_seconds,
                .fret = fhp.fret,
                .width = fhp.width,
                .ramp_seconds = arrival_seconds - ramp_start_seconds,
                .unpitched_ramp = unpitched_ramp,
            });
    }

    return state;
}

} // namespace rock_hero::common::core
