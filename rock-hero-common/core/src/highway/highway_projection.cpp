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

// How long the fretting hand must say nothing before its light goes out: a QUARTER NOTE (user
// ruling 2026-08-30, re-ruled down from one measure to be sighted at this much more aggressive
// length). The one threshold the rest derivation reads.
//
// Quarter-note-referenced rather than beat- or measure-referenced, for the reason
// g_minimum_kept_sustain_whole_note states about the same figure: one signature beat of 12/8 is
// an eighth, and a measure varies with the meter, so neither says the same thing about silence in
// two meters. Its OWN constant rather than that bound reused — a shortest-earned-ring and a
// shortest-noticed-silence are two rules that happen to share a note value today.
constexpr Fraction g_backlight_rest_whole_note{1, 4};

// The threshold in signature beats: a whole note is `signature_denominator` beats, so it scales
// with the meter exactly as the two whole-note-referenced bounds in grid_arithmetic.h do.
[[nodiscard]] constexpr Fraction backlightRestBeats(const int signature_denominator) noexcept
{
    return Fraction{
        signature_denominator * g_backlight_rest_whole_note.numerator,
        g_backlight_rest_whole_note.denominator
    };
}

// SIGHTING SWITCH — whether a picking-hand TAP keeps the fretting hand's light lit. True is the
// conservative side and the shipped default: a tap says nothing about the fretting hand, but
// fading a board's light through a tap run has never been looked at. Flip to false to sight it.
// Its companion in this batch is the harmonic node light's colour, which has since become a
// RUNTIME rig (F9; g_harmonic_light_candidates in highway_renderer.cpp) — that one is a UI fact
// with three answers to compare side by side, while this is a musical one with two.
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
    // tempo map's — the threshold at the local meter, and marginBefore, the ONE arrival lead the
    // hand's own morph and the picking hand's light rise already share.
    //
    // A hand-posture span can only ever EXTEND a lit stretch, never open one: its members are
    // notes, and a note's onset lights the board at or before the span it belongs to starts. That
    // is what makes the returning statement a rest leads always a NOTE, whose grid position is in
    // hand here — so folding spans in as ends alone is not a shortcut, it is the whole of what
    // they can contribute.
    const auto seconds_at = [&tempo_map](const GridPosition position) {
        return tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, position));
    };
    // The shared arrival lead in seconds at one position — stated once here because both the
    // returning rests and the trailing one carry it.
    const auto lead_seconds_at = [&tempo_map, &seconds_at](const GridPosition position) {
        return seconds_at(position) - seconds_at(marginBefore(tempo_map, position));
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
        const GridPosition position = chart.notes[index].position;
        // The threshold as a POSITION rather than as a duration compared against a duration: the
        // board qualifies when it went dark at or before the instant one threshold before this
        // statement. Same rule, and the form matters — a difference of two tempo-map queries
        // compared against a difference of two others lands a silence that is EXACTLY the
        // threshold on whichever side the last bit of each interpolation fell, and at a
        // quarter-note threshold an exactly-a-quarter silence is the commonest figure there is.
        //
        // advanceGridPosition clamps at the grid origin, so the window is checked to have fitted
        // rather than trusted. A clamped one always answers no: a statement inside the song's
        // first threshold necessarily has an earlier statement inside that same window — one
        // exists (any_information) and none can predate the origin — so the silence in front of it
        // is short by construction.
        const Fraction rest_beats =
            backlightRestBeats(tempo_map.timeSignatureAt(position.measure).denominator);
        const GridPosition dark_from =
            advanceGridPosition(tempo_map, position, Fraction{} - rest_beats);
        if (any_information && beatDistance(tempo_map, dark_from, position) == rest_beats &&
            lit_until <= seconds_at(dark_from))
        {
            state.backlight_rests.push_back(
                HighwayBacklightRest{
                    .from_seconds = lit_until,
                    .to_seconds = notes[index].start_seconds,
                    .lead_seconds = lead_seconds_at(position),
                });
        }
        // Everything THIS statement lights, folded AFTER its own gap was measured: its onset, its
        // ring, and every span standing at or before that onset. The order is the whole of what
        // keeps the invariant above true — a span opening at this very onset is opened BY this
        // statement, so folding it first answered the silence IN FRONT of the statement with light
        // the statement itself brought, and every silence that ended on a chord vanished.
        while (next_shape < state.chart.shapes.size() &&
               state.chart.shapes[next_shape].start_seconds <= notes[index].start_seconds)
        {
            lit_until = std::max(lit_until, state.chart.shapes[next_shape].end_seconds);
            ++next_shape;
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
                .lead_seconds = lead_seconds_at(last_lit_position),
            });
    }

    return state;
}

} // namespace rock_hero::common::core
