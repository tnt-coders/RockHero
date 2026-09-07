/*!
\file chart_view_state.h
\brief Seconds-resolved chart content shared by the 2D tablature lane and the 3D highway.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/chart/chart.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Which column a posture bracket states a stop in.

The two slots a posture digit can occupy, and the answer is a property of a (span, string) rather
than of either alone: what HEADS the string AT THE INSTANT THE MARK DRAWS, and at that instant
alone, is what decides it (THE DIGIT WINDOW, user ruling 2026-08-31). That instant is the span's
FRONT for every span an EVENT states and its first interior sounding for a carry-opened successor
([D2] amendment 2), so the question is asked where the reader is actually looking rather than over a
window the span's own closing onset could reach into. Published by the projection so the painter and
the hit test read one answer — the digit is drawn exactly where it is clickable, which is the whole
of "nothing drawn is unreachable" for this mark.
*/
enum class StopMarkSlot : std::uint8_t
{
    /*!
    \brief Inside the bracket bars, where a fret number belongs.

    The answer wherever no head at the mark's own instant already prints this string's number: the
    silently-held stop, the ring carried in from outside, the member that ACCUMULATES IN LATER, and
    the fretting-hand head that stands there printing ANOTHER fret — a contradiction states the
    next stop, not this one. A bracket is the span's CHORD FRAME: it states the whole membership at
    the moment the reader meets it, so a head further along the span never takes a digit out of it.

    Also the answer where no digit prints at all — a head at the mark's instant printing this very
    fret, which is the one thing suppression exists for. Wherever a bracket draws, its bars are
    drawn for every posture string, so the mark still occupies this column and nothing else does. A
    span drawing NO bracket — a box-class one, or a successor that never sounds interiorly —
    publishes no slot at all rather than an empty one.
    */
    Bracket,

    /*!
    \brief The satellite column outboard of the closing bar.

    Where a right-hand onset heads the string AT THE MARK'S OWN INSTANT printing a DIFFERENT fret:
    the tap keeps the centre because it is what rings, and the fretting hand's stop — still true —
    takes the column beside the bracket. The FRONT TAP, in other words, which is the only tap that
    displaces anything: one further along the span leaves the mark's own slot empty, so its stop
    prints in the bracket as an ordinary membership digit (user ruling 2026-08-31).
    */
    Satellite,
};

/*!
\brief Whose ink states a claimed stop, and on what terms that ink is shown.

THE SATELLITE REVEAL LAW (user ruling 2026-08-31, extended 2026-09-02). A satellite is the note's
own held FACE, and whether it stands is a question about AUTHORSHIP rather than about where in a
span the note sits: an authored statement earns standing ink wherever it lies, while a stop the
charter did not write waits for the reader to ask. TWO stops are in that second class and one rule
covers both — the one a pull-off DERIVES, whose fret the notation already prints, and THE DEFAULT
under a bare tap, read live off the covering span's posture (\ref chartHeldStops). What the reveal
shows is the whole truth about one note at once, which is why the terms below are the same ones its
real ring is shown on.

Three answers, because "shown" and "who draws it" are one question here: a face the SPAN's own
furniture already prints is drawn wherever that furniture is, and one the note prints for itself is
drawn on the note's terms. Consumers ask \ref stopMarkShown for presence and this for the painter's
half, so the drawn digit and the clickable one stay one record.
*/
enum class StopMarkFace : std::uint8_t
{
    /*!
    \brief The span's own posture furniture states it, so it is drawn wherever that is.

    Two shapes of one answer. A \ref NoteAttack::None hold IS the bracket — the bars are its face,
    and they draw for every posture string. And a tap FRONTING a bracket has its stop printed by
    that bracket, displaced into \ref StopMarkSlot::Satellite because the tap's own head holds the
    string's centre there ([D2]): the bracket OWES the statement, so the stop stands whether it was
    authored or derived, and the note draws nothing of its own beside it.
    */
    Posture,

    /*!
    \brief The note's OWN satellite, standing: drawn whenever the note is.

    An AUTHORED held stop, anywhere it sits. A mid-span tap therefore carries TWO marks and they say
    different things: its fret prints in the opening bracket as grip MEMBERSHIP (the digit window),
    and this is the note's own face — what a press addresses and a typed digit retypes.
    */
    Standing,

    /*!
    \brief The note's own satellite, shown only while the note's truth is revealed.

    A stop the CHARTER did not write, which is two stops under one rule. A DERIVED one
    (\ref chartDerivedStops): the pull-off notation already prints that fret, so a standing digit
    would state it twice, and it is READ-ONLY — the derivation owns the stop and the retype verbs
    refuse it. And THE DEFAULT under a bare tap (\ref chartHeldStops, user ruling 2026-09-02): the
    grip the covering span holds on its string, or 0 where nothing does. That one is owned by
    NOBODY, so it is the opposite of read-only — typing at it authors a real held stop, and its
    whole point is to be the held channel's target on a tap that previously had none.

    Revealing the note shows the whole truth about it at once, so this appears exactly while its
    real ring does — the editor's selection-and-reveal pick, which the host answers, this core
    never learns, and no game surface makes at all.
    */
    Revealed,
};

/*! \brief One bend curve point resolved to an absolute timeline second. */
struct BendPointViewState
{
    /*! \brief Absolute timeline position of this curve point. */
    double seconds{0.0};

    /*! \brief Bend amount in semitones at this point. */
    double semitones{0.0};

