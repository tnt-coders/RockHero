#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <ranges>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/highway/highway_hit_glow.h>
#include <rock_hero/common/core/highway/highway_light.h>
#include <rock_hero/common/core/highway/highway_projection.h>
#include <rock_hero/common/core/highway/highway_tail.h>
#include <rock_hero/common/core/shared/ascii_case.h>
#include <span>
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

// THE FRETTING HAND'S TRACK (HighwayViewState::fret_hand's track): each placement's approach, in
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
                .high_line =
                    static_cast<double>(FretWindow{.fret = fhp.fret, .width = fhp.width}.top()),
                .ramp_seconds = fhp.ramp_seconds,
                .unpitched_ramp = fhp.unpitched_ramp,
                .settle_seconds = fhp.settle_seconds,
            });
    }
    return track;
}

// A member's position on its own rail at an instant: its sounding stop before any glide, eased
// along each leg with the family the rail draws (highwaySlideEaseWeight, the family being the
// stop's own slide-out flag), and the last stop's afterwards. A tap's unpitched slide-out
// never moves the light; a scrape's stops, its slide-out included, ARE the hand's travel. The
// DRAWN position
// (highwayDrawnStop), so a path cannot walk off the board while the head it belongs to is held at
// the edge.
[[nodiscard]] double memberPositionAt(const NoteViewState& note, const double seconds)
{
    const bool scrape = isScrape(note.attack);
    double previous_seconds = note.start_seconds;
    double previous_position = highwayStopPosition(highwayDrawnStop(note, note.fret));
    for (std::size_t index = 0; index < note.slides.size(); ++index)
    {
        const SlideStopViewState& stop = note.slides[index];
        if ((stop.slide_out && !scrape) || stop.fret <= 0)
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
                                        highwaySlideEaseWeight(progress, stop.slide_out));
        }
        previous_seconds = stop.seconds;
        previous_position = stop_position;
    }
    return previous_position;
}

// When the note's hand leaves: the last pitched keyframe when a DRAWN unpitched slide-out
// follows (pressure is already coming off), otherwise the drawn tail's end — which for a scrape
// is where the pick lifts.
[[nodiscard]] double noteReleaseAt(const NoteViewState& note)
{
    const bool drawn_slide_out = !isScrape(note.attack) && !note.slides.empty() &&
                                 note.slides.back().slide_out &&
                                 instantDrawn(note.slides.back().seconds, note.ink_end_seconds);
    if (!drawn_slide_out)
    {
        return note.ink_end_seconds;
    }
    double last_pitched = note.start_seconds;
    for (const SlideStopViewState& keyframe : note.slides)
    {
        if (!keyframe.slide_out && keyframe.fret > 0)
        {
            last_pitched = keyframe.seconds;
        }
    }
    return last_pitched;
}

// THE ONE WALK over the right-hand onset groups both picking-hand producers read, so the grouping
// is stated once: contiguous notes struck together (the shared onset epsilon) whose attack is a
// right-hand onset sounding on the board. Each group with at least one such member is visited with
// the strike's facts and its members' indices into `notes`, ascending. Fretting-hand notes sharing
// the onset are not members.
template <typename Visit>
void forEachTapGroup(const std::vector<NoteViewState>& notes, const Visit& visit)
{
    std::vector<std::size_t> members;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double onset = notes[index].start_seconds;
        std::size_t group_end = index + 1;
        while (group_end < notes.size() &&
               std::abs(notes[group_end].start_seconds - onset) < g_onset_match_epsilon)
        {
            ++group_end;
        }
        HighwayTapOnsetViewState strike{
            .start_seconds = onset,
            .fret_low = 0,
            .fret_high = 0,
            .count = 0,
            .release_seconds = onset,
        };
        members.clear();
        for (std::size_t member = index; member < group_end; ++member)
        {
            const NoteViewState& note = notes[member];
            // Judged on where the note SOUNDS, so an open-string tap HARMONIC strikes its node. The
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
            strike.fret_low =
                strike.count == 0 ? sounding_fret : std::min(strike.fret_low, sounding_fret);
            strike.fret_high = std::max(strike.fret_high, sounding_fret);
            ++strike.count;
            strike.release_seconds = std::max(strike.release_seconds, note.ring_end_seconds);
            members.push_back(member);
        }
        index = group_end;
        if (!members.empty())
        {
            visit(strike, std::span<const std::size_t>(members));
        }
    }
}

