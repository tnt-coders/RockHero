#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// Where a fret-hand placement's approach ramp begins when the placement lands exactly on a glide
// arrival, keyed by the keyframe's advanced grid position. A placement sitting on a keyframe ties
// its ramp to that glide's own segment, so a drawn hand travels with the drawn rail instead of on
// an unrelated metrical margin. UNPITCHED trail-off ends are recorded too, and carry their family
// so the hand eases with the same curve the rail uses. Chord slides record identical values under
// one key.
struct SlideRamp
{
    double start_seconds{0.0};
    bool unpitched{false};
};

// Walks every glide in a note stream once and records where each arrival's segment begins.
//
// A pass of its own rather than a table filled inside the note loop below, because the ramps are
// the PRESENTED stream's answer whichever form that loop projects (chart_projection.h): when the
// fretting hand starts moving is a fact about the chart, and a hand marker that shifted the
// instant the editor's Alt reveal swapped note forms would be reporting the swap rather than the
// chart. Separating it is what lets the note loop read exactly one stream.
[[nodiscard]] std::map<GridPosition, SlideRamp> makeSlideRampStarts(
    const std::vector<ChartNote>& notes, const TempoMap& tempo_map)
{
    std::map<GridPosition, SlideRamp> starts;
    for (const ChartNote& note : notes)
    {
        // A scrape renders through the unpitched machinery end to end and never feeds the
        // slide-locked ramps: it has no fret-hand anchor to ramp. A note carrying no glide at all
        // — nearly every note — leaves before a single position is resolved.
        if (isScrape(note.attack) ||
            (!anyKeyframeStatesFret(note.keyframes) && !note.slide_out.has_value()))
        {
            continue;
        }

        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        double segment_start_seconds = tempo_map.secondsAtGlobalBeatPosition(onset_beat);
        int segment_start_fret = note.fret;
        for (const Keyframe& keyframe : note.keyframes)
        {
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
            // it leaves from). Tying a placement's ramp to a hold's span made the hand drift the
            // whole held stretch to arrive at a fret it never left, so holds fall through to the
            // margin morph. The segment start still advances, which is what gives the following
            // glide its true, shorter span.
            if (*fret != segment_start_fret)
            {
                starts.try_emplace(
                    advanceGridPosition(tempo_map, note.position, keyframe.offset),
                    SlideRamp{.start_seconds = segment_start_seconds, .unpitched = false});
            }
            segment_start_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + keyframe.offset.toDouble());
            segment_start_fret = *fret;
        }
        // The trail-off's own segment starts where the last stated fret left off (the note's onset
        // when there are none) and ends where the RING does, which is exactly the span the rail is
        // drawn over. Recording it ties the hand to that span and marks the family so the ease
        // matches too.
        if (note.slide_out.has_value())
        {
            starts.try_emplace(
                advanceGridPosition(tempo_map, note.position, note.sustain),
                SlideRamp{.start_seconds = segment_start_seconds, .unpitched = true});
        }
    }
    return starts;
}

} // namespace

