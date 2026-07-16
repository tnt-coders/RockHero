/*!
\file polyphonic_salience.h
\brief Onset-associated polyphonic pitch evidence (plan 22 detection contract).
*/

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*! \brief Maximum salience candidates one event carries; six strings plus octave/harmonic spurs. */
inline constexpr int g_max_salience_candidates = 8;

/*! \brief One salient fundamental-frequency candidate. */
struct SaliencePeak
{
    /*! \brief Candidate fundamental frequency in Hz. */
    double f0_hz{0.0};

    /*! \brief Normalized salience weight in [0, 1]. */
    float salience{0.0F};

    /*!
    \brief Compares two salience peaks by their stored fields.
    \param lhs Left-hand peak.
    \param rhs Right-hand peak.
    \return True when both peaks store equal values.
    */
    friend constexpr bool operator==(const SaliencePeak& lhs, const SaliencePeak& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return std::is_eq(lhs.f0_hz <=> rhs.f0_hz) && std::is_eq(lhs.salience <=> rhs.salience);
    }
};

/*!
\brief The chart-blind multi-pitch snapshot published once per onset.

This is the chord-evidence carrier: scoring checks charted member pitches against the candidate
set without detection ever knowing the chart, so replay logs stay re-scoreable against edited
charts. Candidates are detector-ranked; octave and harmonic spurs are expected entries, which is
why consumers match octave-insensitively. Unused slots stay value-initialized so serialized
events compare deterministically.
*/
struct PolyphonicSalience
{
    /*!
    \brief Stream position where the salience analysis completed, in samples.

    This is the last sample the analysis consumed (its causal availability point) and the
    event's stream-ordering key.
    */
    std::uint64_t input_stream_sample{0};

    /*! \brief Input-stream sample rate in Hz, making the timestamp self-describing on replay. */
    double sample_rate_hz{0.0};

    /*! \brief Stream position of the onset this snapshot describes, in samples. */
    std::uint64_t onset_stream_sample{0};

    /*! \brief Number of populated entries at the front of candidates. */
    int candidate_count{0};

    /*! \brief Detector-ranked salience candidates; entries past candidate_count stay zeroed. */
    std::array<SaliencePeak, g_max_salience_candidates> candidates{};

    /*!
    \brief Compares two salience snapshots by their stored fields.
    \param lhs Left-hand snapshot.
    \param rhs Right-hand snapshot.
    \return True when both snapshots store equal values.
    */
    friend constexpr bool operator==(
        const PolyphonicSalience& lhs, const PolyphonicSalience& rhs) noexcept
    {
        // The full array participates (not just candidate_count entries): unused slots are
        // contractually zeroed, so comparing them enforces the determinism the replay logs need.
        return lhs.input_stream_sample == rhs.input_stream_sample &&
               std::is_eq(lhs.sample_rate_hz <=> rhs.sample_rate_hz) &&
               lhs.onset_stream_sample == rhs.onset_stream_sample &&
               lhs.candidate_count == rhs.candidate_count && lhs.candidates == rhs.candidates;
    }
};

// Detection events cross lock-free queues and serialize into plan-23 replay logs, so they must
// stay plain trivially-copyable values with no handles or owning members.
static_assert(std::is_trivially_copyable_v<PolyphonicSalience>);

} // namespace rock_hero::game::core
