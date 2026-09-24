#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ranges>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_tail.h>
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

// THE FRETTING HAND'S TRACK (\ref HighwayViewState::fret_hand): each placement's approach, in
// the board's one motion element.
[[nodiscard]] std::vector<HighwayHandArrival> makeHighwayFretHand(const ChartViewState& scene)
{
    std::vector<HighwayHandArrival> track;
    track.reserve(scene.fret_hand_positions.size());
    for (const FhpViewState& fhp : scene.fret_hand_positions)
    {
        track.push_back(
            HighwayHandArrival{
                .seconds = fhp.seconds,
                .low_line = static_cast<double>(fhp.fret - 1),
                .high_line = static_cast<double>(fhp.fret + fhp.width - 1),
                .ramp_seconds = fhp.ramp_seconds,
                .unpitched_ramp = fhp.unpitched_ramp,
                .settle_seconds = fhp.settle_seconds,
            });
    }
    return track;
}

// A member's position on its own rail at an instant: its sounding stop before any glide, eased
// along each leg with the family the rail draws (highwaySlideEaseWeight, the family being the
// stop's own through glideStopAt), and the last stop's afterwards. A tap's unpitched slide-out
// never moves the light; a scrape's unpitched stops ARE the hand's travel. The DRAWN position
// (highwayDrawnStop), so a path cannot walk off the board while the head it belongs to is held at
// the edge.
[[nodiscard]] double memberPositionAt(const NoteViewState& note, const double seconds)
{
    const bool scrape = isScrape(note.attack);
    double previous_seconds = note.start_seconds;
    double previous_position = highwayStopPosition(highwayDrawnStop(note, note.fret));
    for (std::size_t index = 0; index < note.slides.size(); ++index)
    {
        const GlideStop stop = glideStopAt(note, index);
        if ((stop.unpitched && !scrape) || stop.fret <= 0)
        {
            continue;
        }
        const double stop_position = highwayStopPosition(highwayDrawnStop(note, stop.fret));
        if (seconds <= stop.seconds)
        {
            const double span = stop.seconds - previous_seconds;
            const double progress =
                span > 0.0 ? std::clamp((seconds - previous_seconds) / span, 0.0, 1.0) : 1.0;
            return previous_position + ((stop_position - previous_position) *
                                        highwaySlideEaseWeight(progress, stop.unpitched));
        }
        previous_seconds = stop.seconds;
        previous_position = stop_position;
    }
    return previous_position;
}

// When the member's hand leaves: the last pitched keyframe when a DRAWN unpitched slide-out
// follows (pressure is already coming off), otherwise the drawn tail's end — which for a scrape
// is where the pick lifts.
[[nodiscard]] double memberReleaseAt(const NoteViewState& note)
{
    const bool drawn_slide_out = !isScrape(note.attack) && !note.slides.empty() &&
                                 note.slides.back().slide_out &&
                                 keyframeDrawn(note.slides.back(), note.ink_end_seconds);
    if (!drawn_slide_out)
    {
        return note.ink_end_seconds;
    }
    double last_pitched = note.start_seconds;
    for (const KeyframeViewState& keyframe : note.slides)
    {
        if (!keyframe.slide_out && keyframe.fret > 0)
        {
            last_pitched = keyframe.seconds;
        }
    }
    return last_pitched;
}

} // namespace

