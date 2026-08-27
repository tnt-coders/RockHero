/*!
\file chart.h
\brief Arrangement-owned chart model: the true tab of notes, tuning, and hand placements.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*!
\brief Musical grid position with exact sub-beat resolution.

Serializes as the tempo-map token grammar extended with an exact fraction:
`"<measure>:<beat>"` for whole beats and `"<measure>:<beat>+<n>/<d>"` for sub-beat positions.
*/
struct GridPosition
{
    /*! \brief One-based measure on the song grid. */
    int measure{1};

    /*! \brief One-based beat within the measure. */
    int beat{1};

    /*! \brief Exact fraction from this beat toward the next beat, in [0, 1). */
    Fraction offset{};

    /*!
    \brief Orders two grid positions along the timeline.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const GridPosition& lhs, const GridPosition& rhs) noexcept
    {
        // std::is_neq instead of `!= 0`: GCC's -Wzero-as-null-pointer-constant misfires on
        // ordering-vs-literal-zero comparisons (the standard comparison operator takes an
        // unspecified pointer-constructible parameter), so the named query keeps -Werror builds
        // clean without changing meaning.
        if (const auto measure_order = lhs.measure <=> rhs.measure; std::is_neq(measure_order))
        {
            return measure_order;
        }
        if (const auto beat_order = lhs.beat <=> rhs.beat; std::is_neq(beat_order))
        {
            return beat_order;
        }
        return lhs.offset <=> rhs.offset;
    }

    /*!
    \brief Compares two grid positions for equal value.
    \param lhs Left-hand position.
    \param rhs Right-hand position.
    \return True when both positions store equal values.
    */
    friend constexpr bool operator==(const GridPosition& lhs, const GridPosition& rhs) noexcept =
        default;
};

/*! \brief How a note's onset is produced when it is not a plain pick. */
enum class NoteAttack : std::uint8_t
{
    /*! \brief Plain picked onset. */
    Pick,
    /*!
    \brief Pinch harmonic: the pick stroke's thumb graze damps a node as the plectrum passes.

    An attack rather than a timbre because the graze happens *inside* the onset — one compound
    stroke with its own hand angle, not a pick plus a separate action. That is the same grain
    that already separates `Slap` from `Pop`. It also makes the pinch the one harmonic damped
    off the neck, which is why `nodeIsOnNeck` excludes it.
    */
    Pinch,
    /*!
    \brief Legato: this onset connects to its same-string predecessor with no new pick.

    A relational CLAIM and nothing more. Which way the connection runs — a hammer-on onto a higher
    stop, a pull-off onto a lower one — is read back from the predecessor by \ref resolveLegato and
    is never stored, so no neighbour edit can leave a stale direction behind. A claim the chart does
    not justify draws and plays as the plain pick it sounds like, until the chart justifies it
    again.
    */
    Legato,
    /*!
    \brief Left-hand tap: the fretting hand strikes this note from nowhere.

    The one connection-family onset that is LOCAL: it asserts the hammering motion outright with no
    predecessor to connect to, which is why it survives every neighbour edit and why
    \ref resolveLegato resolves it to the hammer motion unconditionally. Needs somewhere to strike,
    exactly like a two-hand tap (\ref validateChartNoteAlone).
    */
    LeftTap,
    /*! \brief Two-hand tap onset. */
    Tap,
    /*! \brief Popped (bass) onset. */
    Pop,
    /*! \brief Slapped (bass) onset. */
    Slap,
    /*!
    \brief Right-hand pick slide: the pick scrapes along the neck across the sustain.

    Fret data is right-hand travel like a tapped note's: `fret` is where the scrape starts,
    `slide_out` is the required unpitched terminal — at the sustain by definition, because nothing
    rings past a scrape — and `keyframes` holds optional direction-turnaround fret statements,
    the whole path always traveling. The pitched techniques (mute, harmonic node, vibrato,
    tremolo, bend) are overridden while this attack is set: kept in memory so switching the
    attack back restores them, but suppressed by projections and omitted by the document
    writer. Emphasis is never overridden — a scrape has its own dynamics, played aggressively
    or lightly.
    */
    PickSlide,
    /*!
    \brief No onset at all: the fretting hand takes this stop SILENTLY.

    The one fact about the fretting hand a stream of strokes structurally cannot carry, and it is
    an attack because that is what it changes — the note is still a stop on a string at an instant,
    and only the stroke is missing. A finger resting on a fret makes no sound, produces no onset
    and extends no ring, so a hand holding a six-string shape and picking four of it streams
    identically to a hand holding four and moving to the fifth later; both are real playing, so the
    derivation notates the literal notes and this attack is how a charter states the other reading.

    A POINT record: `position`, `string` and `fret` are the whole of it. `sustain` is FORBIDDEN
    rather than merely unused — a silent hold has no ring of its own, and its extent is the span's
    (a stored ring would be falsely bounded by a same-string tap through \ref sustainBoundOf) — and
    every technique is refused with it, since each describes something about a sound that never
    happens. \ref savedChartNote strips them and \ref validateChartNoteAlone refuses a note that
    carries any, which is the same fixpoint pairing a scrape's latent overrides use.

    A silent hold is never a STRIKE: it closes no span, ends no posture, never re-picks a shape and
    never bounds a neighbour's ring. It IS a MEMBER — a shape is made of stops rather than of
    strikes (user ruling 2026-08-27) — so two members at one slot open a span whichever kind they
    are, and a lone member of either kind opens nothing. It presents no head and no tail on any
    surface; what shows it is the arpeggio bracket printing its stop at its span's start
    (\ref deriveChartShapes). The design record is `docs/plans/todo/arpeggio-authoring.md`.

    Declared LAST rather than in any musical order, and that placement is load-bearing: `Pick` must
    keep the zero value so a value-initialized note is a plain picked one. A zero-valued `None`
    would make every default-constructed note a silent hold — an illegal default hiding behind
    correct-looking code, exactly the trap \ref NoteEmphasis::Normal is declared first to avoid.
    */
    None
};

