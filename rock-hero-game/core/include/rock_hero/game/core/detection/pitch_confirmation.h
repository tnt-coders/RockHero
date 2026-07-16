/*!
\file pitch_confirmation.h
\brief Onset-associated confirmed pitch from the live input stream (plan 22 detection contract).
*/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*!
\brief A confirmed pitch decision for one previously published onset.

This is the event scoring's provisional-hit state machine waits for: the onset registered a
provisional hit immediately, and this confirmation (or its absence before the per-register
deadline) resolves it. Confirmation arrives after the estimator has seen enough fundamental
periods, so on low strings it lands tens of milliseconds after its onset — the latency budget in
plan 22's contract bounds that gap per register.
*/
struct PitchConfirmation
{
    /*!
    \brief Monotonic stream position where confirmation was reached, in samples.

    This is the last frame of the confirming span, so it is also the span's end and the event's
    stream-ordering key.
    */
    std::uint64_t input_stream_sample{0};

    /*! \brief Input-stream sample rate in Hz, making the timestamps self-describing on replay. */
    double sample_rate_hz{0.0};

    /*! \brief Stream position of the onset this confirmation resolves, in samples. */
    std::uint64_t onset_stream_sample{0};

    /*! \brief Stream position of the first frame in the confirming span, in samples. */
    std::uint64_t span_begin_sample{0};

    /*! \brief Confirmed fundamental frequency in Hz. */
    double f0_hz{0.0};

    /*! \brief Confidence of the confirmed pitch in [0, 1]. */
    float confidence{0.0F};

    /*!
    \brief Compares two pitch confirmations by their stored fields.
    \param lhs Left-hand confirmation.
    \param rhs Right-hand confirmation.
    \return True when both confirmations store equal values.
    */
    friend constexpr bool operator==(
        const PitchConfirmation& lhs, const PitchConfirmation& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.input_stream_sample == rhs.input_stream_sample &&
               std::is_eq(lhs.sample_rate_hz <=> rhs.sample_rate_hz) &&
               lhs.onset_stream_sample == rhs.onset_stream_sample &&
               lhs.span_begin_sample == rhs.span_begin_sample &&
               std::is_eq(lhs.f0_hz <=> rhs.f0_hz) && std::is_eq(lhs.confidence <=> rhs.confidence);
    }
};

// Detection events cross lock-free queues and serialize into plan-23 replay logs, so they must
// stay plain trivially-copyable values with no handles or owning members.
static_assert(std::is_trivially_copyable_v<PitchConfirmation>);

} // namespace rock_hero::game::core
