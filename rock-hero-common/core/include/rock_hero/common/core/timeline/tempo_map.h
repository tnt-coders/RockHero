/*!
\file tempo_map.h
\brief Song-level beat grid with time-signature changes and sparse timing anchors.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <rock_hero/common/core/timeline/fraction.h>
#include <rock_hero/common/core/timeline/grid_position.h>
#include <rock_hero/common/core/timeline/timeline.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief A meter: how many beats a measure holds, and which note value one beat is. */
struct TimeSignature
{
    /*! \brief Beats per measure. */
    int numerator{4};

    /*! \brief Note value that represents one beat. */
    int denominator{4};

    /*!
    \brief Returns how long one measure lasts on the whole-note axis.

    THE ONE statement of a measure's length: `numerator` beats of one `denominator` note each.

    \return The measure's length in whole notes.
    */
    [[nodiscard]] constexpr Fraction measureWholeNotes() const noexcept
    {
        return Fraction{numerator, denominator};
    }

    /*!
    \brief Compares two meters by their stored fields.
    \param lhs Left-hand meter.
    \param rhs Right-hand meter.
    \return True when both meters store equal values.
    */
    friend constexpr bool operator==(const TimeSignature& lhs, const TimeSignature& rhs) noexcept =
        default;
};

static_assert(sizeof(TimeSignature) <= 16);
static_assert(std::is_trivially_copyable_v<TimeSignature>);

/*! \brief Time signature that starts at a measure and carries forward. */
struct TimeSignatureChange
{
    /*! \brief One-based measure where this time signature begins. */
    int measure{1};

    /*! \brief The meter that governs from this measure on. */
    TimeSignature meter{};

    /*!
    \brief Compares two time-signature changes by their stored fields.
    \param lhs Left-hand time-signature change.
    \param rhs Right-hand time-signature change.
    \return True when both changes store equal values.
    */
    friend bool operator==(const TimeSignatureChange& lhs, const TimeSignatureChange& rhs) =
        default;
};

/*! \brief Pins one grid position to one instant of the session timeline. */
struct BeatAnchor
{
    /*! \brief Where on the grid: any tick-lattice position (`measure:beat[+n/d]`). */
    GridPosition position{};

    /*!
    \brief When: whole milliseconds from the timeline origin.

    The one absolute time a song's musical content stores; every finer value is rounded once at
    its boundary by \ref nearestAnchorMilliseconds and never stored.
    */
    std::int64_t milliseconds{0};

    /*!
    \brief Returns the anchor's time as seconds, the unit every time query speaks.

    The one milliseconds-to-seconds conversion.

    \return The anchor's time in seconds.
    */
    [[nodiscard]] constexpr double seconds() const noexcept
    {
        // Division, not multiplication by a thousandth: correctly rounded, so it is bit-identical
        // to the double the file's three-decimal literal parses to.
        return static_cast<double>(milliseconds) / 1000.0;
    }

    /*!
    \brief Compares two beat anchors by their stored fields.
    \param lhs Left-hand beat anchor.
    \param rhs Right-hand beat anchor.
    \return True when both anchors store equal values.
    */
    friend constexpr bool operator==(const BeatAnchor& lhs, const BeatAnchor& rhs) noexcept =
        default;
};

/*!
\brief Reports whether a time has a whole millisecond an anchor can store.

The guard every boundary taking an untrusted time asks before \ref nearestAnchorMilliseconds: the
rounding of a time past the stored integer's range is not a number any platform agrees on.

\param seconds Time in seconds.
\return True when the time is finite and its nearest whole millisecond fits a 64-bit integer.
*/
[[nodiscard]] bool isRepresentableAnchorTime(double seconds) noexcept;

/*!
\brief Rounds a time to the nearest whole millisecond an anchor can store.

The one seconds-to-milliseconds rounding, asked once at each boundary a finer time arrives through.
Halves round away from zero.

\param seconds Time in seconds; must satisfy \ref isRepresentableAnchorTime (finite and in range).
\return The nearest whole millisecond.
*/
[[nodiscard]] std::int64_t nearestAnchorMilliseconds(double seconds) noexcept;