    /*!
    \brief Compares two bend points by their stored fields.
    \param lhs Left-hand point.
    \param rhs Right-hand point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(
        const BendPointViewState& lhs, const BendPointViewState& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on a
        // floating member, which is why every float-bearing view state here is spelled out. Exact
        // equality is intended; the ordering query expresses it warning-free with identical
        // semantics (NaN compares unequal either way).
        return std::is_eq(lhs.seconds <=> rhs.seconds) &&
               std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*!
\brief One stretch of a note's ring the vibrato channel states as shaking, in absolute seconds.

The channel is a STATE that holds from each statement until the next (\ref Keyframe), so what a
surface has to draw is an interval carrying a WIDTH rather than a flag: a shake can start at a
glide's arrival, widen mid-hold, stop, and start again, and one boolean could say none of it. The
projection reads the channel once and hands both surfaces the same regions, which is what keeps the
lane's sine and the board's wobble covering the same stretch of the same note at the same tier.

A note whose shake runs end to end — every chart written before the keyframe model, and most
written after — yields exactly one region spanning the whole presented tail, so the surfaces draw
what they always drew without a case of their own.
*/
struct VibratoSpanViewState
{
    /*! \brief Absolute timeline position the shake begins. */
    double start_seconds{0.0};

    /*!
    \brief Absolute timeline position the shake stops: the next statement, or the ring's end.

    Equal to \ref start_seconds only where a statement lands exactly on the end the note presents,
    which draws nothing on either surface and still reports the region the channel states.
    */
    double end_seconds{0.0};

    /*!
    \brief How wide the string shakes over this region.

    Never \ref VibratoState::Off: a region exists exactly where the channel says the string shakes,
    so the off value ENDS one rather than describing one. Carried per region rather than per note
    because the channel can step between the widths mid-ring, and the surfaces scale their swing
    from this — the one place either of them learns which tier it is drawing.
    */
    VibratoState state{VibratoState::Narrow};

    /*!
    \brief Compares two vibrato regions by their stored fields.
    \param lhs Left-hand region.
    \param rhs Right-hand region.
    \return True when both regions store equal values.
    */
    friend constexpr bool operator==(
        const VibratoSpanViewState& lhs, const VibratoSpanViewState& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on a
        // floating member. Exact equality is intended; the ordering query expresses it
        // warning-free with identical semantics.
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.state == rhs.state;
    }
};

/*!
\brief One keyframe's POSITION statement, resolved to an absolute timeline second.

The fret channel alone: a keyframe stating only a bend or a vibrato change says nothing about
where the hand is, so it reaches the surfaces through \ref NoteViewState::bend and
\ref NoteViewState::vibrato instead and never appears here. The falls-away terminal is not here
either — it is \ref NoteViewState::slide_out, because it is the ring's END rather than a stop
along the way (W11), and a list holding both would have to say which entry was which.
*/
struct KeyframeViewState
{
    /*! \brief Absolute timeline position the glide reaches its target fret. */
    double seconds{0.0};

    /*! \brief Target fret reached at this keyframe. */
    int fret{0};

    /*!
    \brief The keyframe's authored offset along the ring — its stable identity.

    Carried beside the resolved second because the second cannot name the keyframe back: it is a
    rounded double derived through the tempo map, while the editor's selection keys a keyframe by
    (note slot, offset) and must match the authored `Keyframe::offset` exactly — one producer for
    chart content an editing surface has to point at.

    Stable under sibling edits, which an index would not be: removing an earlier keyframe shifts
    every later index and moves no offset.
    */
    Fraction offset{};

    /*!
    \brief Compares two slide keyframes by their stored fields.
    \param lhs Left-hand keyframe.
    \param rhs Right-hand keyframe.
    \return True when both keyframes store equal values.
    */
    friend constexpr bool operator==(
        const KeyframeViewState& lhs, const KeyframeViewState& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.offset == rhs.offset;
    }
};

/*! \brief Where a claimed stop's face draws, which column it occupies, and on what terms. */
struct StopMarkViewState
{
    /*!
    \brief Absolute timeline position the face draws at.

    WHERE ITS OWN INK IS, which is not one anchor for every face: a \ref StopMarkFace::Posture one
    draws where the span's bracket does, so it carries that bracket's instant and comes from the
    very number the bracket pass positions with; a face the NOTE prints for itself sits beside its
    own head, so it carries the note's onset. One field, one meaning — where this stop is stated.
    */
    double seconds{0.0};

    /*!
    \brief Which column the face occupies.

    Only ever a question for a \ref NoteAttack::None hold, whose face is the bracket ITSELF: its
    box runs out to cover \ref StopMarkSlot::Satellite where its own digit was displaced into that
    column, and stops at the bars where it was not. A note carrying a held stop always answers
    Satellite — a held stop's face IS that column, beside the bracket or beside its own head.
    */
    StopMarkSlot slot{StopMarkSlot::Bracket};

    /*! \brief Whose ink states the stop, and on what terms it is shown (\ref StopMarkFace). */
    StopMarkFace face{StopMarkFace::Posture};

