#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <rock_hero/common/core/chart/chart_fret_hand.h>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <rock_hero/common/core/chart/head_margins.h>
#include <rock_hero/common/core/shared/visible_events.h>
#include <utility>
#include <variant>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Whether this note's end statement is a SLIDE-OUT rather than a shift slide's arrival, from the
// RESOLVED relation and never from position: the two are one statement at the ring's end, and what
// tells them apart is the stop the next head takes (\ref arrivesIntoNextHead). Asked by both walks
// here, so the flag the surfaces key on cannot be derived two ways.
[[nodiscard]] bool noteSlidesOut(const ChartConnections& connections, const std::size_t index)
{
    return slideOutFretOrNull(connections.saved_notes[index], connections.arrives_into[index]) !=
           nullptr;
}

// SHAPE RULE 6, and this is the only place it lives. A span's DRAWN extent keeps the minimum
// sustain distance before the head that closed it — the same margin every other drawn element
// keeps, so consecutive shapes show the gap everything else shows instead of butting exactly — and
// never runs into the hand's next move. What the derivation stores is the MUSICAL CLOSE
// (\ref ChartShape::sustain): the instant the statement actually ended, which is what the spans
// themselves are measured against and what a figure's seams have to abut at. Trimming there would
// put a display margin inside every seam.
//
// FOUR FACTS, and each answers a case the others cannot:
//
//   the CLOSING HEAD (\ref ChartShape::closing_onset_place) is one limit, one margin before it. It
//   is present only where that head cut a live statement: a span whose rings died before the next
//   onset ends where they died and is not pulled back from a head it never reached. Absent too at
//   a close at a slot of held fingers, which sounds nothing to keep a distance from and is exactly
//   what keeps a landing successor tiled onto its predecessor;
//
//   the HAND'S NEXT MOVE (\p move_starts) is the other limit: the first margin morph to begin after
//   the span's front is the hand leaving the grip, so the furniture stops where that morph begins
//   rather than riding it. It is what bounds the span whose rings outlived the morph into the next
//   position without a head to cut them. A glide's ramp is never one: it is a member of the span
//   travelling, the span covers it (shape rule 4), and its rails ride the window with it;
//
//   the LAST STATEMENT (\ref ChartShape::stated_extent) floors the trim, because furniture may not
//   retreat behind the strum it is drawn over. In a fast enough passage the closing onset crowds
//   inside the margin, and a box trimmed blindly would stop short of its own last head;
//
//   PROTECTED ADJACENCY, where even that leaves nothing: a statement made at an instant is drawn
//   however crowded, so it falls back to the musical close itself — exact adjacency, mirroring the
//   sustain rules' own precedent. The one span nothing states at an instant never reaches here,
//   because a close leaving it no room is what deletes it in the walk (shape rule 4's emission).
//
// The extent is therefore positive exactly where the musical close is, and never past it.
[[nodiscard]] Fraction drawnShapeExtent(
    const ChartShape& shape, const HeadMargins& margins, const std::vector<Fraction>& move_starts)
{
    // Bound to a local so the presence test and every read below are provably the same object.
    const std::optional<Fraction>& closing = shape.closing_onset_place;
    std::optional<Fraction> limit;
    if (closing.has_value())
    {
        // The margin at the CLOSING ONSET: it is that head's spacing that is being kept, and a
        // tempo change between the span's front and its close would otherwise measure it at the
        // wrong rate.
        limit = *closing - shape.place - margins.length(*closing);
    }
    // STRICTLY after the front: a morph begun at or before it is the hand arriving into this grip.
    // A span a shape hold states is exempt: the hold is the author's word that the hand holds
    // this grip through the hold's end, so a window changing inside it — widening for a finger
    // added above a held stop — is not the hand leaving (shape rule 9).
    if (const auto move = std::ranges::upper_bound(move_starts, shape.place);
        !shape.hold_stated && move != move_starts.end())
    {
        const Fraction before_move = *move - shape.place;
        limit = limit.has_value() ? std::min(*limit, before_move) : before_move;
    }
    return drawnSpanExtent(shape.sustain, limit, shape.stated_extent);
}