/*!
\brief Song-level warp-anchor beat grid.

TempoMap stores the durable native chart timing model: time-signature changes define the musical
grid, while anchors pin sparse grid positions to absolute times. All intermediate beat and note
positions resolve by linear interpolation between neighboring anchors in quarter-note (metronome)
time: the metronome rate is constant inside each anchor span, so a meter-denominator change
between anchors re-slices beat durations (an eighth-note beat runs twice as fast as a quarter-note
beat) instead of stretching them. Construction precomputes signature-segment and anchor indices so
address and time queries stay logarithmic in the authored list sizes instead of rescanning them;
per-frame consumers such as the timeline grid scan rely on that bound.
*/
class TempoMap
{
public:
    /*! \brief Creates a minimal 4/4 map with one content measure at 120 BPM. */
    TempoMap();

    /*!
    \brief Creates a tempo map from already validated changes and anchors.
    \param time_signatures Time signatures carried forward from their starting measure.
    \param anchors Sparse absolute timing anchors.
    */
    TempoMap(std::vector<TimeSignatureChange> time_signatures, std::vector<BeatAnchor> anchors);

    /*!
    \brief Creates a simple 4/4 120 BPM map that covers an audio duration.
    \param audio_duration Natural duration of the backing audio.
    \return Default tempo map with a terminal downbeat beyond the supplied duration.
    */
    [[nodiscard]] static TempoMap defaultMap(TimeDuration audio_duration);

    /*!
    \brief Returns the stored time-signature changes.
    \return Time-signature change sequence.
    */
    [[nodiscard]] const std::vector<TimeSignatureChange>& timeSignatures() const noexcept;

    /*!
    \brief Returns the stored absolute timing anchors.
    \return Beat-anchor sequence.
    */
    [[nodiscard]] const std::vector<BeatAnchor>& anchors() const noexcept;

    /*!
    \brief Finds the time signature active at a measure.
    \param measure One-based measure to query.
    \return Time signature carried into the supplied measure.
    */
    [[nodiscard]] TimeSignatureChange timeSignatureAt(int measure) const noexcept;

    /*!
    \brief Finds the time signature active at an absolute second position.

    Positions before the first signature's downbeat use the first signature, which governs from
    measure 1.

    \param seconds Absolute second position to query.
    \return Time signature whose reign contains the supplied position.
    */
    [[nodiscard]] TimeSignatureChange timeSignatureAtSeconds(double seconds) const noexcept;

    /*!
    \brief Resolves an absolute second position to a fractional global beat position.

    Inverse of secondsAtGlobalBeatPosition: inverse-interpolates the containing anchor span on the
    metronome axis and converts back to beats through the signature segments. Positions outside
    the authored anchor range clamp to the first or terminal anchor's beat.

    \param seconds Absolute second position to resolve.
    \return Zero-based fractional position on the global beat axis.
    */
    [[nodiscard]] double beatPositionAtSeconds(double seconds) const noexcept;

    /*!
    \brief Re-times an instant resolved under another map to where this map puts the same music.

    The time a tempo edit gives an instant it does not re-address: the music at \p seconds under
    \p origin keeps its place, and this map answers its new time. A span whose two anchors this map
    shares with \p origin re-times nothing, so the answer is exactly \p seconds there — and
    everywhere when the maps are equal. A time outside \p origin's anchors stands before or after
    the music, so it keeps its time too.

    \param origin Map \p seconds was resolved under; it must state the same meters as this map.
    \param seconds Absolute second position under \p origin.
    \return The same music's second position under this map.
    */
    [[nodiscard]] double secondsRetimedFrom(const TempoMap& origin, double seconds) const noexcept;

    /*!
    \brief Returns the quarter-note length of one anchor span.

    Span \p span runs from anchor \p span to the anchor after it, so its tempo is this length over
    the two anchors' seconds; a meter change inside the span does not alter it.

    \param span Zero-based span index.
    \return Quarter notes between the span's two anchors, or zero when the span does not exist.
    */
    [[nodiscard]] double spanQuarterLength(std::size_t span) const noexcept;