    /*!
    \brief Compares two stop marks by their stored fields.
    \param lhs Left-hand mark.
    \param rhs Right-hand mark.
    \return True when both marks store equal values.
    */
    friend constexpr bool operator==(
        const StopMarkViewState& lhs, const StopMarkViewState& rhs) noexcept
    {
        // Hand-written for the float member, like every other float-bearing view state here.
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.slot == rhs.slot &&
               lhs.face == rhs.face;
    }
};

/*!
\brief Whether a claimed stop's face is on show, given whether its note's truth is revealed.

The ONE presence rule for a stop mark, so the painter that draws the digit, the layout that bounds
it, the hit test that reaches it and the caret that types into it cannot disagree about whether
there is one: a \ref StopMarkFace::Revealed face waits for the reveal and every other face stands.
The reveal itself is the host's per-note pick — the same one that swaps a note to its real ring —
and is never a fact this core holds.

\param mark The stop mark being asked about.
\param revealed True when this note's whole truth is on show.

\return True when the face is drawn, and therefore reachable.
*/
[[nodiscard]] constexpr bool stopMarkShown(
    const StopMarkViewState& mark, const bool revealed) noexcept
{
    return mark.face != StopMarkFace::Revealed || revealed;
}

/*!
\brief One sounding note resolved to timeline seconds, in the form its \ref ChartViewState carries.

Normally the PRESENTED form, not the stored one. `ChartNote::sustain` is the actual duration the
string rings, and what a surface draws is derived from it once per chart revision by
\ref presentedChartNotes — the tail trimmed to clear the next head, floored on payload that still
says something, dropped where it was never a deliberate sustain, absent on a dead note, and
marked RESTING from its last always-visible landmark where a span stands at its onset
(
ef rested). Every field here comes from
that derivation, so `end_seconds`, the bend curve, the slide keyframes, the vibrato regions and the
flattened slide-out all describe the presented note and nothing has to trim a second time.

The form is the form its state was projected in (\ref ChartNoteForm), never a per-note choice: a
state carries one form throughout, and the two differ in these notes and in nothing else around
them. Presentation touches the tail alone, so positions, strings, frets, techniques and flags read
the same in either form; `end_seconds`, the bend curve, the slide keyframes and the vibrato
regions are the four a reader must not assume are the presented ones — a region running to the
ring's end runs to the end THIS form presents.

**Scored = presented** (`docs/plans/in-progress/note-sustain-model.md`, ruling 4). When the scorer
exists it reads this, not the chart: what the player is asked to hold is exactly what the board
showed them. The contract holds structurally rather than by discipline, because
\ref makeHighwayViewState composes \ref makeChartViewState with no form argument: every state a
game surface can obtain is the presented one, and \ref ChartNoteForm::Actual is unreachable from
the board, the game and the scorer. That contract is also why the derivation lives in common/core
rather than in a painter — the game must be able to reach it without a surface.
*/
struct NoteViewState
{
    /*! \brief Absolute onset position. */
    double start_seconds{0.0};

    /*!
    \brief Absolute end of the tail; equals start_seconds when the note's form presents none.

    The DRAWN and scored length, never the stored ring: a sub-quarter chug rings for its eighth and
    presents nothing. This is the whole of what the 2D lane draws; the 3D board additionally pins a
    span-held strum's heads past it (\ref ChartViewState::display_hold_ends).

    ONE end per note, and both surfaces draw to it — there is no second per-note LENGTH for a
    surface to read differently, the tail law included: since the execution-form amendment it can
    only MARK this (\ref rested), never move or empty it, so a resting ring carries its
    rules-1-to-4 end here like every other and the board's rest/reveal modulates alpha alone.

    In the editor reveal's \ref ChartNoteForm::Actual state it is the stored ring instead, so it is
    strictly later than the onset for every note there (the positive-sustain invariant) and the
    equals-the-onset case simply does not arise.
    */
    double end_seconds{0.0};

    /*!
    \brief True where this ribbon RESTS: the board draws its resting part only inside the reveal.

    THE TAIL LAW's verdict (\ref presentedChartNotes; generalized 2026-09-06), carried per note
    because "no tail" and "a tail the furniture carries" are different facts and only the
    derivation can tell them apart. Since the execution-form amendment (user ruling 2026-09-03)
    \ref end_seconds carries the rules-1-to-4 end here like everywhere else — one length, this
    verdict beside it. The 2D lane draws the ribbon regardless; the 3D board draws the portion
    before \ref reveal_from_seconds always and the remainder only inside the reveal window,
    which is the one distance-scoped draw decision the amendment deliberately re-admits.

    False in the \ref ChartNoteForm::Actual reveal, where the whole point is the ring the chart
    stores: nothing rests in the form that exists to show the truth. False, too, for a member
    whose landmark is its own end — a handed-over member under a span, whose statement finishes
    at the takeover: the curtain owns none of its ribbon, so the projection publishes no window
    (\ref hasRestingRemainder) and the board draws it as any unrested ribbon.
    */
    bool rested{false};

    /*!
    \brief Where this tail's resting remainder begins, in timeline seconds.

    The note's rested-from offset (\ref ChartPresentation::rested_from) resolved onto the clock:
    equal to \ref start_seconds where the whole ribbon rests, the end of the informative payload
    where a technique plays out and the plain remainder joins the curtain. The board anchors the
    note's local reveal window HERE rather than at the head, so the stated portion stays always
    visible and the curtain owns everything past it. Meaningful only beside a true \ref rested,
    and zero everywhere else so a stray read is inert.
    */
    double reveal_from_seconds{0.0};

    /*!
    \brief Depth in seconds of the board's sliding reveal window for this note; 0 where
    \ref rested is false.

    \ref g_tail_reveal_lead_whole_note resolved at this note's own meter and tempo into real
    time, published here because tempo is not on the renderer's read surface. The
    board draws a resting remainder only where it lies within this window of its anchor, with the
    alpha gradient full at the anchor and zero at the window's outer edge, so ink materializes
    continuously as it scrolls in; everything past the window emits no geometry at all.
    Meaningful only beside a true \ref rested, and zero everywhere else so a stray read is inert.
    */
    double reveal_lead_seconds{0.0};

    /*!
    \brief One-based chart string, counted from the lowest-pitched string.

    Never shifted for display: a surface's "show at least N strings" minimum adds empty lanes
    BELOW the chart's strings, and each surface maps chart strings onto those lanes when it lays
    out (\ref displayedLane) — so the scene holds one chart fact and the two surfaces cannot
    disagree about which string a note is on.
    */
    int string{1};

    /*!
    \brief Fret sounded; zero is the open string — or a natural harmonic, whose position lives in
    \ref harmonic_node instead. Ask \ref openString rather than testing this against zero.
    */
    int fret{0};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*!
    \brief The fretting-hand stop under a right-hand onset; absent elsewhere, never absent there.

