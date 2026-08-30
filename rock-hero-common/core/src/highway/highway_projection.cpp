#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <string>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Measures per derived camera framing zone for measures that contain notes. A standard
// automatic phrase generator uses 2-4 for its phrase creation; 4 keeps the camera's framing
// target at rest the longest but reads too static, so 2 gives the tighter, livelier frame.
constexpr int g_camera_zone_measures = 2;

// How long the fretting hand must say nothing before its light goes out, in measures at the
// local meter. The one threshold the rest derivation reads; one measure is the starting value,
// tuned at sighting.
constexpr int g_backlight_rest_measures = 1;

// SIGHTING SWITCH — whether a picking-hand TAP keeps the fretting hand's light lit. True is the
// conservative side and the shipped default: a tap says nothing about the fretting hand, but
// fading a board's light through a tap run has never been looked at. Flip to false to sight it.
// Its companion in this batch is the harmonic node light's colour candidate,
// g_harmonic_light_candidate in highway_renderer.cpp — one constant per layer because a colour
// is a UI fact and this one is a musical one.
constexpr bool g_backlight_taps_keep_light = true;

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
            HighwaySectionViewState{
                .seconds = tempo_map.secondsAtGlobalBeatPosition(
                    globalBeatPosition(tempo_map, section.position)),
                .name = std::move(name),
            });
    }

    // The chart scene is the one shared projection; everything below derives board-only
    // structure from it.
    state.chart = makeChartViewState(arrangement, tempo_map);
    if (!arrangement.chart.has_value())
    {
        return state;
    }
    const Chart& chart = *arrangement.chart;
    const std::vector<NoteViewState>& notes = state.chart.notes;

    // Per-note tap light-rise durations: the right-hand light rises over the fret-hand
    // placements' own arrival margin (\ref marginBefore) at the onset's meter; zero for
    // fretting-hand notes. The scene's notes pair one-to-one with the chart's, and a note's
    // position survives the saved-form transform, so the grid position comes straight from the
    // chart. Feeds makeHighwayTapOnsets.
    std::vector<double> tap_rise_seconds;
    tap_rise_seconds.reserve(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        double rise_seconds = 0.0;
        if (rightHandOnset(notes[index].attack))
        {
            rise_seconds = notes[index].start_seconds -
                           tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(
                               tempo_map, marginBefore(tempo_map, chart.notes[index].position)));
        }
        tap_rise_seconds.push_back(rise_seconds);
    }
    // Tapping-hand onsets derive purely from the resolved notes (right-hand tap lighting).
    state.tap_onsets = makeHighwayTapOnsets(notes, tap_rise_seconds);

    // Onset groups and their repeat classification, derived here once per chart revision. The
    // rules look backward through the whole note stream, so the renderer's visible window could
    // neither afford them per frame nor even see everything they depend on.
    HighwayChordGrouping grouping = makeHighwayChordGroups(notes, state.chart.shapes);
    state.chord_groups = std::move(grouping.groups);
    state.note_group = std::move(grouping.note_group);

    // Every beat of the song grid, resolved once so beat bars never touch the tempo map per
    // frame. Beat indices ascend, so a forward cursor keeps this one pass over the anchors
    // regardless of song length.
    TempoMap::ForwardBeatTimeCursor beat_cursor{tempo_map};
    const std::int64_t terminal_beat = tempo_map.terminalGlobalBeatIndex();
    state.beats.reserve(static_cast<std::size_t>(terminal_beat) + 1);
    for (std::int64_t index = 0; index <= terminal_beat; ++index)
    {
        const auto [measure, beat_in_measure] = tempo_map.beatAtGlobalIndex(index);
        state.beats.push_back(
            HighwayBeatViewState{
                .seconds = beat_cursor.secondsAt(static_cast<double>(index)),
                .measure_downbeat = beat_in_measure == 1,
            });
    }

    // Camera framing zones: the camera's framing window is quantized to these derived boundaries
    // so its target steps only here and rests in between — the step-then-rest cadence that defines
    // the intended camera feel. The derivation mirrors a standard automatic phrase generator: runs
    // of measures containing note onsets split into g_camera_zone_measures-sized groups aligned to
    // downbeats, a run of empty measures collapses into one zone however long (rests are the
    // camera's travel time, not framing churn), and a section start forces a new zone.
    std::vector<double> measure_starts;
    for (const HighwayBeatViewState& beat : state.beats)
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
        while (note_cursor < notes.size() && notes[note_cursor].start_seconds < measure_start)
        {
            ++note_cursor;
        }
        const bool empty =
            note_cursor >= notes.size() || notes[note_cursor].start_seconds >= measure_end;
        // A section starting since the previous downbeat (mid-measure starts snap forward to
        // this one) restarts the grouping.
        bool section_cut = false;
        while (section_cursor < state.sections.size() &&
               state.sections[section_cursor].seconds <= measure_start + g_onset_match_epsilon)
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

    // Backlight rests: the stretches where the fretting hand states nothing for long enough that
    // its light has nothing to say. Derived here because both quantities a rest carries are the
    // tempo map's — the local measure that sets the threshold, and marginBefore, the ONE arrival
    // lead the hand's own morph and the picking hand's light rise already share.
    //
    // A hand-posture span can only ever EXTEND a lit stretch, never open one: its members are
    // notes, and a note's onset lights the board at or before the span it belongs to starts. That
    // is what makes the returning statement a rest leads always a NOTE, whose grid position is in
    // hand here — so folding spans in as ends alone is not a shortcut, it is the whole of what
    // they can contribute.
    const auto margin_seconds_at = [&tempo_map](const GridPosition position) {
        return tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, position)) -
               tempo_map.secondsAtGlobalBeatPosition(
                   globalBeatPosition(tempo_map, marginBefore(tempo_map, position)));
    };
    const auto measure_seconds_at = [&tempo_map](const int measure) {
        const std::int64_t first_beat = tempo_map.globalBeatIndex(measure, 1);
        const std::int64_t next_measure = first_beat + tempo_map.beatsPerMeasureAt(measure);
        return tempo_map.secondsAtGlobalBeatPosition(static_cast<double>(next_measure)) -
               tempo_map.secondsAtGlobalBeatPosition(static_cast<double>(first_beat));
    };
    double lit_until = 0.0;
    bool any_information = false;
    GridPosition last_lit_position{};
    std::size_t next_shape = 0;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        if (!highwayBacklightKeepsLit(notes[index].attack, g_backlight_taps_keep_light))
        {
            continue;
        }
        while (next_shape < state.chart.shapes.size() &&
               state.chart.shapes[next_shape].start_seconds <= notes[index].start_seconds)
        {
            lit_until = std::max(lit_until, state.chart.shapes[next_shape].end_seconds);
            ++next_shape;
        }
        const GridPosition position = chart.notes[index].position;
        const double threshold_seconds =
            static_cast<double>(g_backlight_rest_measures) * measure_seconds_at(position.measure);
        if (any_information && notes[index].start_seconds > lit_until &&
            notes[index].start_seconds - lit_until >= threshold_seconds)
        {
            state.backlight_rests.push_back(
                HighwayBacklightRest{
                    .from_seconds = lit_until,
                    .to_seconds = notes[index].start_seconds,
                    .lead_seconds = margin_seconds_at(position),
                });
        }
        lit_until = std::max({lit_until, notes[index].start_seconds, notes[index].end_seconds});
        any_information = true;
        last_lit_position = position;
    }
    if (any_information)
    {
        // The trailing rest, which nothing returns from: an infinite end lets the shared
        // brightness query fade the light out and leave it out with no case of its own.
        while (next_shape < state.chart.shapes.size())
        {
            lit_until = std::max(lit_until, state.chart.shapes[next_shape].end_seconds);
            ++next_shape;
        }
        state.backlight_rests.push_back(
            HighwayBacklightRest{
                .from_seconds = lit_until,
                .to_seconds = std::numeric_limits<double>::infinity(),
                .lead_seconds = margin_seconds_at(last_lit_position),
            });
    }

    return state;
}

} // namespace rock_hero::common::core