// Rationale lives on the declaration in highway_projection.h.
std::vector<HighwayTapOnsetViewState> makeHighwayTapOnsets(
    const std::vector<NoteViewState>& notes, const std::vector<double>& note_rise_seconds)
{
    std::vector<HighwayTapOnsetViewState> onsets;
    std::vector<const NoteViewState*> taps;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double onset = notes[index].start_seconds;
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - onset) < g_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayTapOnsetViewState view{.seconds = onset, .path = {}};
        taps.clear();
        for (std::size_t member = index; member < group_end; ++member)
        {
            const NoteViewState& note = notes[member];
            // Judged on where the note SOUNDS, so an open-string tap HARMONIC lights its node. The
            // guard exists to keep a malformed chart from putting a light off the board, and the
            // sounding position is what has to be on the board — reading `fret` instead dropped the
            // light from a note the rules explicitly allow, since E4 accepts a tap that strikes a
            // node in place of a fret. Asking for the DRAWN position closes the other end of that
            // guard: the zero test below catches a light below the nut, and the board cap catches
            // one past the last fret, which a node legally can be.
            // The integer fret CONTAINING the sounding place, since the light spans fret slots: a
            // node at 12.0 lies in fret 12, one at 2.669 in fret 3 — the one ceil law.
            const int sounding_fret = handFretOf(highwayDrawnStop(note, note.fret));
            if (!rightHandOnset(note.attack) || sounding_fret <= 0)
            {
                continue;
            }
            view.fret_low =
                view.count == 0 ? sounding_fret : std::min(view.fret_low, sounding_fret);
            view.fret_high = std::max(view.fret_high, sounding_fret);
            ++view.count;
            view.rise_seconds = std::max(
                view.rise_seconds,
                member < note_rise_seconds.size() ? note_rise_seconds[member] : 0.0);
            taps.push_back(&note);
        }
        index = group_end;
        if (taps.empty())
        {
            continue;
        }
        // The instants first — the onset, every member's stops the light follows up to and
        // including the first past the ink end, and the release where it extends the path —
        // because a chord's extent at any of them spans every member's position there, which is
        // read once the list is whole.
        std::vector<HighwayHandArrival>& path = view.path;
        path.push_back(HighwayHandArrival{.seconds = onset});
        view.release_seconds = onset;
        for (const NoteViewState* const tap : taps)
        {
            view.release_seconds = std::max(view.release_seconds, memberReleaseAt(*tap));
            const bool scrape = isScrape(tap->attack);
            for (std::size_t stop_index = 0; stop_index < tap->slides.size(); ++stop_index)
            {
                const GlideStop stop = glideStopAt(*tap, stop_index);
                if ((stop.unpitched && !scrape) || stop.fret <= 0)
                {
                    continue;
                }
                path.push_back(
                    HighwayHandArrival{
                        .seconds = stop.seconds,
                        .unpitched_ramp = stop.unpitched,
                        .settle_seconds = std::max(0.0, stop.seconds - tap->ink_end_seconds),
                    });
                if (!keyframeDrawn(tap->slides[stop_index], tap->ink_end_seconds))
                {
                    break;
                }
            }
        }
        std::ranges::sort(path, std::ranges::less{}, &HighwayHandArrival::seconds);
        // Two members stopping at one instant are one arrival: an unpitched leg into it keeps
        // the unpitched ease, and the longer settle wins, whichever the sort put first.
        std::size_t kept = 1;
        for (std::size_t at = 1; at < path.size(); ++at)
        {
            HighwayHandArrival& last = path[kept - 1];
            if (path[at].seconds - last.seconds < g_onset_match_epsilon)
            {
                last.unpitched_ramp = last.unpitched_ramp || path[at].unpitched_ramp;
                last.settle_seconds = std::max(last.settle_seconds, path[at].settle_seconds);
                continue;
            }
            path[kept] = path[at];
            ++kept;
        }
        path.resize(kept);
        // The release extends the path only past its last stop: inside a cut leg it is where the
        // light fades, never a stop the hand makes, and an arrival there would split the leg.
        if (view.release_seconds - path.back().seconds >= g_onset_match_epsilon)
        {
            path.push_back(HighwayHandArrival{.seconds = view.release_seconds});
        }
        for (std::size_t at = 0; at < path.size(); ++at)
        {
            HighwayHandArrival& arrival = path[at];
            double low = memberPositionAt(*taps.front(), arrival.seconds);
            double high = low;
            for (std::size_t tap = 1; tap < taps.size(); ++tap)
            {
                const double position = memberPositionAt(*taps[tap], arrival.seconds);
                low = std::min(low, position);
                high = std::max(high, position);
            }
            arrival.low_line = low - 1.0;
            arrival.high_line = high;
            arrival.ramp_seconds = at == 0 ? 0.0 : arrival.seconds - path[at - 1].seconds;
        }
        // Crowding clamp, mirroring the fret-hand ramps: the rise never reaches backward past
        // the previous tap onset's release, so a dense run keeps its per-tap dips.
        if (!onsets.empty())
        {
            view.rise_seconds = std::clamp(
                view.rise_seconds, 0.0, std::max(0.0, onset - onsets.back().release_seconds));
        }
        onsets.push_back(std::move(view));
    }
    return onsets;
}

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
    // placements' own arrival margin (\ref marginBefore) before the onset; zero for
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
    state.fret_hand = makeHighwayFretHand(state.chart);

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
                // Stamped below by the zone walk, which is the one place that decides which
                // downbeat a section start belongs to.
                .section_start = false,
            });
    }

    // Camera framing zones: the camera's framing window is quantized to these derived boundaries
    // so its target steps only here and rests in between — the step-then-rest cadence that defines
    // the intended camera feel. The derivation mirrors a standard automatic phrase generator: runs
    // of measures containing note onsets split into g_camera_zone_measures-sized groups aligned to
    // downbeats, a run of empty measures collapses into one zone however long (rests are the
    // camera's travel time, not framing churn), and a section start forces a new zone.
    //
    // The walk keeps beat INDICES rather than a parallel list of seconds, because the same pass
    // that decides a section cut is also the one that promotes that downbeat's bar: which
    // downbeat a section belongs to is answered once, here, for both the camera and the board.
    std::vector<std::size_t> downbeat_indices;
    for (std::size_t index = 0; index < state.beats.size(); ++index)
    {
        if (state.beats[index].measure_downbeat)
        {
            downbeat_indices.push_back(index);
        }
    }
    std::size_t note_cursor = 0;
    std::size_t section_cursor = 0;
    int measures_in_zone = 0;
    bool run_empty = false;
    for (std::size_t measure = 0; measure < downbeat_indices.size(); ++measure)
    {
        const double measure_start = state.beats[downbeat_indices[measure]].seconds;
        const double measure_end = measure + 1 < downbeat_indices.size()
                                       ? state.beats[downbeat_indices[measure + 1]].seconds
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
        state.beats[downbeat_indices[measure]].section_start = section_cut;
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