    The COMPLETE resolved stop (\ref chartHeldStops), not the stored `ChartNote::held`, and three
    tiers fold into it in this order. An AUTHORED value is what the charter typed. A pull-off off a
    right-hand onset STATES the stop the other hand was holding under it, so that DERIVATION
    supersedes the field wherever the notation already says the fret (user ruling 2026-08-31,
    DERIVED HELD). And where the chart states neither, THE DEFAULT answers (user ruling 2026-09-02):
    a tap says nothing about the other hand, so the hand is holding whatever grip the covering span
    holds — its posture's fret on this string, or 0, the open string, where no span covers the note
    or the posture names no fret there. Every consumer reads this one answer, which is what keeps
    all three the same kind of statement on every surface.

    PRESENT FOR EVERY RIGHT-HAND ONSET, therefore, and absent on every other note — the ATTACK
    decides whose stop this is, and a silent hold's claim is its own \ref fret, which this field has
    never carried. Presence no longer says the chart states a stop; it says the question arises.
    WHICH tier answered it is \ref stop_mark's \ref StopMarkFace, and WHERE the stop is drawn —
    beside the bracket, inside it, or nowhere — is that same mark's answer, so a satellite is never
    inferred from this field alone.
    */
    std::optional<int> held{};

    /*!
    \brief The face that states this note's CLAIMED stop — where it draws, and on what terms.

    What it MEANS differs by which shape the claim takes, and both read the one mark. A
    \ref NoteAttack::None note has no head and no tail, so the bracket printing its stop IS its
    face — what the pointer selects and what a typed fret writes to — and a hold's claim is stated
    where its SPAN'S mark draws, which is not in general where the note was authored, nor, since
    [D2]'s amendment 2, where the span begins. A note carrying \ref held has a head of its own, and
    the stop it holds wears a SATELLITE beside that head: its own face at its own slot.

    EVERY HELD STOP HAS A FACE (user ruling 2026-08-31, THE SATELLITE REVEAL). A satellite is the
    note's own held face at the note's own slot, note-scoped, so this is present for every
    right-hand onset — mid-span and span-less claims included, and, since the default gives a bare
    tap a stop of its own (user ruling 2026-09-02), taps that state nothing too — and
    \ref StopMarkFace says whose ink states it and on what terms it shows. What that replaced was a
    gate on the digit's COLUMN, which published a face only where the span's own bracket happened
    to print one: it was the stopgap for "no reader exists", and the readers now exist.

    Two facts about a mid-span tap, and they are not the same fact: its fret prints in the opening
    bracket as grip MEMBERSHIP (\ref ShapeStringViewState::digit, the digit window, unchanged and
    independent), and its satellite here is the note's own face — what a press addresses and a
    typed digit retypes. An AUTHORED stop stands; a DERIVED one and a DEFAULT one show while the
    note's truth is revealed, because neither is the charter's ink — the pull-off notation already
    prints the one fret, and the posture already prints the other. A default wears its own satellite
    even where its value coincides with the posture digit beside it, because the two are different
    statements about the same fret.

    PRINT AND CLICK ARE ONE DECISION (user ruling 2026-08-31). \ref stopMarkShown is the one
    presence rule every consumer asks, and \ref seconds carries the instant its own ink draws at,
    so a drawn digit is reachable and an undrawn one is not — by construction rather than by a
    second rule. What stood here before all of it asked whether the span STARTED at this note, a
    proxy that answered nothing about what was drawn and missed a deferred bracket whole.

    A silent hold's face is the bracket ITSELF: the bars draw for every posture string, so it is
    always a \ref StopMarkFace::Posture face and \ref slot only says how far its extent runs.

    The SLOT rides the instant rather than sitting beside it, because the two are one fact and a
    reader that had them apart could hit-test a column the digit was never printed in — and
    \ref StopMarkSlot::Satellite is what makes a hold's displaced digit reachable: the mark's
    clickable extent then runs out to cover the column it was actually drawn in, which is the
    drawn-digit-clicks-nowhere gap closed by construction (user ruling 2026-08-27).

    A HOLD that joined no span — one past its span's end, one on a string the sound already states,
    one whose span dissolved unjustified — carries none, because its face was that span's bracket
    and no bracket is drawn: "nothing undrawn is clickable" again by construction. A held stop is
    not gated that way, its face being its own; the span it reached is read from the derivation
    (\ref ChartShapes::claim_shapes) and never re-derived here, and it decides only whether the
    BRACKET owes the statement. Absent on every note that claims no stop at all, whose face is its
    own head at its own instant.
    */
    std::optional<StopMarkViewState> stop_mark{};

    /*!
    \brief What this note's connection claim resolves to (\ref resolveLegato).

    The chart stores a claim and never a direction, so the drawn hammer-on or pull-off mark can only
    come from here. `Unjustified` is not a state of its own on either surface: a claim nothing
    justifies draws exactly what the plain pick beside it draws, which is why no cue exists and why
    every attack outside \ref legatoClaimable leaves this at its default.
    */
    LegatoMotion legato{LegatoMotion::Unjustified};

    /*! \brief True when the picking hand's palm damps the string (`ChartNote::palm_mute`). */
    bool palm_mute{false};

    /*! \brief True when the string is deadened into an unpitched click (`ChartNote::dead`). */
    bool dead{false};

    /*!
    \brief Harmonic node in fret units, and the assertion that this note is a harmonic.

    Mirrors `ChartNote::harmonic_node`, carrying the chart's exact node point (the 3.2 / 2.7 /
    5.8 family): presence is the whole test, and every harmonic carries one, a pinch included
    (rule-enforced). The highway places a harmonic head at the true node; the lane, which has no
    fretboard axis, selects the diamond head from it and prints the node as the head's label.

    A pinch's node lies off the neck where the thumb grazes, so ask `nodeIsOnNeck` before
    anchoring a head to it or labeling a head with it. Both surfaces today draw only a pinch's
    fretted stop — its left-hand half — and how the right-hand node will be shown is an open
    question, not a ruling.
    */
    std::optional<double> harmonic_node{};

    /*! \brief True when the note is tremolo picked. */
    bool tremolo{false};