/*!
\brief How hard a note is struck relative to its neighbours — the dynamics axis.

One axis rather than two flags, so the two loud/quiet claims cannot both be set: a note that is
simultaneously accented and ghosted is not a rule to enforce but a state that cannot be written
down. `Normal` is the implied default and never serializes.

Dynamics, not technique: emphasis composes with every attack, mute, and articulation there is,
scrapes included — an accented scrape is one played aggressively, a ghosted one played lightly —
so no combination is refused and no compatibility cell opens.

A heavier tier above `Accent` is deliberately absent for now. Guitar Pro notates two loud tiers
and both import as `Accent`; the enum extends if the distinction ever earns its place, which is
why no consumer compares against `Accent` directly — they ask \ref isAccented, and the document
writer switches exhaustively so a new value cannot serialize as nothing.
*/
enum class NoteEmphasis : std::uint8_t
{
    /*!
    \brief The default weight; never written to a document.

    Listed FIRST rather than in the axis's musical order so that value-initialization lands on
    it: a zero-valued `Ghost` would make every default-constructed or resized emphasis the quiet
    extreme, which is an illegal default hiding behind correct-looking code. Nothing compares
    these values for loudness — \ref isAccented is the one classifier — so the declaration order
    costs no meaning.
    */
    Normal,
    /*! \brief Ghost note: struck deliberately quietly. */
    Ghost,
    /*! \brief Struck harder than its neighbours. */
    Accent
};

/*!
\brief Reports whether a note is struck LOUDER than normal.

The one place the loud end of the axis is defined, so a heavier tier arriving above \ref
NoteEmphasis::Accent lights up every consumer at once instead of leaving each open-coded
comparison quietly answering "not accented". Its quiet twin is \ref isGhosted below.

\param emphasis How hard the note is struck.

\return True for every emphasis above normal.
*/
[[nodiscard]] constexpr bool isAccented(NoteEmphasis emphasis) noexcept
{
    return emphasis == NoteEmphasis::Accent;
}

/*!
\brief Reports whether a note is struck QUIETER than normal.

The quiet end's one classifier, mirroring \ref isAccented: added once both renderers and the
view-state grouping were each open-coding the ghost comparison, so a second quiet tier arriving
below \ref NoteEmphasis::Ghost lights up every consumer at once.

\param emphasis How hard the note is struck.

\return True for every emphasis below normal.
*/
[[nodiscard]] constexpr bool isGhosted(NoteEmphasis emphasis) noexcept
{
    return emphasis == NoteEmphasis::Ghost;
}

/*!
\brief What a note's connection claim resolves to: the motion it plays as, or nothing.

The read side of \ref NoteAttack::Legato. Direction is never stored, so this is the only place a
hammer-on and a pull-off are told apart, and every surface asks \ref resolveLegato for it rather
than reading it off a note.

`Unjustified` covers both notes that make no claim at all and a claim the chart does not justify —
one value on purpose, because the two are indistinguishable everywhere it is read: each draws and
scores as a plain pick.
*/
enum class LegatoMotion : std::uint8_t
{
    /*! \brief No connection: the onset is a plain pick as far as any surface can tell. */
    Unjustified,
    /*! \brief Hammer-on: the fretting hand strikes onto a stop above the predecessor's. */
    Hammer,
    /*! \brief Pull-off: the finger releases onto a stop below the predecessor's. */
    Pull
};

/*!
\brief Reports whether the attack belongs to the connection family — the `H` toggle's domain.

`Pick`, `Legato`, and `LeftTap` are the three states `H` and `Ctrl+H` move a note between; every
other attack is produced by the picking hand (`Tap`, `Pinch`, `PickSlide`) or is a bass articulation
(`Pop`, `Slap`) whose onset is already fully described, so a connection claim would say nothing
about it and the toggle skips it in both directions.

Display asks the same question for a different reason: inside this family the beside-head mark comes
from the note's RESOLVED \ref LegatoMotion, and outside it the attack carries a mark of its own.

\param attack Attack to classify.

\return True when the attack can carry, or be given, a connection claim.
*/
[[nodiscard]] constexpr bool legatoClaimable(NoteAttack attack) noexcept
{
    return attack == NoteAttack::Pick || attack == NoteAttack::Legato ||
           attack == NoteAttack::LeftTap;
}

/*!
\brief Snaps a notated open-string node label to the nearest true node offset.

Notation stores conventional labels rather than measured positions — the 7th partial is written
"2.7" or "2.8" against a true 2.669 — and a touch even slightly off a node chokes a high harmonic
instead of ringing it. This maps a label onto the physics: a string's nth-partial nodes sit at
`12*log2(n/(n-k))` fret units above its stop for `k = 1..n-1`, and fret positions are logarithmic,
so the stop and the offset simply add. Callers resolve the label against an open string and place
the result against the real stop.

\param notated Node label as written, in open-string fret units.
\param max_partial Highest partial to consider, so a label cannot snap onto an absurd high-order
                   node that happens to sit nearer to it.

\return Nearest true node offset. Always defined, since every partial from 2 up has nodes; a
        `max_partial` below 2 yields the octave.
*/
[[nodiscard]] double snapHarmonicNode(double notated, int max_partial);

/*!
\brief True when a harmonic's node lies on the neck, where a display can point at it.

Every harmonic damps its node with a finger on the fretboard except a **pinch**, whose thumb
grazes the string out over the body. A right-hand tap harmonic belongs on the neck side: the
damping finger is the picking hand's, but it lands on the fretboard.

Asks about the *attack* alone; callers pair it with their own `harmonic_node.has_value()`, which
reads plainly and stays visible to the optional-access checker, which cannot see through a wrapper.

\param attack How the onset is produced.

\return True unless the node is off the neck.
*/
[[nodiscard]] constexpr bool nodeIsOnNeck(NoteAttack attack) noexcept
{
    return attack != NoteAttack::Pinch;
}

/*!
\brief Reports whether the attack is a pick scrape: the plectrum dragged along the string.

A scrape is unpitched travel end to end, and everything about it follows from that: it overrides
the note's other techniques in memory and the writer strips them (\ref savedChartNote), every
keyframe of its path is unpitched and its required terminal is its slide-out, it renders through
the unpitched machinery on both surfaces, it never anchors a fret-hand placement, and it never
justifies a legato claim on the note after it (\ref resolveLegato). Asked by name so the one
attack those rules hang on is grep-able and can never be mistaken for an incidental equality.

\param attack Attack to classify.

\return True when the attack is a pick scrape.
*/
[[nodiscard]] constexpr bool isScrape(NoteAttack attack) noexcept
{
    return attack == NoteAttack::PickSlide;
}

/*!
\brief Reports whether the note is a stop taken with no stroke at all (\ref NoteAttack::None).

The one question every reader of the note stream that means "what SOUNDS" has to ask, spelled once
so the skip is greppable and can never be mistaken for an incidental equality. A silent hold is in
the stream because it is a stop on a string at an instant like any other note — it moves, deletes,
selects and retypes through the same verbs — but it produces no onset, so nothing that draws a
head, measures a ring, bounds a neighbour's tail, groups a strum, justifies a connection or counts
a strike may include it.

Contrast \ref rightHandOnset, which asks the opposite kind of question: a tap DOES sound and is
merely the other hand's, so it is invisible to the fretting hand's posture grouping while staying a
real onset everywhere else.

\param attack Attack to classify.

\return True when the note is a silently-held shape member.
*/
[[nodiscard]] constexpr bool silentHold(NoteAttack attack) noexcept
{
    return attack == NoteAttack::None;
}