    /*!
    \brief Finds the span an anchor standing exactly at a grid position opens.

    Span \c i starts at anchor \c i, so the answer is that anchor's index. The terminal anchor only
    closes the last span, so a position it holds opens none.

    \param position Grid position the anchor stands at.
    \return Zero-based span index, or empty when no anchor opening a span stands at the position.
    */
    [[nodiscard]] std::optional<std::size_t> spanStartingAt(GridPosition position) const noexcept;

    /*!
    \brief Returns the terminal anchor grown to cover a time.

    The terminal moves to the first downbeat at or after its own position whose time reaches
    \p cover_until_seconds, timed from the last anchor before it at \p seconds_per_quarter and
    rounded to a whole millisecond once, so a long extension carries no accumulated rounding. A
    terminal on a downbeat that already covers the time comes back unchanged: it never moves
    earlier. A terminal left mid-measure (a meter edit's re-addressed map) always moves, so a
    zero time re-seats it on the next downbeat and nothing further.

    \param cover_until_seconds Time the map must reach, such as the end of the song's audio.
    \param seconds_per_quarter Tempo the last span keeps, as seconds per quarter note.
    \return The covering terminal anchor.
    */
    [[nodiscard]] BeatAnchor terminalCovering(
        double cover_until_seconds, double seconds_per_quarter) const;

    /*!
    \brief Finds the beat count for the measure's active time signature.
    \param measure One-based measure to query.
    \return Beats per measure.
    */
    [[nodiscard]] int beatsPerMeasureAt(int measure) const noexcept;

    /*!
    \brief Converts a measure beat to a zero-based global beat index.
    \param measure One-based measure to convert.
    \param beat One-based beat within the measure.
    \return Zero-based beat index counted from measure 1 beat 1.
    */
    [[nodiscard]] std::int64_t globalBeatIndex(int measure, int beat) const noexcept;

    /*!
    \brief Converts a zero-based global beat index back to a measure beat.
    \param global_beat_index Zero-based beat index counted from measure 1 beat 1.
    \return One-based measure and one-based beat.
    */
    [[nodiscard]] std::pair<int, int> beatAtGlobalIndex(
        std::int64_t global_beat_index) const noexcept;

    /*!
    \brief Converts a grid position to its position on the whole-note axis.

    The whole-note axis is the one axis a meter change does not bend: a beat is one
    signature-denominator note, so the same musical duration is a different beat count on either
    side of a change but the same whole-note count throughout. It is also the axis the tick
    lattice is ruled on (\ref g_tick_quantum_note_value), which is what lets a whole-note step
    carry a stored position from one meter into another without leaving the lattice. Exact
    rationals throughout, unlike the metronome's quarter-note axis, which is a double for time.

    \param position Valid grid position.
    \return Whole notes from measure 1 beat 1.
    */
    [[nodiscard]] Fraction wholeNotePositionAt(GridPosition position) const noexcept;

    /*!
    \brief Addresses a place on the whole-note axis as a grid position: the inverse of
    \ref wholeNotePositionAt.

    The one way a whole-note place becomes a `measure:beat+n/d` address, re-sliced at the meter
    governing the place. Places before the song's start clamp to the grid origin; places past the
    last signature change keep extending at its meter.

    \param whole_note_position Whole notes from measure 1 beat 1.
    \return The grid position at that place.
    */
    [[nodiscard]] GridPosition gridPositionAtWholeNote(Fraction whole_note_position) const noexcept;

    /*!
    \brief Converts a grid position to its quarter-note (metronome) position.

    THE one statement that a quarter is a quarter of a whole note: the axis every time query
    interpolates on and the unit an audio engine pinned to quarter beats reads.

    \param position Valid grid position.
    \return Quarter notes from measure 1 beat 1.
    */
    [[nodiscard]] double quarterPositionAt(GridPosition position) const noexcept;