// The approach a fret-hand arrival rides when it lands exactly on a PITCHED glide's arrival, keyed
// by the keyframe's whole-note place: where the leg into that arrival starts. The arrival itself
// is the placement's own instant — nothing presentation decides moves a statement — so the hand
// travels with the rail and completes where the sound goes.
struct SlideRamp
{
    Fraction start{};
    // The note whose rail the ramp follows: its ink end is where the settle begins.
    std::size_t note{0};
};

// Walks every glide in the stored stream once and records the segment each pitched arrival rides.
// A pass of its own rather than a table filled inside the note loop below, because the key is a
// place the fret-hand pass looks a ramp up by, and the note loop resolves seconds.
//
// The SLIDE-OUT is the RESOLVED fact: a slide-out and a shift slide's arrival are the same
// statement at the same place, and only the relation tells them apart (\ref arrivesIntoNextHead).
// A slide-out is no arrival the hand lands, so it files nothing; the window rides it as motion
// (\ref FretHandResolutions::rides).
[[nodiscard]] std::map<Fraction, SlideRamp> makeSlideRampStarts(const ChartConnections& connections)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    std::map<Fraction, SlideRamp> starts;
    for (std::size_t index = 0; index < notes.size(); ++index)
    {
        const ChartNote& note = notes[index];
        // A scrape is the picking hand's travel and never feeds the slide-locked ramps: it has
        // no fret-hand anchor to ramp. A note carrying no glide at all — nearly every note —
        // leaves before a single position is resolved.
        if (isScrape(note.attack) || !anyKeyframeStatesFret(note.keyframes))
        {
            continue;
        }
        const bool slides_out = noteSlidesOut(connections, index);

        const Fraction place = connections.places[index];
        Fraction segment_start = place;
        int segment_start_fret = note.fret;
        for (std::size_t keyframe_index = 0; keyframe_index < note.keyframes.size();
             ++keyframe_index)
        {
            const Keyframe& keyframe = note.keyframes[keyframe_index];
            // Only the POSITION channel makes a segment: a keyframe stating a bend or a vibrato
            // change says nothing about where the hand is, so the glide runs through it unkinked
            // and it neither starts nor ends a ramp.
            //
            // Bound to a local so the optional check and the access are provably the same object.
            const std::optional<int>& fret = keyframe.fret;
            if (!fret.has_value() || (slides_out && keyframe_index + 1 == note.keyframes.size()))
            {
                continue;
            }
            // An equal-fret keyframe is a HOLD, not a glide — nothing travels across it (the
            // pitch is pinned, which is how a slide notated on a tied continuation records where
            // it leaves from). Tying a placement's ramp to a hold's span would make the hand drift
            // the whole held stretch to arrive at a fret it never left, so holds fall through to
            // the margin morph. The segment start still advances, which is what gives the following
            // glide its true, shorter span.
            const Fraction keyframe_place = place + keyframe.offset;
            if (*fret != segment_start_fret)
            {
                starts.try_emplace(
                    keyframe_place, SlideRamp{.start = segment_start, .note = index});
            }
            segment_start = keyframe_place;
            segment_start_fret = *fret;
        }
    }
    return starts;
}

// One arrival of the fretting hand's window before its approach is resolved: where it stands, the
// window it rests on, and the ride ending exactly there if one does — the ride's own arrival, its
// return, or the hand event its slide-outs lead into.
struct TrackArrival
{
    Fraction front{};
    FretWindow window{};
    const FretHandRide* ride{nullptr};
};

