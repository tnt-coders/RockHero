#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <rock_hero/common/core/shared/displayed_strings.h>
#include <utility>

namespace rock_hero::common::core
{

namespace
{

// Measures per derived camera framing zone for measures that contain notes. A standard
// automatic phrase generator uses 2-4 for its phrase creation; 4 keeps the camera's framing
// target at rest the longest but reads too static, so 2 gives the tighter, livelier frame.
constexpr int g_camera_zone_measures = 2;

} // namespace

HighwayViewState makeHighwayViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map,
    const std::vector<SongSection>& sections, HighwayDisplayOptions options)
{
    HighwayViewState state;
    state.options = options;

    // Sections are song-level structure, so they resolve even when the arrangement has no chart.
    state.sections.reserve(sections.size());
    for (const SongSection& section : sections)
    {
        // Upper-cased here, once per projection, because the board draws every section name that
        // way and doing it in the renderer meant a fresh allocation and transform per visible
        // section per frame for a value that only changes when the chart does.
        std::string name = asciiUppered(section.name);
        state.sections.push_back(
            HighwaySectionView{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, section.position)),
                .name = std::move(name),
            });
    }

    if (!arrangement.chart.has_value())
    {
        return state;
    }

    const Chart& chart = *arrangement.chart;
    // Display padding (editor "show at least N strings"): the chart's strings occupy the top of a
    // larger displayed lane range, so every note/posture string index shifts up by the padding
    // amount, keeping the shared string-color palette anchored exactly as the 2D tab anchors it.
    const int chart_string_count = static_cast<int>(chart.tuning.strings.size());
    state.string_count = displayedStringCount(chart_string_count, options.minimum_string_count);
    state.capo = chart.tuning.capo;
    const int displayed_lane_shift = state.string_count - chart_string_count;

    // Note onsets ascend, so the forward cursor resolves them in amortized constant time.
    // Sustain ends and intra-note payload offsets can jump past later onsets, so those use the
    // plain resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    // Per-note tap light-rise durations, resolved through the same margin-morph rule the
    // fret-hand ramps use below; zero for non-tap notes. Feeds makeHighwayTapOnsets.
    std::vector<double> tap_rise_seconds;
    tap_rise_seconds.reserve(chart.notes.size());
    // Glide arrivals feed the hand window's slide-locked ramps: a placement sitting exactly on a
    // waypoint's advanced grid position ties its ramp to that glide's own segment, so the window
    // travels with the drawn rail instead of on an unrelated metrical margin. UNPITCHED trail-off
    // ends are recorded too, and carry their family so the window eases with the same curve the
    // rail uses — a trail-off's curve is defined, so the window follows it precisely rather than
    // approximating it with a margin morph and the pitched curve. Chord slides record identical
    // values under one key.
    struct SlideRamp
    {
        double start_seconds{0.0};
        bool unpitched{false};
    };
    std::map<GridPosition, SlideRamp> slide_ramp_starts;
    // The projection reads the SAVED stream throughout. A pick slide overrides its other
    // techniques in memory (chart.h) and the display must show the scrape without them — which is
    // exactly the SAVED form, so the projection takes savedChartNote rather than restating which
    // fields a scrape suppresses; stating that list twice is how the board and the document drift
    // apart. Projected ONCE, up front, because chartEffectiveSustains states saved form as its
    // precondition too: fed the in-memory stream, a latent full mute chokes an onset group the
    // saved chart holds. What stays local is how a scrape RENDERS: through the unpitched
    // machinery (dimmed glide, no waypoint furniture, no hand-window contribution) and never
    // feeding the slide-locked ramps.
    std::vector<ChartNote> saved_notes;
    saved_notes.reserve(chart.notes.size());
    for (const ChartNote& in_memory_note : chart.notes)
    {
        saved_notes.push_back(savedChartNote(in_memory_note));
    }

    state.notes.reserve(saved_notes.size());
    for (const ChartNote& note : saved_notes)
    {
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        const bool scrape = note.attack == NoteAttack::PickSlide;
        HighwayNoteView view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        view.string = note.string + displayed_lane_shift;
        view.fret = note.fret;
        view.attack = note.attack;
        view.mute = note.mute;
        view.harmonic_node = note.harmonic_node;
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
        view.slides.reserve(note.slides.size() + 1);
        double glide_segment_start_seconds = view.start_seconds;
        int glide_segment_start_fret = note.fret;
        for (const SlideWaypoint& waypoint : note.slides)
        {
            const double waypoint_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + waypoint.offset.toDouble());
            view.slides.push_back(
                HighwaySlideView{
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
        // The slide-out flattens into the view's slide list so the renderer keeps one uniform
        // segment model; it owns its geometry and dims unpitched. Pitched glides — shift and
        // legato alike — are already ordinary waypoints above. A scrape's slide-out is its
        // required terminal and flattens the same way, but never feeds the placement ramps —
        // the scrape has no fret-hand anchor to ramp.
        if (const SlideOut* const slide_out = slideOutOrNull(note); slide_out != nullptr)
        {
            view.slides.push_back(
                HighwaySlideView{
                    .seconds = tempo_map.secondsAtGlobalBeatPosition(
                        onset_beat + slide_out->offset.toDouble()),
                    .fret = slide_out->fret,
                    .unpitched = true,
                });
            // The trail-off's own segment starts where the last pitched waypoint left off (the
            // note's onset when there are none), which is exactly the span the renderer draws it
            // over. Recording it ties the window to that span and marks the family so the ease
            // matches too.
            if (!scrape)
            {
                slide_ramp_starts.try_emplace(
                    advanceGridPosition(tempo_map, note.position, slide_out->offset),
                    SlideRamp{.start_seconds = glide_segment_start_seconds, .unpitched = true});
            }
        }
        double rise_seconds = 0.0;
        if (rightHandOnset(note.attack))
        {
            // The right-hand light's rise uses the fret-hand placements' own arrival rule:
            // the minimum-sustain-distance margin at the onset's meter.
            const TimeSignatureChange signature = tempo_map.timeSignatureAt(note.position.measure);
            const Fraction margin = minimumSustainDistanceBeats(signature.denominator);
            const GridPosition rise_start = advanceGridPosition(
                tempo_map, note.position, Fraction{-margin.numerator, margin.denominator});
            rise_seconds = view.start_seconds - tempo_map.secondsAtGlobalBeatPosition(
                                                    globalBeatPosition(tempo_map, rise_start));
        }
        tap_rise_seconds.push_back(rise_seconds);
        state.notes.push_back(std::move(view));
    }
    // Tapping-hand onsets derive purely from the resolved notes (right-hand tap lighting).
    state.tap_onsets = makeHighwayTapOnsets(state.notes, tap_rise_seconds);

    // Display hold ends, resolved from the one effective-sustain authority rather than recomputed
    // in seconds. The loop above pushes exactly one view per chart note, so the indices line up.
    const std::vector<Fraction> effective_sustains =
        chartEffectiveSustains(saved_notes, chart.shapes, tempo_map);
    state.display_hold_ends.reserve(saved_notes.size());
    for (std::size_t index = 0; index < saved_notes.size(); ++index)
    {
        const double onset_beat = globalBeatPosition(tempo_map, saved_notes[index].position);
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + effective_sustains[index].toDouble()));
    }

    state.shapes.reserve(chart.shapes.size());
    // The shared arrival rule, answered for every span in one pass.
    const std::vector<bool> arrivals = chartShapeArrivals(chart, tempo_map);
    std::size_t shape_index = 0;
    for (const ChartShape& shape : chart.shapes)
    {
        const double start_beat = globalBeatPosition(tempo_map, shape.position);

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
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const std::optional<int>& fret = chord_template.frets[index];
                if (!fret.has_value())
                {
                    continue;
                }
                strings.push_back(
                    HighwayShapeStringView{
                        .string = static_cast<int>(index) + 1 + displayed_lane_shift,
                        .fret = *fret,
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
                // A strummed chord is a box; sequential arrival, or a posture string ringing
                // through the start un-restruck, renders arpeggio-style.
                .arpeggio = arrivals[shape_index],
                .strings = std::move(strings),
            });
        ++shape_index;
    }

    // Every placement gets an eased approach ramp (fhp-window-motion plan): a slide-matched
    // placement ramps over its glide segment so the window travels with the note, any other
    // placement morphs over the shared minimum-sustain-distance margin at the arrival's meter,
    // and crowded transitions shorten against the previous arrival rather than overlapping it.
    // The synthetic pre-first nut window counts as arriving at the chart origin.
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
            const TimeSignatureChange signature = tempo_map.timeSignatureAt(fhp.position.measure);
            const Fraction margin = minimumSustainDistanceBeats(signature.denominator);
            const GridPosition morph_start = advanceGridPosition(
                tempo_map, fhp.position, Fraction{-margin.numerator, margin.denominator});
            ramp_start_seconds =
                tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, morph_start));
        }
        const double previous_arrival_seconds = state.fret_hand_positions.empty()
                                                    ? tempo_map.secondsAtBeat(1, 1)
                                                    : state.fret_hand_positions.back().seconds;
        ramp_start_seconds = std::clamp(
            ramp_start_seconds,
            std::min(previous_arrival_seconds, arrival_seconds),
            arrival_seconds);
        state.fret_hand_positions.push_back(
            HighwayFhpView{
                .seconds = arrival_seconds,
                .fret = fhp.fret,
                .width = fhp.width,
                .ramp_seconds = arrival_seconds - ramp_start_seconds,
                .unpitched_ramp = unpitched_ramp,
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

    // Camera framing zones: the camera's framing window is quantized to these derived boundaries
    // so its target steps only here and rests in between — the step-then-rest cadence that defines
    // the intended camera feel. The derivation mirrors a standard automatic phrase generator: runs
    // of measures containing note onsets split into g_camera_zone_measures-sized groups aligned to
    // downbeats, a run of empty measures collapses into one zone however long (rests are the
    // camera's travel time, not framing churn), and a section start forces a new zone.
    std::vector<double> measure_starts;
    for (const HighwayBeatView& beat : state.beats)
    {
        if (beat.measure_downbeat)
        {
            measure_starts.push_back(beat.seconds);
        }
    }
    std::size_t note_cursor = 0;
    std::size_t section_cursor = 0;
    int measures_in_zone = 0;
    bool run_empty = false;
    for (std::size_t measure = 0; measure < measure_starts.size(); ++measure)
    {
        const double measure_start = measure_starts[measure];
        const double measure_end = measure + 1 < measure_starts.size()
                                       ? measure_starts[measure + 1]
                                       : std::numeric_limits<double>::infinity();
        while (note_cursor < state.notes.size() &&
               state.notes[note_cursor].start_seconds < measure_start)
        {
            ++note_cursor;
        }
        const bool empty = note_cursor >= state.notes.size() ||
                           state.notes[note_cursor].start_seconds >= measure_end;
        // A section starting since the previous downbeat (mid-measure starts snap forward to
        // this one) restarts the grouping.
        bool section_cut = false;
        while (section_cursor < state.sections.size() &&
               state.sections[section_cursor].seconds <=
                   measure_start + g_highway_onset_match_epsilon)
        {
            section_cut = true;
            ++section_cursor;
        }
        if (measure == 0 || section_cut || empty != run_empty ||
            (!empty && measures_in_zone >= g_camera_zone_measures))
        {
            state.camera_zone_starts.push_back(measure_start);
            measures_in_zone = 0;
        }
        run_empty = empty;
        ++measures_in_zone;
    }

    return state;
}

} // namespace rock_hero::common::core