    /*! \brief How hard the note is struck relative to its neighbours. */
    NoteEmphasis emphasis{NoteEmphasis::Normal};

    /*! \brief Bend curve points in ascending time order; empty when not bent. */
    std::vector<BendPointViewState> bend;

    /*!
    \brief The keyframes that state a POSITION, in ascending time order; empty when nothing travels.

    The falls-away terminal is NOT among them — it is \ref slide_out. It used to be flattened on as
    one more keyframe so consumers had one uniform segment model, and the model is still one, now as
    a READ rather than as data: \ref glideStopCount and \ref glideStopAt walk the keyframes and the
    terminal as one sequence, so the uniform view survives while the state stops calling the ring's
    end a stop along the way. Whether a stop is unpitched follows from the note's attack and its
    place in the sequence, which is why no entry here carries a flag saying so.
    */
    std::vector<KeyframeViewState> slides;

    /*!
    \brief Fret the note's unpitched falls-away gestures toward; absent when the tail simply ends.

    Carries no time of its own: a slide-out ends the note, so it lands at \ref end_seconds by
    definition (W11 deleted the stored offset for the same reason). A scrape's terminal is its
    required end and reads here like any other.
    */
    std::optional<int> slide_out{};

    /*!
    \brief The stretches of the tail the string shakes over, in ascending time order.

    Empty when the note never shakes, which is what "is this note played with vibrato" asks now
    that the channel can start and stop mid-ring (\ref VibratoSpanViewState). Declared beside the
    other two tail payloads because it is one: the regions are clipped to the tail this state's
    FORM presents, exactly as the bend curve and the slide keyframes are.
    */
    std::vector<VibratoSpanViewState> vibrato;

    /*!
    \brief Compares two note view states by their stored fields.
    \param lhs Left-hand note.
    \param rhs Right-hand note.
    \return True when both notes store equal values.
    */
    friend bool operator==(const NoteViewState& lhs, const NoteViewState& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.end_seconds <=> rhs.end_seconds) && lhs.rested == rhs.rested &&
               lhs.string == rhs.string && lhs.fret == rhs.fret && lhs.attack == rhs.attack &&
               lhs.stop_mark == rhs.stop_mark && lhs.legato == rhs.legato &&
               lhs.palm_mute == rhs.palm_mute && lhs.dead == rhs.dead &&
               lhs.harmonic_node == rhs.harmonic_node && lhs.tremolo == rhs.tremolo &&
               lhs.emphasis == rhs.emphasis && lhs.bend == rhs.bend && lhs.slides == rhs.slides &&
               lhs.slide_out == rhs.slide_out && lhs.vibrato == rhs.vibrato;
    }
};

/*! \brief One stop of a note's drawn gesture: a keyframe's arrival, or the falls-away terminal. */
struct GlideStop
{
    /*! \brief Absolute timeline position the gesture reaches this stop. */
    double seconds{0.0};

    /*! \brief Fret reached here. */
    int fret{0};

    /*!
    \brief True when this stop is unpitched travel rather than a pitched arrival.

    NOT "the glide trails off here". A pick slide's every stop carries it, because a scrape's whole
    path is unpitched travel — the turnarounds included — so reading it as an ending mis-draws every
    scrape. The terminal carries it too, and there the release reading does hold.
    */
    bool unpitched{false};
};

/*!
\brief How many stops a note's drawn gesture has: its position keyframes plus any terminal.

The uniform segment model every geometry consumer walks — the rail, the tail's sample times, the
camera's framing, the lane's diagonals. It is a read rather than a stored list so the terminal can
stay what it is (\ref NoteViewState::slide_out) without every consumer restating "and then the
trail-off"; pairing it with \ref glideStopAt keeps the walk allocation-free on the per-frame path.

\param note Note whose gesture is being walked.
\return Number of stops; zero for a note that never travels.
*/
[[nodiscard]] inline std::size_t glideStopCount(const NoteViewState& note) noexcept
{
    return note.slides.size() + (note.slide_out.has_value() ? 1U : 0U);
}

/*!
\brief One stop of a note's drawn gesture, by index into the uniform sequence.

Indices below `note.slides.size()` are the position keyframes in time order; the one index past
them is the terminal, which sits at the ring's end.

\param note Note whose gesture is being walked.
\param index Stop index, below \ref glideStopCount for this note.
\return The stop's time, fret and pitched-ness.
*/
[[nodiscard]] inline GlideStop glideStopAt(const NoteViewState& note, const std::size_t index)
{
    if (index < note.slides.size())
    {
        const KeyframeViewState& keyframe = note.slides[index];
        return GlideStop{
            .seconds = keyframe.seconds,
            .fret = keyframe.fret,
            // A scrape's travel is the PICKING hand's, so every stop on it is unpitched; on any
            // other note a stated position is a stop the finger arrives at.
            .unpitched = isScrape(note.attack),
        };
    }
    // The terminal. `value_or` rather than a dereference: the count above admits this index only
    // when the note carries one, and stating that as a fallback keeps the access unconditional
    // instead of resting on a guard a reader (or a checker) has to tie back to the count.
    return GlideStop{
        .seconds = note.end_seconds,
        .fret = note.slide_out.value_or(note.fret),
        .unpitched = true,
    };
}