/*!
\brief Reports whether the attack is produced by the picking hand at the neck (tap, pick slide).

These onsets never anchor, cover, or ring into a fretting-hand posture; the fret-hand
generator, posture derivation, chord grouping, and camera framing all share this predicate.

\param attack Attack to classify.

\return True when the picking hand produces the onset at the neck.
*/
[[nodiscard]] constexpr bool rightHandOnset(NoteAttack attack) noexcept
{
    return attack == NoteAttack::Tap || isScrape(attack);
}

/*!
\brief Reports whether a note wears a mute mark at all, either hand's.

Two independent flags, so "muted" is a question rather than a field: every surface that decides
whether to draw a mute mark, box a fret number, or hold a repeat box off asks this, and none of
them re-spells the disjunction. Which mark it then draws is the separate question \ref
ChartNote::dead answers alone.

\param palm_mute True when the picking hand's palm damps the string.
\param dead True when the string is deadened into an unpitched click.

\return True when either mute applies.
*/
[[nodiscard]] constexpr bool isMuted(const bool palm_mute, const bool dead) noexcept
{
    return palm_mute || dead;
}

/*!
\brief Which of a note's two fretting-hand stops a verb, a caret, or a typed digit addresses.

A note under a right-hand onset states two stops at one slot — what the picking hand SOUNDS
(\ref ChartNote::fret) and what the fretting hand HOLDS (\ref ChartNote::held) — so "the fret of
this note" stopped being one question the moment the second stop became storable. Every surface
that can reach both names which one it means with this rather than by testing the attack, so the
click, the caret stop and the typed digit cannot disagree about what they addressed.

`Sounding` is listed first so a value-initialized channel is the one every note has; a note with no
held stop simply has no `Held` channel to address, which is what makes an unreachable state
unreachable rather than merely unused.
*/
enum class ChartStopChannel : std::uint8_t
{
    /*! \brief The note's own sounding fret (\ref ChartNote::fret). */
    Sounding,

    /*! \brief The fretting-hand stop under a right-hand onset (\ref ChartNote::held). */
    Held
};

/*!
\brief One statement along a ringing note: a moment, and what changes at it.

The chart's one interval-payload record. A keyframe fixes a MOMENT inside the note's ring and
carries any SUBSET of the channels that can change while a string sounds. The moment is stored
ONCE and every technique authored there lands on it, so moving the moment moves everything that
meant "at that moment" — which parallel per-technique arrays could not do: a glide target, a
vibrato start, and a bend value at one instant were three independently editable copies of one
offset, and dragging any of them sheared the authored figure with no rule able to object, because
both the before and the after were legal (`docs/plans/todo/unified-waypoint-model.md`).

Each channel reads independently along the ring:

- **fret** — position, discrete. Interpolates between fret-STATING keyframes: equal frets are a
  HOLD, different frets are travel. A fret-less keyframe says nothing about position and a glide
  passes through it unkinked, which is what lets a bend change mid-travel between two frets
  without the path having to name a stop the hand never takes.
- **bend** — push, continuous, in semitones and never negative. Interpolates between bend-stating
  keyframes, starting from the note's own onset value (\ref ChartNote::bend), and holds flat past
  the last one. A compound bend is a sequence of values, a bent slide is one value held across
  fret-stating keyframes, and a mid-hold curl is a new value on a keyframe stating no fret.
- **vibrato** — state. Holds from each statement until the next, so a delayed start, a mid-ring
  end, several regions, and vibrato through a glide are all just statements.

A keyframe never sits on a later onset of its own string while it states a FRET: a glide into a
real note ends the minimum sustain distance before its landing, and the landing renders its own
head, so storing the landing's coordinates a second time is what \ref validateChartNotes refuses.
A bend or vibrato statement there says nothing about position and is bound only by the ring.

On a pick slide the keyframes are optional direction turnarounds — unpitched right-hand travel,
which is why a saved scrape carries fret statements and nothing else — and the gesture's terminal
is its required \ref ChartNote::slide_out.

A keyframe stating NOTHING is not a record at all but a location with no fact attached;
\ref keyframeStatesNothing is that question's one spelling and \ref validateChartNoteAlone refuses
such a keyframe.
*/
struct Keyframe
{
    /*! \brief Beat-fraction offset from the note onset; strictly positive, within the sustain. */
    Fraction offset{};

    /*!
    \brief Fret the hand has reached here; absent when the keyframe states nothing about position.
    */
    std::optional<int> fret{};

    /*! \brief Bend amount in semitones here, never negative; absent when the push is unstated. */
    std::optional<double> bend{};

    /*! \brief Whether the string shakes from here on; absent when vibrato is unstated. */
    std::optional<bool> vibrato{};

    /*!
    \brief Compares two keyframes by their stored fields.
    \param lhs Left-hand keyframe.
    \param rhs Right-hand keyframe.
    \return True when both keyframes store equal values.

    Defaulted despite the floating channel: the compare happens inside `std::optional`, where
    clang's -Wfloat-equal does not reach, so this needs no hand-written twin (the bare `double` on
    \ref ChartNote does).
    */
    friend bool operator==(const Keyframe& lhs, const Keyframe& rhs) noexcept = default;
};

/*!
\brief Reports whether a keyframe states no channel at all — the one shape no chart may hold.

A keyframe IS its statements: a location carrying none says nothing that could be drawn, played,
or edited, and it would still shift every neighbour's index and survive every edit. Refused by
\ref validateChartNoteAlone, and asked by \ref stripKeyframeChannels for every rule that sheds a
channel.

\param keyframe Keyframe to classify.

\return True when no channel is stated.
*/
[[nodiscard]] inline bool keyframeStatesNothing(const Keyframe& keyframe) noexcept
{
    return !keyframe.fret.has_value() && !keyframe.bend.has_value() &&
           !keyframe.vibrato.has_value();
}