// Every arrival of the window in ascending order: each hand event, each ride's own arrival where
// nothing else stands at its end, and each return. No two share a front (\ref FretHandRide::back).
[[nodiscard]] std::vector<TrackArrival> trackArrivals(const FretHandResolutions& hand)
{
    std::vector<TrackArrival> arrivals;
    arrivals.reserve(hand.events.size() + (2 * hand.rides.size()));
    for (const FretHandEvent& event : hand.events)
    {
        arrivals.push_back(TrackArrival{.front = event.front, .window = event.window});
    }
    for (const FretHandRide& ride : hand.rides)
    {
        const auto at_end = std::ranges::lower_bound(
            hand.events, ride.end, std::ranges::less{}, &FretHandEvent::front);
        const bool event_at_end = at_end != hand.events.end() && at_end->front == ride.end;
        if (!event_at_end && ride.back != ride.end)
        {
            arrivals.push_back(TrackArrival{.front = ride.end, .window = ride.ridden});
        }
        // Bound to a local so the presence test and the read are provably the same object.
        const std::optional<Fraction>& back = ride.back;
        if (back.has_value())
        {
            arrivals.push_back(TrackArrival{.front = *back, .window = ride.standing});
        }
    }
    std::ranges::sort(arrivals, std::ranges::less{}, &TrackArrival::front);
    auto ride = hand.rides.begin();
    for (TrackArrival& arrival : arrivals)
    {
        while (ride != hand.rides.end() && ride->end < arrival.front)
        {
            ++ride;
        }
        if (ride != hand.rides.end() && ride->end == arrival.front)
        {
            arrival.ride = &*ride;
        }
    }
    return arrivals;
}

} // namespace

// Derives both layers and hands them to the projection proper; a chartless arrangement projects
// the empty state.
// Rationale lives on the declaration in chart_projection.h.
double railLeavesCurveAt(
    const HandApproach approach, const double arrival_seconds,
    const double ink_end_seconds) noexcept
{
    if (approach == HandApproach::Release)
    {
        return std::min(ink_end_seconds, arrival_seconds - g_minimum_sustain_distance_seconds);
    }
    return ink_end_seconds;
}

ChartViewState makeChartViewState(const Arrangement& arrangement, const TempoMap& tempo_map)
{
    if (!arrangement.chart.has_value())
    {
        return {};
    }
    const Chart& chart = *arrangement.chart;
    const ChartResolutions resolutions =
        chartResolutions(chart.notes, chart.shape_holds, chart.tuning.capo, tempo_map);
    return makeChartViewState(
        chart,
        resolutions,
        fretHandResolutions(resolutions, chart.fret_hand_positions, tempo_map),
        tempo_map);
}