/*!
\brief True when the glide continues the same note at this keyframe rather than ending it.

Decided by the keyframe's place in the sustain and nothing else: strictly inside means the note is
still sounding, so the lane draws its linked continuation head there in the note's own head shape;
exactly at the sustain end means a shift-slide glide-end, where the note stops and the re-picked
landing draws its own head, so no linked glyph. Being unpitched does not unlink a keyframe — a
scrape's turnaround is one gesture continuing, and its head is what keeps the corner from reading
as a break. The falls-away terminal never reaches this question at all: it is \ref
NoteViewState::slide_out rather than a keyframe, so nothing asks whether the note continues
through the instant it ends at.

A READ of two shared facts, not a stored field, so the one continuation rule cannot be restated
per surface. Being a read is also what makes it correct in either \ref ChartNoteForm without a
second rule: it asks the tail the note in front of it actually has. The reading genuinely differs
between the forms, and that is the answer rather than a discrepancy — a shift-slide's arrival sits
exactly at the PRESENTED end (rule 2 stops the trimmed tail there) and strictly inside the ACTUAL
one, so the same keyframe that draws no glyph on the lane's ordinary picture draws a mid-tail
continuation head under the editor's reveal. The glide really does continue there; the presented
tail is simply cut before it.

\param note Note the keyframe belongs to.
\param keyframe One of the note's \ref NoteViewState::slides entries.
\return True when the keyframe is a continuation of the note.
*/
[[nodiscard]] constexpr bool linkedKeyframe(
    const NoteViewState& note, const KeyframeViewState& keyframe) noexcept
{
    return keyframe.seconds < note.end_seconds;
}

/*!
\brief True when nothing stops OR touches the string: a genuine open string.

Fret zero alone cannot answer this — a natural harmonic (and a tap harmonic on an open string)
also stores fret 0, with the node carrying its position, and rendering one as an open string
erased the harmonic from the board outright: every decision between the open-string treatment
(the hand-window bar, the window-spanning tail band, the faded tail edge) and the fretted treatment
must ask this instead of testing `fret == 0`.

\param note Note to classify.
\return True when the note is an open string with no harmonic node.
*/
[[nodiscard]] inline bool openString(const NoteViewState& note) noexcept
{
    return note.fret == 0 && !note.harmonic_node.has_value();
}

/*!
\brief The fret slot the note's fretting hand occupies.

\ref fretFor through the mirrored fields: a natural harmonic's finger stands on the node, so its
slot is the fret CONTAINING the node (the ceil law chart.h derives), while every other note's slot
is its own fret. This is what fret-aligned furniture — the onset span line, the hit-glow fret
lines — aligns to; a head itself keeps the node's exact fractional position.

\param note Note to place.
\return Fret slot of the fretting hand; zero for a true open string.
*/
[[nodiscard]] inline int fretFor(const NoteViewState& note)
{
    return fretFor(note.fret, note.harmonic_node, note.attack);
}

/*! \brief What the hand holds on one string under a shape span. */
struct ShapeStringViewState
{
    /*! \brief One-based chart string (unshifted, like \ref NoteViewState::string). */
    int string{1};

    /*!
    \brief Stop held on the string: a fret pressed, the open string, or a harmonic node touched.
    */
    ChartStop stop{};

    /*!
    \brief Where this string's posture digit prints, or absent where nothing prints it.

    The digit rule, answered once by the projection instead of by each surface (user ruling
    2026-08-27), and asked at THE INSTANT THE MARK DRAWS — that instant and no other (THE DIGIT
    WINDOW, user ruling 2026-08-31). One head can stand on the string there, and the three answers
    are one question about it: centred in the bracket where NOTHING heads the string; displaced
    into the satellite column where a head there — whichever hand made it — sounds at ANOTHER
    place; and absent where a head there sounds at THIS one — a number stated twice beside itself
    is the only thing suppression exists to prevent. The hand is no part of the test: a centred
    digit sits where a head at that instant sits and the note pass paints after the brackets, so
    a head sounding elsewhere covers it, and the satellite is the one slot it cannot paint over.
    Compared as PLACES (\ref ChartStop), never as printed digits: two facts that happen to print
    the same number are still two facts, so a tap at fret 12 under a node-12 grip takes the
    satellite and both print "12", while a fretted-5 head printing its node "17" over a grip
    holding 5 puts the 5 in the satellite beside it rather than losing it under the head.

    A head LATER in the span suppresses nothing, because the opening bracket is the span's CHORD
    FRAME: it states the full membership at the moment the reader meets it, so an accumulation's
    members print their frets there and their own heads restate them as they arrive. Asking over
    the whole span emptied that frame of everything still to come, and its inclusive end let the
    onset that CLOSED the span decide the digits inside it.

    Absent is about the DIGIT alone wherever a bracket draws at all: the bars draw for every posture
    string either way, and they are what a silently-held member is selected by. What the slot
    decides is how far that mark's drawn — and therefore clickable — extent runs, and, where a tap
    FRONTS this bracket, that the bracket is what prints that tap's held stop
    (\ref StopMarkFace::Posture). This entry is the SPAN's membership statement and nothing else: a
    tap further along carries its held fret here as an ordinary member AND wears its own satellite
    (\ref NoteViewState::stop_mark), two facts stated in two inks.
    Where the span draws NO bracket the entry is absent for a different reason entirely, and the
    posture entry beside it still stands: the posture is a fact the class rule and the box identity
    both read.
    */
    std::optional<StopMarkSlot> digit{StopMarkSlot::Bracket};

    /*!
    \brief Compares two posture entries by their stored fields.
    \param lhs Left-hand entry.
    \param rhs Right-hand entry.
    \return True when both entries store equal values.
    */
    friend constexpr bool operator==(
        const ShapeStringViewState& lhs, const ShapeStringViewState& rhs) noexcept = default;
};

/*! \brief One hand-posture span resolved to timeline seconds for rendering. */
struct ShapeViewState
{
    /*! \brief Absolute start of the span. */
    double start_seconds{0.0};

    /*!
    \brief Where the span's furniture STOPS: the drawn extent, on every surface, every time.

    Rule 12a's answer and the only end anything draws unasked (\ref makeChartViewState, which is
    where the whole of that rule now lives): the musical close pulled back to keep the minimum
    sustain distance before the head that closed the span, so consecutive spans show the gap every
    other drawn element shows instead of butting exactly. The margin is a DISPLAY rule, so this is
    the field it is in and \ref close_seconds beside it stays the musical fact.

    Never after \ref close_seconds, and EQUAL to it wherever no margin was owed — a span that simply
    ran out, one whose rings died a full margin early, one closed at a slot of held fingers, and one
    so crowded the trim left nothing. Two ends that coincide is the ordinary case, not a corner.
    */
    double drawn_end_seconds{0.0};