    /*!
    \brief Resolves a grid position to absolute seconds: \ref secondsAtWholeNote at its place.
    \param position Grid position to resolve; its offset is the exact fraction from its beat toward
           the next beat.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAt(GridPosition position) const noexcept;

    /*!
    \brief Resolves a place on the whole-note axis to absolute seconds.

    THE route every chart instant takes to the clock — an onset, a ring's end, a keyframe — so one
    instant reached two ways (a ring ending on the next head, say) resolves to one double. A
    duration is added on the whole-note axis before the time query, never on the beat axis, so an
    instant past a meter change lands where the music does. Places outside the authored anchor
    range clamp to the first or terminal anchor's time.

    \param whole_note_position Whole notes from measure 1 beat 1.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtWholeNote(Fraction whole_note_position) const noexcept;

    /*!
    \brief Resolves an absolute second position to a place on the whole-note axis: the inverse of
    \ref secondsAtWholeNote.

    A double, because a time between two ticks names no exact place; a caller wanting a lattice
    place rounds it (\ref tickPlaceReached). Times outside the authored anchor range clamp to the
    first or terminal anchor's place.

    \param seconds Absolute second position to resolve.
    \return Whole notes from measure 1 beat 1.
    */
    [[nodiscard]] double wholeNotePositionAtSeconds(double seconds) const noexcept;

    /*!
    \brief Resolves a whole beat to absolute seconds; the whole-beat convenience of secondsAt.
    \param measure One-based measure to resolve.
    \param beat One-based beat within the measure.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtBeat(int measure, int beat) const noexcept;

    /*!
    \brief Resolves a fractional global beat position to absolute seconds.

    This is the shared interpolation query behind secondsAt and secondsAtBeat, exposed for hot
    paths such as the timeline grid scan that already address lines on the global beat axis and
    should not pay a measure/beat address round trip per line. Positions outside the authored
    anchor range clamp to the first or last anchor's time.

    \param global_beat_position Zero-based position on the global beat axis counted from measure 1
           beat 1; fractional values address points between whole beats.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtGlobalBeatPosition(double global_beat_position) const noexcept;

    /*!
    \brief Amortized-constant sequential resolver for non-decreasing global beat positions.

    Grid scans resolve thousands of monotonically increasing beat positions per refresh. This
    cursor advances an anchor-span index forward instead of re-running
    secondsAtGlobalBeatPosition's binary search per query, so a whole scan costs one pass over the
    anchors regardless of line count, while returning bit-identical results.

    \note Positions passed to secondsAt must be non-decreasing across calls, and the cursor is
          valid only while the tempo map it was created from is alive and unmodified.
    */
    class ForwardBeatTimeCursor
    {
    public:
        /*!
        \brief Binds the cursor to a tempo map.
        \param tempo_map Map whose anchor spans the cursor walks; must outlive the cursor.
        */
        explicit ForwardBeatTimeCursor(const TempoMap& tempo_map) noexcept;

        /*!
        \brief Resolves a beat position that is at or after every previously resolved position.
        \param global_beat_position Zero-based fractional position on the global beat axis.
        \return Interpolated second position, identical to secondsAtGlobalBeatPosition.
        */
        [[nodiscard]] double secondsAt(double global_beat_position) noexcept;

    private:
        // Owning map; a pointer instead of a reference keeps the cursor copyable.
        const TempoMap* m_tempo_map;

        // Index of the current span's right anchor; starts at the first interpolable pair and
        // only moves forward.
        std::size_t m_right_anchor{1};

        // Index of the signature segment containing the last resolved position, advanced
        // monotonically so the beat-to-quarter conversion stays amortized constant time.
        std::size_t m_segment{0};
    };

    /*!
    \brief Returns the terminal anchor's global beat index.

    Honest because the tempo-map rules hold the terminal on a downbeat with no offset, so its
    position is a whole beat.

    \return Zero-based global beat index for the terminal anchor.
    */
    [[nodiscard]] std::int64_t terminalGlobalBeatIndex() const noexcept;

    /*!
    \brief Compares two tempo maps by their stored fields.
    \param lhs Left-hand tempo map.
    \param rhs Right-hand tempo map.
    \return True when both tempo maps store equal changes and anchors.
    */
    friend bool operator==(const TempoMap& lhs, const TempoMap& rhs);

private:
    // One normalized time-signature reign covering measures [start_measure, next segment's
    // start_measure). Derived from m_time_signatures so beat-address queries binary-search a
    // monotonic table instead of rewalking the signature list on every call.
    struct SignatureSegment
    {
        // First measure governed by this segment; segment starts are non-decreasing and begin at 1.
        int start_measure{1};

