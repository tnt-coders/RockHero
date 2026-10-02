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
#include <rock_hero/common/core/shared/visible_events.h>
#include <utility>
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

// RULE 12A, and this is the only place it lives. A span's DRAWN extent keeps the minimum sustain
// distance before the head that closed it — the same margin every other drawn element keeps, so
// consecutive shapes show the gap everything else shows instead of butting exactly. What the
// derivation stores is the MUSICAL CLOSE (\ref ChartShape::sustain): the instant the statement
// actually ended, which is what the spans themselves are measured against and what a figure's seams
// have to abut at. Trimming there would put a display margin inside every seam.
//
// THREE FACTS, and each answers a case the others cannot:
//
//   the CLOSING HEAD (\ref ChartShape::closing_onset) is what the distance is kept from, and it is
//   not the close: a span whose rings died a full margin early ends where they died and is not
//   pulled back from a head it never reached. Absent where there is no head at all — a span whose
//   statement simply ran out, and a close at a slot of held fingers, which sounds nothing to keep a
//   distance from and is exactly what keeps a landing successor tiled onto its predecessor;
//
//   the LAST STATEMENT (\ref ChartShape::stated_extent) floors the trim, because furniture may not
//   retreat behind the strum it is drawn over. In a fast enough passage the closing onset crowds
//   inside the margin, and a box trimmed blindly would stop short of its own last head;
//
//   PROTECTED ADJACENCY, where even that leaves nothing: a statement made at an instant is drawn
//   however crowded, so it falls back to the musical close itself — exact adjacency, mirroring the
//   sustain rules' own precedent. The one span nothing states at an instant never reaches here,
//   because a close leaving it no room is what deletes it in the walk ([D2] edge (b)).
//
// The extent is therefore positive exactly where the musical close is, and never past it.
[[nodiscard]] Fraction drawnShapeExtent(const ChartShape& shape, const TempoMap& tempo_map)
{
    // Bound to a local so the presence test and every read below are provably the same object.
    const std::optional<GridPosition>& closing = shape.closing_onset;
    std::optional<Fraction> limit;
    if (closing.has_value())
    {
        // The margin at the CLOSING ONSET: it is that head's spacing that is being kept, and a
        // tempo change between the span's front and its close would otherwise measure it at the
        // wrong rate.
        limit = beatDistance(tempo_map, shape.position, *closing) -
                minimumSustainDistanceBeats(tempo_map, *closing);
    }
    return drawnHoldExtent(shape.sustain, limit, shape.stated_extent);
}

// The approach a fret-hand placement rides when the placement lands exactly on a glide arrival,
// keyed by the keyframe's grid position: where the leg into that arrival starts, and whether it
// eases like unpitched travel. The arrival itself is the placement's own instant — nothing
// presentation decides moves a statement — so the hand travels with the rail and completes where
// the sound goes.
struct SlideRamp
{
    double start_seconds{0.0};
    bool unpitched{false};
    // The note whose rail the ramp follows: its ink end is where the settle begins.
    std::size_t note{0};
};

// Walks every glide in the stored stream once and records the segment each arrival rides. A pass
// of its own rather than a table filled inside the note loop below, because the key is a grid
// position the fret-hand pass looks a ramp up by, and the note loop resolves seconds.
//
// The SLIDE-OUT is the RESOLVED fact: a slide-out and a shift slide's arrival are the same
// statement at the same place, and only the relation tells them apart (\ref arrivesIntoNextHead).
// Asking position alone would ease every arrival with the slide-out curve.
[[nodiscard]] std::map<GridPosition, SlideRamp> makeSlideRampStarts(
    const ChartConnections& connections, const TempoMap& tempo_map)
{
    const std::vector<ChartNote>& notes = connections.saved_notes;
    std::map<GridPosition, SlideRamp> starts;
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

        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        double segment_start_seconds = tempo_map.secondsAtGlobalBeatPosition(onset_beat);
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
            if (!fret.has_value())
            {
                continue;
            }
            // An equal-fret keyframe is a HOLD, not a glide — nothing travels across it (the
            // pitch is pinned, which is how a slide notated on a tied continuation records where
            // it leaves from). Tying a placement's ramp to a hold's span would make the hand drift
            // the whole held stretch to arrive at a fret it never left, so holds fall through to
            // the margin morph. The segment start still advances, which is what gives the following
            // glide its true, shorter span. The slide-out's segment starts where the last sounded
            // fret left off and ends where the ring does, and is marked unpitched so the ease
            // matches the slide-out.
            const bool unpitched = slides_out && keyframe_index + 1 == note.keyframes.size();
            const double keyframe_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + keyframe.offset.toDouble());
            if (*fret != segment_start_fret || unpitched)
            {
                const SlideRamp ramp{
                    .start_seconds = segment_start_seconds,
                    .unpitched = unpitched,
                    .note = index,
                };
                // A PITCHED ramp outranks an unpitched one at a shared key: a slide-out on one
                // string and a shift slide's arrival on another can end on the same head, and the
                // placement standing there is the hand LANDING. The key names no string, so the tie
                // is settled here rather than by which note the walk happened to reach first.
                SlideRamp& filed =
                    starts
                        .try_emplace(
                            advanceGridPosition(tempo_map, note.position, keyframe.offset), ramp)
                        .first->second;
                if (filed.unpitched && !unpitched)
                {
                    filed = ramp;
                }
            }
            segment_start_seconds = keyframe_seconds;
            segment_start_fret = *fret;
        }
    }
    return starts;
}

} // namespace