// One group's PATH appended to the picking hand's track: the onset, every member's stops the light
// follows up to and including the first past the ink end, and the release where it extends the
// path. The instants come first, because a chord's extent at any of them spans every member's
// position there, which is read once the group's list is whole. The first arrival is instant, and
// every later one ramps over the leg from the arrival before it. An instant strike is not a lost
// morph: the reading rule (highwayLitTrackTime) holds a light's window at its start through its
// rise, so a ramp into a strike could never show — and one clamped against a release rather than
// the previous arrival broke the track's ordering for a glide the next strike's margin cuts.
void appendTapGroupPath(
    const std::vector<NoteViewState>& notes, const HighwayTapOnsetViewState& strike,
    const std::span<const std::size_t> members, std::vector<HighwayHandArrival>& track)
{
    const std::size_t head = track.size();
    track.push_back(HighwayHandArrival{.seconds = strike.start_seconds});
    for (const std::size_t member : members)
    {
        const NoteViewState& tap = notes[member];
        const bool scrape = isScrape(tap.attack);
        for (std::size_t stop_index = 0; stop_index < tap.slides.size(); ++stop_index)
        {
            const SlideStopViewState& stop = tap.slides[stop_index];
            if ((stop.slide_out && !scrape) || stop.fret <= 0)
            {
                continue;
            }
            track.push_back(
                HighwayHandArrival{
                    .seconds = stop.seconds,
                    .unpitched_ramp = stop.slide_out,
                    .settle_seconds = std::max(0.0, stop.seconds - tap.ink_end_seconds),
                });
            if (!instantDrawn(stop.seconds, tap.ink_end_seconds))
            {
                break;
            }
        }
    }
    std::ranges::sort(
        std::ranges::subrange(track.begin() + static_cast<std::ptrdiff_t>(head), track.end()),
        std::ranges::less{},
        &HighwayHandArrival::seconds);
    // Two members stopping at one instant are one arrival: an unpitched leg into it keeps the
    // unpitched ease, and the longer settle wins, whichever the sort put first.
    std::size_t kept = head + 1;
    for (std::size_t at = head + 1; at < track.size(); ++at)
    {
        HighwayHandArrival& last = track[kept - 1];
        if (track[at].seconds - last.seconds < g_onset_match_epsilon)
        {
            last.unpitched_ramp = last.unpitched_ramp || track[at].unpitched_ramp;
            last.settle_seconds = std::max(last.settle_seconds, track[at].settle_seconds);
            continue;
        }
        track[kept] = track[at];
        ++kept;
    }
    track.resize(kept);
    // The LIGHT's release (the latest member's noteReleaseAt, the drawn end the per-strike pulse
    // fades from) extends the path only past its last stop: inside a cut leg it is where the light
    // fades, never a stop the hand makes, and an arrival there would split the leg. Not the
    // strike's own hold end, which runs to the true ring and so could read a leg the rail never
    // draws.
    double light_release = strike.start_seconds;
    for (const std::size_t member : members)
    {
        light_release = std::max(light_release, noteReleaseAt(notes[member]));
    }
    if (light_release - track.back().seconds >= g_onset_match_epsilon)
    {
        track.push_back(HighwayHandArrival{.seconds = light_release});
    }
    for (std::size_t at = head; at < track.size(); ++at)
    {
        HighwayHandArrival& arrival = track[at];
        double low = memberPositionAt(notes[members.front()], arrival.seconds);
        double high = low;
        for (const std::size_t member : members.subspan(1))
        {
            const double position = memberPositionAt(notes[member], arrival.seconds);
            low = std::min(low, position);
            high = std::max(high, position);
        }
        arrival.low_line = low - 1.0;
        arrival.high_line = high;
        arrival.ramp_seconds = at == head ? 0.0 : arrival.seconds - track[at - 1].seconds;
    }
}

