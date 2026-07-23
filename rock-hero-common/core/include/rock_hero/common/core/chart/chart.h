/*!
\file chart.h
\brief Arrangement-owned chart model: the true tab of notes, shapes, and postures.
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
    /*! \brief Hammer-on from the previous note. */
    Hammer,
    /*! \brief Pull-off from the previous note. */
    Pull,
    /*! \brief Two-hand tap onset. */
    Tap,
    /*! \brief Popped (bass) onset. */
    Pop,
    /*! \brief Slapped (bass) onset. */
    Slap
};

/*! \brief Muting applied to a note. */
enum class NoteMute : std::uint8_t
{
    /*! \brief No muting. */
    None,
    /*! \brief Palm mute: pitched but damped. */
    Palm,
    /*! \brief Full fret-hand mute: percussive, unpitched. */
    Full
};

/*! \brief Harmonic timbre applied to a note. */
enum class NoteHarmonic : std::uint8_t
{
    /*! \brief No harmonic. */
    None,
    /*! \brief Natural harmonic at a string node. */
    Natural,
    /*! \brief Pinch harmonic. */
    Pinch
};

/*! \brief One point of a bend curve, positioned relative to the note onset. */
struct BendPoint
{
    /*! \brief Beat-fraction offset from the note onset, within the sustain. */
    Fraction offset{};

    /*! \brief Bend amount in semitones at this point; 0.5 is a quarter-tone curl. */
    double semitones{0.0};

    /*!
    \brief Compares two bend points by their stored fields.
    \param lhs Left-hand bend point.
    \param rhs Right-hand bend point.
    \return True when both points store equal values.
    */
    friend constexpr bool operator==(const BendPoint& lhs, const BendPoint& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating member. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.offset == rhs.offset && std::is_eq(lhs.semitones <=> rhs.semitones);
    }
};

/*!
\brief One pitched slide-curve waypoint: by this offset the fret hand has glided to the fret.

Waypoints describe the note's own pitch curve — legato junctions, holds, and shift-slide glides
toward a re-picked landing — and are always pitched; the only unpitched gesture is the separate
\ref SlideOut terminal, so an "unpitched middle" cannot be written. A waypoint never sits on a
later onset of its own string: a shift-slide glide ends the minimum sustain distance before its
landing, and the landing note renders its own head.
*/
struct SlideWaypoint
{
    /*! \brief Beat-fraction offset from the note onset; strictly positive, within the sustain. */
    Fraction offset{};

    /*! \brief Target fret reached at this offset. */
    int fret{0};

    /*!
    \brief Compares two slide waypoints by their stored fields.
    \param lhs Left-hand waypoint.
    \param rhs Right-hand waypoint.
    \return True when both waypoints store equal values.
    */
    friend constexpr bool operator==(const SlideWaypoint& lhs, const SlideWaypoint& rhs) noexcept =
        default;
};

/*!
\brief Unpitched slide-out: pressure releases and the pitch falls away off the note's end.

No landing note exists — that is what distinguishes a slide-out from a pitched glide, which is
plain \ref SlideWaypoint data — so the gesture legitimately owns its own end offset and target
fret; there is no other event to desync from.
*/
struct SlideOut
{
    /*! \brief Beat-fraction offset from the note onset where the slide-out ends; strictly after
        every curve waypoint, within the sustain. */
    Fraction offset{};

    /*! \brief Fret the slide-out gestures toward; never a sounded landing. */
    int fret{0};

    /*!
    \brief Compares two slide-outs by their stored fields.
    \param lhs Left-hand slide-out.
    \param rhs Right-hand slide-out.
    \return True when both store equal values.
    */
    friend constexpr bool operator==(const SlideOut& lhs, const SlideOut& rhs) noexcept = default;
};

/*!
\brief One string sounding once: the only event kind in the note stream.

A strummed chord is simultaneous notes at one position; shape spans supply the notation layer.
A zero sustain means the note has no sustain tail.
*/
struct ChartNote
{
    /*! \brief Musical onset position. */
    GridPosition position;

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*! \brief Fret sounded; zero is the open string. */
    int fret{0};

    /*! \brief Sustain duration in beats; zero means no sustain. */
    Fraction sustain{};

    /*! \brief How the onset is produced. */
    NoteAttack attack{NoteAttack::Pick};

    /*! \brief Muting applied to the note. */
    NoteMute mute{NoteMute::None};

