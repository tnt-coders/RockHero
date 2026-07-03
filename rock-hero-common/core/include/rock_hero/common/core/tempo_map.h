/*!
\file tempo_map.h
\brief Song-level beat grid with time-signature changes and sparse timing anchors.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <rock_hero/common/core/fraction.h>
#include <rock_hero/common/core/timeline.h>
#include <utility>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief Time signature that starts at a measure and carries forward. */
struct TimeSignatureChange
{
    /*! \brief One-based measure where this time signature begins. */
    int measure{1};

    /*! \brief Beats per measure. */
    int numerator{4};

    /*! \brief Note value that represents one beat. */
    int denominator{4};

    /*!
    \brief Compares two time-signature changes by their stored fields.
    \param lhs Left-hand time-signature change.
    \param rhs Right-hand time-signature change.
    \return True when both changes store equal values.
    */
    friend bool operator==(const TimeSignatureChange& lhs, const TimeSignatureChange& rhs) =
        default;
};

/*! \brief Absolute timing pin for one addressed measure beat. */
struct BeatAnchor
{
    /*! \brief One-based measure addressed by this anchor. */
    int measure{1};

    /*! \brief One-based beat within the anchor measure. */
    int beat{1};

    /*! \brief Absolute second position of the addressed beat. */
    double seconds{0.0};

    /*!
    \brief Compares two beat anchors by their stored fields.
    \param lhs Left-hand beat anchor.
    \param rhs Right-hand beat anchor.
    \return True when both anchors store equal values.
    */
    friend constexpr bool operator==(const BeatAnchor& lhs, const BeatAnchor& rhs) noexcept
    {
        return lhs.measure == rhs.measure && lhs.beat == rhs.beat &&
               std::is_eq(lhs.seconds <=> rhs.seconds);
    }
};

/*!
\brief Song-level warp-anchor beat grid.

TempoMap stores the durable native chart timing model: time-signature changes define the musical
grid, while anchors pin sparse measure beats to absolute seconds. All intermediate beat and note
positions resolve by linear interpolation between neighboring anchors. Construction precomputes
signature-segment and anchor beat indices so address and time queries stay logarithmic in the
authored list sizes instead of rescanning them; per-frame consumers such as the timeline grid scan
rely on that bound.
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
    \brief Resolves an addressed beat to absolute seconds.
    \param measure One-based measure to resolve.
    \param beat One-based beat within the measure.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtBeat(int measure, int beat) const noexcept;

    /*!
    \brief Resolves a fractional note position to absolute seconds.
    \param measure One-based measure to resolve.
    \param beat One-based beat within the measure.
    \param offset Exact fraction from this beat toward the next beat.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtNote(int measure, int beat, Fraction offset) const noexcept;

    /*!
    \brief Resolves a fractional global beat position to absolute seconds.

    This is the shared interpolation query behind secondsAtBeat and secondsAtNote, exposed for hot
    paths such as the timeline grid scan that already address lines on the global beat axis and
    should not pay a measure/beat address round trip per line. Positions outside the authored
    anchor range clamp to the first or last anchor's time.

    \param global_beat_position Zero-based position on the global beat axis counted from measure 1
           beat 1; fractional values address points between whole beats.
    \return Interpolated second position.
    */
    [[nodiscard]] double secondsAtGlobalBeatPosition(double global_beat_position) const noexcept;

    /*!
    \brief Returns the terminal anchor's global beat index.
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

        // Beats per measure inside this segment, clamped to at least one.
        int beats_per_measure{4};

        // Global beat index of the first beat of the segment's first measure.
        std::int64_t start_beat_index{0};
    };

    // Rebuilds the derived lookup tables; must run whenever the authored vectors change.
    void buildDerivedIndices();

    // Finds the segment governing a measure; the front segment starts at measure 1 so normalized
    // inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForMeasure(int measure) const noexcept;

    // Finds the segment containing a global beat index; the front segment starts at index 0 so
    // normalized inputs always match.
    [[nodiscard]] const SignatureSegment& segmentForBeatIndex(
        std::int64_t global_beat_index) const noexcept;

    std::vector<TimeSignatureChange> m_time_signatures;
    std::vector<BeatAnchor> m_anchors;

    // Derived signature segments; never empty, rebuilt by buildDerivedIndices on construction.
    std::vector<SignatureSegment> m_segments;

    // Global beat index of each anchor, parallel to m_anchors; non-decreasing for valid maps.
    std::vector<std::int64_t> m_anchor_beat_indices;
};

} // namespace rock_hero::common::core