// Resolves every musical position of the two layers to seconds, once, into the scene both
// surfaces draw.
ChartViewState makeChartViewState(
    const Chart& chart, const ChartResolutions& resolutions, const FretHandResolutions& hand,
    const TempoMap& tempo_map)
{
    ChartViewState state;
    state.open_strings = chart.tuning.strings;
    state.capo = chart.tuning.capo;

    // Every per-note fact this projection reads comes from the notes layer: the stored stream it
    // draws, where each ring's ink stops, each note's resolved connection motion, and each note's
    // hold. It is derived from the saved form, so a pick slide's in-memory overrides (chart.h) are
    // already stripped and the scrape draws as the scrape it is.
    const std::vector<ChartNote>& notes = resolutions.connections.saved_notes;
    const std::vector<Fraction>& places = resolutions.connections.places;

    // Where each fret-hand placement's approach ramp begins; asked once for the whole chart and
    // read by the placement pass.
    const std::map<Fraction, SlideRamp> slide_ramp_starts =
        makeSlideRampStarts(resolutions.connections);

    // EVERY instant below — onsets included — takes the one route to the clock: its whole-note
    // place, then the time query. One instant reached two ways (a ring ending on the next head, a
    // free tail's ink end on its ring end) is then one exact place and so one double, which is what
    // lets a surface compare them exactly.
    state.notes.reserve(notes.size());
    state.note_length_ends.reserve(notes.size());
    for (std::size_t note_index = 0; note_index < notes.size(); ++note_index)
    {
        const ChartNote& note = notes[note_index];
        const Fraction place = places[note_index];
        NoteViewState view;
        view.start_seconds = tempo_map.secondsAtWholeNote(place);
        view.margin_start_seconds =
            tempo_map.secondsAtWholeNote(place - resolutions.margins.length(place));
        view.ring_end_seconds = tempo_map.secondsAtWholeNote(place + note.sustain);
        const Fraction& ink_end = resolutions.ink_end[note_index];
        view.ink_end_seconds = ink_end.numerator > 0 ? tempo_map.secondsAtWholeNote(place + ink_end)
                                                     : view.start_seconds;
        state.note_length_ends.push_back(
            tempo_map.secondsAtWholeNote(place + resolutions.note_lengths[note_index]));
        // THE TAIL LAW's verdict, carried per note so the board knows which ribbons the curtain
        // owns PART of and from where (\ref NoteViewState::rested): the one reading both
        // distance-scoped consumers share (\ref hasRestingRemainder), so a member resting at its
        // own end — a handover — publishes no window and the board draws it as any unrested
        // ribbon.
        // Bound once so the presence test and the read below are provably the same object.
        const std::optional<Fraction>& rested_from = resolutions.rested_from[note_index];
        view.rested = hasRestingRemainder(rested_from, ink_end);
        if (view.rested)
        {
            // The resting remainder's start on the clock — the landmark whose cases
            // \ref ChartPresentation::rested_from states — the anchor the board hangs the note's
            // local reveal window on, so the stated portion rides outside it.
            view.reveal_from_seconds = tempo_map.secondsAtWholeNote(place + *rested_from);
            // The board's reveal-window depth, resolved here because tempo is not on the
            // renderer's read surface: \ref g_tail_reveal_lead_whole_note measured back from the
            // onset, SHORTENED at the song's front to the room available — a positive-but-smaller
            // lead that compresses the curtain rather than disabling it. Only an onset at the
            // song's very first instant yields a zero lead, and the renderer treats that one note
            // as not rested at all.
            const Fraction lead_place = std::max(Fraction{}, place - g_tail_reveal_lead_whole_note);
            view.reveal_lead_seconds =
                view.start_seconds - tempo_map.secondsAtWholeNote(lead_place);
        }
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        // Internal evidence, never drawn: every held stop is already printed by other ink.
        view.held_fret = resolutions.held_stops[note_index];
        view.legato = resolutions.connections.legato[note_index];
        view.palm_mute = note.palm_mute;
        view.dead = note.dead;
        view.harmonic_node = note.harmonic_node;
        view.tremolo = note.tremolo;
        view.emphasis = note.emphasis;
        // The bend channel's control polyline, opened by the ONSET: the note's own bend value is a
        // stating point by definition, which is the whole of what a pre-bend is and what lets the
        // curve start at the head instead of at a synthesized anchor. A note whose channel never
        // leaves rest draws no curve at all, so nothing is opened for it and the loop below finds
        // no statements to add.
        if (noteIsBent(note))
        {
            view.bend.reserve(note.keyframes.size() + 1);
            view.bend.push_back(
                BendPointViewState{.seconds = view.start_seconds, .semitones = note.bend});
        }
        view.slides.reserve(note.keyframes.size());
        view.keyframes.reserve(note.keyframes.size());
        // THE ONE FLAG that isolates every surface (noteSlidesOut): false draws a linked arrival
        // head at the ring's end; true draws a floating slide-out chip.
        const bool slides_out = noteSlidesOut(resolutions.connections, note_index);
        // The pair fact the surfaces need for the band a mark at the END takes, carried per note
        // from the walk that resolved it (\ref ChartConnections::end_heads). The projection keeps
        // the chart's note order one to one, so the chart index is the projection's.
        view.end_head = resolutions.connections.end_heads[note_index];
        // The vibrato channel resolved into the SPANS it states, folded through the same one
        // authority every other reader of the channel uses (`RingState` in chart.h). A span is
        // every consecutive vibrating leg: a leg without vibrato ends it, and a change of width
        // inside it is a step the span carries rather than a second span, so the drawn wave keeps
        // one phase and one envelope across the change. Neither surface needs the channel's rules
        // a second time.
        //
        // A span opens running to the ring's end and is cut short only when a leg without vibrato
        // follows, so a note with no keyframes falls out of the same walk with no case of its own:
        // one span covering the whole ring. A span opening exactly at that end is kept —
        // degenerate, drawing nothing, and still the honest answer that the string vibrates.
        const auto open_span = [&](const double seconds, const VibratoState width) {
            view.vibrato.push_back(
                VibratoSpanViewState{
                    .start_seconds = seconds,
                    .end_seconds = view.ring_end_seconds,
                    .state = width,
                    .width_steps = {},
                });
        };
        RingState ring = ringStateAtOnset(note);
        if (hasVibrato(ring.vibrato))
        {
            open_span(view.start_seconds, ring.vibrato);
        }
        for (std::size_t keyframe_index = 0; keyframe_index < note.keyframes.size();
             ++keyframe_index)
        {
            const Keyframe& keyframe = note.keyframes[keyframe_index];
            const double keyframe_seconds = tempo_map.secondsAtWholeNote(place + keyframe.offset);
            const VibratoState was = ring.vibrato;
            ring.advance(keyframe);
            if (ring.vibrato != was)
            {
                if (!hasVibrato(was))
                {
                    open_span(keyframe_seconds, ring.vibrato);
                }
                else if (!hasVibrato(ring.vibrato))
                {
                    view.vibrato.back().end_seconds = keyframe_seconds;
                }
                else
                {
                    view.vibrato.back().width_steps.push_back(
                        VibratoWidthStepViewState{
                            .seconds = keyframe_seconds, .state = ring.vibrato
                        });
                }
            }
            // Bound to locals so each optional check and its access are provably the same object.
            const std::optional<double>& bend = keyframe.bend;
            const std::optional<int>& fret = keyframe.fret;
            std::optional<std::size_t> bend_point;
            if (bend.has_value())
            {
                bend_point = view.bend.size();
                view.bend.push_back(
                    BendPointViewState{.seconds = keyframe_seconds, .semitones = *bend});
            }
            // The keyframe's mark, decided here once from what it states: the stop it states, else
            // a head at the fret in force where it changes the vibrato, else the curve's dot.
            KeyframeMark mark = KeyframeCurveMark{};
            if (fret.has_value())
            {
                mark = KeyframeStopMark{.stop = view.slides.size()};
                view.slides.push_back(
                    SlideStopViewState{
                        .seconds = keyframe_seconds,
                        .fret = *fret,
                        .slide_out = slides_out && keyframe_index + 1 == note.keyframes.size(),
                    });
            }
            else if (ring.vibrato != was)
            {
                mark = KeyframeRestMark{.fret = ring.fret};
            }
            view.keyframes.push_back(
                KeyframeViewState{
                    .seconds = keyframe_seconds,
                    .offset = keyframe.offset,
                    .mark = mark,
                    .bend_point = bend_point,
                });
        }
        state.notes.push_back(std::move(view));
    }

    // One chip per hand event, derived and authored alike (FretHandResolutions::events); a
    // position's identity is its grid position, so the authored position standing at a chip, if
    // any, is the one stored at the chip's own.
    const std::vector<FretHandPosition>& authored = chart.fret_hand_positions;
    state.fret_hand_positions.reserve(hand.events.size());
    for (const FretHandEvent& event : hand.events)
    {
        const GridPosition fhp_position = tempo_map.gridPositionAtWholeNote(event.front);
        const auto stated = std::ranges::lower_bound(
            authored, fhp_position, std::ranges::less{}, &FretHandPosition::position);
        FhpAuthored statement = FhpAuthored::Nothing;
        if (stated != authored.end() && stated->position == fhp_position)
        {
            statement = stated->end_fret.has_value() ? FhpAuthored::FretAndEnd : FhpAuthored::Fret;
        }
        state.fret_hand_positions.push_back(
            FhpViewState{
                .seconds = tempo_map.secondsAtWholeNote(event.front),
                .position = fhp_position,
                .fret = event.window.fret,
                .width = event.window.width,
                .authored = statement,
            });
    }

    // THE FRETTING HAND'S TRACK: every arrival gets an eased approach. One landing exactly on a
    // pitched glide's arrival ramps over that glide's segment, and one where a ridden slide-out
    // ends over the slide-out's, so a drawn hand travels with the note; any other morphs over the
    // shared minimum-sustain-distance margin before the arrival, and crowded transitions shorten
    // against the previous arrival rather than overlapping it. The glide's rail is drawn to its ink
    // end and no further, so where that falls before the arrival the approach settles off the curve
    // there; a release rail ends at speed, so its leg settles off the curve no later than one
    // margin before its arrival, whatever arrives there (railLeavesCurveAt).
    //
    // Each MARGIN morph's start is kept, ascending, for the span pass below: it is where the hand
    // leaves whatever grip it held (\ref drawnShapeExtent).
    const std::vector<TrackArrival> hand_arrivals = trackArrivals(hand);
    Fraction previous_arrival{};
    std::vector<Fraction> move_starts;
    state.fret_hand_track.reserve(hand_arrivals.size());
    for (const TrackArrival& arrival : hand_arrivals)
    {
        const double arrival_seconds = tempo_map.secondsAtWholeNote(arrival.front);
        // A placement is not a head, so its margin is asked of the authority directly.
        const Fraction margin_start =
            arrival.front - minimumSustainDistance(tempo_map, arrival.front);
        Fraction ramp_start = margin_start;
        HandApproach approach = HandApproach::Morph;
        std::optional<HandSettle> settle;
        const auto settle_from = [&](const double leaves_curve, const FretWindow& heading) {
            if (arrival_seconds > leaves_curve)
            {
                settle = HandSettle{
                    .seconds = arrival_seconds - leaves_curve,
                    .low_line = static_cast<double>(heading.fret - 1),
                    .high_line = static_cast<double>(heading.top()),
                };
            }
        };
        // A hand LANDING a pitched glide follows that glide, even where a slide-out on another
        // string ends at the same instant: the slide-out is motion, the landing a position.
        const auto glide = slide_ramp_starts.find(arrival.front);
        if (glide != slide_ramp_starts.end())
        {
            ramp_start = glide->second.start;
            approach = HandApproach::Glide;
            settle_from(
                railLeavesCurveAt(
                    approach, arrival_seconds, state.notes[glide->second.note].ink_end_seconds),
                arrival.window);
        }
        else if (arrival.ride != nullptr)
        {
            const FretHandRide& ride = *arrival.ride;
            ramp_start = ride.departure;
            approach = HandApproach::Release;
            settle_from(
                railLeavesCurveAt(
                    approach, arrival_seconds, state.notes[ride.note].ink_end_seconds),
                ride.ridden);
        }
        // A ramp never reaches back past the previous arrival: crowded transitions shorten
        // against it rather than overlapping it.
        ramp_start = std::clamp(ramp_start, previous_arrival, arrival.front);
        previous_arrival = arrival.front;
        if (approach == HandApproach::Morph)
        {
            move_starts.push_back(ramp_start);
        }
        state.fret_hand_track.push_back(
            HandArrival{
                .seconds = arrival_seconds,
                .low_line = static_cast<double>(arrival.window.fret - 1),
                .high_line = static_cast<double>(arrival.window.top()),
                .ramp_seconds = arrival_seconds - tempo_map.secondsAtWholeNote(ramp_start),
                .approach = approach,
                .settle = settle,
            });
    }

    state.shapes.reserve(resolutions.shapes.size());
    // The shared arrival rule, answered for every span once per chart revision beside the spans
    // themselves (\ref ChartResolutions::arrivals) — the absorption rule keys on the same answer,
    // so deriving it here as well would be the class asked twice.
    const std::vector<bool>& arrivals = resolutions.arrivals;

    // WHERE a posture string states its fret, decided per string by what heads that string AT THE
    // INSTANT THE BRACKET DRAWS — the posture-smart rule. Answered here rather than in the tab
    // painter because the drawn digit needs a hit target, so the painter's column and the click's
    // column come from one statement instead of two derivations free to disagree.
    //
    // THE DIGIT WINDOW is that one instant and nothing besides. A head LATER in the span never
    // suppresses, because the opening bracket is the span's CHORD FRAME: it states the whole
    // membership at the moment the reader meets it, so members that accumulate in afterwards print
    // their frets there exactly as the ones already down do. Asking over the whole span instead
    // would empty an accumulation's frame of everything still to arrive, and its inclusive end
    // would let the very onset that CLOSED the span decide the digits inside it.
    //
    // Three answers, one question about the one head this instant can carry. NOTHING heads the
    // string: the bracket's centre is the whole of what states the stop — the silent-string case
    // and the carried-ring case alike, and the case a member arriving later is in. A head SOUNDING
    // AT THIS PLACE states it already, so the bracket prints nothing beside it. A head sounding at
    // ANOTHER place, WHICHEVER HAND MADE IT, owns the string's centre — the centred digit sits
    // exactly where a head at this instant sits and the note pass paints after the brackets — so
    // the grip it does not state takes the column beside the bracket, the one slot a head cannot
    // paint over: the centre carries what SOUNDS and the satellite what the fretting hand HOLDS.
    // THE PLACE IS PART OF THE TEST on every arm, compared as a stop and never as a printed
    // number (\ref ChartStop): a head prints where it SOUNDS (tabNoteHeadText reads the same
    // authority), so a node head over a node grip suppresses because both are that node, and a
    // fretted head over a node grip printing the same digit does not — a number stated twice
    // beside itself is the only thing suppression exists to prevent, and two different places are
    // not one number.
    const auto digit_slot = [&notes, &places](
                                const Fraction at,
                                const int string,
                                const ChartStop& stop) -> std::optional<StopMarkSlot> {
        // The ONE head this string can carry here: the stream is sorted by (position, string) and
        // refuses duplicate onsets (\ref ChartErrorCode::UnsortedOrDuplicateNotes), so the first
        // match is the only match and there is never a second answer to reconcile with it.
        for (auto index = static_cast<std::size_t>(
                 std::distance(places.begin(), std::ranges::lower_bound(places, at)));
             index < notes.size() && places[index] == at;
             ++index)
        {
            const ChartNote& head = notes[index];
            if (head.string != string)
            {
                continue;
            }
            const ChartStop printed =
                soundingStopAt(head.harmonic_node, head.attack, head.fret, head.fret);
            if (printed == stop)
            {
                return std::nullopt;
            }
            return StopMarkSlot::Satellite;
        }
        return StopMarkSlot::Bracket;
    };

    for (std::size_t shape_index = 0; shape_index < resolutions.shapes.size(); ++shape_index)
    {
        const ChartShape& shape = resolutions.shapes[shape_index];
        const Fraction place = shape.place;
        // WHERE the span's one opening mark draws, TAKEN FROM THE WALK (shape rule 4; published as
        // \ref ChartShape::bracket_place). Every span an EVENT states carries its own FRONT; a
        // CARRY-OPENED successor — one a landing or a member's death founded — carries its first
        // interior SOUNDING instead, because nothing at all is stated at a boundary; one that never
        // sounds interiorly carries nothing and draws no mark.
        //
        // Read rather than re-scanned: a scan here for "the first sounding at or after the span's
        // start" would be the walk's own grouping question asked a second time, against an extent
        // the closing trim has already shortened.
        //
        // ONE condition gates it, and this is the one: a bracket is ARPEGGIO furniture. A box-class
        // span states itself with its strums' own boxes and opens no mark of its own, which is the
        // ordinary disposition of a landing successor rather than a corner of one. Everything
        // downstream keys on this optional: the deferred bracket and the coincidence rule that
        // suppresses a chord box under an arpeggio box both ask "is a mark drawn here", and there
        // is one answer to ask.
        //
        // Bound once so the presence test and every read below are provably the same object.
        const std::optional<Fraction> bracket =
            arrivals[shape_index] ? shape.bracket_place : std::optional<Fraction>{};

        std::vector<ShapeStringViewState> strings;
        if (shape.posture < resolutions.postures.size())
        {
            const ChartPosture& posture = resolutions.postures[shape.posture];
            // Posture array index 0 is the lowest string. The bracket states the grip and nothing
            // else: a ring struck before the span is a tail, whatever sounds under the shape.
            for (std::size_t index = 0; index < posture.stops.size(); ++index)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const std::optional<ChartStop>& stop = posture.stops[index];
                if (!stop.has_value())
                {
                    continue;
                }
                const int string = static_cast<int>(index) + 1;
                strings.push_back(
                    ShapeStringViewState{
                        .string = string,
                        .stop = *stop,
                        // Asked AT the mark's own instant, which is the whole window (THE DIGIT
                        // WINDOW above): the bracket is the span's chord frame, so it states every
                        // member no head right there already states at its own place. A span
                        // drawing no bracket prints no digit anywhere, which is exactly the
                        // empty slot — the posture entry itself stays, because the POSTURE is a
                        // fact of its own that the class rule and the repeat-box identity test
                        // both read.
                        .digit = bracket.has_value() ? digit_slot(*bracket, string, *stop)
                                                     : std::optional<StopMarkSlot>{},
                    });
            }
        }
        state.shapes.push_back(
            ShapeViewState{
                .start_seconds = tempo_map.secondsAtWholeNote(place),
                // BOTH ENDS, from the one site that owns them. The DRAWN extent is where shape rule
                // 6's trim is taken and the only place it is (\ref drawnShapeExtent), which is why
                // the spans resolve after the hand's moves; the CLOSE is what the walk stored,
                // published verbatim so the editor's reveal has the truth to reach for and no
                // surface has to undo the trim to get it.
                .drawn_end_seconds = tempo_map.secondsAtWholeNote(
                    place + drawnShapeExtent(shape, resolutions.margins, move_starts)),
                .close_seconds = tempo_map.secondsAtWholeNote(place + shape.sustain),
                // A strummed chord is a box; sequential arrival, or a posture string ringing
                // through the start un-restruck, renders as arpeggio brackets.
                .arpeggio = arrivals[shape_index],
                .strings = std::move(strings),
                .bracket_seconds =
                    bracket.has_value()
                        ? std::optional<double>{tempo_map.secondsAtWholeNote(*bracket)}
                        : std::nullopt,
                .held = shape.hold_stated,
            });
    }

    // A none hold forms no span of its own, so it is published beside the spans; a none hold always
    // fits its notes, so its stated extent is the one it draws.
    for (const ShapeHold& hold : chart.shape_holds)
    {
        if (std::holds_alternative<NoneGrip>(hold.grip))
        {
            state.none_holds.push_back(
                NoneHoldViewState{
                    .start = hold.start,
                    .end = hold.end,
                    .start_seconds = tempo_map.secondsAt(hold.start),
                    .end_seconds = tempo_map.secondsAt(hold.end),
                });
        }
    }

    // The 2D lane's visible-range indexes, over the further of each event's two ends
    // (\ref ChartViewState::ring_end_prefix_max).
    state.ring_end_prefix_max =
        makeSustainPrefixMax(state.notes | std::views::transform(&NoteViewState::ring_end_seconds));
    state.shape_close_prefix_max =
        makeSustainPrefixMax(state.shapes | std::views::transform(&ShapeViewState::close_seconds));
    return state;
}

} // namespace rock_hero::common::core