    /*!
    \brief THE MUSICAL CLOSE: the instant the span's statement actually ended.

    \ref ChartShape::sustain resolved to seconds, carrying no display margin at all — the closing
    EVENT's own onset where an event closed the span, the shape's own reach where the statement ran
    out, whichever came first. It is what the spans themselves are measured against and what
    abutting spans tile at.

    Published beside the drawn extent because the editor's 2D lane REVEALS it (user ruling
    2026-09-04): while the lane's reveal is held, or while the span covers a note in the selection,
    that span's furniture runs to here instead. It is the same bargain the note reveal strikes — the
    drawn tail is the presented one, and the reveal shows the ring the chart stores — with one
    difference forced by the data: presentation gives a note two FORMS, while the margin here is a
    single display rule over one span, so the two ends ride one state and the surface picks.

    The 3D board draws no reveal and reads the drawn extent alone.
    */
    double close_seconds{0.0};

    /*!
    \brief True when the span's members arrive SEPARATELY (arpeggio brackets) rather than together
    (chord box).

    Taken from \ref ChartResolutions::arrivals, derived once per chart revision by
    \ref chartShapeArrivals — not re-derived here, and not a question about the span's start alone:
    an interior partial sounding, an inherited claim and a right-hand onset anywhere inside the span
    each flip it. A CARRY-OPENED SUCCESSOR — one a landing or a member's DEATH founded — is no
    longer an arpeggio by construction (user ruling 2026-08-30): a boundary is not a sounding, so it
    classifies by those same triggers and a chord sliding into chords is a box at both ends.
    */
    bool arpeggio{false};

    /*!
    \brief The span's posture, lowest string first; empty when the posture is unknown.

    The whole held posture, stated whether or not a note sounds on the string: a posture is a
    claim about the fretting hand, not about what is struck. The arpeggio brackets on both
    surfaces read an arpeggio span's entries.
    */
    std::vector<ShapeStringViewState> strings;

    /*!
    \brief Where a span's posture bracket anchors; absent only where a carry-opened successor never
    sounds interiorly.

    Not the same fact as \ref arpeggio, which is the CLASS — what the span's rails and its name say
    it is. This is where the span's one opening mark is drawn, and [D2]'s amendment 2 separated
    them: a CARRY-OPENED SUCCESSOR is a span that draws no mark at its own start at all. Nothing is
    stated at a boundary — a chord slide keeps the fingers planted, so all that happens at a landing
    is the fingers arriving, and a member's death states nothing either — so the continued tails
    plus the chord NAME changing there are the whole statement. Its bracket DEFERS to the span's
    first INTERIOR sounding — the ink follows the sound — and a successor that never sounds
    interiorly draws no bracket at all, which is what the empty state means.

    Every other span keeps its FRONT: there the front IS the statement, made by a strum or by an
    authored hold, rather than a continuation of one. A full restrike wears its own full box at its
    own onset either way, which is a different mark from this one.

    Both surfaces read it — the 2D lane's "[ fret ]" marks and the board's arpeggio box alike — and
    \ref ShapeStringViewState::digit is resolved AT this instant and at no other (THE DIGIT WINDOW,
    user ruling 2026-08-31), so the mark states the span's whole membership where the reader meets
    it and only a head standing right there takes a number out of it. A silently-held stop's own
    face rides it too (\ref NoteViewState::stop_mark) — the bars ARE that face — which is what keeps
    an undrawn bracket from being clickable.

    EMPTY on a BOX-class span, which is the one condition gating it and it is stated once, at the
    projection (user ruling 2026-08-30). A bracket is arpeggio furniture: a box-class span states
    itself with its strums' own boxes and opens no mark at all, so publishing its start here would
    be an instant at which nothing draws — and the coincidence rule that suppresses a chord box
    under an arpeggio box keys on exactly this optional. Since the successor ruling of the same day
    that is the ORDINARY disposition of a landing successor rather than a corner of one.

    Taken from the derivation's \ref ChartShape::bracket_position rather than re-scanned: the walk
    is what knows which slots a statement covers, and a re-scan asked that grouping question a
    second time against an extent the closing trim had already shortened.
    */
    std::optional<double> bracket_seconds{};

    /*!
    \brief Compares two shape view states by their stored fields.
    \param lhs Left-hand shape.
    \param rhs Right-hand shape.
    \return True when both shapes store equal values.
    */
    friend bool operator==(const ShapeViewState& lhs, const ShapeViewState& rhs)
    {
        return std::is_eq(lhs.start_seconds <=> rhs.start_seconds) &&
               std::is_eq(lhs.drawn_end_seconds <=> rhs.drawn_end_seconds) &&
               std::is_eq(lhs.close_seconds <=> rhs.close_seconds) &&
               lhs.arpeggio == rhs.arpeggio && lhs.strings == rhs.strings &&
               lhs.bracket_seconds == rhs.bracket_seconds;
    }
};

/*! \brief One fret-hand placement resolved to a timeline second, with its eased approach. */
struct FhpViewState
{
    /*! \brief Absolute position the hand arrives at this placement. */
    double seconds{0.0};

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand. */
    int width{4};

    /*!
    \brief Duration of the hand's eased approach ending at \ref seconds; zero arrives instantly.

    When the fretting hand starts moving toward this placement is a fact about the chart, not
    about a surface, so it is derived once here: a placement landing exactly on a slide keyframe —
    pitched glide or unpitched trail-off end alike — ramps over that glide's own segment so a
    drawn hand travels with the drawn rail, and every other placement morphs over the
    minimum-sustain-distance margin at its meter (shortened when placements crowd closer than the
    ramp). The board's hand window animates it; the lane's static marker draws the arrival alone.
    */
    double ramp_seconds{0.0};

