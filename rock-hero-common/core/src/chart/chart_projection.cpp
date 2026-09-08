#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <rock_hero/common/core/chart/chart_legato.h>
#include <rock_hero/common/core/chart/chart_presentation.h>
#include <rock_hero/common/core/chart/chart_projection.h>
#include <rock_hero/common/core/chart/chart_rules.h>
#include <rock_hero/common/core/chart/grid_arithmetic.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

namespace
{

// RULE 12A, and this is the only place it lives (user ruling 2026-09-04). A span's DRAWN extent
// keeps the minimum sustain distance before the head that closed it — the same margin every other
// drawn element keeps, so consecutive shapes show the gap everything else shows instead of butting
// exactly. What the derivation stores is the MUSICAL CLOSE (\ref ChartShape::sustain): the instant
// the statement actually ended, which is what the spans themselves are measured against and what a
// figure's seams have to abut at. Trimming there put a display margin inside every seam.
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
//   retreat behind the strum it is drawn over. At anything faster than a sixteenth the closing
//   onset crowds inside the margin, and a box trimmed blindly would stop short of its own last
//   head;
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
    if (!closing.has_value())
    {
        return shape.sustain;
    }
    // The margin at the CLOSING ONSET's own measure: it is that head's spacing that is being kept,
    // and a meter change between the span's front and its close would otherwise take the wrong one.
    const Fraction margin =
        minimumSustainDistanceBeats(tempo_map.timeSignatureAt(closing->measure).denominator);
    const Fraction limit = beatDistance(tempo_map, shape.position, *closing) - margin;
    const Fraction trimmed = std::max(std::min(shape.sustain, limit), shape.stated_extent);
    return Fraction{} < trimmed ? trimmed : shape.sustain;
}

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
    state.open_strings = chart.tuning.strings;
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
    //
    // WHO PRINTS the displaced digit is the hand's question, and it is the user's ruling on both
    // halves (2026-09-07, THE PLANT'S FACE). The bracket's number is the one statement that the
    // left hand is on that string at all, so under a RIGHT-hand head the bracket prints the held
    // stop itself, standing whatever its authorship, and the note's face defers to it
    // (\ref StopMarkFace::Posture). A FRETTING-hand head states the hand's presence with its own
    // number, so the stop planted beneath it is the refinement the notation already prints in the
    // pull-off, and the NOTE wears it as its own reveal-only satellite (\ref chartHeldStops): the
    // bracket then prints nothing on that string, so exactly one ink states it. A fretting-hand
    // head that holds no second stop at all — an artificial harmonic pressing the fret its head
    // does not print — has no face of its own, so the bracket prints its pressed fret, standing.
    //
    // Asked of the PRESENTED stream in either form, for the arrival rule's own reason: whether a
    // string sounds is a fact about the chart, not about which tails the caller drew. The held
    // table is index-parallel to it, as every resolution is.
    const auto digit_slot = [&presented_notes, &resolutions](
                                const GridPosition& at,
                                const int string,
                                const ChartStop& stop) -> std::optional<StopMarkSlot> {
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
            const ChartStop printed =
                soundingStopAt(head->harmonic_node, head->attack, head->fret, head->fret);
            if (printed == stop)
            {
                return std::nullopt;
            }
            // The note's own face states a fretting-hand head's plant, so the bracket does not —
            // asked as an EQUALITY with the posture's stop rather than as the plant's presence,
            // because the two agree by construction today (the planting strike's posture entry IS
            // its plant) and a two-place agreement is exactly what a test should not assume: were
            // they ever to differ, the honest picture is two facts in two inks, never silence.
            // Bound once so the presence test and the read are provably the same object.
            const auto index = static_cast<std::size_t>(head - presented_notes.begin());
            const std::optional<int>& held = resolutions.held_stops[index];
            if (!rightHandOnset(head->attack) && held.has_value() && frettedStop(*held) == stop)
            {
                return std::nullopt;
            }
            return StopMarkSlot::Satellite;
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
                        // member no head right there already states — at its own place, or as the
                        // plant that head wears as its own face. A
                        // span drawing no bracket prints no digit anywhere, which is exactly the
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
                // reach for and no surface has to undo the trim to get it (user ruling 2026-09-04).
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
        state.display_hold_ends.push_back(tempo_map.secondsAtGlobalBeatPosition(
            onset_beat + resolutions.holds[note_index].toDouble()));
        // THE TAIL LAW's verdict, carried per note so the board knows which ribbons the curtain
        // owns PART of and from where (\ref NoteViewState::rested): the one reading both
        // distance-scoped consumers share (\ref hasRestingRemainder), so a member resting at its
        // own end — a handover — publishes no window and the board draws it as any unrested
        // ribbon. Never set in the ACTUAL reveal: that form exists to show the ring the chart
        // stores, so nothing in it rests.
        // Bound once so the presence test and the read below are provably the same object.
        const std::optional<Fraction>& rested_from = resolutions.rested_from[note_index];
        view.rested = form == ChartNoteForm::Presented && hasRestingRemainder(rested_from, note);
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
        // The COMPLETE resolved held stop, copied straight across (\ref chartHeldStops): under a
        // right-hand onset the authored value, the one a pull-off derives over it (user ruling
        // 2026-08-31, DERIVED HELD), or — where the chart states neither — THE DEFAULT FACT of the
        // tap, the grip the covering span holds on its string (user ruling 2026-09-02); under a
        // fretting-hand onset the stop a pull-off PLANTS beneath it (user ruling 2026-09-07, THE
        // PLANT'S FACE). Which notes carry one is a rule the resolution owns rather than one this
        // pass re-applies: a silent hold's claim IS its own fret, and this field never carried it.
        view.held = resolutions.held_stops[note_index];
        // THE FACE THIS NOTE'S CLAIMED STOP WEARS — where its ink draws, and on what terms it
        // shows (user ruling 2026-08-31, THE SATELLITE REVEAL). The two shapes of claim wear two
        // different faces, so they are published apart rather than through one gate that could
        // only ever fit one of them.
        //
        // A HELD STOP'S FACE IS ITS OWN SATELLITE, at the note's own slot, for EVERY note carrying
        // a resolved stop — mid-span taps and span-less claims included, and a fretting-hand
        // source's plant. What that replaced was a gate on the digit's COLUMN, which published a
        // face only where the SPAN's bracket happened to print one and left every other held stop
        // faceless.
        //
        // Bound to a local so the presence test and the read below are provably the same object.
        if (const std::optional<int>& held = view.held; held.has_value())
        {
            // Standing where the charter AUTHORED the stop, revealed where the chart did not state
            // it at all — ONE rule over that whole class whatever derived it, enumerated once on
            // \ref StopMarkFace::Revealed and deliberately not restated here. What every member of
            // it shares is that no charter typed the value: a pull-off in the notation or the
            // covering span's posture answered instead, so it waits for the reader to ask. Either
            // way the reveal shows the whole truth about the note at once.
            //
            // Asked of the RESOLUTIONS rather than of the stored field, because who states a stop
            // is exactly what those walks answer and a value comparison cannot: a claim present
            // that no pull-off plants is the authored one, and everything else is answered by
            // something other than the charter. The wide table is the one ownership authority
            // (\ref ChartResolutions::planted_stops); a fretting-hand note claims nothing, so it
            // can only ever answer Revealed here.
            double mark_seconds = view.start_seconds;
            const bool authored = resolutions.claimed_stops[note_index].has_value() &&
                                  !resolutions.planted_stops[note_index].has_value();
            StopMarkFace face = authored ? StopMarkFace::Standing : StopMarkFace::Revealed;
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
            //
            // A DEFAULT never reaches here, by construction rather than by a test: this face is
            // owed by the span a note's CLAIM joined, and a tap that states nothing joins none
            // (ChartShapes::claim_shapes is absent for it). So a default wears the note's own
            // satellite even where its value coincides with the posture digit beside it — which is
            // what the ruling asks for (user, 2026-09-02). Nor does a fretting-hand source's PLANT,
            // for the same reason and one more: a plant is no claim, and the slot rule leaves the
            // bracket's digit absent on a string whose head wears the stop as its own face, so the
            // column test below could not pass either (THE PLANT'S FACE, user ruling 2026-09-07).
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