/*!
\brief Clears channels across a note's keyframes and drops only the ones the clearing emptied.

Every rule that sheds a channel needs this, and needs it to be exactly this. Such a rule clears
CHANNELS rather than whole keyframes — a capo floor takes the fret, not the bend authored at the
same instant — so a keyframe it empties is no record at all and must go. A keyframe that ARRIVED
stating nothing is a different thing entirely: illegal data \ref validateChartNoteAlone refuses,
and \ref normalizeChart runs before \ref validateChartRules on every load, so a strip that dropped
every empty keyframe it found would quietly repair that refusal out of existence. Removing what
the strip itself emptied is the only reading that does neither, and it is spelled once because
every shedding rule asks it.

\param keyframes Keyframes to strip in place, left in order.
\param strip Applied to each keyframe in turn: clears whatever channels the caller's rule owns and
       returns whether it cleared any.

\return True when the strip cleared a channel anywhere — what a caller reports as its repair.
*/
template <typename Strip>
[[nodiscard]] bool stripKeyframeChannels(std::vector<Keyframe>& keyframes, const Strip& strip)
{
    bool stripped = false;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < keyframes.size(); ++index)
    {
        Keyframe& keyframe = keyframes[index];
        const bool cleared = strip(keyframe);
        stripped = stripped || cleared;
        if (cleared && keyframeStatesNothing(keyframe))
        {
            continue;
        }
        if (kept != index)
        {
            keyframes[kept] = keyframe;
        }
        ++kept;
    }
    keyframes.resize(kept);
    return stripped;
}

/*!
\brief Reports whether any keyframe states a POSITION — whether the note travels at all.

The question every rule about gliding asks, and it is not "are there keyframes": a note whose only
statements are a mid-ring curl or a delayed shake never moves the hand, so an open string may keep
them and a dead note's E25 tail is not earned by them. Spelled once so the travel rules and the
presentation rules cannot drift about what travelling means.

\param keyframes The note's keyframes.

\return True when at least one keyframe states a fret.
*/
[[nodiscard]] inline bool anyKeyframeStatesFret(const std::vector<Keyframe>& keyframes) noexcept
{
    for (const Keyframe& keyframe : keyframes)
    {
        if (keyframe.fret.has_value())
        {
            return true;
        }
    }
    return false;
}

/*!
\brief One string sounding once: the only event kind in the note stream.

A strummed chord is simultaneous notes at one position; shape spans supply the notation layer.
*/
struct ChartNote
{
    /*! \brief Musical onset position. */
    GridPosition position;

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*!
    \brief The ACTUAL duration the string rings, in beats. Strictly positive.

    Guitar Pro's notated duration at import, what the editor's verbs author, and what playback will
    sound. Not what any surface draws: the drawn tail is derived from this once per chart revision
    by \ref presentedChartNotes, so a sub-quarter chug rings for its eighth and shows nothing, and
    a dead note carries the duration of its damped stroke while presenting no tail at all (E25).
    Storing the truth once is what keeps the readability policy from being destruction that every
    later reader then has to guess back (`docs/plans/in-progress/note-sustain-model.md`).

    Every note that SOUNDS rings for some length, so zero is not an encoding — \ref
    validateChartNoteAlone refuses it structurally, since no repair can invent a duration. The one
    exception is the one note that never sounds: a \ref NoteAttack::None hold has no ring of its
    own, so zero is not merely allowed there but REQUIRED, and any other value is refused. The only
    bound is \ref sustainBoundOf: a re-strike stops the ring, so the tail may reach the next onset
    on its own string exactly and never pass it (40-Q2-B, \ref normalizeSustainOverlaps). Payload
    offsets lie within it.
    */
    Fraction sustain{};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*!
    \brief The fretting-hand stop UNDER a right-hand onset; absent when the hand states none.

    The one fact a note stream cannot otherwise carry about the fretting hand at an onset the OTHER
    hand produces: a two-hand tap sounds where the tapping finger lands, and the stop the fretting
    hand is holding below it is a different fret on the same string at the same instant. Two facts,
    one slot — which is exactly why this is a FIELD rather than a second note. A silently-held stop
    (\ref NoteAttack::None) is the same fact where no right hand sounds at all, and the two are read
    through one query (\ref claimedStop), never by testing the attack twice.

    Legal only where \ref rightHandOnset says the picking hand made the onset, because only there is
    the note's own fret NOT the fretting hand's — on every other attack the hand's stop is already
    \ref fret, and a second copy could only ever drift from it. That rule is enforced through the
    same fixpoint the pick slide's latents use (\ref savedChartNote strips it everywhere else), so
    no list of attacks has to be kept in step. Which means the field OUTLIVES an attack change in
    memory, exactly as those latents do, so nothing reads it bare: \ref claimedStop is the read, and
    it asks the attack for the same reason the writer does.

    Refused where it equals \ref fret: a stop that repeats the sounding fret states nothing (and the
    picking hand cannot sound the string at the very fret the other hand is stopping). Zero is a
    real statement rather than an absence — the open string deliberately left in the voicing — which
    is why this is an optional and not a sentinel. The board and the capo bind it exactly as they
    bind \ref fret.

    It is a CLAIM at this note's slot: the string becomes a posture string of the shape in force
    there, it counts toward the two-member threshold, it justifies a shape the hand alone stated,
    and a stop on a new string mid-shape splits that shape — all through the same rules a
    \ref NoteAttack::None note goes through, because they are the same statement.
    Design record: `docs/plans/todo/arpeggio-authoring.md`.
    */
    std::optional<int> held{};

    /*!
    \brief True when the picking hand's palm rests on the strings: still pitched, but damped.

    Independent of \ref dead rather than exclusive with it, because the two hands are doing two
    different things and can do them at once — a dead string inside a palm-muted chord is
    ordinary charting, and one mute axis could not write it down.
    */
    bool palm_mute{false};

    /*!
    \brief True when the string is deadened into an unpitched click.

    Named for the technique rather than for a hand: standard tab writes a dead note as an X, both
    surfaces draw that X, and either hand (or both) can be the one deadening the string — which is
    also why this is not "fret-hand mute".

    A note carrying both mutes SOUNDS and SCORES exactly as a dead note: the palm flag on it is
    charting truth about where the hand is, not a third sounding state. So anything asking what
    the string sounds like — the sounding rules here, the 2D X fill, the 3D head base — reads THIS
    flag alone and never both, while anything asking only whether a mark appears at all asks \ref
    isMuted.

    Nothing DRAWS a choice between the two: each mark comes from its own flag, so a both-muted note
    simply wears both (the palm mark and the dead X stack on one head, and the 2D lane pairs a
    white X with the palm hand's dark plate). No surface has, or needs, a "both" branch.
    */
    bool dead{false};

    /*!
    \brief String position of the harmonic node, in fret units — and the assertion that this
    note *is* a harmonic.

    There is no separate harmonic field: the node is what makes a note a harmonic, so its
    presence is the claim, and a node cannot disagree with a kind that no longer exists. Node
    points are not fret positions (the 3.2 / 2.7 / 5.8 family), which is why this is a `double`
    while `fret` stays the integer the fretting hand stops.

    Which hand damps the node is carried by `attack`: every attack damps with a finger on the
    neck except `Pinch`, whose thumb grazes the string over the body — ask `nodeIsOnNeck` rather
    than testing the attack directly. On a pinch the value is where the picking hand grazes,
    which no surface shows yet (roadmap 25-Q5).

    **Every** harmonic has one, a pinch included: the overtone that squeals is *determined* by
    where the thumb lands, so an absent node is missing data rather than a different technique,
    and `chart_rules` refuses a `Pinch` carrying none.
    */
    std::optional<double> harmonic_node{};