ChartViewState makeChartViewState(const Arrangement& arrangement, const TempoMap& tempo_map)
{
    ChartViewState state;
    if (!arrangement.chart.has_value())
    {
        return state;
    }

    const Chart& chart = *arrangement.chart;
    state.open_strings = chart.tuning.strings;
    state.capo = chart.tuning.capo;

    // Every per-note fact this projection derives comes from the one resolutions pass: the stored
    // stream it draws, where each ring's ink stops, each note's resolved connection motion, and
    // each note's hold. It is derived from the saved form, so a pick slide's in-memory overrides
    // (chart.h) are already stripped and the scrape draws as the scrape it is.
    const ChartResolutions resolutions = chartResolutions(chart.notes, tempo_map);
    const std::vector<ChartNote>& notes = resolutions.connections.saved_notes;

    // Where each fret-hand placement's approach ramp begins; asked once for the whole chart and
    // read by the placement pass at the bottom.
    const std::map<GridPosition, SlideRamp> slide_ramp_starts =
        makeSlideRampStarts(resolutions.connections, tempo_map);

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
    const auto digit_slot = [&notes](
                                const GridPosition& at,
                                const int string,
                                const ChartStop& stop) -> std::optional<StopMarkSlot> {
        // The ONE head this string can carry here: the stream is sorted by (position, string) and
        // refuses duplicate onsets (\ref ChartErrorCode::UnsortedOrDuplicateNotes), so the first
        // match is the only match and there is never a second answer to reconcile with it.
        for (auto head =
                 std::ranges::lower_bound(notes, at, std::ranges::less{}, &ChartNote::position);
             head != notes.end() && head->position == at;
             ++head)
        {
            if (head->string != string)
            {
                continue;
            }
            const ChartStop printed =
                soundingStopAt(head->harmonic_node, head->attack, head->fret, head->fret);
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
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // WHERE the span's one opening mark draws, TAKEN FROM THE WALK ([D2]; published as \ref
        // ChartShape::bracket_position). Every span an EVENT states carries its own FRONT; a
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
        const std::optional<GridPosition> bracket =
            arrivals[shape_index] ? shape.bracket_position : std::optional<GridPosition>{};

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
                .start_seconds = tempo_map.secondsAtGlobalBeatPosition(start_beat),
                // BOTH ENDS, from the one site that owns them. The DRAWN extent is where rule 12a's
                // margin is taken and the only place it is (\ref drawnShapeExtent); the CLOSE is
                // what the walk stored, published verbatim so the editor's reveal has the truth to
                // reach for and no surface has to undo the trim to get it.
                .drawn_end_seconds = tempo_map.secondsAtGlobalBeatPosition(
                    start_beat + drawnShapeExtent(shape, tempo_map).toDouble()),
                .close_seconds =
                    tempo_map.secondsAtGlobalBeatPosition(start_beat + shape.sustain.toDouble()),
                // A strummed chord is a box; sequential arrival, or a posture string ringing
                // through the start un-restruck, renders as arpeggio brackets.
                .arpeggio = arrivals[shape_index],
                .strings = std::move(strings),
                .bracket_seconds =
                    bracket.has_value()
                        ? std::optional<double>{tempo_map.secondsAtGlobalBeatPosition(
                              globalBeatPosition(tempo_map, *bracket))}
                        : std::nullopt,
            });
    }

    // Note onsets ascend, and the forward cursor resolves them in amortized constant time. Ring
    // ends and intra-note payload offsets can jump past later onsets, so those use the plain
    // resolver instead of a second cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    state.notes.reserve(notes.size());
    state.display_hold_ends.reserve(notes.size());
    for (std::size_t note_index = 0; note_index < notes.size(); ++note_index)
    {
        const ChartNote& note = notes[note_index];
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        NoteViewState view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.ring_end_seconds =
            tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble());
        // Resolved through the same expression as the ring end so a free tail's two ends are one
        // number, which is what lets a surface test "draws in full" exactly.
        const Fraction& ink_end = resolutions.ink_end[note_index];
        view.ink_end_seconds =
            ink_end.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + ink_end.toDouble())
                : view.start_seconds;
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.holds[note_index].toDouble()));
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
            view.reveal_from_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + rested_from->toDouble());
            // The board's reveal-window depth, resolved here because tempo is not on the
            // renderer's read surface: \ref g_tail_reveal_lead_whole_note at this onset's own
            // meter (\ref tailRevealLeadBeats), SHORTENED at the song's front to the room
            // available — a positive-but-smaller lead that compresses the curtain rather than
            // disabling it. Only an onset at the song's very first instant yields a zero lead,
            // and the renderer treats that one note as not rested at all.
            const double lead_beat = std::max(
                0.0,
                onset_beat - tailRevealLeadBeats(
                                 tempo_map.timeSignatureAt(note.position.measure).denominator)
                                 .toDouble());
            view.reveal_lead_seconds =
                view.start_seconds - tempo_map.secondsAtGlobalBeatPosition(lead_beat);
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
            const double keyframe_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + keyframe.offset.toDouble());
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

    // Every placement gets an eased approach ramp: a slide-matched placement ramps over its glide
    // segment so a drawn hand travels with the note, any other placement morphs over the shared
    // minimum-sustain-distance margin before the arrival, and crowded transitions shorten against
    // the previous arrival rather than overlapping it. The synthetic pre-first nut window
    // counts as arriving at the chart origin. Each window's reach is derived once for the whole
    // stream from the stops the notes state under it (deriveFretHandWidths); only the index
    // finger's fret is stored.
    const std::vector<int> fhp_widths =
        deriveFretHandWidths(resolutions, chart.fret_hand_positions, tempo_map);
    state.fret_hand_positions.reserve(chart.fret_hand_positions.size());
    for (std::size_t fhp_index = 0; fhp_index < chart.fret_hand_positions.size(); ++fhp_index)
    {
        const FretHandPosition& fhp = chart.fret_hand_positions[fhp_index];
        const double arrival_seconds =
            tempo_map.secondsAtGlobalBeatPosition(globalBeatPosition(tempo_map, fhp.position));
        double ramp_start_seconds = 0.0;
        bool unpitched_ramp = false;
        // The settle is the stretch of the glide past its rail's ink end: the rail is drawn to
        // the crop, the hand completes at the arrival, and the board eases the difference.
        double settle_seconds = 0.0;
        if (const auto slide = slide_ramp_starts.find(fhp.position);
            slide != slide_ramp_starts.end())
        {
            ramp_start_seconds = slide->second.start_seconds;
            unpitched_ramp = slide->second.unpitched;
            settle_seconds =
                std::max(0.0, arrival_seconds - state.notes[slide->second.note].ink_end_seconds);
        }
        else
        {
            ramp_start_seconds = tempo_map.secondsAtGlobalBeatPosition(
                globalBeatPosition(tempo_map, marginBefore(tempo_map, fhp.position)));
        }
        // A ramp never reaches back past the previous arrival: crowded transitions shorten
        // against it rather than overlapping it.
        const double previous_arrival_seconds = state.fret_hand_positions.empty()
                                                    ? tempo_map.secondsAtBeat(1, 1)
                                                    : state.fret_hand_positions.back().seconds;
        ramp_start_seconds =
            std::clamp(ramp_start_seconds, previous_arrival_seconds, arrival_seconds);
        state.fret_hand_positions.push_back(
            FhpViewState{
                .seconds = arrival_seconds,
                .fret = fhp.fret,
                .width = fhp_widths[fhp_index],
                .end_authored = fhp.end_fret.has_value(),
                .ramp_seconds = arrival_seconds - ramp_start_seconds,
                .unpitched_ramp = unpitched_ramp,
                .settle_seconds = settle_seconds,
            });
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