    /*! \brief Harmonic timbre applied to the note. */
    NoteHarmonic harmonic{NoteHarmonic::None};

    /*!
    \brief Precise fractional touch position for harmonics that sound between frets.

    Natural-harmonic node points are not fret positions (the 3.2 / 2.7 / 5.8 family), so
    harmonic notes may carry the exact touch position here while `fret` stays the integer
    display anchor. Only meaningful when `harmonic` is set; absent when the touch position is
    the fret itself.
    */
    std::optional<double> touch{};

    /*! \brief True when the note is played with vibrato. */
    bool vibrato{false};

    /*! \brief True when the note is tremolo picked. */
    bool tremolo{false};

    /*! \brief True when the note is accented. */
    bool accent{false};

    /*! \brief Bend curve across the sustain; empty when the note is not bent. */
    std::vector<BendPoint> bend;

    /*! \brief Pitched slide-curve waypoints across the sustain; empty when the curve is flat. */
    std::vector<SlideWaypoint> slides;

    /*! \brief Unpitched slide-out off the note's end; absent when the tail simply ends. */
    std::optional<SlideOut> slide_out{};

    /*!
    \brief Compares two notes by their stored fields.
    \param lhs Left-hand note.
    \param rhs Right-hand note.
    \return True when both notes store equal values.
    */
    friend bool operator==(const ChartNote& lhs, const ChartNote& rhs) = default;
};

/*!
\brief Returns the note's unpitched slide-out as a nullable pointer.
\param note Note whose tail is inspected.
\return Address of the slide-out when present, or nullptr when the tail simply ends.

Binding the optional behind a parameter lets call sites null-check instead of dereferencing an
optional, and keeps clang-tidy's unchecked-optional-access analysis reliable inside note loops,
where a has_value() guard on the loop variable's own member is not otherwise credited.
*/
[[nodiscard]] inline const SlideOut* slideOutOrNull(const ChartNote& note) noexcept
{
    return note.slide_out.has_value() ? &*note.slide_out : nullptr;
}

/*!
\brief Reusable hand posture: per-string frets and fingerings.

Array index 0 is the lowest-pitched string; null entries mean the string is not part of the
posture. Fingers use 0 for the thumb and 1-4 for index through pinky.
*/
struct ChordTemplate
{
    /*! \brief Display name; may be empty for unnamed shapes. */
    std::string name;

    /*! \brief Fret held per string; nullopt when the string is not part of the posture. */
    std::vector<std::optional<int>> frets;

    /*! \brief Finger per string; nullopt when unspecified or unused. */
    std::vector<std::optional<int>> fingers;

    /*!
    \brief Compares two templates by their stored fields.
    \param lhs Left-hand template.
    \param rhs Right-hand template.
    \return True when both templates store equal values.
    */
    friend bool operator==(const ChordTemplate& lhs, const ChordTemplate& rhs) = default;
};

/*!
\brief Hand-posture span referencing a chord template.

One mechanism covers strummed chords, chugged riffs on a held shape, and arpeggios: the notes
under the span are the sounding truth, the shape adds the notation layer (name, box or bracket,
fingering). Whether the span renders as a chord box or an arpeggio bracket derives from whether
its notes arrive together or sequentially.
*/
struct ChartShape
{
    /*! \brief Musical start of the span. */
    GridPosition position;

    /*! \brief Span duration in beats; strictly positive. */
    Fraction sustain{};

    /*! \brief Index into the chart's chord template table. */
    std::size_t chord{0};

    /*!
    \brief Compares two shapes by their stored fields.
    \param lhs Left-hand shape.
    \param rhs Right-hand shape.
    \return True when both shapes store equal values.
    */
    friend bool operator==(const ChartShape& lhs, const ChartShape& rhs) = default;
};

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

Notes say what sounds; shapes say what the hand holds; templates are reusable postures. There is
exactly one chart per arrangement — difficulty is a derived rating, never authored variants.
*/
struct Chart
{
    /*! \brief Instrument tuning; the strings array length is the string count everywhere. */
    ChartTuning tuning;

    /*! \brief Reusable hand postures referenced by shapes, in table order. */
    std::vector<ChordTemplate> templates;

    /*! \brief Every sounding onset, sorted by (position, string). */
    std::vector<ChartNote> notes;

    /*! \brief Hand-posture spans, sorted by position. */
    std::vector<ChartShape> shapes;

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