// The pops a note's scored arrivals after its onset make, appended to the list of the hand the note
// belongs to — `right_hand` picks the right-hand-onset notes (rightHandOnset), so a tapped glide's
// landings pop with the picking hand. Every arrival pops the wires of its fret (the game registers
// these as hit-or-miss, and the editor previews 100%-perfect play). A marked slide keyframe
// (highwayMarksKeyframe) is a fret arrival: the finger lands on a new fret. A bend target is a
// pitch arrival on the fret the finger stays planted on: each curve point ending a sloped segment
// (bend reached, release completed) within the ink end, while flat holds and the onset point are
// not — the strike already covers the onset. Releases are left for clampStrikePops.
void appendArrivalPops(
    const std::vector<NoteViewState>& notes, const bool right_hand,
    std::vector<HighwayStrikePop>& pops)
{
    for (const NoteViewState& note : notes)
    {
        if (rightHandOnset(note.attack) != right_hand)
        {
            continue;
        }
        for (const SlideStopViewState& keyframe : note.slides)
        {
            if (highwayMarksKeyframe(note, keyframe))
            {
                pops.push_back(
                    HighwayStrikePop{
                        .onset_seconds = keyframe.seconds,
                        .release_seconds = 0.0,
                        .fret = keyframe.fret,
                    });
            }
        }
        for (std::size_t point = 1; note.fret > 0 && point < note.bend.size(); ++point)
        {
            const BendPointViewState& segment_from = note.bend[point - 1];
            const BendPointViewState& arrival = note.bend[point];
            if (arrival.seconds > note.ink_end_seconds)
            {
                break; // the curve's remaining points lie in the ring's ending zone
            }
            if (std::is_neq(arrival.semitones <=> segment_from.semitones))
            {
                pops.push_back(
                    HighwayStrikePop{
                        .onset_seconds = arrival.seconds,
                        .release_seconds = 0.0,
                        .fret = note.fret,
                    });
            }
        }
    }
}

// THE POP CLAMP, one rule for every pop of both hands, applied once here: sorted by onset, each
// pop's release is clamped (highwayHitGlowRelease) against the next pop of its hand landing on the
// same strips — the same fret's wires, or the box sides — so a fast run keeps a discrete pop per
// strike instead of fusing into a shimmer. Every box pop of one hand lands on the same two strips
// (the hand's live window, wherever its chord's frets were), so each clamps against the next. Pops
// within the onset epsilon of each other are one strike and never clamp each other. The scan stops
// one nominal release plus the guard past the pop: a later partner would leave the nominal release.
void clampStrikePops(
    std::vector<HighwayStrikePop>& pops, const double nominal_release_seconds,
    const double trough_guard_seconds)
{
    std::ranges::stable_sort(pops, std::ranges::less{}, &HighwayStrikePop::onset_seconds);
    const double horizon = nominal_release_seconds + trough_guard_seconds;
    for (std::size_t index = 0; index < pops.size(); ++index)
    {
        HighwayStrikePop& pop = pops[index];
        double spacing = std::numeric_limits<double>::infinity();
        for (std::size_t next = index + 1; next < pops.size(); ++next)
        {
            const double gap = pops[next].onset_seconds - pop.onset_seconds;
            if (gap > horizon)
            {
                break;
            }
            if (gap >= g_onset_match_epsilon && pops[next].fret == pop.fret)
            {
                spacing = gap;
                break;
            }
        }
        pop.release_seconds =
            highwayHitGlowRelease(nominal_release_seconds, trough_guard_seconds, spacing);
    }
}

// THE FRETTING HAND'S POPS. Its strikes, walked as onset clusters under the chord boxes' own
// grouping rule (notes within the onset epsilon strike together): a boxed cluster pops its box's
// two sides INSTEAD of its fret lines — the box interior stays deliberately dark (what the interior
// does instead is an open decision) — and a lone open pops the same two, because its bar spans the
// window; an unboxed cluster's fretted notes each pop the wires bounding the slot they PRESS. Then
// the fretting-hand notes' landings and bend arrivals (appendArrivalPops), and the one clamp.
//
// Whether a box covers a cluster is the projection's answer, not a member count here: a second
// reading of "is there a box here" would light both, or neither, wherever the two disagreed. BOTH
// PRODUCERS (review R2(b)): a strum's own chord box, and the ARPEGGIO mark its covering span draws.
// A lone note under a bracket wears no chord box, so reading the box alone would light its per-fret
// lines straight through a mark already standing over them.
//
// A fretted pop skips fret 0, which is a natural harmonic's stop (its finger is on the node and
// presses nothing) as much as a true open string's, so no node reaches the wires and the
// containing-fret ceil could only ever answer the note's own fret. Whether a natural's strike
// should light anything at all is open, parked with the tabled harmonic node light in the backlog.
[[nodiscard]] std::vector<HighwayStrikePop> makeFretHandPops(
    const std::vector<NoteViewState>& notes, const std::vector<HighwayChordGroupViewState>& groups,
    const std::vector<std::size_t>& note_group, const double nominal_release_seconds,
    const double trough_guard_seconds)
{
    std::vector<HighwayStrikePop> pops;
    for (std::size_t index = 0; index < notes.size();)
    {
        const double cluster_start = notes[index].start_seconds;
        std::size_t cluster_end = index + 1;
        while (cluster_end < notes.size() &&
               std::abs(notes[cluster_end].start_seconds - cluster_start) < g_onset_match_epsilon)
        {
            ++cluster_end;
        }
        const auto members = std::span(notes).subspan(index, cluster_end - index);
        const bool any_open = std::ranges::any_of(members, [](const NoteViewState& note) {
            return !rightHandOnset(note.attack) && openString(note);
        });
        const HighwayChordGroupViewState& covering = groups[note_group[index]];
        const bool boxed =
            covering.box_treatment != HighwayChordBoxTreatment::None || covering.arpeggio_mark;
        if (boxed || any_open)
        {
            pops.push_back(
                HighwayStrikePop{
                    .onset_seconds = cluster_start,
                    .release_seconds = 0.0,
                    .fret = std::nullopt,
                });
        }
        for (const NoteViewState& note : members)
        {
            if (!boxed && !rightHandOnset(note.attack) && note.fret > 0)
            {
                pops.push_back(
                    HighwayStrikePop{
                        .onset_seconds = note.start_seconds,
                        .release_seconds = 0.0,
                        .fret = note.fret,
                    });
            }
        }
        index = cluster_end;
    }
    appendArrivalPops(notes, /*right_hand=*/false, pops);
    clampStrikePops(pops, nominal_release_seconds, trough_guard_seconds);
    return pops;
}