    /*!
    \brief Whether the string shakes at the ONSET — the vibrato channel's opening statement.

    An onset fact like the fret, not a whole-note flag: it holds from the onset until the first
    keyframe that states vibrato, and says nothing about the rest of the ring. A note whose shake
    runs end to end simply states it here and never states it again, which is what every chart
    written before the keyframe model says and why the key survived the change unaltered.
    */
    bool vibrato{false};

    /*!
    \brief True when the note is unmeasured noise picking — as fast as possible, no real
    timing.

    The charting standard reserves this for true noise (an outro strummed purely for sound);
    measured fast repetition is spelled out as discrete notes instead, so every timed pick is
    its own chart event. Deliberately named for the technique: tremolo picking is *pitched*
    noise — the fret still sets a measurable pitch — where a scrape is *unpitched* noise carried
    by its attack, which is why the two never share a field and a pick slide never sets this
    flag.
    */
    bool tremolo{false};

    /*! \brief How hard the note is struck relative to its neighbours. */
    NoteEmphasis emphasis{NoteEmphasis::Normal};

    /*!
    \brief How far the string is already pushed at the ONSET, in semitones; zero when unbent.

    The bend channel's opening value, and the whole of what a pre-bend is: the finger arrives with
    the string already bent, so there is no separate pre-bend flag to disagree with the amount. It
    interpolates toward the first keyframe that states a bend and holds flat when none does, which
    is why it is declared HERE, beside the array that continues it, rather than up among the
    onset flags.

    Never negative (W9-K, ratified 2026-08-25): a finger cannot lower a stopped string's pitch, so
    a downward push is a data error rather than a technique — dips and dives belong to the whammy
    bar's own model (`docs/plans/todo/whammy-bar-support.md`).
    */
    double bend{0.0};

    /*!
    \brief Everything that changes across the ring, in ascending offset order.

    One array for every interval channel (\ref Keyframe), because a glide target, a bend value and
    a vibrato start authored at one instant are one moment with three facts on it rather than
    three coincident copies of an offset. Empty when nothing changes after the onset.
    */
    std::vector<Keyframe> keyframes;

    /*!
    \brief Fret the unpitched falls-away gestures toward; absent when the tail simply ends.

    A slide-out has no offset of its own because it needs none: pressure releases off the note's
    END, so its moment is the ring's end by definition and a stored copy could only ever drift
    from it (W11). That also makes the terminal a scrape's required shape for free — a pick slide
    rings exactly as long as it travels — and it is why a truncation may park a slide-out exactly
    on the onset that silences the string.

    Never a sounded landing: a glide INTO a note is fret-stating keyframe data, and the note it
    arrives at renders its own head.
    */
    std::optional<int> slide_out{};

    /*!
    \brief Compares two notes by their stored fields.
    \param lhs Left-hand note.
    \param rhs Right-hand note.
    \return True when both notes store equal values.

    Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the bare
    \ref bend double. Exact equality is intended; the ordering query expresses it warning-free with
    identical semantics (NaN compares unequal either way). Every field is listed, so a field added
    above and forgotten here would silently compare equal — the one hazard the hand-written form
    carries.
    */
    friend bool operator==(const ChartNote& lhs, const ChartNote& rhs)
    {
        return lhs.position == rhs.position && lhs.string == rhs.string && lhs.fret == rhs.fret &&
               lhs.sustain == rhs.sustain && lhs.attack == rhs.attack && lhs.held == rhs.held &&
               lhs.palm_mute == rhs.palm_mute && lhs.dead == rhs.dead &&
               lhs.harmonic_node == rhs.harmonic_node && lhs.vibrato == rhs.vibrato &&
               lhs.tremolo == rhs.tremolo && lhs.emphasis == rhs.emphasis &&
               std::is_eq(lhs.bend <=> rhs.bend) && lhs.keyframes == rhs.keyframes &&
               lhs.slide_out == rhs.slide_out;
    }
};

/*!
\brief The fretting-hand stop this note CLAIMS at its slot, if any — the one claim query.

A shape is made of stops, and the chart records a stop the hand takes without sounding it in two
shapes for one reason: where nothing sounds at all the whole note is the statement
(\ref NoteAttack::None, whose \ref ChartNote::fret is the stop), and where the picking hand sounds
the string the fretting hand's stop rides the note as \ref ChartNote::held. They are the same
statement about the same hand at the same slot, so the span derivation, the inert sweep and the
posture display ask THIS rather than testing the attack and then reading a different field per
branch — two spellings that would be free to disagree about what a claim is.

Absent on every note whose own \ref ChartNote::fret already IS the fretting hand's stop: there is
nothing extra to claim, because the note itself is the claim the ordinary posture rules already
read.

Asked of the ATTACK on both arms, so the answer is the same for a note and for its saved form. The
field survives in memory on an attack that may not carry it — an attack change leaves it behind
exactly as it leaves a scrape's pitched techniques behind, for the same reason: changing back must
restore what the charter typed, and \ref savedChartNote is what keeps it out of the document. A
claim query that read the bare field would therefore see a stop no surface draws, and the settle
that judges claims runs on the in-memory stream.

\param note Note to ask.

\return The claimed stop, or nothing when the note claims none beyond its own sounding fret.
*/
[[nodiscard]] constexpr std::optional<int> claimedStop(const ChartNote& note) noexcept
{
    if (silentHold(note.attack))
    {
        return note.fret;
    }
    return rightHandOnset(note.attack) ? note.held : std::nullopt;
}

/*!
\brief What each channel of a ringing note has STATED at one instant along its ring.

The one reading of the onset-facts-plus-keyframes split
(`docs/plans/todo/unified-waypoint-model.md`): a channel opens on the note itself and every later
change lands on a keyframe, so "what is in force here" is a fold over the two — and every reader
folding it by hand was a second copy of the model's semantics, free to disagree with the first.

The STATEMENT in force, never the sounding value. Between two statements the position channel is
travelling and the bend channel is climbing its curve, and both are interpolated by the surfaces
that draw them (\ref Keyframe); this says what the last statement at or before the instant was.
For the discrete channels that IS what sounds; for the bend it is the value the curve is
interpolating away from.
*/
struct RingState
{
    /*! \brief Fret last stated; the note's own fret until a keyframe states another. */
    int fret{0};

