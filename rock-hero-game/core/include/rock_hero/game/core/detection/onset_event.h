/*!
\file onset_event.h
\brief Detected attack transient in the live input stream (plan 22 detection contract).
*/

#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace rock_hero::game::core
{

/*! \brief Spectral character of a detected onset. */
enum class OnsetCharacter
{
    /*! \brief The onset leads into periodic, pitched signal. */
    Pitched,

    /*! \brief The onset is percussive and unpitched — the class that scores full mutes. */
    Percussive,

    /*! \brief The detector could not classify the onset's spectral character. */
    Unknown
};

/*!
\brief One detected attack transient, timestamped in input-stream sample time.

Onsets are published as soon as the transient is detected and never wait for pitch evidence —
the fast-onset/slow-pitch split is what makes scoring's provisional-hit state machine work.
Timestamps are monotonic positions in the input device stream, never wall-clock time;
correlating them to song time is the scoring consumer's job through the playback clock and the
calibration offsets.
*/
struct OnsetEvent
{
    /*! \brief Monotonic position of the transient in the input device stream, in samples. */
    std::uint64_t input_stream_sample{0};

    /*! \brief Input-stream sample rate in Hz, making the timestamp self-describing on replay. */
    double sample_rate_hz{0.0};

    /*! \brief Normalized onset strength in [0, 1]. */
    float strength{0.0F};

    /*! \brief Spectral character classification of the transient. */
    OnsetCharacter character{OnsetCharacter::Unknown};

    /*!
    \brief Compares two onset events by their stored fields.
    \param lhs Left-hand onset event.
    \param rhs Right-hand onset event.
    \return True when both events store equal values.
    */
    friend constexpr bool operator==(const OnsetEvent& lhs, const OnsetEvent& rhs) noexcept
    {
        // Hand-written, not defaulted: a defaulted comparison trips clang's -Wfloat-equal on the
        // floating members. Exact equality is intended; the ordering query expresses it warning-
        // free with identical semantics (NaN compares unequal either way).
        return lhs.input_stream_sample == rhs.input_stream_sample &&
               std::is_eq(lhs.sample_rate_hz <=> rhs.sample_rate_hz) &&
               std::is_eq(lhs.strength <=> rhs.strength) && lhs.character == rhs.character;
    }
};

// Detection events cross lock-free queues and serialize into plan-23 replay logs, so they must
// stay plain trivially-copyable values with no handles or owning members.
static_assert(std::is_trivially_copyable_v<OnsetEvent>);

} // namespace rock_hero::game::core