// THE PICKING HAND'S POPS. Its strikes: a tapped chord pops its box's two sides (the interior stays
// dark like the strummed boxes), a single tap the wires of its own sounding slot like a fretted
// single. Then the right-hand-onset notes' landings and bend arrivals, and the one clamp.
[[nodiscard]] std::vector<HighwayStrikePop> makePickHandPops(
    const std::vector<NoteViewState>& notes,
    const std::vector<HighwayTapOnsetViewState>& tap_onsets, const double nominal_release_seconds,
    const double trough_guard_seconds)
{
    std::vector<HighwayStrikePop> pops;
    pops.reserve(tap_onsets.size());
    for (const HighwayTapOnsetViewState& strike : tap_onsets)
    {
        pops.push_back(
            HighwayStrikePop{
                .onset_seconds = strike.start_seconds,
                .release_seconds = 0.0,
                .fret = tappedChord(strike) ? std::nullopt : std::optional<int>{strike.fret_low},
            });
    }
    appendArrivalPops(notes, /*right_hand=*/true, pops);
    clampStrikePops(pops, nominal_release_seconds, trough_guard_seconds);
    return pops;
}

} // namespace

// Rationale lives on the declaration in highway_projection.h.
std::vector<HighwayTapOnsetViewState> makeHighwayTapOnsets(
    const std::vector<NoteViewState>& notes, const std::span<const double> margin_rise)
{
    assert(margin_rise.size() == notes.size() && "one margin per note");
    std::vector<HighwayTapOnsetViewState> onsets;
    forEachTapGroup(
        notes, [&](const HighwayTapOnsetViewState& struck, std::span<const std::size_t>) {
            // The hold is DRAWN by rule 12a, as a posture span's is: a head standing where the
            // struck rings close is kept one margin clear of.
            HighwayTapOnsetViewState strike = struck;
            const auto closing = std::ranges::lower_bound(
                notes,
                strike.release_seconds - g_onset_match_epsilon,
                std::ranges::less{},
                &NoteViewState::start_seconds);
            std::optional<double> limit;
            if (closing != notes.end() &&
                std::abs(closing->start_seconds - strike.release_seconds) < g_onset_match_epsilon)
            {
                const auto head = static_cast<std::size_t>(std::distance(notes.begin(), closing));
                limit = closing->start_seconds - margin_rise[head] - strike.start_seconds;
            }
            strike.release_seconds =
                strike.start_seconds +
                drawnHoldExtent(strike.release_seconds - strike.start_seconds, limit, 0.0);
            onsets.push_back(strike);
        });
    return onsets;
}

// Rationale lives on the declaration in highway_projection.h.
HighwayHandLight makePickHandLight(
    const std::vector<NoteViewState>& notes, const std::span<const double> margin_rise)
{
    assert(margin_rise.size() == notes.size() && "one margin rise per note");
    HighwayHandLight light;
    // One lit-stretch item per tapped member, all starting at its group's onset.
    std::vector<HighwayLitStretch> items;
    forEachTapGroup(
        notes,
        [&](const HighwayTapOnsetViewState& strike, const std::span<const std::size_t> members) {
            for (const std::size_t member : members)
            {
                items.push_back(
                    HighwayLitStretch{
                        .start_seconds = strike.start_seconds,
                        .release_seconds = noteReleaseAt(notes[member]),
                        .rise_seconds = margin_rise[member],
                    });
            }
            // The hand has moved on: an earlier group's arrivals at or after this onset — the tail
            // of a leg still settling, or a release past this strike — are not where it stands.
            while (!light.track.empty() &&
                   light.track.back().seconds > strike.start_seconds - g_onset_match_epsilon)
            {
                light.track.pop_back();
            }
            appendTapGroupPath(notes, strike, members, light.track);
        });
    light.lit = mergeLitEvidence(std::move(items), g_pick_light_rest_seconds);
    return light;
}