    /*! \brief Bend last stated in semitones; the note's onset value until a keyframe restates. */
    double bend{0.0};

    /*! \brief Vibrato state in force; the note's onset state until a keyframe states another. */
    bool vibrato{false};

    /*!
    \brief Applies one keyframe's statements, leaving every channel it does not state alone.

    \param keyframe Keyframe whose statements advance the running state.
    */
    void advance(const Keyframe& keyframe) noexcept
    {
        // `value_or` rather than a has_value() branch per channel: a keyframe stating nothing about
        // a channel is pass-through for it by definition, which is exactly what carrying the
        // running value forward says — and it keeps each optional access total, which the CI-only
        // unchecked-optional-access checker credits where a guard on a loop variable's member is
        // not.
        fret = keyframe.fret.value_or(fret);
        bend = keyframe.bend.value_or(bend);
        vibrato = keyframe.vibrato.value_or(vibrato);
    }
};

/*!
\brief The state every channel opens with: the note's own onset facts.

Where a fold over the ring begins, and the whole of what the split means — the onset is not a
keyframe (\ref Keyframe), so the opening value of each channel is read from the note itself here
and nowhere else.

\param note Note whose ring is being read.

\return The state in force from the onset until the first keyframe that states a channel.
*/
[[nodiscard]] inline RingState ringStateAtOnset(const ChartNote& note) noexcept
{
    return RingState{.fret = note.fret, .bend = note.bend, .vibrato = note.vibrato};
}

/*!
\brief The state in force at an offset along a note's ring.

A statement standing exactly AT the instant counts, which is what makes a channel's value at a
keyframe the value that keyframe states rather than the one it replaces.

Readers that need every keyframe's before-and-after — the change detection the presentation trim
runs, the regions the vibrato channel states — fold \ref ringStateAtOnset and \ref
RingState::advance themselves rather than sampling this per keyframe, which would walk the array
once per entry to learn what one pass already knows.

\param note Note whose ring is read.
\param offset Beat-fraction offset from the note's onset.

\return Every channel's statement in force at that instant.
*/
[[nodiscard]] inline RingState ringStateAt(const ChartNote& note, const Fraction offset)
{
    RingState state = ringStateAtOnset(note);
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (offset < keyframe.offset)
        {
            // Keyframes ascend, so nothing from here on is in force at the instant.
            break;
        }
        state.advance(keyframe);
    }
    return state;
}

/*!
\brief Reports whether the note's bend channel says anything at all.

The onset value is always a statement, so "is this note bent" is not "are there bend keyframes":
a pre-bend states its whole curve at the onset and nowhere else. Equally, a channel that opens at
rest and is never restated is no curve — the note simply never bends — which is why a lone zero is
not enough. Spelled once because the projection asks it to decide whether a curve is drawn at all
and the importer asks it before folding a merged note's curve into a neighbour, and a flat zero
statement authored by one and not the other would be a curve nobody wrote.

\param note Note to classify.

\return True when the note opens bent or states a bend anywhere along its ring.
*/
[[nodiscard]] inline bool noteIsBent(const ChartNote& note) noexcept
{
    if (std::is_neq(note.bend <=> 0.0))
    {
        return true;
    }
    for (const Keyframe& keyframe : note.keyframes)
    {
        if (keyframe.bend.has_value())
        {
            return true;
        }
    }
    return false;
}

/*!
\brief The chart's SLOT order: ascending position, then ascending string.

A `(position, string)` pair is a slot, and the chart's one per-string authored array — the notes,
silent holds included — is kept in this order, holding no slot twice. Stated separately from
\ref chartNoteOrderLess because a SLOT is what the editor's selection keys and hit resolution
address, so the order they merge by is named for the thing they name rather than for the record
that happens to occupy it.

\param lhs_position Left-hand position.
\param lhs_string Left-hand string.
\param rhs_position Right-hand position.
\param rhs_string Right-hand string.
\return True when the left slot comes strictly before the right one.
*/
[[nodiscard]] constexpr bool chartSlotOrderLess(
    const GridPosition& lhs_position, const int lhs_string, const GridPosition& rhs_position,
    const int rhs_string) noexcept
{
    return lhs_position < rhs_position || (lhs_position == rhs_position && lhs_string < rhs_string);
}

/*!
\brief The chart's note order: ascending onset, then ascending string.

Every note stream is kept in this order — the validator refuses one that is not, the editor's
planners restore it before gating a candidate, and every cursor walk over a stream assumes it —
so it is stated once. Two notes equal under it are the same slot, which no chart may hold twice.

\param lhs Left-hand note.
\param rhs Right-hand note.
\return True when lhs comes strictly before rhs in the chart's order.
*/
[[nodiscard]] constexpr bool chartNoteOrderLess(const ChartNote& lhs, const ChartNote& rhs) noexcept
{
    return chartSlotOrderLess(lhs.position, lhs.string, rhs.position, rhs.string);
}

/*!
\brief Returns the fret the note's unpitched slide-out gestures toward, as a nullable pointer.
\param note Note whose tail is inspected.
\return Address of the slide-out's fret when present, or nullptr when the tail simply ends.

Binding the optional behind a parameter lets call sites null-check instead of dereferencing an
optional, and keeps clang-tidy's unchecked-optional-access analysis reliable inside note loops,
where a has_value() guard on the loop variable's own member is not otherwise credited.
*/
[[nodiscard]] inline const int* slideOutFretOrNull(const ChartNote& note) noexcept
{
    return note.slide_out.has_value() ? &*note.slide_out : nullptr;
}

/*!
\brief The fret the **fretting hand** occupies for this note.

Not the same as `note.fret`, which is the **stop**. A fret-hand harmonic — `fret == 0` plus a
node, with neither tapping-hand attack — holds no stop, so the hand is at the node, the only
place it touches the string. Every other node-bearing note keeps the hand on its fret: a pinch
and a two-hand tap because the node belongs to the picking hand, and a harmonic over a real stop
(`fret > 0` — the harp and artificial-harmonic family) because the fretting hand is pressing that
stop while the picking hand damps the node.

Fret `N` occupies the neck from wire `N-1` to wire `N` (`highwayNoteCenterX` is the midpoint of
those two), so the fret containing a node at `p` is `ceil(p)`: 2.669 lies in fret 3 and 3.156 in
fret 4. **Neither `round` nor `floor` works.** A fret-hand window over frets `[f, f+w-1]` covers
fret units `[f-1, f+w-1]`, so when the harmonic is the window's edge note the window only reliably
covers `[H-1, H]` for the fret `H` it was given. Measured over every node below fret 25, `floor`
leaves the head outside the window 18 times and `round` 7 times; `ceil` never does.

\param note Note to place.

\return Fret the fretting hand is on; zero when nothing stops the string.
*/
[[nodiscard]] int fretFor(const ChartNote& note);