ChartViewState makeChartViewState(
    const Arrangement& arrangement, const TempoMap& tempo_map, ChartNoteForm form)
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
    const ChartResolutions resolutions = chartResolutions(chart.notes, tempo_map);
    const std::vector<ChartNote>& presented_notes = resolutions.presented_notes;

    // The ONE place the form is read. It selects the stream the per-note VIEW fields below come
    // from and nothing else in this function: the holds, the ramps, the span arrivals and their
    // postures all keep reading the presented stream, which is what makes the two forms differ in
    // `notes` alone. The editor's actual-ring reveal is the only caller asking for the saved form,
    // and it draws that form as ordinary notation — techniques riding the real ring, with the
    // payload presentation clipped restored, which a view-side end swap could not put back.
    const std::vector<ChartNote>& drawn_notes =
        form == ChartNoteForm::Actual ? resolutions.connections.saved_notes : presented_notes;

    // Where each fret-hand placement's approach ramp begins, from the presented stream in either
    // form; asked once for the whole chart and read by the placement pass at the bottom.
    const std::map<GridPosition, SlideRamp> slide_ramp_starts =
        makeSlideRampStarts(presented_notes, tempo_map);

    // The span pass runs BEFORE the notes: a claim's mark is a column of the bracket its fret went
    // into, so the note loop below reads the answer this pass publishes rather than deciding it a
    // second time from the same inputs.
    state.shapes.reserve(resolutions.shapes.size());
    // The shared arrival rule, answered for every span once per chart revision beside the spans
    // themselves (\ref ChartResolutions::arrivals) — the absorption rule keys on the same answer,
    // so deriving it here as well would be the class asked twice. It reads the presented stream for
    // the attacks it still derives (the right-hand onsets inside a span) and takes the rest off the
    // spans, where the walk recorded it against the STORED rings — the class is a fact about the
    // hands, and E25 governs what a surface draws of a ring rather than what the hands did (user
    // ruling 2026-08-28).
    const std::vector<bool>& arrivals = resolutions.arrivals;

    // WHERE a posture string states its fret, decided per string by whether any HEAD inside the
    // span states it — the posture-smart rule, which lived in the tab painter until the drawn digit
    // needed a hit target (user ruling 2026-08-27). Answered here so the painter's column and the
    // click's column come from one statement instead of two derivations free to disagree.
    //
    // TWO facts about one string decide it, and the whole span is the window for both.
    //
    // THE FRONT PRINTS NO LIE (user ruling 2026-08-31, THE ACCUMULATION LAW): heads draw at
    // ARRIVALS and rails run from the FRONT, so a member that SOUNDS gets no digit — its own head
    // states its fret wherever in the span that head falls. Asking at one instant was right only
    // while every struck member sounded there; an accumulation's members arrive one at a time by
    // definition, and a digit decided at the front would print a stack for members whose heads are
    // about to state themselves, the bracket claiming what the notes say.
    //
    // A CLAIM keeps its face whatever else the string does, and that is not an exception to the
    // rule above but the other half of it: a silently-held stop draws NO head anywhere, so the
    // bracket digit is the whole of what the charter's record has — the editor places its hit box
    // from this very answer, and a claim with no face would be an authored record that is stored,
    // undrawn and unclickable. A later sounding of the same stop states that instant; it does not
    // state that the finger was already down at the front, which is exactly what the hold says.
    //
    // The SATELLITE keeps the bracket's own instant, and that is not an inconsistency: it is the
    // column drawn BESIDE the mark, so a tap somewhere else in the span says nothing about where
    // that column goes. One question about the span, one about the mark's own slot.
    //
    // Asked of the PRESENTED stream in either form, for the arrival rule's own reason: whether a
    // string sounds is a fact about the chart, not about which tails the caller drew.
    const auto digit_slot = [&presented_notes](
                                const GridPosition& at,
                                const GridPosition& front,
                                const GridPosition& end,
                                const int string,
                                const int fret) -> std::optional<StopMarkSlot> {
        bool claimed = false;
        bool sounded = false;
        for (auto head = std::ranges::lower_bound(
                 presented_notes, front, std::ranges::less{}, &ChartNote::position);
             head != presented_notes.end() && !(end < head->position);
             ++head)
        {
            if (head->string != string)
            {
                continue;
            }
            if (silentHold(head->attack))
            {
                claimed = true;
                continue;
            }
            if (rightHandOnset(head->attack))
            {
                // The right hand is what rings, so it keeps the centre; the fretting hand has NOT
                // moved, so its stop is still true and takes the column beside the bracket — at
                // the mark's own slot, which is where that column is drawn.
                if (head->position == at && head->fret != fret)
                {
                    return StopMarkSlot::Satellite;
                }
                sounded = sounded || head->fret == fret;
                continue;
            }
            // A fretting-hand head states this string's fret with its own number, wherever inside
            // the span it falls. THE FRET IS PART OF THE TEST, because the window runs to the
            // span's own end and a span the exact-adjacency fallback floors there ends ON the very
            // onset that closed it: a head at ANOTHER fret is that closing contradiction, and it
            // states the next statement's stop rather than this one's.
            sounded = sounded || head->fret == fret;
        }
        // The centre a fret number belongs in, for the two strings that need one: a stop the hand
        // CLAIMS, and a ring carried in from outside the span with no head of its own inside it.
        return claimed || !sounded ? std::optional{StopMarkSlot::Bracket}
                                   : std::optional<StopMarkSlot>{};
    };

    for (std::size_t shape_index = 0; shape_index < resolutions.shapes.size(); ++shape_index)
    {
        const ChartShape& shape = resolutions.shapes[shape_index];
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // WHERE the span's one opening mark draws, TAKEN FROM THE WALK ([D2] amendment 2, refined
        // by review F7; published as \ref ChartShape::bracket_position). Every span an EVENT states
        // carries its own start; a LANDING-OPENED span carries its first interior SOUNDING, because
        // a chord slide keeps the fingers planted and all that happens at the landing is the
        // fingers arriving; one that never sounds interiorly carries nothing and draws no mark.
        //
        // Read rather than re-scanned: a scan here for "the first sounding at or after the span's
        // start" was the walk's own grouping question asked a second time, against an extent the
        // closing trim has already shortened.
        //
        // ONE condition gates it, and this is the one: a bracket is ARPEGGIO furniture. A box-class
        // span states itself with its strums' own boxes and opens no mark of its own — which since
        // the 2026-08-30 successor ruling is the ordinary disposition of a landing successor, not a
        // corner of one. Everything downstream keys on this optional: the deferred bracket, the
        // claim's published face, and the coincidence rule that suppresses a chord box under an
        // arpeggio box all ask "is a mark drawn here", and there is one answer to ask.
        //
        // Bound once so the presence test and every read below are provably the same object.
        const std::optional<GridPosition> bracket =
            arrivals[shape_index] ? shape.bracket_position : std::optional<GridPosition>{};
        // The span's last covered instant, for the digit rule's head scan. Advanced from the
        // span's own position by its own sustain, which is the one arithmetic that answers it.
        const GridPosition span_end = advanceGridPosition(tempo_map, shape.position, shape.sustain);

        std::vector<ShapeStringViewState> strings;
        if (shape.posture < resolutions.postures.size())
        {
            const ChartPosture& posture = resolutions.postures[shape.posture];
            // Posture array index 0 is the lowest string.
            for (std::size_t index = 0; index < posture.frets.size(); ++index)
            {
                // Bound to a local so the optional check and the access are provably the same
                // object (bugprone-unchecked-optional-access cannot track repeated indexing).
                const std::optional<int>& fret = posture.frets[index];
                if (!fret.has_value())
                {
                    continue;
                }
                const int string = static_cast<int>(index) + 1;
                strings.push_back(
                    ShapeStringViewState{
                        .string = string,
                        .fret = *fret,
                        // Asked over the SPAN, with the mark's own instant carried alongside for
                        // the satellite column: a struck member's head states its fret wherever it
                        // arrives, and a deferred bracket still has to place a claim's column
                        // where it is actually drawn. A span drawing no bracket prints no digit
                        // anywhere, which is exactly the empty slot — the posture entry itself
                        // stays, because the POSTURE is a fact of its own that the class rule and
                        // the repeat-box identity test both read.
                        .digit = bracket.has_value()
                                     ? digit_slot(*bracket, shape.position, span_end, string, *fret)
                                     : std::optional<StopMarkSlot>{},
                    });
            }
        }
        state.shapes.push_back(
            ShapeViewState{
                .start_seconds = tempo_map.secondsAtGlobalBeatPosition(start_beat),
                .end_seconds =
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

    // Note onsets ascend — presentation moves no note, so they ascend in either form — and the
    // forward cursor resolves them in amortized constant time. Sustain ends and intra-note payload
    // offsets can jump past later onsets, so those use the plain resolver instead of a second
    // cursor.
    TempoMap::ForwardBeatTimeCursor onset_cursor{tempo_map};
    state.notes.reserve(drawn_notes.size());
    state.display_hold_ends.reserve(drawn_notes.size());
    for (std::size_t note_index = 0; note_index < drawn_notes.size(); ++note_index)
    {
        const ChartNote& note = drawn_notes[note_index];
        const double onset_beat = globalBeatPosition(tempo_map, note.position);
        NoteViewState view;
        view.start_seconds = onset_cursor.secondsAt(onset_beat);
        view.end_seconds =
            note.sustain.numerator > 0
                ? tempo_map.secondsAtGlobalBeatPosition(onset_beat + note.sustain.toDouble())
                : view.start_seconds;
        // C3: where the covering span's furniture already owns this member's whole ring, its own
        // ribbon does not draw at all (\ref chartSuppressedTails). Ink only — end_seconds above is
        // the whole ring either way, so hit testing, culling and the hold keep measuring it. The
        // answer needs no resolving to seconds, and that is the ruling's doing: all or nothing per
        // note means there is no instant part way along the ring for either surface to find.
        //
        // The ACTUAL form takes none of it, which is the reveal's whole point: what the reader
        // asked to see is exactly the ring the picture was hiding, so hiding it again would answer
        // the wrong question. That makes this the one per-note fact the two forms disagree about
        // besides the tail's end, and the form branch below stays the only place the streams part.
        view.tail_suppressed =
            form != ChartNoteForm::Actual && resolutions.suppressed_tails[note_index];
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.holds[note_index].toDouble()));
        view.string = note.string;
        view.fret = note.fret;
        view.attack = note.attack;
        view.held = note.held;
        // The mark that states this note's CLAIMED stop: the posture bracket of the span the claim
        // joined, and the column its digit printed in. Both are read off the span this pass already
        // projected rather than re-derived — the instant IS where that bracket draws, and the slot
        // IS the entry the claim's fret went into — so the mark can never sit
        // where nothing was drawn. The span index comes from the derivation rather than being
        // searched for here, and a note claiming no stop leaves it absent, because a sounding
        // note's face is its own head at its own instant.
        //
        // Bound to a local so the optional check and the access are provably the same object.
        if (const std::optional<std::size_t>& shape_index = resolutions.claim_shapes[note_index];
            shape_index.has_value() && *shape_index < state.shapes.size())
        {
            // A HELD stop's mark is the note's OWN, so it is published only where the span it
            // joined starts at this note. Elsewhere the stop still prints — as the shape's
            // ordinary posture digit in that span's bracket — but that digit belongs to the span
            // rather than to this record, and publishing a mark for it would make a column
            // clickable where nothing of this note's is drawn. A silent hold needs no such test:
            // its own face IS that bracket wherever it draws.
            //
            // A span drawing NO bracket ([D2] amendment 2) publishes no face either: there is
            // nothing drawn for a click to land on, and "nothing undrawn is clickable" is a
            // property this pass owes rather than a rule a surface enforces.
            const ShapeViewState& span = state.shapes[*shape_index];
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<double>& bracket_seconds = span.bracket_seconds;
            if (bracket_seconds.has_value() &&
                (!view.held.has_value() ||
                 resolutions.shapes[*shape_index].position == note.position))
            {
                const auto entry =
                    std::ranges::find(span.strings, note.string, &ShapeStringViewState::string);
                view.stop_mark = StopMarkViewState{
                    // The bracket's own instant, which a deferred one moves off the span's start.
                    // The face IS that bracket, so it goes where the bracket went.
                    .seconds = *bracket_seconds,
                    // A string whose digit prints nowhere still shows the bracket bars, and those
                    // are the column the mark occupies — which is what keeps a silently-held stop
                    // selectable there without making an undrawn digit clickable.
                    .slot = entry == span.strings.end()
                                ? StopMarkSlot::Bracket
                                : entry->digit.value_or(StopMarkSlot::Bracket),
                };
            }
        }
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
        // The vibrato channel resolved into the REGIONS it states, folded through the same one
        // authority every other reader of the channel uses (`RingState` in chart.h). It is a state
        // that holds from each statement until the next, so a surface needs the stretch it covers
        // and the WIDTH over it, not a flag: this walks the statements and closes a region wherever
        // the state changes, at the ring's end when it never does. Each region carries the width it
        // was stated at, so a shake that steps to the wide tier mid-ring is two regions and neither
        // surface needs the channel's rules a second time.
        //
        // Old content falls out of the same walk with no case of its own, which is what makes the
        // two surfaces draw it exactly as they always did: a shake stated at the onset and never
        // restated opens here and closes at `end_seconds`, one region covering the whole presented
        // tail. A region opening exactly at that end is kept — degenerate, drawing nothing, and
        // still the honest answer that this channel says the string shakes.
        RingState ring = ringStateAtOnset(note);
        double shake_start_seconds = view.start_seconds;
        for (const Keyframe& keyframe : note.keyframes)
        {
            const double keyframe_seconds =
                tempo_map.secondsAtGlobalBeatPosition(onset_beat + keyframe.offset.toDouble());
            const VibratoState was = ring.vibrato;
            ring.advance(keyframe);
            if (ring.vibrato != was)
            {
                // Closing and opening are asked SEPARATELY rather than as an either/or, because a
                // step from one width to the other does both at this instant: the narrow region
                // ends here and the wide one starts here. An `if/else` would have hidden that case
                // behind whichever arm it happened to take, leaving the whole step drawn at the
                // width the note opened with.
                if (isShaking(was))
                {
                    view.vibrato.push_back(
                        VibratoSpanViewState{
                            .start_seconds = shake_start_seconds,
                            .end_seconds = keyframe_seconds,
                            .state = was,
                        });
                }
                if (isShaking(ring.vibrato))
                {
                    shake_start_seconds = keyframe_seconds;
                }
            }
            // Bound to locals so each optional check and its access are provably the same object.
            const std::optional<double>& bend = keyframe.bend;
            if (bend.has_value())
            {
                view.bend.push_back(
                    BendPointViewState{.seconds = keyframe_seconds, .semitones = *bend});
            }
            const std::optional<int>& fret = keyframe.fret;
            if (fret.has_value())
            {
                view.slides.push_back(
                    KeyframeViewState{
                        .seconds = keyframe_seconds,
                        .fret = *fret,
                        .offset = keyframe.offset,
                    });
            }
        }
        if (isShaking(ring.vibrato))
        {
            view.vibrato.push_back(
                VibratoSpanViewState{
                    .start_seconds = shake_start_seconds,
                    .end_seconds = view.end_seconds,
                    .state = ring.vibrato,
                });
        }
        // The terminal is carried as the terminal (W9-L): it happens at the ring's end by
        // definition, so it has no offset of its own to state and is not one of the stops along
        // the way. Consumers that want the gesture as one uniform sequence read it through
        // glideStopAt, which is where the old flatten went.
        if (const int* const slide_out = slideOutFretOrNull(note); slide_out != nullptr)
        {
            view.slide_out = *slide_out;
        }
        state.notes.push_back(std::move(view));
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