// Rationale lives on the declaration in highway_projection.h.
std::vector<HighwayLitStretch> makeFretHandLight(
    const ChartViewState& scene, const std::span<const double> margin_rise)
{
    assert(margin_rise.size() == scene.notes.size() && "one margin rise per note");
    std::vector<HighwayLitStretch> items;
    items.reserve(scene.notes.size() + scene.shapes.size());
    for (std::size_t index = 0; index < scene.notes.size(); ++index)
    {
        const NoteViewState& note = scene.notes[index];
        // A bare right-hand onset says nothing about the fretting hand; one whose held stop is
        // pressed is that hand holding it. No tier is needed: a DEFAULT held stop is above zero
        // only under a covering span, which is evidence anyway. Bound to a local so the presence
        // test and the read are provably the same object.
        const std::optional<StopMarkViewState>& held = note.stop_mark;
        if (rightHandOnset(note.attack) && (!held.has_value() || held->fret <= 0))
        {
            continue;
        }
        items.push_back(
            HighwayLitStretch{
                .start_seconds = note.start_seconds,
                .release_seconds = noteReleaseAt(note),
                .rise_seconds = margin_rise[index],
            });
    }
    // A span never OPENS a light: it starts at a note's onset, which carries the rise, or tiles
    // onto its predecessor, so it lights its drawn extent with no rise of its own.
    for (const ShapeViewState& shape : scene.shapes)
    {
        items.push_back(
            HighwayLitStretch{
                .start_seconds = shape.start_seconds,
                .release_seconds = shape.drawn_end_seconds,
                .rise_seconds = 0.0,
            });
    }
    return mergeLitEvidence(std::move(items), g_hand_rest_seconds);
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

    // Per-note margin rise: whichever hand a note lights, its light rises over the fret-hand
    // placements' own arrival margin (marginBefore) before the onset. One loop feeds both hands'
    // producers. The scene's notes pair one-to-one with the chart's, and a note's position
    // survives the saved-form transform, so the grid position comes straight from the chart.
    std::vector<double> margin_rise_seconds;
    margin_rise_seconds.reserve(notes.size());
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        margin_rise_seconds.push_back(
            notes[index].start_seconds -
            tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(
                tempo_map, marginBefore(tempo_map, chart.notes[index].position))));
    }
    // Onset groups and their repeat classification, derived here once per chart revision. The
    // rules look backward through the whole note stream, so the renderer's visible window could
    // neither afford them per frame nor even see everything they depend on.
    HighwayChordGrouping grouping = makeHighwayChordGroups(notes, state.chart.shapes);
    state.chord_groups = std::move(grouping.groups);
    state.note_group = std::move(grouping.note_group);

    // Both hands' lights, their pops and the tap onsets derive purely from the resolved scene; the
    // fretting hand's pops read the groups above for which clusters wear a box.
    state.tap_onsets = makeHighwayTapOnsets(notes, margin_rise_seconds);
    state.pick_hand = makePickHandLight(notes, margin_rise_seconds);
    state.pick_hand.pops = makePickHandPops(
        notes, state.tap_onsets, g_hit_glow_release_seconds, g_hit_glow_trough_guard_seconds);
    state.fret_hand = HighwayHandLight{
        .track = makeHighwayFretHand(state.chart),
        .lit = makeFretHandLight(state.chart, margin_rise_seconds),
        .pops = makeFretHandPops(
            notes,
            state.chord_groups,
            state.note_group,
            g_hit_glow_release_seconds,
            g_hit_glow_trough_guard_seconds),
    };

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
    std::vector<double> downbeat_seconds;
    for (const HighwayBeatViewState& beat : state.beats)
    {
        if (beat.measure_downbeat)
        {
            downbeat_seconds.push_back(beat.seconds);
        }
    }
    std::size_t note_cursor = 0;
    std::size_t section_cursor = 0;
    int measures_in_zone = 0;
    bool run_empty = false;
    for (std::size_t measure = 0; measure < downbeat_seconds.size(); ++measure)
    {
        const double measure_start = downbeat_seconds[measure];
        const double measure_end = measure + 1 < downbeat_seconds.size()
                                       ? downbeat_seconds[measure + 1]
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

    return state;
}

} // namespace rock_hero::common::core