/*!
\brief \ref fretFor on the raw fields, for the view-state twins that mirror them.
\param fret Stored fret; zero is the open string.
\param harmonic_node Harmonic node when the note is a harmonic.
\param attack How the onset is produced.
\return Fret the fretting hand is on; zero when nothing stops the string.
*/
[[nodiscard]] int fretFor(int fret, const std::optional<double>& harmonic_node, NoteAttack attack);

/*!
\brief Formats a harmonic node for display: one decimal, dropped when whole.

The one label authority for both surfaces — the 2D head text and the 3D floor numbers print a
node through this, so 2.311741 reads "2.3" everywhere and the two can never round apart.

\param node Node position in fret units; never negative.
\return "2.3" for fractional nodes, "12" for whole ones.
*/
[[nodiscard]] std::string harmonicNodeText(double node);

/*!
\brief The note as a saved document records it: everything its attack cannot carry stripped.

The one seam between memory and document, and the one authority on what each attack may state. A
pick slide overrides the pitched techniques in memory — kept so toggling the attack back restores
them — but a saved scrape never carries them; a silent hold (\ref NoteAttack::None) states its stop
and nothing else at all, ring included; and \ref ChartNote::held survives only under a right-hand
onset, because only there is the note's own fret not the fretting hand's. The writer emits this form
and
\ref validateChartNoteAlone refuses any note that is not already equal to it, so the two can never
disagree about what a legal document is, and a technique field added to \ref ChartNote later is
refused on both attacks by the one rule instead of needing a row in a list.

\param note Note as held in memory.

\return The note as the document writer records it.
*/
[[nodiscard]] ChartNote savedChartNote(const ChartNote& note);

/*!
\brief True when the note is a harmonic damped by the fretting hand touching its node.

The key E7/E9 rules and the resolver's release clause turn on: a node with no real stop (`fret == 0`
— the string speaks from the nut or the capo) and no attack whose node belongs to the OTHER hand or
to nothing at all. `Pinch` is excluded because its thumb grazes off the neck. `PickSlide` is
excluded because a scrape's node is never a sounding node at all: E2 forbids one in any saved chart,
so a node found on a scrape is purely the in-memory latent the attack toggle preserves (chart.h's
override contract), and reading it as a fretting-hand touch made two things go wrong at once — the
connection resolver refused to release from a scrape while the SAVED stream it is contracted to
judge (where the node is stripped) says there is nothing to refuse, so a pull the released-fret
semantics rule valid silently resolved to nothing; and the normalizer's fret-hand-harmonic stage
stripped the scrape's REQUIRED slide-out terminal, producing a chart that E2 then rejected on
re-read. `Tap` is NOT excluded — an open-string tap harmonic has
nothing pressed either, which is exactly what those rules test. Contrast `fretFor`'s node branch,
which additionally excludes `Tap` because the hand-placement question cares which HAND owns the
node, not whether a stop is pressed.

\param note Note to classify.

\return True when the fretting hand touches the node and presses nothing.
*/
[[nodiscard]] inline bool fretHandHarmonic(
    const int fret, const std::optional<double>& harmonic_node, const NoteAttack attack) noexcept
{
    // `nodeIsOnNeck` rather than a spelled-out pinch test, so this, the dead-pinch rule, and the
    // placement rules cannot drift apart if another off-neck harmonic is ever added.
    return harmonic_node.has_value() && fret == 0 && nodeIsOnNeck(attack) && !isScrape(attack);
}

/*!
\brief The stop a note's string speaks from: its own fret, or the capo when the string is open.

Fret 0 means the open string under the 0-means-open convention, so the stop it names is the nut or
the capo — the capo is what stops a capo'd string. The one spelling of that fact, read by the
node-beyond-the-stop rule, the pinch's default node (the octave above the stop), and the importer's
harmonic placement, which used to carry three copies of the same conditional.

\param note Note whose stop is wanted.
\param capo The tuning's capo fret; 0 for none.

\return The stop fret.
*/
[[nodiscard]] constexpr int physicalStopFret(const ChartNote& note, const int capo) noexcept
{
    return note.fret == 0 ? capo : note.fret;
}

/*! \copydoc fretHandHarmonic(int,const std::optional<double>&,NoteAttack) */
[[nodiscard]] inline bool fretHandHarmonic(const ChartNote& note) noexcept
{
    return fretHandHarmonic(note.fret, note.harmonic_node, note.attack);
}

/*!
\brief True when the note's attack strikes from nowhere but has nothing to strike — E4's whole test.

Two attacks strike a note into existence with no pick stroke and no predecessor to come from: the
left-hand tap and the two-hand tap. Both need a place to land — a fret, or a harmonic's node, which
the tap harmonic strikes directly — and an open string with neither is not a quiet tap but a note
nothing produced. A `Legato` claim is deliberately not among them: whether it strikes at all is
\ref resolveLegato's answer, so an open string it cannot justify simply plays as the pick it sounds
like.

Spelled once because three callers ask it for three different purposes and any drift between them
would be a silent corruption: validation REFUSES such a note, the editor's plan finalize FLATTENS
the attack to a pick (the truth is the note's own, so the repair rides the edit that stranded it),
and the importer flattens it early, before the passes that read the attack shape a song around one
that cannot survive.

\param note Note to classify.

\return True when the attack strikes from nowhere and nothing is there to strike.
*/
[[nodiscard]] inline bool nothingToStrike(const ChartNote& note) noexcept
{
    return (note.attack == NoteAttack::LeftTap || note.attack == NoteAttack::Tap) &&
           note.fret == 0 && !note.harmonic_node.has_value();
}

/*! \brief Where a note sounds on the fret axis, and whether that place is a node or a fret. */
struct SoundingPosition
{
    /*! \brief True when the position is a harmonic NODE rather than a fret. */
    bool at_node{false};

    /*!
    \brief The position in fret units — fractional for a node, the stop's own number otherwise.
    */
    double position{0.0};
};