        // The meter inside this segment, both terms clamped to at least one.
        TimeSignature meter{};

        // Global beat index of the first beat of the segment's first measure.
        std::int64_t start_beat_index{0};

        // Whole-note position of the segment's first downbeat, exact: the axis stored positions
        // are held to, and the metronome's quarter-note axis below is this one scaled by four.
        Fraction start_whole_note_position{};

        // Absolute second position of the segment's first downbeat, filled after the anchor
        // indices exist because it resolves through anchor interpolation.
        double start_seconds{0.0};

        // Quarter notes per beat, the metronome's scale of this segment; exact for the
        // power-of-two denominators the package format allows.
        [[nodiscard]] double quartersPerBeat() const noexcept
        {
            return quarterPositionOf(Fraction{1, meter.denominator});
        }

        // Quarter-note position of the segment's first downbeat on the metronome timeline.
        [[nodiscard]] double startQuarterPosition() const noexcept
        {
            return quarterPositionOf(start_whole_note_position);
        }
    };

    // Quarter notes in a whole-note place: the one place the factor of four is written.
    [[nodiscard]] static double quarterPositionOf(Fraction whole_note_position) noexcept;

    // Whole notes in a quarter-note position: the inverse of quarterPositionOf, beside it so the
    // factor is stated once in each direction.
    [[nodiscard]] static double wholeNotePositionOf(double quarter_position) noexcept;

    // Inverse-interpolates the anchor span containing a time onto the metronome axis, clamping
    // outside the authored anchors: the step every seconds-to-place query shares.
    [[nodiscard]] double quarterPositionAtSeconds(double seconds) const noexcept;

    // The anchor span holding a time: the last anchor at or before it, held below the terminal so
    // a span always follows. Requires at least two anchors.
    [[nodiscard]] std::size_t spanAtSeconds(double seconds) const noexcept;

    // Interpolates a quarter-note position to seconds through a binary search of the anchors.
    [[nodiscard]] double secondsAtQuarterPosition(double quarter_position) const noexcept;

    // Rebuilds the derived lookup tables; must run whenever the authored vectors change.
    void buildDerivedIndices();

    // Finds the segment governing a measure; the front segment starts at measure 1 so normalized
    // inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForMeasure(int measure) const noexcept;

    // Finds the segment containing a global beat index; the front segment starts at index 0 so
    // normalized inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForBeatIndex(
        std::int64_t global_beat_index) const noexcept;

    // Finds the segment containing a whole-note position; the front segment starts at zero so
    // clamped inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForWholeNotePosition(
        Fraction whole_note_position) const noexcept;

    // Finds the segment containing a quarter-note position; the front segment starts at zero so
    // clamped inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForQuarterPosition(
        double quarter_position) const noexcept;

    // Converts a fractional global beat position to its quarter-note (metronome) position using
    // the active segment's scale; the interpolation axis every time query resolves through.
    [[nodiscard]] double quarterPositionAtBeatPosition(double global_beat_position) const noexcept;

    // Shared interpolation tail behind secondsAtGlobalBeatPosition and ForwardBeatTimeCursor:
    // right_index names the first anchor at or after the quarter position (clamped to at least
    // one so a left neighbor exists); indexes past the terminal anchor clamp to its time.
    [[nodiscard]] double secondsAtAnchorSpan(
        std::size_t right_index, double quarter_position) const noexcept;

    std::vector<TimeSignatureChange> m_time_signatures;
    std::vector<BeatAnchor> m_anchors;

    // Derived signature segments; never empty, rebuilt by buildDerivedIndices on construction.
    std::vector<SignatureSegment> m_segments;

    // Quarter-note position of each anchor, parallel to m_anchors; the interpolation axis, so
    // seconds stay linear in metronome time between anchors regardless of meter changes.
    std::vector<double> m_anchor_quarter_positions;
};

} // namespace rock_hero::common::core