    /*!
    \brief True when \ref ramp_seconds spans an UNPITCHED glide, so an animated hand eases with the
    unpitched release curve instead of the pitched one.

    The hand follows whatever the rail draws, and the two families are different functions of
    progress (\ref highwaySlideEaseWeight). Easing every move with the pitched curve left the
    window and the rail sharing only their endpoints. Note the consequence: the unpitched curve
    arrives at full travel with nonzero slope, so the window stops abruptly at the release — which
    is exactly what the drawn rail does at the same instant.
    */
    bool unpitched_ramp{false};

    /*!
    \brief Compares two placements by their stored fields.
    \param lhs Left-hand placement.
    \param rhs Right-hand placement.
    \return True when both placements store equal values.
    */
    friend constexpr bool operator==(const FhpViewState& lhs, const FhpViewState& rhs) noexcept
    {
        return std::is_eq(lhs.seconds <=> rhs.seconds) && lhs.fret == rhs.fret &&
               lhs.width == rhs.width && std::is_eq(lhs.ramp_seconds <=> rhs.ramp_seconds) &&
               lhs.unpitched_ramp == rhs.unpitched_ramp;
    }
};

/*!
\brief Seconds-resolved chart content for one arrangement: the scene both surfaces draw.

Built once per chart revision by \ref makeChartViewState and shared immutably: positions are
resolved through the tempo map at projection time so rendering never queries musical positions per
frame. The 2D tablature lane renders this directly; the 3D highway composes it inside
\ref HighwayViewState beside the board-only structure it adds. One producer, so the two surfaces
cannot drift on a shared chart fact — where they are allowed to differ is in their painters, never
in their data.

The editor holds a SECOND state of the same chart, projected in \ref ChartNoteForm::Actual, for the
reveal it draws while Alt is held. That is the one producer asked a different question, not a
second projection: the two states differ in \ref notes and are equal in every other member.
*/
struct ChartViewState
{
    /*!
    \brief The tuning's open-string pitch names, lowest string first; empty means no chart.

    \ref ChartTuning::strings verbatim — "E2", "A2", and whatever a drop or altered tuning names
    instead — which is what lets a surface LABEL a string rather than only count them. Carried
    rather than re-derived because the count was already carried and the count IS this array's
    length: one fact, so a surface that draws the name and one that lays out lanes cannot disagree
    about how many strings there are.

    Indexed by chart string minus one. The CHART's strings, never a display count: a surface pads
    to its own minimum when it lays out (\ref displayedStringCount).
    */
    std::vector<std::string> open_strings;

    /*!
    \brief Capo fret from the chart tuning; 0 means no capo.

    Carried so a surface can indicate the string floor: the chart stores absolute frets with 0
    meaning the capo'd open string, so without this nothing in the drawn content says a capo
    exists at all.
    */
    int capo{0};

    /*! \brief Sounding notes in ascending onset order. */
    std::vector<NoteViewState> notes;

    /*!
    \brief Per-note hold end in seconds — the 3D board's, one entry per \ref notes entry.

    How long a pinned head lasts: the note's presented end, except that a member of a two-or-more
    onset group under a covering hand-shape span whose presented tail is empty is held to THE
    SPAN'S MUSICAL CLOSE — the strum's heads stay pinned at the hit line for as long as the posture
    is held, instead of vanishing the instant it is struck, and they go on standing there while
    repeat boxes restate the same shape over them. A fully dead group is choked rather than held and
    keeps its own end. The note's own ring does not cap this; \ref chartHolds says why.

    The CLOSE and not the drawn rails (user ruling 2026-09-04, which moved rule 12a's margin to the
    projection): a hold is how long the hand is down, and the margin is ink spacing. So a held head
    now stands one margin longer than it did — right up to the onset that ended the shape, which is
    the honest answer and the one the rails were never the authority for.

    **The 2D lane does not read this.** It draws, lays out, hit-tests and culls by each note's
    presented tail (\ref NoteViewState::end_seconds) alone, so the ribbons under sub-quarter chugs
    are simply absent there — the chord box over the strum already states how long the posture is
    fretted, and a ribbon repeating that used the one mark that means "this string is still
    ringing" to say something else. The board has no chord box, so pinning the heads is how it
    states the same fact (ruling 3 of `docs/plans/in-progress/note-sustain-model.md`). One chart,
    one hold, two idioms.

    Resolved here from \ref chartHolds, the ONE authority for that rule, rather than recomputed in
    seconds: it used to be computed twice, once in beats for the chart rules and once in seconds
    for the board, and both copies carried the same defect — a long span shadowed by a short one
    that started inside it silently lost its hold — and were fixed separately. That is the whole
    argument for resolving the beats answer instead of restating it.

    Feeds the board's visible-range prefix maximum, so a pinned strum stays in range for as long
    as it is held.
    */
    std::vector<double> display_hold_ends;

    /*! \brief Hand-posture spans in ascending start order. */
    std::vector<ShapeViewState> shapes;

    /*! \brief Fret-hand placements in ascending arrival order. */
    std::vector<FhpViewState> fret_hand_positions;

    /*!
    \brief Number of strings the chart's tuning declares; zero means no chart is loaded.

    Asked rather than stored, because \ref open_strings already IS the answer: the count and the
    names were one array in the chart and stay one here.

    \return The chart's string count.
    */
    [[nodiscard]] int stringCount() const noexcept
    {
        return static_cast<int>(open_strings.size());
    }

    /*!
    \brief Compares two chart view states by their stored fields.
    \param lhs Left-hand state.
    \param rhs Right-hand state.
    \return True when both states store equal values.
    */
    friend bool operator==(const ChartViewState& lhs, const ChartViewState& rhs) = default;
};

} // namespace rock_hero::common::core