/*!
\brief Where a note SOUNDS on the fret axis at a given stop.

The one authority for a fact both surfaces need and each used to derive: a harmonic sounds at its
NODE, not at the stop under the finger, and the node RIDES that stop — fret spacing is logarithmic,
so the node's offset above the stop is constant in fret units and a glide that moves the stop moves
the node by the same amount. That is what lets one rule serve every point of a gesture: the onset
passes the note's own fret, a slide junction the fret it has travelled to.

A pinch is the exception the `at_node` flag exists for as much as the position is: its node is over
the body rather than on the neck, so a pinch sounds at its stop as far as any neck coordinate goes
(the squeal's own cue is roadmap 25-Q5). Callers need the flag because a node and a fret are read
differently — 2D labels a node to one decimal and a fret as a whole number, 3D places a node on the
fret line and a fret at its slot's midpoint.

\param harmonic_node The note's node, if it has one.
\param attack The note's attack, which decides whose hand owns the node.
\param note_fret The note's own stop.
\param fret_at_point The stop being asked about — `note_fret` at the onset.

\return The sounding place, and whether it is a node.
*/
[[nodiscard]] constexpr SoundingPosition soundingPositionAt(
    const std::optional<double>& harmonic_node, NoteAttack attack, int note_fret,
    int fret_at_point) noexcept
{
    if (harmonic_node.has_value() && nodeIsOnNeck(attack))
    {
        return SoundingPosition{
            .at_node = true,
            .position = *harmonic_node + static_cast<double>(fret_at_point - note_fret),
        };
    }
    return SoundingPosition{.at_node = false, .position = static_cast<double>(fret_at_point)};
}

/*!
\brief True when the FRETTING finger is standing on the note's node.

A refinement of \ref fretHandHarmonic rather than a near twin, and it is spelled as one so the two
can never drift: that predicate asks whether anything is PRESSED at a node on the neck, and this
adds the one further question of which HAND owns it. A two-hand tap harmonic's node belongs to the
picking hand, which is on the neck rather than off it, so a tap is the single exclusion.

Restating the conditions here instead let the two disagree about a pick slide. This one used to
read `nodeIsOnNeck`, which excludes only a pinch, so a scrape carrying a latent node at fret 0
counted as a fretting finger and \ref fretFor sent the hand to the node — exactly the failure
\ref fretHandHarmonic excludes the scrape to avoid.

Two things turn on this one fact and each used to spell it out: where the fretting hand sits
(\ref fretFor returns the node's fret instead of the note's), and how far up the node may lie — a
finger cannot be past the last fret, so the neck caps it rather than the string.

Deliberately NOT what the 3D board asks when placing a note: a note sounds from its node whichever
hand is damping it, so the board's own axis ignores which hand that is.

\param note Note to classify.

\return True when the fretting hand's finger is the one touching the node.
*/
[[nodiscard]] inline bool frettingFingerOnNode(
    const int fret, const std::optional<double>& harmonic_node, const NoteAttack attack) noexcept
{
    return fretHandHarmonic(fret, harmonic_node, attack) && attack != NoteAttack::Tap;
}

/*! \copydoc frettingFingerOnNode(int,const std::optional<double>&,NoteAttack) */
[[nodiscard]] inline bool frettingFingerOnNode(const ChartNote& note) noexcept
{
    return frettingFingerOnNode(note.fret, note.harmonic_node, note.attack);
}

/*!
\brief The fret the note's finger occupies when the note ends — what a following pull-off
releases from.

The position channel read at the ring's END through the one authority (\ref ringStateAt), which is
where the pass-through rule comes from: a note that glided hands over its last fret-STATING
keyframe rather than its onset fret (a 5→7 slide releases from 7), while keyframes stating only a
bend or a vibrato change say nothing about position and carry the running fret forward. An
unpitched trail-off is already a release, so the last stated position still rules. Meaningful only
for a note a finger actually stops: a scrape's travel is the pick's position, which is why the
connection resolver disqualifies a scrape before ever asking this.

\param note Note whose end position is read.

\return Fret at the note's end.
*/
[[nodiscard]] inline int releasedFret(const ChartNote& note)
{
    return ringStateAt(note, note.sustain).fret;
}

/*! \brief Fret-hand position: where the hand sits on the neck from this point on. */
struct FretHandPosition
{
    /*! \brief Musical position the hand arrives at this placement. */
    GridPosition position;

    /*! \brief Lowest fret under the index finger. */
    int fret{1};

    /*! \brief Fret span covered by the hand; four unless the passage stretches wider. */
    int width{4};

    /*!
    \brief Compares two fret-hand positions by their stored fields.
    \param lhs Left-hand entry.
    \param rhs Right-hand entry.
    \return True when both entries store equal values.
    */
    friend constexpr bool operator==(
        const FretHandPosition& lhs, const FretHandPosition& rhs) noexcept = default;
};

/*! \brief Instrument tuning for one arrangement. */
struct ChartTuning
{
    /*!
    \brief Open-string pitches from the lowest-pitched string upward, as note names with octave
    such as "E2". The array length defines the arrangement's string count everywhere.
    */
    std::vector<std::string> strings;

    /*! \brief Capo fret; zero means no capo. */
    int capo{0};

    /*! \brief Fine tuning offset in cents. */
    double cent_offset{0.0};

    /*!
    \brief Compares two tunings by their stored fields.
    \param lhs Left-hand tuning.
    \param rhs Right-hand tuning.
    \return True when both tunings store equal values.
    */
    friend bool operator==(const ChartTuning& lhs, const ChartTuning& rhs)
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.strings == rhs.strings && lhs.capo == rhs.capo &&
               std::is_eq(lhs.cent_offset <=> rhs.cent_offset);
    }
};

/*!
\brief The true tab of one arrangement.

Notes say where each finger goes and, for all but one attack, what it sounds; the hand placements
say where the hand sits on the neck. What the hand HOLDS — the chord boxes and arpeggio brackets
both surfaces draw — is derived from those two rather than stored beside them
(\ref deriveChartShapes), because a span is a statement about the notes under it and a stored one
could only ever disagree with them. There is exactly one chart per arrangement — difficulty is a
derived rating, never authored variants.
*/
struct Chart
{
    /*! \brief Instrument tuning; the strings array length is the string count everywhere. */
    ChartTuning tuning;

    /*!
    \brief Every stop the fretting hand takes, sorted by (position, string), one slot at most once.

    Almost all of them sound; a \ref NoteAttack::None entry is the one that does not, and it is in
    this array rather than beside it because a silently-held shape member IS a stop on a string at
    an instant — the same slot, the same fret, the same verbs — with only the stroke missing. A
    second array keyed the same way would have reused one slot space for two record kinds, so
    disjointness had to be enforced by a rule; here it is slot uniqueness, which this array already
    owed.
    */
    std::vector<ChartNote> notes;

    /*! \brief Fret-hand positions, sorted by position. */
    std::vector<FretHandPosition> fret_hand_positions;

    /*!
    \brief Compares two charts by their stored fields.
    \param lhs Left-hand chart.
    \param rhs Right-hand chart.
    \return True when both charts store equal values.
    */
    friend bool operator==(const Chart& lhs, const Chart& rhs) = default;
};

} // namespace rock_hero::common::core
