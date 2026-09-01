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

    // WHERE a posture string states its fret, decided per string by what heads that string AT THE
    // INSTANT THE BRACKET DRAWS — the posture-smart rule, which lived in the tab painter until the
    // drawn digit needed a hit target (user ruling 2026-08-27). Answered here so the painter's
    // column and the click's column come from one statement instead of two derivations free to
    // disagree.
    //
    // THE DIGIT WINDOW is that one instant and nothing besides (user ruling 2026-08-31). A head
    // LATER in the span never suppresses, because the opening bracket is the span's CHORD FRAME:
    // it states the whole membership at the moment the reader meets it, so members that accumulate
    // in afterwards print their frets there exactly as the ones already down do. Asking over the
    // whole span instead emptied an accumulation's frame of everything still to arrive, and its
    // inclusive end let the very onset that CLOSED the span decide the digits inside it.
    //
    // Three answers, one question about the one head this instant can carry. NOTHING heads the
    // string: the bracket's centre is the whole of what states the stop — the silent-string case
    // and the carried-ring case alike, and the case a member arriving later is in. A head PRINTING
    // THIS NUMBER states it already, so the bracket prints nothing beside it. A RIGHT-HAND onset
    // printing ANOTHER number keeps the centre because it is what rings, and the fretting hand's
    // stop — still true — takes the column beside the bracket. THE FRET IS PART OF THE TEST on
    // every arm: what suppresses a digit is a head printing this very fret.
    //
    // Asked of the PRESENTED stream in either form, for the arrival rule's own reason: whether a
    // string sounds is a fact about the chart, not about which tails the caller drew.
    const auto digit_slot = [&presented_notes](
                                const GridPosition& at,
                                const int string,
                                const int fret) -> std::optional<StopMarkSlot> {
        // The ONE head this string can carry here: the stream is sorted by (position, string) and
        // refuses duplicate onsets (\ref ChartErrorCode::UnsortedOrDuplicateNotes), so the first
        // match is the only match and there is never a second answer to reconcile with it.
        for (auto head = std::ranges::lower_bound(
                 presented_notes, at, std::ranges::less{}, &ChartNote::position);
             head != presented_notes.end() && head->position == at;
             ++head)
        {
            if (head->string != string)
            {
                continue;
            }
            // A silent hold heads nothing: it draws no number anywhere, so the bracket's own
            // column is the whole of what that stop has.
            if (silentHold(head->attack))
            {
                break;
            }
            if (rightHandOnset(head->attack) && head->fret != fret)
            {
                return StopMarkSlot::Satellite;
            }
            if (head->fret == fret)
            {
                return std::nullopt;
            }
            return StopMarkSlot::Bracket;
        }
        return StopMarkSlot::Bracket;
    };

    // WHERE a span's one opening mark draws, or nothing where it draws none. Bound once here
    // because two passes ask it — the span pass that publishes the bracket, and the note pass that
    // asks whether a tap FRONTS one — and a second spelling of the arrival gate is exactly how the
    // two would come to disagree about whether a mark exists.
    const auto bracket_position =
        [&resolutions, &arrivals](const std::size_t shape_index) -> std::optional<GridPosition> {
        return arrivals[shape_index] ? resolutions.shapes[shape_index].bracket_position
                                     : std::optional<GridPosition>{};
    };

    for (std::size_t shape_index = 0; shape_index < resolutions.shapes.size(); ++shape_index)
    {
        const ChartShape& shape = resolutions.shapes[shape_index];
        const double start_beat = globalBeatPosition(tempo_map, shape.position);
        // WHERE the span's one opening mark draws, TAKEN FROM THE WALK ([D2] amendment 2, refined
        // by review F7; published as \ref ChartShape::bracket_position). Every span an EVENT states
        // carries its own FRONT; a CARRY-OPENED successor — one a landing or a member's death
        // founded — carries its first interior SOUNDING instead, because nothing at all is stated
        // at a boundary; one that never sounds interiorly carries nothing and draws no mark.
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
        const std::optional<GridPosition> bracket = bracket_position(shape_index);

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
                        // Asked AT the mark's own instant, which is the whole window (THE DIGIT
                        // WINDOW above): the bracket is the span's chord frame, so it states every
                        // member whose head is not already printing that number right there. A
                        // span drawing no bracket prints no digit anywhere, which is exactly the
                        // empty slot — the posture entry itself stays, because the POSTURE is a
                        // fact of its own that the class rule and the repeat-box identity test
                        // both read.
                        .digit = bracket.has_value() ? digit_slot(*bracket, string, *fret)
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
        // The RESOLVED held stop, never the stored field (user ruling 2026-08-31, DERIVED HELD): a
        // pull-off off a right-hand onset STATES the stop the other hand was holding under it, so
        // the resolution is a fact about the note's NEIGHBOUR and \ref chartClaimedStops is the one
        // reader that knows it. The attack is what says whose stop it is — a silent hold's claim IS
        // its own fret, which this field has never carried.
        view.held =
            rightHandOnset(note.attack) ? resolutions.claimed_stops[note_index] : std::nullopt;
        // THE FACE THIS NOTE'S CLAIMED STOP WEARS — where its ink draws, and on what terms it
        // shows (user ruling 2026-08-31, THE SATELLITE REVEAL). The two shapes of claim wear two
        // different faces, so they are published apart rather than through one gate that could
        // only ever fit one of them.
        //
        // A HELD STOP'S FACE IS ITS OWN SATELLITE, at the note's own slot, for EVERY right-hand
        // onset carrying a resolved stop — mid-span taps and span-less claims included. What that
        // replaced was a gate on the digit's COLUMN, which published a face only where the SPAN's
        // bracket happened to print one and left every other held stop faceless.
        //
        // Bound to a local so the presence test and the read below are provably the same object.
        if (const std::optional<int>& held = view.held; held.has_value())
        {
            // Standing where the stop is AUTHORED, revealed where the notation DERIVES it: a
            // pull-off already prints that fret, so a standing digit would state it twice, and the
            // reveal shows the whole truth about the note at once. Asked of the derivation itself
            // (ChartResolutions::derived_stops) rather than of the stored field, because who states
            // a stop is exactly what that walk answers and a value comparison cannot.
            double mark_seconds = view.start_seconds;
            StopMarkFace face = resolutions.derived_stops[note_index].has_value()
                                    ? StopMarkFace::Revealed
                                    : StopMarkFace::Standing;
            // THE ONE EXCEPTION, and it is [D2]'s displaced digit: a tap FRONTING its span's
            // bracket has that bracket printing its stop, because the tap's own head holds the
            // string's centre there. The bracket OWES the statement, so the stop stands whatever
            // its authorship and this note draws nothing of its own beside it — and the face
            // carries the BRACKET's instant, the very number the bracket pass positions the digit
            // with, so print and click stay one decision (user ruling 2026-08-31).
            //
            // Both halves are the test: the mark draws at this note's own position, AND the span's
            // digit for this string went to the satellite column there. Reading the column alone
            // would let a tap further along the span claim the face the FRONT tap's head displaced.
            if (const std::optional<std::size_t>& shape_index =
                    resolutions.claim_shapes[note_index];
                shape_index.has_value() && *shape_index < state.shapes.size())
            {
                const ShapeViewState& span = state.shapes[*shape_index];
                // Bound once so the presence test and the read are provably the same object.
                const std::optional<double>& bracket_seconds = span.bracket_seconds;
                const auto entry =
                    std::ranges::find(span.strings, note.string, &ShapeStringViewState::string);
                if (bracket_seconds.has_value() &&
                    bracket_position(*shape_index) == note.position &&
                    entry != span.strings.end() && entry->digit == StopMarkSlot::Satellite)
                {
                    mark_seconds = *bracket_seconds;
                    face = StopMarkFace::Posture;
                }
            }
            view.stop_mark = StopMarkViewState{
                .seconds = mark_seconds,
                // A held stop's face IS the satellite column, beside the bracket or beside its own
                // head; the slot is only ever a question for the hold below.
                .slot = StopMarkSlot::Satellite,
                .face = face,
            };
        }
        // A SILENT hold IS the bracket: the bars are its face, drawn for every posture string
        // wherever its span's mark draws, so it needs no digit to be selectable and the column only
        // says how far its extent runs. A span drawing NO bracket ([D2] amendment 2) leaves it
        // faceless, which is the whole of why nothing undrawn is clickable for it — a property this
        // pass owes rather than a rule a surface enforces. The span index comes from the derivation
        // rather than being searched for here, and a note claiming no stop at all leaves this
        // absent, because a sounding note's face is its own head at its own instant.
        //
        // Bound to a local so the optional check and the access are provably the same object.
        else if (
            const std::optional<std::size_t>& shape_index = resolutions.claim_shapes[note_index];
            shape_index.has_value() && *shape_index < state.shapes.size()
        )
        {
            const ShapeViewState& span = state.shapes[*shape_index];
            // Bound once so the presence test and the read are provably the same object.
            const std::optional<double>& bracket_seconds = span.bracket_seconds;
            // THE COLUMN THIS HOLD'S DIGIT IS DRAWN IN, read off the very entry that decided it
            // prints, so print and click are ONE decision and the hold's clickable extent covers
            // exactly what was drawn. What stood here asked instead whether the SPAN started at
            // this note — a proxy that missed a deferred bracket entirely.
            const auto entry =
                std::ranges::find(span.strings, note.string, &ShapeStringViewState::string);
            const std::optional<StopMarkSlot> column =
                entry == span.strings.end() ? std::optional<StopMarkSlot>{} : entry->digit;
            if (bracket_seconds.has_value())
            {
                view.stop_mark = StopMarkViewState{
                    // The bracket's own instant, which a deferred one moves off the span's start.
                    // The face IS that bracket, so it goes where the bracket went.
                    .seconds = *bracket_seconds,
                    .slot = column.value_or(StopMarkSlot::Bracket),
                    .face = StopMarkFace::Posture,
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
