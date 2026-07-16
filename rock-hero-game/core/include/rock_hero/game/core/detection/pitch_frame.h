/*!
\file pitch_frame.h
\brief Periodic per-hop pitch estimate from the live input stream (plan 22 detection contract).
*/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*!
\brief One analysis hop's fundamental-frequency estimate.

Frames are the stream form of pitch evidence: the tuner and sustain/bend tracking consume the
continuous frame stream, while scoring waits for the onset-associated PitchConfirmation instead.
A frame with zero confidence means the hop was analyzed but no periodic pitch was found; hops
are never skipped, so frame timestamps advance by the analysis hop size.
*/
struct PitchFrame
{
    /*! \brief Monotonic position of the analyzed hop in the input device stream, in samples. */
    std::uint64_t input_stream_sample{0};

    /*! \brief Input-stream sample rate in Hz, making the timestamp self-describing on replay. */
    double sample_rate_hz{0.0};

    /*! \brief Estimated fundamental frequency in Hz; meaningful only when confidence is nonzero. */
    double f0_hz{0.0};

    /*! \brief Estimator confidence in [0, 1]; zero means no periodic pitch was found this hop. */
    float confidence{0.0F};

    /*! \brief Periodicity / clarity of the analyzed signal in [0, 1]. */
    float clarity{0.0F};

    /*! \brief Linear RMS level of the analyzed hop in [0, 1]. */
    float rms{0.0F};

    /*!
    \brief Compares two pitch frames by their stored fields.
    \param lhs Left-hand pitch frame.
    \param rhs Right-hand pitch frame.
    \return True when both frames store equal values.
    */
    friend constexpr bool operator==(const PitchFrame& lhs, const PitchFrame& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.input_stream_sample == rhs.input_stream_sample &&
               std::is_eq(lhs.sample_rate_hz <=> rhs.sample_rate_hz) &&
               std::is_eq(lhs.f0_hz <=> rhs.f0_hz) &&
               std::is_eq(lhs.confidence <=> rhs.confidence) &&
               std::is_eq(lhs.clarity <=> rhs.clarity) && std::is_eq(lhs.rms <=> rhs.rms);
    }
};

// Detection events cross lock-free queues and serialize into plan-23 replay logs, so they must
// stay plain trivially-copyable values with no handles or owning members.
static_assert(std::is_trivially_copyable_v<PitchFrame>);

} // namespace rock_hero::game::core
